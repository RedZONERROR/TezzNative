<?php
$t0 = microtime(true);
$steps = [];
function ms() { global $t0; return round((microtime(true)-$t0)*1000); }

header('Content-Type: application/json');
ob_start();

$steps[] = ['step' => 'php_started', 'ms' => ms()];
$steps[] = ['step' => 'php_version=' . PHP_VERSION, 'ms' => ms()];
$steps[] = ['step' => 'sapi=' . php_sapi_name(), 'ms' => ms()];
$steps[] = ['step' => 'opcache_enabled=' . (function_exists('opcache_get_status') ? var_export(opcache_get_status(false)['opcache_enabled'] ?? false, true) : 'N/A'), 'ms' => ms()];
$steps[] = ['step' => 'validate_timestamps=' . ini_get('opcache.validate_timestamps'), 'ms' => ms()];
$steps[] = ['step' => 'revalidate_freq=' . ini_get('opcache.revalidate_freq'), 'ms' => ms()];

// DB connect
try {
    $pdo = new PDO(
        'mysql:host=localhost;dbname=u190073748_academy;charset=utf8mb4',
        'u190073748_academy', 'TezzErp@2026',
        [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION, PDO::ATTR_TIMEOUT => 5]
    );
    $steps[] = ['step' => 'db_connect_ok', 'ms' => ms()];

    // Query setting
    $r = $pdo->query("SELECT key_value FROM settings WHERE key_name='email_otp_enabled' LIMIT 1");
    $row = $r->fetch();
    $steps[] = ['step' => 'setting_otp=' . ($row ? $row['key_value'] : 'NULL'), 'ms' => ms()];

    // Query admin
    $r2 = $pdo->prepare("SELECT password FROM admins WHERE username='rohit.dev' LIMIT 1");
    $r2->execute();
    $a = $r2->fetch();
    $steps[] = ['step' => 'admin_fetched hash_prefix=' . substr($a['password'] ?? 'NONE', 0, 10), 'ms' => ms()];

    // Password verify
    $ok = password_verify('Admin@123', $a['password'] ?? '');
    $steps[] = ['step' => 'password_verify=' . ($ok?'PASS':'FAIL'), 'ms' => ms()];

} catch (Exception $e) {
    $steps[] = ['step' => 'DB_ERROR: ' . $e->getMessage(), 'ms' => ms()];
}

// OpenSSL
$raw = openssl_random_pseudo_bytes(4);
$steps[] = ['step' => 'openssl_random_ok', 'ms' => ms()];

// File write
@file_put_contents('/tmp/diag_test_' . time() . '.txt', 'test');
$steps[] = ['step' => 'file_write_tmp', 'ms' => ms()];

$logsDir = dirname(__FILE__) . '/academy.tezzcorp.in/logs';
@mkdir($logsDir, 0755, true);
@file_put_contents($logsDir . '/diag.txt', date('[Y-m-d H:i:s]') . " diagnostic\n", FILE_APPEND);
$steps[] = ['step' => 'file_write_logs', 'ms' => ms()];

echo json_encode(['ok' => true, 'steps' => $steps], JSON_PRETTY_PRINT);
