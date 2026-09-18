<?php
declare(strict_types=1);

require dirname(__DIR__) . '/lib/bootstrap.php';

$config = tezz_load_config();
date_default_timezone_set((string)($config['timezone'] ?? 'UTC'));

try {
    $pdo = tezz_pdo($config);
    if (!$pdo) {
        tezz_json([
            'ok' => false,
            'service' => 'tezznative-api',
            'db' => 'missing-config',
            'time' => date(DATE_ATOM),
        ], 500);
    }
    $dbVersion = (string)$pdo->query('SELECT VERSION()')->fetchColumn();
    tezz_json([
        'ok' => true,
        'service' => 'tezznative-api',
        'db' => 'connected',
        'db_version' => $dbVersion,
        'time' => date(DATE_ATOM),
    ]);
} catch (Throwable $e) {
    tezz_json([
        'ok' => false,
        'service' => 'tezznative-api',
        'db' => 'error',
        'error' => $e->getMessage(),
    ], 500);
}
