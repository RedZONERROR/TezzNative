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
    $result = tezz_submit_error_report($pdo, [
        'user_name' => (string)($_POST['user_name'] ?? ''),
        'email' => (string)($_POST['email'] ?? ''),
        'platform' => (string)($_POST['platform'] ?? ''),
        'cli_version' => (string)($_POST['cli_version'] ?? ''),
        'command_text' => (string)($_POST['command_text'] ?? ''),
        'error_text' => (string)($_POST['error_text'] ?? ''),
        'source' => (string)($_POST['source'] ?? 'website'),
    ]);
    tezz_json($result, $result['ok'] ? 200 : 400);
} catch (Throwable $e) {
    tezz_json([
        'ok' => false,
        'error' => $e->getMessage(),
    ], 500);
}
