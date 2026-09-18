<?php
declare(strict_types=1);

require_once __DIR__ . '/_common.php';

ensureMethod('POST');
$pdo = apiDb();
$userId = requireAuthenticatedUserId();
$user = fetchUserContext($pdo, $userId);
requireAnyPermission($user, ['students', 'edit-student', 'report-card']);

function progressionManageIsAdmin(array $userContext): bool
{
    $roles = array_map(
        static fn($value): string => strtolower(trim((string)$value)),
        (array)($userContext['roles'] ?? [])
    );
    $roleTokens = ['admin', 'super admin', 'superadmin', 'administrator', 'owner'];
    foreach ($roles as $role) {
        if (in_array($role, $roleTokens, true)) {
            return true;
        }
    }
    $permissions = array_map(
        static fn($value): string => strtolower(trim((string)$value)),
        (array)($userContext['permissions'] ?? [])
    );
    return in_array('all-access', $permissions, true) || in_array('role-management', $permissions, true);
}

function progressionManageInput(): array
{
    $raw = file_get_contents('php://input');
    $decoded = is_string($raw) && trim($raw) !== '' ? json_decode($raw, true) : null;
    $payload = is_array($decoded) ? $decoded : [];
    foreach ($_POST as $key => $value) {
        if (!array_key_exists($key, $payload)) {
            $payload[$key] = $value;
        }
    }
    return $payload;
}

function progressionManageNormalizeUids($raw): array
{
    if (is_array($raw)) {
        $items = $raw;
    } elseif (is_string($raw) && trim($raw) !== '') {
        $decoded = json_decode($raw, true);
        if (is_array($decoded)) {
            $items = $decoded;
        } else {
            $items = preg_split('/\s*,\s*/', $raw) ?: [];
        }
    } else {
        $items = [];
    }

    $uids = array_map(static fn($value): string => trim((string)$value), $items);
    $uids = array_values(array_filter($uids, static fn(string $value): bool => $value !== ''));
    return array_values(array_unique($uids));
}

function progressionManageResultMap(PDO $pdo, array $studentUids, string $sessionFilter = ''): array
{
    $result = [];
    $safeUids = array_values(array_filter(array_map(static fn($item): string => trim((string)$item), $studentUids), static fn(string $item): bool => $item !== ''));
    if ($safeUids === [] || !apiTableExists($pdo, 'results')) {
        return $result;
    }

    $resultCols = apiTableColumns($pdo, 'results');
    $resultUidCol = apiResolveColumn($resultCols, ['uid', 'student_uid']);
    $resultMarksCol = apiResolveColumn($resultCols, ['marks_obtained', 'marks']);
    $resultMaxMarksCol = apiResolveColumn($resultCols, ['max_marks', 'total_marks']);
    if ($resultUidCol === null || $resultMarksCol === null || $resultMaxMarksCol === null) {
        return $result;
    }

    $resultSessionCol = apiResolveColumn($resultCols, ['session', 'academic_session']);
    $resultTermCol = apiResolveColumn($resultCols, ['term', 'exam_term']);
    $resultSubjectCol = apiResolveColumn($resultCols, ['subject_id', 'subject']);

    $subjectTable = '';
    if (apiTableExists($pdo, 'subject')) {
        $subjectTable = 'subject';
    } elseif (apiTableExists($pdo, 'subjects')) {
        $subjectTable = 'subjects';
    }

    $subjectJoinSql = '';
    $failExpr = 'COALESCE(r.' . apiIdent($resultMarksCol) . ', 0) < CEIL(COALESCE(NULLIF(r.' . apiIdent($resultMaxMarksCol) . ', 0), 0) * 0.33)';
    if ($subjectTable !== '' && $resultSubjectCol !== null) {
        $subjectCols = apiTableColumns($pdo, $subjectTable);
        $subjectIdCol = apiResolveColumn($subjectCols, ['id', 'subject_id']);
        $subjectPassingCol = apiResolveColumn($subjectCols, ['passing_marks', 'pass_marks', 'minimum_marks']);
        if ($subjectIdCol !== null && $subjectPassingCol !== null) {
            $subjectJoinSql = ' LEFT JOIN ' . apiIdent($subjectTable) . ' sub ON sub.' . apiIdent($subjectIdCol) . ' = r.' . apiIdent($resultSubjectCol);
            $failExpr = 'COALESCE(r.' . apiIdent($resultMarksCol) . ', 0) < COALESCE(NULLIF(sub.' . apiIdent($subjectPassingCol) . ', 0), CEIL(COALESCE(NULLIF(r.' . apiIdent($resultMaxMarksCol) . ', 0), 0) * 0.33))';
        }
    }

    $placeholders = implode(',', array_fill(0, count($safeUids), '?'));
    $where = ['r.' . apiIdent($resultUidCol) . ' IN (' . $placeholders . ')'];
    $params = $safeUids;

    if ($resultTermCol !== null) {
        $where[] = "UPPER(COALESCE(r." . apiIdent($resultTermCol) . ", '')) = 'ANNUAL'";
    }
    if ($resultSessionCol !== null && $sessionFilter !== '') {
        $where[] = 'r.' . apiIdent($resultSessionCol) . ' = ?';
        $params[] = $sessionFilter;
    }

    $sql = 'SELECT
                r.' . apiIdent($resultUidCol) . ' AS student_uid,
                COUNT(*) AS total_subjects,
                SUM(CASE WHEN ' . $failExpr . ' THEN 1 ELSE 0 END) AS failed_subjects
            FROM ' . apiIdent('results') . ' r'
            . $subjectJoinSql
            . ' WHERE ' . implode(' AND ', $where)
            . ' GROUP BY r.' . apiIdent($resultUidCol);

    $stmt = $pdo->prepare($sql);
    $stmt->execute($params);
    $rows = $stmt->fetchAll(PDO::FETCH_ASSOC) ?: [];

    foreach ($rows as $row) {
        $uid = trim((string)($row['student_uid'] ?? ''));
        if ($uid === '') {
            continue;
        }
        $totalSubjects = (int)($row['total_subjects'] ?? 0);
        $failedSubjects = (int)($row['failed_subjects'] ?? 0);
        $status = 'Pending';
        if ($totalSubjects > 0) {
            $status = $failedSubjects > 0 ? 'Fail' : 'Pass';
        }
        $result[$uid] = $status;
    }

    return $result;
}

function progressionEnsureLogTable(PDO $pdo): void
{
    $pdo->exec(
        'CREATE TABLE IF NOT EXISTS ' . apiIdent('student_progression_logs') . ' (
            id INT AUTO_INCREMENT PRIMARY KEY,
            student_uid VARCHAR(255) NOT NULL,
            student_id INT NULL,
            from_class VARCHAR(120) NULL,
            to_class_id INT NOT NULL,
            to_class_name VARCHAR(120) NOT NULL,
            from_session VARCHAR(40) NULL,
            to_session VARCHAR(40) NULL,
            annual_result VARCHAR(20) NOT NULL DEFAULT \'Pending\',
            promotion_mode VARCHAR(40) NOT NULL DEFAULT \'auto\',
            promoted_by VARCHAR(120) NULL,
            created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
            KEY idx_progression_uid (student_uid),
            KEY idx_progression_to_class (to_class_id),
            KEY idx_progression_created (created_at)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci'
    );
}

$input = progressionManageInput();
$action = strtolower(trim((string)($input['action'] ?? 'promote')));
if ($action !== 'promote') {
    jsonResponse(400, [
        'success' => false,
        'message' => 'Unsupported action'
    ]);
}

$uids = progressionManageNormalizeUids($input['uids'] ?? []);
$newClassId = (int)($input['new_class_id'] ?? 0);
$newSession = trim((string)($input['new_session'] ?? ''));
$allowFailed = filter_var($input['allow_failed'] ?? false, FILTER_VALIDATE_BOOLEAN);
$sessionFilter = trim((string)($input['session'] ?? ''));

if ($uids === []) {
    jsonResponse(400, [
        'success' => false,
        'message' => 'No students selected'
    ]);
}
if ($newClassId <= 0) {
    jsonResponse(400, [
        'success' => false,
        'message' => 'Select target class first'
    ]);
}
if ($newSession === '' || mb_strlen($newSession) > 20) {
    jsonResponse(400, [
        'success' => false,
        'message' => 'Provide a valid session value'
    ]);
}

$isAdmin = progressionManageIsAdmin($user);
$classCols = apiTableColumns($pdo, 'class');
$classIdCol = apiResolveColumn($classCols, ['id', 'class_id']);
$classNameCol = apiResolveColumn($classCols, ['class_name', 'name']);
if ($classCols === [] || $classIdCol === null || $classNameCol === null) {
    jsonResponse(500, [
        'success' => false,
        'message' => 'Class table is not configured'
    ]);
}

$targetClassStmt = $pdo->prepare(
    'SELECT ' . apiIdent($classIdCol) . ' AS id, ' . apiIdent($classNameCol) . ' AS class_name
     FROM ' . apiIdent('class') . '
     WHERE ' . apiIdent($classIdCol) . ' = :id
     LIMIT 1'
);
$targetClassStmt->execute([':id' => $newClassId]);
$targetClass = $targetClassStmt->fetch(PDO::FETCH_ASSOC) ?: null;
if (!is_array($targetClass)) {
    jsonResponse(404, [
        'success' => false,
        'message' => 'Target class not found'
    ]);
}
$targetClassName = trim((string)($targetClass['class_name'] ?? ''));

$studentCols = apiTableColumns($pdo, 'students');
$studentIdCol = apiResolveColumn($studentCols, ['id', 'student_id']);
$studentUidCol = apiResolveColumn($studentCols, ['uid', 'student_uid']);
$studentClassCol = apiResolveColumn($studentCols, ['class', 'class_id']);
$studentCurrentClassCol = apiResolveColumn($studentCols, ['current_class']);
$studentApplyingClassCol = apiResolveColumn($studentCols, ['applying_class']);
$studentPreviousClassCol = apiResolveColumn($studentCols, ['previous_class']);
$studentPrevYearStatusCol = apiResolveColumn($studentCols, ['previous_year_status']);
$studentSessionCol = apiResolveColumn($studentCols, ['session', 'academic_session']);
$studentUpdatedAtCol = apiResolveColumn($studentCols, ['updated_at']);

if ($studentIdCol === null || $studentUidCol === null || $studentClassCol === null) {
    jsonResponse(500, [
        'success' => false,
        'message' => 'Students table is not configured for progression'
    ]);
}

$placeholders = implode(',', array_fill(0, count($uids), '?'));
$studentSql = 'SELECT '
    . 's.' . apiIdent($studentIdCol) . ' AS student_id, '
    . 's.' . apiIdent($studentUidCol) . ' AS uid, '
    . 's.' . apiIdent($studentClassCol) . ' AS class_value, '
    . ($studentCurrentClassCol !== null
        ? 's.' . apiIdent($studentCurrentClassCol)
        : "''")
    . ' AS current_class, '
    . ($studentSessionCol !== null
        ? 's.' . apiIdent($studentSessionCol)
        : "''")
    . ' AS session
    FROM ' . apiIdent('students') . ' s
    WHERE s.' . apiIdent($studentUidCol) . ' IN (' . $placeholders . ')';

$studentStmt = $pdo->prepare($studentSql);
$studentStmt->execute($uids);
$studentRows = $studentStmt->fetchAll(PDO::FETCH_ASSOC) ?: [];
if ($studentRows === []) {
    jsonResponse(404, [
        'success' => false,
        'message' => 'No matching students found'
    ]);
}

$rowByUid = [];
foreach ($studentRows as $row) {
    $uid = trim((string)($row['uid'] ?? ''));
    if ($uid !== '') {
        $rowByUid[$uid] = $row;
    }
}
$missingUids = array_values(array_diff($uids, array_keys($rowByUid)));
if ($missingUids !== []) {
    jsonResponse(404, [
        'success' => false,
        'message' => 'Students not found: ' . implode(', ', $missingUids)
    ]);
}

$statusMap = progressionManageResultMap($pdo, $uids, $sessionFilter);
$nonPassUids = [];
foreach ($uids as $uid) {
    $status = $statusMap[$uid] ?? 'Pending';
    if ($status !== 'Pass') {
        $nonPassUids[] = $uid;
    }
}

if (!$isAdmin && $nonPassUids !== []) {
    jsonResponse(403, [
        'success' => false,
        'message' => 'Only annual-pass students can be promoted. Failed/Pending students require admin override.',
        'data' => [
            'blocked_uids' => $nonPassUids
        ]
    ]);
}
if ($isAdmin && $nonPassUids !== [] && !$allowFailed) {
    jsonResponse(409, [
        'success' => false,
        'message' => 'Some selected students are Fail/Pending. Enable admin override to continue.',
        'data' => [
            'blocked_uids' => $nonPassUids
        ]
    ]);
}

try {
    progressionEnsureLogTable($pdo);
    $pdo->beginTransaction();

    $setParts = [
        's.' . apiIdent($studentClassCol) . ' = :new_class_value'
    ];
    if ($studentCurrentClassCol !== null) {
        $setParts[] = 's.' . apiIdent($studentCurrentClassCol) . ' = :new_class_name';
    }
    if ($studentApplyingClassCol !== null) {
        $setParts[] = 's.' . apiIdent($studentApplyingClassCol) . ' = :new_class_name';
    }
    if ($studentPreviousClassCol !== null) {
        $setParts[] = 's.' . apiIdent($studentPreviousClassCol) . ' = :prev_class_value';
    }
    if ($studentPrevYearStatusCol !== null) {
        $setParts[] = 's.' . apiIdent($studentPrevYearStatusCol) . ' = :prev_year_status';
    }
    if ($studentSessionCol !== null) {
        $setParts[] = 's.' . apiIdent($studentSessionCol) . ' = :new_session';
    }
    if ($studentUpdatedAtCol !== null) {
        $setParts[] = 's.' . apiIdent($studentUpdatedAtCol) . ' = NOW()';
    }

    $updateSql = 'UPDATE ' . apiIdent('students') . ' s SET '
        . implode(', ', $setParts)
        . ' WHERE s.' . apiIdent($studentIdCol) . ' = :student_id';
    $updateStmt = $pdo->prepare($updateSql);

    $logSql = 'INSERT INTO ' . apiIdent('student_progression_logs') . '
        (student_uid, student_id, from_class, to_class_id, to_class_name, from_session, to_session, annual_result, promotion_mode, promoted_by)
        VALUES (:student_uid, :student_id, :from_class, :to_class_id, :to_class_name, :from_session, :to_session, :annual_result, :promotion_mode, :promoted_by)';
    $logStmt = $pdo->prepare($logSql);

    $overriddenCount = 0;
    foreach ($uids as $uid) {
        $row = $rowByUid[$uid];
        $annual = $statusMap[$uid] ?? 'Pending';
        $mode = ($annual === 'Pass') ? 'auto' : 'admin_override';
        if ($mode === 'admin_override') {
            $overriddenCount += 1;
        }

        $fromClass = trim((string)($row['current_class'] ?? ''));
        if ($fromClass === '') {
            $fromClass = trim((string)($row['class_value'] ?? ''));
        }

        $params = [
            ':student_id' => (int)($row['student_id'] ?? 0),
            ':new_class_value' => (string)$newClassId,
            ':new_class_name' => $targetClassName,
            ':prev_class_value' => $fromClass,
            ':prev_year_status' => $mode === 'auto' ? 'Promoted' : 'Promoted by Admin Override',
            ':new_session' => $newSession
        ];

        foreach ($params as $name => $value) {
            if (!str_contains($updateSql, $name)) {
                unset($params[$name]);
            }
        }

        $updateStmt->execute($params);

        $logStmt->execute([
            ':student_uid' => $uid,
            ':student_id' => (int)($row['student_id'] ?? 0),
            ':from_class' => $fromClass,
            ':to_class_id' => $newClassId,
            ':to_class_name' => $targetClassName,
            ':from_session' => trim((string)($row['session'] ?? '')),
            ':to_session' => $newSession,
            ':annual_result' => $annual,
            ':promotion_mode' => $mode,
            ':promoted_by' => trim((string)($user['name'] ?? $user['uid'] ?? 'system'))
        ]);
    }

    $pdo->commit();

    erpSendAdminNotification(
        $pdo,
        'Student progression completed',
        count($uids) . ' student(s) moved to ' . $targetClassName . ' (' . $newSession . ').',
        $overriddenCount > 0 ? 'warning' : 'good'
    );

    jsonResponse(200, [
        'success' => true,
        'message' => count($uids) . ' student(s) moved successfully.',
        'data' => [
            'promoted_count' => count($uids),
            'overridden_count' => $overriddenCount,
            'target_class_id' => $newClassId,
            'target_class_name' => $targetClassName,
            'target_session' => $newSession
        ]
    ]);
} catch (Throwable $e) {
    if ($pdo->inTransaction()) {
        $pdo->rollBack();
    }
    error_log('api/v2/progression-manage failed: ' . $e->getMessage());
    jsonResponse(500, [
        'success' => false,
        'message' => 'Failed to process progression action'
    ]);
}

