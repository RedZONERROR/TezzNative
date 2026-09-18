<?php
declare(strict_types=1);

require_once __DIR__ . '/_common.php';
require_once __DIR__ . '/_erp_sync.php';

$method = strtoupper($_SERVER['REQUEST_METHOD'] ?? 'GET');
if (!in_array($method, ['GET', 'POST'], true)) {
    jsonResponse(405, [
        'success' => false,
        'message' => 'Method not allowed',
    ]);
}

$pdo = apiDb();
if (defined('PDO::MYSQL_ATTR_USE_BUFFERED_QUERY')) {
    try {
        $pdo->setAttribute(PDO::MYSQL_ATTR_USE_BUFFERED_QUERY, true);
    } catch (Throwable $e) {
        // Keep running even when driver blocks this attribute.
    }
}
$userId = requireAuthenticatedUserId();
$user = fetchUserContext($pdo, $userId);
requireAnyPermission($user, ['index', 'profile']);

$userPermissions = array_values(array_filter(array_map(
    static fn($item): string => strtolower(trim((string)$item)),
    is_array($user['permissions'] ?? null) ? $user['permissions'] : []
)));
$userRoles = array_values(array_filter(array_map(
    static fn($item): string => strtolower(trim((string)$item)),
    is_array($user['roles'] ?? null) ? $user['roles'] : []
)));
$canApplyUpdates = in_array('all-access', $userPermissions, true)
    || in_array('updates', $userPermissions, true)
    || in_array('version-rollouts', $userPermissions, true)
    || in_array('release', $userPermissions, true)
    || in_array('owner', $userRoles, true)
    || in_array('admin', $userRoles, true)
    || in_array('administrator', $userRoles, true)
    || in_array('super admin', $userRoles, true)
    || in_array('superadmin', $userRoles, true);

$rawBody = file_get_contents('php://input');
$jsonBody = [];
if (is_string($rawBody) && trim($rawBody) !== '') {
    $decoded = json_decode($rawBody, true);
    if (is_array($decoded)) {
        $jsonBody = $decoded;
    }
}
$payload = array_merge($_GET, $_POST, $jsonBody);

$settings = erpSyncReadSettings($pdo);
$meta = erpSyncLocalMeta($settings);
$crmBaseUrl = erpSyncCrmBaseUrl($settings);
$syncToken = erpSyncResolveToken($settings, $payload);
$schoolId = erpSyncResolveSchoolId($settings, $payload);
$currentVersion = erpSyncResolveCurrentVersion($settings, $payload);
$license = apiGetLicenseState($pdo);
$planCode = strtolower(trim((string)($license['plan_code'] ?? '')));
$isRestrictedTier = in_array($planCode, ['basic', 'silver'], true);
$standaloneMode = defined('ERP_STANDALONE_MODE') && ERP_STANDALONE_MODE === true;

$syncConfig = [
    'crm_base_url' => $crmBaseUrl,
    'has_sync_token' => $syncToken !== '',
    'school_id' => $schoolId,
    'current_version' => $currentVersion,
    'local_meta' => $meta,
    'license' => $license,
    'standalone_mode' => $standaloneMode,
];

$resolveRemoteError = static function (array $remote): string {
    $json = is_array($remote['json'] ?? null) ? $remote['json'] : [];
    $message = trim((string)($json['message'] ?? $json['error'] ?? ''));
    if ($message !== '') {
        return $message;
    }
    $error = trim((string)($remote['error'] ?? ''));
    if ($error !== '') {
        return $error;
    }
    $status = (int)($remote['http_status'] ?? 0);
    return $status > 0 ? ('CRM request failed with HTTP ' . $status) : 'CRM request failed';
};

$defaultAllowedUpdatePaths = static function (string $scope): array {
    $scope = strtolower(trim($scope));
    if ($scope === 'erp_only') {
        return ['api/tezzerp/', 'api/v2/', 'tezzerp/'];
    }
    return [
        'api/',
        'api/tezzerp/',
        'api/v2/',
        'tezzerp/',
        'public/',
        'assets/',
        'includes/',
        'index.php',
        'about.php',
        'contact.php',
        'faculty.php',
        'apply.php',
        'login.php',
    ];
};

$normalizeAllowedUpdatePaths = static function ($value) use ($defaultAllowedUpdatePaths): array {
    if (is_string($value)) {
        $value = array_map('trim', explode(',', $value));
    }
    if (!is_array($value)) {
        return [];
    }
    $out = [];
    foreach ($value as $entry) {
        $path = strtolower(trim((string)$entry));
        if ($path === '') {
            continue;
        }
        $path = str_replace('\\', '/', $path);
        while (str_contains($path, '//')) {
            $path = str_replace('//', '/', $path);
        }
        $path = ltrim($path, '/');
        if ($path === '' || str_contains($path, '..')) {
            continue;
        }
        if (str_ends_with($path, '/')) {
            $path = rtrim($path, '/') . '/';
        }
        $out[$path] = true;
    }
    return array_values(array_keys($out));
};

$extractRemoteVersion = static function (array $remoteJson): string {
    return erpSyncNormalizeVersion((string)(
        $remoteJson['latest_version']
        ?? $remoteJson['release_version']
        ?? $remoteJson['version']
        ?? (($remoteJson['latest_release'] ?? [])['version'] ?? '')
    ));
};

$checkUpdate = static function () use ($crmBaseUrl, $syncToken, $schoolId, &$currentVersion): array {
    $query = [
        'mode' => 'check-update',
        'sync_token' => $syncToken,
    ];
    if ($schoolId !== '') {
        $query['school_id'] = $schoolId;
    }
    if ($currentVersion !== '') {
        $query['v'] = $currentVersion;
    }

    $url = rtrim($crmBaseUrl, '/') . '/releases-ui.php?' . http_build_query($query);
    return erpSyncRequest('GET', $url);
};

$resolveVersionFromUpdateHistory = static function () use ($pdo): string {
    try {
        if (!apiTableExists($pdo, 'update_history')) {
            return '';
        }
        $columns = apiTableColumns($pdo, 'update_history');
        if (!in_array('version', $columns, true)) {
            return '';
        }

        $orderBy = '';
        if (in_array('update_date', $columns, true)) {
            $orderBy = ' ORDER BY update_date DESC';
        } elseif (in_array('updated_at', $columns, true)) {
            $orderBy = ' ORDER BY updated_at DESC';
        } elseif (in_array('created_at', $columns, true)) {
            $orderBy = ' ORDER BY created_at DESC';
        } elseif (in_array('id', $columns, true)) {
            $orderBy = ' ORDER BY id DESC';
        }

        $stmt = $pdo->query('SELECT version FROM update_history' . $orderBy . ' LIMIT 1');
        $version = erpSyncNormalizeVersion((string)$stmt->fetchColumn());
        return $version;
    } catch (Throwable $e) {
        error_log('api/v2 update history version read failed: ' . $e->getMessage());
        return '';
    }
};

$parseSqlStatements = static function (string $sql): array {
    $sql = str_replace(["\r\n", "\r"], "\n", $sql);
    $sql = preg_replace('/^\xEF\xBB\xBF/', '', $sql) ?? $sql;

    $length = strlen($sql);
    $buffer = '';
    $statements = [];
    $inSingle = false;
    $inDouble = false;
    $inBacktick = false;

    for ($i = 0; $i < $length; $i++) {
        $char = $sql[$i];
        $next = ($i + 1 < $length) ? $sql[$i + 1] : '';
        $prev = ($i > 0) ? $sql[$i - 1] : '';

        if (!$inSingle && !$inDouble && !$inBacktick) {
            // Strip line comments: -- comment / # comment
            if (
                $char === '-'
                && $next === '-'
                && ($i === 0 || $prev === "\n" || ctype_space((string)$prev))
            ) {
                while ($i < $length && $sql[$i] !== "\n") {
                    $i++;
                }
                continue;
            }
            if ($char === '#' && ($i === 0 || $prev === "\n")) {
                while ($i < $length && $sql[$i] !== "\n") {
                    $i++;
                }
                continue;
            }
            // Strip block comments: /* ... */
            if ($char === '/' && $next === '*') {
                $i += 2;
                while ($i < $length - 1 && !($sql[$i] === '*' && $sql[$i + 1] === '/')) {
                    $i++;
                }
                $i++;
                continue;
            }
        }

        if ($char === "'" && !$inDouble && !$inBacktick) {
            if ($inSingle && $next === "'") {
                $buffer .= "''";
                $i++;
                continue;
            }
            $inSingle = !$inSingle;
            $buffer .= $char;
            continue;
        }
        if ($char === '"' && !$inSingle && !$inBacktick) {
            if ($inDouble && $next === '"') {
                $buffer .= '""';
                $i++;
                continue;
            }
            $inDouble = !$inDouble;
            $buffer .= $char;
            continue;
        }
        if ($char === '`' && !$inSingle && !$inDouble) {
            if ($inBacktick && $next === '`') {
                $buffer .= '``';
                $i++;
                continue;
            }
            $inBacktick = !$inBacktick;
            $buffer .= $char;
            continue;
        }

        if ($char === ';' && !$inSingle && !$inDouble && !$inBacktick) {
            $statement = trim($buffer);
            if ($statement !== '') {
                $statements[] = $statement;
            }
            $buffer = '';
            continue;
        }

        $buffer .= $char;
    }

    $tail = trim($buffer);
    if ($tail !== '') {
        $statements[] = $tail;
    }

    return $statements;
};

$readPackageSqlScripts = static function (string $packageUrl): array {
    $packageUrl = trim($packageUrl);
    if ($packageUrl === '') {
        return [
            'ok' => true,
            'source' => 'none',
            'scripts' => [],
            'errors' => [],
        ];
    }

    if (!extension_loaded('zip')) {
        return [
            'ok' => false,
            'source' => 'package_zip',
            'scripts' => [],
            'errors' => ['PHP zip extension is not enabled on this server'],
        ];
    }

    $parts = parse_url($packageUrl);
    $scheme = strtolower((string)($parts['scheme'] ?? ''));
    if (!in_array($scheme, ['http', 'https'], true)) {
        return [
            'ok' => false,
            'source' => 'package_zip',
            'scripts' => [],
            'errors' => ['package_url must use http or https'],
        ];
    }

    $zipPath = '';
    $zip = null;

    try {
        $tmpBase = tempnam(sys_get_temp_dir(), 'tezzerp_upd_');
        if ($tmpBase === false) {
            throw new RuntimeException('Could not create temporary file for update package');
        }
        @unlink($tmpBase);
        $zipPath = $tmpBase . '.zip';

        $context = stream_context_create([
            'http' => [
                'timeout' => 60,
                'follow_location' => 1,
                'max_redirects' => 3,
                'user_agent' => 'TezzERP-Updater/3.0',
            ],
        ]);

        $source = @fopen($packageUrl, 'rb', false, $context);
        if (!is_resource($source)) {
            throw new RuntimeException('Unable to download update package from package_url');
        }
        $target = @fopen($zipPath, 'wb');
        if (!is_resource($target)) {
            fclose($source);
            throw new RuntimeException('Unable to write downloaded package to temp storage');
        }

        // Keep SQL scan cap aligned with package deploy cap to avoid false failures
        // when full packages are larger than legacy 80MB threshold.
        $maxBytes = 150 * 1024 * 1024; // 150MB safety cap.
        $bytes = stream_copy_to_stream($source, $target, $maxBytes + 1);
        fclose($source);
        fclose($target);

        if (!is_int($bytes) || $bytes <= 0) {
            throw new RuntimeException('Downloaded package is empty or unreadable');
        }
        if ($bytes > $maxBytes) {
            throw new RuntimeException('Package is too large for SQL migration scan (max 150MB)');
        }

        $zip = new ZipArchive();
        if ($zip->open($zipPath) !== true) {
            throw new RuntimeException('Downloaded package is not a valid ZIP archive');
        }

        $normalizePath = static function (string $path): string {
            $path = str_replace('\\', '/', trim($path));
            while (str_contains($path, '//')) {
                $path = str_replace('//', '/', $path);
            }
            $path = ltrim($path, '/');
            if (str_starts_with($path, './')) {
                $path = substr($path, 2);
            }
            return $path;
        };

        $manifestCandidates = [
            'deploy/update-manifest.json',
            'update-manifest.json',
            'deploy/release-manifest.json',
            'release-manifest.json',
        ];
        $manifestPath = '';
        $manifestData = [];
        foreach ($manifestCandidates as $manifestCandidate) {
            $idx = $zip->locateName($manifestCandidate, ZipArchive::FL_NOCASE);
            if (!is_int($idx)) {
                continue;
            }
            $manifestRaw = $zip->getFromIndex($idx);
            if (!is_string($manifestRaw) || trim($manifestRaw) === '') {
                continue;
            }
            $decoded = json_decode($manifestRaw, true);
            if (!is_array($decoded)) {
                continue;
            }
            $manifestPath = $normalizePath((string)$zip->getNameIndex($idx));
            $manifestData = $decoded;
            break;
        }

        $sqlFiles = [];
        if ($manifestData !== []) {
            $manifestSql = $manifestData['sql_files'] ?? null;
            if (!is_array($manifestSql)) {
                $manifestSql = $manifestData['updater']['sql_files'] ?? null;
            }
            if (!is_array($manifestSql)) {
                $manifestSql = $manifestData['migrations'] ?? null;
            }
            if (is_array($manifestSql)) {
                $manifestDir = trim(dirname($manifestPath), '/');
                foreach ($manifestSql as $entry) {
                    $entryPath = $normalizePath((string)$entry);
                    if ($entryPath === '' || str_contains($entryPath, '..')) {
                        continue;
                    }
                    if ($manifestDir !== '' && !str_contains($entryPath, '/')) {
                        $entryPath = $manifestDir . '/' . $entryPath;
                    }
                    if (!preg_match('/\.sql$/i', $entryPath)) {
                        continue;
                    }
                    $sqlFiles[] = $entryPath;
                }
            }
        }

        if ($sqlFiles === []) {
            for ($i = 0; $i < $zip->numFiles; $i++) {
                $name = $normalizePath((string)$zip->getNameIndex($i));
                if ($name === '' || str_ends_with($name, '/')) {
                    continue;
                }
                if (!preg_match('/\.sql$/i', $name)) {
                    continue;
                }
                if (!preg_match('#(^|/)deploy/sql/#i', $name)) {
                    continue;
                }
                $sqlFiles[] = $name;
            }
        }

        $sqlFiles = array_values(array_unique($sqlFiles));
        sort($sqlFiles, SORT_NATURAL | SORT_FLAG_CASE);

        $scripts = [];
        foreach ($sqlFiles as $sqlPath) {
            $body = $zip->getFromName($sqlPath, 0, ZipArchive::FL_NOCASE);
            if (!is_string($body)) {
                $idx = $zip->locateName($sqlPath, ZipArchive::FL_NOCASE);
                if (is_int($idx)) {
                    $body = (string)$zip->getFromIndex($idx);
                    $sqlPath = $normalizePath((string)$zip->getNameIndex($idx));
                }
            }
            if (!is_string($body) || trim($body) === '') {
                continue;
            }
            if (strlen($body) > (5 * 1024 * 1024)) {
                return [
                    'ok' => false,
                    'source' => 'package_zip',
                    'scripts' => [],
                    'errors' => ['SQL file too large to execute safely: ' . $sqlPath],
                ];
            }
            $scripts[] = [
                'name' => $sqlPath,
                'hash' => hash('sha256', $sqlPath . "\n" . $body),
                'sql' => $body,
                'source' => 'package_zip',
            ];
        }

        return [
            'ok' => true,
            'source' => 'package_zip',
            'package_url' => $packageUrl,
            'manifest_path' => $manifestPath,
            'scripts' => $scripts,
            'errors' => [],
        ];
    } catch (Throwable $e) {
        return [
            'ok' => false,
            'source' => 'package_zip',
            'scripts' => [],
            'errors' => [$e->getMessage()],
        ];
    } finally {
        if ($zip instanceof ZipArchive) {
            $zip->close();
        }
        if ($zipPath !== '' && is_file($zipPath)) {
            @unlink($zipPath);
        }
    }
};

$readLocalVersionSqlScripts = static function (string $targetVersion): array {
    $targetVersion = trim($targetVersion);
    if ($targetVersion === '') {
        return [];
    }

    $root = dirname(__DIR__, 2);
    $sqlDir = $root . '/deploy/sql';
    if (!is_dir($sqlDir)) {
        return [];
    }

    $tokenRaw = strtolower($targetVersion);
    $token = preg_replace('/[^a-z0-9]+/i', '_', $tokenRaw);
    $token = trim((string)$token, '_');
    if ($token === '') {
        return [];
    }

    $patterns = [
        $sqlDir . '/*' . $token . '*.sql',
        $sqlDir . '/*' . str_replace('_', '', $token) . '*.sql',
    ];

    $files = [];
    foreach ($patterns as $pattern) {
        foreach (glob($pattern) ?: [] as $file) {
            if (!is_string($file) || !is_file($file) || !is_readable($file)) {
                continue;
            }
            $files[] = $file;
        }
    }
    $files = array_values(array_unique($files));
    sort($files, SORT_NATURAL | SORT_FLAG_CASE);

    $scripts = [];
    foreach ($files as $file) {
        $body = @file_get_contents($file);
        if (!is_string($body) || trim($body) === '') {
            continue;
        }
        if (strlen($body) > (5 * 1024 * 1024)) {
            continue;
        }
        $name = ltrim(str_replace('\\', '/', str_replace($root, '', $file)), '/');
        $scripts[] = [
            'name' => $name,
            'hash' => hash('sha256', $name . "\n" . $body),
            'sql' => $body,
            'source' => 'local',
        ];
    }

    return $scripts;
};

$historyVersion = $resolveVersionFromUpdateHistory();
if ($historyVersion !== '') {
    $currentVersion = $historyVersion;
    $syncConfig['current_version'] = $historyVersion;
}

if ($method === 'GET' || strtolower(trim((string)($payload['action'] ?? ''))) === 'check_update') {
    if ($standaloneMode) {
        jsonResponse(200, [
            'success' => true,
            'message' => 'Standalone ERP mode: local update state only',
            'data' => [
                'sync' => $syncConfig,
                'update_available' => false,
                'remote' => [
                    'current_version' => $currentVersion !== '' ? $currentVersion : trim((string)($meta['current_version'] ?? '')),
                    'source' => 'local',
                ],
            ],
        ]);
    }

    if ($syncToken === '') {
        jsonResponse(409, [
            'success' => false,
            'message' => 'Sync token is missing. Set CRM sync settings in School Settings first.',
            'data' => [
                'sync' => $syncConfig,
                'update_available' => false,
            ],
        ]);
    }

    $remote = $checkUpdate();
    if (!$remote['ok']) {
        jsonResponse(502, [
            'success' => false,
            'message' => $resolveRemoteError($remote),
            'data' => [
                'sync' => $syncConfig,
                'update_available' => false,
            ],
        ]);
    }

    $remoteJson = is_array($remote['json']) ? $remote['json'] : [];
    $remoteScope = strtolower(trim((string)($remoteJson['package_scope'] ?? '')));
    if ($isRestrictedTier && $remoteScope !== '' && $remoteScope !== 'erp_only') {
        $remoteJson['package_scope'] = 'erp_only';
        $remoteJson['scope_enforced_local'] = true;
    }
    if (!isset($remoteJson['allowed_update_paths']) || !is_array($remoteJson['allowed_update_paths'])) {
        $remoteJson['allowed_update_paths'] = $defaultAllowedUpdatePaths((string)($remoteJson['package_scope'] ?? 'full'));
    }
    if ((string)($remoteJson['package_scope'] ?? '') === 'erp_only') {
        $remoteJson['allowed_update_paths'] = array_values(array_unique(array_merge(
            ['api/tezzerp/', 'tezzerp/'],
            $normalizeAllowedUpdatePaths($remoteJson['allowed_update_paths'])
        )));
    } else {
        $remoteJson['allowed_update_paths'] = $normalizeAllowedUpdatePaths($remoteJson['allowed_update_paths']);
    }
    $updateAvailable = !empty($remoteJson['update_available']);

    jsonResponse(200, [
        'success' => true,
        'message' => $updateAvailable ? 'Update available' : 'ERP is up-to-date',
        'data' => [
            'sync' => $syncConfig,
            'update_available' => $updateAvailable,
            'remote' => $remoteJson,
        ],
    ]);
}

$action = strtolower(trim((string)($payload['action'] ?? '')));
if (!in_array($action, ['apply_update', 'update_apply'], true)) {
    jsonResponse(400, [
        'success' => false,
        'message' => 'Unsupported action',
    ]);
}

if (!$canApplyUpdates) {
    jsonResponse(403, [
        'success' => false,
        'message' => 'Only organization owner/admin can apply updates',
    ]);
}

$targetVersion = erpSyncNormalizeVersion((string)(
    $payload['version']
    ?? $payload['release_version']
    ?? $payload['target_version']
    ?? ''
));
if ($targetVersion === '') {
    jsonResponse(400, [
        'success' => false,
        'message' => 'version is required',
    ]);
}

$releaseId = trim((string)($payload['release_id'] ?? ''));
$packageUrl = erpSyncNormalizeUrl((string)($payload['package_url'] ?? ''));
$packageScope = strtolower(trim((string)($payload['package_scope'] ?? '')));
$allowedUpdatePaths = $normalizeAllowedUpdatePaths($payload['allowed_update_paths'] ?? []);
$remoteApplyPayload = [];

if (!$standaloneMode && $syncToken !== '' && ($packageUrl === '' || $packageScope === '' || $releaseId === '' || $allowedUpdatePaths === [])) {
    $remote = $checkUpdate();
    if ($remote['ok']) {
        $remoteJson = is_array($remote['json']) ? $remote['json'] : [];
        if (is_array($remoteJson)) {
            $remoteApplyPayload = $remoteJson;
            $remoteVersion = $extractRemoteVersion($remoteJson);
            $isSameTarget = $remoteVersion !== '' && $remoteVersion === $targetVersion;
            if ($isSameTarget || $packageUrl === '') {
                if ($releaseId === '') {
                    $releaseId = trim((string)($remoteJson['release_id'] ?? $remoteJson['id'] ?? ''));
                }
                if ($packageUrl === '') {
                    $packageUrl = erpSyncNormalizeUrl((string)($remoteJson['package_url'] ?? ''));
                }
                if ($packageScope === '') {
                    $packageScope = strtolower(trim((string)($remoteJson['package_scope'] ?? '')));
                }
                if ($allowedUpdatePaths === []) {
                    $allowedUpdatePaths = $normalizeAllowedUpdatePaths($remoteJson['allowed_update_paths'] ?? []);
                }
            }
        }
    }
}

if ($packageScope === '') {
    $packageScope = 'full';
}

if ($allowedUpdatePaths === []) {
    $allowedUpdatePaths = $defaultAllowedUpdatePaths($packageScope);
}
if ($packageScope === 'erp_only') {
    $allowedUpdatePaths = array_values(array_unique(array_merge(
        ['api/tezzerp/', 'tezzerp/'],
        $normalizeAllowedUpdatePaths($allowedUpdatePaths)
    )));
}

if ($isRestrictedTier && $packageScope !== 'erp_only') {
    jsonResponse(403, [
        'success' => false,
        'message' => 'This plan can apply ERP-only updates only',
        'data' => [
            'plan_code' => $planCode,
            'required_scope' => 'erp_only',
            'package_scope' => $packageScope,
        ],
    ]);
}

if ($packageUrl === '') {
    jsonResponse(400, [
        'success' => false,
        'message' => 'package_url is required to apply update package',
        'data' => [
            'target_version' => $targetVersion,
            'release_id' => $releaseId,
            'package_scope' => $packageScope,
            'allowed_update_paths' => $allowedUpdatePaths,
            'remote' => $remoteApplyPayload,
        ],
    ]);
}

$runSafeMigrations = static function () use ($pdo, $targetVersion): array {
    $steps = [];
    $errors = [];
    $warnings = [];

    $runStep = static function (string $name, callable $callback, bool $strict = true) use (&$steps, &$errors, &$warnings): void {
        try {
            $callback();
            $steps[] = ['step' => $name, 'ok' => true];
        } catch (Throwable $e) {
            if ($strict) {
                $steps[] = ['step' => $name, 'ok' => false, 'error' => $e->getMessage()];
                $errors[] = $name . ': ' . $e->getMessage();
            } else {
                $steps[] = ['step' => $name, 'ok' => true, 'warning' => $e->getMessage(), 'soft_failed' => true];
                $warnings[] = $name . ': ' . $e->getMessage();
            }
        }
    };

    $runStep('ensure_system_settings_table', static function () use ($pdo): void {
        $pdo->exec(
            "CREATE TABLE IF NOT EXISTS system_settings (
                setting_key VARCHAR(191) NOT NULL,
                setting_value LONGTEXT NULL,
                created_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
                updated_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
                PRIMARY KEY (setting_key)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"
        );
    });

    $runStep('ensure_update_history_table', static function () use ($pdo): void {
        $pdo->exec(
            "CREATE TABLE IF NOT EXISTS update_history (
                id INT(11) NOT NULL AUTO_INCREMENT,
                version VARCHAR(40) NOT NULL,
                description VARCHAR(255) NULL,
                applied_by VARCHAR(160) NULL,
                update_date DATETIME NULL,
                created_at DATETIME NULL,
                updated_at DATETIME NULL,
                PRIMARY KEY (id),
                KEY idx_update_history_version (version)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"
        );
    });

    $runStep('disable_runtime_seeding', static function () use ($pdo): void {
        if (function_exists('apiSystemSettingsSet')) {
            apiSystemSettingsSet($pdo, 'runtime_seeding_disabled', '1');
        }
    }, false);

    $schemaCallbacks = [
        'ensure_fee_schema' => 'apiEnsureFeeOpsSchema',
        'ensure_exams_schema' => 'apiEnsureExamsSchema',
        'ensure_results_schema' => 'apiEnsureResultsSchema',
        'ensure_inventory_schema' => 'apiEnsureInventorySchema',
        'ensure_canteen_schema' => 'apiEnsureCanteenSchema',
        'ensure_hostel_schema' => 'apiEnsureHostelSchema',
        'ensure_payroll_schema' => 'apiEnsurePayrollSchema',
        'ensure_plan_links_table' => 'apiEnsurePlanLinksTable',
        // Intentionally no runtime master data inserts.
        'ensure_basic_plan_guardrails' => 'apiEnsureBasicPlanMasterData',
    ];

    foreach ($schemaCallbacks as $stepName => $fn) {
        if (!function_exists($fn)) {
            // Non-blocking: some schools may not include optional module callbacks.
            $steps[] = ['step' => $stepName, 'ok' => true, 'warning' => $fn . ' missing', 'soft_failed' => true];
            $warnings[] = $stepName . ': callback missing';
            continue;
        }
        $runStep($stepName, static function () use ($fn, $pdo): void {
            $fn($pdo);
        }, false);
    }

    $runStep('persist_target_version_hints', static function () use ($pdo, $targetVersion): void {
        if (function_exists('apiSystemSettingsSet')) {
            apiSystemSettingsSet($pdo, 'current_version', $targetVersion);
            apiSystemSettingsSet($pdo, 'installed_version', $targetVersion);
            apiSystemSettingsSet($pdo, 'system_version', $targetVersion);
        }
    }, false);

    return [
        'ok' => $errors === [],
        'steps' => $steps,
        'errors' => $errors,
        'warnings' => $warnings,
    ];
};

$runPackageSqlMigrations = static function () use (
    $pdo,
    $packageUrl,
    $targetVersion,
    $releaseId,
    $user,
    $parseSqlStatements,
    $readPackageSqlScripts,
    $readLocalVersionSqlScripts
): array {
    $steps = [];
    $errors = [];
    $warnings = [];

    $pushStep = static function (string $step, bool $ok, array $meta = []) use (&$steps, &$errors, &$warnings): void {
        $row = array_merge(['step' => $step, 'ok' => $ok], $meta);
        $steps[] = $row;
        if (!$ok && empty($meta['warning'])) {
            $message = (string)($meta['error'] ?? $step . ' failed');
            $errors[] = $step . ': ' . $message;
        } elseif (!empty($meta['warning'])) {
            $warnings[] = $step . ': ' . (string)$meta['warning'];
        }
    };

    try {
        $pdo->exec(
            "CREATE TABLE IF NOT EXISTS update_sql_history (
                id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
                version VARCHAR(40) NOT NULL,
                release_id VARCHAR(80) NULL,
                script_name VARCHAR(255) NOT NULL,
                script_hash CHAR(64) NOT NULL,
                status VARCHAR(20) NOT NULL DEFAULT 'success',
                applied_by VARCHAR(160) NULL,
                statements_total INT NOT NULL DEFAULT 0,
                error_message TEXT NULL,
                applied_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
                PRIMARY KEY (id),
                KEY idx_update_sql_history_version (version),
                KEY idx_update_sql_history_release (release_id),
                KEY idx_update_sql_history_hash (script_hash),
                KEY idx_update_sql_history_status (status)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"
        );
        $pushStep('ensure_update_sql_history_table', true);
    } catch (Throwable $e) {
        $pushStep('ensure_update_sql_history_table', false, ['error' => $e->getMessage()]);
        return [
            'ok' => false,
            'steps' => $steps,
            'errors' => $errors,
            'scripts_total' => 0,
            'applied_count' => 0,
            'skipped_count' => 0,
        ];
    }

    $packageScriptsResponse = $readPackageSqlScripts($packageUrl);
    if (!$packageScriptsResponse['ok']) {
        $pushStep('load_package_sql_scripts', false, ['error' => implode('; ', $packageScriptsResponse['errors'] ?? [])]);
        return [
            'ok' => false,
            'steps' => $steps,
            'errors' => $errors,
            'scripts_total' => 0,
            'applied_count' => 0,
            'skipped_count' => 0,
        ];
    }

    $pushStep('load_package_sql_scripts', true, [
        'source' => (string)($packageScriptsResponse['source'] ?? 'none'),
        'scripts' => count((array)($packageScriptsResponse['scripts'] ?? [])),
    ]);

    $scripts = [];
    $seenHashes = [];
    foreach ((array)($packageScriptsResponse['scripts'] ?? []) as $script) {
        if (!is_array($script)) {
            continue;
        }
        $hash = strtolower(trim((string)($script['hash'] ?? '')));
        if ($hash === '' || isset($seenHashes[$hash])) {
            continue;
        }
        $seenHashes[$hash] = true;
        $scripts[] = $script;
    }

    // Local fallback: if package_url not provided, try target-versioned deploy/sql files.
    if ($scripts === []) {
        foreach ($readLocalVersionSqlScripts($targetVersion) as $script) {
            if (!is_array($script)) {
                continue;
            }
            $hash = strtolower(trim((string)($script['hash'] ?? '')));
            if ($hash === '' || isset($seenHashes[$hash])) {
                continue;
            }
            $seenHashes[$hash] = true;
            $scripts[] = $script;
        }
        if ($scripts !== []) {
            $pushStep('load_local_version_sql_scripts', true, ['scripts' => count($scripts)]);
        }
    }

    $scriptsTotal = count($scripts);
    if ($scriptsTotal === 0) {
        $pushStep('sql_scripts_not_required', true, ['message' => 'No SQL migration script found in package or local version folder']);
        return [
            'ok' => true,
            'steps' => $steps,
            'errors' => [],
            'scripts_total' => 0,
            'applied_count' => 0,
            'skipped_count' => 0,
            'source' => (string)($packageScriptsResponse['source'] ?? 'none'),
            'manifest_path' => (string)($packageScriptsResponse['manifest_path'] ?? ''),
        ];
    }

    $checkApplied = $pdo->prepare(
        "SELECT id FROM update_sql_history
         WHERE script_hash = :hash
           AND status = 'success'
         ORDER BY id DESC
         LIMIT 1"
    );
    $insertHistory = $pdo->prepare(
        "INSERT INTO update_sql_history
            (version, release_id, script_name, script_hash, status, applied_by, statements_total, error_message, applied_at)
         VALUES
            (:version, :release_id, :script_name, :script_hash, :status, :applied_by, :statements_total, :error_message, NOW())"
    );

    $appliedCount = 0;
    $skippedCount = 0;

    $executeSqlStatement = static function (string $statement) use ($pdo): void {
        $stmt = $pdo->prepare($statement);
        if (!$stmt) {
            throw new RuntimeException('Failed to prepare SQL statement');
        }

        $stmt->execute();
        // Drain all possible rowsets (CALL / SELECT / procedure outputs) to avoid
        // "Cannot execute queries while other unbuffered queries are active".
        do {
            while ($stmt->fetch(PDO::FETCH_NUM)) {
                // no-op: consume current rowset
            }
        } while ($stmt->nextRowset());
        $stmt->closeCursor();
    };

    $isIgnorableSqlError = static function (Throwable $e, string $statement): bool {
        $msg = strtolower(trim($e->getMessage()));
        $stmt = strtolower(trim($statement));
        $mysqlCode = 0;
        if ($e instanceof PDOException && is_array($e->errorInfo ?? null) && isset($e->errorInfo[1])) {
            $mysqlCode = (int)$e->errorInfo[1];
        }

        if (str_contains($msg, "data truncated for column 'template_type'")) {
            return true;
        }

        $ignorableCodes = [
            1050, // table exists
            1051, // unknown table
            1054, // unknown column
            1060, // duplicate column
            1061, // duplicate key name
            1062, // duplicate entry
            1091, // can't drop/check that doesn't exist
            1146, // table doesn't exist
            1265, // data truncated (legacy enum/value drift)
        ];
        if ($mysqlCode > 0 && in_array($mysqlCode, $ignorableCodes, true)) {
            // Keep this strict except known legacy-safe drift case for template type.
            if (
                $mysqlCode === 1265
                && !str_contains($msg, "data truncated for column 'template_type'")
            ) {
                return false;
            }
            return true;
        }

        $msgHints = [
            'duplicate column name',
            'duplicate key name',
            'already exists',
            'doesn\'t exist',
            'unknown column',
            'unknown table',
            'duplicate entry',
            'cannot drop',
        ];
        foreach ($msgHints as $hint) {
            if (str_contains($msg, $hint)) {
                // Only ignore for idempotent/ddl style statements.
                if (
                    str_starts_with($stmt, 'alter table')
                    || str_starts_with($stmt, 'create table')
                    || str_starts_with($stmt, 'create index')
                    || str_starts_with($stmt, 'drop table')
                    || str_starts_with($stmt, 'drop index')
                ) {
                    return true;
                }
                // Explicit legacy-safe mapping drift in template preference key enum.
                if (
                    $hint === 'data truncated'
                    && str_contains($msg, "data truncated for column 'template_type'")
                    && (
                        str_starts_with($stmt, 'insert ')
                        || str_starts_with($stmt, 'update ')
                    )
                ) {
                    return true;
                }
            }
        }

        return false;
    };

    foreach ($scripts as $script) {
        $scriptName = substr((string)($script['name'] ?? 'script.sql'), 0, 255);
        $scriptHash = strtolower(trim((string)($script['hash'] ?? '')));
        $scriptSql = (string)($script['sql'] ?? '');
        if ($scriptHash === '') {
            $pushStep('script_hash_missing', false, ['error' => 'Missing hash for ' . $scriptName]);
            break;
        }

        $checkApplied->execute([':hash' => $scriptHash]);
        $alreadyApplied = ($checkApplied->fetchColumn() !== false);
        $checkApplied->closeCursor();
        if ($alreadyApplied) {
            $skippedCount++;
            $pushStep('skip_already_applied_sql', true, ['script' => $scriptName]);
            continue;
        }

        $statements = $parseSqlStatements($scriptSql);
        if ($statements === []) {
            $insertHistory->execute([
                ':version' => $targetVersion,
                ':release_id' => $releaseId !== '' ? substr($releaseId, 0, 80) : null,
                ':script_name' => $scriptName,
                ':script_hash' => $scriptHash,
                ':status' => 'success',
                ':applied_by' => substr((string)($user['email'] ?? $user['name'] ?? 'system'), 0, 160),
                ':statements_total' => 0,
                ':error_message' => null,
            ]);
            $appliedCount++;
            $pushStep('apply_sql_script', true, ['script' => $scriptName, 'statements' => 0]);
            continue;
        }

        try {
            foreach ($statements as $statement) {
                try {
                    $executeSqlStatement($statement);
                } catch (Throwable $e) {
                    if ($isIgnorableSqlError($e, $statement)) {
                        $pushStep('sql_statement_warning', true, [
                            'script' => $scriptName,
                            'warning' => $e->getMessage(),
                        ]);
                        continue;
                    }
                    throw $e;
                }
            }

            $insertHistory->execute([
                ':version' => $targetVersion,
                ':release_id' => $releaseId !== '' ? substr($releaseId, 0, 80) : null,
                ':script_name' => $scriptName,
                ':script_hash' => $scriptHash,
                ':status' => 'success',
                ':applied_by' => substr((string)($user['email'] ?? $user['name'] ?? 'system'), 0, 160),
                ':statements_total' => count($statements),
                ':error_message' => null,
            ]);

            $appliedCount++;
            $pushStep('apply_sql_script', true, ['script' => $scriptName, 'statements' => count($statements)]);
        } catch (Throwable $e) {
            $insertHistory->execute([
                ':version' => $targetVersion,
                ':release_id' => $releaseId !== '' ? substr($releaseId, 0, 80) : null,
                ':script_name' => $scriptName,
                ':script_hash' => $scriptHash,
                ':status' => 'failed',
                ':applied_by' => substr((string)($user['email'] ?? $user['name'] ?? 'system'), 0, 160),
                ':statements_total' => count($statements),
                ':error_message' => substr($e->getMessage(), 0, 65500),
            ]);
            $pushStep('apply_sql_script', false, [
                'script' => $scriptName,
                'statements' => count($statements),
                'error' => $e->getMessage(),
            ]);
            break;
        }
    }

    return [
        'ok' => $errors === [],
        'steps' => $steps,
        'errors' => $errors,
        'warnings' => $warnings,
        'scripts_total' => $scriptsTotal,
        'applied_count' => $appliedCount,
        'skipped_count' => $skippedCount,
        'source' => (string)($packageScriptsResponse['source'] ?? 'none'),
        'manifest_path' => (string)($packageScriptsResponse['manifest_path'] ?? ''),
    ];
};

$applyPackageFiles = static function () use (
    $packageUrl,
    $packageScope,
    $allowedUpdatePaths
): array {
    if ($packageUrl === '') {
        return [
            'ok' => false,
            'steps' => [['step' => 'resolve_package_url', 'ok' => false, 'error' => 'package_url is empty']],
            'errors' => ['package_url is empty'],
        ];
    }
    if (!extension_loaded('zip')) {
        return [
            'ok' => false,
            'steps' => [['step' => 'zip_extension', 'ok' => false, 'error' => 'PHP zip extension missing']],
            'errors' => ['PHP zip extension missing'],
        ];
    }

    $rootPath = dirname(__DIR__, 2);
    $normalizePath = static function (string $path): string {
        $path = str_replace('\\', '/', trim($path));
        while (str_contains($path, '//')) {
            $path = str_replace('//', '/', $path);
        }
        $path = ltrim($path, '/');
        if (str_starts_with($path, './')) {
            $path = substr($path, 2);
        }
        return $path;
    };
    $isAllowedPath = static function (string $relative, array $allowed) use ($normalizePath): bool {
        $relative = strtolower($normalizePath($relative));
        if ($relative === '' || str_contains($relative, '..')) {
            return false;
        }
        foreach ($allowed as $rule) {
            $rule = strtolower($normalizePath((string)$rule));
            if ($rule === '') {
                continue;
            }
            if (str_ends_with($rule, '/')) {
                if (str_starts_with($relative, $rule)) {
                    return true;
                }
                continue;
            }
            if ($relative === $rule) {
                return true;
            }
        }
        return false;
    };
    $protectedUpdaterFiles = array_fill_keys([
        'api/tezzerp/erp-updates.php',
        'api/tezzerp/updates/releases.php',
        'api/v2/erp-updates.php',
        'api/v2/updates/releases.php',
    ], true);
    $removeDir = static function (string $dir) use (&$removeDir): void {
        if (!is_dir($dir)) {
            return;
        }
        $items = @scandir($dir) ?: [];
        foreach ($items as $item) {
            if ($item === '.' || $item === '..') {
                continue;
            }
            $path = $dir . DIRECTORY_SEPARATOR . $item;
            if (is_dir($path)) {
                $removeDir($path);
            } else {
                @unlink($path);
            }
        }
        @rmdir($dir);
    };

    $steps = [];
    $errors = [];
    $zipPath = '';
    $extractDir = '';
    $zip = null;

    try {
        $tmpBase = tempnam(sys_get_temp_dir(), 'tezzerp_pkg_');
        if ($tmpBase === false) {
            throw new RuntimeException('Unable to create temp storage for package');
        }
        @unlink($tmpBase);
        $zipPath = $tmpBase . '.zip';
        $extractDir = $tmpBase . '_dir';
        if (!@mkdir($extractDir, 0775, true) && !is_dir($extractDir)) {
            throw new RuntimeException('Unable to create temp extract directory');
        }

        $context = stream_context_create([
            'http' => [
                'timeout' => 120,
                'follow_location' => 1,
                'max_redirects' => 5,
                'user_agent' => 'TezzERP-Updater/3.0',
            ],
        ]);
        $source = @fopen($packageUrl, 'rb', false, $context);
        if (!is_resource($source)) {
            throw new RuntimeException('Unable to download package from package_url');
        }
        $target = @fopen($zipPath, 'wb');
        if (!is_resource($target)) {
            fclose($source);
            throw new RuntimeException('Unable to write package into temp storage');
        }
        $maxBytes = 150 * 1024 * 1024;
        $bytes = stream_copy_to_stream($source, $target, $maxBytes + 1);
        fclose($source);
        fclose($target);
        if (!is_int($bytes) || $bytes <= 0) {
            throw new RuntimeException('Downloaded package is empty');
        }
        if ($bytes > $maxBytes) {
            throw new RuntimeException('Package exceeds 150MB update limit');
        }
        $steps[] = ['step' => 'download_package', 'ok' => true, 'bytes' => $bytes];

        $zip = new ZipArchive();
        if ($zip->open($zipPath) !== true) {
            throw new RuntimeException('Package is not a valid ZIP archive');
        }

        $entryNames = [];
        $topLevels = [];
        for ($i = 0; $i < $zip->numFiles; $i++) {
            $name = $normalizePath((string)$zip->getNameIndex($i));
            if ($name === '') {
                continue;
            }
            $entryNames[] = $name;
            $segment = explode('/', $name, 2)[0] ?? '';
            if ($segment !== '') {
                $topLevels[$segment] = true;
            }
        }
        $steps[] = ['step' => 'scan_package_entries', 'ok' => true, 'entries' => count($entryNames)];

        $hasPrefix = static function (array $names, string $prefix): bool {
            $prefix = strtolower(trim($prefix));
            foreach ($names as $name) {
                if (str_starts_with(strtolower($name), $prefix)) {
                    return true;
                }
            }
            return false;
        };

        $packageRoot = '';
        if ($packageScope === 'erp_only' && $hasPrefix($entryNames, 'erp-only-package/')) {
            $packageRoot = 'erp-only-package';
        } elseif ($packageScope === 'full' && $hasPrefix($entryNames, 'full-package/')) {
            $packageRoot = 'full-package';
        } elseif ($hasPrefix($entryNames, 'package/')) {
            $packageRoot = 'package';
        } elseif (count($topLevels) === 1) {
            $first = array_key_first($topLevels);
            if (is_string($first) && $first !== '' && (
                $hasPrefix($entryNames, strtolower($first) . '/api/tezzerp/')
                || $hasPrefix($entryNames, strtolower($first) . '/tezzerp/')
            )) {
                $packageRoot = $first;
            }
        }

        if (!$zip->extractTo($extractDir)) {
            throw new RuntimeException('Unable to extract package');
        }
        $steps[] = ['step' => 'extract_package', 'ok' => true, 'package_root' => $packageRoot];

        $sourceRoot = rtrim($extractDir, '/');
        $packageRootPrefix = '';
        if ($packageRoot !== '') {
            $packageRootPrefix = strtolower($packageRoot . '/');
        }

        $candidateFiles = 0;
        $copiedFiles = 0;
        $unchangedFiles = 0;
        $skippedFiles = 0;
        $protectedSkipped = 0;
        $protectedCandidates = 0;
        $protectedApplied = 0;
        $protectedUnchanged = 0;
        $protectedQueued = [];
        $protectedErrors = [];
        $protectedAppliedPaths = [];
        $appliedPaths = [];

        $iterator = new RecursiveIteratorIterator(
            new RecursiveDirectoryIterator($sourceRoot, FilesystemIterator::SKIP_DOTS),
            RecursiveIteratorIterator::SELF_FIRST
        );
        foreach ($iterator as $item) {
            if (!$item instanceof SplFileInfo || !$item->isFile()) {
                continue;
            }
            $absPath = $item->getPathname();
            $rel = $normalizePath(substr($absPath, strlen($sourceRoot) + 1));
            if ($rel === '' || str_contains($rel, '..')) {
                $skippedFiles++;
                continue;
            }
            if ($packageRootPrefix !== '') {
                $relLower = strtolower($rel);
                if (!str_starts_with($relLower, $packageRootPrefix)) {
                    $skippedFiles++;
                    continue;
                }
                $rel = substr($rel, strlen($packageRoot) + 1);
                if ($rel === '' || str_contains($rel, '..')) {
                    $skippedFiles++;
                    continue;
                }
            }
            if (!$isAllowedPath($rel, $allowedUpdatePaths)) {
                $skippedFiles++;
                continue;
            }
            if (isset($protectedUpdaterFiles[strtolower($rel)])) {
                $protectedSkipped++;
                $protectedQueued[] = ['src' => $absPath, 'rel' => $rel];
                continue;
            }
            if (str_starts_with(strtolower($rel), 'deploy/') || str_starts_with(strtolower($rel), 'sql/')) {
                $skippedFiles++;
                continue;
            }
            $candidateFiles++;
            $destPath = rtrim($rootPath, '/\\') . '/' . $rel;
            $destDir = dirname($destPath);
            if (!is_dir($destDir) && !@mkdir($destDir, 0775, true) && !is_dir($destDir)) {
                throw new RuntimeException('Unable to create destination directory: ' . $destDir);
            }
            $same = is_file($destPath)
                && hash_file('sha256', $absPath) === hash_file('sha256', $destPath);
            if ($same) {
                $unchangedFiles++;
                continue;
            }
            $tmpDest = $destPath . '.updtmp_' . bin2hex(random_bytes(4));
            if (!@copy($absPath, $tmpDest)) {
                throw new RuntimeException('Unable to copy file: ' . $rel);
            }
            if (!@rename($tmpDest, $destPath)) {
                @unlink($tmpDest);
                throw new RuntimeException('Unable to replace file: ' . $rel);
            }
            @chmod($destPath, 0644);
            $copiedFiles++;
            if (count($appliedPaths) < 80) {
                $appliedPaths[] = $rel;
            }
        }

        if ($candidateFiles === 0 && count($protectedQueued) === 0) {
            throw new RuntimeException('No updatable files found in package for allowed paths');
        }

        foreach ($protectedQueued as $protectedItem) {
            $absPath = (string)($protectedItem['src'] ?? '');
            $rel = (string)($protectedItem['rel'] ?? '');
            if ($absPath === '' || $rel === '') {
                continue;
            }
            $protectedCandidates++;
            $destPath = rtrim($rootPath, '/\\') . '/' . $rel;
            $destDir = dirname($destPath);
            if (!is_dir($destDir) && !@mkdir($destDir, 0775, true) && !is_dir($destDir)) {
                $protectedErrors[] = 'Unable to create destination directory: ' . $destDir;
                continue;
            }
            $same = is_file($destPath)
                && hash_file('sha256', $absPath) === hash_file('sha256', $destPath);
            if ($same) {
                $protectedUnchanged++;
                continue;
            }
            $tmpDest = $destPath . '.updtmp_' . bin2hex(random_bytes(4));
            if (!@copy($absPath, $tmpDest)) {
                $protectedErrors[] = 'Unable to copy protected updater file: ' . $rel;
                continue;
            }
            if (!@rename($tmpDest, $destPath)) {
                @unlink($tmpDest);
                $protectedErrors[] = 'Unable to replace protected updater file: ' . $rel;
                continue;
            }
            @chmod($destPath, 0644);
            $protectedApplied++;
            if (count($protectedAppliedPaths) < 20) {
                $protectedAppliedPaths[] = $rel;
            }
        }

        $steps[] = [
            'step' => 'apply_package_files',
            'ok' => true,
            'allowed_update_paths' => array_values($allowedUpdatePaths),
            'candidate_files' => $candidateFiles,
            'copied_files' => $copiedFiles,
            'unchanged_files' => $unchangedFiles,
            'skipped_files' => $skippedFiles,
            'protected_updater_files_skipped' => $protectedSkipped,
            'protected_updater_candidates' => $protectedCandidates,
            'protected_updater_applied' => $protectedApplied,
            'protected_updater_unchanged' => $protectedUnchanged,
            'protected_updater_errors' => $protectedErrors,
            'protected_updater_applied_sample' => $protectedAppliedPaths,
            'applied_paths_sample' => $appliedPaths,
        ];

        return [
            'ok' => true,
            'steps' => $steps,
            'errors' => [],
            'allowed_update_paths' => array_values($allowedUpdatePaths),
            'candidate_files' => $candidateFiles,
            'copied_files' => $copiedFiles,
            'unchanged_files' => $unchangedFiles,
            'skipped_files' => $skippedFiles,
            'protected_updater_files_skipped' => $protectedSkipped,
            'protected_updater_candidates' => $protectedCandidates,
            'protected_updater_applied' => $protectedApplied,
            'protected_updater_unchanged' => $protectedUnchanged,
            'protected_updater_errors' => $protectedErrors,
            'protected_updater_applied_sample' => $protectedAppliedPaths,
            'package_scope' => $packageScope,
            'package_url' => $packageUrl,
        ];
    } catch (Throwable $e) {
        $steps[] = ['step' => 'apply_package_files', 'ok' => false, 'error' => $e->getMessage()];
        $errors[] = $e->getMessage();
        return [
            'ok' => false,
            'steps' => $steps,
            'errors' => $errors,
            'allowed_update_paths' => array_values($allowedUpdatePaths),
            'package_scope' => $packageScope,
            'package_url' => $packageUrl,
        ];
    } finally {
        if ($zip instanceof ZipArchive) {
            $zip->close();
        }
        if ($zipPath !== '' && is_file($zipPath)) {
            @unlink($zipPath);
        }
        if ($extractDir !== '' && is_dir($extractDir)) {
            $removeDir($extractDir);
        }
    }
};

$migration = $runSafeMigrations();
if (!$migration['ok']) {
    jsonResponse(500, [
        'success' => false,
        'message' => 'Update migration failed. Version not marked installed.',
        'data' => [
            'target_version' => $targetVersion,
            'migration' => $migration,
        ],
    ]);
}

$packageSqlMigration = $runPackageSqlMigrations();
if (!$packageSqlMigration['ok']) {
    jsonResponse(500, [
        'success' => false,
        'message' => 'Update SQL migration failed. Version not marked installed.',
        'data' => [
            'target_version' => $targetVersion,
            'migration' => $migration,
            'package_sql' => $packageSqlMigration,
        ],
    ]);
}

$packageFileApply = $applyPackageFiles();
if (!$packageFileApply['ok']) {
    jsonResponse(500, [
        'success' => false,
        'message' => 'Update file deployment failed. Version not marked installed.',
        'data' => [
            'target_version' => $targetVersion,
            'migration' => $migration,
            'package_sql' => $packageSqlMigration,
            'package_files' => $packageFileApply,
        ],
    ]);
}

$metaPatch = [
    'current_version' => $targetVersion,
    'installed_version' => $targetVersion,
    'installed_release_id' => $releaseId !== '' ? substr($releaseId, 0, 80) : null,
    'installed_at' => gmdate('c'),
    'installed_package_scope' => $packageScope,
    'allowed_update_paths' => array_values($allowedUpdatePaths),
];
if ($packageUrl !== '') {
    $metaPatch['installed_package_url'] = $packageUrl;
}

$recordUpdateHistory = static function () use ($pdo, $targetVersion, $releaseId, $packageScope, $user): bool {
    try {
        if (!apiTableExists($pdo, 'update_history')) {
            return false;
        }

        $columns = apiTableColumns($pdo, 'update_history');
        if (!in_array('version', $columns, true)) {
            return false;
        }

        $payload = [
            'version' => $targetVersion,
            'description' => 'ERP update applied (scope: ' . $packageScope . ($releaseId !== '' ? ', release: ' . $releaseId : '') . ')',
            'applied_by' => (string)($user['email'] ?? $user['name'] ?? 'system'),
        ];

        $insertCols = [];
        $insertVals = [];
        $insertParams = [];
        foreach ($payload as $column => $value) {
            if (!in_array($column, $columns, true)) {
                continue;
            }
            $insertCols[] = $column;
            $insertVals[] = ':' . $column;
            $insertParams[':' . $column] = $value;
        }

        if (in_array('update_date', $columns, true)) {
            $insertCols[] = 'update_date';
            $insertVals[] = 'NOW()';
        }
        if (in_array('created_at', $columns, true)) {
            $insertCols[] = 'created_at';
            $insertVals[] = 'NOW()';
        }
        if (in_array('updated_at', $columns, true)) {
            $insertCols[] = 'updated_at';
            $insertVals[] = 'NOW()';
        }

        if ($insertCols === []) {
            return false;
        }

        $sql = 'INSERT INTO update_history (' . implode(', ', $insertCols) . ') VALUES (' . implode(', ', $insertVals) . ')';
        $stmt = $pdo->prepare($sql);
        $stmt->execute($insertParams);
        return true;
    } catch (Throwable $e) {
        error_log('api/v2 update history insert failed: ' . $e->getMessage());
        return false;
    }
};

$persisted = erpSyncPersistMeta($pdo, $settings, $metaPatch);
$historyLogged = $recordUpdateHistory();

$forcePersist = static function () use ($pdo, $targetVersion, $releaseId, $packageScope, $packageUrl): bool {
    $updated = false;

    if (function_exists('apiSystemSettingsSet')) {
        $updated = apiSystemSettingsSet($pdo, 'current_version', $targetVersion) || $updated;
        $updated = apiSystemSettingsSet($pdo, 'installed_version', $targetVersion) || $updated;
        $updated = apiSystemSettingsSet($pdo, 'system_version', $targetVersion) || $updated;
        if ($releaseId !== '') {
            $updated = apiSystemSettingsSet($pdo, 'installed_release_id', substr($releaseId, 0, 80)) || $updated;
        }
        $updated = apiSystemSettingsSet($pdo, 'installed_at', gmdate('c')) || $updated;
        $updated = apiSystemSettingsSet($pdo, 'installed_package_scope', $packageScope) || $updated;
        if ($packageUrl !== '') {
            $updated = apiSystemSettingsSet($pdo, 'installed_package_url', $packageUrl) || $updated;
        }
    }

    try {
        $siteColumns = function_exists('apiTableColumns') ? apiTableColumns($pdo, 'site_settings') : [];
        if ($siteColumns !== []) {
            $setParts = [];
            $params = [];
            $sitePatch = [
                'current_version' => $targetVersion,
                'installed_version' => $targetVersion,
                'installed_release_id' => $releaseId !== '' ? substr($releaseId, 0, 80) : '',
                'installed_at' => gmdate('c'),
                'installed_package_scope' => $packageScope,
                'installed_package_url' => $packageUrl,
            ];
            foreach ($sitePatch as $column => $value) {
                if (!in_array($column, $siteColumns, true)) {
                    continue;
                }
                if ($value === '' && in_array($column, ['installed_release_id', 'installed_package_url'], true)) {
                    continue;
                }
                $setParts[] = apiIdent($column) . ' = :' . $column;
                $params[':' . $column] = (string)$value;
            }
            if ($setParts !== []) {
                $stmt = $pdo->prepare('UPDATE site_settings SET ' . implode(', ', $setParts));
                $stmt->execute($params);
                $updated = true;
            }
        }
    } catch (Throwable $e) {
        error_log('api/v2 erp updates fallback persist failed: ' . $e->getMessage());
    }

    return $updated;
};

$nextSettings = erpSyncReadSettings($pdo);
$nextMeta = erpSyncLocalMeta($nextSettings);
$savedVersion = erpSyncResolveCurrentVersion($nextSettings);
$versionSaved = ($savedVersion === $targetVersion);
$persistedFallback = false;
if (!$versionSaved) {
    $persistedFallback = $forcePersist();
    $nextSettings = erpSyncReadSettings($pdo);
    $nextMeta = erpSyncLocalMeta($nextSettings);
    $savedVersion = erpSyncResolveCurrentVersion($nextSettings);
    $versionSaved = ($savedVersion === $targetVersion);
}
$historyVersionAfterApply = $resolveVersionFromUpdateHistory();
if (!$versionSaved && $historyVersionAfterApply !== '' && $historyVersionAfterApply === $targetVersion) {
    $savedVersion = $historyVersionAfterApply;
    $versionSaved = true;
}
$persistedFinal = $persisted || $persistedFallback || $historyLogged;

jsonResponse(200, [
    'success' => true,
    'message' => $versionSaved
        ? 'Update installed and version saved'
        : ($persistedFinal
            ? 'Update marked installed, but saved version readback mismatch'
            : 'Update applied, but local metadata could not be persisted'),
    'data' => [
        'persisted' => $persistedFinal,
        'persisted_initial' => $persisted,
        'persisted_fallback' => $persistedFallback,
        'update_history_logged' => $historyLogged,
        'target_version' => $targetVersion,
        'saved_version' => $savedVersion,
        'version_saved' => $versionSaved,
        'migration' => $migration,
        'package_sql' => $packageSqlMigration,
        'package_files' => $packageFileApply,
        'sync' => [
            'crm_base_url' => $crmBaseUrl,
            'has_sync_token' => $syncToken !== '',
            'school_id' => $schoolId,
            'current_version' => erpSyncResolveCurrentVersion($nextSettings),
            'local_meta' => $nextMeta,
            'license' => $license,
        ],
    ],
]);
