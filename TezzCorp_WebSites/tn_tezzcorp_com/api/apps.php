<?php
declare(strict_types=1);

require dirname(__DIR__) . '/lib/bootstrap.php';

$config = tezz_load_config();

try {
    $pdo = tezz_pdo($config);
    if (!$pdo) {
        tezz_json([
            'ok' => true,
            'source' => 'fallback',
            'apps' => [
                ['name' => 'TezzNative Compiler', 'status' => 'ready'],
                ['name' => 'TezzOS Builder', 'status' => 'ready'],
                ['name' => 'TezzDB', 'status' => 'ready'],
            ],
        ]);
    }

    $pdo->exec(
        'CREATE TABLE IF NOT EXISTS tn_apps (
            id INT AUTO_INCREMENT PRIMARY KEY,
            name VARCHAR(120) NOT NULL,
            status VARCHAR(40) NOT NULL DEFAULT "ready",
            updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4'
    );

    $count = (int)$pdo->query('SELECT COUNT(*) FROM tn_apps')->fetchColumn();
    if ($count === 0) {
        $stmt = $pdo->prepare('INSERT INTO tn_apps (name, status) VALUES (?, ?)');
        $stmt->execute(['TezzNative Compiler', 'ready']);
        $stmt->execute(['TezzOS Builder', 'ready']);
        $stmt->execute(['TezzDB', 'ready']);
    }

    $rows = $pdo->query('SELECT id, name, status, updated_at FROM tn_apps ORDER BY id')->fetchAll();
    tezz_json([
        'ok' => true,
        'source' => 'mysql',
        'apps' => $rows,
    ]);
} catch (Throwable $e) {
    tezz_json([
        'ok' => false,
        'error' => $e->getMessage(),
    ], 500);
}
