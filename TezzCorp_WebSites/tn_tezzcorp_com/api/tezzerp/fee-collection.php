<?php
declare(strict_types=1);

require_once __DIR__ . '/_common.php';
require_once __DIR__ . '/_message_events.php';

$pdo = apiDb();
apiEnsureFeeOpsSchema($pdo);
$userId = requireAuthenticatedUserId();
$user = fetchUserContext($pdo, $userId);
requireAnyPermission($user, ['collect', 'collection']);

$requestMethod = strtoupper((string) ($_SERVER['REQUEST_METHOD'] ?? 'GET'));
$input = $requestMethod === 'POST' ? $_POST : $_GET;
$rawAction = strtolower(trim((string) ($input['action'] ?? '')));
$action = $rawAction;

$legacyActionMap = [
    'search_students' => 'lookup',
    'get_dues' => 'dues',
    'get_history' => 'history',
    'collect_fee' => 'collect',
    'delete_payment' => 'delete'
];
if (isset($legacyActionMap[$action])) {
    $action = $legacyActionMap[$action];
}
$isLegacyLookup = $rawAction === 'search_students';
$isLegacyDues = $rawAction === 'get_dues';
$isLegacyHistory = $rawAction === 'get_history';
$isLegacyCollect = $rawAction === 'collect_fee';
$isLegacyDelete = $rawAction === 'delete_payment';

if ($requestMethod === 'GET') {
    if (!in_array($action, ['lookup', 'dues', 'history', 'print_receipt'], true)) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Unsupported action'
        ]);
    }
} elseif ($requestMethod === 'POST') {
    if (!in_array($action, ['collect', 'delete'], true)) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Unsupported action'
        ]);
    }
} else {
    jsonResponse(405, [
        'success' => false,
        'message' => 'Method not allowed'
    ]);
}

function feeSessionMonthOrder(): array
{
    return [0, 4, 5, 6, 7, 8, 9, 10, 11, 12, 1, 2, 3];
}

function feeMonthName(int $monthId): string
{
    if ($monthId === 0) {
        return 'Pending Dues (Previous Session)';
    }
    if ($monthId >= 1 && $monthId <= 12) {
        return (string) date('F', mktime(0, 0, 0, $monthId, 1));
    }
    return 'Unknown';
}

function feeReceiptUrl(int $paymentId): string
{
    return '/tezzerp/fee-receipt.php?payment_id=' . $paymentId;
}

function feeSessionColumn(PDO $pdo, string $table): string
{
    if (!apiTableExists($pdo, $table)) {
        return '';
    }
    if (apiColumnExists($pdo, $table, 'session')) {
        return 'session';
    }
    if (apiColumnExists($pdo, $table, 'academic_session')) {
        return 'academic_session';
    }
    return '';
}

function feeStudentSchema(PDO $pdo): array
{
    $studentColumns = apiTableColumns($pdo, 'students');
    $idCol = apiResolveColumn($studentColumns, ['id', 'student_id']);
    $uidCol = apiResolveColumn($studentColumns, ['uid', 'student_uid']);
    $nameCol = apiResolveColumn($studentColumns, ['full_name', 'name', 'student_name']);
    $fatherCol = apiResolveColumn($studentColumns, ['father_name', 'guardian_name']);
    $rollCol = apiResolveColumn($studentColumns, ['roll_no', 'roll_number']);
    $mobileCol = apiResolveColumn($studentColumns, ['mobile_number', 'mobile', 'phone']);
    $classCol = apiResolveColumn($studentColumns, ['class', 'class_id', 'current_class', 'applying_class']);
    $transportCol = apiResolveColumn($studentColumns, ['transport_id']);
    $statusCol = apiResolveColumn($studentColumns, ['status']);
    $admissionDateCol = apiResolveColumn($studentColumns, ['admission_date', 'created_at']);

    $classColumns = apiTableColumns($pdo, 'class');
    $classIdCol = apiResolveColumn($classColumns, ['id', 'class_id']);
    $classNameCol = apiResolveColumn($classColumns, ['class_name', 'name', 'title']);
    $hasClassJoin = $classCol !== null && $classColumns !== [] && $classIdCol !== null && $classNameCol !== null;

    $classJoinSql = '';
    if ($hasClassJoin) {
        $classJoinSql = ' LEFT JOIN ' . apiIdent('class') . ' c ON ('
            . apiQualifiedColumn($classIdCol, 'c') . ' = CAST(' . apiQualifiedColumn($classCol, 's') . ' AS UNSIGNED)'
            . ' OR ' . apiQualifiedColumn($classNameCol, 'c') . ' = ' . apiQualifiedColumn($classCol, 's')
            . ')';
    }

    $documentsJoinSql = '';
    $photoExpr = "''";
    if (apiTableExists($pdo, 'documents')) {
        $documentColumns = apiTableColumns($pdo, 'documents');
        $documentUidCol = apiResolveColumn($documentColumns, ['uid', 'student_uid']);
        $documentPhotoCol = apiResolveColumn($documentColumns, ['student_photo', 'photo', 'image']);
        if ($uidCol !== null && $documentUidCol !== null && $documentPhotoCol !== null) {
            $documentsJoinSql = ' LEFT JOIN ' . apiIdent('documents') . ' d ON '
                . apiQualifiedColumn($documentUidCol, 'd') . ' = ' . apiQualifiedColumn($uidCol, 's');
            $photoExpr = 'COALESCE(' . apiQualifiedColumn($documentPhotoCol, 'd') . ", '')";
        }
    }

    $classNameExpr = "'N/A'";
    $classIdExpr = "''";
    if ($classCol !== null) {
        $classNameExpr = 'COALESCE(NULLIF(' . apiQualifiedColumn($classCol, 's') . ", ''), 'N/A')";
        $classIdExpr = apiQualifiedColumn($classCol, 's');
    }
    if ($hasClassJoin) {
        $classNameExpr = 'COALESCE(NULLIF(' . apiQualifiedColumn($classNameCol, 'c') . ", ''), NULLIF(" . apiQualifiedColumn($classCol, 's') . ", ''), 'N/A')";
    }

    return [
        'student_columns' => $studentColumns,
        'id_col' => $idCol,
        'uid_col' => $uidCol,
        'name_col' => $nameCol,
        'father_col' => $fatherCol,
        'roll_col' => $rollCol,
        'mobile_col' => $mobileCol,
        'class_col' => $classCol,
        'transport_col' => $transportCol,
        'status_col' => $statusCol,
        'admission_date_col' => $admissionDateCol,
        'class_id_col' => $classIdCol,
        'class_name_col' => $classNameCol,
        'has_class_join' => $hasClassJoin,
        'class_join_sql' => $classJoinSql,
        'documents_join_sql' => $documentsJoinSql,
        'class_name_expr' => $classNameExpr,
        'class_id_expr' => $classIdExpr,
        'photo_expr' => $photoExpr
    ];
}

function resolveFeeStudent(PDO $pdo, string $studentUid = '', int $studentId = 0): array
{
    $schema = feeStudentSchema($pdo);
    if (($schema['id_col'] ?? null) === null && ($schema['uid_col'] ?? null) === null) {
        jsonResponse(500, [
            'success' => false,
            'message' => 'Students table is not configured'
        ]);
    }

    $idExpr = ($schema['id_col'] !== null)
        ? apiQualifiedColumn((string) $schema['id_col'], 's')
        : '0';
    $uidExpr = ($schema['uid_col'] !== null)
        ? 'COALESCE(NULLIF(' . apiQualifiedColumn((string) $schema['uid_col'], 's') . ", ''), '')"
        : "''";
    $nameExpr = ($schema['name_col'] !== null)
        ? "COALESCE(NULLIF(" . apiQualifiedColumn((string) $schema['name_col'], 's') . ", ''), 'Unknown Student')"
        : "'Unknown Student'";
    $fatherExpr = ($schema['father_col'] !== null)
        ? "COALESCE(NULLIF(" . apiQualifiedColumn((string) $schema['father_col'], 's') . ", ''), '-')"
        : "'-'";
    $rollExpr = ($schema['roll_col'] !== null)
        ? "COALESCE(NULLIF(" . apiQualifiedColumn((string) $schema['roll_col'], 's') . ", ''), '-')"
        : "'-'";
    $mobileExpr = ($schema['mobile_col'] !== null)
        ? "COALESCE(NULLIF(" . apiQualifiedColumn((string) $schema['mobile_col'], 's') . ", ''), '-')"
        : "'-'";
    $transportExpr = ($schema['transport_col'] !== null)
        ? apiQualifiedColumn((string) $schema['transport_col'], 's')
        : '0';

    $selectSql = 'SELECT '
        . $idExpr . ' AS ' . apiIdent('id') . ', '
        . $uidExpr . ' AS ' . apiIdent('uid') . ', '
        . $nameExpr . ' AS ' . apiIdent('full_name') . ', '
        . $fatherExpr . ' AS ' . apiIdent('father_name') . ', '
        . $rollExpr . ' AS ' . apiIdent('roll_no') . ', '
        . ($schema['class_name_expr'] ?? "'N/A'") . ' AS ' . apiIdent('class_name') . ', '
        . ($schema['class_id_expr'] ?? "''") . ' AS ' . apiIdent('class_id') . ', '
        . $mobileExpr . ' AS ' . apiIdent('mobile_number') . ', '
        . $transportExpr . ' AS ' . apiIdent('transport_id') . ', '
        . ($schema['photo_expr'] ?? "''") . ' AS ' . apiIdent('student_photo')
        . ' FROM ' . apiIdent('students') . ' s'
        . (string) ($schema['class_join_sql'] ?? '')
        . (string) ($schema['documents_join_sql'] ?? '');

    $student = false;

    if ($studentUid !== '') {
        if ($schema['uid_col'] !== null) {
            $stmt = $pdo->prepare(
                $selectSql
                . ' WHERE ' . apiQualifiedColumn((string) $schema['uid_col'], 's') . ' = :uid'
                . ' LIMIT 1'
            );
            $stmt->execute([':uid' => $studentUid]);
            $student = $stmt->fetch(PDO::FETCH_ASSOC);
        } elseif ($schema['id_col'] !== null && preg_match('/^\d+$/', $studentUid)) {
            $stmt = $pdo->prepare(
                $selectSql
                . ' WHERE ' . apiQualifiedColumn((string) $schema['id_col'], 's') . ' = :id'
                . ' LIMIT 1'
            );
            $stmt->execute([':id' => (int) $studentUid]);
            $student = $stmt->fetch(PDO::FETCH_ASSOC);
        }
    }

    if (!$student && $studentId > 0) {
        if ($schema['id_col'] !== null) {
            $stmt = $pdo->prepare(
                $selectSql
                . ' WHERE ' . apiQualifiedColumn((string) $schema['id_col'], 's') . ' = :id'
                . ' LIMIT 1'
            );
            $stmt->execute([':id' => $studentId]);
            $student = $stmt->fetch(PDO::FETCH_ASSOC);
        } elseif ($schema['uid_col'] !== null) {
            $stmt = $pdo->prepare(
                $selectSql
                . ' WHERE ' . apiQualifiedColumn((string) $schema['uid_col'], 's') . ' = :uid'
                . ' LIMIT 1'
            );
            $stmt->execute([':uid' => (string) $studentId]);
            $student = $stmt->fetch(PDO::FETCH_ASSOC);
        }
    }

    if (!$student) {
        jsonResponse(404, [
            'success' => false,
            'message' => 'Student not found'
        ]);
    }
    return $student;
}

function parsePaymentMonths(mixed $rawMonths, mixed $fallbackMonth): array
{
    if (is_array($rawMonths)) {
        $monthTokens = $rawMonths;
    } elseif (is_string($rawMonths) && trim($rawMonths) !== '') {
        $monthTokens = explode(',', $rawMonths);
    } else {
        $monthTokens = explode(',', (string) $fallbackMonth);
    }

    $paymentMonths = [];
    foreach ($monthTokens as $monthToken) {
        $monthText = trim((string) $monthToken);
        if ($monthText === '') {
            continue;
        }
        if (!preg_match('/^\d{1,2}$/', $monthText)) {
            jsonResponse(400, [
                'success' => false,
                'message' => 'Payment months must be numeric values between 0 and 12'
            ]);
        }
        $monthValue = (int) $monthText;
        if ($monthValue < 0 || $monthValue > 12) {
            jsonResponse(400, [
                'success' => false,
                'message' => 'Payment months must be between 0 and 12'
            ]);
        }
        $paymentMonths[] = (string) $monthValue;
    }

    if (empty($paymentMonths)) {
        $paymentMonths = [date('n')];
    }

    return array_values(array_unique($paymentMonths));
}

function ensureFeeCollectionItemsTable(PDO $pdo): void
{
    if (apiTableExists($pdo, 'fee_collection_items')) {
        return;
    }

    $pdo->exec(
        "CREATE TABLE IF NOT EXISTS fee_collection_items (
            id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
            payment_id INT UNSIGNED NOT NULL,
            student_id INT UNSIGNED NOT NULL,
            student_uid VARCHAR(60) NOT NULL,
            session VARCHAR(20) NOT NULL,
            month_id TINYINT UNSIGNED NOT NULL DEFAULT 0,
            month_name VARCHAR(60) NOT NULL,
            fee_total DECIMAL(12,2) NOT NULL DEFAULT 0,
            paid_before DECIMAL(12,2) NOT NULL DEFAULT 0,
            due_before DECIMAL(12,2) NOT NULL DEFAULT 0,
            collected_amount DECIMAL(12,2) NOT NULL DEFAULT 0,
            discount_amount DECIMAL(12,2) NOT NULL DEFAULT 0,
            status_before VARCHAR(30) NOT NULL DEFAULT 'unknown',
            fee_components_json LONGTEXT NULL,
            created_on TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            INDEX idx_fee_collection_items_payment_id (payment_id),
            INDEX idx_fee_collection_items_student_id (student_id),
            INDEX idx_fee_collection_items_session (session)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"
    );
}

function fetchFeeDues(PDO $pdo, array $student, string $session): array
{
    $studentId = (int) ($student['id'] ?? 0);
    $studentUid = (string) ($student['uid'] ?? '');
    $classId = apiResolveClassId($pdo, $student['class_id'] ?? '');
    $transportId = (int) ($student['transport_id'] ?? 0);
    $sessionVariants = apiAcademicSessionVariants($session);
    if (!$sessionVariants) {
        $sessionVariants = [$session];
    }

    $pendingRows = [];
    if (apiTableExists($pdo, 'pending_fees')) {
        $pendingStmt = $pdo->prepare(
            "SELECT id, pending_amount, status, collection_id
             FROM pending_fees
             WHERE student_uid = :uid"
        );
        $pendingStmt->execute([':uid' => $studentUid]);
        $pendingRows = $pendingStmt->fetchAll(PDO::FETCH_ASSOC);
    }

    $totalPendingDue = 0.0;
    $isPendingCleared = true;
    $pendingCollectionIds = [];

    foreach ($pendingRows as $row) {
        if ((string) ($row['status'] ?? '') === 'pending') {
            $totalPendingDue += (float) ($row['pending_amount'] ?? 0);
            $isPendingCleared = false;
        } else {
            $collectionId = (int) ($row['collection_id'] ?? 0);
            if ($collectionId > 0) {
                $pendingCollectionIds[] = $collectionId;
            }
        }
    }

    $structureSessionColumn = feeSessionColumn($pdo, 'fee_structure');
    $structureSessionParts = [];
    $structureSessionParams = [];
    if ($structureSessionColumn !== '') {
        foreach ($sessionVariants as $index => $sessionVariant) {
            $param = ':structure_session_' . $index;
            $structureSessionParts[] = $structureSessionColumn . ' = ' . $param;
            $structureSessionParams[$param] = $sessionVariant;
        }
    }
    $structureStatusWhere = apiColumnExists($pdo, 'fee_structure', 'status')
        ? "AND COALESCE(NULLIF(status, ''), 'active') IN ('active', 'Active', '1')"
        : '';
    $structureSessionWhere = $structureSessionParts
        ? 'AND (' . implode(' OR ', $structureSessionParts) . ')'
        : '';

    $feeStructures = [];
    if (apiTableExists($pdo, 'fee_structure')) {
        $structureStmt = $pdo->prepare(
            "SELECT particular_id, amount, frequency, custom_frequency, applicable_months
             FROM fee_structure
             WHERE class_id = :class_id
               {$structureSessionWhere}
               {$structureStatusWhere}"
        );
        $structureStmt->execute(array_merge([
            ':class_id' => $classId
        ], $structureSessionParams));
        $feeStructures = $structureStmt->fetchAll(PDO::FETCH_ASSOC);
    }

    $specialRows = [];
    if (apiTableExists($pdo, 'special_structure')) {
        $specialStatusWhere = apiColumnExists($pdo, 'special_structure', 'status')
            ? "AND status = 'active'"
            : '';
        $specialStmt = $pdo->prepare(
            "SELECT particular_id, fee_type, special_amount, discount_percentage
             FROM special_structure
             WHERE student_uid = :uid
               {$specialStatusWhere}"
        );
        $specialStmt->execute([':uid' => $studentUid]);
        $specialRows = $specialStmt->fetchAll(PDO::FETCH_ASSOC);
    }

    $specialAcademicMap = [];
    $specialTransport = null;
    foreach ($specialRows as $specialRow) {
        if ((string) ($specialRow['fee_type'] ?? '') === 'transport') {
            $specialTransport = $specialRow;
            continue;
        }
        $specialAcademicMap[(string) ($specialRow['particular_id'] ?? '')] = $specialRow;
    }

    $transportMonthlyRate = 0.0;
    if ($transportId > 0 && apiTableExists($pdo, 'transport')) {
        $transportSessionColumn = feeSessionColumn($pdo, 'transport');
        $transportSessionParts = [];
        $transportSessionParams = [];
        if ($transportSessionColumn !== '') {
            foreach ($sessionVariants as $index => $sessionVariant) {
                $param = ':transport_session_' . $index;
                $transportSessionParts[] = $transportSessionColumn . ' = ' . $param;
                $transportSessionParams[$param] = $sessionVariant;
            }
        }
        $transportSessionWhere = $transportSessionParts
            ? 'AND (' . implode(' OR ', $transportSessionParts) . ')'
            : '';

        $transportStmt = $pdo->prepare(
            "SELECT transport_fees
             FROM transport
             WHERE id = :id
               {$transportSessionWhere}
             LIMIT 1"
        );
        $transportStmt->execute(array_merge([
            ':id' => $transportId
        ], $transportSessionParams));
        $transportMonthlyRate = (float) ($transportStmt->fetchColumn() ?: 0);

        if (is_array($specialTransport)) {
            $specialAmount = (float) ($specialTransport['special_amount'] ?? 0);
            if ($specialAmount > 0) {
                $transportMonthlyRate = max(0, $specialAmount);
            }
        }
    }

    $monthlyDuesMap = [];
    $monthlyComponentsMap = [];
    foreach (feeSessionMonthOrder() as $monthId) {
        if ($monthId !== 0) {
            $monthlyDuesMap[$monthId] = 0.0;
            $monthlyComponentsMap[$monthId] = [];
        }
    }

    $particularLabels = [];
    if ($feeStructures && apiTableExists($pdo, 'particulars')) {
        $particularIds = [];
        foreach ($feeStructures as $fee) {
            $particularId = (int) ($fee['particular_id'] ?? 0);
            if ($particularId > 0) {
                $particularIds[$particularId] = true;
            }
        }

        if ($particularIds) {
            $placeholders = [];
            $labelParams = [];
            $index = 0;
            foreach (array_keys($particularIds) as $particularId) {
                $key = ':particular_id_' . $index++;
                $placeholders[] = $key;
                $labelParams[$key] = $particularId;
            }
            $particularStmt = $pdo->prepare(
                'SELECT id, COALESCE(NULLIF(particular, \'\'), CONCAT(\'Particular \', id)) AS label
                 FROM particulars
                 WHERE id IN (' . implode(', ', $placeholders) . ')'
            );
            foreach ($labelParams as $key => $value) {
                $particularStmt->bindValue($key, $value, PDO::PARAM_INT);
            }
            $particularStmt->execute();
            $rows = $particularStmt->fetchAll(PDO::FETCH_ASSOC);
            foreach ($rows as $row) {
                $particularLabels[(string) ($row['id'] ?? '')] = (string) ($row['label'] ?? '');
            }
        }
    }

    $appendComponent = static function (array &$components, int $monthId, string $label, float $amount): void {
        if (!isset($components[$monthId]) || $amount <= 0) {
            return;
        }
        $normalizedLabel = trim($label);
        if ($normalizedLabel === '') {
            $normalizedLabel = 'Fee';
        }
        if (!isset($components[$monthId][$normalizedLabel])) {
            $components[$monthId][$normalizedLabel] = 0.0;
        }
        $components[$monthId][$normalizedLabel] += $amount;
    };

    foreach ($feeStructures as $fee) {
        $particularId = (string) ($fee['particular_id'] ?? '');
        $amount = (float) ($fee['amount'] ?? 0);
        $componentLabel = $particularLabels[$particularId] ?? ('Particular ' . ($particularId !== '' ? $particularId : ''));

        if (isset($specialAcademicMap[$particularId])) {
            $special = $specialAcademicMap[$particularId];
            $specialAmount = (float) ($special['special_amount'] ?? 0);
            if ($specialAmount > 0) {
                $amount = max(0, $specialAmount);
            }
        }

        $frequency = (string) ($fee['frequency'] ?? 'monthly');
        $applicableMonthsRaw = trim((string) ($fee['applicable_months'] ?? 'all'));

        if ($frequency === 'monthly') {
            $monthsToApply = $applicableMonthsRaw === 'all'
                ? [4, 5, 6, 7, 8, 9, 10, 11, 12, 1, 2, 3]
                : array_map('intval', array_filter(array_map('trim', explode(',', $applicableMonthsRaw)), static fn($value) => $value !== ''));
            foreach ($monthsToApply as $monthId) {
                if (isset($monthlyDuesMap[$monthId])) {
                    $monthlyDuesMap[$monthId] += $amount;
                    $appendComponent($monthlyComponentsMap, $monthId, $componentLabel, $amount);
                }
            }
            continue;
        }

        if ($frequency === 'annual') {
            $monthId = (int) $applicableMonthsRaw;
            if (isset($monthlyDuesMap[$monthId])) {
                $monthlyDuesMap[$monthId] += $amount;
                $appendComponent($monthlyComponentsMap, $monthId, $componentLabel, $amount);
            }
            continue;
        }

        if ($frequency === 'other') {
            if ($applicableMonthsRaw !== '' && $applicableMonthsRaw !== 'all') {
                $monthsToApply = array_map('intval', array_filter(array_map('trim', explode(',', $applicableMonthsRaw)), static fn($value) => $value !== ''));
                foreach ($monthsToApply as $monthId) {
                    if (isset($monthlyDuesMap[$monthId])) {
                        $monthlyDuesMap[$monthId] += $amount;
                        $appendComponent($monthlyComponentsMap, $monthId, $componentLabel, $amount);
                    }
                }
            } elseif (isset($monthlyDuesMap[4])) {
                $monthlyDuesMap[4] += $amount;
                $appendComponent($monthlyComponentsMap, 4, $componentLabel, $amount);
            }
        }
    }

    foreach ($monthlyDuesMap as $monthId => $value) {
        $monthlyDuesMap[$monthId] = $value + $transportMonthlyRate;
        if ($transportMonthlyRate > 0) {
            $appendComponent($monthlyComponentsMap, $monthId, 'Transport', $transportMonthlyRate);
        }
    }

    $payments = [];
    if (apiTableExists($pdo, 'fee_collections')) {
        $paymentSessionColumn = feeSessionColumn($pdo, 'fee_collections');
        $paymentSessionParts = [];
        $paymentSessionParams = [];
        if ($paymentSessionColumn !== '') {
            foreach ($sessionVariants as $index => $sessionVariant) {
                $param = ':payment_session_' . $index;
                $paymentSessionParts[] = $paymentSessionColumn . ' = ' . $param;
                $paymentSessionParams[$param] = $sessionVariant;
            }
        }
        $paymentSessionWhere = $paymentSessionParts
            ? 'AND (' . implode(' OR ', $paymentSessionParts) . ')'
            : '';

        $paymentsStmt = $pdo->prepare(
            "SELECT id, amount_collected, discount, payment_month
             FROM fee_collections
             WHERE student_id = :student_id
               {$paymentSessionWhere}"
        );
        $paymentsStmt->execute(array_merge([
            ':student_id' => $studentId
        ], $paymentSessionParams));
        $payments = $paymentsStmt->fetchAll(PDO::FETCH_ASSOC);
    }

    $totalPaidForMonths = 0.0;
    $totalPaidForPending = 0.0;

    foreach ($payments as $payment) {
        $paymentValue = (float) ($payment['amount_collected'] ?? 0) + (float) ($payment['discount'] ?? 0);
        $paymentMonths = array_map('trim', explode(',', (string) ($payment['payment_month'] ?? '')));
        $paymentMonths = array_values(array_filter($paymentMonths, static fn($value) => $value !== ''));
        $coversPending = in_array('0', $paymentMonths, true);
        $paymentId = (int) ($payment['id'] ?? 0);

        if ($coversPending || in_array($paymentId, $pendingCollectionIds, true)) {
            $totalPaidForPending += $paymentValue;
            continue;
        }
        $totalPaidForMonths += $paymentValue;
    }

    $rows = [];
    $cumulativeMonthlyFee = 0.0;

    if ($totalPendingDue > 0 || !$isPendingCleared) {
        $rows[] = [
            'month_id' => 0,
            'month_name' => feeMonthName(0),
            'total_fee' => round($totalPendingDue, 2),
            'paid_amount' => 0.0,
            'due_amount' => round($totalPendingDue, 2),
            'status' => $totalPendingDue > 0.01 ? 'unpaid' : 'fully_paid',
            'fee_components' => $totalPendingDue > 0
                ? [['label' => 'Pending Dues', 'amount' => round($totalPendingDue, 2)]]
                : []
        ];
    }

    foreach (feeSessionMonthOrder() as $monthId) {
        if ($monthId === 0) {
            continue;
        }

        $monthFee = (float) ($monthlyDuesMap[$monthId] ?? 0.0);
        $cumulativeMonthlyFee += $monthFee;

        $previousCost = $cumulativeMonthlyFee - $monthFee;
        $paidForThisMonth = max(0.0, min($monthFee, $totalPaidForMonths - $previousCost));
        $dueAmount = $monthFee - $paidForThisMonth;

        $status = 'unpaid';
        if ($dueAmount <= 0.01) {
            $status = 'fully_paid';
            $dueAmount = 0.0;
        } elseif ($paidForThisMonth > 0) {
            $status = 'partial';
        }

        $rows[] = [
            'month_id' => $monthId,
            'month_name' => feeMonthName($monthId),
            'total_fee' => round($monthFee, 2),
            'paid_amount' => round($paidForThisMonth, 2),
            'due_amount' => round($dueAmount, 2),
            'status' => $status,
            'fee_components' => array_values(array_map(
                static fn (string $label, float $amount): array => [
                    'label' => $label,
                    'amount' => round($amount, 2)
                ],
                array_keys($monthlyComponentsMap[$monthId] ?? []),
                array_values($monthlyComponentsMap[$monthId] ?? [])
            ))
        ];
    }

    return [
        'items' => $rows,
        'summary' => [
            'total_fee' => round($cumulativeMonthlyFee + $totalPendingDue, 2),
            'total_paid' => round($totalPaidForMonths + $totalPaidForPending, 2),
            'total_outstanding' => round(max(0, ($cumulativeMonthlyFee - $totalPaidForMonths) + $totalPendingDue), 2)
        ]
    ];
}

if ($requestMethod === 'GET' && $action === 'lookup') {
    $search = trim((string) ($_GET['q'] ?? ''));
    $classId = (int) ($_GET['class_id'] ?? 0);
    $limit = queryIntParam('limit', 20, 1, 100);
    $schema = feeStudentSchema($pdo);

    if (($schema['id_col'] ?? null) === null && ($schema['uid_col'] ?? null) === null) {
        if ($isLegacyLookup) {
            jsonResponse(200, [
                'success' => true,
                'status' => 'success',
                'message' => 'Students fetched for fee lookup',
                'data' => []
            ]);
        }
        jsonResponse(200, [
            'success' => true,
            'status' => 'success',
            'message' => 'Students fetched for fee lookup',
            'data' => [
                'items' => []
            ]
        ]);
    }

    $where = ['1=1'];
    if (($schema['status_col'] ?? null) !== null) {
        $where[] = "COALESCE(NULLIF(" . apiQualifiedColumn((string) $schema['status_col'], 's') . ", ''), 'active') != 'deleted'";
    }
    $params = [];

    if ($search !== '') {
        $searchParts = [];
        if (($schema['id_col'] ?? null) !== null) {
            $searchParts[] = "CAST(" . apiQualifiedColumn((string) $schema['id_col'], 's') . " AS CHAR) LIKE :q";
        }
        if (($schema['uid_col'] ?? null) !== null) {
            $searchParts[] = "COALESCE(" . apiQualifiedColumn((string) $schema['uid_col'], 's') . ", '') LIKE :q";
        }
        if (($schema['name_col'] ?? null) !== null) {
            $searchParts[] = "COALESCE(" . apiQualifiedColumn((string) $schema['name_col'], 's') . ", '') LIKE :q";
        }
        if (($schema['father_col'] ?? null) !== null) {
            $searchParts[] = "COALESCE(" . apiQualifiedColumn((string) $schema['father_col'], 's') . ", '') LIKE :q";
        }
        if (($schema['class_col'] ?? null) !== null) {
            $searchParts[] = "COALESCE(" . apiQualifiedColumn((string) $schema['class_col'], 's') . ", '') LIKE :q";
        }
        if (($schema['has_class_join'] ?? false) && ($schema['class_name_col'] ?? null) !== null) {
            $searchParts[] = "COALESCE(" . apiQualifiedColumn((string) $schema['class_name_col'], 'c') . ", '') LIKE :q";
        }
        if (($schema['mobile_col'] ?? null) !== null) {
            $searchParts[] = "COALESCE(" . apiQualifiedColumn((string) $schema['mobile_col'], 's') . ", '') LIKE :q";
        }
        if (($schema['roll_col'] ?? null) !== null) {
            $searchParts[] = "COALESCE(" . apiQualifiedColumn((string) $schema['roll_col'], 's') . ", '') LIKE :q";
        }
        if ($searchParts !== []) {
            $where[] = '(' . implode(' OR ', $searchParts) . ')';
            $params[':q'] = '%' . $search . '%';
        }
    }

    if ($classId > 0 && ($schema['class_col'] ?? null) !== null) {
        if (($schema['has_class_join'] ?? false) && ($schema['class_id_col'] ?? null) !== null) {
            $where[] = "("
                . apiQualifiedColumn((string) $schema['class_id_col'], 'c') . " = :class_id"
                . " OR CAST(" . apiQualifiedColumn((string) $schema['class_col'], 's') . " AS UNSIGNED) = :class_id"
                . ")";
        } else {
            $where[] = "CAST(" . apiQualifiedColumn((string) $schema['class_col'], 's') . " AS UNSIGNED) = :class_id";
        }
        $params[':class_id'] = $classId;
    }

    try {
        $idExpr = ($schema['id_col'] !== null)
            ? apiQualifiedColumn((string) $schema['id_col'], 's')
            : '0';
        $uidExpr = ($schema['uid_col'] !== null)
            ? 'COALESCE(NULLIF(' . apiQualifiedColumn((string) $schema['uid_col'], 's') . ", ''), '')"
            : "''";
        $nameExpr = ($schema['name_col'] !== null)
            ? "COALESCE(NULLIF(" . apiQualifiedColumn((string) $schema['name_col'], 's') . ", ''), 'Unknown Student')"
            : "'Unknown Student'";
        $fatherExpr = ($schema['father_col'] !== null)
            ? "COALESCE(NULLIF(" . apiQualifiedColumn((string) $schema['father_col'], 's') . ", ''), '-')"
            : "'-'";
        $rollExpr = ($schema['roll_col'] !== null)
            ? "COALESCE(NULLIF(" . apiQualifiedColumn((string) $schema['roll_col'], 's') . ", ''), '-')"
            : "'-'";
        $mobileExpr = ($schema['mobile_col'] !== null)
            ? "COALESCE(NULLIF(" . apiQualifiedColumn((string) $schema['mobile_col'], 's') . ", ''), '-')"
            : "'-'";
        $transportExpr = ($schema['transport_col'] !== null)
            ? apiQualifiedColumn((string) $schema['transport_col'], 's')
            : '0';
        $statusExpr = ($schema['status_col'] !== null)
            ? "COALESCE(NULLIF(" . apiQualifiedColumn((string) $schema['status_col'], 's') . ", ''), 'active')"
            : "'active'";
        $admissionDateExpr = ($schema['admission_date_col'] !== null)
            ? "COALESCE(CAST(" . apiQualifiedColumn((string) $schema['admission_date_col'], 's') . " AS CHAR), '')"
            : "''";
        $classNameExpr = (string) ($schema['class_name_expr'] ?? "'N/A'");
        $photoExpr = (string) ($schema['photo_expr'] ?? "''");
        $orderExpr = ($schema['id_col'] !== null)
            ? apiQualifiedColumn((string) $schema['id_col'], 's')
            : (($schema['uid_col'] !== null) ? apiQualifiedColumn((string) $schema['uid_col'], 's') : '1');

        $sql = "SELECT {$idExpr} AS id,
                       {$uidExpr} AS uid,
                       {$nameExpr} AS full_name,
                       {$fatherExpr} AS father_name,
                       {$rollExpr} AS roll_no,
                       {$classNameExpr} AS class_name,
                       {$mobileExpr} AS mobile_number,
                       {$transportExpr} AS transport_id,
                       {$photoExpr} AS student_photo,
                       {$statusExpr} AS status,
                       {$admissionDateExpr} AS admission_date
                FROM " . apiIdent('students') . " s"
                . (string) ($schema['class_join_sql'] ?? '')
                . (string) ($schema['documents_join_sql'] ?? '') . "
                WHERE " . implode(' AND ', $where) . "
                ORDER BY {$orderExpr} DESC
                LIMIT :limit";
        $stmt = $pdo->prepare($sql);
        foreach ($params as $name => $value) {
            $type = $name === ':class_id' ? PDO::PARAM_INT : PDO::PARAM_STR;
            $stmt->bindValue($name, $value, $type);
        }
        $stmt->bindValue(':limit', $limit, PDO::PARAM_INT);
        $stmt->execute();
        $items = $stmt->fetchAll(PDO::FETCH_ASSOC);

        if ($isLegacyLookup) {
            jsonResponse(200, [
                'success' => true,
                'status' => 'success',
                'message' => 'Students fetched for fee lookup',
                'data' => $items
            ]);
        }

        jsonResponse(200, [
            'success' => true,
            'status' => 'success',
            'message' => 'Students fetched for fee lookup',
            'data' => [
                'items' => $items
            ]
        ]);
    } catch (Throwable $e) {
        error_log('api/v2/fee-collection lookup failed: ' . $e->getMessage());
        jsonResponse(500, [
            'success' => false,
            'message' => 'Failed to search students for fee collection'
        ]);
    }
}

if ($requestMethod === 'GET' && $action === 'dues') {
    $studentUid = trim((string) ($_GET['student_uid'] ?? ''));
    $studentId = (int) ($_GET['student_id'] ?? 0);
    $session = trim((string) ($_GET['session'] ?? ''));

    if ($studentUid === '' && $studentId <= 0) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Student UID or student ID is required'
        ]);
    }
    if ($session === '' || strlen($session) > 20) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Session is required'
        ]);
    }

    try {
        $student = resolveFeeStudent($pdo, $studentUid, $studentId);
        $dues = fetchFeeDues($pdo, $student, $session);

        if ($isLegacyDues) {
            jsonResponse(200, [
                'success' => true,
                'status' => 'success',
                'message' => 'Fee dues loaded',
                'data' => $dues['items'],
                'summary' => $dues['summary'],
                'student' => $student,
                'session' => $session
            ]);
        }

        jsonResponse(200, [
            'success' => true,
            'status' => 'success',
            'message' => 'Fee dues loaded',
            'data' => [
                'student' => $student,
                'session' => $session,
                'items' => $dues['items'],
                'summary' => $dues['summary']
            ]
        ]);
    } catch (Throwable $e) {
        error_log('api/v2/fee-collection dues failed: ' . $e->getMessage());
        jsonResponse(500, [
            'success' => false,
            'message' => 'Failed to load fee dues'
        ]);
    }
}

if ($requestMethod === 'GET' && $action === 'history') {
    $studentUid = trim((string) ($_GET['student_uid'] ?? ''));
    $studentId = (int) ($_GET['student_id'] ?? 0);
    $session = trim((string) ($_GET['session'] ?? ''));

    if ($studentUid === '' && $studentId <= 0) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Student UID or student ID is required'
        ]);
    }
    if ($session === '' || strlen($session) > 20) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Session is required'
        ]);
    }

    try {
        $student = resolveFeeStudent($pdo, $studentUid, $studentId);
        if (!apiTableExists($pdo, 'fee_collections')) {
            if ($isLegacyHistory) {
                jsonResponse(200, [
                    'success' => true,
                    'status' => 'success',
                    'message' => 'Fee collection history loaded',
                    'data' => []
                ]);
            }
            jsonResponse(200, [
                'success' => true,
                'status' => 'success',
                'message' => 'Fee collection history loaded',
                'data' => [
                    'student' => $student,
                    'session' => $session,
                    'items' => []
                ]
            ]);
        }
        $historyParams = [
            ':student_id' => (int) $student['id']
        ];
        $historySessionWhere = '';
        $historySessionColumn = feeSessionColumn($pdo, 'fee_collections');
        if ($historySessionColumn !== '') {
            $sessionVariants = apiAcademicSessionVariants($session);
            if (!$sessionVariants) {
                $sessionVariants = [$session];
            }
            $historySessionParts = [];
            foreach ($sessionVariants as $index => $sessionVariant) {
                $key = ':history_session_' . $index;
                $historySessionParts[] = $historySessionColumn . ' = ' . $key;
                $historyParams[$key] = $sessionVariant;
            }
            $historySessionWhere = 'AND (' . implode(' OR ', $historySessionParts) . ')';
        }

        $historyStmt = $pdo->prepare(
            "SELECT id,
                    amount_collected,
                    discount,
                    payment_month,
                    payment_date,
                    payment_method,
                    remarks,
                    status
             FROM fee_collections
             WHERE student_id = :student_id
               {$historySessionWhere}
             ORDER BY id DESC"
        );
        $historyStmt->execute($historyParams);
        $rows = $historyStmt->fetchAll(PDO::FETCH_ASSOC);

        $items = array_map(static function (array $row): array {
            $monthTokens = array_values(array_filter(array_map('trim', explode(',', (string) ($row['payment_month'] ?? ''))), static fn($value) => $value !== ''));
            $monthLabels = array_map(static function (string $monthToken): string {
                return feeMonthName((int) $monthToken);
            }, $monthTokens);

            return [
                'id' => (int) ($row['id'] ?? 0),
                'amount_collected' => (float) ($row['amount_collected'] ?? 0),
                'discount' => (float) ($row['discount'] ?? 0),
                'payment_month' => (string) ($row['payment_month'] ?? ''),
                'months_covered' => implode(', ', $monthLabels),
                'payment_date' => (string) ($row['payment_date'] ?? ''),
                'payment_method' => (string) ($row['payment_method'] ?? 'unknown'),
                'remarks' => (string) ($row['remarks'] ?? ''),
                'status' => (string) ($row['status'] ?? 'unknown'),
                'remaining_balance' => 0,
                'print_url' => feeReceiptUrl((int) ($row['id'] ?? 0))
            ];
        }, $rows);

        if ($isLegacyHistory) {
            jsonResponse(200, [
                'success' => true,
                'status' => 'success',
                'message' => 'Fee collection history loaded',
                'data' => $items
            ]);
        }

        jsonResponse(200, [
            'success' => true,
            'status' => 'success',
            'message' => 'Fee collection history loaded',
            'data' => [
                'student' => $student,
                'session' => $session,
                'items' => $items
            ]
        ]);
    } catch (Throwable $e) {
        error_log('api/v2/fee-collection history failed: ' . $e->getMessage());
        jsonResponse(500, [
            'success' => false,
            'message' => 'Failed to load fee history'
        ]);
    }
}

if ($requestMethod === 'GET' && $action === 'print_receipt') {
    $paymentId = (int) ($_GET['payment_id'] ?? 0);
    if ($paymentId <= 0) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Payment ID is required'
        ]);
    }

    $printUrl = feeReceiptUrl($paymentId);
    if ($rawAction === 'print_receipt') {
        header('Location: ' . $printUrl, true, 302);
        exit;
    }

    jsonResponse(200, [
        'success' => true,
        'status' => 'success',
        'message' => 'Receipt URL generated',
        'data' => [
            'payment_id' => $paymentId,
            'print_url' => $printUrl
        ]
    ]);
}

if ($requestMethod === 'POST' && $action === 'collect') {
    $studentUid = trim((string) ($_POST['student_uid'] ?? ''));
    $studentId = (int) ($_POST['student_id'] ?? 0);
    $session = trim((string) ($_POST['session'] ?? ''));
    $amountCollected = (float) ($_POST['amount_collected'] ?? 0);
    $discount = (float) ($_POST['discount'] ?? 0);
    $paymentDate = trim((string) ($_POST['payment_date'] ?? date('Y-m-d')));
    $paymentMethod = strtolower(trim((string) ($_POST['payment_method'] ?? 'cash')));
    $status = strtolower(trim((string) ($_POST['status'] ?? 'paid')));
    $referenceNo = trim((string) ($_POST['reference_no'] ?? ($_POST['ref_no'] ?? '')));
    $remarks = trim((string) ($_POST['remarks'] ?? ''));
    $paymentMonths = parsePaymentMonths($_POST['payment_months'] ?? ($_POST['months'] ?? null), $_POST['payment_month'] ?? date('n'));

    if ($studentUid === '' && $studentId <= 0) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Student UID or student ID is required'
        ]);
    }
    if ($amountCollected <= 0) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Collected amount must be greater than zero'
        ]);
    }
    if ($discount < 0) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Discount cannot be negative'
        ]);
    }
    if ($session === '' || strlen($session) > 20) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Session is required'
        ]);
    }
    if (!preg_match('/^\d{4}-\d{2}-\d{2}$/', $paymentDate)) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Payment date must be in YYYY-MM-DD format'
        ]);
    }

    $allowedMethods = ['cash', 'card', 'online', 'cheque', 'dd', 'bank_transfer'];
    if (!in_array($paymentMethod, $allowedMethods, true)) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Invalid payment method'
        ]);
    }
    if (in_array($paymentMethod, ['online', 'cheque', 'dd'], true) && $referenceNo === '') {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Reference number is required for selected payment method'
        ]);
    }

    $allowedStatuses = ['paid', 'success', 'completed', 'pending', 'failed', 'cancelled'];
    if (!in_array($status, $allowedStatuses, true)) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Invalid payment status'
        ]);
    }

    try {
        $student = resolveFeeStudent($pdo, $studentUid, $studentId);
        if (!apiTableExists($pdo, 'fee_collections')) {
            jsonResponse(500, [
                'success' => false,
                'message' => 'Fee collection table is not configured'
            ]);
        }
        $monthCsv = implode(',', $paymentMonths);

        $duesSnapshot = fetchFeeDues($pdo, $student, $session);
        $duesByMonth = [];
        foreach (($duesSnapshot['items'] ?? []) as $dueItem) {
            $key = (string) ($dueItem['month_id'] ?? '');
            if ($key === '') {
                continue;
            }
            $duesByMonth[$key] = $dueItem;
        }

        $collectionItems = [];
        $cashPool = max(0.0, $amountCollected);
        $discountPool = max(0.0, $discount);
        foreach ($paymentMonths as $monthToken) {
            $monthKey = (string) $monthToken;
            $dueRow = $duesByMonth[$monthKey] ?? null;
            $monthName = feeMonthName((int) $monthToken);

            $feeTotal = (float) ($dueRow['total_fee'] ?? 0);
            $paidBefore = (float) ($dueRow['paid_amount'] ?? 0);
            $dueBefore = max(0.0, (float) ($dueRow['due_amount'] ?? 0));
            $statusBefore = (string) ($dueRow['status'] ?? 'unknown');
            $components = is_array($dueRow['fee_components'] ?? null) ? $dueRow['fee_components'] : [];

            $allocatable = min($dueBefore, $cashPool + $discountPool);
            if ($allocatable <= 0 && ($cashPool + $discountPool) > 0.01) {
                $allocatable = min($cashPool + $discountPool, $dueBefore > 0 ? $dueBefore : ($cashPool + $discountPool));
            }
            if ($allocatable <= 0) {
                continue;
            }

            $discountAllocated = min($discountPool, $allocatable);
            $discountPool -= $discountAllocated;

            $cashAllocated = min($cashPool, $allocatable - $discountAllocated);
            $cashPool -= $cashAllocated;

            $collectionItems[] = [
                'month_id' => (int) $monthToken,
                'month_name' => $monthName,
                'fee_total' => round($feeTotal, 2),
                'paid_before' => round($paidBefore, 2),
                'due_before' => round($dueBefore, 2),
                'collected_amount' => round($cashAllocated, 2),
                'discount_amount' => round($discountAllocated, 2),
                'status_before' => $statusBefore,
                'fee_components' => $components
            ];
        }

        $remaining = round($cashPool + $discountPool, 2);
        if ($remaining > 0) {
            $discountAllocated = min($discountPool, $remaining);
            $cashAllocated = max(0, $remaining - $discountAllocated);
            $collectionItems[] = [
                'month_id' => 0,
                'month_name' => 'Advance / Adjustment',
                'fee_total' => 0.0,
                'paid_before' => 0.0,
                'due_before' => 0.0,
                'collected_amount' => round($cashAllocated, 2),
                'discount_amount' => round($discountAllocated, 2),
                'status_before' => 'advance',
                'fee_components' => []
            ];
        }

        $collectorName = trim((string) ($user['name'] ?? 'System'));
        if ($collectorName === '') {
            $collectorName = 'System';
        }

        $finalRemarks = $remarks;
        if ($referenceNo !== '') {
            $finalRemarks = trim(('Ref: ' . $referenceNo . ($finalRemarks !== '' ? ' | ' . $finalRemarks : '')));
        }

        $pdo->beginTransaction();
        $collectionSessionColumn = feeSessionColumn($pdo, 'fee_collections');
        $insertColumns = [
            'student_id',
            'amount_collected',
            'discount',
            'remarks',
            'payment_month',
            'payment_date',
            'payment_method',
            'collected_by',
            'status',
            'fee_type'
        ];
        $insertParams = [
            ':student_id' => (int) $student['id'],
            ':amount_collected' => $amountCollected,
            ':discount' => $discount,
            ':remarks' => $finalRemarks,
            ':payment_month' => $monthCsv,
            ':payment_date' => $paymentDate,
            ':payment_method' => $paymentMethod,
            ':collected_by' => $collectorName,
            ':status' => $status,
            ':fee_type' => 'monthly'
        ];
        if ($collectionSessionColumn !== '') {
            $insertColumns[] = $collectionSessionColumn;
            $insertParams[':session'] = $session;
        }
        $insertStmt = $pdo->prepare(
            'INSERT INTO fee_collections (' . implode(', ', $insertColumns) . ')
             VALUES (' . implode(', ', array_keys($insertParams)) . ')'
        );
        $insertStmt->execute($insertParams);

        $paymentId = (int) $pdo->lastInsertId();

        if (in_array('0', $paymentMonths, true) && apiTableExists($pdo, 'pending_fees')) {
            $pendingStmt = $pdo->prepare(
                "UPDATE pending_fees
                 SET status = 'paid',
                     collection_id = :collection_id,
                     updated_on = NOW()
                 WHERE student_uid = :student_uid"
            );
            $pendingStmt->execute([
                ':collection_id' => $paymentId,
                ':student_uid' => (string) ($student['uid'] ?? '')
            ]);
        }

        try {
            ensureFeeCollectionItemsTable($pdo);
        } catch (Throwable $e) {
            error_log('api/v2/fee-collection collect ensure item table failed: ' . $e->getMessage());
        }

        if ($collectionItems && apiTableExists($pdo, 'fee_collection_items')) {
            $itemInsertStmt = $pdo->prepare(
                "INSERT INTO fee_collection_items
                    (payment_id, student_id, student_uid, session, month_id, month_name, fee_total, paid_before, due_before, collected_amount, discount_amount, status_before, fee_components_json)
                 VALUES
                    (:payment_id, :student_id, :student_uid, :session, :month_id, :month_name, :fee_total, :paid_before, :due_before, :collected_amount, :discount_amount, :status_before, :fee_components_json)"
            );

            foreach ($collectionItems as $item) {
                $itemInsertStmt->execute([
                    ':payment_id' => $paymentId,
                    ':student_id' => (int) $student['id'],
                    ':student_uid' => (string) ($student['uid'] ?? ''),
                    ':session' => $session,
                    ':month_id' => (int) ($item['month_id'] ?? 0),
                    ':month_name' => (string) ($item['month_name'] ?? 'Unknown'),
                    ':fee_total' => (float) ($item['fee_total'] ?? 0),
                    ':paid_before' => (float) ($item['paid_before'] ?? 0),
                    ':due_before' => (float) ($item['due_before'] ?? 0),
                    ':collected_amount' => (float) ($item['collected_amount'] ?? 0),
                    ':discount_amount' => (float) ($item['discount_amount'] ?? 0),
                    ':status_before' => (string) ($item['status_before'] ?? 'unknown'),
                    ':fee_components_json' => json_encode($item['fee_components'] ?? [], JSON_UNESCAPED_UNICODE)
                ]);
            }
        }

        $pdo->commit();

        // Gold-plan chargeable messaging trigger for fee collection.
        try {
            $messagePayload = [
                'student_uid' => (string)($student['uid'] ?? ''),
                'student_name' => (string)($student['full_name'] ?? ''),
                'father_name' => (string)($student['father_name'] ?? ''),
                'class_name' => (string)($student['class_name'] ?? ''),
                'session' => $session,
                'amount' => number_format($amountCollected, 2, '.', ''),
                'discount' => number_format($discount, 2, '.', ''),
                'payment_month' => $monthCsv,
                'payment_date' => $paymentDate,
                'payment_method' => $paymentMethod,
                'mobile_number' => (string)($student['mobile_number'] ?? ''),
            ];
            $recipients = [];
            $mobile = trim((string)$messagePayload['mobile_number']);
            if ($mobile !== '' && $mobile !== '-') {
                $recipients[] = [
                    'mobile' => $mobile,
                    'student_name' => (string)$messagePayload['student_name'],
                    'father_name' => (string)$messagePayload['father_name'],
                    'class_name' => (string)$messagePayload['class_name'],
                ];
            }
            erpMessageTrigger(
                $pdo,
                'fee_collected',
                $messagePayload,
                $recipients,
                [
                    'reference_type' => 'fee_collection',
                    'reference_id' => (string)$paymentId,
                    'actor_id' => $userId,
                ]
            );
        } catch (Throwable $ignore) {
            // Messaging failures should not fail the fee collection transaction.
        }

        $amountLabel = number_format($amountCollected, 2, '.', '');
        erpSendAdminNotification(
            $pdo,
            'Fee collected',
            "Fee collected from {$student['full_name']} ({$student['uid']}) for session {$session}. Amount: ₹{$amountLabel}.",
            'good'
        );

        if ($isLegacyCollect) {
            jsonResponse(201, [
                'success' => true,
                'status' => 'success',
                'message' => 'Fee collected successfully',
                'payment_id' => $paymentId,
                'print_url' => feeReceiptUrl($paymentId)
            ]);
        }

        jsonResponse(201, [
            'success' => true,
            'status' => 'success',
            'message' => 'Fee collected successfully',
            'data' => [
                'payment_id' => $paymentId,
                'student' => [
                    'id' => (int) $student['id'],
                    'uid' => (string) $student['uid'],
                    'name' => (string) $student['full_name']
                ],
                'session' => $session,
                'payment_month' => $monthCsv,
                'months' => $paymentMonths,
                'print_url' => feeReceiptUrl($paymentId)
            ]
        ]);
    } catch (Throwable $e) {
        if ($pdo->inTransaction()) {
            $pdo->rollBack();
        }
        error_log('api/v2/fee-collection collect failed: ' . $e->getMessage());
        jsonResponse(500, [
            'success' => false,
            'message' => 'Failed to collect fee'
        ]);
    }
}

if ($requestMethod === 'POST' && $action === 'delete') {
    $paymentId = (int) ($_POST['payment_id'] ?? 0);
    if ($paymentId <= 0) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Payment ID is required'
        ]);
    }

    try {
        $paymentStmt = $pdo->prepare(
            "SELECT student_id, payment_month
             FROM fee_collections
             WHERE id = :id
             LIMIT 1"
        );
        $paymentStmt->execute([':id' => $paymentId]);
        $payment = $paymentStmt->fetch(PDO::FETCH_ASSOC);
        if (!$payment) {
            jsonResponse(404, [
                'success' => false,
                'message' => 'Payment record not found'
            ]);
        }

        $studentStmt = $pdo->prepare('SELECT uid FROM students WHERE id = :id LIMIT 1');
        $studentStmt->execute([':id' => (int) ($payment['student_id'] ?? 0)]);
        $studentUid = (string) ($studentStmt->fetchColumn() ?: '');

        $months = array_values(array_filter(array_map('trim', explode(',', (string) ($payment['payment_month'] ?? ''))), static fn($value) => $value !== ''));

        $pdo->beginTransaction();
        $deleteStmt = $pdo->prepare("DELETE FROM fee_collections WHERE id = :id");
        $deleteStmt->execute([':id' => $paymentId]);

        if (apiTableExists($pdo, 'fee_collection_items')) {
            $itemsDeleteStmt = $pdo->prepare("DELETE FROM fee_collection_items WHERE payment_id = :payment_id");
            $itemsDeleteStmt->execute([':payment_id' => $paymentId]);
        }

        if (in_array('0', $months, true) && $studentUid !== '') {
            $revertStmt = $pdo->prepare(
                "UPDATE pending_fees
                 SET status = 'pending',
                     collection_id = NULL,
                     updated_on = NOW()
                 WHERE student_uid = :student_uid"
            );
            $revertStmt->execute([':student_uid' => $studentUid]);
        }

        $pdo->commit();

        if ($isLegacyDelete) {
            jsonResponse(200, [
                'success' => true,
                'status' => 'success',
                'message' => 'Payment deleted successfully'
            ]);
        }

        jsonResponse(200, [
            'success' => true,
            'status' => 'success',
            'message' => 'Payment deleted successfully'
        ]);
    } catch (Throwable $e) {
        if ($pdo->inTransaction()) {
            $pdo->rollBack();
        }
        error_log('api/v2/fee-collection delete failed: ' . $e->getMessage());
        jsonResponse(500, [
            'success' => false,
            'message' => 'Failed to delete payment'
        ]);
    }
}

jsonResponse(400, [
    'success' => false,
    'message' => 'Unsupported action'
]);
