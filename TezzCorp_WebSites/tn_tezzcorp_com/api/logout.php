<?php
session_start();
require_once '../config/config.php';

// Log request details for debugging
error_log("logout.php accessed: " . date('Y-m-d H:i:s'), 3, __DIR__ . '/../logs/debug.log');
error_log("Request Method: " . $_SERVER['REQUEST_METHOD'], 3, __DIR__ . '/../logs/debug.log');
error_log("Session Data Before Logout: " . json_encode($_SESSION), 3, __DIR__ . '/../logs/debug.log');

// Check if user is authenticated
if (!isset($_SESSION['user_id']) || !isset($_SESSION['user_type'])) {
    error_log("User not authenticated", 3, __DIR__ . '/../logs/error.log');
    http_response_code(401);
    echo json_encode(['success' => false, 'message' => 'Not authenticated']);
    exit;
}

// Validate CSRF token
if (!isset($_SERVER['HTTP_X_CSRF_TOKEN']) || !validateCsrfToken($_SERVER['HTTP_X_CSRF_TOKEN'])) {
    error_log("CSRF validation failed", 3, __DIR__ . '/../logs/error.log');
    http_response_code(403);
    echo json_encode(['success' => false, 'message' => 'Invalid CSRF token']);
    exit;
}

// Only allow POST requests
if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    http_response_code(405);
    echo json_encode(['success' => false, 'message' => 'Method not allowed']);
    exit;
}

try {
    // Clear session data
    $_SESSION = [];
    
    // Destroy the session
    if (session_destroy()) {
        // Clear session cookie
        if (ini_get("session.use_cookies")) {
            $params = session_get_cookie_params();
            setcookie(
                session_name(),
                '',
                time() - 42000,
                $params["path"],
                $params["domain"],
                $params["secure"],
                $params["httponly"]
            );
        }
        
        // Log successful logout
        error_log("User logged out successfully", 3, __DIR__ . '/../logs/debug.log');
        echo json_encode(['success' => true, 'message' => 'Logged out successfully']);
    } else {
        error_log("Failed to destroy session", 3, __DIR__ . '/../logs/error.log');
        http_response_code(500);
        echo json_encode(['success' => false, 'message' => 'Failed to log out']);
    }
} catch (Exception $e) {
    error_log("Logout error: " . $e->getMessage(), 3, __DIR__ . '/../logs/error.log');
    http_response_code(500);
    echo json_encode(['success' => false, 'message' => 'An error occurred during logout']);
}
?>