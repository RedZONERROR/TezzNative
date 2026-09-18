<?php
// callback.php
header('Content-Type: application/json');
require_once 'config/config.php';
require_once 'includes/functions.php';

$input = json_decode(file_get_contents('php://input'), true);
if (!$input) {
    echo json_encode(['success' => false, 'message' => 'Invalid data']);
    exit;
}

$paymentId       = $input['paymentId'];
$orderId         = $input['orderId'] ?? '';
$amount          = (float)($input['amount'] ?? 0);
$customerName    = $input['customerName'] ?? '';
$customerEmail   = $input['customerEmail'] ?? '';
$customerPhone   = $input['customerPhone'] ?? '';
$transactionId   = $input['transactionId'] ?? $orderId;
$rawResponse     = $input['rawResponse'] ?? [];

// === 1. Insert/Update transactions table ===
try {
    $pdo = getDBConnection();

    $stmt = $pdo->prepare("
        INSERT INTO transactions 
        (merchant_transaction_id, order_id, amount, customer_name, customer_email, customer_phone, created_at, status)
        VALUES (?, ?, ?, ?, ?, ?, NOW(), 'paid')
        ON DUPLICATE KEY UPDATE
        status = 'paid',
        customer_name = VALUES(customer_name),
        customer_email = VALUES(customer_email),
        customer_phone = VALUES(customer_phone)
    ");
    $stmt->execute([$transactionId, $paymentId, $amount, $customerName, $customerEmail, $customerPhone]);

    // === 2. Update get_payments ===
    $today = date('Y-m-d');
    $stmt = $pdo->prepare("UPDATE get_payments SET status = 'paid', payment_date = ? WHERE invoice_number = ?");
    $stmt->execute([$today, $paymentId]);

} catch (Exception $e) {
    error_log("DB Error: " . $e->getMessage());
    echo json_encode(['success' => false, 'message' => 'Database error']);
    exit;
}

// === 3. Send Email ===
$subject = "Payment Confirmation - Tezz Corporation";
$body = "
<h2>Dear $customerName,</h2>
<p>Thank you for your payment of <strong>₹" . number_format($amount, 2) . "</strong>.</p>
<p><strong>Invoice:</strong> $paymentId</p>
<p><strong>Transaction ID:</strong> $transactionId</p>
<p><strong>Date:</strong> " . date('d M Y, h:i A') . "</p>
<p>We appreciate your business!</p>
<p>Best regards,<br><strong>Tezz Corporation Team</strong></p>
";

if (sendEmail($customerEmail, $subject, $body)) {
    echo json_encode(['success' => true, 'message' => 'Payment saved & email sent']);
} else {
    echo json_encode(['success' => false, 'message' => 'Payment saved, email failed']);
}
?>