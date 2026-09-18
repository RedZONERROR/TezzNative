<?php
error_reporting(E_ALL);
ini_set('display_errors', 1);

// Include necessary files
require_once __DIR__ . '/config/config.php';
require_once __DIR__ . '/includes/functions.php';

// Log file for cron job
$logFile = __DIR__ . '/logs/cron_monthly_payments.log';
$logData = "Cron job started at: " . date('Y-m-d H:i:s') . "\n";

// Function to get setting using PDO
function getSetting($pdo, $key) {
    $stmt = $pdo->prepare("SELECT setting_value FROM settings WHERE setting_key = ?");
    $stmt->execute([$key]);
    return $stmt->fetchColumn() ?: null;
}

// Function to get the default monthly maintenance amount
function getDefaultMaintenanceAmount($pdo) {
    $stmt = $pdo->prepare("SELECT setting_value FROM settings WHERE setting_key = 'default_maintenance_amount'");
    $stmt->execute();
    $amount = $stmt->fetchColumn();
    return $amount ? (float)$amount : 500.00; // Default to 1000 if not set
}

// Function to get the most recent maintenance charge for a client
function getMaintenanceCharge($pdo, $clientId) {
    $stmt = $pdo->prepare("
        SELECT amount 
        FROM maintenance 
        WHERE client_id = ? 
        ORDER BY created_at DESC 
        LIMIT 1
    ");
    $stmt->execute([$clientId]);
    $result = $stmt->fetchColumn();
    return $result ? (float)$result : null;
}

// Function to generate the invoice number in the format PAY-NNN
function generateInvoiceNumber($pdo) {
    $stmt = $pdo->prepare("SELECT MAX(id) AS max_id FROM get_payments");
    $stmt->execute();
    $max_id = $stmt->fetchColumn();

    // Ensure a valid number
    $fcount = ($max_id !== null) ? $max_id + 1 : 1;

    // Pad to 3 digits
    $sequence = str_pad($fcount, 3, '0', STR_PAD_LEFT); // e.g., 001, 002, etc.

    return "PAY-$sequence";
}

// Function to generate a checkout link for the payment
function generateCheckoutLink($pdo, $clientId, $amount, $invoiceNumber) {
    // Generate a unique transaction ID (even though we're not calling PhonePe API yet)
    $transactionId = 'TXN' . time();

    // Generate a unique payment token
    $paymentToken = bin2hex(random_bytes(16)); // 32-character token

    // Insert into payments table with the payment token
    $stmt = $pdo->prepare("
        INSERT INTO get_payments (client_id, invoice_number, collection_amount, method, payment_method, transaction_id, notes, payment_token, due_date, status, created_at, updated_at)
        VALUES (?, ?, ?, 'UPI', 'PhonePe', ?, 'Maintenance Charge', ?, ?, 'pending', NOW(), NOW())
    ");
    $stmt->execute([$clientId, $invoiceNumber, $amount, $transactionId, $paymentToken, date('Y-m-d')]);

    // Get the ID of the newly inserted payment record
    $paymentId = $pdo->lastInsertId();

    // Generate the checkout URL
    $checkoutUrl = "https://bthdevelopers.com/checkout.php?paymentId=" . urlencode($paymentId) . "&token=" . urlencode($paymentToken);

    return $checkoutUrl;
}

// Function to send payment reminder email with checkout link
function sendPaymentReminder($clientEmail, $contactPerson, $amount, $checkoutUrl) {
    $subject = "Monthly Maintenance Payment Reminder - Bth Developers";
    $body = "<h2>Dear $contactPerson,</h2>
             <p>We hope this message finds you well. This is a reminder that your monthly maintenance payment of ₹$amount for the month of " . date('F Y') . " is due.</p>
             <p>Please complete your payment by clicking the link below:</p>
             <p><a href='$checkoutUrl' style='display: inline-block; background-color: #6366f1; color: white; padding: 10px 20px; text-decoration: none; border-radius: 5px;'>Pay Now</a></p>
             <p>If you have already made the payment, please disregard this email.</p>
             <p>For any questions, feel free to contact us at info@bthdevelopers.com or +919608083342.</p>
             <p>Best regards,<br>Bth Developers Team</p>";

    if (!sendEmail($clientEmail, $subject, $body)) {
        $logData = "Failed to send payment reminder email to $clientEmail\n";
        file_put_contents(__DIR__ . '/logs/cron_monthly_payments.log', $logData, FILE_APPEND);
        return false;
    }
    return true;
}

try {
    $pdo = getDBConnection();

    // Get the current month and year
    $currentMonth = date('m');
    $currentYear = date('Y');

    // Fetch all clients
    $stmt = $pdo->prepare("SELECT id, contact_person, email FROM clients WHERE email IS NOT NULL");
    $stmt->execute();
    $clients = $stmt->fetchAll(PDO::FETCH_ASSOC);

    if (empty($clients)) {
        $logData .= "No clients found with valid emails.\n";
        file_put_contents($logFile, $logData, FILE_APPEND);
        exit;
    }

    $logData .= "Found " . count($clients) . " clients to check for " . date('F Y') . ".\n";

    // Get the default maintenance amount as a fallback
    $defaultMaintenanceAmount = getDefaultMaintenanceAmount($pdo);

    foreach ($clients as $client) {
        $clientId = $client['id'];
        $clientEmail = $client['email'];
        $contactPerson = $client['contact_person'] ?? 'Valued Customer';

        // Check if a payment exists in the transactions table for this client for the current month
        $stmt = $pdo->prepare("
            SELECT COUNT(*) 
            FROM transactions 
            WHERE customer_name = ? 
            AND MONTH(created_at) = ? 
            AND YEAR(created_at) = ? 
            AND status = 'paid'
        ");
        $stmt->execute([$clientEmail, $currentMonth, $currentYear]);
        $paymentCount = $stmt->fetchColumn();

        if ($paymentCount > 0) {
            $logData .= "Payment found for $clientEmail for " . date('F Y') . ". Skipping email.\n";
            continue;
        }

        $logData .= "No payment found for $clientEmail for " . date('F Y') . ". Fetching maintenance charge.\n";

        // Get the client's maintenance charge from the payments table
        $maintenanceCharge = getMaintenanceCharge($pdo, $clientId);
        $amount = $maintenanceCharge ?? $defaultMaintenanceAmount;

        $logData .= "Using amount ₹$amount for $clientEmail (Maintenance Charge: " . ($maintenanceCharge ?? 'Not Found') . ").\n";

        // Generate an invoice number in the format INV-YYYYMMDD-NNN
        $invoiceNumber = generateInvoiceNumber($pdo);

        // Generate a checkout link
        $checkoutUrl = generateCheckoutLink($pdo, $clientId, $amount, $invoiceNumber);

        if ($checkoutUrl) {
            $logData .= "Generated checkout link for $clientEmail: $checkoutUrl\n";

            // Send payment reminder email
            if (sendPaymentReminder($clientEmail, $contactPerson, $amount, $checkoutUrl)) {
                $logData .= "Sent payment reminder email to $clientEmail.\n";
            } else {
                $logData .= "Failed to send payment reminder email to $clientEmail.\n";
            }
        } else {
            $logData .= "Failed to generate checkout link for $clientEmail.\n";
        }
    }

    $logData .= "Cron job completed at: " . date('Y-m-d H:i:s') . "\n\n";
    file_put_contents($logFile, $logData, FILE_APPEND);

} catch (PDOException $e) {
    $logData .= "Database Error: " . $e->getMessage() . "\n";
    $logData .= "Cron job failed at: " . date('Y-m-d H:i:s') . "\n\n";
    file_put_contents($logFile, $logData, FILE_APPEND);
    exit(1);
}

exit(0);
?>