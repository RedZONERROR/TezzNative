<?php
declare(strict_types=1);

require_once __DIR__ . '/_common.php';

$fpdfPath = dirname(__DIR__, 2) . '/fpdf/fpdf.php';
if (!is_file($fpdfPath)) {
    jsonResponse(500, [
        'success' => false,
        'message' => 'PDF library is not available on server'
    ]);
}
require_once $fpdfPath;

ensureMethod('GET');
$pdo = apiDb();
$userId = requireAuthenticatedUserId();
$user = fetchUserContext($pdo, $userId);
requireAnyPermission($user, ['students', 'admission', 'report-card', 'attendance']);

$classId = queryIntParam('class_id', 0, 0, 1000000);
$mode = queryEnumParam('mode', ['all', 'transport'], 'all');
if ($classId <= 0) {
    jsonResponse(400, [
        'success' => false,
        'message' => 'Class is required for PDF export'
    ]);
}

function pdfNormalizeText(string $value, string $fallback = '-'): string
{
    $text = trim(preg_replace('/\s+/', ' ', $value) ?? $value);
    if ($text === '') {
        $text = $fallback;
    }
    if (function_exists('iconv')) {
        $encoded = @iconv('UTF-8', 'windows-1252//TRANSLIT//IGNORE', $text);
        if (is_string($encoded) && $encoded !== '') {
            return $encoded;
        }
    }
    return $text;
}

function pdfShortText(string $value, int $maxChars): string
{
    $text = trim(preg_replace('/\s+/', ' ', $value) ?? $value);
    if ($text === '') {
        return '-';
    }
    if (function_exists('mb_strimwidth')) {
        return mb_strimwidth($text, 0, $maxChars, '...');
    }
    return strlen($text) > $maxChars ? (substr($text, 0, max(0, $maxChars - 3)) . '...') : $text;
}

function pdfResolveImagePath(string $rawPath): string
{
    $candidate = trim($rawPath);
    if ($candidate === '') {
        return '';
    }

    if (preg_match('/^https?:\/\//i', $candidate)) {
        $parsedPath = (string) parse_url($candidate, PHP_URL_PATH);
        $candidate = $parsedPath !== '' ? $parsedPath : '';
    }

    if ($candidate === '') {
        return '';
    }

    $docRoot = rtrim((string) ($_SERVER['DOCUMENT_ROOT'] ?? ''), DIRECTORY_SEPARATOR);
    if ($docRoot === '') {
        return '';
    }

    $relative = ltrim(str_replace(['\\', '//'], '/', $candidate), '/');
    if ($relative === '') {
        return '';
    }

    $fullPath = $docRoot . DIRECTORY_SEPARATOR . str_replace('/', DIRECTORY_SEPARATOR, $relative);
    $real = realpath($fullPath);
    if ($real === false || !is_file($real)) {
        return '';
    }

    $normalizedDocRoot = rtrim(str_replace('\\', '/', $docRoot), '/');
    $normalizedReal = str_replace('\\', '/', $real);
    if (!str_starts_with($normalizedReal, $normalizedDocRoot . '/')) {
        return '';
    }

    $ext = strtolower((string) pathinfo($normalizedReal, PATHINFO_EXTENSION));
    if (!in_array($ext, ['jpg', 'jpeg', 'png'], true)) {
        return '';
    }

    return $real;
}

final class StudentDirectoryPdf extends FPDF
{
    public string $schoolName = 'School';
    public string $reportTitle = 'Students Directory';
    public string $reportMeta = '';
    /** @var array{sn: float, profile: float, mobile: float, roll: float, address: float} */
    public array $columnWidths = [
        'sn' => 10.0,
        'profile' => 78.0,
        'mobile' => 28.0,
        'roll' => 18.0,
        'address' => 60.0
    ];

    public function Header(): void
    {
        $this->SetTextColor(15, 23, 42);
        $this->SetFont('Arial', 'B', 12);
        $this->Cell(0, 5.8, pdfNormalizeText($this->schoolName, 'School'), 0, 1, 'C');
        $this->SetFont('Arial', 'B', 10);
        $this->Cell(0, 4.8, pdfNormalizeText($this->reportTitle), 0, 1, 'C');
        if (trim($this->reportMeta) !== '') {
            $this->SetFont('Arial', '', 8);
            $this->SetTextColor(71, 85, 105);
            $this->Cell(0, 4.2, pdfNormalizeText($this->reportMeta), 0, 1, 'C');
        }
        $this->Ln(1);
        $this->renderTableHeader();
    }

    public function Footer(): void
    {
        $this->SetY(-8);
        $this->SetFont('Arial', '', 7);
        $this->SetTextColor(100, 116, 139);
        $this->Cell(0, 4, 'Page ' . $this->PageNo(), 0, 0, 'R');
    }

    public function renderTableHeader(): void
    {
        $this->SetFillColor(226, 232, 240);
        $this->SetDrawColor(148, 163, 184);
        $this->SetTextColor(15, 23, 42);
        $this->SetFont('Arial', 'B', 8.5);

        $x = $this->GetX();
        $y = $this->GetY();
        $height = 7.2;

        $this->Rect($x, $y, $this->columnWidths['sn'], $height, 'DF');
        $this->Rect($x + $this->columnWidths['sn'], $y, $this->columnWidths['profile'], $height, 'DF');
        $this->Rect($x + $this->columnWidths['sn'] + $this->columnWidths['profile'], $y, $this->columnWidths['mobile'], $height, 'DF');
        $this->Rect($x + $this->columnWidths['sn'] + $this->columnWidths['profile'] + $this->columnWidths['mobile'], $y, $this->columnWidths['roll'], $height, 'DF');
        $this->Rect($x + $this->columnWidths['sn'] + $this->columnWidths['profile'] + $this->columnWidths['mobile'] + $this->columnWidths['roll'], $y, $this->columnWidths['address'], $height, 'DF');

        $this->SetXY($x, $y);
        $this->Cell($this->columnWidths['sn'], $height, 'SN', 0, 0, 'C');
        $this->Cell($this->columnWidths['profile'], $height, 'Photo / Student / Father', 0, 0, 'L');
        $this->Cell($this->columnWidths['mobile'], $height, 'Mobile', 0, 0, 'C');
        $this->Cell($this->columnWidths['roll'], $height, 'Roll', 0, 0, 'C');
        $this->Cell($this->columnWidths['address'], $height, 'Address', 0, 0, 'L');
        $this->Ln($height);
    }
}

function drawStudentRow(StudentDirectoryPdf $pdf, array $row, int $serial): void
{
    $rowHeight = 16.0;
    $bottomLimit = $pdf->GetPageHeight() - 12.0;
    if (($pdf->GetY() + $rowHeight) >= $bottomLimit) {
        $pdf->AddPage();
    }

    $name = pdfShortText((string) ($row['full_name'] ?? ''), 44);
    $father = pdfShortText((string) ($row['father_name'] ?? ''), 44);
    $mobile = pdfShortText((string) ($row['mobile_number'] ?? ''), 20);
    $roll = pdfShortText((string) ($row['roll_no'] ?? ''), 10);
    $address = pdfShortText((string) ($row['address'] ?? ''), 92);
    $photoPath = pdfResolveImagePath((string) ($row['student_photo'] ?? ''));

    $x = $pdf->GetX();
    $y = $pdf->GetY();
    $w = $pdf->columnWidths;

    $pdf->SetDrawColor(203, 213, 225);
    $pdf->Rect($x, $y, $w['sn'], $rowHeight);
    $pdf->Rect($x + $w['sn'], $y, $w['profile'], $rowHeight);
    $pdf->Rect($x + $w['sn'] + $w['profile'], $y, $w['mobile'], $rowHeight);
    $pdf->Rect($x + $w['sn'] + $w['profile'] + $w['mobile'], $y, $w['roll'], $rowHeight);
    $pdf->Rect($x + $w['sn'] + $w['profile'] + $w['mobile'] + $w['roll'], $y, $w['address'], $rowHeight);

    $pdf->SetTextColor(15, 23, 42);
    $pdf->SetFont('Arial', '', 8);
    $pdf->SetXY($x, $y);
    $pdf->Cell($w['sn'], $rowHeight, (string) $serial, 0, 0, 'C');

    $photoX = $x + $w['sn'] + 1.1;
    $photoY = $y + 1.1;
    $photoW = 11.0;
    $photoH = 13.8;

    if ($photoPath !== '') {
        try {
            $pdf->Image($photoPath, $photoX, $photoY, $photoW, $photoH);
        } catch (Throwable $e) {
            $pdf->Rect($photoX, $photoY, $photoW, $photoH);
            $pdf->SetFont('Arial', '', 6);
            $pdf->SetXY($photoX, $photoY + 5);
            $pdf->Cell($photoW, 3.2, 'N/A', 0, 0, 'C');
        }
    } else {
        $pdf->Rect($photoX, $photoY, $photoW, $photoH);
        $pdf->SetFont('Arial', '', 6);
        $pdf->SetXY($photoX, $photoY + 5);
        $pdf->Cell($photoW, 3.2, 'N/A', 0, 0, 'C');
    }

    $textX = $x + $w['sn'] + $photoW + 2.9;
    $textW = $w['profile'] - ($photoW + 3.5);
    $pdf->SetFont('Arial', 'B', 8);
    $pdf->SetXY($textX, $y + 3.0);
    $pdf->Cell($textW, 3.2, pdfNormalizeText($name), 0, 0, 'L');

    $pdf->SetTextColor(71, 85, 105);
    $pdf->SetFont('Arial', '', 7);
    $pdf->SetXY($textX, $y + 8.2);
    $pdf->Cell($textW, 3.1, pdfNormalizeText('F: ' . $father, '-'), 0, 0, 'L');

    $pdf->SetTextColor(15, 23, 42);
    $pdf->SetFont('Arial', '', 8);
    $pdf->SetXY($x + $w['sn'] + $w['profile'], $y);
    $pdf->Cell($w['mobile'], $rowHeight, pdfNormalizeText($mobile), 0, 0, 'C');
    $pdf->Cell($w['roll'], $rowHeight, pdfNormalizeText($roll), 0, 0, 'C');

    $addressX = $x + $w['sn'] + $w['profile'] + $w['mobile'] + $w['roll'] + 1.0;
    $addressW = $w['address'] - 2.0;
    $pdf->SetFont('Arial', '', 7);
    $pdf->SetTextColor(30, 41, 59);
    $pdf->SetXY($addressX, $y + 4.0);
    $pdf->Cell($addressW, 3.1, pdfNormalizeText($address), 0, 0, 'L');

    $pdf->SetXY($x, $y + $rowHeight);
}

try {
    $studentColumns = apiTableColumns($pdo, 'students');
    $studentIdCol = apiResolveColumn($studentColumns, ['id', 'student_id']);
    $studentUidCol = apiResolveColumn($studentColumns, ['uid', 'student_uid']);
    $studentNameCol = apiResolveColumn($studentColumns, ['full_name', 'name', 'student_name']);
    $studentFatherCol = apiResolveColumn($studentColumns, ['father_name', 'guardian_name']);
    $studentRollCol = apiResolveColumn($studentColumns, ['roll_no', 'roll_number', 'roll']);
    $studentMobileCol = apiResolveColumn($studentColumns, ['mobile_number', 'mobile', 'phone']);
    $studentStatusCol = apiResolveColumn($studentColumns, ['status']);
    $studentClassCol = apiResolveColumn($studentColumns, ['class', 'class_id', 'current_class', 'applying_class']);
    $studentAddressCol = apiResolveColumn($studentColumns, ['residential_address', 'address', 'present_address']);
    $studentPermanentAddressCol = apiResolveColumn($studentColumns, ['permanent_address']);
    $studentTransportCol = apiResolveColumn($studentColumns, ['transport', 'is_transport']);
    $studentTransportIdCol = apiResolveColumn($studentColumns, ['transport_id']);
    $studentAdditionalTransportCol = apiResolveColumn($studentColumns, ['additional_transport']);

    if ($studentIdCol === null || $studentNameCol === null || $studentClassCol === null) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Students table is missing required columns for PDF export'
        ]);
    }

    $where = [];
    $params = [];

    if ($studentStatusCol !== null) {
        $where[] = "LOWER(COALESCE(s." . apiIdent($studentStatusCol) . ", 'active')) <> 'deleted'";
    }

    $className = apiClassNameById($pdo, $classId);
    if ($className !== '') {
        $where[] = '('
            . 'CAST(s.' . apiIdent($studentClassCol) . ' AS UNSIGNED) = :class_id_int'
            . ' OR s.' . apiIdent($studentClassCol) . ' = :class_id_str'
            . ' OR CONVERT(s.' . apiIdent($studentClassCol) . ' USING utf8mb4) COLLATE utf8mb4_unicode_ci = CONVERT(:class_name USING utf8mb4) COLLATE utf8mb4_unicode_ci'
            . ')';
        $params[':class_id_int'] = $classId;
        $params[':class_id_str'] = (string) $classId;
        $params[':class_name'] = $className;
    } else {
        $where[] = '('
            . 'CAST(s.' . apiIdent($studentClassCol) . ' AS UNSIGNED) = :class_id_int'
            . ' OR s.' . apiIdent($studentClassCol) . ' = :class_id_str'
            . ')';
        $params[':class_id_int'] = $classId;
        $params[':class_id_str'] = (string) $classId;
    }

    if ($mode === 'transport') {
        $transportClauses = [];
        if ($studentTransportCol !== null) {
            $transportClauses[] = 'CAST(COALESCE(s.' . apiIdent($studentTransportCol) . ', 0) AS UNSIGNED) > 0';
        }
        if ($studentTransportIdCol !== null) {
            $transportClauses[] = 'CAST(COALESCE(s.' . apiIdent($studentTransportIdCol) . ', 0) AS UNSIGNED) > 0';
        }
        if ($studentAdditionalTransportCol !== null) {
            $transportClauses[] = "TRIM(COALESCE(s." . apiIdent($studentAdditionalTransportCol) . ", '')) <> ''";
        }
        if ($transportClauses === []) {
            jsonResponse(400, [
                'success' => false,
                'message' => 'Transport fields are not configured in students table'
            ]);
        }
        $where[] = '(' . implode(' OR ', $transportClauses) . ')';
    }

    $photoExpr = "'' AS student_photo";
    if ($studentUidCol !== null && apiTableExists($pdo, 'documents')) {
        $documentColumns = apiTableColumns($pdo, 'documents');
        $documentUidCol = apiResolveColumn($documentColumns, ['uid', 'student_uid']);
        $documentPhotoCol = apiResolveColumn($documentColumns, ['student_photo', 'photo', 'image']);
        $documentIdCol = apiResolveColumn($documentColumns, ['id', 'document_id']);
        if ($documentUidCol !== null && $documentPhotoCol !== null) {
            $photoExpr = 'COALESCE(('
                . 'SELECT d.' . apiIdent($documentPhotoCol)
                . ' FROM ' . apiIdent('documents') . ' d'
                . ' WHERE d.' . apiIdent($documentUidCol) . ' = s.' . apiIdent($studentUidCol)
                . ($documentIdCol !== null ? (' ORDER BY d.' . apiIdent($documentIdCol) . ' DESC') : '')
                . ' LIMIT 1'
                . "), '') AS student_photo";
        }
    }

    $addressExprParts = [];
    if ($studentAddressCol !== null) {
        $addressExprParts[] = "NULLIF(TRIM(COALESCE(s." . apiIdent($studentAddressCol) . ", '')), '')";
    }
    if ($studentPermanentAddressCol !== null && $studentPermanentAddressCol !== $studentAddressCol) {
        $addressExprParts[] = "NULLIF(TRIM(COALESCE(s." . apiIdent($studentPermanentAddressCol) . ", '')), '')";
    }
    $addressExpr = $addressExprParts !== []
        ? ('COALESCE(' . implode(', ', $addressExprParts) . ", '-') AS address")
        : "'-' AS address";

    $whereSql = $where !== [] ? (' WHERE ' . implode(' AND ', $where)) : '';
    $query = 'SELECT '
        . 's.' . apiIdent($studentIdCol) . ' AS id, '
        . ($studentNameCol !== null ? "COALESCE(NULLIF(s." . apiIdent($studentNameCol) . ", ''), 'Unknown Student')" : "'Unknown Student'") . ' AS full_name, '
        . ($studentFatherCol !== null ? "COALESCE(NULLIF(s." . apiIdent($studentFatherCol) . ", ''), '-')" : "'-'") . ' AS father_name, '
        . ($studentRollCol !== null ? "COALESCE(NULLIF(s." . apiIdent($studentRollCol) . ", ''), '-')" : "'-'") . ' AS roll_no, '
        . ($studentMobileCol !== null ? "COALESCE(NULLIF(s." . apiIdent($studentMobileCol) . ", ''), '-')" : "'-'") . ' AS mobile_number, '
        . $addressExpr . ', '
        . $photoExpr
        . ' FROM ' . apiIdent('students') . ' s'
        . $whereSql
        . ' ORDER BY '
        . ($studentRollCol !== null ? ('CAST(COALESCE(s.' . apiIdent($studentRollCol) . ", '0') AS UNSIGNED), ") : '')
        . 's.' . apiIdent($studentNameCol) . ' ASC, s.' . apiIdent($studentIdCol) . ' ASC';

    $stmt = $pdo->prepare($query);
    foreach ($params as $key => $value) {
        $stmt->bindValue($key, $value, is_int($value) ? PDO::PARAM_INT : PDO::PARAM_STR);
    }
    $stmt->execute();
    $students = $stmt->fetchAll(PDO::FETCH_ASSOC) ?: [];

    $schoolName = 'School';
    if (apiTableExists($pdo, 'site_settings')) {
        $schoolStmt = $pdo->query('SELECT school_name FROM site_settings LIMIT 1');
        $schoolNameValue = trim((string) $schoolStmt->fetchColumn());
        if ($schoolNameValue !== '') {
            $schoolName = $schoolNameValue;
        }
    }

    $classLabel = $className !== '' ? $className : ('Class ' . $classId);
    $reportTitle = $mode === 'transport'
        ? 'Transport Students Directory'
        : 'Class Students Directory';
    $reportMeta = $classLabel . ' | Generated on ' . date('d M Y, h:i A');

    $pdf = new StudentDirectoryPdf('P', 'mm', 'A4');
    $pdf->SetMargins(8, 8, 8);
    $pdf->SetAutoPageBreak(true, 10);
    $pdf->AliasNbPages();
    $pdf->schoolName = $schoolName;
    $pdf->reportTitle = $reportTitle;
    $pdf->reportMeta = $reportMeta;
    $pdf->AddPage();

    if ($students === []) {
        $pdf->SetFont('Arial', '', 9);
        $pdf->SetTextColor(71, 85, 105);
        $pdf->Cell(0, 6.5, pdfNormalizeText('No students found for selected class and filter.'), 0, 1, 'C');
    } else {
        foreach ($students as $index => $row) {
            drawStudentRow($pdf, $row, $index + 1);
        }
    }

    $fileSafeClass = preg_replace('/[^a-z0-9_-]+/i', '-', strtolower($classLabel)) ?: ('class-' . $classId);
    $filename = ($mode === 'transport' ? 'transport-students-' : 'students-')
        . $fileSafeClass
        . '-'
        . date('Ymd-His')
        . '.pdf';

    if (ob_get_length() > 0) {
        @ob_clean();
    }
    header_remove('Content-Type');
    header('Content-Type: application/pdf');
    header('Content-Disposition: inline; filename="' . $filename . '"');
    $pdf->Output('I', $filename);
    exit;
} catch (Throwable $e) {
    error_log('api/tezzerp/students-print-pdf failed: ' . $e->getMessage());
    jsonResponse(500, [
        'success' => false,
        'message' => 'Failed to generate students PDF'
    ]);
}
