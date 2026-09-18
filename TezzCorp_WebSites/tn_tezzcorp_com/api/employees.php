<?php
header('Content-Type: application/json');
require_once '../config/config.php';

// Validate CSRF token
if (!isset($_SERVER['HTTP_X_CSRF_TOKEN']) || $_SERVER['HTTP_X_CSRF_TOKEN'] !== $_SESSION['csrf_token']) {
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

    // Directory for photo uploads
    $uploadDir = __DIR__ . '/../uploads/employees/';
    if (!file_exists($uploadDir)) {
        mkdir($uploadDir, 0755, true);
    }

    if ($method === 'GET') {
        if (isset($_GET['export']) && $_GET['export'] === 'true') {
            // Fetch all employees for export
            $stmt = $pdo->prepare("SELECT * FROM employees ORDER BY created_at DESC");
            $stmt->execute();
            $employees = $stmt->fetchAll(PDO::FETCH_ASSOC);
            echo json_encode(['success' => true, 'data' => $employees]);
        } else {
            // Fetch a single employee by ID
            if (!isset($_GET['id']) || empty(trim($_GET['id']))) {
                error_log("GET: Employee ID missing in request", 3, __DIR__ . '/../logs/error.log');
                echo json_encode(['success' => false, 'message' => 'Employee ID is required']);
                exit;
            }

            $employee_id = intval($_GET['id']);
            $stmt = $pdo->prepare("SELECT * FROM employees WHERE id = ?");
            $stmt->execute([$employee_id]);
            $employee = $stmt->fetch(PDO::FETCH_ASSOC);

            if ($employee) {
                echo json_encode(['success' => true, 'data' => $employee]);
            } else {
                echo json_encode(['success' => false, 'message' => 'Employee not found']);
            }
        }
    } elseif ($method === 'POST') {
        // Add a new employee
        $data = $_POST;

        // Validate required fields
        if (empty($data['first_name']) || empty($data['last_name']) || empty($data['email']) || empty($data['department']) || empty($data['position']) || empty($data['hire_date'])) {
            echo json_encode(['success' => false, 'message' => 'First name, last name, email, department, position, and hire date are required']);
            exit;
        }

        // Handle photo upload
        $photoPath = null;
        if (isset($_FILES['photo']) && $_FILES['photo']['error'] === UPLOAD_ERR_OK) {
            $photo = $_FILES['photo'];
            $ext = pathinfo($photo['name'], PATHINFO_EXTENSION);
            $photoName = uniqid('employee_') . '.' . $ext;
            $photoPath = '/uploads/employees/' . $photoName;

            if (!move_uploaded_file($photo['tmp_name'], $uploadDir . $photoName)) {
                echo json_encode(['success' => false, 'message' => 'Failed to upload photo']);
                exit;
            }
        }

        // Prepare data for insertion
        $stmt = $pdo->prepare("
            INSERT INTO employees (
                first_name, last_name, email, phone, department, position, salary, hire_date, address, status, photo, created_at
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, NOW())
        ");
        $stmt->execute([
            $data['first_name'],
            $data['last_name'],
            $data['email'],
            $data['phone'] ?? null,
            $data['department'],
            $data['position'],
            isset($data['salary']) ? floatval($data['salary']) : null,
            $data['hire_date'],
            $data['address'] ?? null,
            $data['status'] ?? 'active',
            $photoPath
        ]);

        if ($stmt->rowCount() > 0) {
            echo json_encode(['success' => true, 'message' => 'Employee added successfully']);
        } else {
            echo json_encode(['success' => false, 'message' => 'Failed to add employee']);
        }
    } elseif ($method === 'PUT') {
        // Update an existing employee
        if (!isset($_GET['id']) || empty(trim($_GET['id']))) {
            error_log("PUT: Employee ID missing in query parameter", 3, __DIR__ . '/../logs/error.log');
            echo json_encode(['success' => false, 'message' => 'Employee ID is required']);
            exit;
        }

        $employee_id = intval($_GET['id']);

        // Parse the request body (for PUT, we expect application/x-www-form-urlencoded or JSON)
        $input = file_get_contents("php://input");
        parse_str($input, $data);

        // Log the received data for debugging
        error_log("PUT: Received data: " . print_r($data, true), 3, __DIR__ . '/../logs/error.log');

        // Validate required fields
        if (empty($data['first_name']) || empty($data['last_name']) || empty($data['email']) || empty($data['department']) || empty($data['position']) || empty($data['hire_date'])) {
            echo json_encode(['success' => false, 'message' => 'First name, last name, email, department, position, and hire date are required']);
            exit;
        }

        // For PUT, we'll skip photo upload for now (can be handled separately)
        $photoPath = $data['existing_photo'] ?? null;

        $stmt = $pdo->prepare("
            UPDATE employees SET
                first_name = ?, last_name = ?, email = ?, phone = ?, department = ?, position = ?, 
                salary = ?, hire_date = ?, address = ?, status = ?, photo = ?
            WHERE id = ?
        ");
        $stmt->execute([
            $data['first_name'],
            $data['last_name'],
            $data['email'],
            $data['phone'] ?? null,
            $data['department'],
            $data['position'],
            isset($data['salary']) ? floatval($data['salary']) : null,
            $data['hire_date'],
            $data['address'] ?? null,
            $data['status'] ?? 'active',
            $photoPath,
            $employee_id
        ]);

        if ($stmt->rowCount() > 0) {
            echo json_encode(['success' => true, 'message' => 'Employee updated successfully']);
        } else {
            echo json_encode(['success' => false, 'message' => 'No changes made or employee not found']);
        }
    } elseif ($method === 'DELETE') {
        // Delete an employee
        if (!isset($_GET['id']) || empty(trim($_GET['id']))) {
            error_log("DELETE: Employee ID missing in request", 3, __DIR__ . '/../logs/error.log');
            echo json_encode(['success' => false, 'message' => 'Employee ID is required']);
            exit;
        }

        $employee_id = intval($_GET['id']);

        // Fetch the employee to get the photo path
        $stmt = $pdo->prepare("SELECT photo FROM employees WHERE id = ?");
        $stmt->execute([$employee_id]);
        $employee = $stmt->fetch(PDO::FETCH_ASSOC);

        // Delete the photo file if it exists
        if ($employee && !empty($employee['photo'])) {
            $photoPath = __DIR__ . '/..' . $employee['photo'];
            if (file_exists($photoPath)) {
                unlink($photoPath);
            }
        }

        $stmt = $pdo->prepare("DELETE FROM employees WHERE id = ?");
        $stmt->execute([$employee_id]);

        if ($stmt->rowCount() > 0) {
            echo json_encode(['success' => true, 'message' => 'Employee deleted successfully']);
        } else {
            echo json_encode(['success' => false, 'message' => 'Employee not found']);
        }
    } else {
        echo json_encode(['success' => false, 'message' => 'Invalid request method']);
    }
} catch (PDOException $e) {
    error_log("API Error: " . $e->getMessage(), 3, __DIR__ . '/../logs/error.log');
    echo json_encode(['success' => false, 'message' => 'Database error occurred']);
}
?>