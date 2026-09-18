<?php
error_reporting(E_ALL);
ini_set('display_errors', 1);

header('Content-Type: application/json');
require_once '../config/config.php';
require_once '../vendor/autoload.php'; // Composer autoload for Dompdf and PHPMailer

use Dompdf\Dompdf;
use Dompdf\Options;
use PHPMailer\PHPMailer\PHPMailer;
use PHPMailer\PHPMailer\Exception;

// Validate CSRF token
if (!isset($_SERVER['HTTP_X_CSRF_TOKEN']) || $_SERVER['HTTP_X_CSRF_TOKEN'] !== $_SESSION['csrf_token']) {
    sendErrorEmail('Invalid CSRF token detected', $_SESSION['user_email'] ?? 'admin@bthdevelopers.com');
    echo json_encode(['success' => false, 'message' => 'Invalid CSRF token']);
    exit;
}

// Check if user is authenticated
if (!isset($_SESSION['user_id']) || !isset($_SESSION['user_type'])) {
    sendErrorEmail('Unauthorized access attempt', $_SESSION['user_email'] ?? 'admin@bthdevelopers.com');
    echo json_encode(['success' => false, 'message' => 'Unauthorized']);
    exit;
}

// Function to send emails with PHPMailer
function sendEmail($to, $subject, $body, $attachment = null) {
    $mail = new PHPMailer(true);
    try {
        $mail->isSMTP();
        $mail->Host = SMTP_HOST;
        $mail->SMTPAuth = true;
        $mail->Username = SMTP_USERNAME;
        $mail->Password = SMTP_PASSWORD;
        $mail->SMTPSecure = SMTP_SECURE; // Use 'tls' from config.php
        $mail->Port = SMTP_PORT;

        $mail->setFrom(EMAIL_FROM, EMAIL_FROM_NAME); // Use EMAIL_FROM and EMAIL_FROM_NAME
        $mail->addAddress($to);

        $mail->isHTML(true);
        $mail->Subject = $subject;
        $mail->Body = $body;
        $mail->AltBody = strip_tags($body);

        if ($attachment && file_exists($attachment)) {
            $mail->addAttachment($attachment);
        }

        $mail->send();
        return true;
    } catch (Exception $e) {
        error_log("Email sending failed: {$mail->ErrorInfo}", 3, __DIR__ . '/../logs/error.log');
        return false;
    }
}

// Function to send error notification emails
function sendErrorEmail($errorMessage, $to) {
    $subject = 'CRM Error Notification';
    $body = "<h2>CRM System Error</h2><p>An error occurred in the CRM system:</p><p><strong>Error:</strong> $errorMessage</p><p>Please review the logs for more details.</p>";
    sendEmail($to, $subject, $body);
}

// Function to ensure agreement_letter folder exists
function ensureAgreementFolder() {
    $folder = __DIR__ . '/agreement_letter';
    if (!is_dir($folder)) {
        if (!mkdir($folder, 0777, true)) {
            error_log("Failed to create agreement_letter folder: $folder", 3, __DIR__ . '/../logs/error.log');
            sendErrorEmail('Failed to create agreement_letter folder', $_SESSION['user_email'] ?? 'admin@bthdevelopers.com');
            return false;
        }
        chmod($folder, 0777); // Ensure writable permissions
    }
    return $folder;
}

// Function to sanitize filename
function sanitizeFilename($string) {
    $string = preg_replace('/[^a-zA-Z0-9_-]/', '_', $string);
    return trim($string, '_');
}

// Function to generate a unique token
function generateUniqueToken() {
    return bin2hex(random_bytes(16)); // Generates a 32-character random string
}

// Function to generate PDF agreement letter using Dompdf
function generateAgreementPDF($client) {
    $folder = ensureAgreementFolder();
    if (!$folder) {
        return false;
    }

    $htmlContent = generateHtmlContent($client);
    $sanitizedContactPerson = sanitizeFilename($client['contact_person'] ?? 'Unknown');
    $pdfFile = $folder . '/agreement_' . $sanitizedContactPerson . '_' . $client['id'] . '_' . time() . '.pdf';

    try {
        $options = new Options();
        $options->set('isHtml5ParserEnabled', true);
        $options->set('isRemoteEnabled', false);
        $options->set('defaultFont', 'DejaVu Sans');

        $dompdf = new Dompdf($options);
        $dompdf->loadHtml($htmlContent);
        $dompdf->setPaper('A4', 'portrait');
        $dompdf->render();

        $output = $dompdf->output();
        file_put_contents($pdfFile, $output);

        if (file_exists($pdfFile)) {
            return $pdfFile;
        } else {
            error_log("PDF file not created: $pdfFile", 3, __DIR__ . '/../logs/error.log');
            sendErrorEmail('PDF file not generated for client ID: ' . $client['id'], $_SESSION['user_email'] ?? 'admin@bthdevelopers.com');
            return false;
        }
    } catch (Exception $e) {
        error_log("Dompdf error: " . $e->getMessage(), 3, __DIR__ . '/../logs/error.log');
        sendErrorEmail('Failed to generate PDF for client ID: ' . $client['id'] . ' - ' . $e->getMessage(), $_SESSION['user_email'] ?? 'admin@bthdevelopers.com');
        return false;
    }
}

// Function to generate HTML content for the agreement letter
function generateHtmlContent($client) {
    $companyName = htmlspecialchars($client['company_name'] ?? 'N/A');
    $contactPerson = htmlspecialchars($client['contact_person'] ?? 'N/A');
    $email = htmlspecialchars($client['email'] ?? 'N/A');
    $phone = htmlspecialchars($client['phone'] ?? 'N/A');
    $website = htmlspecialchars($client['website'] ?? 'N/A');
    $address = htmlspecialchars($client['address'] ?? 'N/A');
    $industry = htmlspecialchars($client['industry'] ?? 'N/A');
    $status = htmlspecialchars($client['status'] ?? 'N/A');
    $contractValue = isset($client['contract_value']) ? number_format(floatval($client['contract_value']), 2) : 'N/A';
    $notes = htmlspecialchars($client['notes'] ?? 'N/A');
    $createdAt = isset($client['created_at']) ? date('F d, Y', strtotime($client['created_at'])) : 'N/A';
    $updatedAt = isset($client['updated_at']) ? date('F d, Y', strtotime($client['updated_at'])) : 'N/A';
    $today = date('Y-m-d');

    return <<<HTML
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Client Agreement Letter</title>
    <style>
        body { font-family: 'DejaVu Sans', Arial, sans-serif; margin: 15px; padding: 0; line-height: 1.5; }
        .container { max-width: 800px; margin: 0 auto; }
        .header { text-align: center; border-bottom: 2px solid #333; padding-bottom: 20px; }
        .logo { font-size: 36px; font-weight: bold; color: #2c6da0; }
        .company-info { font-size: 14px; color: #555; }
        .section-title { font-size: 20px; font-weight: bold; margin-top: 30px; color: #333; }
        .client-info { margin-bottom: 20px; }
        .client-info-item { margin-bottom: 10px; }
        .terms-list { margin-bottom: 20px; }
        .legal-terms { margin-top: 30px; font-size: 14px; color: #333; border-top: 1px solid #ccc; padding-top: 15px; }
        .footer { text-align: center; font-size: 12px; color: #555; margin-top: 40px; }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1><span class="logo">Bth Developers</span></h1>
            <h2>Client Agreement Letter</h2>
            <div class="company-info">
                <p>Ground Floor, Room No. 2, H. No. 29, Jiachha Tola, Chuhari, Chanpatia, Pashchim Champaran, Bihar - 845450</p>
                <p>Email: info@bthdevelopers.com | Phone: +919608083342</p>
                <p>Website: <a href="https://www.bthdevelopers.com/">https://www.bthdevelopers.com/</a></p>
                <p>GSTIN: 10JJBPK7226M1Z4</p>
                <p>Date: $today</p>
            </div>
        </div>

        <h3 class="section-title">Client Information</h3>
        <div class="client-info">
            <p class="client-info-item"><strong>Company Name:</strong> $companyName</p>
            <p class="client-info-item"><strong>Contact Person:</strong> $contactPerson</p>
            <p class="client-info-item"><strong>Email:</strong> $email</p>
            <p class="client-info-item"><strong>Phone:</strong> $phone</p>
            <p class="client-info-item"><strong>Website:</strong> $website</p>
            <p class="client-info-item"><strong>Address:</strong> $address</p>
            <p class="client-info-item"><strong>Textile:</strong> $industry</p>
            <p class="client-info-item"><strong>Status:</strong> $status</p>
            <p class="client-info-item"><strong>Contract Value:</strong> ₹$contractValue</p>
            <p class="client-info-item"><strong>Notes:</strong> $notes</p>
            <p class="client-info-item"><strong>Created At:</strong> $createdAt</p>
            <p class="client-info-item"><strong>Updated At:</strong> $updatedAt</p>
        </div>

        <h3 class="section-title">Agreement Terms</h3>
        <p>This Agreement is made between <strong>Bth Developers</strong> (hereinafter referred to as "the Company") and <strong>$companyName</strong> (hereinafter referred to as "the Client") on $today. The parties agree as follows:</p>
        <ol class="terms-list">
            <li><strong>Scope of Services:</strong> The Company will provide services as agreed upon with the Client, tailored to the Client's industry needs ($industry).</li>
            <li><strong>Payment Terms:</strong> The Client agrees to pay ₹$contractValue as per the agreed schedule. All payments are due within 30 days of invoice issuance.</li>
            <li><strong>Confidentiality:</strong> Both parties agree to maintain the confidentiality of all proprietary information shared during the engagement.</li>
            <li><strong>Termination:</strong> Either party may terminate this Agreement with 30 days' written notice. Any outstanding payments remain due upon termination.</li>
            <li><strong>Contact:</strong> The primary contact for the Client is $contactPerson ($email, $phone).</li>
        </ol>

        <h3 class="section-title">Legal Terms and Acceptance</h3>
        <div class="legal-terms">
            <p>By receiving this email, you acknowledge and accept the terms and conditions outlined in this agreement. The deal may be finalized via call, email, or physical meetings. This agreement is generated digitally, and no physical or digital signature is required for its validity, in accordance with applicable laws including the Indian Contract Act, 1872, and the Information Technology Act, 2000.</p>
        </div>

        <div class="footer">
            <p>© 2025 Bth Developers. All rights reserved.</p>
        </div>
    </div>
</body>
</html>
HTML;
}

try {
    $pdo = getDBConnection();
    $method = $_SERVER['REQUEST_METHOD'];

    if ($method === 'GET') {
        if (isset($_GET['export']) && $_GET['export'] === 'true') {
            $stmt = $pdo->prepare("SELECT * FROM clients ORDER BY created_at DESC");
            $stmt->execute();
            $clients = $stmt->fetchAll(PDO::FETCH_ASSOC);
            echo json_encode(['success' => true, 'data' => $clients]);
        } elseif (isset($_GET['generate_pdf']) && $_GET['generate_pdf'] === 'true') {
            if (!isset($_GET['id']) || empty(trim($_GET['id']))) {
                error_log("GET: Client ID missing for PDF generation", 3, __DIR__ . '/../logs/error.log');
                sendErrorEmail('Client ID missing for PDF generation', $_SESSION['user_email'] ?? 'admin@bthdevelopers.com');
                echo json_encode(['success' => false, 'message' => 'Client ID is required']);
                exit;
            }

            $client_id = intval($_GET['id']);
            $stmt = $pdo->prepare("SELECT * FROM clients WHERE id = ?");
            $stmt->execute([$client_id]);
            $client = $stmt->fetch(PDO::FETCH_ASSOC);

            if ($client) {
                if (!$client['agreement_accepted']) {
                    echo json_encode(['success' => false, 'message' => 'Agreement not yet accepted by the client']);
                    exit;
                }

                $pdfFile = generateAgreementPDF($client);
                if ($pdfFile) {
                    $subject = "Client Agreement Letter - {$client['company_name']}";
                    $body = "<h2>Dear {$client['contact_person']},</h2><p>Please find attached the agreement letter from Bth Developers for {$client['company_name']}. By receiving this email, you accept the terms outlined in the agreement. Review the terms and let us know if you have any questions.</p><p>Best regards,<br>Bth Developers Team</p>";
                    if (sendEmail($client['email'], $subject, $body, $pdfFile)) {
                        echo json_encode(['success' => true, 'message' => 'Agreement letter sent successfully']);
                    } else {
                        sendErrorEmail('Failed to send agreement letter email for client ID: ' . $client_id, $_SESSION['user_email'] ?? 'admin@bthdevelopers.com');
                        echo json_encode(['success' => false, 'message' => 'Failed to send agreement letter']);
                    }
                } else {
                    echo json_encode(['success' => false, 'message' => 'Failed to generate PDF']);
                }
            } else {
                sendErrorEmail('Client not found for PDF generation: ID ' . $client_id, $_SESSION['user_email'] ?? 'admin@bthdevelopers.com');
                echo json_encode(['success' => false, 'message' => 'Client not found']);
            }
        } else {
            if (!isset($_GET['id']) || empty(trim($_GET['id']))) {
                error_log("GET: Client ID missing in request", 3, __DIR__ . '/../logs/error.log');
                sendErrorEmail('Client ID missing in GET request', $_SESSION['user_email'] ?? 'admin@bthdevelopers.com');
                echo json_encode(['success' => false, 'message' => 'Client ID is required']);
                exit;
            }

            $client_id = intval($_GET['id']);
            $stmt = $pdo->prepare("SELECT * FROM clients WHERE id = ?");
            $stmt->execute([$client_id]);
            $client = $stmt->fetch(PDO::FETCH_ASSOC);

            if ($client) {
                echo json_encode(['success' => true, 'data' => $client]);
            } else {
                sendErrorEmail('Client not found: ID ' . $client_id, $_SESSION['user_email'] ?? 'admin@bthdevelopers.com');
                echo json_encode(['success' => false, 'message' => 'Client not found']);
            }
        }
    } elseif ($method === 'POST') {
        $data = [];
        foreach ($_POST as $key => $value) {
            $data[$key] = $value;
        }

        if (empty($data['company_name']) || empty($data['contact_person']) || empty($data['email'])) {
            sendErrorEmail('Missing required fields for client creation', $_SESSION['user_email'] ?? 'admin@bthdevelopers.com');
            echo json_encode(['success' => false, 'message' => 'Company name, contact person, and email are required']);
            exit;
        }

        $token = generateUniqueToken();

        $stmt = $pdo->prepare("
            INSERT INTO clients (
                company_name, industry, contact_person, email, phone, website, address, 
                contract_value, status, notes, last_contact, last_contact_method, created_at, updated_at, acceptance_token
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, NOW(), NOW(), ?)
        ");
        $stmt->execute([
            $data['company_name'],
            $data['industry'] ?? null,
            $data['contact_person'],
            $data['email'],
            $data['phone'] ?? null,
            $data['website'] ?? null,
            $data['address'] ?? null,
            isset($data['contract_value']) ? floatval($data['contract_value']) : null,
            $data['status'] ?? 'pending',
            $data['notes'] ?? null,
            $data['last_contact'] ?? null,
            $data['last_contact_method'] ?? null,
            $token
        ]);

        if ($stmt->rowCount() > 0) {
            $client_id = $pdo->lastInsertId();
            $stmt = $pdo->prepare("SELECT * FROM clients WHERE id = ?");
            $stmt->execute([$client_id]);
            $client = $stmt->fetch(PDO::FETCH_ASSOC);

            $acceptanceLink = "https://bthdevelopers.com/api/accept_agreement.php?client_id=$client_id&token=$token";
            $subject = "Action Required: Accept Your Agreement with Bth Developers";
            $body = "<h2>Dear {$client['contact_person']},</h2>
                     <p>Welcome to Bth Developers! We have prepared an agreement for {$client['company_name']}.</p>
                     <p>Please review and accept the agreement by clicking the link below:</p>
                     <p><a href=\"$acceptanceLink\">Accept Agreement</a></p>
                     <p>Upon acceptance, you will receive the agreement letter as a PDF attachment.</p>
                     <p>If you have any questions, feel free to contact us at info@bthdevelopers.com or +919608083342.</p>
                     <p>Best regards,<br>Bth Developers Team</p>";

            if (sendEmail($client['email'], $subject, $body)) {
                echo json_encode(['success' => true, 'message' => 'Client added and acceptance email sent successfully']);
            } else {
                sendErrorEmail('Failed to send acceptance email for new client ID: ' . $client_id, $_SESSION['user_email'] ?? 'admin@bthdevelopers.com');
                echo json_encode(['success' => true, 'message' => 'Client added but failed to send acceptance email']);
            }
        } else {
            sendErrorEmail('Failed to add new client', $_SESSION['user_email'] ?? 'admin@bthdevelopers.com');
            echo json_encode(['success' => false, 'message' => 'Failed to add client']);
        }
    } elseif ($method === 'PUT') {
        if (!isset($_GET['id']) || empty(trim($_GET['id']))) {
            error_log("PUT: Client ID missing in query parameter", 3, __DIR__ . '/../logs/error.log');
            sendErrorEmail('Client ID missing for client update', $_SESSION['user_email'] ?? 'admin@bthdevelopers.com');
            echo json_encode(['success' => false, 'message' => 'Client ID is required']);
            exit;
        }

        $client_id = intval($_GET['id']);
        parse_str(file_get_contents("php://input"), $data);

        error_log("PUT: Received data: " . print_r($data, true), 3, __DIR__ . '/../logs/error.log');

        if (empty($data['company_name']) || empty($data['contact_person']) || empty($data['email'])) {
            sendErrorEmail('Missing required fields for client update: ID ' . $client_id, $_SESSION['user_email'] ?? 'info@bthdevelopers.com');
            echo json_encode(['success' => false, 'message' => 'Company name, contact person, and email are required']);
            exit;
        }

        $stmt = $pdo->prepare("
            UPDATE clients SET
                company_name = ?, industry = ?, contact_person = ?, email = ?, phone = ?, 
                website = ?, address = ?, contract_value = ?, status = ?, notes = ?, 
                last_contact = ?, last_contact_method = ?, updated_at = NOW()
            WHERE id = ?
        ");
        $stmt->execute([
            $data['company_name'],
            $data['industry'] ?? null,
            $data['contact_person'],
            $data['email'],
            $data['phone'] ?? null,
            $data['website'] ?? null,
            $data['address'] ?? null,
            isset($data['contract_value']) ? floatval($data['contract_value']) : null,
            $data['status'] ?? 'pending',
            $data['notes'] ?? null,
            $data['last_contact'] ?? null,
            $data['last_contact_method'] ?? null,
            $client_id
        ]);

        if ($stmt->rowCount() > 0) {
            $stmt = $pdo->prepare("SELECT * FROM clients WHERE id = ?");
            $stmt->execute([$client_id]);
            $client = $stmt->fetch(PDO::FETCH_ASSOC);

            if ($client['agreement_accepted']) {
                $pdfFile = generateAgreementPDF($client);
                if ($pdfFile) {
                    $subject = "Updated Agreement Letter - {$client['company_name']}";
                    $body = "<h2>Dear {$client['contact_person']},</h2><p>Please find attached the updated agreement letter for {$client['company_name']} from Bth Developers. By receiving this email, you accept the terms outlined in the agreement. Review the updated terms and contact us with any questions.</p><p>Best regards,<br>Bth Developers Team</p>";
                    if (sendEmail($client['email'], $subject, $body, $pdfFile)) {
                        echo json_encode(['success' => true, 'message' => 'Client updated and agreement letter sent successfully']);
                    } else {
                        sendErrorEmail('Failed to send updated agreement letter email for client ID: ' . $client_id, $_SESSION['user_email'] ?? 'admin@bthdevelopers.com');
                        echo json_encode(['success' => true, 'message' => 'Client updated but failed to send agreement letter']);
                    }
                } else {
                    sendErrorEmail('Failed to generate updated PDF for client ID: ' . $client_id, $_SESSION['user_email'] ?? 'admin@bthdevelopers.com');
                    echo json_encode(['success' => true, 'message' => 'Client updated but failed to generate PDF']);
                }
            } else {
                $token = generateUniqueToken();
                $stmt = $pdo->prepare("UPDATE clients SET acceptance_token = ? WHERE id = ?");
                $stmt->execute([$token, $client_id]);

                $acceptanceLink = "https://bthdevelopers.com/api/accept_agreement.php?client_id=$client_id&token=$token";
                $subject = "Action Required: Accept Your Updated Agreement with Bth Developers";
                $body = "<h2>Dear {$client['contact_person']},</h2>
                         <p>We have updated the agreement for {$client['company_name']} with Bth Developers.</p>
                         <p>Please review and accept the updated agreement by clicking the link below:</p>
                         <p><a href=\"$acceptanceLink\">Accept Agreement</a></p>
                         <p>Upon acceptance, you will receive the updated agreement letter as a PDF attachment.</p>
                         <p>If you have any questions, feel free to contact us at info@bthdevelopers.com or +919608083342.</p>
                         <p>Best regards,<br>Bth Developers Team</p>";

                if (sendEmail($client['email'], $subject, $body)) {
                    echo json_encode(['success' => true, 'message' => 'Client updated and acceptance email sent successfully']);
                } else {
                    sendErrorEmail('Failed to send acceptance email for updated client ID: ' . $client_id, $_SESSION['user_email'] ?? 'admin@bthdevelopers.com');
                    echo json_encode(['success' => true, 'message' => 'Client updated but failed to send acceptance email']);
                }
            }
        } else {
            sendErrorEmail('No changes made or client not found for update: ID ' . $client_id, $_SESSION['user_email'] ?? 'admin@bthdevelopers.com');
            echo json_encode(['success' => false, 'message' => 'No changes made or client not found']);
        }
    } elseif ($method === 'DELETE') {
        if (!isset($_GET['id']) || empty(trim($_GET['id']))) {
            error_log("DELETE: Client ID missing in request", 3, __DIR__ . '/../logs/error.log');
            sendErrorEmail('Client ID missing for client deletion', $_SESSION['user_email'] ?? 'admin@bthdevelopers.com');
            echo json_encode(['success' => false, 'message' => 'Client ID is required']);
            exit;
        }

        $client_id = intval($_GET['id']);
        $stmt = $pdo->prepare("SELECT company_name, contact_person, email FROM clients WHERE id = ?");
        $stmt->execute([$client_id]);
        $client = $stmt->fetch(PDO::FETCH_ASSOC);

        if ($client) {
            $stmt = $pdo->prepare("DELETE FROM clients WHERE id = ?");
            $stmt->execute([$client_id]);

            if ($stmt->rowCount() > 0) {
                $subject = "Bth Developers - Client Account Deletion";
                $body = "<h2>Dear {$client['contact_person']},</h2><p>Your account with Bth Developers for {$client['company_name']} has been deleted from our CRM system. If this was not intended, please contact us immediately.</p><p>Best regards,<br>Bth Developers Team</p>";
                sendEmail($client['email'], $subject, $body);
                echo json_encode(['success' => true, 'message' => 'Client deleted successfully']);
            } else {
                sendErrorEmail('Failed to delete client: ID ' . $client_id, $_SESSION['user_email'] ?? 'admin@bthdevelopers.com');
                echo json_encode(['success' => false, 'message' => 'Client not found']);
            }
        } else {
            sendErrorEmail('Client not found for deletion: ID ' . $client_id, $_SESSION['user_email'] ?? 'admin@bthdevelopers.com');
            echo json_encode(['success' => false, 'message' => 'Client not found']);
        }
    } else {
        sendErrorEmail('Invalid request method received', $_SESSION['user_email'] ?? 'admin@bthdevelopers.com');
        echo json_encode(['success' => false, 'message' => 'Invalid request method']);
    }
} catch (PDOException $e) {
    error_log("API Error: " . $e->getMessage(), 3, __DIR__ . '/../logs/error.log');
    sendErrorEmail('Database error occurred: ' . $e->getMessage(), $_SESSION['user_email'] ?? 'admin@bthdevelopers.com');
    echo json_encode(['success' => false, 'message' => 'Database error occurred']);
}
?>