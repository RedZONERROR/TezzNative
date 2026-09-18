<?php
header('Content-Type: application/json');
session_start();
require_once '../config/config.php';
require_once '../includes/functions.php';

// Validate CSRF token for POST, PUT, DELETE requests
if (in_array($_SERVER['REQUEST_METHOD'], ['POST', 'PUT', 'DELETE'])) {
    if (!isset($_SERVER['HTTP_X_CSRF_TOKEN']) || !validateCsrfToken($_SERVER['HTTP_X_CSRF_TOKEN'])) {
        http_response_code(403);
        echo json_encode(['success' => false, 'message' => 'Invalid CSRF token']);
        exit;
    }
}

try {
    $pdo = getDBConnection();
    $method = $_SERVER['REQUEST_METHOD'];

    if ($method === 'GET') {
        if (isset($_GET['export']) && $_GET['export'] === 'true') {
            // Fetch all payments for export
            try {
                $stmt = $pdo->prepare("SELECT gp.*, c.contact_person, l.license_key 
                                       FROM get_payments gp 
                                       LEFT JOIN clients c ON gp.client_id = c.id 
                                       LEFT JOIN licenses l ON l.client_id = c.id
                                       ORDER BY gp.created_at DESC");
                if (!$stmt) {
                    throw new PDOException("Failed to prepare statement: " . implode(", ", $pdo->errorInfo()));
                }

                if (!$stmt->execute()) {
                    throw new PDOException("Failed to execute query: " . implode(", ", $stmt->errorInfo()));
                }

                $payments = $stmt->fetchAll(PDO::FETCH_ASSOC);

                // Format numeric values with null checks
                foreach ($payments as &$payment) {
                    $payment['collection_amount'] = isset($payment['collection_amount']) ? (float)$payment['collection_amount'] : 0.0;
                }

                echo json_encode(['success' => true, 'data' => $payments]);
            } catch (PDOException $e) {
                error_log("GET Export Error: " . $e->getMessage(), 3, __DIR__ . '/../logs/error.log');
                http_response_code(500);
                echo json_encode(['success' => false, 'message' => 'Failed to fetch payments: ' . $e->getMessage()]);
                exit;
            }
        } else {
            // Fetch a single payment by ID
            if (isset($_GET['id']) && !empty(trim($_GET['id']))) {
                $payment_id = filter_var($_GET['id'], FILTER_VALIDATE_INT);
                if ($payment_id === false || $payment_id <= 0) {
                    http_response_code(400);
                    echo json_encode(['success' => false, 'message' => 'Invalid payment ID']);
                    exit;
                }

                $stmt = $pdo->prepare("SELECT gp.*, c.contact_person, l.license_key 
                                       FROM get_payments gp 
                                       LEFT JOIN clients c ON gp.client_id = c.id 
                                       LEFT JOIN licenses l ON l.client_id = c.id
                                       WHERE gp.id = ?");
                $stmt->execute([$payment_id]);
                $payment = $stmt->fetch(PDO::FETCH_ASSOC);

                if ($payment) {
                    $payment['collection_amount'] = isset($payment['collection_amount']) ? (float)$payment['collection_amount'] : 0.0;
                    echo json_encode(['success' => true, 'data' => $payment]);
                } else {
                    http_response_code(404);
                    echo json_encode(['success' => false, 'message' => 'Payment not found']);
                }
            } else {
                // Fetch all payments
                $stmt = $pdo->prepare("SELECT gp.*, c.contact_person, l.license_key 
                                       FROM get_payments gp 
                                       LEFT JOIN clients c ON gp.client_id = c.id 
                                       LEFT JOIN licenses l ON l.client_id = c.id
                                       ORDER BY gp.created_at DESC");
                $stmt->execute();
                $payments = $stmt->fetchAll(PDO::FETCH_ASSOC);

                // Format numeric values with null checks
                foreach ($payments as &$payment) {
                    $payment['collection_amount'] = isset($payment['collection_amount']) ? (float)$payment['collection_amount'] : 0.0;
                }

                echo json_encode(['success' => true, 'data' => $payments]);
            }
        }
    } elseif ($method === 'POST') {
        // Add a new payment
        $data = array_map('sanitizeInput', $_POST);

        // Validate required fields
        $required_fields = ['client_id', 'method', 'due_date', 'invoice_number', 'status'];
        foreach ($required_fields as $field) {
            if (!isset($data[$field]) || empty(trim($data[$field]))) {
                error_log("POST: Missing required field: $field", 3, __DIR__ . '/../logs/error.log');
                http_response_code(400);
                echo json_encode(['success' => false, 'message' => "$field is required"]);
                exit;
            }
        }

        // Validate payment method
        $valid_methods = ['credit_card', 'bank_transfer', 'cash', 'upi'];
        if (!in_array($data['method'], $valid_methods)) {
            error_log("POST: Invalid payment method: {$data['method']}", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['success' => false, 'message' => 'Invalid payment method']);
            exit;
        }

        // Validate status
        $valid_statuses = ['pending', 'paid', 'overdue'];
        if (!in_array($data['status'], $valid_statuses)) {
            error_log("POST: Invalid status: {$data['status']}", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['success' => false, 'message' => 'Invalid status']);
            exit;
        }

        // Validate due_date format
        if (!DateTime::createFromFormat('Y-m-d', $data['due_date'])) {
            error_log("POST: Invalid due_date format: {$data['due_date']}", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['success' => false, 'message' => 'Invalid due_date format. Use YYYY-MM-DD']);
            exit;
        }

        // Validate payment_date format if provided
        if (isset($data['payment_date']) && !empty($data['payment_date']) && !DateTime::createFromFormat('Y-m-d', $data['payment_date'])) {
            error_log("POST: Invalid payment_date format: {$data['payment_date']}", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['success' => false, 'message' => 'Invalid payment_date format. Use YYYY-MM-DD']);
            exit;
        }

        // Validate foreign keys (client_id) and fetch contract_value
        $client_id = filter_var($data['client_id'], FILTER_VALIDATE_INT);
        if ($client_id === false || $client_id <= 0) {
            error_log("POST: Invalid client ID: {$data['client_id']}", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['success' => false, 'message' => 'Invalid client ID']);
            exit;
        }

        $client_check = $pdo->prepare("SELECT id, contract_value, email, contact_person FROM clients WHERE id = ?");
        $client_check->execute([$client_id]);
        $client = $client_check->fetch(PDO::FETCH_ASSOC);
        if (!$client) {
            error_log("POST: Client not found: $client_id", 3, __DIR__ . '/../logs/error.log');
            http_response_code(404);
            echo json_encode(['success' => false, 'message' => 'Client not found']);
            exit;
        }

        $contract_value = floatval($client['contract_value']);
        $client_email = $client['email'];
        $contact_person = $client['contact_person'];

        // Check if invoice number already exists
        $invoice_number = trim($data['invoice_number']);
        $invoice_check = $pdo->prepare("SELECT id FROM payments WHERE invoice_number = ?");
        $invoice_check->execute([$invoice_number]);
        if ($invoice_check->rowCount() > 0) {
            error_log("POST: Invoice number already exists: $invoice_number", 3, __DIR__ . '/../logs/error.log');
            http_response_code(409);
            echo json_encode(['success' => false, 'message' => 'Invoice number already exists']);
            exit;
        }

        // Prepare data for insertion
        $maintenance_charge = isset($data['collection_amount']) ? floatval($data['collection_amount']) : 0;
        if ($maintenance_charge < 0) {
            error_log("POST: Invalid amounts - maintenance_charge: $maintenance_charge", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['success' => false, 'message' => 'Maintenance charge and collect today amount cannot be negative']);
            exit;
        }

        // Generate a unique payment token
        $payment_token = bin2hex(random_bytes(32));

        // Begin transaction
        $pdo->beginTransaction();
        try {
            $stmt = $pdo->prepare("
                INSERT INTO get_payments (
                    client_id, collection_amount, method, invoice_number, 
                    due_date, payment_date, status, notes, payment_token, created_at, updated_at
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, NOW(), NOW())
            ");
            $stmt->execute([
                $client_id,
                $maintenance_charge,
                $data['method'],
                $invoice_number,
                $data['due_date'],
                isset($data['payment_date']) && !empty($data['payment_date']) ? $data['payment_date'] : null,
                $data['status'],
                $data['notes'] ?? null,
                $payment_token
            ]);

            $payment_id = $pdo->lastInsertId();

            // Send email for maintenance charge with payment link if maintenance_charge > 0
            if ($maintenance_charge > 0) {
                $payment_link = "https://bthdevelopers.com/checkout.php?paymentId=" . urlencode($payment_id) . "&token=" . urlencode($payment_token);
                $subject = "Action Required: Pay Monthly Maintenance Charge - Bth Developers";
                $body = "<h2>Dear $contact_person,</h2>
                         <p>We have scheduled a monthly maintenance charge of ₹$maintenance_charge for your account with Bth Developers.</p>
                         <p><strong>Payment ID:</strong> $payment_id</p>
                         <p><strong>Invoice Number:</strong> $invoice_number</p>
                         <p><strong>Due Date:</strong> {$data['due_date']}</p>
                         <p>Please complete the payment by clicking the link below:</p>
                         <p><a href=\"$payment_link\" style=\"background-color: #0ea5e9; color: white; padding: 10px 20px; text-decoration: none; border-radius: 5px;\">Pay Now</a></p>
                         <p>If you have any questions, feel free to contact us at info@bthdevelopers.com or +919608083342.</p>
                         <p>Best regards,<br>Bth Developers Team</p>";

                if (!sendEmail($client_email, $subject, $body)) {
                    sendErrorEmail("Failed to send maintenance payment email for invoice $invoice_number", $_SESSION['user_email'] ?? 'admin@bthdevelopers.com');
                }
            }

            $pdo->commit();
            echo json_encode(['success' => true, 'message' => 'Payment added successfully', 'paymentId' => $payment_id]);
        } catch (Exception $e) {
            $pdo->rollBack();
            error_log("POST: Transaction failed: " . $e->getMessage(), 3, __DIR__ . '/../logs/error.log');
            http_response_code(500);
            echo json_encode(['success' => false, 'message' => 'Failed to add payment: ' . $e->getMessage()]);
            exit;
        }
    } elseif ($method === 'PUT') {
        // Update an existing payment
        if (!isset($_GET['id']) || empty(trim($_GET['id']))) {
            error_log("PUT: Payment ID missing in query parameter", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['success' => false, 'message' => 'Payment ID is required']);
            exit;
        }

        $payment_id = filter_var($_GET['id'], FILTER_VALIDATE_INT);
        if ($payment_id === false || $payment_id <= 0) {
            error_log("PUT: Invalid payment ID: {$_GET['id']}", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['success' => false, 'message' => 'Invalid payment ID']);
            exit;
        }

        // Parse the request body
        $input = file_get_contents("php://input");
        parse_str($input, $data);
        $data = array_map('sanitizeInput', $data);

        // Validate required fields
        $required_fields = ['client_id', 'method', 'due_date', 'status'];
        foreach ($required_fields as $field) {
            if (!isset($data[$field]) || empty(trim($data[$field]))) {
                error_log("PUT: Missing required field: $field", 3, __DIR__ . '/../logs/error.log');
                http_response_code(400);
                echo json_encode(['success' => false, 'message' => "$field is required"]);
                exit;
            }
        }

        // Validate payment method
        $valid_methods = ['credit_card', 'bank_transfer', 'cash', 'upi'];
        if (!in_array($data['method'], $valid_methods)) {
            error_log("PUT: Invalid payment method: {$data['method']}", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['success' => false, 'message' => 'Invalid payment method']);
            exit;
        }

        // Validate status
        $valid_statuses = ['pending', 'paid', 'overdue'];
        if (!in_array($data['status'], $valid_statuses)) {
            error_log("PUT: Invalid status: {$data['status']}", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['success' => false, 'message' => 'Invalid status']);
            exit;
        }

        // Validate due_date format
        if (!DateTime::createFromFormat('Y-m-d', $data['due_date'])) {
            error_log("PUT: Invalid due_date format: {$data['due_date']}", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['success' => false, 'message' => 'Invalid due_date format. Use YYYY-MM-DD']);
            exit;
        }

        // Validate payment_date format if provided
        if (isset($data['payment_date']) && !empty($data['payment_date']) && !DateTime::createFromFormat('Y-m-d', $data['payment_date'])) {
            error_log("PUT: Invalid payment_date format: {$data['payment_date']}", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['success' => false, 'message' => 'Invalid payment_date format. Use YYYY-MM-DD']);
            exit;
        }

        // Validate foreign keys (client_id) and fetch contract_value
        $client_id = filter_var($data['client_id'], FILTER_VALIDATE_INT);
        if ($client_id === false || $client_id <= 0) {
            error_log("PUT: Invalid client ID: {$data['client_id']}", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['success' => false, 'message' => 'Invalid client ID']);
            exit;
        }

        $client_check = $pdo->prepare("SELECT id, contract_value, email, contact_person FROM clients WHERE id = ?");
        $client_check->execute([$client_id]);
        $client = $client_check->fetch(PDO::FETCH_ASSOC);
        if (!$client) {
            error_log("PUT: Client not found: $client_id", 3, __DIR__ . '/../logs/error.log');
            http_response_code(404);
            echo json_encode(['success' => false, 'message' => 'Client not found']);
            exit;
        }

        $contract_value = floatval($client['contract_value']);
        $client_email = $client['email'];
        $contact_person = $client['contact_person'];

        // Fetch existing payment to get the current invoice number
        $stmt = $pdo->prepare("SELECT invoice_number FROM get_payments WHERE id = ?");
        $stmt->execute([$payment_id]);
        $current_payment = $stmt->fetch(PDO::FETCH_ASSOC);
        if (!$current_payment) {
            error_log("PUT: Payment not found: $payment_id", 3, __DIR__ . '/../logs/error.log');
            http_response_code(404);
            echo json_encode(['success' => false, 'message' => 'Payment not found']);
            exit;
        }

        $invoice_number = $current_payment['invoice_number'];
        $maintenance_charge = isset($data['collection_amount']) ? floatval($data['collection_amount']) : 0;
        if ($maintenance_charge < 0) {
            error_log("PUT: Invalid amounts - maintenance_charge: $maintenance_charge", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['success' => false, 'message' => 'Maintenance charge and collect today amount cannot be negative']);
            exit;
        }

        // Generate a new unique payment token for the updated payment
        $payment_token = bin2hex(random_bytes(32));

        // Begin transaction
        $pdo->beginTransaction();
        try {
            // Update payment (amount is set to contract_value, invoice_number is read-only, update payment_token)
            $stmt = $pdo->prepare("
                UPDATE get_payments SET
                    client_id = ?, collection_amount = ?, method = ?, 
                    due_date = ?, payment_date = ?, status = ?, notes = ?, payment_token = ?, updated_at = NOW()
                WHERE id = ?
            ");
            $stmt->execute([
                $client_id,
                $maintenance_charge,
                $data['method'],
                $data['due_date'],
                isset($data['payment_date']) && !empty($data['payment_date']) ? $data['payment_date'] : null,
                $data['status'],
                $data['notes'] ?? null,
                $payment_token,
                $payment_id
            ]);

            // Send email for maintenance charge with payment link if maintenance_charge > 0
            if ($maintenance_charge > 0) {
                $payment_link = "https://bthdevelopers.com/checkout.php?paymentId=" . urlencode($payment_id) . "&token=" . urlencode($payment_token);
                $subject = "Action Required: Pay Updated Monthly Maintenance Charge - Bth Developers";
                $body = "<h2>Dear $contact_person,</h2>
                         <p>We have updated the monthly maintenance charge for your account with Bth Developers.</p>
                         <p><strong>Payment ID:</strong> $payment_id</p>
                         <p><strong>Invoice Number:</strong> $invoice_number</p>
                         <p><strong>Maintenance Charge:</strong> ₹$maintenance_charge</p>
                         <p><strong>Due Date:</strong> {$data['due_date']}</p>
                         <p>Please complete the payment by clicking the link below:</p>
                         <p><a href=\"$payment_link\" style=\"background-color: #0ea5e9; color: white; padding: 10px 20px; text-decoration: none; border-radius: 5px;\">Pay Now</a></p>
                         <p>If you have any questions, feel free to contact us at info@bthdevelopers.com or +919608083342.</p>
                         <p>Best regards,<br>Bth Developers Team</p>";

                if (!sendEmail($client_email, $subject, $body)) {
                    sendErrorEmail("Failed to send updated maintenance payment email for invoice $invoice_number", $_SESSION['user_email'] ?? 'admin@bthdevelopers.com');
                }
            }

            $pdo->commit();
            echo json_encode(['success' => true, 'message' => 'Payment updated successfully']);
        } catch (Exception $e) {
            $pdo->rollBack();
            error_log("PUT: Transaction failed: " . $e->getMessage(), 3, __DIR__ . '/../logs/error.log');
            http_response_code(500);
            echo json_encode(['success' => false, 'message' => 'Failed to update payment: ' . $e->getMessage()]);
            exit;
        }
    } elseif ($method === 'DELETE') {
        // Delete a payment
        if (!isset($_GET['id']) || empty(trim($_GET['id']))) {
            error_log("DELETE: Payment ID missing in request", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['success' => false, 'message' => 'Payment ID is required']);
            exit;
        }

        $payment_id = filter_var($_GET['id'], FILTER_VALIDATE_INT);
        if ($payment_id === false || $payment_id <= 0) {
            error_log("DELETE: Invalid payment ID: {$_GET['id']}", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['success' => false, 'message' => 'Invalid payment ID']);
            exit;
        }

        $pdo->beginTransaction();
        try {
            $stmt = $pdo->prepare("DELETE FROM get_payments WHERE id = ?");
            $stmt->execute([$payment_id]);

            if ($stmt->rowCount() > 0) {
                $pdo->commit();
                echo json_encode(['success' => true, 'message' => 'Payment deleted successfully']);
            } else {
                $pdo->rollBack();
                error_log("DELETE: Payment not found: $payment_id", 3, __DIR__ . '/../logs/error.log');
                http_response_code(404);
                echo json_encode(['success' => false, 'message' => 'Payment not found']);
            }
        } catch (Exception $e) {
            $pdo->rollBack();
            error_log("DELETE: Transaction failed: " . $e->getMessage(), 3, __DIR__ . '/../logs/error.log');
            http_response_code(500);
            echo json_encode(['success' => false, 'message' => 'Failed to delete payment: ' . $e->getMessage()]);
            exit;
        }
    } else {
        http_response_code(405);
        echo json_encode(['success' => false, 'message' => 'Invalid request method']);
    }
} catch (PDOException $e) {
    error_log("API Error: " . $e->getMessage(), 3, __DIR__ . '/../logs/error.log');
    http_response_code(500);
    echo json_encode(['success' => false, 'message' => 'Database error occurred: ' . $e->getMessage()]);
} catch (Exception $e) {
    error_log("General Error: " . $e->getMessage(), 3, __DIR__ . '/../logs/error.log');
    http_response_code(500);
    echo json_encode(['success' => false, 'message' => 'An unexpected error occurred: ' . $e->getMessage()]);
}
?>