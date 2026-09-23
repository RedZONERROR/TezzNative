<?php

declare(strict_types=1);

function tezz_json(array $data, int $status = 200): void
{
    http_response_code($status);
    header('Content-Type: application/json; charset=utf-8');
    header('Cache-Control: no-store, no-cache, must-revalidate, max-age=0');
    echo json_encode($data, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
    exit;
}

function tezz_load_config(): array
{
    $path = dirname(__DIR__) . '/config/db.php';
    if (!is_file($path)) {
        return [
            'site_name' => 'TezzNative',
            'site_url' => '',
            'db_host' => 'localhost',
            'db_name' => '',
            'db_user' => '',
            'db_pass' => '',
            'timezone' => 'UTC',
            'telemetry_salt' => '',
            'admin_user' => 'admin',
            'admin_token' => '',
            'admin_email' => 'admin@tezzcorp.com',
            'admin_password' => '',
        ];
    }
    $config = require $path;
    if (!is_array($config)) {
        return [];
    }
    return $config;
}

function tezz_configure_timezone(array $config): void
{
    $tz = (string)($config['timezone'] ?? 'UTC');
    if ($tz === '') {
        $tz = 'UTC';
    }
    date_default_timezone_set($tz);
}

function tezz_site_url(array $config): string
{
    $siteUrl = trim((string)($config['site_url'] ?? ''));
    if ($siteUrl !== '') {
        return rtrim($siteUrl, '/');
    }

    $https = (string)($_SERVER['HTTPS'] ?? '');
    $scheme = ($https !== '' && strtolower($https) !== 'off') ? 'https' : 'http';
    $host = tezz_clean_text((string)($_SERVER['HTTP_HOST'] ?? 'tezznative.org'), 190);
    if ($host === '') {
        $host = 'tezznative.org';
    }
    return $scheme . '://' . $host;
}

function tezz_request_path(): string
{
    $uri = (string)($_SERVER['REQUEST_URI'] ?? '/');
    $path = (string)(parse_url($uri, PHP_URL_PATH) ?? '/');
    if ($path === '') {
        $path = '/';
    }
    return $path;
}

function tezz_canonical_url(array $config, ?string $path = null): string
{
    $base = tezz_site_url($config);
    $requestPath = $path ?? tezz_request_path();
    if ($requestPath === '') {
        $requestPath = '/';
    }
    if ($requestPath[0] !== '/') {
        $requestPath = '/' . $requestPath;
    }
    return $base . $requestPath;
}

function tezz_render_seo(
    array $config,
    string $title,
    string $description,
    ?string $path = null,
    string $type = 'website'
): string {
    $fullTitle = tezz_clean_text($title, 180);
    $desc = tezz_clean_text($description, 320);
    if ($desc === '') {
        $desc = 'TezzNative production language portal with docs, SDK reference, and install workflow.';
    }
    $canonical = tezz_canonical_url($config, $path);
    $safeTitle = htmlspecialchars($fullTitle, ENT_QUOTES, 'UTF-8');
    $safeDesc = htmlspecialchars($desc, ENT_QUOTES, 'UTF-8');
    $safeCanonical = htmlspecialchars($canonical, ENT_QUOTES, 'UTF-8');
    $safeType = htmlspecialchars($type, ENT_QUOTES, 'UTF-8');

    return
        "<meta name=\"description\" content=\"{$safeDesc}\">\n" .
        "<meta name=\"robots\" content=\"index,follow,max-snippet:-1,max-image-preview:large,max-video-preview:-1\">\n" .
        "<link rel=\"canonical\" href=\"{$safeCanonical}\">\n" .
        "<meta property=\"og:title\" content=\"{$safeTitle}\">\n" .
        "<meta property=\"og:description\" content=\"{$safeDesc}\">\n" .
        "<meta property=\"og:type\" content=\"{$safeType}\">\n" .
        "<meta property=\"og:url\" content=\"{$safeCanonical}\">\n" .
        "<meta property=\"og:site_name\" content=\"TezzNative Language\">\n" .
        "<meta property=\"og:image\" content=\"/assets/logo/tezz-logo.png\">\n" .
        "<meta name=\"twitter:card\" content=\"summary_large_image\">\n" .
        "<meta name=\"twitter:title\" content=\"{$safeTitle}\">\n" .
        "<meta name=\"twitter:description\" content=\"{$safeDesc}\">\n" .
        "<meta name=\"twitter:image\" content=\"/assets/logo/tezz-logo.png\">";
}

function tezz_pdo(array $config): ?PDO
{
    $host = (string)($config['db_host'] ?? '');
    $name = (string)($config['db_name'] ?? '');
    $user = (string)($config['db_user'] ?? '');
    $pass = (string)($config['db_pass'] ?? '');
    if ($host === '' || $name === '' || $user === '') {
        return null;
    }
    $dsn = sprintf('mysql:host=%s;dbname=%s;charset=utf8mb4', $host, $name);
    return new PDO($dsn, $user, $pass, [
        PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
        PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
        PDO::ATTR_TIMEOUT => 5,
    ]);
}

function tezz_bootstrap_site(?PDO $pdo): void
{
    static $ready = false;
    if ($ready || !$pdo) {
        return;
    }

    $pdo->exec(
        'CREATE TABLE IF NOT EXISTS tn_visits (
            id BIGINT AUTO_INCREMENT PRIMARY KEY,
            visit_key CHAR(64) NOT NULL,
            path VARCHAR(200) NOT NULL,
            ip_hash CHAR(64) NOT NULL,
            user_agent VARCHAR(255) NOT NULL DEFAULT "",
            referer VARCHAR(255) NOT NULL DEFAULT "",
            created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            UNIQUE KEY uq_visit_key (visit_key),
            INDEX idx_visits_path (path),
            INDEX idx_visits_created (created_at)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4'
    );

    $pdo->exec(
        'CREATE TABLE IF NOT EXISTS tn_users (
            id INT AUTO_INCREMENT PRIMARY KEY,
            display_name VARCHAR(100) NOT NULL,
            email VARCHAR(190) NOT NULL,
            password_hash VARCHAR(255) NOT NULL,
            bio TEXT NULL,
            created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            UNIQUE KEY uq_tn_users_email (email)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4'
    );

    $pdo->exec(
        'CREATE TABLE IF NOT EXISTS tn_support_posts (
            id INT AUTO_INCREMENT PRIMARY KEY,
            support_type VARCHAR(40) NOT NULL,
            user_name VARCHAR(100) NOT NULL,
            email VARCHAR(190) NOT NULL,
            title VARCHAR(160) NOT NULL,
            message TEXT NOT NULL,
            upload_path VARCHAR(255) NOT NULL DEFAULT "",
            status VARCHAR(20) NOT NULL DEFAULT "open",
            created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            INDEX idx_tn_support_created (created_at)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4'
    );

    $pdo->exec(
        'CREATE TABLE IF NOT EXISTS tn_error_reports (
            id INT AUTO_INCREMENT PRIMARY KEY,
            user_name VARCHAR(100) NOT NULL,
            email VARCHAR(190) NOT NULL,
            platform VARCHAR(60) NOT NULL,
            cli_version VARCHAR(60) NOT NULL,
            command_text VARCHAR(255) NOT NULL,
            error_text TEXT NOT NULL,
            source VARCHAR(50) NOT NULL DEFAULT "website",
            created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            INDEX idx_tn_errors_created (created_at)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4'
    );

    $pdo->exec(
        'CREATE TABLE IF NOT EXISTS tn_release_versions (
            id INT AUTO_INCREMENT PRIMARY KEY,
            version VARCHAR(60) NOT NULL,
            channel VARCHAR(40) NOT NULL DEFAULT "production",
            notes TEXT NULL,
            created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            UNIQUE KEY uq_tn_release_version (version),
            INDEX idx_tn_release_created (created_at)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4'
    );

    $pdo->exec(
        'CREATE TABLE IF NOT EXISTS tn_cli_reports (
            id INT AUTO_INCREMENT PRIMARY KEY,
            user_name VARCHAR(100) NOT NULL,
            email VARCHAR(190) NOT NULL,
            cli_version VARCHAR(60) NOT NULL,
            platform VARCHAR(60) NOT NULL,
            doctor_status VARCHAR(20) NOT NULL,
            test_mode VARCHAR(30) NOT NULL,
            test_status VARCHAR(20) NOT NULL,
            output_excerpt TEXT NULL,
            created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            INDEX idx_tn_cli_created (created_at)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4'
    );

    $pdo->exec(
        'CREATE TABLE IF NOT EXISTS tn_install_events (
            id INT AUTO_INCREMENT PRIMARY KEY,
            platform VARCHAR(40) NOT NULL,
            version VARCHAR(60) NOT NULL,
            status VARCHAR(20) NOT NULL,
            install_id VARCHAR(100) NOT NULL,
            message TEXT NULL,
            ip_hash CHAR(64) NOT NULL,
            user_agent VARCHAR(255) NOT NULL DEFAULT "",
            created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            INDEX idx_tn_install_created (created_at),
            INDEX idx_tn_install_status (status)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4'
    );

    $pdo->exec(
        'CREATE TABLE IF NOT EXISTS tn_download_requests (
            id BIGINT AUTO_INCREMENT PRIMARY KEY,
            artifact_key VARCHAR(80) NOT NULL,
            artifact_path VARCHAR(255) NOT NULL DEFAULT "",
            platform VARCHAR(80) NOT NULL DEFAULT "",
            version VARCHAR(60) NOT NULL DEFAULT "",
            source VARCHAR(80) NOT NULL DEFAULT "website",
            ip_hash CHAR(64) NOT NULL,
            user_agent VARCHAR(255) NOT NULL DEFAULT "",
            referer VARCHAR(255) NOT NULL DEFAULT "",
            created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            INDEX idx_tn_download_artifact (artifact_key),
            INDEX idx_tn_download_created (created_at)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4'
    );

    $pdo->exec(
        'CREATE TABLE IF NOT EXISTS tn_update_requests (
            id BIGINT AUTO_INCREMENT PRIMARY KEY,
            platform VARCHAR(80) NOT NULL DEFAULT "",
            current_version VARCHAR(60) NOT NULL DEFAULT "",
            latest_version VARCHAR(60) NOT NULL DEFAULT "",
            channel VARCHAR(40) NOT NULL DEFAULT "",
            mode VARCHAR(40) NOT NULL DEFAULT "check",
            install_id VARCHAR(120) NOT NULL DEFAULT "",
            update_available TINYINT(1) NOT NULL DEFAULT 0,
            ip_hash CHAR(64) NOT NULL,
            user_agent VARCHAR(255) NOT NULL DEFAULT "",
            created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            INDEX idx_tn_update_platform (platform),
            INDEX idx_tn_update_created (created_at)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4'
    );

    $ready = true;
}

function tezz_current_version_info(): array
{
    $path = dirname(__DIR__) . '/download/sdk/version.json';
    if (is_file($path)) {
        $data = json_decode((string)file_get_contents($path), true);
        if (is_array($data) && !empty($data['version'])) {
            return $data;
        }
    }
    return [
        'language' => 'TezzNative',
        'version' => '2.2.1',
        'channel' => 'release',
        'api' => 'v2',
        'vendor' => 'TezzCorp Pvt Ltd.',
        'creator' => 'Rohit Pathak',
        'backend' => 'x64-pe-native',
        'compiler' => 'tezzc',
        'codegen' => 'ir-v4',
    ];
}

function tezz_version_compare(string $a, string $b): int
{
    $cleanA = preg_replace('/[^0-9A-Za-z._-]/', '', $a) ?? '';
    $cleanB = preg_replace('/[^0-9A-Za-z._-]/', '', $b) ?? '';
    return version_compare($cleanA, $cleanB);
}

function tezz_file_meta(string $root, string $relative): array
{
    $path = rtrim($root, '/\\') . '/' . ltrim($relative, '/');
    if (!is_file($path)) {
        return [
            'exists' => false,
            'size' => 'Unavailable',
            'bytes' => 0,
            'sha256' => 'Unavailable',
        ];
    }

    $bytes = (int)filesize($path);
    $units = ['B', 'KB', 'MB', 'GB'];
    $size = (float)$bytes;
    $unit = 0;
    while ($size >= 1024 && $unit < count($units) - 1) {
        $size /= 1024;
        $unit++;
    }

    return [
        'exists' => true,
        'size' => number_format($size, $unit === 0 ? 0 : 1) . ' ' . $units[$unit],
        'bytes' => $bytes,
        'sha256' => strtoupper((string)hash_file('sha256', $path)),
    ];
}

function tezz_download_artifacts(string $root): array
{
    $version = (string)(tezz_current_version_info()['version'] ?? '1.1.0');
    $items = [
        [
            'key' => 'windows-sdk',
            'title' => 'Windows SDK ZIP',
            'platform' => 'windows-x64',
            'label' => 'Recommended',
            'href' => '/download/tezznative-sdk.zip',
            'relative' => 'download/tezznative-sdk.zip',
            'command' => 'iwr https://tezznative.org/api/download_request.php?artifact=windows-sdk -OutFile tezznative-sdk.zip',
            'description' => 'Portable compiler, wrapper scripts, standard library, tools, and metadata.',
        ],
        [
            'key' => 'windows-installer',
            'title' => 'Windows CLI Installer',
            'platform' => 'windows-x64',
            'label' => 'CLI Script',
            'href' => '/download/install.ps1',
            'relative' => 'download/install.ps1',
            'command' => 'irm https://tezznative.org/install.ps1 | iex',
            'description' => 'Official one-line automated installer for Windows 10/11 x64.',
        ],
        [
            'key' => 'linux-sdk',
            'title' => 'Linux SDK TAR.GZ',
            'platform' => 'linux-x64',
            'label' => 'Preview',
            'href' => '/download/tezznative-sdk-linux.tar.gz',
            'relative' => 'download/tezznative-sdk-linux.tar.gz',
            'command' => 'curl -fL "https://tezznative.org/api/download_request.php?artifact=linux-sdk" -o tezznative-sdk-linux.tar.gz',
            'description' => 'Linux archive for native target validation and early CLI workflows.',
        ],
    ];

    foreach ($items as $i => $item) {
        $items[$i]['version'] = $version;
        $items[$i]['meta'] = tezz_file_meta($root, (string)$item['relative']);
    }
    return $items;
}

function tezz_find_download_artifact(string $root, string $key): ?array
{
    foreach (tezz_download_artifacts($root) as $artifact) {
        if ((string)$artifact['key'] === $key) {
            return $artifact;
        }
    }
    return null;
}

function tezz_record_download_request(?PDO $pdo, array $config, array $artifact, string $source = 'website'): void
{
    if (!$pdo) {
        return;
    }
    tezz_bootstrap_site($pdo);
    $stmt = $pdo->prepare(
        'INSERT INTO tn_download_requests
         (artifact_key, artifact_path, platform, version, source, ip_hash, user_agent, referer)
         VALUES (?, ?, ?, ?, ?, ?, ?, ?)'
    );
    $stmt->execute([
        tezz_clean_text((string)($artifact['key'] ?? ''), 80),
        tezz_clean_text((string)($artifact['href'] ?? ''), 255),
        tezz_clean_text((string)($artifact['platform'] ?? ''), 80),
        tezz_clean_text((string)($artifact['version'] ?? ''), 60),
        tezz_clean_text($source, 80),
        tezz_ip_hash(tezz_client_ip(), $config),
        tezz_clean_text((string)($_SERVER['HTTP_USER_AGENT'] ?? ''), 255),
        tezz_clean_text((string)($_SERVER['HTTP_REFERER'] ?? ''), 255),
    ]);
}

function tezz_update_payload(array $config, string $platform, string $currentVersion, string $mode = 'check'): array
{
    $info = tezz_current_version_info();
    $latest = (string)($info['version'] ?? '1.1.0');
    $channel = (string)($info['channel'] ?? 'release');
    $updateAvailable = $currentVersion === '' ? true : tezz_version_compare($currentVersion, $latest) < 0;
    $site = tezz_site_url($config);

    return [
        'language' => (string)($info['language'] ?? 'TezzNative'),
        'latest' => [
            'version' => $latest,
            'channel' => $channel,
            'api' => (string)($info['api'] ?? 'v1'),
            'backend' => (string)($info['backend'] ?? 'x64-pe-native'),
        ],
        'current_version' => $currentVersion,
        'platform' => $platform,
        'mode' => $mode,
        'update_available' => $updateAvailable,
        'commands' => [
            'windows_install' => 'iwr ' . $site . '/download/install.ps1 -OutFile install.ps1; powershell -ExecutionPolicy Bypass -File .\\install.ps1',
            'windows_update' => 'powershell -ExecutionPolicy Bypass -File .\\install.ps1 -Mode update',
            'unix_install' => 'curl -fsSL ' . $site . '/download/install.sh | bash',
            'unix_update' => 'TEZZ_INSTALL_MODE=update curl -fsSL ' . $site . '/download/install.sh | bash',
        ],
    ];
}

function tezz_record_update_request(?PDO $pdo, array $config, array $payload, string $installId = ''): void
{
    if (!$pdo) {
        return;
    }
    tezz_bootstrap_site($pdo);
    $latest = (array)($payload['latest'] ?? []);
    $stmt = $pdo->prepare(
        'INSERT INTO tn_update_requests
         (platform, current_version, latest_version, channel, mode, install_id, update_available, ip_hash, user_agent)
         VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)'
    );
    $stmt->execute([
        tezz_clean_text((string)($payload['platform'] ?? ''), 80),
        tezz_clean_text((string)($payload['current_version'] ?? ''), 60),
        tezz_clean_text((string)($latest['version'] ?? ''), 60),
        tezz_clean_text((string)($latest['channel'] ?? ''), 40),
        tezz_clean_text((string)($payload['mode'] ?? 'check'), 40),
        tezz_clean_text($installId, 120),
        !empty($payload['update_available']) ? 1 : 0,
        tezz_ip_hash(tezz_client_ip(), $config),
        tezz_clean_text((string)($_SERVER['HTTP_USER_AGENT'] ?? ''), 255),
    ]);
}

function tezz_client_ip(): string
{
    $keys = [
        'HTTP_CF_CONNECTING_IP',
        'HTTP_X_FORWARDED_FOR',
        'REMOTE_ADDR',
    ];
    foreach ($keys as $key) {
        $raw = (string)($_SERVER[$key] ?? '');
        if ($raw === '') {
            continue;
        }
        $part = trim(explode(',', $raw)[0] ?? '');
        if ($part !== '') {
            return $part;
        }
    }
    return '0.0.0.0';
}

function tezz_ip_hash(string $ip, array $config): string
{
    $salt = (string)($config['telemetry_salt'] ?? '');
    return hash('sha256', $ip . '|' . $salt . '|tezznative');
}

function tezz_clean_text(string $value, int $maxLen): string
{
    $v = trim(str_replace("\r", '', $value));
    if ($v === '') {
        return '';
    }
    if (strlen($v) > $maxLen) {
        $v = substr($v, 0, $maxLen);
    }
    return $v;
}

function tezz_track_visit(?PDO $pdo, array $config, string $path): array
{
    $fallback = [
        'total_visits' => 0,
        'unique_visitors' => 0,
        'today_visits' => 0,
        'today_unique' => 0,
    ];
    if (!$pdo) {
        return $fallback;
    }

    tezz_bootstrap_site($pdo);
    $ipHash = tezz_ip_hash(tezz_client_ip(), $config);
    $ua = tezz_clean_text((string)($_SERVER['HTTP_USER_AGENT'] ?? ''), 255);
    $ref = tezz_clean_text((string)($_SERVER['HTTP_REFERER'] ?? ''), 255);
    $minuteKey = date('YmdHi');
    $visitKey = hash('sha256', $path . '|' . $ipHash . '|' . $minuteKey);

    $stmt = $pdo->prepare(
        'INSERT IGNORE INTO tn_visits (visit_key, path, ip_hash, user_agent, referer)
         VALUES (?, ?, ?, ?, ?)'
    );
    $stmt->execute([$visitKey, $path, $ipHash, $ua, $ref]);

    $all = $pdo->query(
        'SELECT COUNT(*) AS total_visits, COUNT(DISTINCT ip_hash) AS unique_visitors FROM tn_visits'
    )->fetch();
    $today = $pdo->query(
        'SELECT COUNT(*) AS today_visits, COUNT(DISTINCT ip_hash) AS today_unique
         FROM tn_visits WHERE created_at >= CURDATE()'
    )->fetch();
    if (!is_array($all) || !is_array($today)) {
        return $fallback;
    }
    return [
        'total_visits' => (int)($all['total_visits'] ?? 0),
        'unique_visitors' => (int)($all['unique_visitors'] ?? 0),
        'today_visits' => (int)($today['today_visits'] ?? 0),
        'today_unique' => (int)($today['today_unique'] ?? 0),
    ];
}

function tezz_store_support_upload(array $file): string
{
    $err = (int)($file['error'] ?? UPLOAD_ERR_NO_FILE);
    if ($err !== UPLOAD_ERR_OK) {
        return '';
    }
    $tmp = (string)($file['tmp_name'] ?? '');
    if ($tmp === '' || !is_uploaded_file($tmp)) {
        return '';
    }
    $size = (int)($file['size'] ?? 0);
    if ($size <= 0 || $size > 5 * 1024 * 1024) {
        return '';
    }
    $orig = (string)($file['name'] ?? '');
    $ext = strtolower(pathinfo($orig, PATHINFO_EXTENSION));
    $allowed = ['txt', 'md', 'log', 'png', 'jpg', 'jpeg', 'pdf', 'zip'];
    if (!in_array($ext, $allowed, true)) {
        return '';
    }

    $relativeDir = 'storage/support_uploads/' . date('Y/m');
    $baseDir = dirname(__DIR__);
    $targetDir = $baseDir . '/' . $relativeDir;
    if (!is_dir($targetDir)) {
        mkdir($targetDir, 0775, true);
    }
    $rand = bin2hex(random_bytes(6));
    $name = date('Ymd_His') . '_' . $rand . '.' . $ext;
    $target = $targetDir . '/' . $name;
    if (!move_uploaded_file($tmp, $target)) {
        return '';
    }
    return $relativeDir . '/' . $name;
}

function tezz_register_user(?PDO $pdo, array $data): array
{
    if (!$pdo) {
        return ['ok' => false, 'message' => 'Database is not configured yet.'];
    }
    tezz_bootstrap_site($pdo);
    $name = tezz_clean_text((string)($data['display_name'] ?? ''), 100);
    $email = strtolower(tezz_clean_text((string)($data['email'] ?? ''), 190));
    $password = (string)($data['password'] ?? '');
    $bio = tezz_clean_text((string)($data['bio'] ?? ''), 1200);

    if ($name === '' || !filter_var($email, FILTER_VALIDATE_EMAIL) || strlen($password) < 8) {
        return ['ok' => false, 'message' => 'Enter valid name, email, and password (min 8 chars).'];
    }

    try {
        $stmt = $pdo->prepare(
            'INSERT INTO tn_users (display_name, email, password_hash, bio) VALUES (?, ?, ?, ?)'
        );
        $stmt->execute([$name, $email, password_hash($password, PASSWORD_DEFAULT), $bio]);
        return ['ok' => true, 'message' => 'Account created successfully.'];
    } catch (Throwable $e) {
        if (str_contains(strtolower($e->getMessage()), 'duplicate')) {
            return ['ok' => false, 'message' => 'Email already registered.'];
        }
        return ['ok' => false, 'message' => 'Could not create account right now.'];
    }
}

function tezz_submit_support_post(?PDO $pdo, array $data, ?array $file): array
{
    if (!$pdo) {
        return ['ok' => false, 'message' => 'Database is not configured yet.'];
    }
    tezz_bootstrap_site($pdo);
    $type = tezz_clean_text((string)($data['support_type'] ?? 'support'), 40);
    $name = tezz_clean_text((string)($data['user_name'] ?? ''), 100);
    $email = strtolower(tezz_clean_text((string)($data['email'] ?? ''), 190));
    $title = tezz_clean_text((string)($data['title'] ?? ''), 160);
    $message = tezz_clean_text((string)($data['message'] ?? ''), 5000);
    if ($name === '' || !filter_var($email, FILTER_VALIDATE_EMAIL) || $title === '' || $message === '') {
        return ['ok' => false, 'message' => 'Fill name, email, title, and message.'];
    }

    $uploadPath = '';
    if (is_array($file) && (($file['error'] ?? UPLOAD_ERR_NO_FILE) !== UPLOAD_ERR_NO_FILE)) {
        $uploadPath = tezz_store_support_upload($file);
        if ($uploadPath === '') {
            return ['ok' => false, 'message' => 'Upload failed. Allowed: txt, md, log, png, jpg, pdf, zip (<=5MB).'];
        }
    }

    $stmt = $pdo->prepare(
        'INSERT INTO tn_support_posts (support_type, user_name, email, title, message, upload_path)
         VALUES (?, ?, ?, ?, ?, ?)'
    );
    $stmt->execute([$type, $name, $email, $title, $message, $uploadPath]);
    return ['ok' => true, 'message' => 'Support request submitted. Thank you for contributing.'];
}

function tezz_submit_error_report(?PDO $pdo, array $data): array
{
    if (!$pdo) {
        return ['ok' => false, 'message' => 'Database is not configured yet.'];
    }
    tezz_bootstrap_site($pdo);
    $name = tezz_clean_text((string)($data['user_name'] ?? ''), 100);
    $email = strtolower(tezz_clean_text((string)($data['email'] ?? ''), 190));
    $platform = tezz_clean_text((string)($data['platform'] ?? ''), 60);
    $version = tezz_clean_text((string)($data['cli_version'] ?? ''), 60);
    $command = tezz_clean_text((string)($data['command_text'] ?? ''), 255);
    $errorText = tezz_clean_text((string)($data['error_text'] ?? ''), 8000);
    if ($name === '' || !filter_var($email, FILTER_VALIDATE_EMAIL) || $platform === '' || $version === '' || $command === '' || $errorText === '') {
        return ['ok' => false, 'message' => 'Fill all error report fields.'];
    }
    $source = tezz_clean_text((string)($data['source'] ?? 'website'), 50);
    $stmt = $pdo->prepare(
        'INSERT INTO tn_error_reports (user_name, email, platform, cli_version, command_text, error_text, source)
         VALUES (?, ?, ?, ?, ?, ?, ?)'
    );
    $stmt->execute([$name, $email, $platform, $version, $command, $errorText, $source]);
    return ['ok' => true, 'message' => 'Error report saved.'];
}

function tezz_submit_cli_report(?PDO $pdo, array $data): array
{
    if (!$pdo) {
        return ['ok' => false, 'message' => 'Database is not configured yet.'];
    }
    tezz_bootstrap_site($pdo);
    $name = tezz_clean_text((string)($data['user_name'] ?? ''), 100);
    $email = strtolower(tezz_clean_text((string)($data['email'] ?? ''), 190));
    $cliVersion = tezz_clean_text((string)($data['cli_version'] ?? ''), 60);
    $platform = tezz_clean_text((string)($data['platform'] ?? ''), 60);
    $doctorStatus = tezz_clean_text((string)($data['doctor_status'] ?? ''), 20);
    $testMode = tezz_clean_text((string)($data['test_mode'] ?? ''), 30);
    $testStatus = tezz_clean_text((string)($data['test_status'] ?? ''), 20);
    $output = tezz_clean_text((string)($data['output_excerpt'] ?? ''), 7000);
    if ($name === '' || !filter_var($email, FILTER_VALIDATE_EMAIL) || $cliVersion === '' || $platform === '' || $doctorStatus === '' || $testMode === '' || $testStatus === '') {
        return ['ok' => false, 'message' => 'Fill all required CLI report fields.'];
    }
    $stmt = $pdo->prepare(
        'INSERT INTO tn_cli_reports (user_name, email, cli_version, platform, doctor_status, test_mode, test_status, output_excerpt)
         VALUES (?, ?, ?, ?, ?, ?, ?, ?)'
    );
    $stmt->execute([$name, $email, $cliVersion, $platform, $doctorStatus, $testMode, $testStatus, $output]);
    return ['ok' => true, 'message' => 'CLI report submitted.'];
}

function tezz_upsert_release_version(?PDO $pdo, string $version, string $channel, string $notes): array
{
    if (!$pdo) {
        return ['ok' => false, 'message' => 'Database is not configured yet.'];
    }
    tezz_bootstrap_site($pdo);
    $version = tezz_clean_text($version, 60);
    $channel = tezz_clean_text($channel, 40);
    $notes = tezz_clean_text($notes, 5000);
    if ($version === '' || preg_match('/^[A-Za-z0-9._-]+$/', $version) !== 1) {
        return ['ok' => false, 'message' => 'Version format is invalid.'];
    }
    if ($channel === '') {
        $channel = 'production';
    }
    $stmt = $pdo->prepare(
        'INSERT INTO tn_release_versions (version, channel, notes)
         VALUES (?, ?, ?)
         ON DUPLICATE KEY UPDATE channel = VALUES(channel), notes = VALUES(notes)'
    );
    $stmt->execute([$version, $channel, $notes]);
    return ['ok' => true, 'message' => 'Version registry updated.'];
}

function tezz_seed_release_from_config(?PDO $pdo): void
{
    if (!$pdo) {
        return;
    }
    $v = tezz_current_version_info();
    tezz_upsert_release_version(
        $pdo,
        (string)($v['version'] ?? '1.0.0'),
        (string)($v['channel'] ?? 'production'),
        'Seeded from config/version.json'
    );
}

function tezz_record_install_event(?PDO $pdo, array $config, array $data): array
{
    if (!$pdo) {
        return ['ok' => false, 'message' => 'Database is not configured yet.'];
    }
    tezz_bootstrap_site($pdo);
    $platform = tezz_clean_text((string)($data['platform'] ?? ''), 40);
    $version = tezz_clean_text((string)($data['version'] ?? ''), 60);
    $status = tezz_clean_text((string)($data['status'] ?? ''), 20);
    $installId = tezz_clean_text((string)($data['install_id'] ?? ''), 100);
    $message = tezz_clean_text((string)($data['message'] ?? ''), 2000);

    if ($platform === '' || $version === '' || $status === '' || $installId === '') {
        return ['ok' => false, 'message' => 'platform/version/status/install_id required'];
    }

    $stmt = $pdo->prepare(
        'INSERT INTO tn_install_events (platform, version, status, install_id, message, ip_hash, user_agent)
         VALUES (?, ?, ?, ?, ?, ?, ?)'
    );
    $stmt->execute([
        $platform,
        $version,
        $status,
        $installId,
        $message,
        tezz_ip_hash(tezz_client_ip(), $config),
        tezz_clean_text((string)($_SERVER['HTTP_USER_AGENT'] ?? ''), 255),
    ]);
    return ['ok' => true, 'message' => 'install event recorded'];
}

function tezz_fetch_versions(?PDO $pdo, int $limit = 100): array
{
    if (!$pdo) {
        return [];
    }
    tezz_bootstrap_site($pdo);
    $stmt = $pdo->prepare(
        'SELECT version, channel, notes, created_at
         FROM tn_release_versions
         ORDER BY created_at DESC
         LIMIT ?'
    );
    $stmt->bindValue(1, max(1, $limit), PDO::PARAM_INT);
    $stmt->execute();
    return $stmt->fetchAll() ?: [];
}

function tezz_fetch_cli_reports(?PDO $pdo, int $limit = 40): array
{
    if (!$pdo) {
        return [];
    }
    tezz_bootstrap_site($pdo);
    $stmt = $pdo->prepare(
        'SELECT user_name, cli_version, platform, doctor_status, test_mode, test_status, created_at
         FROM tn_cli_reports
         ORDER BY created_at DESC
         LIMIT ?'
    );
    $stmt->bindValue(1, max(1, $limit), PDO::PARAM_INT);
    $stmt->execute();
    return $stmt->fetchAll() ?: [];
}

function tezz_fetch_top_contributors(?PDO $pdo, int $limit = 12): array
{
    if (!$pdo) {
        return [];
    }
    tezz_bootstrap_site($pdo);
    $stmt = $pdo->prepare(
        'SELECT display_name, created_at
         FROM tn_users
         ORDER BY created_at DESC
         LIMIT ?'
    );
    $stmt->bindValue(1, max(1, $limit), PDO::PARAM_INT);
    $stmt->execute();
    return $stmt->fetchAll() ?: [];
}
