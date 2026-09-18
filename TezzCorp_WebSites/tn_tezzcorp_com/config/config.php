<?php
declare(strict_types=1);

// =====================================================
// TezzCorp — Shared Configuration
// Actual FTP/filesystem structure:
//   /  (account root = FTP root)
//   ├── .env                    ← environment file
//   ├── config/
//   │     └── config.php        ← THIS FILE (__DIR__ is here)
//   ├── edu.tezzcorp.com/       ← EDU pages (require '../config/config.php')
//   ├── crm.tezzcorp.com/       ← legacy CRM pages
//   ├── logs/
//   └── …
//
// .env = dirname(__DIR__) . '/.env'
// =====================================================

// ══════════════════════════════════════════
// 1.  .env PARSER
// ══════════════════════════════════════════
(static function (): void {
    // .env is one directory above config/ (at the account root)
    $candidates = [
        dirname(__DIR__) . DIRECTORY_SEPARATOR . '.env',           // CORRECT for production
        dirname(dirname(__DIR__)) . DIRECTORY_SEPARATOR . '.env',  // fallback: one more level up
    ];

    $envFile = null;
    foreach ($candidates as $try) {
        if (is_file($try) && is_readable($try)) {
            $envFile = $try;
            break;
        }
    }

    if ($envFile === null) {
        return; // use hardcoded fallback values in the define() calls below
    }

    $lines = @file($envFile, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES) ?: [];
    foreach ($lines as $raw) {
        $line = trim($raw);
        // skip blanks and comments
        if ($line === '' || $line[0] === '#') {
            continue;
        }
        $eqPos = strpos($line, '=');
        if ($eqPos === false) {
            continue;
        }
        $key = trim(substr($line, 0, $eqPos));
        $val = trim(substr($line, $eqPos + 1));
        // strip surrounding single or double quotes
        $len = strlen($val);
        if ($len >= 2 && (
            ($val[0] === '"'  && $val[$len - 1] === '"') ||
            ($val[0] === "'"  && $val[$len - 1] === "'")
        )) {
            $val = substr($val, 1, $len - 2);
        }
        if ($key !== '' && !isset($_ENV[$key])) {
            $_ENV[$key]    = $val;
            $_SERVER[$key] = $val;
            putenv($key . '=' . $val);
        }
    }
})();


// ══════════════════════════════════════════
// 2.  env() helper  (must be before use)
// ══════════════════════════════════════════
function env(string $key, string $default = ''): string {
    $val = $_ENV[$key] ?? $_SERVER[$key] ?? null;
    if ($val === null) {
        $g = getenv($key);
        $val = ($g !== false) ? $g : null;
    }
    return ($val !== null) ? (string)$val : $default;
}


// ══════════════════════════════════════════
// 3.  ENVIRONMENT DETECTION
// ══════════════════════════════════════════
$_host    = strtolower($_SERVER['HTTP_HOST'] ?? $_SERVER['SERVER_NAME'] ?? '');
$_isLocal = (
    $_host === 'localhost' ||
    $_host === '127.0.0.1' ||
    $_host === '::1' ||
    substr($_host, -5) === '.test' ||
    substr($_host, -6) === '.local' ||
    strpos($_host, 'localhost') !== false
);
$_isEduHost = (
    $_host === 'edu.tezzcorp.com' ||
    (str_ends_with($_host, '.tezzcorp.com') && str_starts_with($_host, 'edu.'))
);
$_isCrmHost = (
    $_host === 'crm.tezzcorp.com' ||
    (str_ends_with($_host, '.tezzcorp.com') && str_starts_with($_host, 'crm.'))
);
define('IS_LOCAL_HOST', $_isLocal);
define('IS_EDU_HOST', $_isEduHost);
define('IS_CRM_HOST', $_isCrmHost);


// ══════════════════════════════════════════
// 4.  DATABASE
// ══════════════════════════════════════════
if ($_isEduHost) {
    define('DB_HOST', env('EDU_DB_HOST', 'localhost'));
    define('DB_PORT', env('EDU_DB_PORT', '3306'));
    define('DB_NAME', env('EDU_DB_NAME', 'u190073748_edu_manage_sdb'));
    define('DB_USER', env('EDU_DB_USER', 'u190073748_edu_manage_usr'));
    define('DB_PASS', env('EDU_DB_PASS', 'SchoolEducationByTezzCorp@2026'));
} else {
    define('DB_HOST', env('CRM_DB_HOST', env('DB_HOST', $_isLocal ? 'localhost' : 'localhost')));
    define('DB_PORT', env('CRM_DB_PORT', env('DB_PORT', '3306')));
    define('DB_NAME', env('CRM_DB_NAME', env('DB_NAME', $_isLocal ? 'tezzcorp_crm' : 'u190073748_tezz_official_')));
    define('DB_USER', env('CRM_DB_USER', env('DB_USER', $_isLocal ? 'root' : 'u190073748_tezz_official_')));
    define('DB_PASS', env('CRM_DB_PASS', env('DB_PASS', $_isLocal ? '' : '')));
}
define('DB_CHARSET',   env('DB_CHARSET',   'utf8mb4'));
define('DB_COLLATION', env('DB_COLLATION', 'utf8mb4_unicode_ci'));


// ══════════════════════════════════════════
// 5.  URLS & PATHS
// ══════════════════════════════════════════
if ($_isLocal) {
    define('APP_URL', 'http://localhost/edu');
} elseif ($_isEduHost) {
    define('APP_URL', 'https://edu.tezzcorp.com');
} elseif ($_isCrmHost) {
    define('APP_URL', 'https://crm.tezzcorp.com');
} else {
    define('APP_URL', env('APP_URL', 'https://edu.tezzcorp.com'));
}
define('CRM_URL',  APP_URL);
define('SITE_URL', $_isLocal ? 'http://localhost'         : env('APP_URL', 'https://www.tezzcorp.com'));

define('ROOT_PATH',   dirname(__DIR__));  // account root (one above config/)
define('CONFIG_PATH', __DIR__);

// logs/ is at the account root (beside config/)
$_logPath = env('LOG_PATH', '');
if ($_logPath === '') {
    $_logPath = $_isLocal
        ? dirname(__DIR__) . '/logs'
        : dirname(__DIR__) . '/logs';    // same on production: /logs beside config/
}
define('LOG_PATH', $_logPath);
unset($_logPath);


// ══════════════════════════════════════════
// 6.  SECURITY & MAILER CONSTANTS
// ══════════════════════════════════════════
define('APP_SECRET',      env('APP_SECRET',  '9f3c0d2a7b6e4d1c8a0f5e2b9c6a1d7e3f8b0a5c2d7e9f1a6b3c8d0e2f4a7c1b'));
define('CSRF_SALT',       'TezzCRM_CSRF_a8vR3nW7kJ2!');
if ($_isEduHost) {
    define('SESSION_NAME', env('EDU_SESSION_NAME', 'tezzcorp_edu_sess'));
} elseif ($_isCrmHost) {
    define('SESSION_NAME', env('CRM_SESSION_NAME', 'tezzcorp_crm_sess'));
} else {
    define('SESSION_NAME', env('SESSION_NAME', 'tezzcorp_sid'));
}
define('SMTP_HOST',       env('SMTP_HOST',    'smtp.hostinger.com'));
define('SMTP_PORT',       env('SMTP_PORT',    '587'));
define('SMTP_USER',       env('SMTP_USERNAME', env('SMTP_USER', '')));
define('SMTP_PASS',       env('SMTP_PASSWORD', env('SMTP_PASS', '')));
define('SMTP_SECURE',     env('SMTP_SECURE',  'tls'));
define('EMAIL_FROM',      env('EMAIL_FROM',   'info@tezzcorp.com'));
define('EMAIL_FROM_NAME', env('EMAIL_FROM_NAME', 'TezzCorp'));


// ══════════════════════════════════════════
// 7.  SESSION
// ══════════════════════════════════════════
if (session_status() === PHP_SESSION_NONE) {
    $cookieDomain = $_isLocal ? '' : env('COOKIE_DOMAIN', '.tezzcorp.com');
    // Use individual session ini settings for maximum PHP version compatibility
    session_name(SESSION_NAME);
    @ini_set('session.cookie_lifetime',  '0');
    @ini_set('session.cookie_path',      '/');
    @ini_set('session.cookie_domain',    $cookieDomain);
    @ini_set('session.cookie_secure',    $_isLocal ? '0' : '1');
    @ini_set('session.cookie_httponly',  '1');
    @ini_set('session.cookie_samesite',  'Lax');
    @ini_set('session.use_strict_mode',  '1');
    @ini_set('session.gc_maxlifetime',   '7200');
    session_start();
    unset($cookieDomain);
}


// ══════════════════════════════════════════
// 8.  SECURITY HEADERS
// ══════════════════════════════════════════
if (!headers_sent()) {
    header('X-Content-Type-Options: nosniff');
    header('X-Frame-Options: SAMEORIGIN');
    header('X-XSS-Protection: 1; mode=block');
    header('Referrer-Policy: strict-origin-when-cross-origin');
    if (!$_isLocal) {
        header('Strict-Transport-Security: max-age=31536000; includeSubDomains');
    }
}


// ══════════════════════════════════════════
// 9.  PDO DATABASE CONNECTION
// ══════════════════════════════════════════
function getDBConnection(): PDO {
    static $pdo = null;
    if ($pdo !== null) return $pdo;
    $dsn = 'mysql:host=' . DB_HOST . ';port=' . DB_PORT . ';dbname=' . DB_NAME . ';charset=' . DB_CHARSET;
    $pdo = new PDO($dsn, DB_USER, DB_PASS, [
        PDO::ATTR_ERRMODE            => PDO::ERRMODE_EXCEPTION,
        PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
        PDO::ATTR_EMULATE_PREPARES   => true,
        PDO::ATTR_PERSISTENT         => false,
    ]);
    try {
        $pdo->exec("SET NAMES utf8mb4 COLLATE utf8mb4_unicode_ci");
        $pdo->exec("SET collation_connection = 'utf8mb4_unicode_ci'");
    } catch (Throwable $ignore) {}
    if (defined('IS_EDU_HOST') && IS_EDU_HOST) {
        try {
            eduEnsureCoreSchema($pdo);
        } catch (Throwable $schemaError) {
            error_log('[EDU schema bootstrap] ' . $schemaError->getMessage());
        }
    }
    return $pdo;
}
function db(): PDO { return getDBConnection(); }

function eduSchemaTableExists(PDO $pdo, string $table): bool {
    try {
        $st = $pdo->prepare(
            "SELECT 1
             FROM information_schema.TABLES
             WHERE TABLE_SCHEMA = DATABASE()
               AND TABLE_NAME = :t
             LIMIT 1"
        );
        $st->execute([':t' => $table]);
        return (int)$st->fetchColumn() === 1;
    } catch (Throwable $e) {
        return false;
    }
}

function eduSchemaColumnMap(PDO $pdo, string $table): array {
    $out = [];
    try {
        $st = $pdo->prepare(
            "SELECT COLUMN_NAME
             FROM information_schema.COLUMNS
             WHERE TABLE_SCHEMA = DATABASE()
               AND TABLE_NAME = :t"
        );
        $st->execute([':t' => $table]);
        $rows = $st->fetchAll(PDO::FETCH_COLUMN) ?: [];
        foreach ($rows as $name) {
            $key = strtolower(trim((string)$name));
            if ($key !== '') {
                $out[$key] = (string)$name;
            }
        }
    } catch (Throwable $e) {
        $out = [];
    }
    return $out;
}

function eduSchemaIndexExists(PDO $pdo, string $table, string $indexName): bool {
    try {
        $st = $pdo->prepare(
            "SELECT 1
             FROM information_schema.STATISTICS
             WHERE TABLE_SCHEMA = DATABASE()
               AND TABLE_NAME = :t
               AND INDEX_NAME = :i
             LIMIT 1"
        );
        $st->execute([':t' => $table, ':i' => $indexName]);
        return (int)$st->fetchColumn() === 1;
    } catch (Throwable $e) {
        return false;
    }
}

function eduEnsureUsersTable(PDO $pdo): void {
    if (!eduSchemaTableExists($pdo, 'users')) {
        $pdo->exec(
            "CREATE TABLE `users` (
                `id` char(32) NOT NULL,
                `organization_id` char(32) DEFAULT NULL,
                `uid` varchar(50) DEFAULT NULL,
                `email` varchar(255) NOT NULL,
                `username` varchar(120) DEFAULT NULL,
                `phone` varchar(20) DEFAULT NULL,
                `mobile` varchar(20) DEFAULT NULL,
                `password_hash` varchar(255) DEFAULT NULL,
                `password` varchar(255) DEFAULT NULL,
                `name` varchar(255) NOT NULL DEFAULT '',
                `role` varchar(100) DEFAULT NULL,
                `status` varchar(32) NOT NULL DEFAULT 'active',
                `photo` varchar(255) DEFAULT NULL,
                `avatar_url` varchar(255) DEFAULT NULL,
                `otp` varchar(10) DEFAULT NULL,
                `last_login_at` datetime(3) DEFAULT NULL,
                `last_login_ip` varchar(45) DEFAULT NULL,
                `remember_token` char(64) DEFAULT NULL,
                `remember_expires_at` datetime(3) DEFAULT NULL,
                `created_at` datetime(3) NOT NULL DEFAULT current_timestamp(3),
                `updated_at` datetime(3) NOT NULL DEFAULT current_timestamp(3) ON UPDATE current_timestamp(3),
                `deleted_at` datetime(3) DEFAULT NULL,
                PRIMARY KEY (`id`),
                KEY `idx_users_org` (`organization_id`),
                KEY `idx_users_email` (`email`),
                KEY `idx_users_status` (`status`),
                KEY `idx_users_deleted` (`deleted_at`)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"
        );
    } else {
        $columns = eduSchemaColumnMap($pdo, 'users');
        $requiredColumns = [
            'organization_id' => "ADD COLUMN `organization_id` char(32) DEFAULT NULL",
            'uid' => "ADD COLUMN `uid` varchar(50) DEFAULT NULL",
            'username' => "ADD COLUMN `username` varchar(120) DEFAULT NULL",
            'phone' => "ADD COLUMN `phone` varchar(20) DEFAULT NULL",
            'mobile' => "ADD COLUMN `mobile` varchar(20) DEFAULT NULL",
            'password_hash' => "ADD COLUMN `password_hash` varchar(255) DEFAULT NULL",
            'password' => "ADD COLUMN `password` varchar(255) DEFAULT NULL",
            'name' => "ADD COLUMN `name` varchar(255) NOT NULL DEFAULT ''",
            'role' => "ADD COLUMN `role` varchar(100) DEFAULT NULL",
            'status' => "ADD COLUMN `status` varchar(32) NOT NULL DEFAULT 'active'",
            'photo' => "ADD COLUMN `photo` varchar(255) DEFAULT NULL",
            'avatar_url' => "ADD COLUMN `avatar_url` varchar(255) DEFAULT NULL",
            'otp' => "ADD COLUMN `otp` varchar(10) DEFAULT NULL",
            'last_login_at' => "ADD COLUMN `last_login_at` datetime(3) DEFAULT NULL",
            'last_login_ip' => "ADD COLUMN `last_login_ip` varchar(45) DEFAULT NULL",
            'remember_token' => "ADD COLUMN `remember_token` char(64) DEFAULT NULL",
            'remember_expires_at' => "ADD COLUMN `remember_expires_at` datetime(3) DEFAULT NULL",
            'created_at' => "ADD COLUMN `created_at` datetime(3) NOT NULL DEFAULT current_timestamp(3)",
            'updated_at' => "ADD COLUMN `updated_at` datetime(3) NOT NULL DEFAULT current_timestamp(3) ON UPDATE current_timestamp(3)",
            'deleted_at' => "ADD COLUMN `deleted_at` datetime(3) DEFAULT NULL",
        ];
        foreach ($requiredColumns as $columnKey => $ddl) {
            if (!isset($columns[$columnKey])) {
                try {
                    $pdo->exec("ALTER TABLE `users` {$ddl}");
                } catch (Throwable $e) {
                    error_log('[EDU users table alter] ' . $columnKey . ': ' . $e->getMessage());
                }
            }
        }
    }

    if (!eduSchemaIndexExists($pdo, 'users', 'idx_users_email')) {
        try { $pdo->exec("CREATE INDEX `idx_users_email` ON `users` (`email`)"); } catch (Throwable $e) {}
    }
    if (!eduSchemaIndexExists($pdo, 'users', 'idx_users_org')) {
        try { $pdo->exec("CREATE INDEX `idx_users_org` ON `users` (`organization_id`)"); } catch (Throwable $e) {}
    }
    if (!eduSchemaIndexExists($pdo, 'users', 'idx_users_status')) {
        try { $pdo->exec("CREATE INDEX `idx_users_status` ON `users` (`status`)"); } catch (Throwable $e) {}
    }
    if (!eduSchemaIndexExists($pdo, 'users', 'idx_users_deleted')) {
        try { $pdo->exec("CREATE INDEX `idx_users_deleted` ON `users` (`deleted_at`)"); } catch (Throwable $e) {}
    }

    $columns = eduSchemaColumnMap($pdo, 'users');
    if (isset($columns['password_hash']) && isset($columns['password'])) {
        try {
            $pdo->exec(
                "UPDATE `users`
                 SET `password_hash` = `password`
                 WHERE (`password_hash` IS NULL OR `password_hash` = '')
                   AND `password` IS NOT NULL
                   AND `password` <> ''"
            );
        } catch (Throwable $e) {}
    }

    if (isset($columns['status'])) {
        try {
            $pdo->exec(
                "UPDATE `users`
                 SET `status` = 'active'
                 WHERE `status` IS NULL OR TRIM(CAST(`status` AS CHAR)) = ''"
            );
        } catch (Throwable $e) {}
    }

    if (isset($columns['organization_id'])) {
        try {
            $orgId = '';
            if (eduSchemaTableExists($pdo, 'organizations')) {
                $orgId = (string)($pdo->query("SELECT id FROM organizations ORDER BY created_at ASC LIMIT 1")->fetchColumn() ?: '');
            }
            if ($orgId !== '') {
                $st = $pdo->prepare(
                    "UPDATE `users`
                     SET `organization_id` = :org
                     WHERE `organization_id` IS NULL OR TRIM(`organization_id`) = ''"
                );
                $st->execute([':org' => $orgId]);
            }
        } catch (Throwable $e) {}
    }
}

function eduEnsureCoreSchema(PDO $pdo): void {
    static $done = false;
    if ($done) {
        return;
    }
    eduEnsureUsersTable($pdo);
    try {
        if (!eduSchemaTableExists($pdo, 'organizations')) {
            $pdo->exec(
                "CREATE TABLE `organizations` (
                    `id` char(32) NOT NULL,
                    `name` varchar(190) NOT NULL DEFAULT 'Organization',
                    `owner_user_id` char(32) DEFAULT NULL,
                    `owner_email` varchar(255) DEFAULT NULL,
                    `created_at` datetime(3) NOT NULL DEFAULT current_timestamp(3),
                    `updated_at` datetime(3) NOT NULL DEFAULT current_timestamp(3) ON UPDATE current_timestamp(3),
                    PRIMARY KEY (`id`),
                    KEY `idx_organizations_owner` (`owner_user_id`)
                ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"
            );
        }
    } catch (Throwable $e) {
        error_log('[EDU organizations table ensure] ' . $e->getMessage());
    }
    try {
        if (!eduSchemaTableExists($pdo, 'accounts')) {
            $pdo->exec(
                "CREATE TABLE `accounts` (
                    `id` char(32) NOT NULL,
                    `organization_id` char(32) DEFAULT NULL,
                    `owner_user_id` char(32) DEFAULT NULL,
                    `name` varchar(190) NOT NULL DEFAULT '',
                    `email` varchar(255) DEFAULT NULL,
                    `phone` varchar(30) DEFAULT NULL,
                    `status` varchar(32) NOT NULL DEFAULT 'active',
                    `created_at` datetime(3) NOT NULL DEFAULT current_timestamp(3),
                    `updated_at` datetime(3) NOT NULL DEFAULT current_timestamp(3) ON UPDATE current_timestamp(3),
                    `deleted_at` datetime(3) DEFAULT NULL,
                    PRIMARY KEY (`id`),
                    KEY `idx_accounts_org` (`organization_id`),
                    KEY `idx_accounts_owner` (`owner_user_id`),
                    KEY `idx_accounts_status` (`status`),
                    KEY `idx_accounts_deleted` (`deleted_at`)
                ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"
            );
        }
    } catch (Throwable $e) {
        error_log('[EDU accounts table ensure] ' . $e->getMessage());
    }
    try {
        if (!eduSchemaTableExists($pdo, 'products')) {
            $pdo->exec(
                "CREATE TABLE `products` (
                    `id` char(32) NOT NULL,
                    `organization_id` char(32) DEFAULT NULL,
                    `code` varchar(80) NOT NULL,
                    `name` varchar(160) NOT NULL,
                    `description` varchar(255) DEFAULT NULL,
                    `status` enum('active','inactive') NOT NULL DEFAULT 'active',
                    `metadata_json` longtext DEFAULT NULL,
                    `created_at` datetime(3) NOT NULL DEFAULT current_timestamp(3),
                    `updated_at` datetime(3) NOT NULL DEFAULT current_timestamp(3) ON UPDATE current_timestamp(3),
                    `deleted_at` datetime(3) DEFAULT NULL,
                    PRIMARY KEY (`id`),
                    UNIQUE KEY `uniq_products_code_org` (`organization_id`,`code`),
                    KEY `idx_products_status` (`status`),
                    KEY `idx_products_deleted` (`deleted_at`)
                ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"
            );
        }
    } catch (Throwable $e) {
        error_log('[EDU products table ensure] ' . $e->getMessage());
    }
    try {
        if (eduSchemaTableExists($pdo, 'products')) {
            $productsCount = (int)($pdo->query("SELECT COUNT(1) FROM products")->fetchColumn() ?: 0);
            if ($productsCount === 0) {
                $seedProductIds = [];
                if (eduSchemaTableExists($pdo, 'product_plans')) {
                    $ids = $pdo->query("SELECT DISTINCT product_id FROM product_plans WHERE product_id IS NOT NULL AND TRIM(product_id) <> ''")->fetchAll(PDO::FETCH_COLUMN) ?: [];
                    foreach ($ids as $id) {
                        $value = trim((string)$id);
                        if ($value !== '') {
                            $seedProductIds[$value] = true;
                        }
                    }
                }
                if ($seedProductIds === [] && eduSchemaTableExists($pdo, 'school_product_entitlements')) {
                    $ids = $pdo->query("SELECT DISTINCT product_id FROM school_product_entitlements WHERE product_id IS NOT NULL AND TRIM(product_id) <> ''")->fetchAll(PDO::FETCH_COLUMN) ?: [];
                    foreach ($ids as $id) {
                        $value = trim((string)$id);
                        if ($value !== '') {
                            $seedProductIds[$value] = true;
                        }
                    }
                }
                if ($seedProductIds === []) {
                    $seedProductIds[uuid32()] = true;
                }
                $insert = $pdo->prepare(
                    "INSERT INTO products
                        (id, organization_id, code, name, description, status, metadata_json, created_at, updated_at, deleted_at)
                     VALUES
                        (:id, NULL, :code, :name, :description, 'active', NULL, UTC_TIMESTAMP(3), UTC_TIMESTAMP(3), NULL)"
                );
                $position = 1;
                foreach (array_keys($seedProductIds) as $productId) {
                    $insert->execute([
                        ':id' => $productId,
                        ':code' => $position === 1 ? 'school' : ('product-' . $position),
                        ':name' => $position === 1 ? 'School (TezzERP)' : ('Product ' . $position),
                        ':description' => 'Auto-seeded EDU product for ERP plan mapping',
                    ]);
                    $position++;
                }
            }
        }
    } catch (Throwable $e) {
        error_log('[EDU products seed ensure] ' . $e->getMessage());
    }
    $done = true;
}


// ══════════════════════════════════════════
// 10.  JSON RESPONSE HELPER
// ══════════════════════════════════════════
function jsonResponse(array $data, int $status = 200): void {
    if (!headers_sent()) {
        http_response_code($status);
        header('Content-Type: application/json; charset=utf-8');
        header('Cache-Control: no-store, no-cache, must-revalidate');
    }
    echo json_encode($data, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
    exit;
}


// ══════════════════════════════════════════
// 11.  CSRF HELPERS
// ══════════════════════════════════════════
function generateCsrfToken(): string {
    if (empty($_SESSION['_csrf_token'])) {
        $_SESSION['_csrf_token'] = bin2hex(random_bytes(24));
    }
    return $_SESSION['_csrf_token'];
}
function validateCsrfToken(string $token): bool {
    if (empty($token) || empty($_SESSION['_csrf_token'])) return false;
    return hash_equals($_SESSION['_csrf_token'], $token);
}


// ══════════════════════════════════════════
// 12.  UUID  (32 lowercase hex chars)
// ══════════════════════════════════════════
function uuid32(): string {
    return str_replace('-', '', sprintf(
        '%04x%04x-%04x-%04x-%04x-%04x%04x%04x',
        mt_rand(0, 0xffff), mt_rand(0, 0xffff),
        mt_rand(0, 0xffff),
        mt_rand(0, 0x0fff) | 0x4000,
        mt_rand(0, 0x3fff) | 0x8000,
        mt_rand(0, 0xffff), mt_rand(0, 0xffff), mt_rand(0, 0xffff)
    ));
}


// ══════════════════════════════════════════
// 13.  HTTPS / COOKIE HELPERS
// ══════════════════════════════════════════
function cookieDomain(): string {
    $host = strtolower($_SERVER['HTTP_HOST'] ?? '');
    return (strpos($host, 'tezzcorp.com') !== false) ? '.tezzcorp.com' : '';
}
function isHttps(): bool {
    return (!empty($_SERVER['HTTPS']) && strtolower($_SERVER['HTTPS']) !== 'off')
        || (!empty($_SERVER['HTTP_X_FORWARDED_PROTO']) && strtolower($_SERVER['HTTP_X_FORWARDED_PROTO']) === 'https')
        || (int)($_SERVER['SERVER_PORT'] ?? 80) === 443;
}


// ══════════════════════════════════════════
// 14.  PERMISSION SYSTEM
// ══════════════════════════════════════════
function _crmUserPerms(): array {
    static $perms = null;
    if ($perms !== null) return $perms;

    $uid = (string)($_SESSION['user_id'] ?? '');
    if ($uid === '') { $perms = []; return $perms; }

    // 60-second session cache
    $cacheKey = '_crm_perms_cache';
    $cacheTs  = '_crm_perms_ts';
    if (isset($_SESSION[$cacheKey]) && is_array($_SESSION[$cacheKey]) && (time() - (int)($_SESSION[$cacheTs] ?? 0)) < 60) {
        $perms = $_SESSION[$cacheKey];
        return $perms;
    }

    $rows = [];
    try {
        $pdo = getDBConnection();
        // Try key_name column first (preferred)
        try {
            $st = $pdo->prepare("
                SELECT p.key_name
                FROM   permissions p
                JOIN   role_permissions rp ON rp.permission_id = p.id
                JOIN   user_roles ur       ON ur.role_id       = rp.role_id
                WHERE  ur.user_id = ?
            ");
            $st->execute([$uid]);
            $rows = $st->fetchAll(PDO::FETCH_COLUMN, 0) ?: [];
        } catch (Throwable $e1) {
            // Fall back to 'name' column
            $st2 = $pdo->prepare("
                SELECT p.name
                FROM   permissions p
                JOIN   role_permissions rp ON rp.permission_id = p.id
                JOIN   user_roles ur       ON ur.role_id       = rp.role_id
                WHERE  ur.user_id = ?
            ");
            $st2->execute([$uid]);
            $rows = $st2->fetchAll(PDO::FETCH_COLUMN, 0) ?: [];
        }
    } catch (Throwable $e) {
        $rows = [];
    }

    if ($rows === [] && defined('IS_EDU_HOST') && IS_EDU_HOST) {
        try {
            $userStmt = $pdo->prepare(
                "SELECT role, email, status
                 FROM users
                 WHERE id = ?
                 LIMIT 1"
            );
            $userStmt->execute([$uid]);
            $userRow = $userStmt->fetch(PDO::FETCH_ASSOC) ?: [];
            $role = strtolower(trim((string)($userRow['role'] ?? '')));
            $status = strtolower(trim((string)($userRow['status'] ?? 'active')));
            if (in_array($status, ['', 'active', '1', 'enabled', 'verified'], true)) {
                if (in_array($role, ['owner', 'admin', 'super admin', 'administrator'], true)) {
                    $rows = ['all'];
                } elseif ($role === 'partner') {
                    $rows = [
                        'erp.partner.view',
                        'erp.school.view',
                        'erp.billing.view',
                        'erp.release.view',
                    ];
                } else {
                    // If no role mapping exists, treat the oldest active org user as owner.
                    $orgId = trim((string)($_SESSION['organization_id'] ?? ''));
                    if ($orgId !== '') {
                        $firstUserStmt = $pdo->prepare(
                            "SELECT id
                             FROM users
                             WHERE organization_id = :org
                               AND (deleted_at IS NULL)
                               AND LOWER(TRIM(COALESCE(status, 'active'))) IN ('', 'active', '1', 'enabled', 'verified')
                             ORDER BY COALESCE(created_at, updated_at, NOW(3)) ASC, id ASC
                             LIMIT 1"
                        );
                        $firstUserStmt->execute([':org' => $orgId]);
                        $firstUserId = trim((string)$firstUserStmt->fetchColumn());
                        if ($firstUserId !== '' && hash_equals($firstUserId, $uid)) {
                            $rows = ['all'];
                        }
                    }
                }
            }
        } catch (Throwable $ignore) {
            // Keep empty permission set when fallback lookup fails.
        }
    }

    $perms = array_fill_keys($rows, true);
    $_SESSION[$cacheKey] = $perms;
    $_SESSION[$cacheTs]  = time();
    return $perms;
}

function can(string $perm): bool {
    $p = _crmUserPerms();
    if (isset($p['all']) || isset($p['super']) || isset($p['admin'])) return true;
    return isset($p[$perm]);
}

function requirePerm(string $perm): void {
    if (empty($_SESSION['user_id'])) {
        header('Location: ' . APP_URL . '/login');
        exit;
    }
    if (!can($perm)) {
        http_response_code(403);
        // Try multiple possible paths for errors/403.php
        $errPaths = [
            dirname(__DIR__) . '/edu.tezzcorp.com/errors/403.php',
            dirname(__DIR__) . '/edu/errors/403.php',
            dirname(__DIR__) . '/crm.tezzcorp.com/errors/403.php',
            __DIR__ . '/../crm.tezzcorp.com/errors/403.php',
            __DIR__ . '/../edu.tezzcorp.com/errors/403.php',
        ];
        foreach ($errPaths as $ep) {
            if (is_file($ep)) { include $ep; exit; }
        }
        echo '<!DOCTYPE html><html><head><title>403 Forbidden</title></head><body>'
           . '<h1>403 &mdash; Access Denied</h1>'
           . '<p>You do not have permission to view this page.</p>'
           . '<a href="javascript:history.back()">Go back</a></body></html>';
        exit;
    }
}

function requireLogin(): void {
    if (empty($_SESSION['user_id'])) {
        $dest = urlencode($_SERVER['REQUEST_URI'] ?? '');
        header('Location: ' . APP_URL . '/login' . ($dest !== '' ? '?next=' . $dest : ''));
        exit;
    }
}


// ══════════════════════════════════════════
// 15.  USER / ORG ID HELPERS
// ══════════════════════════════════════════
function currentUserId(): string {
    return (string)($_SESSION['user_id'] ?? '');
}

function currentOrgId(): string {
    if (!empty($_SESSION['organization_id'])) {
        return (string)$_SESSION['organization_id'];
    }
    $uid = currentUserId();
    if ($uid === '') return '';
    try {
        $st = getDBConnection()->prepare(
            "SELECT organization_id FROM users WHERE id = ? AND deleted_at IS NULL LIMIT 1"
        );
        $st->execute([$uid]);
        $orgId = (string)($st->fetchColumn() ?: '');
        if ($orgId !== '') $_SESSION['organization_id'] = $orgId;
        return $orgId;
    } catch (Throwable $e) {
        return '';
    }
}

function currentPartnerContext(): array {
    static $ctx = null;
    if ($ctx !== null) {
        return $ctx;
    }

    $ctx = [
        'id' => '',
        'name' => '',
        'business_name' => '',
        'email' => '',
    ];

    $uid = currentUserId();
    if ($uid === '') {
        return $ctx;
    }

    $orgId = currentOrgId();
    $sessionEmail = strtolower(trim((string)($_SESSION['user_email'] ?? '')));

    try {
        $pdo = getDBConnection();

        $tableStmt = $pdo->prepare(
            "SELECT 1
             FROM information_schema.TABLES
             WHERE TABLE_SCHEMA = DATABASE()
               AND TABLE_NAME = 'erp_partners'
             LIMIT 1"
        );
        $tableStmt->execute();
        if ((int)$tableStmt->fetchColumn() !== 1) {
            return $ctx;
        }

        $colStmt = $pdo->prepare(
            "SELECT COLUMN_NAME
             FROM information_schema.COLUMNS
             WHERE TABLE_SCHEMA = DATABASE()
               AND TABLE_NAME = 'erp_partners'"
        );
        $colStmt->execute();
        $columnsRaw = $colStmt->fetchAll(PDO::FETCH_COLUMN) ?: [];
        $columns = [];
        foreach ($columnsRaw as $col) {
            $columns[strtolower(trim((string)$col))] = true;
        }

        $has = static function (array $cols, string $name): bool {
            return isset($cols[strtolower($name)]);
        };

        if (!$has($columns, 'id')) {
            return $ctx;
        }

        $selectParts = ['id'];
        $selectParts[] = $has($columns, 'name') ? 'name' : "'' AS name";
        $selectParts[] = $has($columns, 'business_name') ? 'business_name' : "'' AS business_name";
        $selectParts[] = $has($columns, 'email') ? 'email' : "'' AS email";

        $baseWhere = [];
        $baseParams = [];
        if ($has($columns, 'organization_id') && $orgId !== '') {
            $baseWhere[] = 'organization_id = :org';
            $baseParams[':org'] = $orgId;
        }
        if ($has($columns, 'deleted_at')) {
            $baseWhere[] = 'deleted_at IS NULL';
        }

        $row = null;
        if ($has($columns, 'user_id')) {
            $where = $baseWhere;
            $params = $baseParams;
            $where[] = 'user_id = :uid';
            $params[':uid'] = $uid;
            $sql = "SELECT " . implode(', ', $selectParts)
                . " FROM erp_partners"
                . " WHERE " . implode(' AND ', $where)
                . " LIMIT 1";
            $stmt = $pdo->prepare($sql);
            $stmt->execute($params);
            $row = $stmt->fetch(PDO::FETCH_ASSOC) ?: null;
        }

        if ($row === null && $has($columns, 'email')) {
            $email = $sessionEmail;
            if ($email === '') {
                try {
                    $userEmailStmt = $pdo->prepare("SELECT email FROM users WHERE id = :uid LIMIT 1");
                    $userEmailStmt->execute([':uid' => $uid]);
                    $email = strtolower(trim((string)$userEmailStmt->fetchColumn()));
                } catch (Throwable $ignore) {
                    $email = '';
                }
            }

            if ($email !== '') {
                $where = $baseWhere;
                $params = $baseParams;
                $where[] = 'LOWER(TRIM(email)) = :email';
                $params[':email'] = $email;
                $sql = "SELECT " . implode(', ', $selectParts)
                    . " FROM erp_partners"
                    . " WHERE " . implode(' AND ', $where)
                    . " LIMIT 1";
                $stmt = $pdo->prepare($sql);
                $stmt->execute($params);
                $row = $stmt->fetch(PDO::FETCH_ASSOC) ?: null;
            }
        }

        if (is_array($row) && trim((string)($row['id'] ?? '')) !== '') {
            $ctx = [
                'id' => trim((string)($row['id'] ?? '')),
                'name' => trim((string)($row['name'] ?? '')),
                'business_name' => trim((string)($row['business_name'] ?? '')),
                'email' => trim((string)($row['email'] ?? '')),
            ];
        }
    } catch (Throwable $e) {
        $ctx = [
            'id' => '',
            'name' => '',
            'business_name' => '',
            'email' => '',
        ];
    }

    return $ctx;
}

function isPartnerScopedUser(): bool {
    $partner = currentPartnerContext();
    return trim((string)($partner['id'] ?? '')) !== '';
}


// ══════════════════════════════════════════
// 16.  LOGGING
// ══════════════════════════════════════════
function crmLog(string $level, string $msg): void {
    if (!defined('LOG_PATH')) return;
    $dir = LOG_PATH;
    if (!is_dir($dir)) @mkdir($dir, 0755, true);
    $line = sprintf("[%s] [%s] %s\n", date('Y-m-d H:i:s'), strtoupper($level), $msg);
    @file_put_contents($dir . '/crm.log', $line, FILE_APPEND | LOCK_EX);
}
?>
