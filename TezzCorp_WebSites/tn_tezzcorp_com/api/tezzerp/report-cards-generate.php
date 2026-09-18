<?php
declare(strict_types=1);

require_once __DIR__ . '/_common.php';

ensureMethod('POST');
$pdo = apiDb();
$userId = requireAuthenticatedUserId();
$user = fetchUserContext($pdo, $userId);
requireAnyPermission($user, ['report-card', 'set-results']);

function rcH(string $value): string
{
    return htmlspecialchars($value, ENT_QUOTES, 'UTF-8');
}

function rcNormalizeSession(string $session): string
{
    $normalized = trim($session);
    if ($normalized === '') {
        return '';
    }
    if (strlen($normalized) > 20) {
        return substr($normalized, 0, 20);
    }
    return $normalized;
}

function rcResultsClassColumn(PDO $pdo): string
{
    if (apiColumnExists($pdo, 'results', 'class_id')) {
        return 'class_id';
    }
    if (apiColumnExists($pdo, 'results', 'class')) {
        return 'class';
    }
    return '';
}

function rcResultsSessionColumn(PDO $pdo): string
{
    if (apiColumnExists($pdo, 'results', 'session')) {
        return 'session';
    }
    if (apiColumnExists($pdo, 'results', 'academic_session')) {
        return 'academic_session';
    }
    return '';
}

function rcUploadDir(string $suffix): string
{
    $root = rtrim((string) ($_SERVER['DOCUMENT_ROOT'] ?? ''), '/');
    return $root . '/uploads/report_cards/' . ltrim($suffix, '/');
}

function rcPublicPath(string $absolutePath): string
{
    $docRoot = rtrim((string) ($_SERVER['DOCUMENT_ROOT'] ?? ''), '/');
    if ($docRoot !== '' && str_starts_with($absolutePath, $docRoot)) {
        $relative = substr($absolutePath, strlen($docRoot));
        return '/' . ltrim((string) $relative, '/');
    }
    return '/' . ltrim((string) $absolutePath, '/');
}

function rcResolvePublicAsset(?string $storedPath): string
{
    $value = trim((string) $storedPath);
    if ($value === '') {
        return '';
    }
    if (preg_match('/^https?:\/\//i', $value)) {
        return $value;
    }
    return '/' . ltrim($value, '/');
}

function rcUploadSignature(string $field, string $targetSubdir): string
{
    if (!isset($_FILES[$field]) || !is_array($_FILES[$field])) {
        return '';
    }
    $file = $_FILES[$field];
    $error = (int) ($file['error'] ?? UPLOAD_ERR_NO_FILE);
    if ($error === UPLOAD_ERR_NO_FILE) {
        return '';
    }
    if ($error !== UPLOAD_ERR_OK) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Signature upload failed for ' . $field
        ]);
    }

    $mime = strtolower((string) ($file['type'] ?? ''));
    $allowed = ['image/png', 'image/jpeg', 'image/jpg', 'image/webp'];
    if (!in_array($mime, $allowed, true)) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Invalid signature file type for ' . $field
        ]);
    }
    if ((int) ($file['size'] ?? 0) > 2 * 1024 * 1024) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Signature file exceeds 2MB for ' . $field
        ]);
    }

    $dir = rcUploadDir($targetSubdir);
    if (!is_dir($dir) && !mkdir($dir, 0755, true) && !is_dir($dir)) {
        jsonResponse(500, [
            'success' => false,
            'message' => 'Failed to create signature upload directory'
        ]);
    }

    $ext = strtolower((string) pathinfo((string) ($file['name'] ?? ''), PATHINFO_EXTENSION));
    if ($ext === '') {
        $ext = 'png';
    }
    $name = $field . '_' . date('Ymd_His') . '_' . bin2hex(random_bytes(4)) . '.' . $ext;
    $target = rtrim($dir, '/') . '/' . $name;
    if (!move_uploaded_file((string) ($file['tmp_name'] ?? ''), $target)) {
        jsonResponse(500, [
            'success' => false,
            'message' => 'Failed to store signature file for ' . $field
        ]);
    }

    return rcPublicPath($target);
}

function rcDefaultGradeRules(): array
{
    return [
        ['label' => 'A+', 'min' => 90.0, 'max' => 100.0],
        ['label' => 'A', 'min' => 80.0, 'max' => 89.99],
        ['label' => 'B+', 'min' => 70.0, 'max' => 79.99],
        ['label' => 'B', 'min' => 60.0, 'max' => 69.99],
        ['label' => 'C+', 'min' => 50.0, 'max' => 59.99],
        ['label' => 'C', 'min' => 40.0, 'max' => 49.99],
        ['label' => 'F', 'min' => 0.0, 'max' => 39.99],
    ];
}

function rcConfiguredGradeRules(PDO $pdo): array
{
    $raw = apiSystemSettingsGetFirst($pdo, [
        'report_card_grade_rules_json',
        'grading_rules_json',
        'result_grade_rules_json'
    ]);
    if ($raw === '') {
        return rcDefaultGradeRules();
    }

    $decoded = json_decode($raw, true);
    if (!is_array($decoded)) {
        return rcDefaultGradeRules();
    }

    $rules = [];
    foreach ($decoded as $item) {
        if (!is_array($item)) {
            continue;
        }
        $label = trim((string) ($item['label'] ?? ''));
        $min = is_numeric($item['min'] ?? null) ? (float) $item['min'] : null;
        $max = is_numeric($item['max'] ?? null) ? (float) $item['max'] : null;
        if ($label === '' || $min === null || $max === null || $max < $min) {
            continue;
        }
        if (mb_strlen($label) > 12) {
            $label = mb_substr($label, 0, 12);
        }
        $rules[] = [
            'label' => $label,
            'min' => max(0.0, min(100.0, $min)),
            'max' => max(0.0, min(100.0, $max)),
        ];
    }

    if ($rules === []) {
        return rcDefaultGradeRules();
    }

    usort($rules, static fn(array $a, array $b): int => ($a['min'] < $b['min']) ? 1 : -1);
    return $rules;
}

function rcGrade(float $percentage, array $rules): string
{
    $value = max(0.0, min(100.0, $percentage));
    foreach ($rules as $rule) {
        $min = (float) ($rule['min'] ?? 0.0);
        $max = (float) ($rule['max'] ?? 0.0);
        if ($value >= $min && $value <= $max) {
            $label = trim((string) ($rule['label'] ?? ''));
            if ($label !== '') {
                return $label;
            }
        }
    }
    return 'F';
}

function rcFormatDate(?string $date): string
{
    $value = trim((string) $date);
    if ($value === '') {
        return '-';
    }
    $timestamp = strtotime($value);
    if ($timestamp === false) {
        return $value;
    }
    return date('d M Y', $timestamp);
}

function rcNormalizeLayoutMode(string $value): string
{
    return apiNormalizeTemplateLayoutMode($value);
}

function rcNormalizeColorVariant(string $value): string
{
    return apiNormalizeTemplateColorVariant($value);
}

function rcCardsPerPage(string $layoutMode): int
{
    return apiTemplateCardsPerPage($layoutMode);
}

try {
    $gradeRules = rcConfiguredGradeRules($pdo);

    if (!apiTableExists($pdo, 'results') || !apiTableExists($pdo, 'students')) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Required tables are missing for report card generation'
        ]);
    }

    $classId = (int) ($_POST['class_id'] ?? 0);
    $session = rcNormalizeSession((string) ($_POST['session'] ?? ''));
    $term = trim((string) ($_POST['term'] ?? ''));
    $studentInput = $_POST['students'] ?? [];
    $layoutModeInput = trim((string) ($_POST['layout_mode'] ?? ''));
    $colorVariantInput = trim((string) ($_POST['color_variant'] ?? ''));
    $includeLogoInput = trim((string) ($_POST['include_school_logo'] ?? ''));

    if ($classId <= 0) {
        jsonResponse(400, ['success' => false, 'message' => 'class_id is required']);
    }
    if ($session === '') {
        jsonResponse(400, ['success' => false, 'message' => 'session is required']);
    }
    if ($term === '') {
        jsonResponse(400, ['success' => false, 'message' => 'term is required']);
    }

    if (!is_array($studentInput)) {
        $studentInput = [$studentInput];
    }
    $studentUids = array_values(array_unique(array_filter(array_map(
        static fn($value): string => trim((string) $value),
        $studentInput
    ), static fn(string $value): bool => $value !== '')));

    if (!$studentUids) {
        jsonResponse(400, ['success' => false, 'message' => 'At least one student is required']);
    }
    if (count($studentUids) > 200) {
        jsonResponse(400, ['success' => false, 'message' => 'Maximum 200 students can be generated at once']);
    }

    $templatePreferences = apiTemplatePreferencesForType($pdo, 'report-card');
    $layoutMode = rcNormalizeLayoutMode($layoutModeInput !== '' ? $layoutModeInput : (string)($templatePreferences['layout_mode'] ?? 'single'));
    $colorVariant = rcNormalizeColorVariant($colorVariantInput !== '' ? $colorVariantInput : (string)($templatePreferences['color_variant'] ?? 'ocean'));
    $includeLogo = $includeLogoInput === ''
        ? ((string)($templatePreferences['include_school_logo'] ?? '1') !== '0')
        : !in_array(strtolower($includeLogoInput), ['0', 'false', 'no', 'off', 'disabled'], true);
    $cardsPerPage = rcCardsPerPage($layoutMode);

    $className = apiClassNameById($pdo, $classId);
    $sessionColumn = rcResultsSessionColumn($pdo);
    $classColumn = rcResultsClassColumn($pdo);
    $resultsHasTerm = apiColumnExists($pdo, 'results', 'term');
    $subjectHasPassing = apiColumnExists($pdo, 'subject', 'passing_marks');

    $uidParams = [];
    $uidPlaceholders = [];
    foreach ($studentUids as $index => $uid) {
        $key = ':uid_' . $index;
        $uidParams[$key] = $uid;
        $uidPlaceholders[] = $key;
    }

    $where = [];
    $params = $uidParams;
    $where[] = 'r.uid IN (' . implode(', ', $uidPlaceholders) . ')';

    if ($classColumn !== '') {
        if ($classColumn === 'class_id') {
            $classParts = ['r.class_id = :class_id_int', 'r.class_id = :class_id_str'];
            $params[':class_id_int'] = $classId;
            $params[':class_id_str'] = (string) $classId;
            if ($className !== '') {
                $classParts[] = 'r.class_id = :class_name';
                $params[':class_name'] = $className;
            }
            $where[] = '(' . implode(' OR ', $classParts) . ')';
        } else {
            $classParts = ['r.class = :class_id_str'];
            $params[':class_id_str'] = (string) $classId;
            if ($className !== '') {
                $classParts[] = 'r.class = :class_name';
                $params[':class_name'] = $className;
            }
            $where[] = '(' . implode(' OR ', $classParts) . ')';
        }
    }

    if ($sessionColumn !== '') {
        $sessionVariants = apiAcademicSessionVariants($session);
        if (!$sessionVariants) {
            $sessionVariants = [$session];
        }
        $parts = [];
        foreach ($sessionVariants as $index => $variant) {
            $key = ':session_' . $index;
            $parts[] = 'r.' . $sessionColumn . ' = ' . $key;
            $params[$key] = $variant;
        }
        if ($parts) {
            $where[] = '(' . implode(' OR ', $parts) . ')';
        }
    }

    if ($resultsHasTerm) {
        $where[] = 'r.term = :term';
        $params[':term'] = $term;
    }

    $resultsSql = "
        SELECT r.uid,
               r.subject_id,
               COALESCE(r.marks_obtained, 0) AS marks_obtained,
               COALESCE(r.max_marks, 0) AS max_marks,
               COALESCE(NULLIF(s.subject, ''), CONCAT('Subject ', r.subject_id)) AS subject_name,
               " . ($subjectHasPassing ? "COALESCE(s.passing_marks, 0)" : "0") . " AS passing_marks
        FROM results r
        LEFT JOIN subject s ON s.id = r.subject_id
        WHERE " . implode(' AND ', $where) . "
        ORDER BY r.uid ASC, subject_name ASC, r.subject_id ASC
    ";

    $resultsStmt = $pdo->prepare($resultsSql);
    foreach ($params as $key => $value) {
        if (is_int($value)) {
            $resultsStmt->bindValue($key, $value, PDO::PARAM_INT);
        } else {
            $resultsStmt->bindValue($key, (string) $value);
        }
    }
    $resultsStmt->execute();
    $resultRows = $resultsStmt->fetchAll(PDO::FETCH_ASSOC);

    $studentSql = "
        SELECT s.uid,
               COALESCE(NULLIF(s.full_name, ''), 'Unknown Student') AS full_name,
               COALESCE(NULLIF(s.father_name, ''), '-') AS father_name,
               COALESCE(NULLIF(s.mother_name, ''), '-') AS mother_name,
               COALESCE(NULLIF(s.roll_no, ''), '-') AS roll_no,
               s.date_of_birth,
               s.admission_date,
               COALESCE(NULLIF(c.class_name, ''), NULLIF(s.class, ''), 'N/A') AS class_name,
               COALESCE(NULLIF(d.student_photo, ''), '') AS student_photo
        FROM students s
        LEFT JOIN class c ON (c.id = CAST(s.class AS UNSIGNED) OR c.class_name = s.class)
        LEFT JOIN documents d ON d.uid = s.uid
        WHERE s.uid IN (" . implode(', ', $uidPlaceholders) . ")
        ORDER BY s.full_name ASC
    ";
    $studentStmt = $pdo->prepare($studentSql);
    foreach ($uidParams as $key => $value) {
        $studentStmt->bindValue($key, $value);
    }
    $studentStmt->execute();
    $studentRows = $studentStmt->fetchAll(PDO::FETCH_ASSOC);

    if (!$studentRows) {
        jsonResponse(404, [
            'success' => false,
            'message' => 'Selected students not found'
        ]);
    }

    $studentsByUid = [];
    foreach ($studentRows as $row) {
        $uid = (string) ($row['uid'] ?? '');
        if ($uid === '') {
            continue;
        }
        $studentsByUid[$uid] = $row;
    }

    $resultsByUid = [];
    foreach ($resultRows as $row) {
        $uid = (string) ($row['uid'] ?? '');
        if ($uid === '') {
            continue;
        }
        if (!isset($resultsByUid[$uid])) {
            $resultsByUid[$uid] = [];
        }
        $resultsByUid[$uid][] = $row;
    }

    $rankMap = [];
    if ($classColumn !== '') {
        $rankWhere = [];
        $rankParams = [];
        if ($classColumn === 'class_id') {
            $rankParts = ['r.class_id = :rank_class_id_int', 'r.class_id = :rank_class_id_str'];
            $rankParams[':rank_class_id_int'] = $classId;
            $rankParams[':rank_class_id_str'] = (string) $classId;
            if ($className !== '') {
                $rankParts[] = 'r.class_id = :rank_class_name';
                $rankParams[':rank_class_name'] = $className;
            }
            $rankWhere[] = '(' . implode(' OR ', $rankParts) . ')';
        } else {
            $rankParts = ['r.class = :rank_class_id_str'];
            $rankParams[':rank_class_id_str'] = (string) $classId;
            if ($className !== '') {
                $rankParts[] = 'r.class = :rank_class_name';
                $rankParams[':rank_class_name'] = $className;
            }
            $rankWhere[] = '(' . implode(' OR ', $rankParts) . ')';
        }

        if ($sessionColumn !== '') {
            $sessionVariants = apiAcademicSessionVariants($session);
            if (!$sessionVariants) {
                $sessionVariants = [$session];
            }
            $parts = [];
            foreach ($sessionVariants as $index => $variant) {
                $key = ':rank_session_' . $index;
                $parts[] = 'r.' . $sessionColumn . ' = ' . $key;
                $rankParams[$key] = $variant;
            }
            if ($parts) {
                $rankWhere[] = '(' . implode(' OR ', $parts) . ')';
            }
        }

        if ($resultsHasTerm) {
            $rankWhere[] = 'r.term = :rank_term';
            $rankParams[':rank_term'] = $term;
        }

        $rankSql = "
            SELECT r.uid, SUM(COALESCE(r.marks_obtained, 0)) AS total_marks
            FROM results r
            WHERE " . implode(' AND ', $rankWhere) . "
            GROUP BY r.uid
            ORDER BY total_marks DESC, r.uid ASC
        ";
        $rankStmt = $pdo->prepare($rankSql);
        foreach ($rankParams as $key => $value) {
            if (is_int($value)) {
                $rankStmt->bindValue($key, $value, PDO::PARAM_INT);
            } else {
                $rankStmt->bindValue($key, (string) $value);
            }
        }
        $rankStmt->execute();
        $rankRows = $rankStmt->fetchAll(PDO::FETCH_ASSOC);

        $prevMarks = null;
        $currentRank = 0;
        $position = 0;
        foreach ($rankRows as $rankRow) {
            $position += 1;
            $marks = (float) ($rankRow['total_marks'] ?? 0);
            if ($prevMarks === null || abs($marks - $prevMarks) > 0.0001) {
                $currentRank = $position;
                $prevMarks = $marks;
            }
            $rankMap[(string) ($rankRow['uid'] ?? '')] = $currentRank;
        }
    }

    $siteSettings = [];
    if (apiTableExists($pdo, 'site_settings')) {
        $siteSettings = $pdo->query('SELECT * FROM site_settings LIMIT 1')->fetch(PDO::FETCH_ASSOC) ?: [];
    }

    $deptSignature = rcUploadSignature('dept_examination_signature', 'signatures');
    $teacherSignature = rcUploadSignature('class_teacher_signature', 'signatures');
    $principalSignature = rcUploadSignature('principal_signature', 'signatures');

    if ($deptSignature === '' && strtolower((string) ($siteSettings['exam_controller_signature_enabled'] ?? 'disabled')) === 'enabled') {
        $deptSignature = rcResolvePublicAsset((string) ($siteSettings['exam_controller_signature'] ?? ''));
    }
    if ($teacherSignature === '' && strtolower((string) ($siteSettings['director_signature_enabled'] ?? 'disabled')) === 'enabled') {
        $teacherSignature = rcResolvePublicAsset((string) ($siteSettings['director_signature'] ?? ''));
    }
    if ($principalSignature === '' && strtolower((string) ($siteSettings['principal_signature_enabled'] ?? 'disabled')) === 'enabled') {
        $principalSignature = rcResolvePublicAsset((string) ($siteSettings['principal_signature'] ?? ''));
    }

    $schoolName = trim((string) ($siteSettings['school_name'] ?? 'School Name'));
    $schoolAddress = trim((string) ($siteSettings['address'] ?? ''));
    $schoolPhone = trim((string) ($siteSettings['phone'] ?? ''));
    $schoolEmail = trim((string) ($siteSettings['email'] ?? ''));
    $logoUrl = rcResolvePublicAsset((string) ($siteSettings['logo'] ?? ''));

    $studentCards = [];
    $cardSummaries = [];
    foreach ($studentUids as $uid) {
        if (!isset($studentsByUid[$uid])) {
            continue;
        }
        $student = $studentsByUid[$uid];
        $subjects = $resultsByUid[$uid] ?? [];
        $totalObtained = 0.0;
        $totalMax = 0.0;
        foreach ($subjects as $subjectRow) {
            $totalObtained += (float) ($subjectRow['marks_obtained'] ?? 0);
            $totalMax += (float) ($subjectRow['max_marks'] ?? 0);
        }
        $percentage = $totalMax > 0 ? round(($totalObtained / $totalMax) * 100, 2) : 0.0;
        $grade = rcGrade($percentage, $gradeRules);
        $rank = $rankMap[$uid] ?? null;

        $cardSummaries[] = [
            'uid' => $uid,
            'full_name' => (string) ($student['full_name'] ?? 'Unknown Student'),
            'class_name' => (string) ($student['class_name'] ?? ''),
            'total_marks' => round($totalObtained, 2),
            'max_marks' => round($totalMax, 2),
            'percentage' => $percentage,
            'grade' => $grade,
            'rank' => $rank
        ];

        $subjectRowsHtml = '';
        if (!$subjects) {
            $subjectRowsHtml = '<tr><td colspan="6" class="muted">No result rows found for selected session/term.</td></tr>';
        } else {
            foreach ($subjects as $subjectRow) {
                $marksObtained = (float) ($subjectRow['marks_obtained'] ?? 0);
                $maxMarks = (float) ($subjectRow['max_marks'] ?? 0);
                $subjectPercent = $maxMarks > 0 ? round(($marksObtained / $maxMarks) * 100, 2) : 0.0;
                $subjectRowsHtml .= '<tr>'
                    . '<td>' . rcH((string) ($subjectRow['subject_name'] ?? ('Subject ' . ($subjectRow['subject_id'] ?? '')))) . '</td>'
                    . '<td>' . rcH(number_format((float) ($subjectRow['passing_marks'] ?? 0), 0)) . '</td>'
                    . '<td>' . rcH(number_format($marksObtained, 0)) . '</td>'
                    . '<td>' . rcH(number_format($maxMarks, 0)) . '</td>'
                    . '<td>' . rcH(number_format($subjectPercent, 2)) . '%</td>'
                    . '<td>' . rcH(rcGrade($subjectPercent, $gradeRules)) . '</td>'
                    . '</tr>';
            }
        }

        $studentPhoto = rcResolvePublicAsset((string) ($student['student_photo'] ?? ''));
        $photoBlock = $studentPhoto !== ''
            ? '<img class="student-photo" src="' . rcH($studentPhoto) . '" alt="Student Photo">'
            : '<div class="student-photo placeholder">No Photo</div>';
        $logoBlock = ($includeLogo && $logoUrl !== '')
            ? '<img class="school-logo" src="' . rcH($logoUrl) . '" alt="School Logo">'
            : '<div class="school-logo placeholder">Logo</div>';

        $signatureBlock = '<div class="signature-grid">';
        $signatureBlock .= '<div class="signature-item">'
            . ($deptSignature !== '' ? '<img src="' . rcH($deptSignature) . '" alt="Exam Controller Signature">' : '<div class="signature-space"></div>')
            . '<span>Exam Controller</span></div>';
        $signatureBlock .= '<div class="signature-item">'
            . ($teacherSignature !== '' ? '<img src="' . rcH($teacherSignature) . '" alt="Class Teacher Signature">' : '<div class="signature-space"></div>')
            . '<span>Class Teacher</span></div>';
        $signatureBlock .= '<div class="signature-item">'
            . ($principalSignature !== '' ? '<img src="' . rcH($principalSignature) . '" alt="Principal Signature">' : '<div class="signature-space"></div>')
            . '<span>Principal</span></div>';
        $signatureBlock .= '</div>';

        $studentCards[] = '
            <section class="report-card">
                <header class="report-header">
                    <div class="school-brand">
                        ' . $logoBlock . '
                        <div>
                            <h1>' . rcH($schoolName) . '</h1>
                            <p>' . rcH($schoolAddress) . '</p>
                            <p>' . rcH(trim($schoolPhone . ($schoolEmail !== '' ? ' | ' . $schoolEmail : ''))) . '</p>
                        </div>
                    </div>
                    <h2>Report Card - ' . rcH($term) . ' (' . rcH($session) . ')</h2>
                </header>

                <div class="student-head">
                    <div class="student-meta">
                        <p><strong>Name:</strong> ' . rcH((string) ($student['full_name'] ?? '-')) . '</p>
                        <p><strong>Father:</strong> ' . rcH((string) ($student['father_name'] ?? '-')) . '</p>
                        <p><strong>Mother:</strong> ' . rcH((string) ($student['mother_name'] ?? '-')) . '</p>
                        <p><strong>Class:</strong> ' . rcH((string) ($student['class_name'] ?? '-')) . '</p>
                        <p><strong>Roll No:</strong> ' . rcH((string) ($student['roll_no'] ?? '-')) . '</p>
                        <p><strong>UID:</strong> ' . rcH($uid) . '</p>
                        <p><strong>DOB:</strong> ' . rcH(rcFormatDate((string) ($student['date_of_birth'] ?? ''))) . '</p>
                    </div>
                    <div class="student-photo-wrap">' . $photoBlock . '</div>
                </div>

                <table class="marks-table">
                    <thead>
                        <tr>
                            <th>Subject</th>
                            <th>Pass Marks</th>
                            <th>Obtained</th>
                            <th>Max Marks</th>
                            <th>Percent</th>
                            <th>Grade</th>
                        </tr>
                    </thead>
                    <tbody>
                        ' . $subjectRowsHtml . '
                    </tbody>
                </table>

                <div class="summary-row">
                    <span><strong>Total:</strong> ' . rcH(number_format($totalObtained, 2)) . ' / ' . rcH(number_format($totalMax, 2)) . '</span>
                    <span><strong>Percentage:</strong> ' . rcH(number_format($percentage, 2)) . '%</span>
                    <span><strong>Grade:</strong> ' . rcH($grade) . '</span>
                    <span><strong>Rank:</strong> ' . rcH($rank === null ? '-' : (string) $rank) . '</span>
                </div>

                ' . $signatureBlock . '
            </section>
        ';
    }

    if (!$studentCards) {
        jsonResponse(404, [
            'success' => false,
            'message' => 'No matching student records found for generation'
        ]);
    }

    $themePalette = [
        'ocean' => ['bg' => '#eaf2ff', 'surface' => '#ffffff', 'line' => '#c9d9fb', 'head_a' => '#0b3f91', 'head_b' => '#1d6ed8', 'accent' => '#eaf2ff'],
        'royal' => ['bg' => '#f3edff', 'surface' => '#ffffff', 'line' => '#d5caf7', 'head_a' => '#3f1d8f', 'head_b' => '#6a31c7', 'accent' => '#efe9ff'],
        'sunset' => ['bg' => '#fff4eb', 'surface' => '#ffffff', 'line' => '#f4d7bf', 'head_a' => '#9a3412', 'head_b' => '#ea580c', 'accent' => '#fff1e6'],
        'emerald' => ['bg' => '#eafaf3', 'surface' => '#ffffff', 'line' => '#c3ead9', 'head_a' => '#065f46', 'head_b' => '#059669', 'accent' => '#ecfdf5'],
        'mono' => ['bg' => '#f5f6f8', 'surface' => '#ffffff', 'line' => '#d2d6dc', 'head_a' => '#111827', 'head_b' => '#374151', 'accent' => '#f3f4f6'],
    ];
    $palette = $themePalette[$colorVariant] ?? $themePalette['ocean'];
    $layoutClass = $layoutMode === '4up' ? 'layout-4up' : ($layoutMode === '2up' ? 'layout-2up' : 'layout-single');

    $sheetChunks = array_chunk($studentCards, max(1, $cardsPerPage));
    $sheetHtml = [];
    foreach ($sheetChunks as $chunkIndex => $chunkCards) {
        $sheetHtml[] = '<section class="report-sheet ' . rcH($layoutClass) . '"' . ($chunkIndex === count($sheetChunks) - 1 ? '' : ' data-break="1"') . '>' . implode("\n", $chunkCards) . '</section>';
    }

    $generatedDir = rcUploadDir('generated');
    if (!is_dir($generatedDir) && !mkdir($generatedDir, 0755, true) && !is_dir($generatedDir)) {
        jsonResponse(500, [
            'success' => false,
            'message' => 'Failed to create output directory for report cards'
        ]);
    }
    $filename = 'report_cards_' . date('Ymd_His') . '_' . bin2hex(random_bytes(4)) . '.html';
    $outputPath = rtrim($generatedDir, '/') . '/' . $filename;

    $documentHtml = '<!doctype html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Report Cards</title>
    <style>
        :root{
            color-scheme:light;
            --bg:' . rcH($palette['bg']) . ';
            --surface:' . rcH($palette['surface']) . ';
            --line:' . rcH($palette['line']) . ';
            --head-a:' . rcH($palette['head_a']) . ';
            --head-b:' . rcH($palette['head_b']) . ';
            --accent-soft:' . rcH($palette['accent']) . ';
        }
        *{box-sizing:border-box;}
        body{margin:0;padding:16px;font-family:Arial,sans-serif;background:var(--bg);color:#0f172a;}
        .report-sheet{display:grid;gap:12px;align-items:start;min-height:calc(297mm - 20mm);}
        .report-sheet[data-break="1"]{page-break-after:always;break-after:page;margin-bottom:10px;}
        .report-sheet.layout-single{grid-template-columns:1fr;}
        .report-sheet.layout-2up{grid-template-columns:repeat(2,minmax(0,1fr));}
        .report-sheet.layout-4up{grid-template-columns:repeat(2,minmax(0,1fr));}
        .report-card{background:var(--surface);border:1px solid var(--line);border-radius:10px;padding:14px;margin:0;page-break-inside:avoid;}
        .report-header{background:linear-gradient(135deg,var(--head-a),var(--head-b));color:#fff;border-radius:10px;padding:10px 12px;}
        .report-header h1{margin:0 0 4px;font-size:24px;color:#fff;}
        .report-header h2{margin:12px 0 0;font-size:18px;text-align:center;}
        .report-header p{margin:0;font-size:12px;color:#e2e8f0;}
        .school-brand{display:flex;align-items:center;gap:12px;}
        .school-logo{width:64px;height:64px;object-fit:contain;border:1px solid rgba(255,255,255,.3);border-radius:8px;background:#fff;}
        .school-logo.placeholder{display:flex;align-items:center;justify-content:center;font-size:11px;color:#6b7280;}
        .student-head{display:flex;justify-content:space-between;gap:12px;margin-top:12px;}
        .student-meta{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:4px 12px;flex:1;background:var(--accent-soft);border:1px solid var(--line);border-radius:10px;padding:8px;}
        .student-meta p{margin:0;font-size:12px;}
        .student-photo-wrap{width:92px;display:flex;justify-content:flex-end;}
        .student-photo{width:88px;height:88px;object-fit:cover;border:1px solid #dbe3f1;border-radius:8px;}
        .student-photo.placeholder{display:flex;align-items:center;justify-content:center;font-size:11px;color:#6b7280;background:#f8fafc;}
        .marks-table{width:100%;border-collapse:collapse;margin-top:12px;}
        .marks-table th,.marks-table td{border:1px solid var(--line);padding:6px;font-size:12px;text-align:center;}
        .marks-table th{background:var(--accent-soft);font-weight:700;}
        .marks-table td:first-child,.marks-table th:first-child{text-align:left;}
        .muted{color:#6b7280;text-align:center;}
        .summary-row{display:flex;flex-wrap:wrap;gap:12px;margin-top:12px;font-size:12px;}
        .signature-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:16px;margin-top:20px;}
        .signature-item{text-align:center;}
        .signature-item img{max-width:140px;max-height:52px;object-fit:contain;display:block;margin:0 auto 6px;}
        .signature-space{height:52px;border-bottom:1px dashed #9ca3af;margin-bottom:6px;}
        .signature-item span{font-size:11px;color:#374151;}
        .report-sheet.layout-2up .report-card,
        .report-sheet.layout-4up .report-card{padding:10px;}
        .report-sheet.layout-2up .report-header h1{font-size:18px;}
        .report-sheet.layout-2up .report-header h2{font-size:14px;margin-top:8px;}
        .report-sheet.layout-2up .student-meta p,
        .report-sheet.layout-2up .marks-table th,
        .report-sheet.layout-2up .marks-table td,
        .report-sheet.layout-2up .summary-row{font-size:10px;}
        .report-sheet.layout-2up .school-logo{width:48px;height:48px;}
        .report-sheet.layout-2up .student-photo{width:56px;height:56px;}
        .report-sheet.layout-2up .signature-item img{max-width:90px;max-height:34px;}
        .report-sheet.layout-4up .report-header h1{font-size:14px;}
        .report-sheet.layout-4up .report-header h2{font-size:11px;margin-top:6px;}
        .report-sheet.layout-4up .report-header p{font-size:9px;}
        .report-sheet.layout-4up .student-meta p,
        .report-sheet.layout-4up .marks-table th,
        .report-sheet.layout-4up .marks-table td,
        .report-sheet.layout-4up .summary-row{font-size:8px;}
        .report-sheet.layout-4up .school-logo{width:34px;height:34px;border-radius:6px;}
        .report-sheet.layout-4up .student-photo{width:38px;height:38px;border-radius:6px;}
        .report-sheet.layout-4up .student-photo-wrap{width:42px;}
        .report-sheet.layout-4up .signature-grid{gap:8px;margin-top:10px;}
        .report-sheet.layout-4up .signature-item img{max-width:56px;max-height:20px;}
        .report-sheet.layout-4up .signature-space{height:22px;}
        .report-sheet.layout-4up .signature-item span{font-size:8px;}
        @media print{
            body{background:#fff;padding:0;}
            .report-sheet{min-height:auto;}
        }
    </style>
</head>
<body data-layout="' . rcH($layoutMode) . '" data-theme="' . rcH($colorVariant) . '">' . implode("\n", $sheetHtml) . '</body>
</html>';

    if (file_put_contents($outputPath, $documentHtml) === false) {
        jsonResponse(500, [
            'success' => false,
            'message' => 'Failed to write generated report card file'
        ]);
    }

    jsonResponse(200, [
        'success' => true,
        'message' => 'Report cards generated successfully',
        'data' => [
            'download_url' => rcPublicPath($outputPath),
            'report_cards' => $cardSummaries,
            'print_preferences' => [
                'layout_mode' => $layoutMode,
                'cards_per_page' => $cardsPerPage,
                'color_variant' => $colorVariant,
                'paper_size' => 'A4',
                'include_school_logo' => $includeLogo
            ]
        ]
    ]);
} catch (Throwable $e) {
    error_log('api/tezzerp/report-cards-generate failed: ' . $e->getMessage());
    jsonResponse(500, [
        'success' => false,
        'message' => 'Failed to generate report cards'
    ]);
}
