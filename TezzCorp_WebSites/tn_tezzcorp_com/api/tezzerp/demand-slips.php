<?php
declare(strict_types=1);

require_once __DIR__ . '/_common.php';
require_once __DIR__ . '/_message_events.php';

$requestMethod = strtoupper((string) ($_SERVER['REQUEST_METHOD'] ?? 'GET'));
if (!in_array($requestMethod, ['GET', 'POST'], true)) {
    jsonResponse(405, [
        'success' => false,
        'status' => 'error',
        'message' => 'Method not allowed'
    ]);
}

$isLegacyFetch = false;
$scope = 'options';
if ($requestMethod === 'GET') {
    $scope = queryEnumParam('scope', ['options', 'preview'], 'options');
} else {
    $legacyAction = strtolower(trim((string) ($_POST['action'] ?? '')));
    if ($legacyAction === 'fetch') {
        $scope = 'preview';
        $isLegacyFetch = true;
    } else {
        jsonResponse(400, [
            'success' => false,
            'status' => 'error',
            'message' => 'Unsupported action'
        ]);
    }
}

$pdo = apiDb();
apiEnsureFeeOpsSchema($pdo);
$userId = requireAuthenticatedUserId();
$user = fetchUserContext($pdo, $userId);
requireAnyPermission($user, ['demand-slip', 'collection', 'pending-fees']);

const SESSION_MONTH_ORDER = [4, 5, 6, 7, 8, 9, 10, 11, 12, 1, 2, 3];

function demandMonthLabel(int $month): string
{
    if ($month === 0) {
        return 'Pending Dues (Prev. Session)';
    }
    $labels = [
        1 => 'January',
        2 => 'February',
        3 => 'March',
        4 => 'April',
        5 => 'May',
        6 => 'June',
        7 => 'July',
        8 => 'August',
        9 => 'September',
        10 => 'October',
        11 => 'November',
        12 => 'December'
    ];
    return $labels[$month] ?? 'Unknown';
}

function demandDefaultSession(PDO $pdo): string
{
    return substr(apiDefaultAcademicSession($pdo), 0, 20);
}

function demandSessionColumn(PDO $pdo, string $table): string
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

function demandClassSource(PDO $pdo): array
{
    $candidates = ['class', 'class_master'];
    foreach ($candidates as $tableName) {
        if (!apiTableExists($pdo, $tableName)) {
            continue;
        }
        $columns = apiTableColumns($pdo, $tableName);
        $idColumn = apiResolveColumn($columns, ['id', 'class_id']);
        $nameColumn = apiResolveColumn($columns, ['class_name', 'name', 'title']);
        if ($idColumn === null || $nameColumn === null) {
            continue;
        }
        $statusColumn = apiResolveColumn($columns, ['status', 'is_active']);
        return [
            'table' => $tableName,
            'id' => $idColumn,
            'name' => $nameColumn,
            'status' => $statusColumn,
        ];
    }
    return [];
}

function demandTargetMonths(int $uptoMonth): array
{
    $result = [];
    foreach (SESSION_MONTH_ORDER as $month) {
        $result[] = $month;
        if ($month === $uptoMonth) {
            break;
        }
    }
    return $result;
}

function demandDistributeAcademicFees(array $feeStructureRows, array $specialAcademicByParticular): array
{
    $monthly = [];
    foreach (SESSION_MONTH_ORDER as $month) {
        $monthly[$month] = 0.0;
    }

    foreach ($feeStructureRows as $row) {
        $particularId = (int) ($row['particular_id'] ?? 0);
        $baseAmount = (float) ($row['amount'] ?? 0);

        if (isset($specialAcademicByParticular[$particularId])) {
            $special = $specialAcademicByParticular[$particularId];
            $discountAmount = 0.0;
            $discountPercent = (float) ($special['discount_percentage'] ?? 0);
            $specialAmount = (float) ($special['special_amount'] ?? 0);
            if ($discountPercent > 0) {
                $discountAmount += ($baseAmount * $discountPercent / 100);
            }
            if ($specialAmount > 0) {
                $discountAmount += $specialAmount;
            }
            $baseAmount = max(0, $baseAmount - $discountAmount);
        }

        $frequency = strtolower((string) ($row['frequency'] ?? 'monthly'));
        $applicableMonthsRaw = trim((string) ($row['applicable_months'] ?? 'all'));

        if ($frequency === 'monthly') {
            $monthsToApply = $applicableMonthsRaw === 'all'
                ? SESSION_MONTH_ORDER
                : array_map('intval', array_filter(array_map('trim', explode(',', $applicableMonthsRaw)), static fn ($v) => $v !== ''));
            foreach ($monthsToApply as $monthId) {
                if (isset($monthly[$monthId])) {
                    $monthly[$monthId] += $baseAmount;
                }
            }
            continue;
        }

        if ($frequency === 'annual') {
            if (isset($monthly[4])) {
                $monthly[4] += $baseAmount;
            }
            continue;
        }

        $interval = (int) ($row['custom_frequency'] ?? 0);
        if ($interval > 0) {
            for ($index = 2; $index < count(SESSION_MONTH_ORDER); $index += $interval) {
                $monthId = SESSION_MONTH_ORDER[$index];
                if (isset($monthly[$monthId])) {
                    $monthly[$monthId] += $baseAmount;
                }
            }
            continue;
        }

        if ($applicableMonthsRaw !== '' && $applicableMonthsRaw !== 'all') {
            $monthsToApply = array_map('intval', array_filter(array_map('trim', explode(',', $applicableMonthsRaw)), static fn ($v) => $v !== ''));
            foreach ($monthsToApply as $monthId) {
                if (isset($monthly[$monthId])) {
                    $monthly[$monthId] += $baseAmount;
                }
            }
        } elseif (isset($monthly[4])) {
            $monthly[4] += $baseAmount;
        }
    }

    return $monthly;
}

function demandPaidAmountForAcademicMonths(array $payments, array $ignoredCollectionIds): float
{
    $paid = 0.0;
    foreach ($payments as $payment) {
        $collectionId = (int) ($payment['id'] ?? 0);
        $paymentMonth = trim((string) ($payment['payment_month'] ?? ''));
        if (in_array($collectionId, $ignoredCollectionIds, true) || $paymentMonth === '0') {
            continue;
        }
        $paid += (float) ($payment['amount_collected'] ?? 0) + (float) ($payment['discount'] ?? 0);
    }
    return $paid;
}

try {
    $classSource = demandClassSource($pdo);

    if ($scope === 'options') {
        $classes = [];
        if ($classSource !== []) {
            $classWhere = '';
            if (!empty($classSource['status'])) {
                $classWhere = "WHERE COALESCE(NULLIF(" . apiIdent((string)$classSource['status']) . ", ''), 'active') IN ('active', 'Active', '1')";
            }
            $classSql = "SELECT "
                . apiIdent((string)$classSource['id']) . " AS id, "
                . "COALESCE(NULLIF(" . apiIdent((string)$classSource['name']) . ", ''), CONCAT('Class ', " . apiIdent((string)$classSource['id']) . ")) AS class_name "
                . "FROM " . apiIdent((string)$classSource['table']) . " "
                . $classWhere
                . " ORDER BY " . apiIdent((string)$classSource['id']) . " ASC";
            $classStmt = $pdo->query($classSql);
            $classes = $classStmt->fetchAll(PDO::FETCH_ASSOC) ?: [];
        }

        $months = [];
        foreach (SESSION_MONTH_ORDER as $month) {
            $months[] = [
                'id' => $month,
                'name' => demandMonthLabel($month)
            ];
        }

        jsonResponse(200, [
            'success' => true,
            'status' => 'success',
            'message' => 'Demand slip options fetched',
            'data' => [
                'classes' => $classes,
                'months' => $months,
                'default_month' => (int) date('n'),
                'default_session' => demandDefaultSession($pdo)
            ]
        ]);
    }

    $previewInput = $isLegacyFetch ? $_POST : $_GET;
    $classId = (int) ($previewInput['class_id'] ?? 0);
    $month = (int) ($previewInput['month'] ?? 0);
    if ($classId <= 0) {
        jsonResponse(400, [
            'success' => false,
            'status' => 'error',
            'message' => 'class_id is required'
        ]);
    }
    if (!in_array($month, SESSION_MONTH_ORDER, true)) {
        jsonResponse(400, [
            'success' => false,
            'status' => 'error',
            'message' => 'Invalid month'
        ]);
    }

    $session = trim((string) ($previewInput['session'] ?? ''));
    if ($session === '') {
        $session = demandDefaultSession($pdo);
    }
    $session = substr($session, 0, 20);
    $sessionVariants = apiAcademicSessionVariants($session);
    if (!$sessionVariants) {
        $sessionVariants = [$session];
    }

    $settingsStmt = $pdo->query("SELECT school_name, address, phone FROM site_settings LIMIT 1");
    $settings = $settingsStmt->fetch(PDO::FETCH_ASSOC) ?: [
        'school_name' => 'School Name',
        'address' => 'Address',
        'phone' => 'Phone'
    ];
    if (($settings['phone'] ?? '') === '' && apiColumnExists($pdo, 'site_settings', 'school_phone')) {
        $phoneStmt = $pdo->query("SELECT school_phone FROM site_settings LIMIT 1");
        $settings['phone'] = (string) ($phoneStmt->fetchColumn() ?: '');
    }

    if ($classSource === []) {
        jsonResponse(500, [
            'success' => false,
            'status' => 'error',
            'message' => 'Class master not available'
        ]);
    }

    $classInfoStmt = $pdo->prepare(
        "SELECT "
        . apiIdent((string)$classSource['id']) . " AS id, "
        . "COALESCE(NULLIF(" . apiIdent((string)$classSource['name']) . ", ''), CONCAT('Class ', " . apiIdent((string)$classSource['id']) . ")) AS class_name "
        . "FROM " . apiIdent((string)$classSource['table']) . " "
        . "WHERE " . apiIdent((string)$classSource['id']) . " = :id
         LIMIT 1"
    );
    $classInfoStmt->execute([':id' => $classId]);
    $classInfo = $classInfoStmt->fetch(PDO::FETCH_ASSOC);
    if (!$classInfo) {
        jsonResponse(404, [
            'success' => false,
            'status' => 'error',
            'message' => 'Class not found'
        ]);
    }
    $className = (string) ($classInfo['class_name'] ?? '');

    if (!apiTableExists($pdo, 'students')) {
        jsonResponse(500, [
            'success' => false,
            'status' => 'error',
            'message' => 'Students table not available'
        ]);
    }
    $studentColumns = apiTableColumns($pdo, 'students');
    $studentIdCol = apiResolveColumn($studentColumns, ['id', 'student_id']);
    $studentUidCol = apiResolveColumn($studentColumns, ['uid', 'student_uid', 'reg_no', 'registration_no']);
    $studentNameCol = apiResolveColumn($studentColumns, ['full_name', 'student_name', 'name']);
    $studentFatherCol = apiResolveColumn($studentColumns, ['father_name', 'guardian_name', 'father']);
    $studentRollCol = apiResolveColumn($studentColumns, ['roll_no', 'roll_number']);
    $studentClassCol = apiResolveColumn($studentColumns, ['class', 'class_id', 'class_name', 'standard']);
    $studentTransportCol = apiResolveColumn($studentColumns, ['transport_id', 'transport_route_id']);
    $studentMobileCol = apiResolveColumn($studentColumns, ['mobile_number', 'mobile', 'phone', 'contact_no']);
    $studentStatusCol = apiResolveColumn($studentColumns, ['status', 'is_active']);

    if ($studentIdCol === null || $studentUidCol === null || $studentNameCol === null || $studentClassCol === null) {
        jsonResponse(500, [
            'success' => false,
            'status' => 'error',
            'message' => 'Students table columns are incomplete'
        ]);
    }

    $studentsStatusWhere = $studentStatusCol !== null
        ? "AND COALESCE(NULLIF(" . apiIdent((string)$studentStatusCol) . ", ''), 'active') IN ('active', 'Active', '1')"
        : '';
    $studentsSql = "SELECT "
        . apiIdent((string)$studentIdCol) . " AS id, "
        . apiIdent((string)$studentUidCol) . " AS uid, "
        . "COALESCE(NULLIF(" . apiIdent((string)$studentNameCol) . ", ''), 'Unknown Student') AS full_name, "
        . ($studentFatherCol !== null ? "COALESCE(NULLIF(" . apiIdent((string)$studentFatherCol) . ", ''), '-') AS father_name, " : "'-' AS father_name, ")
        . ($studentRollCol !== null ? "COALESCE(NULLIF(" . apiIdent((string)$studentRollCol) . ", ''), '-') AS roll_no, " : "'-' AS roll_no, ")
        . ($studentMobileCol !== null ? "COALESCE(NULLIF(" . apiIdent((string)$studentMobileCol) . ", ''), '') AS mobile_number, " : "'' AS mobile_number, ")
        . ($studentTransportCol !== null ? apiIdent((string)$studentTransportCol) . " AS transport_id " : "0 AS transport_id ")
        . "FROM students "
        . "WHERE ("
        . apiIdent((string)$studentClassCol) . " = :class_id_str "
        . "OR " . apiIdent((string)$studentClassCol) . " = :class_name "
        . "OR CAST(" . apiIdent((string)$studentClassCol) . " AS UNSIGNED) = :class_id_int"
        . ") "
        . $studentsStatusWhere
        . " ORDER BY "
        . "CASE WHEN "
        . ($studentRollCol !== null ? apiIdent((string)$studentRollCol) : "'0'")
        . " REGEXP '^[0-9]+$' THEN CAST("
        . ($studentRollCol !== null ? apiIdent((string)$studentRollCol) : "'0'")
        . " AS UNSIGNED) ELSE 999999 END ASC, "
        . ($studentRollCol !== null ? apiIdent((string)$studentRollCol) : "'0'") . " ASC, "
        . apiIdent((string)$studentNameCol) . " ASC";
    $studentsStmt = $pdo->prepare($studentsSql);
    $studentsStmt->execute([
        ':class_id_str' => (string) $classId,
        ':class_id_int' => $classId,
        ':class_name' => $className
    ]);
    $students = $studentsStmt->fetchAll(PDO::FETCH_ASSOC);

    $structureSessionColumn = demandSessionColumn($pdo, 'fee_structure');
    $structureSessionParts = [];
    $structureSessionParams = [];
    if ($structureSessionColumn !== '') {
        foreach ($sessionVariants as $index => $sessionVariant) {
            $param = ':structure_session_' . $index;
            $structureSessionParts[] = 'fs.' . $structureSessionColumn . ' = ' . $param;
            $structureSessionParams[$param] = $sessionVariant;
        }
    }
    $structureHasStatus = apiColumnExists($pdo, 'fee_structure', 'status');
    $structureStatusWhere = $structureHasStatus
        ? "AND COALESCE(NULLIF(fs.status, ''), 'active') IN ('active', 'Active', '1')"
        : '';
    $structureSessionWhere = $structureSessionParts
        ? 'AND (' . implode(' OR ', $structureSessionParts) . ')'
        : '';
    $classFeeStructure = [];
    if (apiTableExists($pdo, 'fee_structure')) {
        $structureStmt = $pdo->prepare(
            "SELECT fs.particular_id,
                    fs.amount,
                    fs.frequency,
                    fs.custom_frequency,
                    fs.applicable_months,
                    COALESCE(p.particular, CONCAT('Particular ', fs.particular_id)) AS particular_name
             FROM fee_structure fs
             LEFT JOIN particulars p ON p.id = fs.particular_id
             WHERE fs.class_id = :class_id
               {$structureSessionWhere}
               {$structureStatusWhere}"
        );
        $structureStmt->execute(array_merge([
            ':class_id' => $classId
        ], $structureSessionParams));
        $classFeeStructure = $structureStmt->fetchAll(PDO::FETCH_ASSOC);
    }

    $targetMonths = demandTargetMonths($month);
    $demandSlips = [];

    $pendingStmt = null;
    if (apiTableExists($pdo, 'pending_fees')) {
        $pendingStmt = $pdo->prepare(
            "SELECT pending_fees.id,
                    pending_fees.pending_amount,
                    pending_fees.status,
                    pending_fees.collection_id,
                    pending_fees.particular_id,
                    COALESCE(p.particular, CONCAT('Particular ', pending_fees.particular_id)) AS particular_name
             FROM pending_fees
             LEFT JOIN particulars p ON p.id = pending_fees.particular_id
             WHERE student_uid = :uid"
        );
    }

    $specialStmt = null;
    if (apiTableExists($pdo, 'special_structure')) {
        $specialHasStatus = apiColumnExists($pdo, 'special_structure', 'status');
        $specialStatusWhere = $specialHasStatus ? "AND status = 'active'" : '';
        $specialStmt = $pdo->prepare(
            "SELECT particular_id, fee_type, special_amount, discount_percentage
             FROM special_structure
             WHERE student_uid = :uid
               {$specialStatusWhere}"
        );
    }

    $transportStmt = null;
    if (apiTableExists($pdo, 'transport')) {
        $transportSessionColumn = demandSessionColumn($pdo, 'transport');
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
             ORDER BY id DESC
             LIMIT 1"
        );
    } else {
        $transportSessionParams = [];
    }

    $paymentStmt = null;
    if (apiTableExists($pdo, 'fee_collections')) {
        $paymentSessionColumn = demandSessionColumn($pdo, 'fee_collections');
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
        $paymentStmt = $pdo->prepare(
            "SELECT id, amount_collected, discount, payment_month, status
             FROM fee_collections
             WHERE student_id = :student_id
               {$paymentSessionWhere}"
        );
    } else {
        $paymentSessionParams = [];
    }

    foreach ($students as $student) {
        $studentId = (int) ($student['id'] ?? 0);
        $studentUid = (string) ($student['uid'] ?? '');
        if ($studentId <= 0 || $studentUid === '') {
            continue;
        }

        $pendingRows = [];
        if ($pendingStmt instanceof PDOStatement) {
            $pendingStmt->execute([':uid' => $studentUid]);
            $pendingRows = $pendingStmt->fetchAll(PDO::FETCH_ASSOC);
        }

        $totalPendingDue = 0.0;
        $pendingCleared = true;
        $ignoredCollectionIds = [];
        foreach ($pendingRows as $pendingRow) {
            if (($pendingRow['status'] ?? '') === 'pending') {
                $totalPendingDue += (float) ($pendingRow['pending_amount'] ?? 0);
                $pendingCleared = false;
            } elseif (!empty($pendingRow['collection_id'])) {
                $ignoredCollectionIds[] = (int) $pendingRow['collection_id'];
            }
        }

        $specialRows = [];
        if ($specialStmt instanceof PDOStatement) {
            $specialStmt->execute([':uid' => $studentUid]);
            $specialRows = $specialStmt->fetchAll(PDO::FETCH_ASSOC);
        }
        $specialAcademicByParticular = [];
        $specialTransport = null;
        foreach ($specialRows as $specialRow) {
            if (($specialRow['fee_type'] ?? '') === 'transport') {
                $specialTransport = $specialRow;
            } else {
                $specialAcademicByParticular[(int) ($specialRow['particular_id'] ?? 0)] = $specialRow;
            }
        }

        $monthlyDues = demandDistributeAcademicFees($classFeeStructure, $specialAcademicByParticular);

        $transportMonthlyRate = 0.0;
        $transportId = (int) ($student['transport_id'] ?? 0);
        if ($transportId > 0 && $transportStmt instanceof PDOStatement) {
            $transportStmt->execute([
                ':id' => $transportId
            ] + $transportSessionParams);
            $transportMonthlyRate = (float) ($transportStmt->fetchColumn() ?: 0);

            if (is_array($specialTransport)) {
                $discountAmount = 0.0;
                $discountPercent = (float) ($specialTransport['discount_percentage'] ?? 0);
                $specialAmount = (float) ($specialTransport['special_amount'] ?? 0);
                if ($discountPercent > 0) {
                    $discountAmount += ($transportMonthlyRate * $discountPercent / 100);
                }
                if ($specialAmount > 0) {
                    $discountAmount += $specialAmount;
                }
                $transportMonthlyRate = max(0, $transportMonthlyRate - $discountAmount);
            }

            foreach ($monthlyDues as $monthKey => $monthValue) {
                $monthlyDues[$monthKey] = $monthValue + $transportMonthlyRate;
            }
        }

        $payments = [];
        if ($paymentStmt instanceof PDOStatement) {
            $paymentStmt->execute([
                ':student_id' => $studentId
            ] + $paymentSessionParams);
            $payments = $paymentStmt->fetchAll(PDO::FETCH_ASSOC);
        }
        $paidForMonths = demandPaidAmountForAcademicMonths($payments, $ignoredCollectionIds);

        $slipItems = [];
        $totalSlipAmount = 0.0;

        if ($totalPendingDue > 0 && !$pendingCleared) {
            $slipItems[] = [
                'description' => 'Previous Session Dues',
                'qty' => 1,
                'amount' => round($totalPendingDue, 2)
            ];
            $totalSlipAmount += $totalPendingDue;
        }

        $cumulativeFee = 0.0;
        foreach (SESSION_MONTH_ORDER as $monthId) {
            if (!isset($monthlyDues[$monthId])) {
                continue;
            }

            $monthFee = (float) $monthlyDues[$monthId];
            $cumulativeFee += $monthFee;

            $previousMonthsCost = $cumulativeFee - $monthFee;
            $paidForThisMonth = max(0.0, min($monthFee, $paidForMonths - $previousMonthsCost));
            $due = $monthFee - $paidForThisMonth;

            if (in_array($monthId, $targetMonths, true) && $due > 0.1) {
                $slipItems[] = [
                    'description' => 'Fees (' . demandMonthLabel($monthId) . ')',
                    'qty' => 1,
                    'amount' => round($due, 2)
                ];
                $totalSlipAmount += $due;
            }
        }

        if ($totalSlipAmount <= 0) {
            continue;
        }

        $demandSlips[] = [
            'student_id' => $studentId,
            'uid' => $studentUid,
            'student_name' => (string) ($student['full_name'] ?? 'Unknown Student'),
            'father_name' => (string) ($student['father_name'] ?? '-'),
            'class_name' => $className,
            'roll_no' => (string) ($student['roll_no'] ?? '-'),
            'mobile_number' => (string) ($student['mobile_number'] ?? ''),
            'fees' => $slipItems,
            'total_amount' => round($totalSlipAmount, 2)
        ];
    }

    $totalAmount = 0.0;
    foreach ($demandSlips as $slip) {
        $totalAmount += (float) ($slip['total_amount'] ?? 0);
    }

    $notifyMessage = ((string)($previewInput['notify_message'] ?? $previewInput['send_message'] ?? '0') === '1');
    if ($notifyMessage && $demandSlips !== []) {
        try {
            $recipients = [];
            foreach ($demandSlips as $slip) {
                $mobile = trim((string)($slip['mobile_number'] ?? ''));
                if ($mobile === '' || $mobile === '-') {
                    continue;
                }
                $recipients[] = [
                    'mobile' => $mobile,
                    'student_name' => (string)($slip['student_name'] ?? ''),
                    'father_name' => (string)($slip['father_name'] ?? ''),
                    'class_name' => (string)($slip['class_name'] ?? ''),
                    'roll_no' => (string)($slip['roll_no'] ?? ''),
                    'due_amount' => number_format((float)($slip['total_amount'] ?? 0), 2, '.', ''),
                ];
            }
            erpMessageTrigger(
                $pdo,
                'demand_slip_generated',
                [
                    'session' => $session,
                    'month' => (string)$month,
                    'month_name' => demandMonthLabel($month),
                    'class_name' => $className,
                    'students' => count($demandSlips),
                    'total_due' => number_format((float)$totalAmount, 2, '.', ''),
                ],
                $recipients,
                [
                    'reference_type' => 'demand_slip',
                    'reference_id' => 'CLS-' . $classId . '-M-' . $month . '-' . preg_replace('/[^0-9]/', '', $session),
                    'actor_id' => (string)($_SESSION['user_id'] ?? ''),
                ]
            );
        } catch (Throwable $ignore) {
            // Messaging failure should not block demand slip preview.
        }
    }

    if ($isLegacyFetch) {
        jsonResponse(200, [
            'success' => true,
            'status' => 'success',
            'message' => 'Demand slip preview generated',
            'school' => $settings,
            'data' => $demandSlips
        ]);
    }

    jsonResponse(200, [
        'success' => true,
        'status' => 'success',
        'message' => 'Demand slip preview generated',
        'data' => [
            'school' => $settings,
            'session' => $session,
            'month' => $month,
            'class_id' => $classId,
            'class_name' => $className,
            'items' => $demandSlips,
            'summary' => [
                'students' => count($demandSlips),
                'total_amount' => round($totalAmount, 2)
            ]
        ]
    ]);
} catch (Throwable $e) {
    error_log('api/v2/demand-slips failed: ' . $e->getMessage());
    jsonResponse(500, [
        'success' => false,
        'status' => 'error',
        'message' => 'Failed to generate demand slip preview'
    ]);
}
