<?php
declare(strict_types=1);

require_once __DIR__ . '/includes/config.php';

function publicReceiptEsc(string $value): string
{
    return htmlspecialchars($value, ENT_QUOTES, 'UTF-8');
}

function publicReceiptTableExists(PDO $pdo, string $table): bool
{
    $stmt = $pdo->prepare(
        "SELECT COUNT(1)
         FROM information_schema.tables
         WHERE table_schema = DATABASE()
           AND table_name = :table_name"
    );
    $stmt->execute([':table_name' => $table]);
    return ((int)$stmt->fetchColumn()) > 0;
}

function publicReceiptMoney(float $amount, string $symbol, string $code): string
{
    $prefix = trim($symbol);
    if ($prefix === '') {
        $prefix = trim($code) !== '' ? trim($code) : 'INR';
    }
    return $prefix . ' ' . number_format($amount, 2);
}

function publicReceiptStatusLabel(string $status): string
{
    $value = strtolower(trim($status));
    if ($value === '') {
        return 'Pending';
    }
    return ucwords(str_replace('_', ' ', $value));
}

function publicReceiptDate(?string $raw): string
{
    $text = trim((string)$raw);
    if ($text === '') {
        return '-';
    }
    try {
        $date = new DateTime($text, new DateTimeZone('Asia/Kolkata'));
        return $date->format('d M Y, h:i A');
    } catch (Throwable $e) {
        return $text;
    }
}

function publicReceiptExtractMonths(string $text): string
{
    $source = trim($text);
    if ($source === '') {
        return '';
    }

    $monthMap = [
        'january' => 'January',
        'february' => 'February',
        'march' => 'March',
        'april' => 'April',
        'may' => 'May',
        'june' => 'June',
        'july' => 'July',
        'august' => 'August',
        'september' => 'September',
        'october' => 'October',
        'november' => 'November',
        'december' => 'December',
        'jan' => 'January',
        'feb' => 'February',
        'mar' => 'March',
        'apr' => 'April',
        'jun' => 'June',
        'jul' => 'July',
        'aug' => 'August',
        'sep' => 'September',
        'sept' => 'September',
        'oct' => 'October',
        'nov' => 'November',
        'dec' => 'December'
    ];

    preg_match_all('/\b(?:jan(?:uary)?|feb(?:ruary)?|mar(?:ch)?|apr(?:il)?|may|jun(?:e)?|jul(?:y)?|aug(?:ust)?|sep(?:t(?:ember)?)?|oct(?:ober)?|nov(?:ember)?|dec(?:ember)?)\b/i', $source, $matches);
    $months = [];
    foreach (($matches[0] ?? []) as $rawMonth) {
        $key = strtolower(trim((string)$rawMonth));
        if ($key === '') {
            continue;
        }
        if (isset($monthMap[$key])) {
            $months[$monthMap[$key]] = true;
            continue;
        }
        $normalized = ucfirst($key);
        $months[$normalized] = true;
    }

    return implode(', ', array_keys($months));
}

function publicReceiptBuildLineItems(array $dueRows, float $amount, bool $useMonthOnly): array
{
    $items = [];
    foreach ($dueRows as $dueRow) {
        if (!is_array($dueRow)) {
            continue;
        }
        $lineAmount = round((float)($dueRow['amount'] ?? 0), 2);
        if ($lineAmount <= 0) {
            continue;
        }
        $lineLabel = trim((string)($dueRow['label'] ?? 'Fee Due'));
        if ($lineLabel === '') {
            $lineLabel = 'Fee Due';
        }

        if ($useMonthOnly) {
            $monthLabel = publicReceiptExtractMonths($lineLabel);
            if ($monthLabel !== '') {
                $lineLabel = $monthLabel;
            }
        }

        $items[] = [
            'label' => $lineLabel,
            'amount' => $lineAmount
        ];
    }

    if ($items === []) {
        $items[] = [
            'label' => 'Fee Due',
            'amount' => max(0, round($amount, 2))
        ];
    }

    return $items;
}

function publicReceiptDownloadPdf(array $payload): void
{
    $fpdfPath = __DIR__ . '/fpdf/fpdf.php';
    if (!is_file($fpdfPath)) {
        return;
    }

    require_once $fpdfPath;
    if (!class_exists('FPDF')) {
        return;
    }

    $pdf = new FPDF('P', 'mm', 'A4');
    $pdf->SetMargins(8, 8, 8);
    $pdf->SetAutoPageBreak(false);
    $pdf->AddPage();

    $x = 8.0;
    $y = 8.0;
    $w = 194.0;
    $halfHeight = 138.0;

    $pdf->SetDrawColor(176, 194, 214);
    $pdf->SetFillColor(255, 255, 255);
    $pdf->Rect($x, $y, $w, $halfHeight, 'D');

    $padX = 4.0;
    $pdf->SetXY($x + $padX, $y + 4);
    $pdf->SetFont('Arial', 'B', 14);
    $pdf->Cell(126, 7, (string)($payload['school_name'] ?? 'School'), 0, 0, 'L');
    $pdf->SetFont('Arial', 'B', 11);
    $pdf->Cell(60, 7, 'FEE RECEIPT', 0, 1, 'R');

    $pdf->SetFont('Arial', '', 9);
    $pdf->SetX($x + $padX);
    $pdf->Cell(126, 5, (string)($payload['school_contact'] ?? ''), 0, 0, 'L');
    $pdf->Cell(60, 5, 'No: ' . (string)($payload['receipt_no'] ?? '-'), 0, 1, 'R');

    $pdf->SetX($x + $padX);
    $pdf->Cell(126, 5, (string)($payload['school_address'] ?? ''), 0, 0, 'L');
    $pdf->Cell(60, 5, (string)($payload['issued_at'] ?? ''), 0, 1, 'R');

    $pdf->SetDrawColor(196, 212, 230);
    $pdf->Line($x + $padX, $y + 20, $x + $w - $padX, $y + 20);

    $pdf->SetXY($x + $padX, $y + 22);
    $pdf->SetFont('Arial', '', 9);
    $metaPairs = [
        'Request' => (string)($payload['request_id'] ?? '-'),
        'Student' => (string)($payload['student_name'] ?? '-'),
        'Class' => (string)($payload['class_name'] ?? '-'),
        'Session' => (string)($payload['session'] ?? '-'),
        'Guardian' => (string)($payload['guardian_name'] ?? '-'),
        'Mobile' => (string)($payload['guardian_mobile'] ?? '-'),
        'Mode' => (string)($payload['mode_label'] ?? '-'),
        'Status' => (string)($payload['status_label'] ?? '-')
    ];

    $idx = 0;
    foreach ($metaPairs as $label => $value) {
        $colX = $x + $padX + (($idx % 2) * 95);
        $rowY = $y + 22 + (floor($idx / 2) * 5.4);
        $pdf->SetXY($colX, $rowY);
        $pdf->SetFont('Arial', 'B', 8);
        $pdf->Cell(22, 5, $label . ':', 0, 0, 'L');
        $pdf->SetFont('Arial', '', 8);
        $pdf->Cell(72, 5, substr($value, 0, 45), 0, 0, 'L');
        $idx += 1;
    }

    $tableY = $y + 47.5;
    $tableX = $x + $padX;
    $tableW = $w - ($padX * 2);
    $tableCol1 = 142.0;
    $tableCol2 = $tableW - $tableCol1;

    $pdf->SetFillColor(233, 242, 252);
    $pdf->SetDrawColor(196, 212, 230);
    $pdf->SetXY($tableX, $tableY);
    $pdf->SetFont('Arial', 'B', 8);
    $pdf->Cell($tableCol1, 6, 'PARTICULARS', 1, 0, 'L', true);
    $pdf->Cell($tableCol2, 6, 'AMOUNT', 1, 1, 'R', true);

    $pdf->SetFont('Arial', '', 8);
    $items = is_array($payload['line_items'] ?? null) ? $payload['line_items'] : [];
    foreach ($items as $lineItem) {
        if (!is_array($lineItem)) {
            continue;
        }
        $label = (string)($lineItem['label'] ?? 'Fee Due');
        $amountText = (string)($lineItem['amount_text'] ?? '0.00');

        $startY = $pdf->GetY();
        $pdf->SetXY($tableX, $startY);
        $pdf->MultiCell($tableCol1, 5, $label, 1, 'L');
        $endY = $pdf->GetY();
        $height = max(5.0, $endY - $startY);

        $pdf->SetXY($tableX + $tableCol1, $startY);
        $pdf->Cell($tableCol2, $height, $amountText, 1, 1, 'R');
    }

    $totalsY = $pdf->GetY() + 3;
    $pdf->SetXY($tableX, $totalsY);
    $pdf->SetFont('Arial', 'B', 9);
    $pdf->Cell(44, 6, 'Total Payable', 1, 0, 'L');
    $pdf->SetFont('Arial', '', 9);
    $pdf->Cell(46, 6, (string)($payload['total_text'] ?? ''), 1, 0, 'L');
    $pdf->SetFont('Arial', 'B', 9);
    $pdf->Cell(38, 6, 'Gateway Ref', 1, 0, 'L');
    $pdf->SetFont('Arial', '', 9);
    $pdf->Cell(66, 6, substr((string)($payload['gateway_ref'] ?? '-'), 0, 40), 1, 1, 'L');

    $pdf->SetX($tableX);
    $pdf->SetFont('Arial', 'I', 8);
    $pdf->Cell($tableW, 7, 'This is a system-generated receipt and does not require signature.', 0, 1, 'L');

    $fileName = 'fee-receipt-' . preg_replace('/[^A-Za-z0-9_-]+/', '', (string)($payload['request_id'] ?? 'receipt')) . '.pdf';
    $pdf->Output('D', $fileName);
    exit;
}

$requestId = trim((string)($_GET['request_id'] ?? ''));
if ($requestId === '' || preg_match('/^[A-Za-z0-9]+$/', $requestId) !== 1) {
    http_response_code(400);
    echo 'Invalid request id.';
    exit;
}

try {
    $pdo = getDBConnection();
} catch (Throwable $e) {
    http_response_code(500);
    echo 'Database connection failed.';
    exit;
}

if (!publicReceiptTableExists($pdo, 'public_fee_payments')) {
    http_response_code(404);
    echo 'Receipt data not available.';
    exit;
}

$settingsCols = publicReceiptTableExists($pdo, 'site_settings')
    ? (function () use ($pdo): array {
        try {
            $rows = $pdo->query('SHOW COLUMNS FROM site_settings')->fetchAll(PDO::FETCH_ASSOC) ?: [];
            return array_map(
                static fn(array $row): string => strtolower(trim((string)($row['Field'] ?? ''))),
                $rows
            );
        } catch (Throwable $e) {
            return [];
        }
    })()
    : [];

$schoolSelect = [
    "'School' AS school_name",
    "'' AS address",
    "'' AS phone",
    "'' AS email",
    "'' AS logo",
    "'INR' AS currency_code",
    "'Rs' AS currency_symbol",
];
if ($settingsCols !== []) {
    $schoolSelect = [
        in_array('school_name', $settingsCols, true) ? "COALESCE(NULLIF(ss.school_name, ''), 'School') AS school_name" : "'School' AS school_name",
        in_array('address', $settingsCols, true) ? "COALESCE(NULLIF(ss.address, ''), '') AS address" : "'' AS address",
        in_array('phone', $settingsCols, true) ? "COALESCE(NULLIF(ss.phone, ''), '') AS phone" : "'' AS phone",
        in_array('email', $settingsCols, true) ? "COALESCE(NULLIF(ss.email, ''), '') AS email" : "'' AS email",
        in_array('logo', $settingsCols, true) ? "COALESCE(NULLIF(ss.logo, ''), '') AS logo" : "'' AS logo",
        in_array('currency_code', $settingsCols, true) ? "COALESCE(NULLIF(ss.currency_code, ''), 'INR') AS currency_code" : "'INR' AS currency_code",
        in_array('currency_symbol', $settingsCols, true) ? "COALESCE(NULLIF(ss.currency_symbol, ''), 'Rs') AS currency_symbol" : "'Rs' AS currency_symbol",
    ];
}

$sql = "SELECT p.*,\n    " . implode(",\n    ", $schoolSelect)
    . "\nFROM public_fee_payments p\n"
    . ($settingsCols !== [] ? "LEFT JOIN site_settings ss ON 1=1\n" : '')
    . "WHERE p.request_id = :request_id\nLIMIT 1";

$stmt = $pdo->prepare($sql);
$stmt->execute([':request_id' => $requestId]);
$row = $stmt->fetch(PDO::FETCH_ASSOC);
if (!is_array($row)) {
    http_response_code(404);
    echo 'Receipt not found.';
    exit;
}

$status = strtolower(trim((string)($row['status'] ?? 'initiated')));
$isPaid = in_array($status, ['paid', 'settled', 'success'], true);
$paymentOption = strtolower(trim((string)($row['payment_option'] ?? 'upi')));
$isOnlineReceipt = in_array($paymentOption, ['upi', 'online', 'card', 'bank_transfer', 'netbanking'], true);
$isCashReceipt = in_array($paymentOption, ['cash', 'offline'], true);
$amount = round((float)($row['amount'] ?? 0), 2);
$receiptNo = (int)($row['fee_collection_id'] ?? 0) > 0
    ? ('FC-' . (string)((int)$row['fee_collection_id']))
    : ('REQ-' . $requestId);
$issuedAt = publicReceiptDate((string)($row['settled_at'] ?? $row['updated_at'] ?? $row['created_at'] ?? ''));
$currencySymbol = trim((string)($row['currency_symbol'] ?? 'Rs'));
$currencyCode = trim((string)($row['currency_code'] ?? 'INR'));
$dueRows = json_decode((string)($row['selected_due_json'] ?? ''), true);
if (!is_array($dueRows)) {
    $dueRows = [];
}

$lineItems = publicReceiptBuildLineItems($dueRows, $amount, $isCashReceipt);

$logo = trim((string)($row['logo'] ?? ''));
if ($logo !== '' && preg_match('#^https?://#i', $logo) !== 1) {
    $logo = '/' . ltrim(str_replace('\\', '/', $logo), '/');
}
$downloadMode = in_array(strtolower(trim((string)($_GET['download'] ?? '0'))), ['1', 'true', 'yes'], true);
$downloadUrl = '/fee-receipt?request_id=' . rawurlencode($requestId) . '&download=1';
if ($downloadMode && $isOnlineReceipt) {
    $schoolContact = trim((string)($row['phone'] ?? ''));
    $schoolEmail = trim((string)($row['email'] ?? ''));
    if ($schoolEmail !== '') {
        $schoolContact .= ($schoolContact !== '' ? ' | ' : '') . $schoolEmail;
    }
    publicReceiptDownloadPdf([
        'request_id' => $requestId,
        'receipt_no' => $receiptNo,
        'issued_at' => $issuedAt,
        'school_name' => (string)($row['school_name'] ?? 'School'),
        'school_address' => (string)($row['address'] ?? ''),
        'school_contact' => $schoolContact,
        'student_name' => (string)($row['student_name'] ?? 'Student'),
        'class_name' => (string)($row['class_name'] ?? '-'),
        'session' => (string)($row['session'] ?? '-'),
        'guardian_name' => (string)($row['guardian_name'] ?? '-'),
        'guardian_mobile' => (string)($row['guardian_mobile'] ?? $row['contact_number'] ?? '-'),
        'mode_label' => ucwords(str_replace('_', ' ', $paymentOption !== '' ? $paymentOption : 'upi')),
        'status_label' => publicReceiptStatusLabel((string)($row['status'] ?? 'pending')),
        'line_items' => array_map(static function (array $line) use ($currencySymbol, $currencyCode): array {
            return [
                'label' => (string)($line['label'] ?? 'Fee Due'),
                'amount_text' => publicReceiptMoney((float)($line['amount'] ?? 0), $currencySymbol, $currencyCode)
            ];
        }, $lineItems),
        'total_text' => publicReceiptMoney($amount, $currencySymbol, $currencyCode),
        'gateway_ref' => (string)($row['gateway_reference'] ?? '-')
    ]);
}
if ($downloadMode) {
    header('Content-Type: text/html; charset=UTF-8');
    header('Content-Disposition: attachment; filename="fee-receipt-' . rawurlencode($requestId) . '.html"');
}
?>
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Fee Receipt <?php echo publicReceiptEsc($requestId); ?></title>
    <style>
        :root {
            --ink: #13233f;
            --muted: #4f6078;
            --line: #cbd7e6;
            --panel: #ffffff;
            --soft: #f4f8fd;
            --brand: #174f80;
        }
        * { box-sizing: border-box; }
        body {
            margin: 0;
            padding: 18px;
            font-family: "Segoe UI", Tahoma, Arial, sans-serif;
            color: var(--ink);
            background: linear-gradient(180deg, #f4f8fc 0%, #edf3fb 100%);
        }
        .actions {
            width: min(860px, 100%);
            margin: 0 auto 10px;
            display: flex;
            gap: 8px;
            justify-content: flex-end;
        }
        .btn {
            border: 1px solid #9eb2ca;
            border-radius: 999px;
            background: #fff;
            color: var(--ink);
            padding: 8px 14px;
            font-size: 0.84rem;
            cursor: pointer;
            text-decoration: none;
        }
        .btn.primary {
            background: var(--brand);
            border-color: var(--brand);
            color: #fff;
        }
        .sheet {
            width: min(860px, 100%);
            min-height: 132mm;
            max-height: 132mm;
            margin: 0 auto;
            border: 1px solid var(--line);
            border-radius: 14px;
            background: var(--panel);
            box-shadow: 0 14px 34px rgba(18, 38, 64, 0.13);
            overflow: hidden;
            display: grid;
            grid-template-rows: auto 1fr auto;
        }
        .head {
            padding: 12px 14px;
            border-bottom: 1px solid var(--line);
            background: #f7fbff;
            display: flex;
            justify-content: space-between;
            gap: 12px;
        }
        .brand {
            display: flex;
            gap: 10px;
            min-width: 0;
        }
        .logo {
            width: 52px;
            height: 52px;
            border: 1px solid var(--line);
            border-radius: 10px;
            background: #fff;
            overflow: hidden;
            display: grid;
            place-items: center;
            flex: 0 0 52px;
            font-weight: 700;
            color: var(--brand);
        }
        .logo img { width: 100%; height: 100%; object-fit: cover; display: block; }
        .school h1 { margin: 0; font-size: 1rem; text-transform: uppercase; }
        .school p { margin: 2px 0 0; color: var(--muted); font-size: 0.74rem; }
        .meta {
            min-width: 170px;
            border: 1px solid var(--line);
            border-radius: 10px;
            background: #fff;
            padding: 8px 10px;
            text-align: right;
        }
        .meta b { display: block; font-size: 1rem; color: var(--brand); }
        .meta small { color: var(--muted); }
        .body {
            padding: 10px 14px 8px;
            display: grid;
            gap: 9px;
            align-content: start;
        }
        .grid {
            display: grid;
            grid-template-columns: repeat(4, minmax(0, 1fr));
            gap: 7px 10px;
            font-size: 0.75rem;
            padding: 8px 10px;
            border: 1px solid var(--line);
            border-radius: 10px;
            background: var(--soft);
        }
        .table {
            border: 1px solid var(--line);
            border-radius: 10px;
            overflow: hidden;
        }
        table {
            width: 100%;
            border-collapse: collapse;
            table-layout: fixed;
        }
        th, td {
            font-size: 0.74rem;
            padding: 6px 8px;
            border-bottom: 1px solid #deE7f2;
            text-align: left;
            vertical-align: top;
            overflow-wrap: anywhere;
            word-break: break-word;
        }
        th {
            background: #eaf2fb;
            color: #1b3f66;
            text-transform: uppercase;
            letter-spacing: 0.04em;
            font-size: 0.64rem;
        }
        th:first-child, td:first-child {
            width: 72%;
            overflow-wrap: anywhere;
            word-break: break-word;
        }
        th:last-child, td:last-child {
            width: 28%;
            text-align: right;
            white-space: nowrap;
        }
        tbody tr:last-child td { border-bottom: 0; }
        .totals {
            display: grid;
            grid-template-columns: repeat(3, minmax(0, 1fr));
            gap: 8px;
        }
        .totals div {
            border: 1px solid var(--line);
            border-radius: 10px;
            background: #f9fcff;
            padding: 8px;
        }
        .totals span {
            display: block;
            color: var(--muted);
            font-size: 0.66rem;
            text-transform: uppercase;
            letter-spacing: 0.06em;
            font-weight: 600;
        }
        .totals strong {
            display: block;
            margin-top: 3px;
            font-size: 0.85rem;
            color: var(--ink);
            overflow-wrap: anywhere;
            word-break: break-word;
        }
        .foot {
            padding: 8px 14px 10px;
            border-top: 1px dashed #b7c9dd;
            color: var(--muted);
            font-size: 0.7rem;
            display: flex;
            justify-content: space-between;
            gap: 10px;
        }
        .status-chip {
            display: inline-flex;
            align-items: center;
            border-radius: 999px;
            padding: 3px 8px;
            border: 1px solid #b9cce2;
            background: #edf5ff;
            color: #205586;
            font-weight: 700;
            font-size: 0.67rem;
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        .status-chip.warn {
            background: #fff7e6;
            border-color: #f0d9a6;
            color: #9a6808;
        }
        @media print {
            @page { size: A4 portrait; margin: 7mm; }
            body { background: #fff; padding: 0; }
            .actions { display: none !important; }
            .sheet {
                width: 100%;
                max-width: 100%;
                box-shadow: none;
                border-radius: 0;
                min-height: 138mm;
                max-height: 138mm;
            }
        }
    </style>
</head>
<body>
    <?php if (!$downloadMode): ?>
    <div class="actions">
        <a class="btn primary" href="<?php echo publicReceiptEsc($downloadUrl); ?>">Download Receipt</a>
    </div>
    <?php endif; ?>

    <article class="sheet">
        <header class="head">
            <div class="brand">
                <div class="logo">
                    <?php if ($logo !== ''): ?>
                        <img src="<?php echo publicReceiptEsc($logo); ?>" alt="School logo">
                    <?php else: ?>
                        <?php echo publicReceiptEsc(strtoupper(substr(trim((string)($row['school_name'] ?? 'S')), 0, 1))); ?>
                    <?php endif; ?>
                </div>
                <div class="school">
                    <h1><?php echo publicReceiptEsc((string)($row['school_name'] ?? 'School')); ?></h1>
                    <?php if (trim((string)($row['address'] ?? '')) !== ''): ?>
                        <p><?php echo publicReceiptEsc((string)$row['address']); ?></p>
                    <?php endif; ?>
                    <p>
                        <?php echo publicReceiptEsc(trim((string)($row['phone'] ?? ''))); ?>
                        <?php if (trim((string)($row['email'] ?? '')) !== ''): ?>
                            <?php echo ' | ' . publicReceiptEsc((string)$row['email']); ?>
                        <?php endif; ?>
                    </p>
                </div>
            </div>
            <div class="meta">
                <small>Fee Receipt</small>
                <b><?php echo publicReceiptEsc($receiptNo); ?></b>
                <small><?php echo publicReceiptEsc($issuedAt); ?></small>
            </div>
        </header>

        <section class="body">
            <div class="grid">
                <div><strong>Request</strong><br><?php echo publicReceiptEsc($requestId); ?></div>
                <div><strong>Student</strong><br><?php echo publicReceiptEsc((string)($row['student_name'] ?? 'Student')); ?></div>
                <div><strong>Class</strong><br><?php echo publicReceiptEsc((string)($row['class_name'] ?? '-')); ?></div>
                <div><strong>Session</strong><br><?php echo publicReceiptEsc((string)($row['session'] ?? '-')); ?></div>
                <div><strong>Guardian</strong><br><?php echo publicReceiptEsc((string)($row['guardian_name'] ?? '-')); ?></div>
                <div><strong>Mobile</strong><br><?php echo publicReceiptEsc((string)($row['guardian_mobile'] ?? $row['contact_number'] ?? '-')); ?></div>
                <div><strong>Mode</strong><br><?php echo publicReceiptEsc(ucwords(str_replace('_', ' ', (string)($row['payment_option'] ?? 'upi')))); ?></div>
                <div>
                    <strong>Status</strong><br>
                    <span class="status-chip <?php echo $isPaid ? '' : 'warn'; ?>">
                        <?php echo publicReceiptEsc(publicReceiptStatusLabel((string)($row['status'] ?? 'pending'))); ?>
                    </span>
                </div>
            </div>

            <div class="table">
                <table>
                    <thead>
                        <tr>
                            <th>Due Item</th>
                            <th>Amount</th>
                        </tr>
                    </thead>
                    <tbody>
                        <?php foreach ($lineItems as $lineItem): ?>
                            <tr>
                                <td><?php echo publicReceiptEsc((string)($lineItem['label'] ?? 'Fee Due')); ?></td>
                                <td><?php echo publicReceiptEsc(publicReceiptMoney((float)($lineItem['amount'] ?? 0), $currencySymbol, $currencyCode)); ?></td>
                            </tr>
                        <?php endforeach; ?>
                    </tbody>
                </table>
            </div>

            <div class="totals">
                <div>
                    <span>Total Payable</span>
                    <strong><?php echo publicReceiptEsc(publicReceiptMoney($amount, $currencySymbol, $currencyCode)); ?></strong>
                </div>
                <div>
                    <span>Gateway Ref</span>
                    <strong><?php echo publicReceiptEsc((string)($row['gateway_reference'] ?? '-')); ?></strong>
                </div>
                <div>
                    <span>Transaction</span>
                    <strong><?php echo publicReceiptEsc((string)($row['gateway_transaction_id'] ?? '-')); ?></strong>
                </div>
            </div>
        </section>

        <footer class="foot">
            <span>Generated by School ERP public payment portal.</span>
            <span><?php echo $isPaid ? 'Payment verified' : 'Payment pending verification'; ?></span>
        </footer>
    </article>
</body>
</html>
