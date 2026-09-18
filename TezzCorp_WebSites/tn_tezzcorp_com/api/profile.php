<?php
session_start();
require_once '../config/config.php';

// Log request details for debugging
error_log("profile.php accessed: " . date('Y-m-d H:i:s'), 3, __DIR__ . '/../logs/debug.log');
error_log("Request Method: " . $_SERVER['REQUEST_METHOD'], 3, __DIR__ . '/../logs/debug.log');
error_log("Session Data: " . json_encode($_SESSION), 3, __DIR__ . '/../logs/debug.log');

// Check if user is authenticated
if (!isset($_SESSION['user_id']) || !isset($_SESSION['user_type'])) {
    error_log("User not authenticated", 3, __DIR__ . '/../logs/error.log');
    http_response_code(401);
    echo json_encode(['success' => false, 'message' => 'Unauthorized']);
    exit;
}

// Validate CSRF token
if (!isset($_SERVER['HTTP_X_CSRF_TOKEN']) || !validateCsrfToken($_SERVER['HTTP_X_CSRF_TOKEN'])) {
    error_log("CSRF validation failed", 3, __DIR__ . '/../logs/error.log');
    http_response_code(403);
    echo json_encode(['success' => false, 'message' => 'Invalid CSRF token']);
    exit;
}

$user_id = $_SESSION['user_id'];
$user_type = $_SESSION['user_type'];
$user_table = $user_type === 'admin' ? 'admins' : 'users';

try {
    $pdo = getDBConnection();

    if ($_SERVER['REQUEST_METHOD'] === 'GET') {
        // Fetch profile data
        $stmt = $pdo->prepare("SELECT * FROM user_profiles WHERE user_id = ?");
        $stmt->execute([$user_id]);
        $profile = $stmt->fetch(PDO::FETCH_ASSOC);

        // Fetch user email from users/admins table
        $stmt = $pdo->prepare("SELECT email FROM $user_table WHERE id = ?");
        $stmt->execute([$user_id]);
        $user = $stmt->fetch(PDO::FETCH_ASSOC);
        $profile['email'] = $user['email'] ?? '';

        // Placeholder for stats and security (to be implemented based on your requirements)
        $profile['stats'] = [
            'clients_managed' => '247',
            'tasks_completed' => '1,834',
            'revenue_generated' => '$2.4M',
            'team_rating' => '5.0'
        ];
        $profile['security'] = [
            'two_factor_enabled' => true,
            'password_last_changed' => '45 days ago'
        ];

        echo json_encode(['success' => true, 'profile' => $profile]);
    } elseif ($_SERVER['REQUEST_METHOD'] === 'POST') {
        // Update profile
        $input = json_decode(file_get_contents('php://input'), true);
        if (!$input) {
            http_response_code(400);
            echo json_encode(['success' => false, 'message' => 'Invalid input']);
            exit;
        }

        // Fields that can be updated
        $allowed_fields = [
            'first_name', 'last_name', 'phone', 'dob', 'department', 'address',
            'job_title', 'start_date', 'manager', 'office_location', 'work_schedule', 'location'
        ];
        $update_data = [];
        foreach ($allowed_fields as $field) {
            if (isset($input[$field])) {
                $update_data[$field] = $input[$field];
            }
        }

        if (empty($update_data)) {
            echo json_encode(['success' => false, 'message' => 'No fields to update']);
            exit;
        }

        // Check if profile exists
        $stmt = $pdo->prepare("SELECT id FROM user_profiles WHERE user_id = ?");
        $stmt->execute([$user_id]);
        $exists = $stmt->fetch();

        if ($exists) {
            // Update existing profile
            $fields = array_keys($update_data);
            $placeholders = array_map(fn($field) => "$field = ?", $fields);
            $sql = "UPDATE user_profiles SET " . implode(', ', $placeholders) . ", updated_at = CURRENT_TIMESTAMP WHERE user_id = ?";
            $stmt = $pdo->prepare($sql);
            $values = array_values($update_data);
            $values[] = $user_id;
            $stmt->execute($values);
        } else {
            // Insert new profile
            $update_data['user_id'] = $user_id;
            $fields = array_keys($update_data);
            $placeholders = array_fill(0, count($fields), '?');
            $sql = "INSERT INTO user_profiles (" . implode(', ', $fields) . ") VALUES (" . implode(', ', $placeholders) . ")";
            $stmt = $pdo->prepare($sql);
            $stmt->execute(array_values($update_data));
        }

        echo json_encode(['success' => true, 'message' => 'Profile updated successfully']);
    } else {
        http_response_code(405);
        echo json_encode(['success' => false, 'message' => 'Method not allowed']);
    }
} catch (PDOException $e) {
    error_log("Database error: " . $e->getMessage(), 3, __DIR__ . '/../logs/error.log');
    http_response_code(500);
    echo json_encode(['success' => false, 'message' => 'Database error']);
}
?>