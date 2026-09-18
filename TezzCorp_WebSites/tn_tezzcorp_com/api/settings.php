<?php
session_start();
require_once '../config/config.php';

// Log request details for debugging
error_log("settings.php accessed: " . date('Y-m-d H:i:s'), 3, __DIR__ . '/../logs/debug.log');
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

try {
    $pdo = getDBConnection();

    if ($_SERVER['REQUEST_METHOD'] === 'GET') {
        // Fetch settings
        $stmt = $pdo->prepare("SELECT setting_key, setting_value FROM settings WHERE user_id = ?");
        $stmt->execute([$user_id]);
        $rows = $stmt->fetchAll(PDO::FETCH_ASSOC);

        $settings = [];
        foreach ($rows as $row) {
            $key = $row['setting_key'];
            $value = $row['setting_value'];
            // Convert boolean-like strings to actual booleans
            if ($value === 'true') $value = true;
            if ($value === 'false') $value = false;
            // Handle nested settings (e.g., integrations)
            if (strpos($key, '.') !== false) {
                [$parent, $child] = explode('.', $key, 2);
                if (!isset($settings[$parent])) $settings[$parent] = [];
                $settings[$parent][$child] = $value;
            } else {
                $settings[$key] = $value;
            }
        }

        echo json_encode(['success' => true, 'settings' => $settings]);
    } elseif ($_SERVER['REQUEST_METHOD'] === 'POST') {
        // Update settings
        $input = json_decode(file_get_contents('php://input'), true);
        if (!$input) {
            http_response_code(400);
            echo json_encode(['success' => false, 'message' => 'Invalid input']);
            exit;
        }

        // Handle reset
        if (isset($input['reset']) && $input['reset'] === true) {
            $stmt = $pdo->prepare("DELETE FROM settings WHERE user_id = ?");
            $stmt->execute([$user_id]);
            echo json_encode(['success' => true, 'message' => 'Settings reset to defaults']);
            exit;
        }

        // Prepare to upsert settings
        $stmt = $pdo->prepare("INSERT INTO settings (user_id, setting_key, setting_value) VALUES (?, ?, ?) 
                               ON DUPLICATE KEY UPDATE setting_value = ?, updated_at = CURRENT_TIMESTAMP");

        foreach ($input as $key => $value) {
            // Skip nested settings for now (e.g., integrations, billing, team)
            if (in_array($key, ['integrations', 'billing', 'team'])) {
                continue; // These require additional implementation
            }
            $stmt->execute([$user_id, $key, $value, $value]);
        }

        echo json_encode(['success' => true, 'message' => 'Settings updated successfully']);
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