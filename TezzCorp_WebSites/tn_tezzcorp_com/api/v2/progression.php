<?php
declare(strict_types=1);

require_once __DIR__ . '/_common.php';

ensureMethod('GET');
$pdo = apiDb();
$userId = requireAuthenticatedUserId();
$user = fetchUserContext($pdo, $userId);
requireAnyPermission($user, ['students', 'edit-student', 'report-card']);

function progressionIsAdmin(array $userContext): bool
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

function progressionResultMap(PDO $pdo, array $studentUids, string $sessionFilter = ''): array
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
                SUM(CASE WHEN ' . $failExpr . ' THEN 1 ELSE 0 END) AS failed_subjects,
                SUM(COALESCE(r.' . apiIdent($resultMarksCol) . ', 0)) AS total_obtained,
                SUM(COALESCE(r.' . apiIdent($resultMaxMarksCol) . ', 0)) AS total_max
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
        $obtained = (float)($row['total_obtained'] ?? 0);
        $max = (float)($row['total_max'] ?? 0);
        $percentage = $max > 0 ? round(($obtained / $max) * 100, 2) : 0.0;

        $status = 'Pending';
        if ($totalSubjects > 0) {
            $status = $failedSubjects > 0 ? 'Fail' : 'Pass';
        }

        $result[$uid] = [
            'status' => $status,
            'total_subjects' => $totalSubjects,
            'failed_subjects' => $failedSubjects,
            'percentage' => $percentage
        ];
    }

    return $result;
}

$classId = queryIntParam('class_id', 0, 0, 100000);
$session = trim((string)($_GET['session'] ?? ''));
if ($session !== '' && mb_strlen($session) > 20) {
    $session = mb_substr($session, 0, 20);
}

if ($classId <= 0) {
    jsonResponse(200, [
        'success' => true,
        'message' => 'Students fetched',
        'data' => [
            'items' => [],
            'summary' => [
                'total' => 0,
                'pass' => 0,
                'fail' => 0,
                'pending' => 0
            ],
            'meta' => [
                'class_id' => 0,
                'class_name' => '',
                'session' => $session,
                'is_admin' => progressionIsAdmin($user)
            ]
        ]
    ]);
}

$studentCols = apiTableColumns($pdo, 'students');
$studentIdCol = apiResolveColumn($studentCols, ['id', 'student_id']);
$studentUidCol = apiResolveColumn($studentCols, ['uid', 'student_uid']);
$studentNameCol = apiResolveColumn($studentCols, ['full_name', 'name', 'student_name']);
$studentFatherCol = apiResolveColumn($studentCols, ['father_name', 'guardian_name']);
$studentRollCol = apiResolveColumn($studentCols, ['roll_no', 'roll_number']);
$studentMobileCol = apiResolveColumn($studentCols, ['mobile_number', 'mobile', 'phone']);
$studentClassCol = apiResolveColumn($studentCols, ['class', 'class_id']);
$studentCurrentClassCol = apiResolveColumn($studentCols, ['current_class']);
$studentStatusCol = apiResolveColumn($studentCols, ['status']);
$studentSessionCol = apiResolveColumn($studentCols, ['session', 'academic_session']);

if ($studentCols === [] || $studentIdCol === null || $studentUidCol === null) {
    jsonResponse(500, [
        'success' => false,
        'message' => 'Students table is not configured for progression'
    ]);
}

$classCols = apiTableColumns($pdo, 'class');
$classIdCol = apiResolveColumn($classCols, ['id', 'class_id']);
$classNameCol = apiResolveColumn($classCols, ['class_name', 'name']);
$classJoinSql = '';
if ($classIdCol !== null && $classNameCol !== null && $studentClassCol !== null) {
    $classJoinSql = ' LEFT JOIN ' . apiIdent('class') . ' c ON ('
        . 'c.' . apiIdent($classIdCol) . ' = CAST(s.' . apiIdent($studentClassCol) . ' AS UNSIGNED)'
        . ' OR c.' . apiIdent($classNameCol) . ' = s.' . apiIdent($studentClassCol)
        . ')';
}

$documentsJoinSql = '';
$photoExpr = "'' AS student_photo";
if (apiTableExists($pdo, 'documents')) {
    $documentCols = apiTableColumns($pdo, 'documents');
    $docUidCol = apiResolveColumn($documentCols, ['uid', 'student_uid']);
    $docPhotoCol = apiResolveColumn($documentCols, ['student_photo', 'photo', 'image']);
    if ($docUidCol !== null && $docPhotoCol !== null) {
        $documentsJoinSql = ' LEFT JOIN ' . apiIdent('documents') . ' d ON d.' . apiIdent($docUidCol) . ' = s.' . apiIdent($studentUidCol);
        $photoExpr = 'COALESCE(d.' . apiIdent($docPhotoCol) . ", '') AS student_photo";
    }
}

$classNameById = apiClassNameById($pdo, $classId);
$where = [];
$params = [];

if ($studentStatusCol !== null) {
    $where[] = "LOWER(COALESCE(s." . apiIdent($studentStatusCol) . ", '')) IN ('active', 'approved')";
}

if ($studentClassCol !== null) {
    if ($classNameById !== '') {
        $where[] = '('
            . 's.' . apiIdent($studentClassCol) . ' = :class_id_str'
            . ' OR CAST(s.' . apiIdent($studentClassCol) . ' AS UNSIGNED) = :class_id_int'
            . ' OR s.' . apiIdent($studentClassCol) . ' = :class_name'
            . ')';
        $params[':class_name'] = $classNameById;
    } else {
        $where[] = '('
            . 's.' . apiIdent($studentClassCol) . ' = :class_id_str'
            . ' OR CAST(s.' . apiIdent($studentClassCol) . ' AS UNSIGNED) = :class_id_int'
            . ')';
    }
    $params[':class_id_str'] = (string)$classId;
    $params[':class_id_int'] = $classId;
} else {
    jsonResponse(500, [
        'success' => false,
        'message' => 'Student class column is missing'
    ]);
}

$whereSql = $where === [] ? '' : (' WHERE ' . implode(' AND ', $where));
$classLabelExpr = "'-'";
if ($classJoinSql !== '') {
    $classLabelExpr = "COALESCE(NULLIF(c." . apiIdent($classNameCol) . ", ''), NULLIF(s." . apiIdent($studentClassCol) . ", ''), '-')"; // @phpstan-ignore-line
} elseif ($studentCurrentClassCol !== null) {
    $classLabelExpr = "COALESCE(NULLIF(s." . apiIdent($studentCurrentClassCol) . ", ''), NULLIF(s." . apiIdent($studentClassCol) . ", ''), '-')";
} else {
    $classLabelExpr = "COALESCE(NULLIF(s." . apiIdent($studentClassCol) . ", ''), '-')";
}

$sessionExpr = $studentSessionCol !== null
    ? "COALESCE(NULLIF(s." . apiIdent($studentSessionCol) . ", ''), '')"
    : "''";

try {
    $sql = 'SELECT
                s.' . apiIdent($studentIdCol) . ' AS student_id,
                COALESCE(s.' . apiIdent($studentUidCol) . ", '') AS uid,
                COALESCE(NULLIF(s." . apiIdent($studentNameCol ?? $studentUidCol) . ", ''), 'Student') AS full_name,
                " . ($studentFatherCol !== null
                    ? "COALESCE(NULLIF(s." . apiIdent($studentFatherCol) . ", ''), '-')"
                    : "'-'")
                . ' AS father_name,
                ' . ($studentRollCol !== null
                    ? "COALESCE(NULLIF(s." . apiIdent($studentRollCol) . ", ''), '-')"
                    : "'-'")
                . ' AS roll_no,
                ' . ($studentMobileCol !== null
                    ? "COALESCE(NULLIF(s." . apiIdent($studentMobileCol) . ", ''), '-')"
                    : "'-'")
                . ' AS mobile_number,
                ' . $classLabelExpr . ' AS class_name,
                ' . $sessionExpr . ' AS session,
                ' . $photoExpr . '
            FROM ' . apiIdent('students') . ' s'
            . $classJoinSql
            . $documentsJoinSql
            . $whereSql
            . ' ORDER BY CAST(COALESCE(NULLIF('
            . ($studentRollCol !== null ? 's.' . apiIdent($studentRollCol) : 's.' . apiIdent($studentIdCol))
            . ", ''), '0') AS UNSIGNED) ASC, s." . apiIdent($studentIdCol) . ' ASC';

    $stmt = $pdo->prepare($sql);
    foreach ($params as $key => $value) {
        if (is_int($value)) {
            $stmt->bindValue($key, $value, PDO::PARAM_INT);
            continue;
        }
        $stmt->bindValue($key, $value, PDO::PARAM_STR);
    }
    $stmt->execute();
    $items = $stmt->fetchAll(PDO::FETCH_ASSOC) ?: [];

    $uids = array_values(array_filter(array_map(static fn(array $item): string => trim((string)($item['uid'] ?? '')), $items), static fn(string $uid): bool => $uid !== ''));
    $resultMap = progressionResultMap($pdo, $uids, $session);

    $summary = ['total' => 0, 'pass' => 0, 'fail' => 0, 'pending' => 0];
    foreach ($items as &$item) {
        $uid = trim((string)($item['uid'] ?? ''));
        $annual = $resultMap[$uid] ?? ['status' => 'Pending', 'total_subjects' => 0, 'failed_subjects' => 0, 'percentage' => 0.0];
        $statusLabel = (string)($annual['status'] ?? 'Pending');
        $item['annual_result'] = $statusLabel;
        $item['total_subjects'] = (int)($annual['total_subjects'] ?? 0);
        $item['failed_subjects'] = (int)($annual['failed_subjects'] ?? 0);
        $item['percentage'] = (float)($annual['percentage'] ?? 0.0);

        $summary['total'] += 1;
        if ($statusLabel === 'Pass') {
            $summary['pass'] += 1;
        } elseif ($statusLabel === 'Fail') {
            $summary['fail'] += 1;
        } else {
            $summary['pending'] += 1;
        }
    }
    unset($item);

    jsonResponse(200, [
        'success' => true,
        'message' => 'Students fetched',
        'data' => [
            'items' => $items,
            'summary' => $summary,
            'meta' => [
                'class_id' => $classId,
                'class_name' => $classNameById,
                'session' => $session,
                'is_admin' => progressionIsAdmin($user)
            ]
        ]
    ]);
} catch (Throwable $e) {
    error_log('api/v2/progression failed: ' . $e->getMessage());
    jsonResponse(500, [
        'success' => false,
        'message' => 'Failed to fetch progression data'
    ]);
}

