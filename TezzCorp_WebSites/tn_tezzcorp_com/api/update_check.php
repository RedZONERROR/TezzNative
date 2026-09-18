<?php
declare(strict_types=1);

require dirname(__DIR__) . '/lib/bootstrap.php';

$config = tezz_load_config();
tezz_configure_timezone($config);

$platform = tezz_clean_text((string)($_GET['platform'] ?? $_POST['platform'] ?? ''), 80);
$current = tezz_clean_text((string)($_GET['version'] ?? $_GET['current_version'] ?? $_POST['version'] ?? $_POST['current_version'] ?? ''), 60);
$mode = tezz_clean_text((string)($_GET['mode'] ?? $_POST['mode'] ?? 'check'), 40);
$installId = tezz_clean_text((string)($_GET['install_id'] ?? $_POST['install_id'] ?? ''), 120);

$payload = tezz_update_payload($config, $platform, $current, $mode);

try {
    $pdo = tezz_pdo($config);
    tezz_record_update_request($pdo, $config, $payload, $installId);
} catch (Throwable $_e) {
}

tezz_json([
    'ok' => true,
    'data' => $payload,
]);
