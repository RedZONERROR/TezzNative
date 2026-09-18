<?php
declare(strict_types=1);

require_once __DIR__ . '/_common.php';

ensureMethod('GET');
$pdo = apiDb();
$userId = requireAuthenticatedUserId();
$user = fetchUserContext($pdo, $userId);
requireAnyPermission($user, ['attendance', 'students']);

$scope = queryEnumParam('scope', ['options', 'list'], 'list');

function normalizeReportMonth(string $month): string
{
    $token = trim($month);
    if (!preg_match('/^\d{4}-\d{2}$/', $token)) {
        return date('Y-m');
    }
    $year = (int) substr($token, 0, 4);
    $mon = (int) substr($token, 5, 2);
    if ($year < 2000 || $year > 2100 || $mon < 1 || $mon > 12) {
        return date('Y-m');
    }
    return sprintf('%04d-%02d', $year, $mon);
}

function monthRange(string $month): array
{
    $first = $month . '-01';
    $last = date('Y-m-t', strtotime($first) ?: time());
    $days = (int) date('t', strtotime($first) ?: time());
    return [$first, $last, $days];
}

function monthDaysList(string $month): array
{
    [$first, , $days] = monthRange($month);
    $baseTs = strtotime($first) ?: time();
    $items = [];
    for ($i = 1; $i <= $days; $i += 1) {
        $dayTs = strtotime(sprintf('+%d day', $i - 1), $baseTs) ?: $baseTs;
        $iso = date('Y-m-d', $dayTs);
        $items[] = [
            'date' => $iso,
            'day' => (int) date('j', $dayTs),
            'label' => date('d M', $dayTs),
            'weekday' => date('D', $dayTs)
        ];
    }
    return $items;
}

function bindParams(PDOStatement $stmt, array $params): void
{
    foreach ($params as $name => $value) {
        if (is_int($value)) {
            $stmt->bindValue($name, $value, PDO::PARAM_INT);
        } else {
            $stmt->bindValue($name, (string) $value, PDO::PARAM_STR);
        }
    }
}

try {
    if ($scope === 'options') {
        $classes = [];
        if (apiTableExists($pdo, 'class')) {
            $stmt = $pdo->query(
                "SELECT id, COALESCE(NULLIF(class_name, ''), CONCAT('Class ', id)) AS class_name
                 FROM class
                 WHERE COALESCE(status, 'active') = 'active'
                 ORDER BY id ASC"
            );
            $classes = $stmt->fetchAll(PDO::FETCH_ASSOC) ?: [];
        }

        jsonResponse(200, [
            'success' => true,
            'message' => 'Attendance report options loaded',
            'data' => [
                'classes' => $classes,
                'default_month' => date('Y-m')
            ]
        ]);
    }

    if (!apiTableExists($pdo, 'students')) {
        jsonResponse(200, [
            'success' => true,
            'message' => 'Attendance report loaded',
            'data' => [
                'month' => date('Y-m'),
                'days' => monthDaysList(date('Y-m')),
                'items' => [],
                'summary' => [
                    'total_students' => 0,
                    'working_days' => 0,
                    'month_days' => (int) date('t')
                ],
                'pagination' => [
                    'page' => 1,
                    'per_page' => 10,
                    'total' => 0,
                    'total_pages' => 1
                ]
            ]
        ]);
    }

    $page = queryIntParam('page', 1, 1, 100000);
    $perPage = queryIntParam('per_page', 120, 1, 2000);
    $fullClassRaw = strtolower(trim((string) ($_GET['full_class'] ?? '0')));
    $fullClass = in_array($fullClassRaw, ['1', 'true', 'yes', 'on'], true);
    if ($fullClass) {
        $page = 1;
        $perPage = 2000;
    }
    $search = queryStringParam('q', '', 120);
    $classId = queryIntParam('class_id', 0, 0, 1000000);
    $month = normalizeReportMonth(queryStringParam('month', date('Y-m'), 7));

    [$monthStart, $monthEnd, $monthDays] = monthRange($month);
    $days = monthDaysList($month);

    $studentColumns = apiTableColumns($pdo, 'students');
    $studentColumnMap = array_fill_keys($studentColumns, true);

    $idCol = apiResolveColumn($studentColumns, ['id']);
    $uidCol = apiResolveColumn($studentColumns, ['uid', 'student_uid']);
    $nameCol = apiResolveColumn($studentColumns, ['full_name', 'name', 'student_name']);
    $fatherCol = apiResolveColumn($studentColumns, ['father_name', 'father']);
    $rollCol = apiResolveColumn($studentColumns, ['roll_no', 'roll_number', 'roll']);
    $classRefCol = apiResolveColumn($studentColumns, ['class', 'current_class', 'applying_class']);
    $statusCol = apiResolveColumn($studentColumns, ['status']);
    $mobileCol = apiResolveColumn($studentColumns, ['mobile_number', 'mobile', 'phone']);

    if ($idCol === null || $nameCol === null) {
        jsonResponse(200, [
            'success' => true,
            'message' => 'Attendance report loaded',
            'data' => [
                'month' => $month,
                'days' => $days,
                'items' => [],
                'summary' => [
                    'total_students' => 0,
                    'working_days' => 0,
                    'month_days' => $monthDays
                ],
                'pagination' => [
                    'page' => $page,
                    'per_page' => $perPage,
                    'total' => 0,
                    'total_pages' => 1
                ]
            ]
        ]);
    }

    $hasClassTable = apiTableExists($pdo, 'class');
    $joinClass = ($hasClassTable && $classRefCol !== null)
        ? ' LEFT JOIN class c ON c.id = s.' . apiIdent($classRefCol)
        : '';

    $where = [];
    $params = [];

    if ($statusCol !== null) {
        $where[] = "LOWER(COALESCE(s." . apiIdent($statusCol) . ", 'active')) <> 'deleted'";
    }

    if ($search !== '') {
        $searchParts = [];
        $searchParts[] = 's.' . apiIdent($nameCol) . ' LIKE :q';
        if ($uidCol !== null) {
            $searchParts[] = 's.' . apiIdent($uidCol) . ' LIKE :q';
        }
        if ($fatherCol !== null) {
            $searchParts[] = 's.' . apiIdent($fatherCol) . ' LIKE :q';
        }
        if ($rollCol !== null) {
            $searchParts[] = 's.' . apiIdent($rollCol) . ' LIKE :q';
        }
        if ($mobileCol !== null) {
            $searchParts[] = 's.' . apiIdent($mobileCol) . ' LIKE :q';
        }
        if ($hasClassTable) {
            $searchParts[] = 'c.class_name LIKE :q';
        }
        if ($classRefCol !== null) {
            $searchParts[] = 's.' . apiIdent($classRefCol) . ' LIKE :q';
        }
        $where[] = '(' . implode(' OR ', $searchParts) . ')';
        $params[':q'] = '%' . $search . '%';
    }

    if ($classId > 0) {
        $className = apiClassNameById($pdo, $classId);
        if ($classRefCol !== null && $hasClassTable && $className !== '') {
            $where[] = '(c.id = :class_id OR c.class_name = :class_name OR s.' . apiIdent($classRefCol) . ' = :class_id_str OR s.' . apiIdent($classRefCol) . ' = :class_name)';
            $params[':class_id'] = $classId;
            $params[':class_id_str'] = (string) $classId;
            $params[':class_name'] = $className;
        } elseif ($classRefCol !== null) {
            $where[] = 's.' . apiIdent($classRefCol) . ' = :class_id_str';
            $params[':class_id_str'] = (string) $classId;
        } elseif ($hasClassTable) {
            $where[] = 'c.id = :class_id';
            $params[':class_id'] = $classId;
        }
    }

    $whereSql = $where ? (' WHERE ' . implode(' AND ', $where)) : '';
    $offset = ($page - 1) * $perPage;

    $countStmt = $pdo->prepare('SELECT COUNT(*) FROM students s' . $joinClass . $whereSql);
    bindParams($countStmt, $params);
    $countStmt->execute();
    $total = (int) $countStmt->fetchColumn();

    $classExpr = ($hasClassTable && $classRefCol !== null)
        ? ('COALESCE(NULLIF(c.class_name, \'\'), NULLIF(s.' . apiIdent($classRefCol) . ', \'\'), \'N/A\')')
        : ($classRefCol !== null ? ('COALESCE(NULLIF(s.' . apiIdent($classRefCol) . ', \'\'), \'N/A\')') : "'N/A'");

    $studentPhotoExpr = "'' AS student_photo";
    if ($uidCol !== null && apiTableExists($pdo, 'documents')) {
        $documentColumns = apiTableColumns($pdo, 'documents');
        $documentUidCol = apiResolveColumn($documentColumns, ['uid', 'student_uid']);
        $documentPhotoCol = apiResolveColumn($documentColumns, ['student_photo', 'photo', 'image']);
        $documentIdCol = apiResolveColumn($documentColumns, ['id', 'document_id']);
        if ($documentUidCol !== null && $documentPhotoCol !== null) {
            $studentPhotoExpr = 'COALESCE(('
                . 'SELECT d.' . apiIdent($documentPhotoCol)
                . ' FROM ' . apiIdent('documents') . ' d'
                . ' WHERE d.' . apiIdent($documentUidCol) . ' = s.' . apiIdent($uidCol)
                . ($documentIdCol !== null ? (' ORDER BY d.' . apiIdent($documentIdCol) . ' DESC') : '')
                . ' LIMIT 1'
                . "), '') AS student_photo";
        }
    }

    $listSql = 'SELECT s.' . apiIdent($idCol) . ' AS student_id,'
        . ' COALESCE(NULLIF(s.' . apiIdent($nameCol) . ", ''), 'Unknown Student') AS student_name,"
        . ($fatherCol !== null ? (' COALESCE(NULLIF(s.' . apiIdent($fatherCol) . ", ''), '-') AS father_name,") : " '-' AS father_name,")
        . ($rollCol !== null ? (' COALESCE(NULLIF(s.' . apiIdent($rollCol) . ", ''), '-') AS roll_no,") : " '-' AS roll_no,")
        . ' ' . $classExpr . ' AS class_name,'
        . ' ' . $studentPhotoExpr
        . ' FROM students s'
        . $joinClass
        . $whereSql
        . ' ORDER BY s.' . apiIdent($nameCol) . ' ASC, s.' . apiIdent($idCol) . ' ASC'
        . ' LIMIT :limit OFFSET :offset';

    $listStmt = $pdo->prepare($listSql);
    bindParams($listStmt, $params);
    $listStmt->bindValue(':limit', $perPage, PDO::PARAM_INT);
    $listStmt->bindValue(':offset', $offset, PDO::PARAM_INT);
    $listStmt->execute();
    $students = $listStmt->fetchAll(PDO::FETCH_ASSOC) ?: [];

    $studentIds = array_values(array_filter(array_map(static fn (array $row): int => (int) ($row['student_id'] ?? 0), $students)));

    $attendanceByStudent = [];
    $workingDays = 0;

    if ($studentIds !== [] && apiTableExists($pdo, 'student_attendance')) {
        $attendanceColumns = apiTableColumns($pdo, 'student_attendance');
        $attStudentCol = apiResolveColumn($attendanceColumns, ['student_id']);
        $attDateCol = apiResolveColumn($attendanceColumns, ['date', 'attendance_date']);
        $attStatusCol = apiResolveColumn($attendanceColumns, ['status', 'attendance_status']);
        $attTimeCol = apiResolveColumn($attendanceColumns, ['time', 'attendance_time']);

        if ($attStudentCol !== null && $attDateCol !== null && $attStatusCol !== null) {
            $idPlaceholders = [];
            $idParams = [];
            foreach ($studentIds as $index => $id) {
                $placeholder = ':sid_' . $index;
                $idPlaceholders[] = $placeholder;
                $idParams[$placeholder] = $id;
            }

            $attSql = 'SELECT sa.' . apiIdent($attStudentCol) . ' AS student_id,'
                . ' sa.' . apiIdent($attDateCol) . ' AS attendance_date,'
                . ' sa.' . apiIdent($attStatusCol) . ' AS attendance_status,'
                . ($attTimeCol !== null ? (' sa.' . apiIdent($attTimeCol) . ' AS attendance_time') : " NULL AS attendance_time")
                . ' FROM student_attendance sa'
                . ' WHERE sa.' . apiIdent($attDateCol) . ' BETWEEN :month_start AND :month_end'
                . ' AND sa.' . apiIdent($attStudentCol) . ' IN (' . implode(', ', $idPlaceholders) . ')';

            $attStmt = $pdo->prepare($attSql);
            $attStmt->bindValue(':month_start', $monthStart, PDO::PARAM_STR);
            $attStmt->bindValue(':month_end', $monthEnd, PDO::PARAM_STR);
            foreach ($idParams as $name => $value) {
                $attStmt->bindValue($name, $value, PDO::PARAM_INT);
            }
            $attStmt->execute();
            $attendanceRows = $attStmt->fetchAll(PDO::FETCH_ASSOC) ?: [];

            foreach ($attendanceRows as $row) {
                $sid = (int) ($row['student_id'] ?? 0);
                $date = trim((string) ($row['attendance_date'] ?? ''));
                if ($sid <= 0 || $date === '') {
                    continue;
                }
                $attendanceByStudent[$sid][$date] = [
                    'status' => strtoupper(trim((string) ($row['attendance_status'] ?? '0'))),
                    'time' => trim((string) ($row['attendance_time'] ?? ''))
                ];
            }

            // Working days are calculated from attendance-marked dates for filtered scope (old ERP behavior).
            $workingWhere = [];
            $workingParams = [
                ':month_start' => $monthStart,
                ':month_end' => $monthEnd
            ];

            if ($classId > 0 && $classRefCol !== null) {
                $className = apiClassNameById($pdo, $classId);
                if ($className !== '') {
                    $workingWhere[] = '(s.' . apiIdent($classRefCol) . ' = :w_class_id OR s.' . apiIdent($classRefCol) . ' = :w_class_name OR c.id = :w_class_id_int OR c.class_name = :w_class_name)';
                    $workingParams[':w_class_id'] = (string) $classId;
                    $workingParams[':w_class_id_int'] = $classId;
                    $workingParams[':w_class_name'] = $className;
                } else {
                    $workingWhere[] = '(s.' . apiIdent($classRefCol) . ' = :w_class_id OR c.id = :w_class_id_int)';
                    $workingParams[':w_class_id'] = (string) $classId;
                    $workingParams[':w_class_id_int'] = $classId;
                }
            }

            if ($statusCol !== null) {
                $workingWhere[] = "LOWER(COALESCE(s." . apiIdent($statusCol) . ", 'active')) <> 'deleted'";
            }

            $workingSql = 'SELECT COUNT(DISTINCT sa.' . apiIdent($attDateCol) . ')'
                . ' FROM student_attendance sa'
                . ' INNER JOIN students s ON s.' . apiIdent($idCol) . ' = sa.' . apiIdent($attStudentCol)
                . $joinClass
                . ' WHERE sa.' . apiIdent($attDateCol) . ' BETWEEN :month_start AND :month_end';
            if ($workingWhere !== []) {
                $workingSql .= ' AND ' . implode(' AND ', $workingWhere);
            }

            $workingStmt = $pdo->prepare($workingSql);
            bindParams($workingStmt, $workingParams);
            $workingStmt->execute();
            $workingDays = (int) $workingStmt->fetchColumn();
        }
    }

    $items = [];
    foreach ($students as $index => $row) {
        $studentId = (int) ($row['student_id'] ?? 0);
        $dayMap = [];
        $presentCount = 0;
        $leaveCount = 0;

        foreach ($days as $dayInfo) {
            $dateKey = (string) ($dayInfo['date'] ?? '');
            $entry = $attendanceByStudent[$studentId][$dateKey] ?? null;
            $status = $entry['status'] ?? '';

            $symbol = '-';
            $css = 'none';
            if ($status === '1' || $status === 'P' || $status === 'PRESENT') {
                $symbol = 'P';
                $css = 'present';
                $presentCount += 1;
            } elseif ($status === 'L' || $status === 'LEAVE') {
                $symbol = 'L';
                $css = 'leave';
                $leaveCount += 1;
            } elseif ($status === '0' || $status === 'A' || $status === 'ABSENT') {
                $symbol = 'A';
                $css = 'absent';
            }

            $dayMap[$dateKey] = [
                'status' => $status !== '' ? $status : null,
                'symbol' => $symbol,
                'css' => $css,
                'time' => $entry['time'] ?? ''
            ];
        }

        $absentCount = max(0, $workingDays - $presentCount - $leaveCount);
        $attendancePercent = $workingDays > 0 ? round(($presentCount / $workingDays) * 100, 1) : 0.0;

        $items[] = [
            'sn' => (($page - 1) * $perPage) + $index + 1,
            'student_id' => $studentId,
            'student_name' => (string) ($row['student_name'] ?? 'Unknown Student'),
            'father_name' => (string) ($row['father_name'] ?? '-'),
            'roll_no' => (string) ($row['roll_no'] ?? '-'),
            'class_name' => (string) ($row['class_name'] ?? 'N/A'),
            'student_photo' => (string) ($row['student_photo'] ?? ''),
            'present_count' => $presentCount,
            'absent_count' => $absentCount,
            'leave_count' => $leaveCount,
            'working_days' => $workingDays,
            'attendance_percent' => $attendancePercent,
            'attendance' => $dayMap
        ];
    }

    jsonResponse(200, [
        'success' => true,
        'message' => 'Student attendance report loaded',
        'data' => [
            'month' => $month,
            'month_start' => $monthStart,
            'month_end' => $monthEnd,
            'days' => $days,
            'working_days' => $workingDays,
            'items' => $items,
            'summary' => [
                'total_students' => $total,
                'working_days' => $workingDays,
                'month_days' => $monthDays,
                'page_students' => count($items)
            ],
            'pagination' => [
                'page' => $page,
                'per_page' => $perPage,
                'total' => $total,
                'total_pages' => max(1, (int) ceil($total / $perPage))
            ],
            'filters' => [
                'q' => $search,
                'class_id' => $classId,
                'month' => $month
            ]
        ]
    ]);
} catch (Throwable $e) {
    error_log('api/tezzerp/student-attendance-report failed: ' . $e->getMessage());
    jsonResponse(500, [
        'success' => false,
        'message' => 'Failed to load student attendance report'
    ]);
}
