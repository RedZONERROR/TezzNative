<?php
require_once __DIR__ . '/../config/config.php';
require_once __DIR__ . '/../vendor/autoload.php'; // Composer autoload for PHPMailer

use PHPMailer\PHPMailer\PHPMailer;
use PHPMailer\PHPMailer\Exception;

// Ensure session is started
if (session_status() === PHP_SESSION_NONE) {
    session_start();
}

// Function to send emails with PHPMailer
function sendEmail($to, $subject, $body) {
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

        $mail->isHTML(true);
        $mail->Subject = $subject;
        $mail->Body = $body;
        $mail->AltBody = strip_tags($body);

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

// Function to store transaction details (used in process-payment.php)
// function storeTransaction($conn, $merchantTransactionId, $orderId, $amount, $customerName, $customerEmail, $customerPhone) {
//     $stmt = $conn->prepare("INSERT INTO transactions (merchant_transaction_id, order_id, amount, customer_name, customer_email, customer_phone, created_at) VALUES (?, ?, ?, ?, ?, ?, NOW()");
//     $stmt->bind_param('sssdsss', $merchantTransactionId, $orderId, $amount, $customerName, $customerEmail, $customerPhone);
//     $stmt->execute();
//     $stmt->close();
// }

// Function to sanitize input
function sanitizeInput($input) {
    if (is_array($input)) {
        return array_map('sanitizeInput', $input);
    }
    return htmlspecialchars(trim($input), ENT_QUOTES, 'UTF-8');
}
?>