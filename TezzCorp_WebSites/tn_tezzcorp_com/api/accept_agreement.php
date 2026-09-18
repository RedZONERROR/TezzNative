<?php
// ... (Your existing error reporting, includes, and helper functions) ...
error_reporting(E_ALL);
ini_set('display_errors', 1);

require_once '../config/config.php';
require_once '../vendor/autoload.php'; // Composer autoload for Dompdf and PHPMailer

use Dompdf\Dompdf;
use Dompdf\Options;
use PHPMailer\PHPMailer\PHPMailer;
use PHPMailer\PHPMailer\Exception;

// Function to send emails with PHPMailer - MODIFIED TO INCLUDE CC
function sendEmail($to, $subject, $body, $attachment = null, $cc = null) {
    $mail = new PHPMailer(true);
    try {
        $mail->isSMTP();
        $mail->Host = SMTP_HOST;
        $mail->SMTPAuth = true;
        $mail->Username = SMTP_USERNAME;
        $mail->Password = SMTP_PASSWORD;
        $mail->SMTPSecure = SMTP_SECURE;
        $mail->Port = SMTP_PORT;

        $mail->setFrom(EMAIL_FROM, EMAIL_FROM_NAME);
        $mail->addAddress($to);
        
        if ($cc) { // Added CC functionality
            $mail->addAddress($cc, 'CRM Admin');
        }

        $mail->isHTML(true);
        $mail->Subject = $subject;
        $mail->Body = $body;
        $mail->AltBody = strip_tags($body);

        if ($attachment && file_exists($attachment)) {
            $mail->addAttachment($attachment, 'Agreement_Letter.pdf'); // Added filename
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
            sendErrorEmail('Failed to create agreement_letter folder', 'admin@bthdevelopers.com');
            return false;
        }
        chmod($folder, 0777);
    }
    return $folder;
}

// Function to sanitize filename
function sanitizeFilename($string) {
    $string = preg_replace('/[^a-zA-Z0-9_-]/', '_', $string);
    return trim($string, '_');
}

// Function to generate PDF agreement letter using Dompdf
function generateAgreementPDF($client) {
    $folder = ensureAgreementFolder();
    if (!$folder) {
        return false;
    }

    $htmlContent = generateHtmlContent($client);
    // Use the name for a more recognizable file
    $sanitizedCompanyName = sanitizeFilename($client['company_name'] ?? 'Unknown'); 
    $pdfFile = $folder . '/agreement_' . $sanitizedCompanyName . '_' . $client['id'] . '_' . time() . '.pdf';

    try {
        $options = new Options();
        // IMPORTANT: Make sure you have Dompdf properly installed and the defaultFont is available or set to a standard web font.
        $options->set('isHtml5ParserEnabled', true);
        $options->set('isRemoteEnabled', true); 
        $options->set('defaultFont', 'Helvetica'); // Changed to a more standard font for simplicity

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
            sendErrorEmail('PDF file not generated for client ID: ' . $client['id'], 'admin@bthdevelopers.com');
            return false;
        }
    } catch (Exception $e) {
        error_log("Dompdf error: " . $e->getMessage(), 3, __DIR__ . '/../logs/error.log');
        sendErrorEmail('Failed to generate PDF for client ID: ' . $client['id'] . ' - ' . $e->getMessage(), 'admin@bthdevelopers.com');
        return false;
    }
}

// Function to generate HTML content for the agreement letter (Modernized CSS)
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
    
    // Fallback for missing data from database during PDF generation
    // Since the client has already accepted, we can assume data is correct, but adding null-coalescing for safety.
    $contractValue = isset($client['contract_value']) ? number_format((float)$client['contract_value'], 2, '.', '') : 'N/A';

    return <<<HTML
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Client Agreement Letter</title>
    <style>
        @page { margin: 40px; }
        body { font-family: 'Helvetica', Arial, sans-serif; margin: 0; padding: 0; line-height: 1.6; color: #333; }
        .container { padding: 20px; border: 1px solid #eee; border-radius: 8px; }
        .header { text-align: center; border-bottom: 3px solid #2c6da0; padding-bottom: 25px; margin-bottom: 25px; }
        .logo { font-size: 30px; font-weight: 700; color: #2c6da0; display: block; margin-bottom: 5px; }
        .company-info { font-size: 12px; color: #555; }
        h1, h2, h3 { color: #2c6da0; }
        h2 { font-size: 24px; margin: 10px 0 20px 0; }
        h3 { font-size: 18px; margin-top: 25px; border-bottom: 1px solid #eee; padding-bottom: 5px; }
        .client-info { margin-bottom: 20px; background-color: #f9f9f9; padding: 15px; border-radius: 6px; }
        .client-info-item { margin-bottom: 8px; font-size: 14px; }
        .terms-list { margin-top: 15px; padding-left: 20px; }
        .terms-list li { margin-bottom: 10px; }
        .legal-terms { margin-top: 40px; font-size: 13px; color: #666; border-top: 2px dashed #ccc; padding-top: 20px; }
        .signature-block { margin-top: 60px; display: flex; justify-content: space-between; text-align: center; }
        .signature-block div { width: 45%; }
        .signature-line { border-top: 1px solid #333; margin-top: 50px; padding-top: 5px; font-size: 14px; }
        .footer { text-align: center; font-size: 11px; color: #888; margin-top: 40px; padding-top: 10px; border-top: 1px solid #eee; }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <span class="logo">Bth Developers</span>
            <div class="company-info">
                <p>Ward No. 4, Jhiliya, Bettiah, West Champaran, Bihar, 845438 | GSTIN: 10JJBPK7226M1Z4</p>
                <p>Email: info@bthdevelopers.com | Phone: +919608083342 | Website: www.bthdevelopers.com</p>
            </div>
            <h2>Service Agreement Letter</h2>
        </div>

        <p style="font-size: 14px;"><strong>Date:</strong> {$today}</p>
        <p style="font-size: 14px;">This Agreement is entered into between <strong>Bth Developers</strong> ("the Company") and <strong>{$companyName}</strong> ("the Client").</p>

        <h3>Client Information</h3>
        <div class="client-info">
            <p class="client-info-item"><strong>Company Name:</strong> {$companyName}</p>
            <p class="client-info-item"><strong>Contact Person:</strong> {$contactPerson}</p>
            <p class="client-info-item"><strong>Email:</strong> {$email}</p>
            <p class="client-info-item"><strong>Phone:</strong> {$phone}</p>
            <p class="client-info-item"><strong>Address:</strong> {$address}</p>
            <p class="client-info-item"><strong>Industry:</strong> {$industry}</p>
            <p class="client-info-item"><strong>Contract Value:</strong> ₹{$contractValue}</p>
            <p class="client-info-item"><strong>Notes/Maintenance Details:</strong> {$notes}</p>
        </div>

        <h3>Agreement Terms</h3>
        <ol class="terms-list">
            <li><strong>Services:</strong> The Company shall provide services aligned with the Client's industry ($industry) and needs.</li>
            <li><strong>Payment:</strong> The Client agrees to a total contract value of ₹{$contractValue}, payable as per the mutually agreed schedule.</li>
            <li><strong>Confidentiality:</strong> Both parties shall maintain the utmost confidentiality of proprietary information.</li>
            <li><strong>Term & Termination:</strong> This agreement is effective from {$createdAt}. Either party may terminate with 30 days written notice.</li>
            <li><strong>Acceptance:</strong> This document serves as a confirmation of the terms accepted digitally by the Client on {$updatedAt}.</li>
        </ol>

        <h3>Confirmation of Digital Acceptance</h3>
        <div class="legal-terms">
            <p>This agreement has been **digitally accepted** by {$contactPerson} on behalf of {$companyName} via the unique acceptance link. The timestamp of this action is noted in our records.</p>
            <p>No further physical or digital signature is required for the validity of this agreement, in accordance with the Information Technology Act, 2000 (India) and other applicable laws.</p>
        </div>
        
        <div style="height: 100px;"></div> <div class="signature-block">
            <div style="margin-left: auto; text-align: left;">
                <img style="width: 120px;" src="https://www.bthdevelopers.com/img/stamp.png" alt="Bth Developers Stamp">
                <div class="signature-line">Authorized Signatory</div>
            </div>
        </div>

        <div class="footer">
            <p>Document ID: CLIENT-{$client['id']}-{$today}</p>
            <p>&copy; 2025 Bth Developers. All rights reserved.</p>
        </div>
    </div>
</body>
</html>
HTML;
}

try {
    $pdo = getDBConnection();

    if (!isset($_GET['client_id']) || !isset($_GET['token'])) {
        // ... (Error handling remains the same) ...
        error_log("Accept Agreement: Missing client_id or token", 3, __DIR__ . '/../logs/error.log');
        sendErrorEmail('Missing client_id or token in agreement acceptance', 'admin@bthdevelopers.com');
        echo "<h1>Error</h1><p>Missing required parameters.</p>";
        exit;
    }

    $client_id = intval($_GET['client_id']);
    $token = trim($_GET['token']);

    $stmt = $pdo->prepare("SELECT * FROM clients WHERE id = ? AND acceptance_token = ?");
    $stmt->execute([$client_id, $token]);
    $client = $stmt->fetch(PDO::FETCH_ASSOC);

    if ($client) {
        if ($client['agreement_accepted']) {
            echo "<h1>Agreement Already Accepted</h1><p>This agreement has already been accepted. The agreement letter has been sent to your email: {$client['email']} and **{$client['email']}**.</p>";
            exit;
        }

        // Update status in DB
        // NOTE: Keeping the existing token but setting acceptance_token to a unique new value or NULL is a better practice. 
        // For production, consider setting it to a new value or NULL, but for this context we'll keep the current token in the DB.
        $stmt = $pdo->prepare("UPDATE clients SET agreement_accepted = TRUE, status = 'active', updated_at = NOW() WHERE id = ?");
        $stmt->execute([$client_id]);

        // Re-fetch client data to get the updated 'updated_at' and 'status' for the PDF
        $stmt = $pdo->prepare("SELECT * FROM clients WHERE id = ?");
        $stmt->execute([$client_id]);
        $client = $stmt->fetch(PDO::FETCH_ASSOC);

        if ($client && $stmt->rowCount() > 0) {
            $pdfFile = generateAgreementPDF($client);
            if ($pdfFile) {
                $subject = "Client Agreement Letter (Accepted) - {$client['company_name']}";
                $body = "<h2>Dear {$client['contact_person']},</h2>
                        <p>Thank you for accepting the agreement with Bth Developers for {$client['company_name']}. Your agreement is now **active**. Please find attached the final agreement letter.</p>
                        <p>A copy of this accepted agreement has also been sent to the sender's email (info@bthdevelopers.com) for records.</p>
                        <p>Best regards,<br>Bth Developers Team</p>";
                
                // Send to client and CC to the sender's email (info@bthdevelopers.com)
                if (sendEmail($client['email'], $subject, $body, $pdfFile, EMAIL_FROM)) {
                    echo "<h1>Agreement Accepted & Finalized</h1><p>Thank you for accepting the agreement. The final agreement letter has been sent to your email: **{$client['email']}**. A copy has also been sent to info@bthdevelopers.com.</p>";
                } else {
                    sendErrorEmail('Failed to send final agreement letter email after acceptance for client ID: ' . $client_id, 'admin@bthdevelopers.com');
                    echo "<h1>Agreement Accepted</h1><p>Agreement accepted and client status updated, but there was an issue sending the agreement letter to your email. Please contact support at info@bthdevelopers.com.</p>";
                }
            } else {
                sendErrorEmail('Failed to generate PDF after acceptance for client ID: ' . $client_id, 'admin@bthdevelopers.com');
                echo "<h1>Agreement Accepted</h1><p>Agreement accepted, but there was an issue generating the agreement letter. Please contact support at info@bthdevelopers.com.</p>";
            }
        } else {
            sendErrorEmail('Failed to update agreement acceptance status for client ID: ' . $client_id, 'admin@bthdevelopers.com');
            echo "<h1>Error</h1><p>Failed to accept the agreement. Please contact support at info@bthdevelopers.com.</p>";
        }
    } else {
        error_log("Accept Agreement: Invalid client_id or token", 3, __DIR__ . '/../logs/error.log');
        sendErrorEmail('Invalid client_id or token for agreement acceptance: ID ' . $client_id, 'admin@bthdevelopers.com');
        echo "<h1>Error</h1><p>Invalid or expired link. Please contact support at info@bthdevelopers.com.</p>";
    }
} catch (PDOException $e) {
    error_log("Accept Agreement Error: " . $e->getMessage(), 3, __DIR__ . '/../logs/error.log');
    sendErrorEmail('Database error occurred during agreement acceptance: ' . $e->getMessage(), 'admin@bthdevelopers.com');
    echo "<h1>Error</h1><p>A database error occurred. Please contact support at info@bthdevelopers.com.</p>";
}
?>