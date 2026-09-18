<?php
declare(strict_types=1);

require_once __DIR__ . '/_common.php';
require_once __DIR__ . '/_message_events.php';

$pdo = apiDb();
$userId = requireAuthenticatedUserId();
$user = fetchUserContext($pdo, $userId);

$method = strtoupper($_SERVER['REQUEST_METHOD'] ?? 'GET');

if ($method === 'GET') {
    requireAnyPermission($user, ['attendance', 'students']);
    handleGet($pdo);
}

if ($method === 'POST') {
    requireAnyPermission($user, ['attendance']);
    handlePost($pdo);
}

jsonResponse(405, [
    'success' => false,
    'message' => 'Method not allowed'
]);

function handleGet(PDO $pdo): void
{
    $page = queryIntParam('page', 1, 1, 100000);
    $perPage = queryIntParam('per_page', 10, 1, 100);
    $search = queryStringParam('q', '', 120);
    $classId = queryIntParam('class_id', 0, 0, 100000);
    $status = queryEnumParam('status', ['all', 'present', 'absent'], 'all');
    $date = normalizeDateInput(queryStringParam('date', date('Y-m-d'), 20));

    $studentColumns = apiTableColumns($pdo, 'students');
    $fatherCol = apiResolveColumn($studentColumns, ['father_name', 'father']);
    $rollCol = apiResolveColumn($studentColumns, ['roll_no', 'roll_number', 'roll']);

    $params = [':attendance_date' => $date];
    $baseWhere = ["(s.status IS NULL OR s.status <> 'deleted')"];

    if ($search !== '') {
        $searchParts = [
            's.uid LIKE :q',
            's.full_name LIKE :q',
            's.mobile_number LIKE :q',
            'c.class_name LIKE :q'
        ];
        if ($fatherCol !== null) {
            $searchParts[] = 's.' . apiIdent($fatherCol) . ' LIKE :q';
        }
        if ($rollCol !== null) {
            $searchParts[] = 's.' . apiIdent($rollCol) . ' LIKE :q';
        }
        $baseWhere[] = '(' . implode(' OR ', $searchParts) . ')';
        $params[':q'] = '%' . $search . '%';
    }

    if ($classId > 0) {
        $className = apiClassNameById($pdo, $classId);
        if ($className !== '') {
            $baseWhere[] = '(s.class = :class_id_str OR s.class = :class_name OR c.id = :class_id_int OR c.class_name = :class_name)';
            $params[':class_id_str'] = (string) $classId;
            $params[':class_id_int'] = $classId;
            $params[':class_name'] = $className;
        } else {
            $baseWhere[] = '(s.class = :class_id_str OR c.id = :class_id_int)';
            $params[':class_id_str'] = (string) $classId;
            $params[':class_id_int'] = $classId;
        }
    }

    $where = $baseWhere;
    if ($status === 'present') {
        $where[] = "a.status = '1'";
    } elseif ($status === 'absent') {
        $where[] = "(a.id IS NULL OR a.status = '0')";
    }

    $baseWhereSql = $baseWhere ? (' WHERE ' . implode(' AND ', $baseWhere)) : '';
    $whereSql = $where ? (' WHERE ' . implode(' AND ', $where)) : '';

    $fromSql = ' FROM students s
        LEFT JOIN class c ON c.id = s.class
        LEFT JOIN student_attendance a ON a.student_id = s.id AND a.date = :attendance_date';

    $offset = ($page - 1) * $perPage;

    try {
        $countStmt = $pdo->prepare('SELECT COUNT(*)' . $fromSql . $whereSql);
        bindParams($countStmt, $params);
        $countStmt->execute();
        $total = (int) $countStmt->fetchColumn();

        $summaryStmt = $pdo->prepare(
            'SELECT COUNT(*) AS total_students,
                    SUM(CASE WHEN a.status = \'1\' THEN 1 ELSE 0 END) AS present_students'
            . $fromSql
            . $baseWhereSql
        );
        bindParams($summaryStmt, $params);
        $summaryStmt->execute();
        $summary = $summaryStmt->fetch(PDO::FETCH_ASSOC) ?: [];

        $listSql =
            'SELECT s.id AS student_id,
                    COALESCE(NULLIF(s.uid, \'\'), \'-\') AS uid,
                    COALESCE(NULLIF(s.full_name, \'\'), \'Unknown Student\') AS full_name,
                    ' . ($fatherCol !== null ? ('COALESCE(NULLIF(s.' . apiIdent($fatherCol) . ", ''), '-')") : ("'-'")) . ' AS father_name,
                    ' . ($rollCol !== null ? ('COALESCE(NULLIF(s.' . apiIdent($rollCol) . ", ''), '-')") : ("'-'")) . ' AS roll_no,
                    COALESCE(NULLIF(c.class_name, \'\'), NULLIF(s.class, \'\'), \'N/A\') AS class_name,
                    s.class AS class_id,
                    COALESCE(NULLIF(s.mobile_number, \'\'), \'-\') AS mobile_number,
                    a.id AS attendance_id,
                    a.date AS attendance_date,
                    a.time AS attendance_time,
                    COALESCE(a.status, \'0\') AS attendance_status'
            . $fromSql
            . $whereSql
            . ' ORDER BY s.full_name ASC, s.id ASC
                LIMIT :limit OFFSET :offset';

        $listStmt = $pdo->prepare($listSql);
        bindParams($listStmt, $params);
        $listStmt->bindValue(':limit', $perPage, PDO::PARAM_INT);
        $listStmt->bindValue(':offset', $offset, PDO::PARAM_INT);
        $listStmt->execute();
        $items = $listStmt->fetchAll(PDO::FETCH_ASSOC);

        $totalPages = max(1, (int) ceil($total / $perPage));
        $summaryTotal = (int) ($summary['total_students'] ?? 0);
        $summaryPresent = (int) ($summary['present_students'] ?? 0);

        jsonResponse(200, [
            'success' => true,
            'message' => 'Attendance fetched',
            'data' => [
                'items' => $items,
                'pagination' => [
                    'page' => $page,
                    'per_page' => $perPage,
                    'total' => $total,
                    'total_pages' => $totalPages
                ],
                'filters' => [
                    'q' => $search,
                    'class_id' => $classId,
                    'status' => $status,
                    'date' => $date
                ],
                'summary' => [
                    'students_total' => $summaryTotal,
                    'present' => $summaryPresent,
                    'absent' => max(0, $summaryTotal - $summaryPresent)
                ]
            ]
        ]);
    } catch (Throwable $e) {
        error_log('api/v2/student-attendance GET failed: ' . $e->getMessage());
        handleGetFallback($pdo, $page, $perPage, $search, $classId, $status, $date);
    }
}

function handlePost(PDO $pdo): void
{
    $payload = requestPayload();
    $action = strtolower(trim((string) ($payload['action'] ?? 'upsert')));

    if ($action === 'delete') {
        deleteAttendance($pdo, $payload);
        return;
    }

    if (!in_array($action, ['upsert', 'bulk_mark', 'toggle'], true)) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Invalid action'
        ]);
    }

    $studentIds = parseStudentIds($payload);
    if (empty($studentIds)) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'At least one student_id is required'
        ]);
    }

    $date = normalizeDateInput((string) ($payload['date'] ?? date('Y-m-d')));
    $time = normalizeTimeInput((string) ($payload['time'] ?? date('H:i:s')));
    $status = normalizeAttendanceStatus((string) ($payload['status'] ?? '1'));

    $placeholders = implode(',', array_fill(0, count($studentIds), '?'));

    try {
        if (!apiTableExists($pdo, 'student_attendance')) {
            jsonResponse(503, [
                'success' => false,
                'message' => 'Attendance table is not configured'
            ]);
        }

        $attendanceColumns = apiTableColumns($pdo, 'student_attendance');
        $attendanceIdCol = apiFirstColumn($attendanceColumns, ['id', 'attendance_id']);
        $attendanceStudentCol = apiFirstColumn($attendanceColumns, ['student_id']);
        $attendanceDateCol = apiFirstColumn($attendanceColumns, ['date', 'attendance_date']);
        $attendanceTimeCol = apiFirstColumn($attendanceColumns, ['time', 'attendance_time']);
        $attendanceStatusCol = apiFirstColumn($attendanceColumns, ['status', 'attendance_status']);
        $attendanceCreatedCol = apiFirstColumn($attendanceColumns, ['created_at']);
        $attendanceUpdatedCol = apiFirstColumn($attendanceColumns, ['updated_at']);

        if ($attendanceIdCol === null || $attendanceStudentCol === null || $attendanceDateCol === null || $attendanceStatusCol === null) {
            jsonResponse(503, [
                'success' => false,
                'message' => 'Attendance schema is incomplete'
            ]);
        }

        $pdo->beginTransaction();

        $studentsStmt = $pdo->prepare("SELECT id FROM students WHERE id IN ($placeholders)");
        $studentsStmt->execute($studentIds);
        $existingStudents = array_map('intval', array_column($studentsStmt->fetchAll(PDO::FETCH_ASSOC), 'id'));

        $missing = array_values(array_diff($studentIds, $existingStudents));
        if (!empty($missing)) {
            $pdo->rollBack();
            jsonResponse(404, [
                'success' => false,
                'message' => 'Student not found: ' . implode(', ', $missing)
            ]);
        }

        $existingStmt = $pdo->prepare(
            "SELECT " . apiIdent((string)$attendanceIdCol) . " AS id,
                    " . apiIdent((string)$attendanceStudentCol) . " AS student_id
             FROM student_attendance
             WHERE " . apiIdent((string)$attendanceDateCol) . " = ? AND " . apiIdent((string)$attendanceStudentCol) . " IN ($placeholders)"
        );
        $existingStmt->execute(array_merge([$date], $studentIds));
        $existingRows = $existingStmt->fetchAll(PDO::FETCH_ASSOC);

        $attendanceMap = [];
        foreach ($existingRows as $row) {
            $attendanceMap[(int) $row['student_id']] = (int) $row['id'];
        }

        $insertCols = [
            apiIdent((string)$attendanceStudentCol),
            apiIdent((string)$attendanceDateCol),
            apiIdent((string)$attendanceStatusCol)
        ];
        $insertVals = [':student_id', ':date', ':status'];
        if ($attendanceTimeCol !== null) {
            $insertCols[] = apiIdent((string)$attendanceTimeCol);
            $insertVals[] = ':time';
        }
        if ($attendanceCreatedCol !== null) {
            $insertCols[] = apiIdent((string)$attendanceCreatedCol);
            $insertVals[] = 'NOW()';
        }
        if ($attendanceUpdatedCol !== null) {
            $insertCols[] = apiIdent((string)$attendanceUpdatedCol);
            $insertVals[] = 'NOW()';
        }
        $insertStmt = $pdo->prepare(
            'INSERT INTO student_attendance (' . implode(', ', $insertCols) . ')
             VALUES (' . implode(', ', $insertVals) . ')'
        );

        $updateSet = [
            apiIdent((string)$attendanceStatusCol) . ' = :status'
        ];
        if ($attendanceTimeCol !== null) {
            $updateSet[] = apiIdent((string)$attendanceTimeCol) . ' = :time';
        }
        if ($attendanceUpdatedCol !== null) {
            $updateSet[] = apiIdent((string)$attendanceUpdatedCol) . ' = NOW()';
        }
        $updateStmt = $pdo->prepare(
            'UPDATE student_attendance
             SET ' . implode(', ', $updateSet) . '
             WHERE ' . apiIdent((string)$attendanceIdCol) . ' = :id'
        );

        $inserted = 0;
        $updated = 0;
        $records = [];

        foreach ($studentIds as $studentId) {
            if (isset($attendanceMap[$studentId])) {
                $attendanceId = $attendanceMap[$studentId];
                $updateStmt->execute([
                    ':time' => $time,
                    ':status' => $status,
                    ':id' => $attendanceId
                ]);
                $updated += 1;
            } else {
                $insertStmt->execute([
                    ':student_id' => $studentId,
                    ':date' => $date,
                    ':status' => $status
                ] + ($attendanceTimeCol !== null ? [':time' => $time] : []));
                $attendanceId = (int) $pdo->lastInsertId();
                $inserted += 1;
            }

            $records[] = [
                'attendance_id' => $attendanceId,
                'student_id' => $studentId,
                'date' => $date,
                'time' => $time,
                'status' => $status
            ];
        }

        $pdo->commit();

        // Gold-plan chargeable messaging trigger for attendance updates.
        try {
            $studentColumnsForMsg = apiTableColumns($pdo, 'students');
            $idColForMsg = apiFirstColumn($studentColumnsForMsg, ['id', 'student_id']);
            $uidColForMsg = apiFirstColumn($studentColumnsForMsg, ['uid', 'student_uid']);
            $nameColForMsg = apiFirstColumn($studentColumnsForMsg, ['full_name', 'name', 'student_name']);
            $fatherColForMsg = apiFirstColumn($studentColumnsForMsg, ['father_name', 'guardian_name']);
            $classColForMsg = apiFirstColumn($studentColumnsForMsg, ['class', 'class_id', 'current_class', 'applying_class']);
            $mobileColForMsg = apiFirstColumn($studentColumnsForMsg, ['mobile_number', 'mobile', 'phone', 'contact_no']);

            $recipients = [];
            if ($idColForMsg !== null && $nameColForMsg !== null && $mobileColForMsg !== null && $studentIds !== []) {
                $placeholdersMsg = implode(',', array_fill(0, count($studentIds), '?'));
                $selectParts = [
                    apiIdent((string)$idColForMsg) . ' AS student_id',
                    ($uidColForMsg !== null ? apiIdent((string)$uidColForMsg) : "''") . ' AS student_uid',
                    apiIdent((string)$nameColForMsg) . ' AS student_name',
                    ($fatherColForMsg !== null ? apiIdent((string)$fatherColForMsg) : "''") . ' AS father_name',
                    ($classColForMsg !== null ? apiIdent((string)$classColForMsg) : "''") . ' AS class_name',
                    apiIdent((string)$mobileColForMsg) . ' AS mobile_number',
                ];
                $msgStmt = $pdo->prepare(
                    'SELECT ' . implode(', ', $selectParts)
                    . ' FROM students WHERE ' . apiIdent((string)$idColForMsg) . ' IN (' . $placeholdersMsg . ')'
                );
                $msgStmt->execute($studentIds);
                $rows = $msgStmt->fetchAll(PDO::FETCH_ASSOC) ?: [];
                foreach ($rows as $row) {
                    $mobile = trim((string)($row['mobile_number'] ?? ''));
                    if ($mobile === '' || $mobile === '-') {
                        continue;
                    }
                    $recipients[] = [
                        'mobile' => $mobile,
                        'student_name' => (string)($row['student_name'] ?? ''),
                        'father_name' => (string)($row['father_name'] ?? ''),
                        'class_name' => (string)($row['class_name'] ?? ''),
                    ];
                }
            }

            erpMessageTrigger(
                $pdo,
                'attendance_marked',
                [
                    'attendance_status' => $status === '1' ? 'Present' : 'Absent',
                    'attendance_date' => $date,
                    'attendance_time' => $time,
                    'total_students' => count($studentIds),
                ],
                $recipients,
                [
                    'reference_type' => 'attendance',
                    'reference_id' => $date . '-' . md5(json_encode($studentIds)),
                    'actor_id' => (string)($_SESSION['user_id'] ?? ''),
                ]
            );
        } catch (Throwable $ignore) {
            // Messaging failures must not block attendance updates.
        }

        jsonResponse(200, [
            'success' => true,
            'message' => 'Attendance updated',
            'data' => [
                'inserted' => $inserted,
                'updated' => $updated,
                'records' => $records
            ]
        ]);
    } catch (Throwable $e) {
        if ($pdo->inTransaction()) {
            $pdo->rollBack();
        }

        error_log('api/v2/student-attendance POST failed: ' . $e->getMessage());
        jsonResponse(500, [
            'success' => false,
            'message' => 'Failed to update attendance'
        ]);
    }
}

function deleteAttendance(PDO $pdo, array $payload): void
{
    $attendanceId = (int) ($payload['attendance_id'] ?? $payload['id'] ?? 0);
    if ($attendanceId <= 0) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'attendance_id is required'
        ]);
    }

    try {
        $attendanceColumns = apiTableColumns($pdo, 'student_attendance');
        $attendanceIdCol = apiFirstColumn($attendanceColumns, ['id', 'attendance_id']);
        if ($attendanceIdCol === null) {
            jsonResponse(503, [
                'success' => false,
                'message' => 'Attendance schema is incomplete'
            ]);
        }

        $stmt = $pdo->prepare('DELETE FROM student_attendance WHERE ' . apiIdent((string)$attendanceIdCol) . ' = :id');
        $stmt->execute([':id' => $attendanceId]);

        if ($stmt->rowCount() < 1) {
            jsonResponse(404, [
                'success' => false,
                'message' => 'Attendance record not found'
            ]);
        }

        jsonResponse(200, [
            'success' => true,
            'message' => 'Attendance record deleted',
            'data' => [
                'attendance_id' => $attendanceId
            ]
        ]);
    } catch (Throwable $e) {
        error_log('api/v2/student-attendance delete failed: ' . $e->getMessage());
        jsonResponse(500, [
            'success' => false,
            'message' => 'Failed to delete attendance record'
        ]);
    }
}

function handleGetFallback(
    PDO $pdo,
    int $page,
    int $perPage,
    string $search,
    int $classId,
    string $status,
    string $date
): void {
    try {
        if (!apiTableExists($pdo, 'students')) {
            jsonResponse(200, [
                'success' => true,
                'message' => 'Attendance fetched',
                'data' => [
                    'items' => [],
                    'pagination' => [
                        'page' => $page,
                        'per_page' => $perPage,
                        'total' => 0,
                        'total_pages' => 1
                    ],
                    'filters' => [
                        'q' => $search,
                        'class_id' => $classId,
                        'status' => $status,
                        'date' => $date
                    ],
                    'summary' => [
                        'students_total' => 0,
                        'present' => 0,
                        'absent' => 0
                    ],
                    'mode' => 'fallback-no-students'
                ]
            ]);
        }

        $studentColumns = apiTableColumns($pdo, 'students');
        $studentIdCol = apiFirstColumn($studentColumns, ['id', 'student_id']);
        if ($studentIdCol === null) {
            jsonResponse(200, [
                'success' => true,
                'message' => 'Attendance fetched',
                'data' => [
                    'items' => [],
                    'pagination' => [
                        'page' => $page,
                        'per_page' => $perPage,
                        'total' => 0,
                        'total_pages' => 1
                    ],
                    'filters' => [
                        'q' => $search,
                        'class_id' => $classId,
                        'status' => $status,
                        'date' => $date
                    ],
                    'summary' => [
                        'students_total' => 0,
                        'present' => 0,
                        'absent' => 0
                    ],
                    'mode' => 'fallback-no-student-id'
                ]
            ]);
        }

        $uidCol = apiFirstColumn($studentColumns, ['uid', 'admission_no', 'roll_number']);
        $nameCol = apiFirstColumn($studentColumns, ['full_name', 'name', 'student_name']);
        $fatherCol = apiFirstColumn($studentColumns, ['father_name', 'father']);
        $rollCol = apiFirstColumn($studentColumns, ['roll_no', 'roll_number', 'roll']);
        $classCol = apiFirstColumn($studentColumns, ['class', 'class_id']);
        $mobileCol = apiFirstColumn($studentColumns, ['mobile_number', 'mobile', 'phone']);
        $statusCol = apiFirstColumn($studentColumns, ['status']);

        $attendanceColumns = apiTableExists($pdo, 'student_attendance')
            ? apiTableColumns($pdo, 'student_attendance')
            : [];
        $attendanceIdCol = apiFirstColumn($attendanceColumns, ['id', 'attendance_id']);
        $attendanceStudentCol = apiFirstColumn($attendanceColumns, ['student_id']);
        $attendanceDateCol = apiFirstColumn($attendanceColumns, ['date', 'attendance_date']);
        $attendanceTimeCol = apiFirstColumn($attendanceColumns, ['time', 'attendance_time']);
        $attendanceStatusCol = apiFirstColumn($attendanceColumns, ['status', 'attendance_status']);
        $attendanceAvailable = $attendanceStudentCol !== null && $attendanceDateCol !== null && $attendanceStatusCol !== null;

        $params = [':attendance_date' => $date];
        $where = [];
        if ($statusCol !== null) {
            $where[] = "(s." . apiIdent((string)$statusCol) . " IS NULL OR s." . apiIdent((string)$statusCol) . " <> 'deleted')";
        }
        if ($search !== '') {
            $searchParts = [
                's.' . apiIdent((string)($uidCol ?? $studentIdCol)) . ' LIKE :q',
                's.' . apiIdent((string)($nameCol ?? $studentIdCol)) . ' LIKE :q',
                's.' . apiIdent((string)($mobileCol ?? $studentIdCol)) . ' LIKE :q'
            ];
            if ($fatherCol !== null) {
                $searchParts[] = 's.' . apiIdent((string)$fatherCol) . ' LIKE :q';
            }
            if ($rollCol !== null) {
                $searchParts[] = 's.' . apiIdent((string)$rollCol) . ' LIKE :q';
            }
            $where[] = '(' . implode(' OR ', $searchParts) . ')';
            $params[':q'] = '%' . $search . '%';
        }
        if ($classId > 0 && $classCol !== null) {
            $where[] = 's.' . apiIdent((string)$classCol) . ' = :class_id';
            $params[':class_id'] = (string)$classId;
        }
        if ($status === 'present' && $attendanceAvailable) {
            $where[] = "a." . apiIdent((string)$attendanceStatusCol) . " = '1'";
        } elseif ($status === 'absent' && $attendanceAvailable) {
            $attendanceNullCol = $attendanceIdCol !== null ? (string)$attendanceIdCol : (string)$attendanceStudentCol;
            $where[] = "(a." . apiIdent($attendanceNullCol) . " IS NULL OR a." . apiIdent((string)$attendanceStatusCol) . " = '0')";
        }

        $whereSql = $where !== [] ? (' WHERE ' . implode(' AND ', $where)) : '';

        if ($attendanceAvailable) {
            $fromSql = ' FROM students s
                LEFT JOIN student_attendance a
                  ON a.' . apiIdent((string)$attendanceStudentCol) . ' = s.' . apiIdent((string)$studentIdCol) . '
                 AND a.' . apiIdent((string)$attendanceDateCol) . ' = :attendance_date';
        } else {
            $fromSql = ' FROM students s';
            unset($params[':attendance_date']);
        }

        $countStmt = $pdo->prepare('SELECT COUNT(*)' . $fromSql . $whereSql);
        bindParams($countStmt, $params);
        $countStmt->execute();
        $total = (int)$countStmt->fetchColumn();

        $offset = ($page - 1) * $perPage;
        $listStmt = $pdo->prepare(
            'SELECT s.' . apiIdent((string)$studentIdCol) . ' AS student_id,
                    ' . ($uidCol !== null ? ('COALESCE(NULLIF(s.' . apiIdent((string)$uidCol) . ", ''), '-')") : ("'-'")) . ' AS uid,
                    ' . ($nameCol !== null ? ('COALESCE(NULLIF(s.' . apiIdent((string)$nameCol) . ", ''), 'Unknown Student')") : ("'Unknown Student'")) . ' AS full_name,
                    ' . ($fatherCol !== null ? ('COALESCE(NULLIF(s.' . apiIdent((string)$fatherCol) . ", ''), '-')") : ("'-'")) . ' AS father_name,
                    ' . ($rollCol !== null ? ('COALESCE(NULLIF(s.' . apiIdent((string)$rollCol) . ", ''), '-')") : ("'-'")) . ' AS roll_no,
                    ' . ($classCol !== null ? ('COALESCE(NULLIF(s.' . apiIdent((string)$classCol) . ", ''), 'N/A')") : ("'N/A'")) . ' AS class_name,
                    ' . ($classCol !== null ? ('s.' . apiIdent((string)$classCol)) : ("''")) . ' AS class_id,
                    ' . ($mobileCol !== null ? ('COALESCE(NULLIF(s.' . apiIdent((string)$mobileCol) . ", ''), '-')") : ("'-'")) . ' AS mobile_number,
                    ' . ($attendanceAvailable && $attendanceIdCol !== null ? ('a.' . apiIdent((string)$attendanceIdCol)) : ('NULL')) . ' AS attendance_id,
                    ' . ($attendanceAvailable ? ('a.' . apiIdent((string)$attendanceDateCol)) : ("'" . $date . "'")) . ' AS attendance_date,
                    ' . ($attendanceAvailable && $attendanceTimeCol !== null ? ('a.' . apiIdent((string)$attendanceTimeCol)) : ("''")) . ' AS attendance_time,
                    ' . ($attendanceAvailable ? ('COALESCE(a.' . apiIdent((string)$attendanceStatusCol) . ", '0')") : ("'0'")) . ' AS attendance_status'
            . $fromSql
            . $whereSql
            . ' ORDER BY full_name ASC, student_id ASC
                LIMIT :limit OFFSET :offset'
        );
        bindParams($listStmt, $params);
        $listStmt->bindValue(':limit', $perPage, PDO::PARAM_INT);
        $listStmt->bindValue(':offset', $offset, PDO::PARAM_INT);
        $listStmt->execute();
        $items = $listStmt->fetchAll(PDO::FETCH_ASSOC) ?: [];

        $present = 0;
        foreach ($items as $row) {
            if ((string)($row['attendance_status'] ?? '0') === '1') {
                $present += 1;
            }
        }

        jsonResponse(200, [
            'success' => true,
            'message' => 'Attendance fetched',
            'data' => [
                'items' => $items,
                'pagination' => [
                    'page' => $page,
                    'per_page' => $perPage,
                    'total' => $total,
                    'total_pages' => max(1, (int)ceil($total / $perPage))
                ],
                'filters' => [
                    'q' => $search,
                    'class_id' => $classId,
                    'status' => $status,
                    'date' => $date
                ],
                'summary' => [
                    'students_total' => $total,
                    'present' => $present,
                    'absent' => max(0, $total - $present)
                ],
                'mode' => 'fallback'
            ]
        ]);
    } catch (Throwable $fallbackError) {
        error_log('api/v2/student-attendance fallback failed: ' . $fallbackError->getMessage());
        jsonResponse(500, [
            'success' => false,
            'message' => 'Failed to fetch attendance'
        ]);
    }
}

function bindParams(PDOStatement $stmt, array $params): void
{
    foreach ($params as $name => $value) {
        if ($name === ':class_id' || $name === ':class_id_int') {
            $stmt->bindValue($name, (int) $value, PDO::PARAM_INT);
            continue;
        }

        $stmt->bindValue($name, $value);
    }
}

function requestPayload(): array
{
    if (!empty($_POST)) {
        return $_POST;
    }

    $raw = file_get_contents('php://input');
    if ($raw === false || trim($raw) === '') {
        return [];
    }

    $decoded = json_decode($raw, true);
    return is_array($decoded) ? $decoded : [];
}

function parseStudentIds(array $payload): array
{
    $raw = [];

    if (isset($payload['student_id'])) {
        $raw[] = $payload['student_id'];
    }

    if (array_key_exists('student_ids', $payload)) {
        $value = $payload['student_ids'];
        if (is_array($value)) {
            $raw = array_merge($raw, $value);
        } elseif (is_string($value) && trim($value) !== '') {
            $trimmed = trim($value);
            if (substr($trimmed, 0, 1) === '[') {
                $decoded = json_decode($trimmed, true);
                if (is_array($decoded)) {
                    $raw = array_merge($raw, $decoded);
                }
            } else {
                $raw = array_merge($raw, explode(',', $trimmed));
            }
        }
    }

    $clean = [];
    foreach ($raw as $value) {
        if (!is_scalar($value) || !is_numeric($value)) {
            continue;
        }
        $id = (int) $value;
        if ($id > 0) {
            $clean[$id] = $id;
        }
    }

    return array_values($clean);
}

function normalizeDateInput(string $value): string
{
    $trimmed = trim($value);
    if ($trimmed === '') {
        return date('Y-m-d');
    }

    $date = DateTimeImmutable::createFromFormat('Y-m-d', $trimmed);
    if ($date !== false && $date->format('Y-m-d') === $trimmed) {
        return $trimmed;
    }

    return date('Y-m-d');
}

function normalizeTimeInput(string $value): string
{
    $trimmed = trim($value);
    if ($trimmed === '') {
        return date('H:i:s');
    }

    $time = DateTimeImmutable::createFromFormat('H:i:s', $trimmed);
    if ($time !== false && $time->format('H:i:s') === $trimmed) {
        return $trimmed;
    }

    $timeMinutes = DateTimeImmutable::createFromFormat('H:i', $trimmed);
    if ($timeMinutes !== false) {
        return $timeMinutes->format('H:i:s');
    }

    return date('H:i:s');
}

function normalizeAttendanceStatus(string $value): string
{
    $status = trim($value);
    if ($status === '1' || strtolower($status) === 'present') {
        return '1';
    }
    if ($status === '0' || strtolower($status) === 'absent') {
        return '0';
    }

    jsonResponse(400, [
        'success' => false,
        'message' => 'status must be 0 or 1'
    ]);
}
