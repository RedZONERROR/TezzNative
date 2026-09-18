<?php
require_once '../config/config.php';

header('Content-Type: application/json');

// CSRF Token Validation
$csrf_token = isset($_SERVER['HTTP_X_CSRF_TOKEN']) ? $_SERVER['HTTP_X_CSRF_TOKEN'] : '';
if (!validateCsrfToken($csrf_token)) {
    echo json_encode(['success' => false, 'message' => 'Invalid CSRF token']);
    exit;
}

// Check if user is authenticated
if (!isset($_SESSION['user_id']) || !isset($_SESSION['user_type'])) {
    echo json_encode(['success' => false, 'message' => 'Unauthorized']);
    exit;
}

try {
    $pdo = getDBConnection();
    $method = $_SERVER['REQUEST_METHOD'];

    // Function to generate a license key in the format XXXXX-XXXXX-XXXXX
    function generateLicenseKey() {
        $characters = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789';
        $license_key = '';
        for ($i = 0; $i < 15; $i++) {
            $license_key .= $characters[rand(0, strlen($characters) - 1)];
            if ($i == 4 || $i == 9) {
                $license_key .= '-';
            }
        }
        return $license_key;
    }

    switch ($method) {
        case 'GET':
            // Fetch licenses
            $export = isset($_GET['export']) && $_GET['export'] === 'true';
            $license_id = isset($_GET['id']) ? (int)$_GET['id'] : null;

            if ($license_id) {
                // Fetch a single license
                $stmt = $pdo->prepare("
                    SELECT l.*, c.company_name AS client_name 
                    FROM licenses l 
                    LEFT JOIN clients c ON l.client_id = c.id 
                    WHERE l.id = ?
                ");
                $stmt->execute([$license_id]);
                $license = $stmt->fetch(PDO::FETCH_ASSOC);

                if ($license) {
                    echo json_encode(['success' => true, 'data' => $license]);
                } else {
                    echo json_encode(['success' => false, 'message' => 'License not found']);
                }
            } else {
                // Fetch all licenses
                $stmt = $pdo->prepare("
                    SELECT l.*, c.company_name AS client_name 
                    FROM licenses l 
                    LEFT JOIN clients c ON l.client_id = c.id
                ");
                $stmt->execute();
                $licenses = $stmt->fetchAll(PDO::FETCH_ASSOC);

                if ($export) {
                    echo json_encode(['success' => true, 'data' => $licenses]);
                } else {
                    echo json_encode(['success' => true, 'data' => $licenses]);
                }
            }
            break;

        case 'POST':
            // Add a new license with auto-generated license key
            $client_id = (int)($_POST['client_id'] ?? 0);
            $issued_date = $_POST['issued_date'] ?? '';
            $expiry_date = $_POST['expiry_date'] ?? '';
            $status = $_POST['status'] ?? 'active';
            $notes = trim($_POST['notes'] ?? '');

            // Validate inputs
            if (empty($client_id) || empty($issued_date) || empty($expiry_date)) {
                echo json_encode(['success' => false, 'message' => 'All required fields must be filled']);
                exit;
            }

            // Check if client exists
            $stmt = $pdo->prepare("SELECT id FROM clients WHERE id = ?");
            $stmt->execute([$client_id]);
            if (!$stmt->fetch()) {
                echo json_encode(['success' => false, 'message' => 'Invalid client']);
                exit;
            }

            // Generate a unique license key
            do {
                $license_key = generateLicenseKey();
                $stmt = $pdo->prepare("SELECT COUNT(*) FROM licenses WHERE license_key = ?");
                $stmt->execute([$license_key]);
                $exists = $stmt->fetchColumn();
            } while ($exists);

            // Insert license into database
            $stmt = $pdo->prepare("
                INSERT INTO licenses (license_key, client_id, issued_date, expiry_date, status, notes)
                VALUES (?, ?, ?, ?, ?, ?)
            ");
            $stmt->execute([$license_key, $client_id, $issued_date, $expiry_date, $status, $notes]);

            echo json_encode(['success' => true, 'message' => 'License added successfully', 'license_key' => $license_key]);
            break;

        case 'PUT':
            // Update an existing license
            parse_str(file_get_contents("php://input"), $_PUT);
            $license_id = isset($_GET['id']) ? (int)$_GET['id'] : 0;
            $license_key = trim($_PUT['license_key'] ?? '');
            $client_id = (int)($_PUT['client_id'] ?? 0);
            $issued_date = $_PUT['issued_date'] ?? '';
            $expiry_date = $_PUT['expiry_date'] ?? '';
            $status = $_PUT['status'] ?? 'active';
            $notes = trim($_PUT['notes'] ?? '');

            // Validate inputs
            if (empty($license_id) || empty($license_key) || empty($client_id) || empty($issued_date) || empty($expiry_date)) {
                echo json_encode(['success' => false, 'message' => 'All required fields must be filled']);
                exit;
            }

            // Validate license key format
            if (!preg_match('/^[A-Z0-9]{5}-[A-Z0-9]{5}-[A-Z0-9]{5}$/', $license_key)) {
                echo json_encode(['success' => false, 'message' => 'Invalid license key format. Use XXXXX-XXXXX-XXXXX']);
                exit;
            }

            // Check if license exists
            $stmt = $pdo->prepare("SELECT * FROM licenses WHERE id = ?");
            $stmt->execute([$license_id]);
            $license = $stmt->fetch(PDO::FETCH_ASSOC);
            if (!$license) {
                echo json_encode(['success' => false, 'message' => 'License not found']);
                exit;
            }

            // Check if client exists
            $stmt = $pdo->prepare("SELECT id FROM clients WHERE id = ?");
            $stmt->execute([$client_id]);
            if (!$stmt->fetch()) {
                echo json_encode(['success' => false, 'message' => 'Invalid client']);
                exit;
            }

            // Update license in database
            $stmt = $pdo->prepare("
                UPDATE licenses 
                SET license_key = ?, client_id = ?, issued_date = ?, expiry_date = ?, status = ?, notes = ?
                WHERE id = ?
            ");
            $stmt->execute([$license_key, $client_id, $issued_date, $expiry_date, $status, $notes, $license_id]);

            echo json_encode(['success' => true, 'message' => 'License updated successfully']);
            break;

        case 'DELETE':
            // Delete a license
            $license_id = isset($_GET['id']) ? (int)$_GET['id'] : 0;

            if (empty($license_id)) {
                echo json_encode(['success' => false, 'message' => 'License ID is required']);
                exit;
            }

            // Check if license exists
            $stmt = $pdo->prepare("SELECT * FROM licenses WHERE id = ?");
            $stmt->execute([$license_id]);
            $license = $stmt->fetch(PDO::FETCH_ASSOC);
            if (!$license) {
                echo json_encode(['success' => false, 'message' => 'License not found']);
                exit;
            }

            // Delete license from database
            $stmt = $pdo->prepare("DELETE FROM licenses WHERE id = ?");
            $stmt->execute([$license_id]);

            echo json_encode(['success' => true, 'message' => 'License deleted successfully']);
            break;

        default:
            echo json_encode(['success' => false, 'message' => 'Method not allowed']);
            break;
    }
} catch (Exception $e) {
    // Log detailed error message for debugging
    error_log("Error in licenses.php: " . $e->getMessage() . " on line " . $e->getLine() . " in file " . $e->getFile(), 3, __DIR__ . '/../logs/error.log');
    echo json_encode(['success' => false, 'message' => 'An error occurred: ' . $e->getMessage()]);
}
?>