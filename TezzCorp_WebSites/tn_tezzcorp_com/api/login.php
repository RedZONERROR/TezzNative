<?php
// Load database configuration
require_once $_SERVER['DOCUMENT_ROOT'] . '/includes/config.php';

// Set headers for JSON API
header('Content-Type: application/json; charset=UTF-8');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: POST');
header('Access-Control-Allow-Headers: Content-Type');

class LoginController {
    private $pdo;
    private $logger;

    public function __construct() {
        // Initialize database connection
        $host = DB_HOST;
        $db = DB_NAME;
        $user = DB_USER;
        $pass = DB_PASS;

        try {
            $this->pdo = new PDO(
                "mysql:host=$host;dbname=$db;charset=utf8mb4",
                $user,
                $pass,
                [
                    PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
                    PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
                    PDO::ATTR_EMULATE_PREPARES => false,
                ]
            );
        } catch (PDOException $e) {
            $this->logError('Database connection failed: ' . $e->getMessage());
            $this->sendError(500, 'Database connection failed');
        }

        // Initialize logger
        $this->logger = function ($message) {
            $logMessage = date('Y-m-d H:i:s') . ' - ' . $message . PHP_EOL;
            file_put_contents(__DIR__ . '/logs/api.log', $logMessage, FILE_APPEND);
        };
    }

    private function quoteIdent(string $name): string
    {
        return '`' . str_replace('`', '', $name) . '`';
    }

    private function tableColumns(string $table): array
    {
        try {
            $stmt = $this->pdo->query('SHOW COLUMNS FROM ' . $this->quoteIdent($table));
            $rows = $stmt->fetchAll();
            return array_map(static fn(array $row): string => (string)($row['Field'] ?? ''), $rows ?: []);
        } catch (Throwable $e) {
            return [];
        }
    }

    private function firstExisting(array $columns, array $candidates): ?string
    {
        foreach ($candidates as $candidate) {
            if (in_array($candidate, $columns, true)) {
                return $candidate;
            }
        }
        return null;
    }

    private function tableExists(string $table): bool
    {
        $name = trim($table);
        if ($name === '') {
            return false;
        }
        try {
            $stmt = $this->pdo->prepare(
                "SELECT COUNT(1)
                 FROM information_schema.tables
                 WHERE table_schema = DATABASE()
                   AND table_name = :table_name"
            );
            $stmt->execute([':table_name' => $name]);
            return ((int)$stmt->fetchColumn()) > 0;
        } catch (Throwable $e) {
            return false;
        }
    }

    private function normalizePlanCode(string $value): string
    {
        $token = strtolower(trim($value));
        if ($token === '') {
            return 'gold';
        }
        $token = preg_replace('/[^a-z0-9_-]/', '', $token) ?? $token;
        $aliases = [
            'g20' => 'gold',
            'premium' => 'gold',
            'starter' => 'basic',
            'enterprise' => 'platinum',
        ];
        return $aliases[$token] ?? $token;
    }

    private function readCurrentPlanCode(): string
    {
        $planCode = '';
        try {
            if ($planCode === '' && $this->tableExists('erp_license_state')) {
                $cols = $this->tableColumns('erp_license_state');
                if (in_array('plan_code', $cols, true)) {
                    $stmt = $this->pdo->query(
                        "SELECT COALESCE(NULLIF(plan_code, ''), '') AS plan_code
                         FROM erp_license_state
                         ORDER BY id DESC
                         LIMIT 1"
                    );
                    $planCode = trim((string)$stmt->fetchColumn());
                }
            }

            if ($planCode === '' && $this->tableExists('site_settings')) {
                $siteCols = $this->tableColumns('site_settings');
                if (in_array('plan_code', $siteCols, true)) {
                    $stmt = $this->pdo->query('SELECT COALESCE(NULLIF(plan_code, \'\'), \'\') FROM site_settings LIMIT 1');
                    $planCode = trim((string)$stmt->fetchColumn());
                }
                if ($planCode === '' && in_array('feature_flags_json', $siteCols, true)) {
                    $stmt = $this->pdo->query('SELECT COALESCE(NULLIF(feature_flags_json, \'\'), \'\') FROM site_settings LIMIT 1');
                    $raw = trim((string)$stmt->fetchColumn());
                    if ($raw !== '') {
                        $decoded = json_decode($raw, true);
                        if (is_array($decoded)) {
                            $planCode = trim((string)($decoded['plan_code'] ?? $decoded['tier'] ?? ($decoded['_erp_meta']['plan_code'] ?? '')));
                        }
                    }
                }
            }

            if ($planCode === '' && $this->tableExists('system_settings')) {
                $stmt = $this->pdo->prepare(
                    "SELECT setting_value
                     FROM system_settings
                     WHERE setting_key = :setting_key
                     ORDER BY id DESC
                     LIMIT 1"
                );
                $stmt->execute([':setting_key' => 'plan_code']);
                $planCode = trim((string)$stmt->fetchColumn());
            }
        } catch (Throwable $e) {
            $this->logError('Plan code read failed: ' . $e->getMessage());
        }
        return $this->normalizePlanCode($planCode);
    }

    private function workspaceAccessByPlan(string $planCode): array
    {
        $planCode = $this->normalizePlanCode($planCode);
        if ($planCode === 'basic') {
            return ['administration'];
        }
        if ($planCode === 'silver') {
            return ['administration', 'staff'];
        }
        return ['administration', 'staff', 'student'];
    }

    private function portalRoleWorkspace(string $portalRole, bool $isTeacherActor = false): string
    {
        $portalRole = strtolower(trim($portalRole));
        if ($portalRole === 'student') {
            return 'student';
        }
        if ($portalRole === 'staff' || $isTeacherActor) {
            return 'staff';
        }
        return 'administration';
    }

    private function isPortalRoleAllowedByPlan(string $portalRole, string $planCode): bool
    {
        $workspace = $this->portalRoleWorkspace($portalRole, $portalRole === 'staff');
        $allowed = $this->workspaceAccessByPlan($planCode);
        return in_array($workspace, $allowed, true);
    }

    private function ensureStudentPortalAccountsTable(): void
    {
        try {
            $this->pdo->exec(
                "CREATE TABLE IF NOT EXISTS student_portal_accounts (
                    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
                    student_id BIGINT NULL,
                    student_uid VARCHAR(120) NULL,
                    login_key VARCHAR(190) NULL,
                    password_hash VARCHAR(255) NOT NULL,
                    status VARCHAR(24) NOT NULL DEFAULT 'active',
                    last_login_at DATETIME NULL,
                    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
                    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
                    PRIMARY KEY (id),
                    KEY idx_student_id (student_id),
                    KEY idx_student_uid (student_uid),
                    KEY idx_login_key (login_key),
                    KEY idx_status (status)
                ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"
            );
        } catch (Throwable $e) {
            $this->logError('Failed to ensure student_portal_accounts table: ' . $e->getMessage());
        }
    }

    private function normalizeMobile(string $value): string
    {
        return preg_replace('/\D+/', '', trim($value)) ?: '';
    }

    private function normalizeClassToken(string $value): string
    {
        $token = strtolower(trim($value));
        $token = preg_replace('/\s+/', ' ', $token) ?? $token;
        return trim($token);
    }

    private function classLookupMaps(): array
    {
        $idToName = [];
        $nameToId = [];
        $classColumns = $this->tableColumns('class');
        $classIdCol = $this->firstExisting($classColumns, ['id', 'class_id']);
        $classNameCol = $this->firstExisting($classColumns, ['class_name', 'name', 'title']);
        if ($classIdCol === null || $classNameCol === null) {
            return [$idToName, $nameToId];
        }
        try {
            $sql = 'SELECT ' . $this->quoteIdent($classIdCol) . ' AS id, '
                . $this->quoteIdent($classNameCol) . ' AS class_name FROM ' . $this->quoteIdent('class');
            $rows = $this->pdo->query($sql)->fetchAll() ?: [];
            foreach ($rows as $row) {
                $id = (int)($row['id'] ?? 0);
                $name = trim((string)($row['class_name'] ?? ''));
                if ($id > 0 && $name !== '') {
                    $idToName[$id] = $name;
                    $nameToId[$this->normalizeClassToken($name)] = $id;
                }
            }
        } catch (Throwable $e) {
            $this->logError('Class lookup map failed: ' . $e->getMessage());
        }
        return [$idToName, $nameToId];
    }

    private function classValueMatchesFilter(string $classValue, string $wantedClass, array $classMaps): bool
    {
        $classValue = trim($classValue);
        $wantedClass = trim($wantedClass);
        if ($wantedClass === '') {
            return true;
        }

        [$idToName, $nameToId] = $classMaps;
        $wantedToken = $this->normalizeClassToken($wantedClass);
        $wantedId = ctype_digit($wantedClass) ? (int)$wantedClass : (int)($nameToId[$wantedToken] ?? 0);

        $valueToken = $this->normalizeClassToken($classValue);
        $valueId = ctype_digit($classValue) ? (int)$classValue : (int)($nameToId[$valueToken] ?? 0);

        if ($wantedId > 0 && $valueId > 0) {
            return $wantedId === $valueId;
        }
        if ($wantedId > 0 && isset($idToName[$wantedId])) {
            return $this->normalizeClassToken($idToName[$wantedId]) === $valueToken;
        }
        if ($valueId > 0 && isset($idToName[$valueId])) {
            return $wantedToken === $this->normalizeClassToken($idToName[$valueId]);
        }
        return $wantedToken === $valueToken;
    }

    private function fetchStudentAccountsByMobileClass(string $mobile, string $className): array
    {
        $mobileDigits = $this->normalizeMobile($mobile);
        $className = trim($className);
        if ($mobileDigits === '' || $className === '') {
            return [];
        }

        $studentColumns = $this->tableColumns('students');
        $studentIdCol = $this->firstExisting($studentColumns, ['id', 'student_id']);
        $studentUidCol = $this->firstExisting($studentColumns, ['uid', 'student_uid', 'uuid']);
        $studentNameCol = $this->firstExisting($studentColumns, ['full_name', 'name', 'student_name']);
        $studentMobileCol = $this->firstExisting($studentColumns, ['mobile_number', 'mobile', 'phone']);
        $studentClassCol = $this->firstExisting($studentColumns, ['class', 'current_class', 'applying_class']);
        $studentStatusCol = $this->firstExisting($studentColumns, ['status']);
        if ($studentIdCol === null || $studentMobileCol === null || $studentClassCol === null) {
            return [];
        }

        $selectCols = [
            $this->quoteIdent($studentIdCol) . ' AS student_id',
            ($studentUidCol !== null ? $this->quoteIdent($studentUidCol) : $this->quoteIdent($studentIdCol)) . ' AS student_uid',
            ($studentNameCol !== null ? $this->quoteIdent($studentNameCol) : "''") . ' AS student_name',
            $this->quoteIdent($studentMobileCol) . ' AS student_mobile',
            $this->quoteIdent($studentClassCol) . ' AS class_value',
        ];

        $mobileExpr = 'REPLACE(REPLACE(REPLACE(REPLACE(COALESCE('
            . $this->quoteIdent($studentMobileCol)
            . ", ''), ' ', ''), '-', ''), '+', ''), '.', '')";
        $where = [
            '(' . $mobileExpr . ' = :mobile_digits OR LOWER(' . $this->quoteIdent($studentMobileCol) . ') = LOWER(:mobile_raw))'
        ];
        if ($studentStatusCol !== null) {
            $where[] = "COALESCE(NULLIF(" . $this->quoteIdent($studentStatusCol) . ", ''), 'active') IN ('active', 'approved', 'Active', 'Approved', '1')";
        }

        $sql = 'SELECT ' . implode(', ', $selectCols)
            . ' FROM ' . $this->quoteIdent('students')
            . ' WHERE ' . implode(' AND ', $where)
            . ' ORDER BY ' . $this->quoteIdent($studentIdCol) . ' DESC LIMIT 60';
        $stmt = $this->pdo->prepare($sql);
        $stmt->execute([
            ':mobile_digits' => $mobileDigits,
            ':mobile_raw' => trim($mobile),
        ]);
        $rows = $stmt->fetchAll() ?: [];
        if ($rows === []) {
            return [];
        }

        $classMaps = $this->classLookupMaps();
        [$idToName, ] = $classMaps;
        $accounts = [];
        foreach ($rows as $row) {
            $rowMobile = $this->normalizeMobile((string)($row['student_mobile'] ?? ''));
            if ($rowMobile === '' || $rowMobile !== $mobileDigits) {
                continue;
            }
            $classValue = trim((string)($row['class_value'] ?? ''));
            if (!$this->classValueMatchesFilter($classValue, $className, $classMaps)) {
                continue;
            }

            $classId = ctype_digit($classValue) ? (int)$classValue : 0;
            $classLabel = $classValue;
            if ($classId > 0 && isset($idToName[$classId]) && trim((string)$idToName[$classId]) !== '') {
                $classLabel = trim((string)$idToName[$classId]);
            }

            $uid = trim((string)($row['student_uid'] ?? ''));
            if ($uid === '') {
                $uid = 'STU' . (string)((int)($row['student_id'] ?? 0));
            }
            $accounts[] = [
                'student_id' => (int)($row['student_id'] ?? 0),
                'student_uid' => $uid,
                'student_name' => trim((string)($row['student_name'] ?? '')) ?: 'Student',
                'class_name' => $classLabel,
                'class_value' => $classValue,
                'mobile_number' => $rowMobile,
            ];
        }

        $deduped = [];
        $seen = [];
        foreach ($accounts as $account) {
            $key = strtolower((string)($account['student_uid'] ?? ''));
            if ($key === '' || isset($seen[$key])) {
                continue;
            }
            $seen[$key] = true;
            $deduped[] = $account;
        }
        return $deduped;
    }

    private function isAdminRoleToken(string $role): bool
    {
        $token = strtolower(trim($role));
        if ($token === '') {
            return false;
        }
        return preg_match('/\b(admin|administrator|owner|principal|super\s*admin)\b/i', $token) === 1;
    }

    private function isTeacherRoleToken(string $role): bool
    {
        $token = strtolower(trim($role));
        if ($token === '') {
            return false;
        }
        return preg_match('/\b(teacher|faculty|instructor|staff)\b/i', $token) === 1;
    }

    private function normalizePermissionToken(string $value): string
    {
        $token = strtolower(trim($value));
        if ($token === '') {
            return '';
        }
        $token = preg_replace('/^(?:view|menu|module|route|page|permission|perm|feature|erp)[:-]+/i', '', $token) ?? $token;
        $token = preg_replace('/[\s_]+/', '-', $token) ?? $token;
        $token = trim($token, '-');

        $aliasMap = [
            'dashboard' => 'index',
            'home' => 'index',
            'student' => 'students',
            'staffs' => 'staff',
            'teacherdashboard' => 'teacher-dashboard',
            'classes' => 'class',
            'subjects' => 'subject',
            'fees' => 'collection',
            'fee-collection' => 'collection',
            'fee-collect' => 'collect',
            'fee-card' => 'collection',
            'attendance-report' => 'attendance',
            'staff-attendance-report' => 'staff-attendance',
            'settings' => 'site-settings',
            'site-settings' => 'settings',
            'register' => 'staff-form',
            'register-staff' => 'staff-form',
            'registration' => 'staff-form',
            'reportcard' => 'report-card',
            'class-teachers' => 'class-teacher',
            'subject-teachers' => 'subject-teacher',
            'studentportal' => 'student-portal',
            'student-portal-dashboard' => 'student-portal',
        ];
        return $aliasMap[$token] ?? $token;
    }

    private function expandPermissionToken(string $value): array
    {
        $token = $this->normalizePermissionToken($value);
        if ($token === '') {
            return [];
        }
        $expanded = [$token];
        if ($token === 'index') {
            $expanded[] = 'dashboard';
            $expanded[] = 'home';
        }
        if ($token === 'students') {
            $expanded[] = 'student';
            $expanded[] = 'attendance-report';
        }
        if ($token === 'staff') {
            $expanded[] = 'staffs';
            $expanded[] = 'staff-attendance-report';
            $expanded[] = 'teacher-dashboard';
        }
        if ($token === 'class') {
            $expanded[] = 'classes';
        }
        if ($token === 'subject') {
            $expanded[] = 'subjects';
        }
        if ($token === 'collection') {
            $expanded[] = 'fees';
            $expanded[] = 'fee-collection';
            $expanded[] = 'fee-card';
        }
        if ($token === 'collect') {
            $expanded[] = 'fee-collect';
            $expanded[] = 'fee-card';
        }
        if ($token === 'attendance') {
            $expanded[] = 'attendance-report';
        }
        if ($token === 'staff-attendance') {
            $expanded[] = 'staff-attendance-report';
        }
        if ($token === 'student-portal') {
            $expanded[] = 'studentportal';
        }
        return array_values(array_unique(array_filter($expanded, static fn(string $item): bool => $item !== '')));
    }

    private function refreshSessionAclFromDatabase(string $userId, array $fallbackRoles, array $fallbackPermissions): array
    {
        $resolvedRoles = array_values(array_unique(array_filter(array_map(
            static fn($value): string => trim((string)$value),
            $fallbackRoles
        ))));
        $resolvedPermissions = [];
        foreach ($fallbackPermissions as $permission) {
            foreach ($this->expandPermissionToken((string)$permission) as $expanded) {
                $resolvedPermissions[] = $expanded;
            }
        }
        $resolvedPermissions = array_values(array_unique(array_filter(array_map(
            static fn($value): string => trim((string)$value),
            $resolvedPermissions
        ))));

        $userId = trim($userId);
        if ($userId === '') {
            return [
                'roles' => $resolvedRoles,
                'permissions' => $resolvedPermissions,
            ];
        }

        try {
            $roleColumns = $this->tableColumns('roles');
            $userRoleColumns = $this->tableColumns('user_roles');
            $rolesIdCol = $this->firstExisting($roleColumns, ['id', 'role_id']);
            $rolesNameCol = $this->firstExisting($roleColumns, ['role_name', 'name', 'title']);
            $rolesStatusCol = $this->firstExisting($roleColumns, ['status', 'is_active']);
            $userRoleUserIdCol = $this->firstExisting($userRoleColumns, ['user_id', 'uid', 'staff_id']);
            $userRoleRoleIdCol = $this->firstExisting($userRoleColumns, ['role_id']);
            if ($rolesIdCol !== null && $rolesNameCol !== null && $userRoleUserIdCol !== null && $userRoleRoleIdCol !== null) {
                $rolesSql = "SELECT DISTINCT r." . $this->quoteIdent($rolesNameCol) . " AS role_name
                             FROM roles r
                             JOIN user_roles ur ON r." . $this->quoteIdent($rolesIdCol) . " = ur." . $this->quoteIdent($userRoleRoleIdCol) . "
                             WHERE ur." . $this->quoteIdent($userRoleUserIdCol) . " = ?";
                if ($rolesStatusCol !== null) {
                    $rolesSql .= " AND (LOWER(CAST(r." . $this->quoteIdent($rolesStatusCol) . " AS CHAR)) = 'active' OR CAST(r." . $this->quoteIdent($rolesStatusCol) . " AS CHAR) = '1')";
                }
                $rolesStmt = $this->pdo->prepare($rolesSql);
                $rolesStmt->execute([$userId]);
                $dbRoles = array_values(array_filter(array_map(
                    static fn($value): string => trim((string)$value),
                    array_column($rolesStmt->fetchAll(PDO::FETCH_ASSOC) ?: [], 'role_name')
                )));
                if ($dbRoles !== []) {
                    $resolvedRoles = array_values(array_unique($dbRoles));
                }
            }
        } catch (Throwable $e) {
            $this->logError('Session ACL role refresh failed: ' . $e->getMessage());
        }

        try {
            $permColumns = $this->tableColumns('permissions');
            $rolePermColumns = $this->tableColumns('role_permissions');
            $userRoleColumns = $this->tableColumns('user_roles');
            $permIdCol = $this->firstExisting($permColumns, ['id', 'permission_id']);
            $permNameCol = $this->firstExisting($permColumns, ['permission_name', 'key_name', 'name']);
            $rolePermRoleCol = $this->firstExisting($rolePermColumns, ['role_id']);
            $rolePermPermCol = $this->firstExisting($rolePermColumns, ['permission_id']);
            $urUserCol = $this->firstExisting($userRoleColumns, ['user_id', 'uid', 'staff_id']);
            $urRoleCol = $this->firstExisting($userRoleColumns, ['role_id']);

            if ($permIdCol !== null && $permNameCol !== null && $rolePermRoleCol !== null && $rolePermPermCol !== null && $urUserCol !== null && $urRoleCol !== null) {
                $permSql = "SELECT DISTINCT p." . $this->quoteIdent($permNameCol) . " AS permission_name
                            FROM permissions p
                            JOIN role_permissions rp ON p." . $this->quoteIdent($permIdCol) . " = rp." . $this->quoteIdent($rolePermPermCol) . "
                            JOIN user_roles ur ON rp." . $this->quoteIdent($rolePermRoleCol) . " = ur." . $this->quoteIdent($urRoleCol) . "
                            WHERE ur." . $this->quoteIdent($urUserCol) . " = ?";
                $permStmt = $this->pdo->prepare($permSql);
                $permStmt->execute([$userId]);
                $dbPermissions = [];
                foreach (array_column($permStmt->fetchAll(PDO::FETCH_ASSOC) ?: [], 'permission_name') as $permissionName) {
                    foreach ($this->expandPermissionToken((string)$permissionName) as $expanded) {
                        $dbPermissions[] = $expanded;
                    }
                }
                $dbPermissions = array_values(array_unique(array_filter(array_map(
                    static fn($value): string => trim((string)$value),
                    $dbPermissions
                ))));
                if ($dbPermissions !== []) {
                    $resolvedPermissions = $dbPermissions;
                }
            }
        } catch (Throwable $e) {
            $this->logError('Session ACL permission refresh failed: ' . $e->getMessage());
        }

        $portalActor = strtolower(trim((string)($_SESSION['portal_actor'] ?? '')));
        if ($portalActor === 'student') {
            $resolvedPermissions = ['student-portal', 'profile'];
        } elseif ($portalActor === 'teacher' && !in_array('teacher-dashboard', $resolvedPermissions, true)) {
            $resolvedPermissions[] = 'teacher-dashboard';
        }

        if ($resolvedPermissions === []) {
            $resolvedPermissions = ['index', 'profile'];
        } else {
            if (!in_array('profile', $resolvedPermissions, true)) {
                $resolvedPermissions[] = 'profile';
            }
            if (!in_array('index', $resolvedPermissions, true) && !in_array('dashboard', $resolvedPermissions, true)) {
                $resolvedPermissions[] = 'index';
            }
        }
        $resolvedPermissions = array_values(array_unique($resolvedPermissions));

        if ($resolvedRoles === []) {
            $resolvedRoles = ['User'];
        }

        return [
            'roles' => $resolvedRoles,
            'permissions' => $resolvedPermissions,
        ];
    }

    private function authenticateStudentPortalUser(string $username, string $password = ''): ?array
    {
        $username = trim($username);
        if ($username === '') {
            return null;
        }
        $passwordBypass = (trim($password) === '');

        $studentColumns = $this->tableColumns('students');
        $studentIdCol = $this->firstExisting($studentColumns, ['id', 'student_id']);
        $studentUidCol = $this->firstExisting($studentColumns, ['uid', 'student_uid', 'uuid']);
        if ($studentIdCol === null) {
            return null;
        }

        $studentNameCol = $this->firstExisting($studentColumns, ['full_name', 'name', 'student_name']);
        $studentEmailCol = $this->firstExisting($studentColumns, ['email', 'email_address']);
        $studentPhotoCol = $this->firstExisting($studentColumns, ['photo', 'student_photo', 'avatar', 'image']);
        $studentMobileCol = $this->firstExisting($studentColumns, ['mobile_number', 'mobile', 'phone']);
        $studentAdmissionCol = $this->firstExisting($studentColumns, ['admission_number', 'admission_no', 'registration_no']);
        $studentOtpCol = $this->firstExisting($studentColumns, ['otp', 'pin', 'portal_pin']);
        $studentStatusCol = $this->firstExisting($studentColumns, ['status']);

        $selectCols = [
            $this->quoteIdent($studentIdCol) . ' AS student_id',
            ($studentUidCol !== null ? $this->quoteIdent($studentUidCol) : $this->quoteIdent($studentIdCol)) . ' AS student_uid',
            ($studentNameCol !== null ? $this->quoteIdent($studentNameCol) : "''") . ' AS student_name',
            ($studentEmailCol !== null ? $this->quoteIdent($studentEmailCol) : "''") . ' AS student_email',
            ($studentPhotoCol !== null ? $this->quoteIdent($studentPhotoCol) : "''") . ' AS student_photo',
            ($studentMobileCol !== null ? $this->quoteIdent($studentMobileCol) : "''") . ' AS student_mobile',
            ($studentAdmissionCol !== null ? $this->quoteIdent($studentAdmissionCol) : "''") . ' AS admission_ref',
            ($studentOtpCol !== null ? $this->quoteIdent($studentOtpCol) : "''") . ' AS student_otp',
        ];

        $whereParts = [];
        $params = [];
        $identityCols = [$studentUidCol, $studentEmailCol, $studentMobileCol, $studentAdmissionCol];
        foreach ($identityCols as $candidateCol) {
            if ($candidateCol === null) {
                continue;
            }
            $whereParts[] = 'LOWER(' . $this->quoteIdent($candidateCol) . ') = LOWER(?)';
            $params[] = $username;
        }
        if ($whereParts === []) {
            return null;
        }

        $sql = "SELECT " . implode(', ', $selectCols)
            . " FROM " . $this->quoteIdent('students')
            . " WHERE (" . implode(' OR ', $whereParts) . ")";
        if ($studentStatusCol !== null) {
            $sql .= " AND COALESCE(NULLIF(" . $this->quoteIdent($studentStatusCol) . ", ''), 'active') IN ('active', 'approved', 'Active', 'Approved', '1')";
        }
        $sql .= " LIMIT 1";

        $stmt = $this->pdo->prepare($sql);
        $stmt->execute($params);
        $student = $stmt->fetch();
        if (!is_array($student) || $student === []) {
            return null;
        }

        $this->ensureStudentPortalAccountsTable();
        $portalColumns = $this->tableColumns('student_portal_accounts');
        $portalHasTable = $portalColumns !== [];
        $portalIdCol = $this->firstExisting($portalColumns, ['id']);
        $portalStudentIdCol = $this->firstExisting($portalColumns, ['student_id']);
        $portalStudentUidCol = $this->firstExisting($portalColumns, ['student_uid']);
        $portalLoginKeyCol = $this->firstExisting($portalColumns, ['login_key']);
        $portalHashCol = $this->firstExisting($portalColumns, ['password_hash', 'password']);
        $portalStatusCol = $this->firstExisting($portalColumns, ['status']);

        $account = null;
        if ($portalHasTable && $portalHashCol !== null) {
            $portalWhere = [];
            $portalParams = [];
            $studentId = trim((string)($student['student_id'] ?? ''));
            $studentUid = trim((string)($student['student_uid'] ?? ''));
            if ($portalStudentIdCol !== null && $studentId !== '') {
                $portalWhere[] = $this->quoteIdent($portalStudentIdCol) . ' = ?';
                $portalParams[] = $studentId;
            }
            if ($portalStudentUidCol !== null && $studentUid !== '') {
                $portalWhere[] = 'LOWER(' . $this->quoteIdent($portalStudentUidCol) . ') = LOWER(?)';
                $portalParams[] = $studentUid;
            }
            if ($portalLoginKeyCol !== null) {
                $portalWhere[] = 'LOWER(' . $this->quoteIdent($portalLoginKeyCol) . ') = LOWER(?)';
                $portalParams[] = $username;
            }
            if ($portalWhere !== []) {
                $portalSql = 'SELECT * FROM ' . $this->quoteIdent('student_portal_accounts') . ' WHERE (' . implode(' OR ', $portalWhere) . ') LIMIT 1';
                $portalStmt = $this->pdo->prepare($portalSql);
                $portalStmt->execute($portalParams);
                $account = $portalStmt->fetch() ?: null;
            }
        }

        $passwordOk = $passwordBypass;
        $otpMatched = false;
        if (!$passwordBypass) {
            if (is_array($account) && $portalHashCol !== null) {
                $portalStatus = strtolower(trim((string)($account[$portalStatusCol ?? 'status'] ?? 'active')));
                if ($portalStatus !== '' && in_array($portalStatus, ['inactive', 'disabled', 'blocked', 'deleted'], true)) {
                    return null;
                }
                $hash = trim((string)($account[$portalHashCol] ?? ''));
                if ($hash !== '') {
                    $passwordOk = password_verify($password, $hash) || hash_equals($hash, $password);
                }
            }

            if (!$passwordOk && $studentOtpCol !== null) {
                $otp = trim((string)($student['student_otp'] ?? ''));
                if ($otp !== '' && hash_equals($otp, $password)) {
                    $passwordOk = true;
                    $otpMatched = true;
                }
            }
            if (!$passwordOk && !is_array($account)) {
                $mobileSeed = preg_replace('/\D+/', '', (string)($student['student_mobile'] ?? '')) ?: '';
                $passwordSeed = preg_replace('/\D+/', '', $password) ?: '';
                if ($mobileSeed !== '' && strlen($mobileSeed) >= 10 && hash_equals($mobileSeed, $passwordSeed)) {
                    $passwordOk = true;
                }
            }

            if (!$passwordOk) {
                return null;
            }
        }

        if (!$passwordBypass && $portalHasTable && $portalHashCol !== null && ($otpMatched || !is_array($account))) {
            try {
                $studentId = trim((string)($student['student_id'] ?? ''));
                $studentUid = trim((string)($student['student_uid'] ?? ''));
                $passwordHash = password_hash($password, PASSWORD_DEFAULT);

                if (is_array($account) && $portalIdCol !== null) {
                    $updateParts = [];
                    $updateParams = [];
                    if ($portalHashCol !== null) {
                        $updateParts[] = $this->quoteIdent($portalHashCol) . ' = ?';
                        $updateParams[] = $passwordHash;
                    }
                    if ($portalLoginKeyCol !== null) {
                        $updateParts[] = $this->quoteIdent($portalLoginKeyCol) . ' = ?';
                        $updateParams[] = $username;
                    }
                    if ($portalStatusCol !== null) {
                        $updateParts[] = $this->quoteIdent($portalStatusCol) . " = 'active'";
                    }
                    if ($this->firstExisting($portalColumns, ['last_login_at']) !== null) {
                        $updateParts[] = '`last_login_at` = NOW()';
                    }
                    if ($this->firstExisting($portalColumns, ['updated_at']) !== null) {
                        $updateParts[] = '`updated_at` = NOW()';
                    }
                    if ($updateParts !== []) {
                        $updateSql = 'UPDATE ' . $this->quoteIdent('student_portal_accounts')
                            . ' SET ' . implode(', ', $updateParts)
                            . ' WHERE ' . $this->quoteIdent($portalIdCol) . ' = ? LIMIT 1';
                        $updateParams[] = (string)($account[$portalIdCol] ?? '');
                        $upd = $this->pdo->prepare($updateSql);
                        $upd->execute($updateParams);
                    }
                } else {
                    $insertCols = [];
                    $insertVals = [];
                    $insertParams = [];
                    if ($portalStudentIdCol !== null) {
                        $insertCols[] = $this->quoteIdent($portalStudentIdCol);
                        $insertVals[] = '?';
                        $insertParams[] = $studentId !== '' ? $studentId : null;
                    }
                    if ($portalStudentUidCol !== null) {
                        $insertCols[] = $this->quoteIdent($portalStudentUidCol);
                        $insertVals[] = '?';
                        $insertParams[] = $studentUid !== '' ? $studentUid : null;
                    }
                    if ($portalLoginKeyCol !== null) {
                        $insertCols[] = $this->quoteIdent($portalLoginKeyCol);
                        $insertVals[] = '?';
                        $insertParams[] = $username;
                    }
                    $insertCols[] = $this->quoteIdent($portalHashCol);
                    $insertVals[] = '?';
                    $insertParams[] = $passwordHash;
                    if ($portalStatusCol !== null) {
                        $insertCols[] = $this->quoteIdent($portalStatusCol);
                        $insertVals[] = '?';
                        $insertParams[] = 'active';
                    }
                    if ($this->firstExisting($portalColumns, ['last_login_at']) !== null) {
                        $insertCols[] = '`last_login_at`';
                        $insertVals[] = 'NOW()';
                    }
                    if ($insertCols !== []) {
                        $insertSql = 'INSERT INTO ' . $this->quoteIdent('student_portal_accounts')
                            . ' (' . implode(', ', $insertCols) . ') VALUES (' . implode(', ', $insertVals) . ')';
                        $ins = $this->pdo->prepare($insertSql);
                        $ins->execute($insertParams);
                    }
                }
            } catch (Throwable $e) {
                $this->logError('Student portal account upsert failed: ' . $e->getMessage());
            }
        } elseif (!$passwordBypass && $portalHasTable && is_array($account)) {
            try {
                $portalId = trim((string)($account[$portalIdCol ?? 'id'] ?? ''));
                if ($portalId !== '' && $portalIdCol !== null && in_array('last_login_at', $portalColumns, true)) {
                    $touch = $this->pdo->prepare(
                        'UPDATE ' . $this->quoteIdent('student_portal_accounts')
                        . ' SET `last_login_at` = NOW() WHERE ' . $this->quoteIdent($portalIdCol) . ' = ? LIMIT 1'
                    );
                    $touch->execute([$portalId]);
                }
            } catch (Throwable $e) {
                $this->logError('Student portal last_login update failed: ' . $e->getMessage());
            }
        }

        $studentUid = trim((string)($student['student_uid'] ?? ''));
        $studentId = trim((string)($student['student_id'] ?? ''));
        $studentEmail = trim((string)($student['student_email'] ?? ''));
        $studentName = trim((string)($student['student_name'] ?? ''));
        $studentPhoto = trim((string)($student['student_photo'] ?? ''));
        $studentMobile = trim((string)($student['student_mobile'] ?? ''));

        return [
            'id' => $studentUid !== '' ? $studentUid : $studentId,
            'uid' => $studentUid !== '' ? $studentUid : $studentId,
            'email' => $studentEmail,
            'name' => $studentName !== '' ? $studentName : ($studentMobile !== '' ? $studentMobile : 'Student'),
            'photo' => $studentPhoto,
            'roles' => ['Student'],
            'permissions' => ['student-portal', 'profile'],
            'redirect' => '/tezzerp/student/',
            'portal_actor' => 'student',
            'student_uid' => $studentUid !== '' ? $studentUid : $studentId,
            'student_id' => $studentId,
        ];
    }

    private function completeLogin(array $identity, string $fallbackUsername): void
    {
        $id = trim((string)($identity['id'] ?? ''));
        if ($id === '') {
            $this->sendError(500, 'Login session could not be initialized');
        }
        $uid = trim((string)($identity['uid'] ?? ''));
        if ($uid === '') {
            $uid = $id;
        }
        $email = trim((string)($identity['email'] ?? ''));
        $name = trim((string)($identity['name'] ?? ''));
        $photo = trim((string)($identity['photo'] ?? ''));
        $roles = array_values(array_filter(array_map(static fn($value): string => trim((string)$value), (array)($identity['roles'] ?? []))));
        $permissions = array_values(array_filter(array_map(static fn($value): string => trim((string)$value), (array)($identity['permissions'] ?? []))));
        if ($roles === []) {
            $roles = ['User'];
        }
        if ($permissions === []) {
            $permissions = ['index', 'profile'];
        }

        if (session_status() === PHP_SESSION_NONE) {
            session_start();
        }
        if (session_status() === PHP_SESSION_ACTIVE) {
            session_regenerate_id(true);
            // Clear stale identity context before writing a new actor session.
            unset(
                $_SESSION['id'],
                $_SESSION['user_id'],
                $_SESSION['uid'],
                $_SESSION['email'],
                $_SESSION['name'],
                $_SESSION['photo'],
                $_SESSION['roles'],
                $_SESSION['permissions'],
                $_SESSION['portal_actor'],
                $_SESSION['student_uid'],
                $_SESSION['student_id'],
                $_SESSION['staff_id']
            );
        }
        $_SESSION['id'] = $id;
        $_SESSION['user_id'] = $id;
        $_SESSION['uid'] = $uid;
        $_SESSION['email'] = $email !== '' ? $email : $fallbackUsername;
        $_SESSION['name'] = $name !== '' ? $name : ($email !== '' ? $email : $fallbackUsername);
        $_SESSION['photo'] = $photo;
        $_SESSION['roles'] = $roles;
        $_SESSION['permissions'] = $permissions;

        $portalActor = trim((string)($identity['portal_actor'] ?? ''));
        if ($portalActor !== '') {
            $_SESSION['portal_actor'] = $portalActor;
        }
        $studentUid = trim((string)($identity['student_uid'] ?? ''));
        if ($studentUid !== '') {
            $_SESSION['student_uid'] = $studentUid;
        }
        $studentId = trim((string)($identity['student_id'] ?? ''));
        if ($studentId !== '') {
            $_SESSION['student_id'] = $studentId;
        }
        $staffId = trim((string)($identity['staff_id'] ?? ''));
        if ($staffId !== '') {
            $_SESSION['staff_id'] = $staffId;
        }
        if ($portalActor === '') {
            unset($_SESSION['portal_actor'], $_SESSION['student_uid'], $_SESSION['student_id']);
        }

        try {
            require_once __DIR__ . '/v2/_erp_sync.php';
            if (function_exists('erpSyncPullLicenseFromCrm')) {
                erpSyncPullLicenseFromCrm($this->pdo, []);
            }
            require_once __DIR__ . '/tezzerp/_common.php';
            if (function_exists('apiSyncPlanLinksFromFeatureFlags')) {
                $license = function_exists('apiGetLicenseState') ? apiGetLicenseState($this->pdo) : [];
                $flags = function_exists('apiReadFeatureFlagsJson') ? apiReadFeatureFlagsJson($this->pdo) : [];
                apiSyncPlanLinksFromFeatureFlags(
                    $this->pdo,
                    is_array($flags) ? $flags : [],
                    (string)($license['plan_code'] ?? '')
                );
            }
            if (function_exists('apiSyncPlanPermissionsNow')) {
                apiSyncPlanPermissionsNow($this->pdo);
            }
            if (function_exists('apiEnsureBranchSchema')) {
                apiEnsureBranchSchema($this->pdo);
            }
            $refreshedAcl = $this->refreshSessionAclFromDatabase(
                $id,
                is_array($_SESSION['roles'] ?? null) ? $_SESSION['roles'] : $roles,
                is_array($_SESSION['permissions'] ?? null) ? $_SESSION['permissions'] : $permissions
            );
            $_SESSION['roles'] = is_array($refreshedAcl['roles'] ?? null)
                ? array_values($refreshedAcl['roles'])
                : $_SESSION['roles'];
            $_SESSION['permissions'] = is_array($refreshedAcl['permissions'] ?? null)
                ? array_values($refreshedAcl['permissions'])
                : $_SESSION['permissions'];
        } catch (Throwable $syncError) {
            $this->logError('Runtime license sync on login failed: ' . $syncError->getMessage());
        }

        try {
            $secret = defined('AUTH_KEY') ? (string)AUTH_KEY : (defined('SECURE_AUTH_KEY') ? (string)SECURE_AUTH_KEY : '');
            if ($secret !== '') {
                $payload = base64_encode(json_encode([
                    'id' => $id,
                    'iat' => time(),
                ], JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES));
                $sig = hash_hmac('sha256', $payload, $secret);
                $cookieValue = $payload . '.' . $sig;

                $isHttps = (
                    (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off')
                    || ((int)($_SERVER['SERVER_PORT'] ?? 0) === 443)
                    || (strtolower((string)($_SERVER['HTTP_X_FORWARDED_PROTO'] ?? '')) === 'https')
                );

                setcookie('erp_auth', $cookieValue, [
                    'expires' => time() + 3600,
                    'path' => '/',
                    'secure' => $isHttps,
                    'httponly' => true,
                    'samesite' => 'Lax',
                ]);
            }
        } catch (Throwable $e) {
            $this->logError('erp_auth cookie set failed: ' . $e->getMessage());
        }

        $syncedPlanCode = $this->readCurrentPlanCode();
        $portalActor = strtolower(trim((string)($_SESSION['portal_actor'] ?? '')));
        if (!in_array($portalActor, ['student', 'teacher', 'admin'], true)) {
            $portalActor = 'admin';
            $sessionRoles = is_array($_SESSION['roles'] ?? null) ? $_SESSION['roles'] : [];
            foreach ($sessionRoles as $role) {
                $roleName = trim((string)$role);
                if ($roleName === '') {
                    continue;
                }
                if ($this->isTeacherRoleToken($roleName) && !$this->isAdminRoleToken($roleName)) {
                    $portalActor = 'teacher';
                    break;
                }
            }
            if ($portalActor === 'admin' && isset($_SESSION['student_uid'])) {
                $portalActor = 'student';
            }
        }
        $resolvedPortalRole = $portalActor === 'teacher' ? 'staff' : ($portalActor === 'student' ? 'student' : 'admin');
        if (!$this->isPortalRoleAllowedByPlan($resolvedPortalRole, $syncedPlanCode)) {
            unset(
                $_SESSION['id'],
                $_SESSION['user_id'],
                $_SESSION['uid'],
                $_SESSION['email'],
                $_SESSION['name'],
                $_SESSION['photo'],
                $_SESSION['roles'],
                $_SESSION['permissions'],
                $_SESSION['portal_actor'],
                $_SESSION['student_uid'],
                $_SESSION['student_id'],
                $_SESSION['staff_id']
            );
            session_write_close();
            if ($resolvedPortalRole === 'staff') {
                $this->sendError(403, 'Staff workspace is not available in your current plan');
            }
            if ($resolvedPortalRole === 'student') {
                $this->sendError(403, 'Student workspace is not available in your current plan');
            }
            $this->sendError(403, 'Administration workspace is not available in your current plan');
        }

        $redirect = '/tezzerp/administration/';
        if ($resolvedPortalRole === 'staff') {
            $redirect = '/tezzerp/staff/';
        } elseif ($resolvedPortalRole === 'student') {
            $redirect = '/tezzerp/student/';
        }

        $this->sendSuccess(200, [
            'message' => 'Login successful',
            'email' => $_SESSION['email'],
            'name' => $_SESSION['name'],
            'photo' => $_SESSION['photo'],
            'roles' => $_SESSION['roles'],
            'permissions' => $_SESSION['permissions'],
            'plan_code' => $syncedPlanCode,
            'redirect' => $redirect
        ]);
    }

    // Validate input data
    private function validateInput(array $data, bool $requirePassword = true): array {
        $errors = [];
        $validData = [];

        $fields = [
            'username' => ['type' => 'string', 'max' => 255, 'required' => false],
            'email' => ['type' => 'string', 'max' => 255, 'required' => false],
            'password' => ['type' => 'string', 'max' => 255, 'required' => $requirePassword],
        ];

        foreach ($fields as $field => $rules) {
            if (isset($data[$field])) {
                $value = $data[$field];

                if ($rules['type'] === 'string' && !is_string($value)) {
                    $errors[] = "$field must be a string";
                } elseif (isset($rules['max']) && strlen($value) > $rules['max']) {
                    $errors[] = "$field exceeds maximum length of {$rules['max']} characters";
                }

                $validData[$field] = $value;
            } elseif ($rules['required']) {
                $errors[] = "$field is required";
            }
        }

        $username = trim((string) ($validData['username'] ?? ''));
        $email = trim((string) ($validData['email'] ?? ''));
        if ($username === '' && $email === '') {
            $errors[] = 'username or email is required';
        }

        if ($errors) {
            $this->sendError(400, 'Validation failed: ' . implode('; ', $errors));
        }

        if (!$requirePassword && !isset($validData['password'])) {
            $validData['password'] = '';
        }

        return $validData;
    }

    // Send JSON payload response
    private function sendPayload(int $code, array $payload): void {
        http_response_code($code);
        echo json_encode($payload);
        exit;
    }

    // Send JSON error response
    private function sendError(int $code, string $message): void {
        $this->sendPayload($code, ['success' => false, 'message' => $message]);
    }

    // Send JSON success response
    private function sendSuccess(int $code, array $data): void {
        http_response_code($code);
        echo json_encode(['success' => true, ...$data]);
        exit;
    }

    // Log error message
    private function logError(string $message): void {
        ($this->logger)('ERROR: ' . $message);
    }

    // Handle POST request for login
    public function post(array $data) {
        $action = strtolower(trim((string)($data['action'] ?? 'login')));
        $portalRole = strtolower(trim((string)($data['portal_role'] ?? 'admin')));
        if (!in_array($portalRole, ['admin', 'staff', 'student'], true)) {
            $portalRole = 'admin';
        }
        $currentPlanCode = $this->readCurrentPlanCode();

        if ($action === 'student_accounts') {
            if (!$this->isPortalRoleAllowedByPlan('student', $currentPlanCode)) {
                $this->sendError(403, 'Student workspace is not available in your current plan');
            }
            $mobile = trim((string)($data['mobile_number'] ?? $data['username'] ?? ''));
            $className = trim((string)($data['class_name'] ?? ''));
            if ($mobile === '' || $className === '') {
                $this->sendError(400, 'Mobile number and class are required');
            }
            $students = $this->fetchStudentAccountsByMobileClass($mobile, $className);
            if ($students === []) {
                $this->sendError(404, 'No active student account found for this mobile and class');
            }
            $this->sendSuccess(200, [
                'message' => 'Student accounts found',
                'students' => $students,
                'count' => count($students)
            ]);
        }

        if (!$this->isPortalRoleAllowedByPlan($portalRole, $currentPlanCode)) {
            if ($portalRole === 'staff') {
                $this->sendError(403, 'Staff workspace is not available in your current plan');
            }
            if ($portalRole === 'student') {
                $this->sendError(403, 'Student workspace is not available in your current plan');
            }
            $this->sendError(403, 'Administration workspace is not available in your current plan');
        }

        $validData = $this->validateInput($data, $portalRole !== 'student');
        $username = trim((string)($validData['username'] ?? $validData['email'] ?? ''));
        $password = (string)($validData['password'] ?? '');

        try {
            if ($portalRole === 'student') {
                $mobile = trim((string)($data['mobile_number'] ?? $username));
                $className = trim((string)($data['class_name'] ?? ''));
                if ($mobile === '' || $className === '') {
                    $this->sendError(400, 'Use mobile number and class for student login');
                }

                $studentAccounts = $this->fetchStudentAccountsByMobileClass($mobile, $className);
                if ($studentAccounts === []) {
                    $this->sendError(404, 'No active student account found for this mobile and class');
                }

                $selectedUid = trim((string)($data['student_uid'] ?? ''));
                if ($selectedUid === '' && count($studentAccounts) > 1) {
                    $this->sendPayload(409, [
                        'success' => false,
                        'code' => 'ACCOUNT_SELECTION_REQUIRED',
                        'message' => 'Multiple student accounts found for this mobile number. Select one account to continue.',
                        'students' => $studentAccounts
                    ]);
                }
                if ($selectedUid === '' && count($studentAccounts) === 1) {
                    $selectedUid = trim((string)($studentAccounts[0]['student_uid'] ?? ''));
                }

                $selected = null;
                foreach ($studentAccounts as $studentAccount) {
                    if (strcasecmp(trim((string)($studentAccount['student_uid'] ?? '')), $selectedUid) === 0) {
                        $selected = $studentAccount;
                        break;
                    }
                }
                if (!is_array($selected)) {
                    $this->sendError(400, 'Selected student account is invalid for this mobile and class');
                }

                $studentIdentity = $this->authenticateStudentPortalUser($selectedUid, '');
                if (!is_array($studentIdentity)) {
                    $this->sendError(401, 'Invalid credentials');
                }
                $studentIdentity['student_uid'] = trim((string)($selected['student_uid'] ?? ($studentIdentity['student_uid'] ?? '')));
                $studentIdentity['student_id'] = trim((string)($selected['student_id'] ?? ($studentIdentity['student_id'] ?? '')));
                $this->completeLogin($studentIdentity, $mobile);
                return;
            }

            $tableCandidates = $portalRole === 'staff'
                ? ['staff', 'users']
                : ['users', 'staff'];

            $user = null;
            $userTable = '';
            $userColumns = [];
            $userIdCol = null;
            $userPassCol = null;
            $userEmailCol = null;
            $userLoginCol = null;
            $userUidCol = null;
            $userNameCol = null;
            $userPhotoCol = null;
            $userRoleCol = null;

            foreach ($tableCandidates as $candidateTable) {
                $candidateColumns = $this->tableColumns($candidateTable);
                if ($candidateTable === 'staff') {
                    $candidateIdCol = $this->firstExisting($candidateColumns, ['id', 'staff_id', 'user_id']);
                    $candidateRoleCol = $this->firstExisting($candidateColumns, ['role', 'job_title', 'designation']);
                    $candidateLoginCol = $this->firstExisting($candidateColumns, ['mobile', 'phone', 'name', 'email', 'username', 'user_name']);
                } else {
                    $candidateIdCol = $this->firstExisting($candidateColumns, ['id', 'user_id']);
                    $candidateRoleCol = $this->firstExisting($candidateColumns, ['role', 'job_title']);
                    $candidateLoginCol = $this->firstExisting($candidateColumns, ['username', 'user_name', 'login', 'mobile', 'phone']);
                }
                $candidatePassCol = $this->firstExisting($candidateColumns, ['password', 'password_hash', 'pass_hash', 'passwd']);
                $candidateEmailCol = $this->firstExisting($candidateColumns, ['email']);
                $candidateUidCol = $this->firstExisting($candidateColumns, ['uid', 'user_uid', 'uuid']);
                $candidateNameCol = $this->firstExisting($candidateColumns, ['name', 'full_name', 'display_name']);
                $candidatePhotoCol = $this->firstExisting($candidateColumns, ['photo', 'avatar', 'profile_image', 'image', 'profile_photo', 'profile_pic', 'photo_url', 'avatar_url', 'picture', 'user_photo']);
                if ($candidateIdCol === null || $candidatePassCol === null) {
                    continue;
                }

                $whereParts = [];
                $params = [];
                if ($candidateEmailCol !== null) {
                    $whereParts[] = 'LOWER(' . $this->quoteIdent($candidateEmailCol) . ') = LOWER(?)';
                    $params[] = $username;
                }
                if ($candidateLoginCol !== null && $candidateLoginCol !== $candidateEmailCol) {
                    $whereParts[] = 'LOWER(' . $this->quoteIdent($candidateLoginCol) . ') = LOWER(?)';
                    $params[] = $username;
                }
                if ($whereParts === []) {
                    continue;
                }

                $selectCols = [
                    $this->quoteIdent($candidateIdCol) . ' AS id',
                    $this->quoteIdent($candidatePassCol) . ' AS password',
                    ($candidateUidCol !== null ? $this->quoteIdent($candidateUidCol) : $this->quoteIdent($candidateIdCol)) . ' AS uid',
                    ($candidateEmailCol !== null ? $this->quoteIdent($candidateEmailCol) : "''") . ' AS email',
                    ($candidateNameCol !== null ? $this->quoteIdent($candidateNameCol) : "''") . ' AS name',
                    ($candidatePhotoCol !== null ? $this->quoteIdent($candidatePhotoCol) : "''") . ' AS photo',
                    ($candidateRoleCol !== null ? $this->quoteIdent($candidateRoleCol) : "''") . ' AS role',
                    ($candidateTable === 'staff' ? $this->quoteIdent($candidateIdCol) : 'NULL') . ' AS staff_id'
                ];
                $candidateSql = 'SELECT ' . implode(', ', $selectCols)
                    . ' FROM ' . $this->quoteIdent($candidateTable)
                    . ' WHERE ' . implode(' OR ', $whereParts)
                    . ' LIMIT 1';
                $stmt = $this->pdo->prepare($candidateSql);
                $stmt->execute($params);
                $candidateUser = $stmt->fetch();
                if (!$candidateUser) {
                    continue;
                }

                $user = $candidateUser;
                $userTable = $candidateTable;
                $userColumns = $candidateColumns;
                $userIdCol = $candidateIdCol;
                $userPassCol = $candidatePassCol;
                $userEmailCol = $candidateEmailCol;
                $userLoginCol = $candidateLoginCol;
                $userUidCol = $candidateUidCol;
                $userNameCol = $candidateNameCol;
                $userPhotoCol = $candidatePhotoCol;
                $userRoleCol = $candidateRoleCol;
                break;
            }

            if (!$user) {
                $this->logError("User not found for portal role {$portalRole}: $username");
                $this->sendError(401, 'Invalid credentials');
            }

            $storedPassword = (string)($user['password'] ?? '');
            $passwordOk = $storedPassword !== ''
                && (password_verify($password, $storedPassword) || hash_equals($storedPassword, $password));
            if (!$passwordOk) {
                $this->logError("Invalid password for user: $username");
                $this->sendError(401, 'Invalid credentials');
            }

            $rawRoleToken = trim((string)($user['role'] ?? ''));
            if ($portalRole === 'admin' && !$this->isAdminRoleToken($rawRoleToken) && $userTable === 'staff') {
                $this->sendError(403, 'Use staff login for this account');
            }
            if ($portalRole === 'staff' && $this->isAdminRoleToken($rawRoleToken) && $userTable === 'users') {
                $this->sendError(403, 'Use administration login for this account');
            }

            $roles = [];
            try {
                $roleColumns = $this->tableColumns('roles');
                $userRoleColumns = $this->tableColumns('user_roles');
                $rolesIdCol = $this->firstExisting($roleColumns, ['id', 'role_id']);
                $rolesNameCol = $this->firstExisting($roleColumns, ['role_name', 'name', 'title']);
                $rolesStatusCol = $this->firstExisting($roleColumns, ['status', 'is_active']);
                $userRoleUserIdCol = $this->firstExisting($userRoleColumns, ['user_id', 'uid']);
                $userRoleRoleIdCol = $this->firstExisting($userRoleColumns, ['role_id']);

                if ($rolesIdCol !== null && $rolesNameCol !== null && $userRoleUserIdCol !== null && $userRoleRoleIdCol !== null) {
                    $rolesSql = "SELECT r." . $this->quoteIdent($rolesIdCol) . " AS id, r." . $this->quoteIdent($rolesNameCol) . " AS role_name
                                 FROM roles r
                                 JOIN user_roles ur ON r." . $this->quoteIdent($rolesIdCol) . " = ur." . $this->quoteIdent($userRoleRoleIdCol) . "
                                 WHERE ur." . $this->quoteIdent($userRoleUserIdCol) . " = ?";
                    if ($rolesStatusCol !== null) {
                        $rolesSql .= " AND (r." . $this->quoteIdent($rolesStatusCol) . " = 'active' OR r." . $this->quoteIdent($rolesStatusCol) . " = 1)";
                    }
                    $rolesStmt = $this->pdo->prepare($rolesSql);
                    $rolesStmt->execute([(string)($user['id'] ?? '')]);
                    $roles = $rolesStmt->fetchAll() ?: [];
                }
            } catch (Throwable $e) {
                $this->logError('Role lookup fallback used: ' . $e->getMessage());
            }

            if (empty($roles)) {
                $fallbackRole = trim((string)($user['role'] ?? ''));
                if ($fallbackRole === '') {
                    $fallbackRole = $portalRole === 'staff' ? 'Teacher' : 'User';
                }
                $roles = [
                    ['id' => '0', 'role_name' => $fallbackRole]
                ];
            }

            $permissions = [];
            try {
                $permColumns = $this->tableColumns('permissions');
                $rolePermColumns = $this->tableColumns('role_permissions');
                $userRoleColumns = $this->tableColumns('user_roles');
                $permIdCol = $this->firstExisting($permColumns, ['id', 'permission_id']);
                $permNameCol = $this->firstExisting($permColumns, ['permission_name', 'key_name', 'name']);
                $rolePermRoleCol = $this->firstExisting($rolePermColumns, ['role_id']);
                $rolePermPermCol = $this->firstExisting($rolePermColumns, ['permission_id']);
                $urUserCol = $this->firstExisting($userRoleColumns, ['user_id', 'uid']);
                $urRoleCol = $this->firstExisting($userRoleColumns, ['role_id']);

                if ($permIdCol !== null && $permNameCol !== null && $rolePermRoleCol !== null && $rolePermPermCol !== null && $urUserCol !== null && $urRoleCol !== null) {
                    $permSql = "SELECT DISTINCT p." . $this->quoteIdent($permNameCol) . " AS permission_name
                                FROM permissions p
                                JOIN role_permissions rp ON p." . $this->quoteIdent($permIdCol) . " = rp." . $this->quoteIdent($rolePermPermCol) . "
                                JOIN user_roles ur ON rp." . $this->quoteIdent($rolePermRoleCol) . " = ur." . $this->quoteIdent($urRoleCol) . "
                                WHERE ur." . $this->quoteIdent($urUserCol) . " = ?";
                    $permStmt = $this->pdo->prepare($permSql);
                    $permStmt->execute([(string)($user['id'] ?? '')]);
                    $permissions = array_values(array_filter(array_map(
                        static fn($value): string => trim((string)$value),
                        array_column($permStmt->fetchAll() ?: [], 'permission_name')
                    )));
                }
            } catch (Throwable $e) {
                $this->logError('Permissions lookup fallback used: ' . $e->getMessage());
            }

            if (empty($permissions)) {
                $permissions = ['index', 'profile'];
            }

            $roleNames = array_values(array_filter(array_map(
                static fn($item): string => trim((string)($item['role_name'] ?? '')),
                $roles
            )));
            $identity = [
                'id' => (string)($user['id'] ?? ''),
                'uid' => (string)($user['uid'] ?? ($user['id'] ?? '')),
                'email' => (string)($user['email'] ?? ''),
                'name' => (string)($user['name'] ?? ''),
                'photo' => (string)($user['photo'] ?? ''),
                'roles' => $roleNames,
                'permissions' => $permissions,
            ];
            if ($userTable === 'staff') {
                $identity['staff_id'] = (string)($user['staff_id'] ?? $user['id'] ?? '');
            }

            $isTeacherActor = false;
            foreach ($roleNames as $roleName) {
                if ($this->isTeacherRoleToken($roleName) && !$this->isAdminRoleToken($roleName)) {
                    $isTeacherActor = true;
                    break;
                }
            }
            if (
                !$isTeacherActor
                && $userTable === 'staff'
                && !$this->isAdminRoleToken($rawRoleToken)
                && $portalRole === 'staff'
            ) {
                $isTeacherActor = true;
            }

            if ($isTeacherActor) {
                $identity['portal_actor'] = 'teacher';
                $identity['redirect'] = '/tezzerp/staff/';
                if (!in_array('teacher-dashboard', $identity['permissions'], true)) {
                    $identity['permissions'][] = 'teacher-dashboard';
                }
            }

            $this->completeLogin($identity, $username);
            return;
        } catch (PDOException $e) {
            $this->logError('Login failed: ' . $e->getMessage());
            $this->sendError(500, 'Login failed due to database error');
        }
    }

    // Route requests
    public function handleRequest() {
        $method = $_SERVER['REQUEST_METHOD'];

        if ($method === 'POST') {
            $data = null;
            $rawBody = file_get_contents('php://input');

            if ($rawBody !== '') {
                $decoded = json_decode($rawBody, true);
                if (json_last_error() === JSON_ERROR_NONE) {
                    if (is_array($decoded)) {
                        $data = $decoded;
                    } elseif (is_string($decoded)) {
                        $decodedAgain = json_decode($decoded, true);
                        if (json_last_error() === JSON_ERROR_NONE && is_array($decodedAgain)) {
                            $data = $decodedAgain;
                        }
                    }
                }
            }

            if (!is_array($data) || empty($data)) {
                $data = $_POST;
            }

            if (!is_array($data) || empty($data)) {
                $this->sendError(400, 'Invalid payload');
            }

            $this->post($data);
            return;
        }

        $this->sendError(405, 'Method not allowed');
    }
}

// Ensure logs directory exists
if (!file_exists(__DIR__ . '/logs')) {
    mkdir(__DIR__ . '/logs', 0755, true);
}

// Instantiate and handle request
$controller = new LoginController();
$controller->handleRequest();
?>
