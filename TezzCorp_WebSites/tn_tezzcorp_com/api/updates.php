<?php
/**
 * AI-ERP 2.0 Update Server API
 * Manages update packages and metadata.
 */

error_reporting(E_ALL);
ini_set('display_errors', 0);
ini_set('log_errors', 1);
ini_set('error_log', __DIR__ . '/../logs/error.log');

require_once '../config/config.php';

// Set headers for JSON response
header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type, X-CSRF-Token');

// Handle preflight OPTIONS request
if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(200);
    exit;
}

// Log request details
error_log("Received request: Method=" . $_SERVER['REQUEST_METHOD'] . ", Action=" . ($_GET['action'] ?? 'none') . ", GET=" . json_encode($_GET) . ", POST=" . json_encode($_POST), 3, __DIR__ . '/../logs/error.log');

$versions_dir = __DIR__ . '/versions';
if (!file_exists($versions_dir)) {
    mkdir($versions_dir, 0755, true);
}

function validateVersion($version) {
    return preg_match('/^(\d+(\.\d+)*)$/', $version);
}

function handleFileUpload($file, $version) {
    global $versions_dir;

    if ($file['error'] !== UPLOAD_ERR_OK) {
        error_log("File upload error: Code=" . $file['error'], 3, __DIR__ . '/../logs/error.log');
        return ['success' => false, 'message' => 'File upload error: ' . $file['error']];
    }

    $file_name = $file['name'];
    $file_tmp = $file['tmp_name'];
    $file_ext = strtolower(pathinfo($file_name, PATHINFO_EXTENSION));

    if ($file_ext !== 'zip') {
        error_log("Invalid file extension: $file_ext", 3, __DIR__ . '/../logs/error.log');
        return ['success' => false, 'message' => 'Only ZIP files are allowed'];
    }

    $version_dir = $versions_dir . '/v' . $version;
    if (!file_exists($version_dir)) {
        mkdir($version_dir, 0755, true);
    }

    $new_file_name = 'update_' . time() . '_' . bin2hex(random_bytes(8)) . '.' . $file_ext;
    $destination = $version_dir . '/' . $new_file_name;

    if (move_uploaded_file($file_tmp, $destination)) {
        error_log("File uploaded successfully: $destination", 3, __DIR__ . '/../logs/error.log');
        return ['success' => true, 'file_path' => $new_file_name];
    } else {
        error_log("Failed to move uploaded file: $destination", 3, __DIR__ . '/../logs/error.log');
        return ['success' => false, 'message' => 'Failed to move uploaded file'];
    }
}

function deleteFile($version, $file_path) {
    global $versions_dir;
    $file = $versions_dir . '/v' . $version . '/' . $file_path;
    if (file_exists($file)) {
        unlink($file);
        error_log("Deleted file: $file", 3, __DIR__ . '/../logs/error.log');
        $version_dir = $versions_dir . '/v' . $version;
        if (is_dir($version_dir) && count(array_diff(scandir($version_dir), ['.', '..'])) === 0) {
            rmdir($version_dir);
            error_log("Removed empty directory: $version_dir", 3, __DIR__ . '/../logs/error.log');
        }
    } else {
        error_log("File not found for deletion: $file", 3, __DIR__ . '/../logs/error.log');
    }
}

try {
    $pdo = getDBConnection();
    $method = $_SERVER['REQUEST_METHOD'];

    if ($method === 'GET' && isset($_GET['action']) && $_GET['action'] === 'check_update') {
        $current_version = $_GET['version'] ?? '';
        $channel = $_GET['channel'] ?? '';
        $license = $_GET['license'] ?? '';
        $domain = $_GET['domain'] ?? '';

        if (!$current_version || !$channel || !$license || !$domain) {
            $missing_params = [];
            if (!$current_version) $missing_params[] = 'version';
            if (!$channel) $missing_params[] = 'channel';
            if (!$license) $missing_params[] = 'license';
            if (!$domain) $missing_params[] = 'domain';
            error_log("Missing parameters: " . implode(', ', $missing_params) . " - version=$current_version, channel=$channel, license=$license, domain=$domain", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['status' => 'error', 'message' => 'Missing required parameters: ' . implode(', ', $missing_params)]);
            exit;
        }

        $stmt = $pdo->prepare("SELECT COUNT(*) FROM licenses WHERE license_key = ?");
        $stmt->execute([$license]);
        if ($stmt->fetchColumn() == 0) {
            error_log("Invalid license: license=$license, domain=$domain", 3, __DIR__ . '/../logs/error.log');
            http_response_code(403);
            echo json_encode(['status' => 'error', 'message' => 'Invalid license']);
            exit;
        }

        $stmt = $pdo->prepare("SELECT version, release_date, changelog, file_path FROM updates WHERE version > ? AND status = 'released' AND channel = ? ORDER BY version DESC LIMIT 1");
        $stmt->execute([$current_version, $channel]);
        $update = $stmt->fetch(PDO::FETCH_ASSOC);

        if ($update) {
            if (empty($update['release_date']) || !strtotime($update['release_date'])) {
                error_log("Invalid release date in updates table: version={$update['version']}, release_date={$update['release_date']}", 3, __DIR__ . '/../logs/error.log');
                http_response_code(500);
                echo json_encode(['status' => 'error', 'message' => 'Invalid release date in update record']);
                exit;
            }
            $file_path = $versions_dir . '/v' . $update['version'] . '/' . $update['file_path'];
            $size = file_exists($file_path) ? round(filesize($file_path) / 1024 / 1024, 2) . ' MB' : 'Unknown';
            $response = [
                'status' => 'success',
                'update_available' => true,
                'version' => $update['version'],
                'release_date' => $update['release_date'],
                'notes' => $update['changelog'],
                'size' => $size
            ];
            error_log("Update found: " . json_encode($response), 3, __DIR__ . '/../logs/error.log');
            echo json_encode($response);
        } else {
            $response = ['status' => 'success', 'update_available' => false];
            error_log("No update found: version=$current_version, channel=$channel", 3, __DIR__ . '/../logs/error.log');
            echo json_encode($response);
        }
    } elseif ($method === 'POST' && isset($_GET['action']) && $_GET['action'] === 'download_update') {
        $version = $_POST['version'] ?? '';
        $license = $_POST['license'] ?? '';
        $domain = $_POST['domain'] ?? '';

        if (!$version || !$license || !$domain) {
            $missing_params = [];
            if (!$version) $missing_params[] = 'version';
            if (!$license) $missing_params[] = 'license';
            if (!$domain) $missing_params[] = 'domain';
            error_log("Missing parameters for download: " . implode(', ', $missing_params) . " - version=$version, license=$license, domain=$domain", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['status' => 'error', 'message' => 'Missing required parameters: ' . implode(', ', $missing_params)]);
            exit;
        }

        $stmt = $pdo->prepare("SELECT file_path FROM updates WHERE version = ? AND status = 'released'");
        $stmt->execute([$version]);
        $update = $stmt->fetch(PDO::FETCH_ASSOC);

        if (!$update) {
            error_log("Update not found: version=$version", 3, __DIR__ . '/../logs/error.log');
            http_response_code(404);
            echo json_encode(['status' => 'error', 'message' => 'Update not found']);
            exit;
        }

        $file_path = $versions_dir . '/v' . $version . '/' . $update['file_path'];
        if (!file_exists($file_path)) {
            error_log("Update file not found: $file_path", 3, __DIR__ . '/../logs/error.log');
            http_response_code(404);
            echo json_encode(['status' => 'error', 'message' => 'Update file not found']);
            exit;
        }

        if (!is_readable($file_path)) {
            error_log("Update file not readable: $file_path", 3, __DIR__ . '/../logs/error.log');
            http_response_code(500);
            echo json_encode(['status' => 'error', 'message' => 'Update file not accessible']);
            exit;
        }

        error_log("Serving update file: $file_path", 3, __DIR__ . '/../logs/error.log');
        header('Content-Type: application/zip');
        header('Content-Disposition: attachment; filename="update_' . $version . '.zip"');
        header('Content-Length: ' . filesize($file_path));
        readfile($file_path);
        exit;
    } elseif ($method === 'GET') {
        $stmt = $pdo->prepare("SELECT id, version, release_date, status, channel, file_path, changelog FROM updates ORDER BY id DESC");
        $stmt->execute();
        $updates = $stmt->fetchAll(PDO::FETCH_ASSOC);
        echo json_encode(['status' => 'success', 'data' => $updates]);
    } elseif ($method === 'POST') {
        $version = $_POST['version'] ?? '';
        $release_date = $_POST['release_date'] ?? '';
        $status = $_POST['status'] ?? '';
        $channel = $_POST['channel'] ?? '';
        $changelog = $_POST['changelog'] ?? '';
        $file = $_FILES['update_file'] ?? null;

        if (empty($version) || !validateVersion($version)) {
            error_log("Invalid version number: $version", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['status' => 'error', 'message' => 'Invalid version number']);
            exit;
        }
        if (empty($release_date) || !strtotime($release_date)) {
            error_log("Invalid release date: $release_date", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['status' => 'error', 'message' => 'Invalid release date']);
            exit;
        }
        if (!in_array($status, ['released', 'pending', 'beta'])) {
            error_log("Invalid status: $status", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['status' => 'error', 'message' => 'Invalid status']);
            exit;
        }
        if (!in_array($channel, ['stable', 'beta'])) {
            error_log("Invalid channel: $channel", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['status' => 'error', 'message' => 'Invalid channel']);
            exit;
        }
        if (!$file) {
            error_log("Update file is required", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['status' => 'error', 'message' => 'Update file is required']);
            exit;
        }

        $upload_result = handleFileUpload($file, $version);
        if (!$upload_result['success']) {
            http_response_code(400);
            echo json_encode(['status' => 'error', 'message' => $upload_result['message']]);
            exit;
        }

        $stmt = $pdo->prepare("INSERT INTO updates (version, release_date, status, channel, file_path, changelog) VALUES (?, ?, ?, ?, ?, ?)");
        $stmt->execute([$version, $release_date, $status, $channel, $upload_result['file_path'], $changelog]);

        echo json_encode(['status' => 'success', 'message' => 'Update added successfully']);
    } elseif ($method === 'PUT') {
        parse_str(file_get_contents("php://input"), $put_data);
        $id = $_GET['id'] ?? 0;
        $version = $put_data['version'] ?? '';
        $release_date = $put_data['release_date'] ?? '';
        $status = $put_data['status'] ?? '';
        $channel = $put_data['channel'] ?? '';
        $changelog = $put_data['changelog'] ?? '';
        $file = $_FILES['update_file'] ?? null;

        if (!$id) {
            error_log("Update ID is required", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['status' => 'error', 'message' => 'Update ID is required']);
            exit;
        }

        $stmt = $pdo->prepare("SELECT * FROM updates WHERE id = ?");
        $stmt->execute([$id]);
        $update = $stmt->fetch(PDO::FETCH_ASSOC);
        if (!$update) {
            error_log("Update not found: id=$id", 3, __DIR__ . '/../logs/error.log');
            http_response_code(404);
            echo json_encode(['status' => 'error', 'message' => 'Update not found']);
            exit;
        }

        if (empty($version) || !validateVersion($version)) {
            error_log("Invalid version number: $version", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['status' => 'error', 'message' => 'Invalid version number']);
            exit;
        }
        if (empty($release_date) || !strtotime($release_date)) {
            error_log("Invalid release date: $release_date", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['status' => 'error', 'message' => 'Invalid release date']);
            exit;
        }
        if (!in_array($status, ['released', 'pending', 'beta'])) {
            error_log("Invalid status: $status", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['status' => 'error', 'message' => 'Invalid status']);
            exit;
        }
        if (!in_array($channel, ['stable', 'beta'])) {
            error_log("Invalid channel: $channel", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['status' => 'error', 'message' => 'Invalid channel']);
            exit;
        }

        $old_version = $update['version'];
        $old_file_path = $update['file_path'];
        $new_file_path = $old_file_path;

        if ($file && $file['size'] > 0) {
            $upload_result = handleFileUpload($file, $version);
            if (!$upload_result['success']) {
                http_response_code(400);
                echo json_encode(['status' => 'error', 'message' => $upload_result['message']]);
                exit;
            }
            $new_file_path = $upload_result['file_path'];
            if ($version === $old_version) {
                deleteFile($old_version, $old_file_path);
            } else {
                $old_file = $versions_dir . '/v' . $old_version . '/' . $old_file_path;
                $new_file = $versions_dir . '/v' . $version . '/' . $old_file_path;
                if (file_exists($old_file)) {
                    if (!file_exists($versions_dir . '/v' . $version)) {
                        mkdir($versions_dir . '/v' . $version, 0755, true);
                    }
                    rename($old_file, $new_file);
                    error_log("Renamed file: $old_file to $new_file", 3, __DIR__ . '/../logs/error.log');
                }
                $old_version_dir = $versions_dir . '/v' . $old_version;
                if (is_dir($old_version_dir) && count(array_diff(scandir($old_version_dir), ['.', '..'])) === 0) {
                    rmdir($old_version_dir);
                    error_log("Removed empty directory: $old_version_dir", 3, __DIR__ . '/../logs/error.log');
                }
            }
        } elseif ($version !== $old_version) {
            $old_file = $versions_dir . '/v' . $old_version . '/' . $old_file_path;
            $new_file = $versions_dir . '/v' . $version . '/' . $old_file_path;
            if (file_exists($old_file)) {
                if (!file_exists($versions_dir . '/v' . $version)) {
                    mkdir($versions_dir . '/v' . $version, 0755, true);
                }
                rename($old_file, $new_file);
                error_log("Renamed file: $old_file to $new_file", 3, __DIR__ . '/../logs/error.log');
            }
            $old_version_dir = $versions_dir . '/v' . $old_version;
            if (is_dir($old_version_dir) && count(array_diff(scandir($old_version_dir), ['.', '..'])) === 0) {
                rmdir($old_version_dir);
                error_log("Removed empty directory: $old_version_dir", 3, __DIR__ . '/../logs/error.log');
            }
        }

        $stmt = $pdo->prepare("UPDATE updates SET version = ?, release_date = ?, status = ?, channel = ?, file_path = ?, changelog = ? WHERE id = ?");
        $stmt->execute([$version, $release_date, $status, $channel, $new_file_path, $changelog, $id]);

        echo json_encode(['status' => 'success', 'message' => 'Update updated successfully']);
    } elseif ($method === 'DELETE') {
        $id = $_GET['id'] ?? 0;

        if (!$id) {
            error_log("Update ID is required", 3, __DIR__ . '/../logs/error.log');
            http_response_code(400);
            echo json_encode(['status' => 'error', 'message' => 'Update ID is required']);
            exit;
        }

        $stmt = $pdo->prepare("SELECT version, file_path FROM updates WHERE id = ?");
        $stmt->execute([$id]);
        $update = $stmt->fetch(PDO::FETCH_ASSOC);
        if (!$update) {
            error_log("Update not found: id=$id", 3, __DIR__ . '/../logs/error.log');
            http_response_code(404);
            echo json_encode(['status' => 'error', 'message' => 'Update not found']);
            exit;
        }

        deleteFile($update['version'], $update['file_path']);

        $stmt = $pdo->prepare("DELETE FROM updates WHERE id = ?");
        $stmt->execute([$id]);

        echo json_encode(['status' => 'success', 'message' => 'Update deleted successfully']);
    } else {
        error_log("Invalid request method: $method", 3, __DIR__ . '/../logs/error.log');
        http_response_code(405);
        echo json_encode(['status' => 'error', 'message' => 'Method not allowed']);
    }
} catch (Exception $e) {
    error_log("Error in updates API: " . $e->getMessage(), 3, __DIR__ . '/../logs/error.log');
    http_response_code(500);
    echo json_encode(['status' => 'error', 'message' => 'Internal server error']);
}
?>