<?php
declare(strict_types=1);

/**
 * config/db.php — TezzNative Language Portal Database Layer
 * Returns a PDO singleton backed by .env credentials (falls back to hardcoded).
 * Also provides site-level config constants for legacy includes.
 */

// ── Lazy PDO singleton ────────────────────────────────────────────────────────
function tn_pdo(): PDO {
    static $pdo = null;
    if ($pdo !== null) return $pdo;

    $candidates = [
        [
            'host'    => $_ENV['TN_DB_HOST'] ?? 'localhost',
            'port'    => (int)($_ENV['TN_DB_PORT'] ?? 3306),
            'dbname'  => $_ENV['TN_DB_NAME'] ?? 'u190073748_tezz_native_db',
            'user'    => $_ENV['TN_DB_USER'] ?? 'u190073748_tezz_native_ur',
            'pass'    => $_ENV['TN_DB_PASS'] ?? 'TezzNativeByRohit@9608',
            'charset' => 'utf8mb4',
        ],
        [
            'host'    => $_ENV['DB_HOST'] ?? 'localhost',
            'port'    => (int)($_ENV['DB_PORT'] ?? 3306),
            'dbname'  => $_ENV['DB_NAME'] ?? 'u190073748_tezz_official_',
            'user'    => $_ENV['DB_USER'] ?? 'u190073748_tezz_usr__vr__',
            'pass'    => $_ENV['DB_PASS'] ?? 'TezzCorpOfficial#@2026',
            'charset' => 'utf8mb4',
        ],
    ];

    $options = [
        PDO::ATTR_ERRMODE            => PDO::ERRMODE_EXCEPTION,
        PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
        PDO::ATTR_EMULATE_PREPARES   => false,
        PDO::MYSQL_ATTR_INIT_COMMAND => "SET NAMES utf8mb4 COLLATE utf8mb4_unicode_ci, time_zone = '+05:30'",
    ];

    $lastEx = null;
    foreach ($candidates as $c) {
        try {
            $dsn = "mysql:host={$c['host']};port={$c['port']};dbname={$c['dbname']};charset={$c['charset']}";
            $pdo = new PDO($dsn, $c['user'], $c['pass'], $options);
            return $pdo;
        } catch (Throwable $e) {
            $lastEx = $e;
        }
    }

    if ($lastEx !== null) {
        throw $lastEx;
    }
    throw new RuntimeException("Unable to connect to database");
}

// ── Legacy array for includes that do `$cfg = require 'db.php'` ──────────────
return [
    'site_name'      => 'TezzNative Language',
    'site_url'       => 'https://tn.tezzcorp.com/',
    'db_host'        => $_ENV['DB_HOST']    ?? 'localhost',
    'db_name'        => $_ENV['DB_NAME']    ?? 'u190073748_tezz_native_db',
    'db_user'        => $_ENV['DB_USER']    ?? 'u190073748_tezz_native_ur',
    'db_pass'        => $_ENV['DB_PASS']    ?? 'TezzNativeByRohit@9608',
    'timezone'       => 'Asia/Kolkata',
    'telemetry_salt' => '02f1e071629feebe917f2497d219acadb9cfbc89f4201566',
    'admin_user'     => 'admin',
    'admin_token'    => 'tezz-HvQtV0eefyCNQiTOyEeNOJ_J',
    'admin_email'    => 'admin@tezzcorp.com',
    'admin_password' => 'tezz-HvQtV0eefyCNQiTOyEeNOJ_J',
];
