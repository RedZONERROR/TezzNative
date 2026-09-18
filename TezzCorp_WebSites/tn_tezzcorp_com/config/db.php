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

    // Load .env if not already loaded (nav.php already does this in production)
    $host    = $_ENV['DB_HOST']    ?? getenv('DB_HOST')    ?: 'localhost';
    $port    = (int)($_ENV['DB_PORT']    ?? getenv('DB_PORT')    ?: 3306);
    $dbname  = $_ENV['DB_NAME']    ?? getenv('DB_NAME')    ?: 'u190073748_tezz_native_db';
    $user    = $_ENV['DB_USER']    ?? getenv('DB_USER')    ?: 'u190073748_tezz_native_ur';
    $pass    = $_ENV['DB_PASS']    ?? getenv('DB_PASS')    ?: 'TezzNativeByRohit@9608';
    $charset = $_ENV['DB_CHARSET'] ?? getenv('DB_CHARSET') ?: 'utf8mb4';

    $dsn = "mysql:host={$host};port={$port};dbname={$dbname};charset={$charset}";
    $options = [
        PDO::ATTR_ERRMODE            => PDO::ERRMODE_EXCEPTION,
        PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
        PDO::ATTR_EMULATE_PREPARES   => false,
        PDO::MYSQL_ATTR_INIT_COMMAND => "SET NAMES {$charset} COLLATE utf8mb4_unicode_ci, time_zone = '+05:30'",
    ];

    $pdo = new PDO($dsn, $user, $pass, $options);
    return $pdo;
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
