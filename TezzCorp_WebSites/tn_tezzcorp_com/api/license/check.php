<?php
require_once __DIR__ . '/../../config/config.php';

$apiClient = requireApiClient();

// accept JSON or form-data
$raw = file_get_contents('php://input');
$input = [];
if ($raw && str_contains($_SERVER['CONTENT_TYPE'] ?? '', 'application/json')) {
    $input = json_decode($raw, true) ?: [];
} else {
    $input = $_POST;
}

$result = checkLicense($input, $apiClient);
jsonResponse(['ok' => true] + $result);
?>