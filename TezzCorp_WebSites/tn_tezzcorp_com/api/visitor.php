<?php
declare(strict_types=1);

require dirname(__DIR__) . '/lib/bootstrap.php';

$config = tezz_load_config();
tezz_configure_timezone($config);

try {
    $pdo = tezz_pdo($config);
    $path = tezz_clean_text((string)($_GET['path'] ?? '/'), 200);
    $stats = tezz_track_visit($pdo, $config, $path !== '' ? $path : '/');
    tezz_json([
        'ok' => true,
        'data' => $stats,
    ]);
} catch (Throwable $e) {
    tezz_json([
        'ok' => false,
        'error' => $e->getMessage(),
    ], 500);
}
