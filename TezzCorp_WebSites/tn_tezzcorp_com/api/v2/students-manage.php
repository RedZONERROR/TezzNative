<?php
declare(strict_types=1);

require_once __DIR__ . '/_common.php';

ensureMethod('POST');
$pdo = apiDb();
$userId = requireAuthenticatedUserId();
$user = fetchUserContext($pdo, $userId);
requireAnyPermission($user, ['edit-student', 'students']);
$studentColumns = apiTableColumns($pdo, 'students');
$studentsHasTransportColumn = apiResolveColumn($studentColumns, ['transport', 'is_transport']);
$studentsHasTransportIdColumn = apiResolveColumn($studentColumns, ['transport_id']);
$studentsAdditionalTransportColumn = apiResolveColumn($studentColumns, ['additional_transport']);
$studentsHasUpdatedAt = apiTableHasColumn($pdo, 'students', 'updated_at');
$documentsHasUid = apiTableExists($pdo, 'documents') && apiTableHasColumn($pdo, 'documents', 'uid');

$action = strtolower(trim((string) ($_POST['action'] ?? 'update')));

function normalizeUids($raw): array
{
    if (is_array($raw)) {
        $uids = $raw;
    } elseif (is_string($raw) && $raw !== '') {
        $decoded = json_decode($raw, true);
        if (is_array($decoded)) {
            $uids = $decoded;
        } else {
            $uids = preg_split('/\s*,\s*/', $raw) ?: [];
        }
    } else {
        $uids = [];
    }

    $uids = array_map(static fn($value) => trim((string) $value), $uids);
    return array_values(array_filter($uids, static fn($value) => $value !== ''));
}

/**
 * @return array<string, array<string, mixed>>
 */
function studentsManageTableMeta(PDO $pdo, string $table): array
{
    static $cache = [];
    $safeTable = str_replace('`', '', trim($table));
    if ($safeTable === '') {
        return [];
    }
    if (isset($cache[$safeTable])) {
        return $cache[$safeTable];
    }
    if (!apiTableExists($pdo, $safeTable)) {
        $cache[$safeTable] = [];
        return [];
    }

    try {
        $stmt = $pdo->query('SHOW COLUMNS FROM ' . apiIdent($safeTable));
        $rows = $stmt->fetchAll(PDO::FETCH_ASSOC) ?: [];
        $meta = [];
        foreach ($rows as $row) {
            $field = strtolower(trim((string)($row['Field'] ?? '')));
            if ($field === '') {
                continue;
            }
            $meta[$field] = $row;
        }
        $cache[$safeTable] = $meta;
        return $meta;
    } catch (Throwable $e) {
        error_log('api/v2/students-manage table meta failed: ' . $e->getMessage());
        $cache[$safeTable] = [];
        return [];
    }
}

function studentsManageTransportMode(?string $transportColumn, ?string $transportIdColumn, array $meta): string
{
    if ($transportIdColumn !== null) {
        return 'separate';
    }
    if ($transportColumn === null) {
        return 'none';
    }
    $columnMeta = $meta[strtolower($transportColumn)] ?? [];
    $type = strtolower(trim((string)($columnMeta['Type'] ?? '')));
    if ($type === '') {
        return 'flag';
    }
    if (preg_match('/^tinyint\b/', $type)) {
        return 'flag';
    }
    if (preg_match('/^(smallint|mediumint|int|bigint|decimal|numeric|float|double|real)\b/', $type)) {
        return 'route_id';
    }
    return 'flag';
}

$studentsMeta = studentsManageTableMeta($pdo, 'students');
$studentsTransportMode = studentsManageTransportMode($studentsHasTransportColumn, $studentsHasTransportIdColumn, $studentsMeta);
$studentsTransportUsesRouteColumn = $studentsTransportMode === 'route_id';

if ($action === 'approve' || $action === 'delete') {
    $uids = normalizeUids($_POST['uids'] ?? []);
    if (empty($uids)) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'No student UIDs provided'
        ]);
    }

    $placeholders = implode(',', array_fill(0, count($uids), '?'));

    try {
        $pdo->beginTransaction();
        $checkStmt = $pdo->prepare("SELECT uid FROM students WHERE uid IN ($placeholders)");
        $checkStmt->execute($uids);
        $existing = $checkStmt->fetchAll(PDO::FETCH_COLUMN, 0);

        $missing = array_diff($uids, $existing);
        if (!empty($missing)) {
            $pdo->rollBack();
            jsonResponse(404, [
                'success' => false,
                'message' => 'Students not found: ' . implode(', ', $missing)
            ]);
        }

        if ($action === 'approve') {
            $approveSet = "status = 'active'";
            if ($studentsHasUpdatedAt) {
                $approveSet .= ', updated_at = NOW()';
            }
            $updateStmt = $pdo->prepare("UPDATE students SET {$approveSet} WHERE uid IN ($placeholders)");
            $updateStmt->execute($uids);

            $pdo->commit();
            erpSendAdminNotification(
                $pdo,
                'Students approved',
                'Student approvals completed for ' . count($uids) . ' record(s).',
                'good'
            );
            jsonResponse(200, [
                'success' => true,
                'message' => 'Students approved successfully',
                'data' => ['count' => $updateStmt->rowCount()]
            ]);
        }

        $deleteType = strtolower(trim((string) ($_POST['delete_type'] ?? 'temporary')));
        if (!in_array($deleteType, ['temporary', 'permanent'], true)) {
            $pdo->rollBack();
            jsonResponse(400, [
                'success' => false,
                'message' => 'Invalid delete type'
            ]);
        }

        if ($deleteType === 'temporary') {
            $temporarySet = "status = 'deleted'";
            if ($studentsHasUpdatedAt) {
                $temporarySet .= ', updated_at = NOW()';
            }
            $deleteStmt = $pdo->prepare("UPDATE students SET {$temporarySet} WHERE uid IN ($placeholders)");
            $deleteStmt->execute($uids);
        } else {
            if ($documentsHasUid) {
                $docStmt = $pdo->prepare("DELETE FROM documents WHERE uid IN ($placeholders)");
                $docStmt->execute($uids);
            }
            $deleteStmt = $pdo->prepare("DELETE FROM students WHERE uid IN ($placeholders)");
            $deleteStmt->execute($uids);
        }

        $pdo->commit();
        erpSendAdminNotification(
            $pdo,
            $deleteType === 'temporary' ? 'Students marked deleted' : 'Students deleted permanently',
            'Student delete action completed for ' . count($uids) . ' record(s).',
            'warning'
        );
        jsonResponse(200, [
            'success' => true,
            'message' => $deleteType === 'temporary'
                ? 'Students marked as deleted'
                : 'Students deleted permanently',
            'data' => ['count' => $deleteStmt->rowCount()]
        ]);
    } catch (Throwable $e) {
        if ($pdo->inTransaction()) {
            $pdo->rollBack();
        }
        error_log('api/v2/students-manage bulk action failed: ' . $e->getMessage());
        jsonResponse(500, [
            'success' => false,
            'message' => 'Student update failed'
        ]);
    }
}

if ($action !== 'update') {
    jsonResponse(400, [
        'success' => false,
        'message' => 'Unsupported action'
    ]);
}

$uid = trim((string) ($_POST['uid'] ?? ''));
$studentId = (int) ($_POST['student_id'] ?? 0);
if ($uid === '' && $studentId <= 0) {
    jsonResponse(400, [
        'success' => false,
        'message' => 'Student UID or student ID is required'
    ]);
}

$fullName = trim((string) ($_POST['full_name'] ?? ''));
if ($fullName === '') {
    jsonResponse(400, [
        'success' => false,
        'message' => 'Student name is required'
    ]);
}

$classValueRaw = trim((string) ($_POST['class_id'] ?? ($_POST['class'] ?? '')));
$classNameFallback = trim((string) ($_POST['class_name'] ?? ''));
$classValue = $classValueRaw !== '' ? $classValueRaw : $classNameFallback;
$mobile = trim((string) ($_POST['mobile_number'] ?? ''));
$email = trim((string) ($_POST['email_address'] ?? ''));
$status = strtolower(trim((string) ($_POST['status'] ?? 'active')));
$admissionDate = trim((string) ($_POST['admission_date'] ?? ''));
$hasTransportInput = array_key_exists('transport', $_POST);
$hasTransportIdInput = array_key_exists('transport_id', $_POST);
$transportRequested = $hasTransportInput || $hasTransportIdInput;
$transportId = null;
$transportFlag = 0;
$transportRaw = '';
$transportIdRaw = '';
$transportRouteValueSet = false;
$transportRouteValue = null;

if ($classValue === '') {
    jsonResponse(400, [
        'success' => false,
        'message' => 'Class is required'
    ]);
}

if ($mobile !== '' && !preg_match('/^\d{10,15}$/', $mobile)) {
    jsonResponse(400, [
        'success' => false,
        'message' => 'Mobile number must be 10 to 15 digits'
    ]);
}

if ($email !== '' && !filter_var($email, FILTER_VALIDATE_EMAIL)) {
    jsonResponse(400, [
        'success' => false,
        'message' => 'Invalid email address'
    ]);
}

$allowedStatuses = ['active', 'approved', 'deleted'];
if (!in_array($status, $allowedStatuses, true)) {
    jsonResponse(400, [
        'success' => false,
        'message' => 'Invalid student status'
    ]);
}

if ($admissionDate !== '' && !preg_match('/^\d{4}-\d{2}-\d{2}$/', $admissionDate)) {
    jsonResponse(400, [
        'success' => false,
        'message' => 'Admission date must be in YYYY-MM-DD format'
    ]);
}

if ($transportRequested) {
    $transportRaw = trim((string) ($_POST['transport'] ?? ''));
    $transportIdRaw = trim((string) ($_POST['transport_id'] ?? ''));
    if ($transportIdRaw !== '' && is_numeric($transportIdRaw)) {
        $candidateTransportId = (int) $transportIdRaw;
        if ($candidateTransportId > 0) {
            $transportId = $candidateTransportId;
        }
    }

    $explicitTransport = null;
    if ($hasTransportInput) {
        if ($transportRaw === '') {
            $explicitTransport = 0;
        } elseif (is_numeric($transportRaw)) {
            $candidateTransport = (int) $transportRaw;
            if ($candidateTransport > 1) {
                if ($transportId === null) {
                    $transportId = $candidateTransport;
                }
                $explicitTransport = 1;
            } else {
                $explicitTransport = $candidateTransport === 1 ? 1 : 0;
            }
        } else {
            $normalizedTransport = strtolower($transportRaw);
            $explicitTransport = in_array($normalizedTransport, ['1', 'true', 'yes', 'on'], true) ? 1 : 0;
        }
    }

    $transportFlag = $explicitTransport !== null ? $explicitTransport : ($transportId !== null ? 1 : 0);
    if ($transportFlag !== 1) {
        $transportFlag = 0;
        $transportId = null;
    }

    if ($transportFlag !== 1) {
        $transportRouteValueSet = true;
        $transportRouteValue = null;
    } elseif ($transportId !== null) {
        $transportRouteValueSet = true;
        $transportRouteValue = $transportId;
    }
}

try {
    if ($uid !== '') {
        $existsStmt = $pdo->prepare('SELECT id, uid FROM students WHERE uid = :uid LIMIT 1');
        $existsStmt->execute([':uid' => $uid]);
    } else {
        $existsStmt = $pdo->prepare('SELECT id, uid FROM students WHERE id = :id LIMIT 1');
        $existsStmt->execute([':id' => $studentId]);
    }
    $targetStudent = $existsStmt->fetch(PDO::FETCH_ASSOC);
    if (!$targetStudent) {
        jsonResponse(404, [
            'success' => false,
            'message' => 'Student not found'
        ]);
    }
    $targetId = (int) ($targetStudent['id'] ?? 0);

    $set = [
        'full_name = :full_name',
        '`class` = :class_value',
        'mobile_number = :mobile_number',
        'email_address = :email_address',
        'status = :status'
    ];
    if ($studentsHasUpdatedAt) {
        $set[] = 'updated_at = NOW()';
    }
    $params = [
        ':target_id' => $targetId,
        ':full_name' => $fullName,
        ':class_value' => $classValue,
        ':mobile_number' => $mobile,
        ':email_address' => $email,
        ':status' => $status
    ];

    if ($admissionDate !== '') {
        $set[] = 'admission_date = :admission_date';
        $params[':admission_date'] = $admissionDate;
    }

    if ($transportRequested) {
        if ($studentsTransportUsesRouteColumn) {
            if ($studentsHasTransportColumn !== null && $transportRouteValueSet) {
                $set[] = apiIdent($studentsHasTransportColumn) . ' = :transport_route_value';
                $params[':transport_route_value'] = $transportRouteValue;
            }
        } else {
            if ($studentsHasTransportIdColumn !== null) {
                $set[] = apiIdent($studentsHasTransportIdColumn) . ' = :transport_id';
                $params[':transport_id'] = $transportId;
            }
            if ($studentsHasTransportColumn !== null) {
                $set[] = apiIdent($studentsHasTransportColumn) . ' = :transport_flag';
                $params[':transport_flag'] = $transportFlag;
            }
        }
        if ($studentsAdditionalTransportColumn !== null && $transportFlag !== 1) {
            $set[] = apiIdent($studentsAdditionalTransportColumn) . ' = :additional_transport';
            $params[':additional_transport'] = '';
        }
    }

    $updateStmt = $pdo->prepare('UPDATE students SET ' . implode(', ', $set) . ' WHERE id = :target_id');
    $updateStmt->execute($params);

    $transportFlagSql = '0 AS transport';
    $transportIdSql = '0 AS transport_id';
    if ($studentsTransportUsesRouteColumn && $studentsHasTransportColumn !== null) {
        $transportIdSql = 'COALESCE(CAST(s.' . apiIdent($studentsHasTransportColumn) . ' AS UNSIGNED), 0) AS transport_id';
        $transportFlagSql = '(CASE WHEN CAST(s.' . apiIdent($studentsHasTransportColumn) . ' AS UNSIGNED) > 0 THEN 1 ELSE 0 END) AS transport';
    } elseif ($studentsHasTransportIdColumn !== null) {
        $transportIdSql = 'COALESCE(CAST(s.' . apiIdent($studentsHasTransportIdColumn) . ' AS UNSIGNED), 0) AS transport_id';
    }
    if (!$studentsTransportUsesRouteColumn && $studentsHasTransportColumn !== null) {
        $fallbackSql = $studentsHasTransportIdColumn !== null
            ? '(CASE WHEN CAST(s.' . apiIdent($studentsHasTransportIdColumn) . ' AS UNSIGNED) > 0 THEN 1 ELSE 0 END)'
            : '0';
        $transportFlagSql = 'COALESCE(CAST(s.' . apiIdent($studentsHasTransportColumn) . ' AS UNSIGNED), ' . $fallbackSql . ') AS transport';
    } elseif (!$studentsTransportUsesRouteColumn && $studentsHasTransportIdColumn !== null) {
        $transportFlagSql = '(CASE WHEN CAST(s.' . apiIdent($studentsHasTransportIdColumn) . ' AS UNSIGNED) > 0 THEN 1 ELSE 0 END) AS transport';
    }

    $rowSql = 'SELECT s.id,
                      s.uid,
                      COALESCE(NULLIF(s.full_name, \'\'), \'Unknown Student\') AS full_name,
                      COALESCE(NULLIF(s.father_name, \'\'), \'-\') AS father_name,
                      COALESCE(NULLIF(s.mother_name, \'\'), \'-\') AS mother_name,
                      s.class AS class_id,
                      COALESCE(NULLIF(c.class_name, \'\'), NULLIF(s.`class`, \'\'), NULLIF(s.current_class, \'\'), NULLIF(s.applying_class, \'\'), \'N/A\') AS class_name,
                      COALESCE(NULLIF(s.roll_no, \'\'), \'-\') AS roll_no,
                      COALESCE(NULLIF(s.mobile_number, \'\'), \'-\') AS mobile_number,
                      COALESCE(NULLIF(s.email_address, \'\'), \'-\') AS email_address,
                      COALESCE(s.status, \'unknown\') AS status,
                      ' . $transportFlagSql . ',
                      ' . $transportIdSql . ',
                      s.admission_date
               FROM students s
               LEFT JOIN class c ON (c.id = CAST(s.class AS UNSIGNED) OR c.class_name = s.class)
               WHERE s.id = :target_id
               LIMIT 1';
    $rowStmt = $pdo->prepare($rowSql);
    $rowStmt->execute([':target_id' => $targetId]);
    $updatedStudent = $rowStmt->fetch(PDO::FETCH_ASSOC);

    $studentName = (string) ($updatedStudent['full_name'] ?? $fullName);
    $className = (string) ($updatedStudent['class_name'] ?? '-');
    $rollNo = (string) ($updatedStudent['roll_no'] ?? '-');
    $fatherName = (string) ($updatedStudent['father_name'] ?? '-');
    $motherName = (string) ($updatedStudent['mother_name'] ?? '-');
    $mobileNumber = (string) ($updatedStudent['mobile_number'] ?? '-');
    erpSendAdminNotification(
        $pdo,
        'Student record updated',
        "Student {$studentName} has been updated.\nClass: {$className}\nRoll No: {$rollNo}\nFather: {$fatherName}\nMother: {$motherName}\nMobile: {$mobileNumber}",
        'info'
    );

    jsonResponse(200, [
        'success' => true,
        'message' => 'Student updated successfully',
        'data' => [
            'item' => $updatedStudent
        ]
    ]);
} catch (Throwable $e) {
    error_log('api/v2/students-manage update failed: ' . $e->getMessage());
    jsonResponse(500, [
        'success' => false,
        'message' => 'Failed to update student'
    ]);
}
