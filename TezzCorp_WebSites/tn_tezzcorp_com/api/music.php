<?php
session_start();
header('Content-Type: application/json');
require_once '../config/config.php';

// Validate CSRF token
// if (!isset($_SERVER['HTTP_X_CSRF_TOKEN']) || !validateCsrfToken($_SERVER['HTTP_X_CSRF_TOKEN'])) {
//     echo json_encode(['success' => false, 'message' => 'Invalid CSRF token']);
//     exit;
// }

// Check if user is authenticated
if (!isset($_SESSION['user_id']) || !isset($_SESSION['user_type'])) {
    echo json_encode(['success' => false, 'message' => 'Unauthorized']);
    exit;
}

// Configuration: Set to true if Hostinger blocks MP3 files
const USE_PNG_EXTENSION = false; // Change to true if MP3 files are blocked

try {
    $pdo = getDBConnection();
    $method = $_SERVER['REQUEST_METHOD'];

    if ($method === 'GET') {
        $stmt = $pdo->prepare("SELECT * FROM music");
        $stmt->execute();
        $musicList = $stmt->fetchAll(PDO::FETCH_ASSOC);

        if (empty($musicList)) {
            echo json_encode(['success' => true, 'tracks' => [], 'message' => 'No music files found']);
        } else {
            echo json_encode(['success' => true, 'tracks' => $musicList]);
        }
    } elseif ($method === 'POST') {
        $title = isset($_POST['title']) ? trim($_POST['title']) : 'Untitled';
        $artist = isset($_POST['artist']) ? trim($_POST['artist']) : 'Unknown Artist';
        $duration = isset($_POST['duration']) ? trim($_POST['duration']) : '0:00';

        // Handle music file upload
        $file_path = '';
        if (isset($_FILES['music_file']) && $_FILES['music_file']['error'] === UPLOAD_ERR_OK) {
            $upload_dir = __DIR__ . '/../crm/audio/';
            if (!is_dir($upload_dir)) {
                mkdir($upload_dir, 0755, true);
            }

            // Validate file type
            $finfo = finfo_open(FILEINFO_MIME_TYPE);
            $mime_type = finfo_file($finfo, $_FILES['music_file']['tmp_name']);
            finfo_close($finfo);
            $allowed_types = ['audio/mpeg', 'audio/wav', 'audio/ogg'];
            if (!in_array($mime_type, $allowed_types)) {
                echo json_encode(['success' => false, 'message' => 'Invalid audio file format. Supported formats: MP3, WAV, OGG']);
                exit;
            }

            $extension = USE_PNG_EXTENSION ? 'png' : pathinfo($_FILES['music_file']['name'], PATHINFO_EXTENSION);
            $new_filename = uniqid('music_') . '.' . $extension;
            $destination = $upload_dir . $new_filename;

            if (move_uploaded_file($_FILES['music_file']['tmp_name'], $destination)) {
                $file_path = '/crm/audio/' . $new_filename;
            } else {
                echo json_encode(['success' => false, 'message' => 'Failed to upload music file']);
                exit;
            }
        } else {
            echo json_encode(['success' => false, 'message' => 'Music file is required']);
            exit;
        }

        // Handle thumbnail (either URL or uploaded file)
        $thumbnail_url = '';
        if (isset($_POST['thumbnail_url']) && !empty(trim($_POST['thumbnail_url']))) {
            $thumbnail_url = trim($_POST['thumbnail_url']);
        } elseif (isset($_FILES['thumbnail_file']) && $_FILES['thumbnail_file']['error'] === UPLOAD_ERR_OK) {
            $upload_dir = __DIR__ . '/../uploads/thumbnails/';
            if (!is_dir($upload_dir)) {
                mkdir($upload_dir, 0755, true);
            }

            $file_extension = pathinfo($_FILES['thumbnail_file']['name'], PATHINFO_EXTENSION);
            $new_filename = uniqid('thumb_') . '.' . $file_extension;
            $destination = $upload_dir . $new_filename;

            if (move_uploaded_file($_FILES['thumbnail_file']['tmp_name'], $destination)) {
                $thumbnail_url = '/uploads/thumbnails/' . $new_filename;
            } else {
                echo json_encode(['success' => false, 'message' => 'Failed to upload thumbnail']);
                exit;
            }
        } else {
            $thumbnail_url = 'https://picsum.photos/48/48?random=' . rand(1, 100);
        }

        if (!empty($thumbnail_url) && !filter_var($thumbnail_url, FILTER_VALIDATE_URL) && !file_exists(__DIR__ . '/../' . $thumbnail_url)) {
            echo json_encode(['success' => false, 'message' => 'Invalid thumbnail URL']);
            exit;
        }

        $stmt = $pdo->prepare("
            INSERT INTO music (title, artist, file_path, thumbnail_url, duration)
            VALUES (?, ?, ?, ?, ?)
        ");
        $stmt->execute([$title, $artist, $file_path, $thumbnail_url, $duration]);

        if ($stmt->rowCount() > 0) {
            echo json_encode(['success' => true, 'message' => 'Music added successfully']);
        } else {
            echo json_encode(['success' => false, 'message' => 'Failed to add music']);
        }
    } elseif ($method === 'PUT') {
        // Parse the raw input for PUT request
        $putData = [];
        parse_str(file_get_contents("php://input"), $putData);

        $musicId = isset($putData['music_id']) ? intval($putData['music_id']) : 0;
        $title = isset($putData['title']) ? trim($putData['title']) : 'Untitled';
        $artist = isset($putData['artist']) ? trim($putData['artist']) : 'Unknown Artist';
        $duration = isset($putData['duration']) ? trim($putData['duration']) : '0:00';
        $existing_music_file = isset($putData['existing_music_file']) ? trim($putData['existing_music_file']) : '';
        $existing_thumbnail_url = isset($putData['existing_thumbnail_url']) ? trim($putData['existing_thumbnail_url']) : '';

        if ($musicId <= 0) {
            echo json_encode(['success' => false, 'message' => 'Music ID is required']);
            exit;
        }

        // Handle music file (keep existing if no new file is uploaded)
        $file_path = $existing_music_file;
        if (isset($_FILES['music_file']) && $_FILES['music_file']['error'] === UPLOAD_ERR_OK) {
            $upload_dir = __DIR__ . '/../crm/audio/';
            if (!is_dir($upload_dir)) {
                mkdir($upload_dir, 0755, true);
            }

            // Validate file type
            $finfo = finfo_open(FILEINFO_MIME_TYPE);
            $mime_type = finfo_file($finfo, $_FILES['music_file']['tmp_name']);
            finfo_close($finfo);
            $allowed_types = ['audio/mpeg', 'audio/wav', 'audio/ogg'];
            if (!in_array($mime_type, $allowed_types)) {
                echo json_encode(['success' => false, 'message' => 'Invalid audio file format. Supported formats: MP3, WAV, OGG']);
                exit;
            }

            $extension = USE_PNG_EXTENSION ? 'png' : pathinfo($_FILES['music_file']['name'], PATHINFO_EXTENSION);
            $new_filename = uniqid('music_') . '.' . $extension;
            $destination = $upload_dir . $new_filename;

            if (move_uploaded_file($_FILES['music_file']['tmp_name'], $destination)) {
                $file_path = '/crm/audio/' . $new_filename;

                // Delete old music file if it exists
                if ($existing_music_file && file_exists(__DIR__ . '/../' . $existing_music_file)) {
                    unlink(__DIR__ . '/../' . $existing_music_file);
                }
            } else {
                echo json_encode(['success' => false, 'message' => 'Failed to upload music file']);
                exit;
            }
        }

        // Handle thumbnail (either URL, uploaded file, or keep existing)
        $thumbnail_url = $existing_thumbnail_url;
        if (isset($putData['thumbnail_url']) && !empty(trim($putData['thumbnail_url']))) {
            $thumbnail_url = trim($putData['thumbnail_url']);
        } elseif (isset($_FILES['thumbnail_file']) && $_FILES['thumbnail_file']['error'] === UPLOAD_ERR_OK) {
            $upload_dir = __DIR__ . '/../uploads/thumbnails/';
            if (!is_dir($upload_dir)) {
                mkdir($upload_dir, 0755, true);
            }

            $file_extension = pathinfo($_FILES['thumbnail_file']['name'], PATHINFO_EXTENSION);
            $new_filename = uniqid('thumb_') . '.' . $file_extension;
            $destination = $upload_dir . $new_filename;

            if (move_uploaded_file($_FILES['thumbnail_file']['tmp_name'], $destination)) {
                $thumbnail_url = '/uploads/thumbnails/' . $new_filename;

                // Delete old thumbnail if it was uploaded
                if ($existing_thumbnail_url && strpos($existing_thumbnail_url, '/uploads/thumbnails/') === 0) {
                    $old_thumbnail_path = __DIR__ . '/../' . $existing_thumbnail_url;
                    if (file_exists($old_thumbnail_path)) {
                        unlink($old_thumbnail_path);
                    }
                }
            } else {
                echo json_encode(['success' => false, 'message' => 'Failed to upload thumbnail']);
                exit;
            }
        }

        if (!empty($thumbnail_url) && !filter_var($thumbnail_url, FILTER_VALIDATE_URL) && !file_exists(__DIR__ . '/../' . $thumbnail_url)) {
            echo json_encode(['success' => false, 'message' => 'Invalid thumbnail URL']);
            exit;
        }

        $stmt = $pdo->prepare("
            UPDATE music
            SET title = ?, artist = ?, file_path = ?, thumbnail_url = ?, duration = ?
            WHERE id = ?
        ");
        $stmt->execute([$title, $artist, $file_path, $thumbnail_url, $duration, $musicId]);

        if ($stmt->rowCount() >= 0) { // rowCount might be 0 if no changes are made
            echo json_encode(['success' => true, 'message' => 'Music updated successfully']);
        } else {
            echo json_encode(['success' => false, 'message' => 'Failed to update music']);
        }
    } elseif ($method === 'DELETE') {
        if (!isset($_GET['id']) || empty(trim($_GET['id']))) {
            error_log("DELETE: Music ID missing in request", 3, __DIR__ . '/../logs/error.log');
            echo json_encode(['success' => false, 'message' => 'Music ID is required']);
            exit;
        }

        $musicId = intval($_GET['id']);
        $stmt = $pdo->prepare("SELECT file_path, thumbnail_url FROM music WHERE id = ?");
        $stmt->execute([$musicId]);
        $music = $stmt->fetch(PDO::FETCH_ASSOC);

        if ($music) {
            // Delete music file
            if ($music['file_path'] && file_exists(__DIR__ . '/../' . $music['file_path'])) {
                unlink(__DIR__ . '/../' . $music['file_path']);
            }

            // Delete thumbnail file if it was uploaded
            if (strpos($music['thumbnail_url'], '/uploads/thumbnails/') === 0) {
                $thumbnail_path = __DIR__ . '/../' . $music['thumbnail_url'];
                if (file_exists($thumbnail_path)) {
                    unlink($thumbnail_path);
                }
            }

            $stmt = $pdo->prepare("DELETE FROM music WHERE id = ?");
            $stmt->execute([$musicId]);

            if ($stmt->rowCount() > 0) {
                echo json_encode(['success' => true, 'message' => 'Music deleted successfully']);
            } else {
                echo json_encode(['success' => false, 'message' => 'Failed to delete music']);
            }
        } else {
            echo json_encode(['success' => false, 'message' => 'Music not found']);
        }
    } else {
        echo json_encode(['success' => false, 'message' => 'Invalid request method']);
    }
} catch (Exception $e) {
    error_log("Error in music.php: " . $e->getMessage(), 3, __DIR__ . '/../logs/error.log');
    echo json_encode(['success' => false, 'message' => 'An error occurred: ' . $e->getMessage()]);
}
?>