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
    $result = tezz_record_install_event($pdo, $config, [
        'platform' => (string)($_POST['platform'] ?? ''),
        'version' => (string)($_POST['version'] ?? ''),
        'status' => (string)($_POST['status'] ?? ''),
        'install_id' => (string)($_POST['install_id'] ?? ''),
        'message' => (string)($_POST['message'] ?? ''),
    ]);
    tezz_json($result, $result['ok'] ? 200 : 400);
} catch (Throwable $e) {
    tezz_json([
        'ok' => false,
        'error' => $e->getMessage(),
    ], 500);
}
