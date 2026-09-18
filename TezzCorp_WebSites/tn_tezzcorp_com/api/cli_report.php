<?php
declare(strict_types=1);

require dirname(__DIR__) . '/lib/bootstrap.php';

$config = tezz_load_config();
tezz_configure_timezone($config);

if (strtoupper((string)($_SERVER['REQUEST_METHOD'] ?? 'GET')) !== 'POST') {
    tezz_json([
        'ok' => false,
        'error' => 'POST required',
    ], 405);
}

try {
    $pdo = tezz_pdo($config);
    $result = tezz_submit_cli_report($pdo, [
        'user_name' => (string)($_POST['user_name'] ?? ''),
        'email' => (string)($_POST['email'] ?? ''),
        'cli_version' => (string)($_POST['cli_version'] ?? ''),
        'platform' => (string)($_POST['platform'] ?? ''),
        'doctor_status' => (string)($_POST['doctor_status'] ?? ''),
        'test_mode' => (string)($_POST['test_mode'] ?? ''),
        'test_status' => (string)($_POST['test_status'] ?? ''),
        'output_excerpt' => (string)($_POST['output_excerpt'] ?? ''),
    ]);
    tezz_json($result, $result['ok'] ? 200 : 400);
} catch (Throwable $e) {
    tezz_json([
        'ok' => false,
        'error' => $e->getMessage(),
    ], 500);
}
