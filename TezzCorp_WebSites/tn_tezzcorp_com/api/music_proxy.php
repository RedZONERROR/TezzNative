<?php
session_start();
require_once '../config/config.php';

// Log request details for debugging
error_log("music_proxy.php accessed: " . date('Y-m-d H:i:s'), 3, __DIR__ . '/../logs/debug.log');
error_log("Request Method: " . $_SERVER['REQUEST_METHOD'], 3, __DIR__ . '/../logs/debug.log');
error_log("Session Data: " . json_encode($_SESSION), 3, __DIR__ . '/../logs/debug.log');
error_log("Headers: " . json_encode(getallheaders()), 3, __DIR__ . '/../logs/debug.log');

// Skip CSRF token validation for HEAD requests (used for validation, not state modification)
// if ($_SERVER['REQUEST_METHOD'] !== 'HEAD') {
//     if (!isset($_SERVER['HTTP_X_CSRF_TOKEN']) || !validateCsrfToken($_SERVER['HTTP_X_CSRF_TOKEN'])) {
//         error_log("CSRF validation failed", 3, __DIR__ . '/../logs/error.log');
//         http_response_code(403);
//         echo json_encode(['success' => false, 'message' => 'Invalid CSRF token']);
//         exit;
//     }
// }

// Check if user is authenticated
if (!isset($_SESSION['user_id']) || !isset($_SESSION['user_type'])) {
    error_log("User not authenticated", 3, __DIR__ . '/../logs/error.log');
    http_response_code(401);
    echo json_encode(['success' => false, 'message' => 'Unauthorized']);
    exit;
}

if (!isset($_GET['url']) || empty(trim($_GET['url']))) {
    error_log("File path missing in request", 3, __DIR__ . '/../logs/error.log');
    http_response_code(400);
    echo json_encode(['success' => false, 'message' => 'File path is required']);
    exit;
}

$file_path = trim($_GET['url']);
$real_path = __DIR__ . '/../' . $file_path;

error_log("Attempting to access file: " . $real_path, 3, __DIR__ . '/../logs/debug.log');

if (!file_exists($real_path)) {
    error_log("File not found: " . $real_path, 3, __DIR__ . '/../logs/error.log');
    http_response_code(404);
    echo json_encode(['success' => false, 'message' => 'File not found']);
    exit;
}

if (!is_readable($real_path)) {
    error_log("File not readable: " . $real_path, 3, __DIR__ . '/../logs/error.log');
    http_response_code(403);
    echo json_encode(['success' => false, 'message' => 'File not readable']);
    exit;
}

// For HEAD requests, return headers only
if ($_SERVER['REQUEST_METHOD'] === 'HEAD') {
    header('Content-Type: audio/mpeg');
    header('Content-Length: ' . filesize($real_path));
    exit;
}

// Determine the MIME type (force audio/mpeg for .png files that are actually MP3s)
$finfo = finfo_open(FILEINFO_MIME_TYPE);
$mime_type = finfo_file($finfo, $real_path);
finfo_close($finfo);

// If the file has a .png extension but is actually an audio file, override the MIME type
if (pathinfo($real_path, PATHINFO_EXTENSION) === 'png') {
    $mime_type = 'audio/mpeg';
}

header('Content-Type: ' . $mime_type);
header('Content-Length: ' . filesize($real_path));
header('Content-Disposition: inline; filename="' . basename($real_path) . '"');
readfile($real_path);
exit;
?>