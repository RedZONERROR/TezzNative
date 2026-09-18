<?php
session_start();
require_once 'config/config.php';
require_once 'includes/functions.php';

// Check for paymentId and token in query parameters
if (!isset($_GET['paymentId']) || empty(trim($_GET['paymentId'])) || !isset($_GET['token']) || empty(trim($_GET['token']))) {
    header('Location: https://www.tezzcorp.com');
    exit;
}

$paymentId = filter_var($_GET['paymentId'], FILTER_VALIDATE_INT);
$token = trim($_GET['token']);
if ($paymentId === false || $paymentId <= 0) {
    header('Location: https://www.tezzcorp.com');
    exit;
}

// Fetch payment details from the database and validate the token
try {
    $pdo = getDBConnection();
    $stmt = $pdo->prepare("
        SELECT gp.*, c.contact_person, c.email AS client_email, c.phone AS client_phone
        FROM get_payments gp
        LEFT JOIN clients c ON gp.client_id = c.id
        WHERE gp.id = ? AND gp.payment_token = ?
    ");
    $stmt->execute([$paymentId, $token]);
    $payment = $stmt->fetch(PDO::FETCH_ASSOC);

    if (!$payment) {
        header('Location: https://www.tezzcorp.com');
        exit;
    }

    // Prepare payment data
    $amount = $payment['collection_amount'];
    $orderId = $payment['invoice_number'];
    $customerName = $payment['contact_person'];
    $customerEmail = $payment['client_email'];
    $customerPhone = $payment['client_phone'];
    $description = $payment['collection_amount'] > 0 ? 'Immediate Payment Request' : 'Monthly Maintenance Charge';

} catch (PDOException $e) {
    error_log("Checkout Error: " . $e->getMessage(), 3, __DIR__ . '/logs/error.log');
    header('Location: https://www.tezzcorp.com');
    exit;
}

// Get company settings from database
$stmt = $pdo->prepare("SELECT setting_key, setting_value FROM settings WHERE setting_key IN ('company_name', 'logo_path')");
$stmt->execute();
$settings = $stmt->fetchAll(PDO::FETCH_KEY_PAIR);

$companyName = $settings['company_name'] ?? 'Bth Developers';
$logoPath = $settings['logo_path'] ?? '/logo.svg';

// Generate CSRF token for the POST request
$csrfToken = generateCsrfToken();

$gateway_type = "Advanced";
?>
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Checkout - <?php echo htmlspecialchars($companyName); ?></title>
    <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css">
    
    <!-- Favicon -->
    <link rel="icon" type="image/png" href="logo.svg">
    
    <style>
        body {
            background-color: #f8f9fa;
            font-family: Arial, sans-serif;
        }
        .checkout-container {
            max-width: 600px;
            margin: 50px auto;
            padding: 30px;
            background-color: #fff;
            border-radius: 10px;
            box-shadow: 0 0 20px rgba(0, 0, 0, 0.1);
        }
        .logo {
            max-height: 60px;
            margin-bottom: 20px;
        }
        .order-summary {
            background-color: #f8f9fa;
            border-radius: 8px;
            padding: 20px;
            margin-bottom: 20px;
        }
        .order-summary .row {
            padding: 5px 0;
            border-bottom: 1px solid #e9ecef;
        }
        .order-summary .row:last-child {
            border-bottom: none;
        }
        .pay-button {
            background-color: #5e2572;
            border: none;
            padding: 12px 20px;
            font-weight: bold;
            transition: background-color 0.3s ease;
        }
        .pay-button:hover {
            background-color: #4a1d5e;
        }
        .phonepe-logo {
            height: 30px;
            margin-right: 10px;
        }
        .secure-badge {
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 10px;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="checkout-container">
            <div class="text-center mb-4">
                <img style="width: 100px;" src="<?php echo htmlspecialchars($logoPath); ?>" alt="<?php echo htmlspecialchars($companyName); ?>" class="logo">
                <h2>Complete Your Payment</h2>
                <p class="text-muted">Secure payment powered by PhonePe</p>
            </div>
            
            <div class="order-summary">
                <h4>Order Summary</h4>
                <div class="row mb-2">
                    <div class="col-6">Payment ID:</div>
                    <div class="col-6 text-end"><?php echo htmlspecialchars($paymentId); ?></div>
                </div>
                <div class="row mb-2">
                    <div class="col-6">Order ID:</div>
                    <div class="col-6 text-end"><?php echo htmlspecialchars($orderId); ?></div>
                </div>
                <div class="row mb-2">
                    <div class="col-6">Amount:</div>
                    <div class="col-6 text-end">₹<?php echo number_format($amount, 2); ?></div>
                </div>
                <?php if (!empty($description)): ?>
                <div class="row mb-2">
                    <div class="col-6">Description:</div>
                    <div class="col-6 text-end"><?php echo htmlspecialchars($description); ?></div>
                </div>
                <?php endif; ?>
                <?php if (!empty($customerName)): ?>
                <div class="row mb-2">
                    <div class="col-6">Customer Name:</div>
                    <div class="col-6 text-end"><?php echo htmlspecialchars($customerName); ?></div>
                </div>
                <?php endif; ?>
                <?php if (!empty($customerEmail)): ?>
                <div class="row mb-2">
                    <div class="col-6">Email:</div>
                    <div class="col-6 text-end"><?php echo htmlspecialchars($customerEmail); ?></div>
                </div>
                <?php endif; ?>
                <?php if (!empty($customerPhone)): ?>
                <div class="row">
                    <div class="col-6">Phone:</div>
                    <div class="col-6 text-end"><?php echo htmlspecialchars($customerPhone); ?></div>
                </div>
                <?php endif; ?>
            </div>
            
            <div class="d-grid gap-2">
                <button type="button" id="payNowButton" class="btn btn-primary pay-button">
                    <img src="upi-logo.png" alt="UPI" class="phonepe-logo">
                    PAY NOW ₹<?php echo number_format($amount, 2); ?>
                </button>
            </div>
            
            <div class="mt-4 text-center">
                <small class="text-muted">By clicking "PAY NOW", you agree to our terms and conditions.</small>
            </div>
            
            <div class="mt-4 text-center secure-badge">
                <img src="secure-payment-badge.svg" alt="Secure Payment" height="30">
                <p class="small text-muted mt-2">Your payment information is secure. We use industry-standard encryption to protect your data.</p>
            </div>
            
            <!-- Loading overlay -->
            <div id="loadingOverlay" style="display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: rgba(0,0,0,0.5); z-index: 9999;">
                <div style="position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%); background: white; padding: 20px; border-radius: 10px; text-align: center;">
                    <div class="spinner-border text-primary" role="status">
                        <span class="visually-hidden">Loading...</span>
                    </div>
                    <p class="mt-2">Initiating payment, please wait...</p>
                </div>
            </div>
        </div>
    </div>
    
    <script>
    document.addEventListener('DOMContentLoaded', () => {
        const payBtn = document.getElementById('payNowButton');
        const overlay = document.getElementById('loadingOverlay');
        let win = null, poll = null;
    
        payBtn.onclick = () => {
            overlay.style.display = 'block';
    
            // Mobile-first centered popup
            const isMobile = innerWidth <= 768;
            const w = isMobile ? innerWidth - 40 : 540;
            const h = isMobile ? innerHeight - 100 : 720;
            const left = (screen.width / 2) - (w / 2);
            const top  = (screen.height / 2) - (h / 2);
    
            win = window.open('', 'payWin',
                `width=${w},height=${h},left=${left},top=${top},scrollbars=yes,resizable=yes`);
    
            if (!win) { 
                alert('Please allow popups to continue the payment.');
                overlay.style.display = 'none'; 
                return; 
            }
    
            // ✅ Embed PHP safely
            const data = {
                gateway_type: 'Advanced',
                cust_Mobile : <?= json_encode($customerPhone) ?>,
                cust_Email  : <?= json_encode($customerEmail) ?>,
                txnAmount   : <?= json_encode($amount) ?>,
                txnNote     : <?= json_encode($description) ?>
            };
    
            // Escape input values
            const inputs = Object.entries(data)
                .map(([k, v]) => `<input type="hidden" name="${k}" value="${String(v).replace(/"/g, '&quot;')}">`)
                .join('');
    
            // ✅ Write auto-submit form to popup
            win.document.write(`
            <!DOCTYPE html>
            <html>
            <head>
                <meta name="viewport" content="width=device-width,initial-scale=1">
                <title>Redirecting...</title>
            </head>
            <body style="font-family:Arial;text-align:center;padding:40px;background:#f9f9f9;">
                <h2>Please wait...</h2>
                <form id="f" method="POST" action="https://pay.tezzcorp.in/init/txnProcess.php">${inputs}</form>
                <script>document.getElementById('f').submit();<\/script>
            </body>
            </html>`);
            win.document.close();
    
            // Hide overlay if nothing happens (failsafe)
            setTimeout(() => overlay.style.display = 'none', 1000);
        };
    
        // ✅ Backup listener for postMessage events
        window.addEventListener('message', e => {
            if (e.origin !== 'https://www.tezzcorp.com' && e.origin !== 'https://pay.tezzcorp.in') return;
        
            const res = e.data;
            if (res.success) {
                win?.close();
                overlay.style.display = 'none';
        
                // ✅ Callback to your backend
                fetch('callback.php', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({
                        paymentId: <?= json_encode($orderId) ?>,
                        amount: <?= json_encode($amount) ?>,
                        customerName: <?= json_encode($customerName) ?>,
                        customerEmail: <?= json_encode($customerEmail) ?>,
                        customerPhone: <?= json_encode($customerPhone) ?>,
                        transactionId: res.data.transactionId,
                        rawResponse: res.data.rawPayload
                    })
                })
                .then(r => r.json())
                .then(r => {
                    // alert(r.success ? 'Payment successful! Email sent.' : 'Payment saved, but email failed.');
                    location.href = 'https://www.tezzcorp.com/';
                })
                .catch(() => alert('Callback failed. Please refresh to confirm payment.'));
            }
        });
    });
    </script>
</body>
</html>