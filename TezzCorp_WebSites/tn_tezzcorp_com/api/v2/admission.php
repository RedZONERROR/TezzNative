<?php
declare(strict_types=1);

require_once __DIR__ . '/_common.php';

$requestMethod = strtoupper((string) ($_SERVER['REQUEST_METHOD'] ?? 'GET'));
if (!in_array($requestMethod, ['GET', 'POST'], true)) {
    jsonResponse(405, [
        'success' => false,
        'message' => 'Method not allowed'
    ]);
}

$pdo = apiDb();
$userId = requireAuthenticatedUserId();
$user = fetchUserContext($pdo, $userId);
requireAnyPermission($user, ['admission', 'students']);

if ($requestMethod === 'GET') {
    $action = strtolower(trim((string) ($_GET['action'] ?? 'student')));
    if ($action !== 'student') {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Unsupported action'
        ]);
    }

    $uid = trim((string) ($_GET['uid'] ?? ''));
    $studentId = (int) ($_GET['student_id'] ?? 0);
    if ($uid === '' && $studentId <= 0) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Student UID or student ID is required'
        ]);
    }

    try {
        $studentTableColumns = apiTableColumns($pdo, 'students');
        $idColumn = apiFirstColumn($studentTableColumns, ['id', 'student_id']);
        $uidColumn = apiFirstColumn($studentTableColumns, ['uid', 'student_uid']);
        if ($idColumn === null && $uidColumn === null) {
            jsonResponse(500, [
                'success' => false,
                'message' => 'Students table is not configured'
            ]);
        }

        $where = '';
        $params = [];
        if ($uid !== '' && $uidColumn !== null) {
            $where = 's.' . apiIdent($uidColumn) . ' = :uid';
            $params[':uid'] = $uid;
        } elseif ($studentId > 0 && $idColumn !== null) {
            $where = 's.' . apiIdent($idColumn) . ' = :id';
            $params[':id'] = $studentId;
        } elseif ($uid !== '' && $idColumn !== null) {
            // Fallback when uid column does not exist.
            $where = 's.' . apiIdent($idColumn) . ' = :id';
            $params[':id'] = (int)$uid;
        } else {
            jsonResponse(400, [
                'success' => false,
                'message' => 'Student lookup column is not available'
            ]);
        }

        $studentStmt = $pdo->prepare(
            'SELECT s.* FROM students s WHERE ' . $where . ' LIMIT 1'
        );
        $studentStmt->execute($params);
        $student = $studentStmt->fetch(PDO::FETCH_ASSOC);
        if (!$student) {
            jsonResponse(404, [
                'success' => false,
                'message' => 'Student not found'
            ]);
        }

        // Build class_name in a schema-safe way so this endpoint works with
        // both legacy and modern student schemas.
        $classValue = '';
        foreach (['class', 'current_class', 'applying_class'] as $candidateCol) {
            if (array_key_exists($candidateCol, $student)) {
                $candidateValue = trim((string)($student[$candidateCol] ?? ''));
                if ($candidateValue !== '') {
                    $classValue = $candidateValue;
                    break;
                }
            }
        }

        $resolvedClassName = '';
        if ($classValue !== '') {
            $classColumns = apiTableColumns($pdo, 'class');
            $classIdCol = apiFirstColumn($classColumns, ['id', 'class_id']);
            $classNameCol = apiFirstColumn($classColumns, ['class_name', 'name', 'title']);
            if ($classIdCol !== null && $classNameCol !== null) {
                $classStmt = $pdo->prepare(
                    'SELECT ' . apiIdent($classNameCol) . ' AS class_name
                     FROM class
                     WHERE ' . apiIdent($classIdCol) . ' = :class_id
                        OR LOWER(' . apiIdent($classNameCol) . ') = LOWER(:class_name)
                     LIMIT 1'
                );
                $classStmt->execute([
                    ':class_id' => (int)$classValue,
                    ':class_name' => $classValue
                ]);
                $classRow = $classStmt->fetch(PDO::FETCH_ASSOC) ?: [];
                $resolvedClassName = trim((string)($classRow['class_name'] ?? ''));
            }
        }

        $student['class_name'] = $resolvedClassName !== '' ? $resolvedClassName : ($classValue !== '' ? $classValue : 'N/A');

        $documents = [];
        if (apiTableHasColumn($pdo, 'documents', 'uid')) {
            $documentFields = array_values(array_intersect(
                apiTableColumns($pdo, 'documents'),
                [
                    'uid',
                    'aadhaar_card_upload',
                    'progress_report',
                    'birth_certificate',
                    'slc',
                    'residential_proof',
                    'father_photo',
                    'student_photo',
                    'mother_photo'
                ]
            ));

            if (!empty($documentFields)) {
                $quotedFields = implode(', ', array_map(static fn(string $field): string => "`{$field}`", $documentFields));
                $documentsStmt = $pdo->prepare(
                    "SELECT {$quotedFields}
                     FROM documents
                     WHERE uid = :uid
                     LIMIT 1"
                );
                $documentsStmt->execute([':uid' => (string) ($student['uid'] ?? '')]);
                $documents = $documentsStmt->fetch(PDO::FETCH_ASSOC) ?: [];
            }
        }

        jsonResponse(200, [
            'success' => true,
            'message' => 'Student admission record fetched',
            'data' => [
                'item' => array_merge($student, $documents)
            ]
        ]);
    } catch (Throwable $e) {
        error_log('api/v2/admission get student failed: ' . $e->getMessage());
        jsonResponse(500, [
            'success' => false,
            'message' => 'Failed to load student admission record'
        ]);
    }
}

$payload = $_POST ?: (json_decode(file_get_contents('php://input'), true) ?: []);

if (!is_array($payload) || $payload === []) {
    jsonResponse(400, [
        'success' => false,
        'message' => 'Missing admission data'
    ]);
}

function generateAdmissionUid(PDO $pdo): string
{
    $prefix = 'STU';
    $characters = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ';
    $maxAttempts = 10;

    for ($attempt = 0; $attempt < $maxAttempts; $attempt += 1) {
        $random = '';
        for ($i = 0; $i < 13; $i += 1) {
            $random .= $characters[random_int(0, strlen($characters) - 1)];
        }
        $uid = $prefix . $random;
        $stmt = $pdo->prepare('SELECT uid FROM students WHERE uid = ?');
        $stmt->execute([$uid]);
        if (!$stmt->fetch()) {
            return $uid;
        }
    }

    throw new RuntimeException('Failed to generate unique UID');
}

/**
 * Fetch SHOW COLUMNS metadata keyed by lowercase field name.
 *
 * @return array<string, array<string, mixed>>
 */
function admissionTableMeta(PDO $pdo, string $table): array
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
            $field = trim((string)($row['Field'] ?? ''));
            if ($field === '') {
                continue;
            }
            $meta[strtolower($field)] = $row;
        }
        $cache[$safeTable] = $meta;
        return $meta;
    } catch (Throwable $e) {
        error_log('api/v2/admission table meta failed: ' . $e->getMessage());
        $cache[$safeTable] = [];
        return [];
    }
}

/**
 * Determine how transport data is stored in students table.
 * - separate: dedicated `transport_id` exists
 * - route_id: `transport` itself stores route id (legacy schemas)
 * - flag: `transport` is a boolean-like flag
 */
function admissionTransportColumnMode(?string $transportFlagColumn, ?string $transportIdColumn, array $studentMeta): string
{
    if ($transportIdColumn !== null) {
        return 'separate';
    }
    if ($transportFlagColumn === null) {
        return 'none';
    }

    $meta = $studentMeta[strtolower($transportFlagColumn)] ?? [];
    $type = strtolower(trim((string)($meta['Type'] ?? '')));
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

function validateAdmissionInput(PDO $pdo, array $data, bool $isUpdate = false, ?string $existingUid = null): array
{
    $errors = [];
    $studentData = [];
    $documents = [];

    $studentFields = [
        'aadhaar_number' => ['type' => 'string', 'pattern' => '/^\d{12}$/', 'error' => 'aadhaar_number must be a 12-digit number'],
        'full_name' => ['type' => 'string', 'required' => true],
        'admission_date' => ['type' => 'date'],
        'date_of_birth' => ['type' => 'date', 'required' => true],
        'gender' => ['type' => 'enum', 'values' => ['Male', 'Female', 'Other'], 'required' => true],
        'mobile_number' => ['type' => 'string', 'pattern' => '/^\d{10}$/', 'error' => 'mobile_number must be a 10-digit number', 'required' => true],
        'age_year' => ['type' => 'integer'],
        'age_months' => ['type' => 'integer'],
        'age_days' => ['type' => 'integer'],
        'nationality' => ['type' => 'string'],
        'category' => ['type' => 'enum', 'values' => ['General', 'OBC', 'EBC', 'SC', 'ST']],
        'languages' => ['type' => 'string'],
        'id_card' => ['type' => 'string'],
        'father_name' => ['type' => 'string'],
        'mother_name' => ['type' => 'string'],
        'father_dob' => ['type' => 'date'],
        'father_nationality' => ['type' => 'string'],
        'father_qualification' => ['type' => 'string'],
        'father_occupation' => ['type' => 'string'],
        'father_designation' => ['type' => 'string'],
        'father_organization' => ['type' => 'string'],
        'father_office_address' => ['type' => 'string'],
        'father_mobile' => ['type' => 'string', 'pattern' => '/^\d{10}$/', 'error' => 'father_mobile must be a 10-digit number'],
        'mother_dob' => ['type' => 'date'],
        'mother_nationality' => ['type' => 'string'],
        'mother_qualification' => ['type' => 'string'],
        'mother_occupation' => ['type' => 'string'],
        'mother_designation' => ['type' => 'string'],
        'mother_organization' => ['type' => 'string'],
        'mother_office_address' => ['type' => 'string'],
        'mother_mobile' => ['type' => 'string', 'pattern' => '/^\d{10}$/', 'error' => 'mother_mobile must be a 10-digit number'],
        'last_exam_passed' => ['type' => 'string'],
        'stream' => ['type' => 'string'],
        'trade_sector' => ['type' => 'string'],
        'applying_class' => ['type' => 'string'],
        'class' => ['type' => 'integer'],
        'medium' => ['type' => 'enum', 'values' => ['English', 'Hindi', 'Regional']],
        'contribution' => ['type' => 'string'],
        'elaborate_choices' => ['type' => 'string'],
        'rte' => ['type' => 'string'],
        'email_address' => ['type' => 'email'],
        'additional_email' => ['type' => 'email'],
        'residential_address' => ['type' => 'string'],
        'permanent_address' => ['type' => 'string'],
        'bank_account_number' => ['type' => 'string'],
        'ifsc_code' => ['type' => 'string'],
        'registration_no' => ['type' => 'string'],
        'session' => ['type' => 'string'],
        'roll_no' => ['type' => 'string'],
        'current_school' => ['type' => 'string'],
        'additional_transport' => ['type' => 'string'],
        'transport' => ['type' => 'integer'],
        'transport_id' => ['type' => 'integer']
    ];

    foreach ($studentFields as $field => $rules) {
        $hasField = array_key_exists($field, $data);
        if (!$hasField) {
            if (!$isUpdate && !empty($rules['required'])) {
                $errors[] = "$field is required";
            }
            continue;
        }

        $value = is_string($data[$field]) ? trim($data[$field]) : $data[$field];

        if ($isUpdate && ($value === '' || $value === null)) {
            if ($field === 'transport_id') {
                $studentData[$field] = null;
                continue;
            }
            if ($field === 'transport') {
                $studentData[$field] = 0;
                continue;
            }
            if ($field === 'additional_transport') {
                $studentData[$field] = '';
                continue;
            }
        }

        if ($value === '') {
            if (!$isUpdate && !empty($rules['required'])) {
                $errors[] = "$field is required";
            }
            continue;
        }

        if ($value === '' && !empty($rules['required'])) {
            $errors[] = "$field is required";
            continue;
        }

        switch ($rules['type']) {
            case 'string':
                $studentData[$field] = (string) $value;
                break;
            case 'integer':
                if (filter_var($value, FILTER_VALIDATE_INT) === false) {
                    $errors[] = "$field must be an integer";
                } else {
                    $studentData[$field] = (int) $value;
                }
                break;
            case 'date':
                $date = DateTime::createFromFormat('Y-m-d', (string) $value);
                if (!$date || $date->format('Y-m-d') !== $value) {
                    $errors[] = "$field must be a valid date (YYYY-MM-DD)";
                } else {
                    $studentData[$field] = (string) $value;
                }
                break;
            case 'email':
                if (!filter_var($value, FILTER_VALIDATE_EMAIL)) {
                    $errors[] = "$field must be a valid email address";
                } else {
                    $studentData[$field] = (string) $value;
                }
                break;
            case 'enum':
                if (!in_array($value, $rules['values'], true)) {
                    $errors[] = "$field must be one of: " . implode(', ', $rules['values']);
                } else {
                    $studentData[$field] = (string) $value;
                }
                break;
        }

        if (isset($rules['pattern']) && is_string($value) && !preg_match($rules['pattern'], $value)) {
            $errors[] = $rules['error'];
        }
    }

    $transportRequested = array_key_exists('transport', $data) || array_key_exists('transport_id', $data);
    if ($transportRequested) {
        $resolvedTransportId = null;
        if (array_key_exists('transport_id', $studentData) && $studentData['transport_id'] !== null && $studentData['transport_id'] !== '') {
            $candidateTransportId = (int)$studentData['transport_id'];
            if ($candidateTransportId > 0) {
                $resolvedTransportId = $candidateTransportId;
            }
        }

        $resolvedTransportFlag = null;
        if (array_key_exists('transport', $studentData)) {
            $transportValue = (int)$studentData['transport'];
            if ($transportValue > 1 && $resolvedTransportId === null) {
                // Legacy payload compatibility: transport sometimes carries route id directly.
                $resolvedTransportId = $transportValue;
                $resolvedTransportFlag = 1;
            } else {
                $resolvedTransportFlag = ($transportValue === 1) ? 1 : 0;
            }
        }

        if ($resolvedTransportId !== null) {
            $resolvedTransportFlag = 1;
        }
        if ($resolvedTransportFlag === null) {
            $resolvedTransportFlag = $resolvedTransportId !== null ? 1 : 0;
        }
        if ($resolvedTransportFlag !== 1) {
            $resolvedTransportFlag = 0;
            $resolvedTransportId = null;
        }

        $studentData['transport'] = $resolvedTransportFlag;
        $studentData['transport_id'] = $resolvedTransportId;
        if ($resolvedTransportFlag === 0) {
            $studentData['additional_transport'] = '';
        }
    }

    $hasAadhaarColumn = apiTableHasColumn($pdo, 'students', 'aadhaar_number');
    if (!$hasAadhaarColumn) {
        unset($studentData['aadhaar_number']);
    }

    if ($hasAadhaarColumn && isset($studentData['aadhaar_number'])) {
        if ($isUpdate && $existingUid) {
            $stmt = $pdo->prepare('SELECT uid FROM students WHERE aadhaar_number = ? AND uid != ?');
            $stmt->execute([$studentData['aadhaar_number'], $existingUid]);
        } else {
            $stmt = $pdo->prepare('SELECT uid FROM students WHERE aadhaar_number = ?');
            $stmt->execute([$studentData['aadhaar_number']]);
        }
        if ($stmt->fetch()) {
            $errors[] = 'A student with this aadhaar_number already exists';
        }
    }

    $documentFields = [
        'aadhaar_card_upload',
        'progress_report',
        'birth_certificate',
        'slc',
        'residential_proof',
        'father_photo',
        'student_photo',
        'mother_photo'
    ];

    foreach ($documentFields as $field) {
        if (isset($data[$field]) && $data[$field] !== '') {
            $documents[$field] = (string) $data[$field];
        }
    }

    if ($errors) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Validation failed: ' . implode('; ', $errors)
        ]);
    }

    return ['studentData' => $studentData, 'documents' => $documents];
}

try {
    $action = strtolower(trim((string) ($payload['action'] ?? 'create')));
    if (!in_array($action, ['create', 'update'], true)) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Unsupported action'
        ]);
    }

    $existingStudent = null;
    if ($action === 'update') {
        $targetUid = trim((string) ($payload['uid'] ?? ''));
        $targetStudentId = (int) ($payload['student_id'] ?? 0);
        if ($targetUid === '' && $targetStudentId <= 0) {
            jsonResponse(400, [
                'success' => false,
                'message' => 'Student UID or student ID is required for update'
            ]);
        }

        if ($targetUid !== '') {
            $existingStmt = $pdo->prepare('SELECT id, uid FROM students WHERE uid = :uid LIMIT 1');
            $existingStmt->execute([':uid' => $targetUid]);
        } else {
            $existingStmt = $pdo->prepare('SELECT id, uid FROM students WHERE id = :id LIMIT 1');
            $existingStmt->execute([':id' => $targetStudentId]);
        }

        $existingStudent = $existingStmt->fetch(PDO::FETCH_ASSOC);
        if (!$existingStudent) {
            jsonResponse(404, [
                'success' => false,
                'message' => 'Student not found'
            ]);
        }
    }

    $validated = validateAdmissionInput(
        $pdo,
        $payload,
        $action === 'update',
        (string) ($existingStudent['uid'] ?? '')
    );
    $studentData = is_array($validated['studentData'] ?? null) ? $validated['studentData'] : [];
    $documents = is_array($validated['documents'] ?? null) ? $validated['documents'] : [];

    $studentColumns = apiTableColumns($pdo, 'students');
    $studentMeta = admissionTableMeta($pdo, 'students');
    $documentColumns = apiTableColumns($pdo, 'documents');
    $studentColumnMap = array_fill_keys($studentColumns, true);
    $documentColumnMap = array_fill_keys($documentColumns, true);
    $studentTransportFlagColumn = apiResolveColumn($studentColumns, ['transport', 'is_transport', 'has_transport']);
    $studentTransportIdColumn = apiResolveColumn($studentColumns, ['transport_id', 'transport_route_id']);
    $studentAdditionalTransportColumn = apiResolveColumn($studentColumns, ['additional_transport']);
    $studentTransportColumnMode = admissionTransportColumnMode($studentTransportFlagColumn, $studentTransportIdColumn, $studentMeta);
    $studentTransportUsesRouteColumn = $studentTransportColumnMode === 'route_id';

    $normalizedTransportFlag = null;
    if (array_key_exists('transport', $studentData)) {
        $normalizedTransportFlag = ((int) $studentData['transport'] === 1) ? 1 : 0;
    }

    $normalizedTransportId = null;
    if (array_key_exists('transport_id', $studentData)) {
        if ($studentData['transport_id'] !== null && $studentData['transport_id'] !== '') {
            $candidateTransportId = (int) $studentData['transport_id'];
            if ($candidateTransportId > 0) {
                $normalizedTransportId = $candidateTransportId;
            }
        }
    }

    if ($studentTransportUsesRouteColumn && $studentTransportFlagColumn !== null) {
        $hasRouteUpdate = false;
        $routeValue = null;

        if (array_key_exists('transport_id', $studentData)) {
            $hasRouteUpdate = true;
            $routeValue = $normalizedTransportId;
        } elseif ($normalizedTransportFlag === 0) {
            // Explicit disable should clear route assignment.
            $hasRouteUpdate = true;
            $routeValue = null;
        }

        if ($hasRouteUpdate) {
            $studentData[$studentTransportFlagColumn] = $routeValue;
        }
        unset($studentData['transport']);
        unset($studentData['transport_id']);
    } else {
        if (array_key_exists('transport', $studentData)) {
            if ($studentTransportFlagColumn !== null) {
                $studentData[$studentTransportFlagColumn] = $normalizedTransportFlag;
            } else {
                $studentData['transport'] = $normalizedTransportFlag;
            }
            if ($studentTransportFlagColumn !== null && strcasecmp($studentTransportFlagColumn, 'transport') !== 0) {
                unset($studentData['transport']);
            }
        }

        if (array_key_exists('transport_id', $studentData)) {
            if ($studentTransportIdColumn !== null) {
                $studentData[$studentTransportIdColumn] = $normalizedTransportId;
            } else {
                $studentData['transport_id'] = $normalizedTransportId;
            }
            if ($studentTransportIdColumn !== null && strcasecmp($studentTransportIdColumn, 'transport_id') !== 0) {
                unset($studentData['transport_id']);
            }
        }
    }
    if (array_key_exists('additional_transport', $studentData)) {
        $normalizedAdditionalTransport = (string) ($studentData['additional_transport'] ?? '');
        if ($studentAdditionalTransportColumn !== null) {
            $studentData[$studentAdditionalTransportColumn] = $normalizedAdditionalTransport;
        } else {
            $studentData['additional_transport'] = $normalizedAdditionalTransport;
        }
        if ($studentAdditionalTransportColumn !== null && strcasecmp($studentAdditionalTransportColumn, 'additional_transport') !== 0) {
            unset($studentData['additional_transport']);
        }
    }

    $studentData = array_intersect_key($studentData, $studentColumnMap);
    $documents = array_intersect_key($documents, $documentColumnMap);

    if (!isset($studentColumnMap['uid'])) {
        throw new RuntimeException('Students table is missing required column: uid');
    }
    if (!isset($studentColumnMap['full_name'])) {
        throw new RuntimeException('Students table is missing required column: full_name');
    }

    $pdo->beginTransaction();

    if ($action === 'create') {
        $uid = generateAdmissionUid($pdo);
        if (isset($studentColumnMap['status'])) {
            $studentData['status'] = 'approved';
        }
        if (isset($studentColumnMap['admission_date']) && empty($studentData['admission_date'])) {
            $studentData['admission_date'] = date('Y-m-d');
        }

        $studentFields = array_keys($studentData);
        $studentFields[] = 'uid';
        $placeholders = array_fill(0, count($studentFields), '?');
        $insertColumns = $studentFields;
        $values = array_values($studentData);
        $values[] = $uid;

        if (isset($studentColumnMap['created_at'])) {
            $insertColumns[] = 'created_at';
            $placeholders[] = 'NOW()';
        }

        $sql = 'INSERT INTO students (' . implode(',', $insertColumns) . ') VALUES (' . implode(',', $placeholders) . ')';
        $stmt = $pdo->prepare($sql);
        $stmt->execute($values);

        if (!empty($documents) && isset($documentColumnMap['uid'])) {
            if (isset($documentColumnMap['aadhaar_card_upload']) && !array_key_exists('aadhaar_card_upload', $documents)) {
                $documents['aadhaar_card_upload'] = '';
            }
            $documentFields = array_keys($documents);
            $documentFields[] = 'uid';
            $placeholders = array_fill(0, count($documentFields), '?');
            $docSql = 'INSERT INTO documents (' . implode(',', $documentFields) . ') VALUES (' . implode(',', $placeholders) . ')';
            $docStmt = $pdo->prepare($docSql);
            $docValues = array_values($documents);
            $docValues[] = $uid;
            $docStmt->execute($docValues);
        }
    } else {
        $uid = (string) ($existingStudent['uid'] ?? '');
        $studentId = (int) ($existingStudent['id'] ?? 0);
        if ($uid === '' || $studentId <= 0) {
            throw new RuntimeException('Student update target could not be resolved');
        }

        if (isset($studentColumnMap['admission_date']) && empty($studentData['admission_date']) && !empty($payload['admission_date'])) {
            $studentData['admission_date'] = (string) $payload['admission_date'];
        }

        if (!empty($studentData)) {
            $set = [];
            $params = [':id' => $studentId];
            foreach ($studentData as $field => $value) {
                $set[] = "`{$field}` = :{$field}";
                $params[":{$field}"] = $value;
            }
            if (isset($studentColumnMap['updated_at'])) {
                $set[] = 'updated_at = NOW()';
            }

            if (!empty($set)) {
                $updateSql = 'UPDATE students SET ' . implode(', ', $set) . ' WHERE id = :id';
                $updateStmt = $pdo->prepare($updateSql);
                $updateStmt->execute($params);
            }
        }

        if (isset($documentColumnMap['uid']) && !empty($documents)) {
            $documentExistsStmt = $pdo->prepare('SELECT uid FROM documents WHERE uid = :uid LIMIT 1');
            $documentExistsStmt->execute([':uid' => $uid]);
            $documentExists = (bool) $documentExistsStmt->fetch(PDO::FETCH_ASSOC);

            if ($documentExists) {
                $set = [];
                $params = [':uid' => $uid];
                foreach ($documents as $field => $value) {
                    $set[] = "`{$field}` = :{$field}";
                    $params[":{$field}"] = $value;
                }
                if (!empty($set)) {
                    $docUpdateSql = 'UPDATE documents SET ' . implode(', ', $set) . ' WHERE uid = :uid';
                    $docUpdateStmt = $pdo->prepare($docUpdateSql);
                    $docUpdateStmt->execute($params);
                }
            } else {
                if (isset($documentColumnMap['aadhaar_card_upload']) && !array_key_exists('aadhaar_card_upload', $documents)) {
                    $documents['aadhaar_card_upload'] = '';
                }
                $documentFields = array_keys($documents);
                $documentFields[] = 'uid';
                $placeholders = array_fill(0, count($documentFields), '?');
                $docSql = 'INSERT INTO documents (' . implode(',', $documentFields) . ') VALUES (' . implode(',', $placeholders) . ')';
                $docStmt = $pdo->prepare($docSql);
                $docValues = array_values($documents);
                $docValues[] = $uid;
                $docStmt->execute($docValues);
            }
        }
    }

    $pdo->commit();

    jsonResponse($action === 'create' ? 201 : 200, [
        'success' => true,
        'message' => $action === 'create'
            ? 'Student registered successfully'
            : 'Student updated successfully',
        'data' => ['uid' => $uid]
    ]);
} catch (Throwable $e) {
    if ($pdo->inTransaction()) {
        $pdo->rollBack();
    }
    error_log('api/v2/admission failed: ' . $e->getMessage());
    jsonResponse(500, [
        'success' => false,
        'message' => 'Failed to register student'
    ]);
}
