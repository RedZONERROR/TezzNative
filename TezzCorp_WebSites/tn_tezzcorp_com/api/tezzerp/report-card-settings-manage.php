<?php
declare(strict_types=1);

require_once __DIR__ . '/_common.php';

ensureMethod('POST');
$pdo = apiDb();
$userId = requireAuthenticatedUserId();
$user = fetchUserContext($pdo, $userId);
requireAnyPermission($user, ['set-results', 'report-card', 'site-settings']);

$action = strtolower(trim((string) ($_POST['action'] ?? 'save')));
if (!in_array($action, ['save'], true)) {
    jsonResponse(400, [
        'success' => false,
        'message' => 'Unsupported action'
    ]);
}

function parseTermsInput(string $raw): array
{
    $text = trim($raw);
    if ($text === '') {
        return [];
    }

    $decoded = json_decode($text, true);
    if (!is_array($decoded)) {
        $decoded = preg_split('/\s*,\s*/', $text) ?: [];
    }

    $terms = [];
    foreach ($decoded as $value) {
        $term = trim((string) $value);
        if ($term === '') {
            continue;
        }
        if (mb_strlen($term) > 40) {
            $term = mb_substr($term, 0, 40);
        }
        if (!in_array($term, $terms, true)) {
            $terms[] = $term;
        }
    }

    return array_slice($terms, 0, 12);
}

function parseGradeRulesInput(string $raw): array
{
    $text = trim($raw);
    if ($text === '') {
        return [];
    }

    $decoded = json_decode($text, true);
    if (!is_array($decoded)) {
        $decoded = [];
        $chunks = preg_split('/[\r\n,]+/', $text) ?: [];
        foreach ($chunks as $chunk) {
            $line = trim((string) $chunk);
            if ($line === '') {
                continue;
            }
            if (!preg_match('/^\s*([^:]+)\s*:\s*(-?\d+(?:\.\d+)?)\s*-\s*(-?\d+(?:\.\d+)?)\s*$/', $line, $match)) {
                continue;
            }
            $decoded[] = [
                'label' => trim((string) ($match[1] ?? '')),
                'min' => (float) ($match[2] ?? 0),
                'max' => (float) ($match[3] ?? 0),
            ];
        }
    }

    $rules = [];
    foreach ($decoded as $item) {
        if (!is_array($item)) {
            continue;
        }
        $label = trim((string) ($item['label'] ?? ''));
        $min = $item['min'] ?? null;
        $max = $item['max'] ?? null;
        if ($label === '' || !is_numeric($min) || !is_numeric($max)) {
            continue;
        }
        $minNum = max(0.0, min(100.0, (float) $min));
        $maxNum = max(0.0, min(100.0, (float) $max));
        if ($maxNum < $minNum) {
            continue;
        }
        if (mb_strlen($label) > 12) {
            $label = mb_substr($label, 0, 12);
        }
        $rules[] = [
            'label' => $label,
            'min' => $minNum,
            'max' => $maxNum
        ];
    }

    usort($rules, static fn(array $a, array $b): int => ($a['min'] < $b['min']) ? 1 : -1);
    return array_slice($rules, 0, 20);
}

try {
    $terms = parseTermsInput((string) ($_POST['terms'] ?? ''));
    if ($terms === []) {
        $terms = ['FIRST TERMINAL', 'SECOND TERMINAL', 'ANNUAL'];
    }

    $minMarks = is_numeric($_POST['min_marks'] ?? null) ? (int) $_POST['min_marks'] : 0;
    $maxMarks = is_numeric($_POST['max_marks'] ?? null) ? (int) $_POST['max_marks'] : 1000;

    if ($minMarks < 0) {
        $minMarks = 0;
    }
    if ($maxMarks <= $minMarks) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Max marks must be greater than minimum marks'
        ]);
    }

    $gradeRules = parseGradeRulesInput((string) ($_POST['grade_rules'] ?? ''));

    apiSystemSettingsSet($pdo, 'results_term_labels_json', json_encode($terms, JSON_UNESCAPED_UNICODE));
    apiSystemSettingsSet($pdo, 'results_min_marks', (string) $minMarks);
    apiSystemSettingsSet($pdo, 'results_max_marks', (string) $maxMarks);
    if ($gradeRules !== []) {
        apiSystemSettingsSet($pdo, 'report_card_grade_rules_json', json_encode($gradeRules, JSON_UNESCAPED_UNICODE));
    }

    erpSendAdminNotification(
        $pdo,
        'Result policy updated',
        'Exam terms, marks policy, and grading rules were updated from Results Entry settings.',
        'info'
    );

    jsonResponse(200, [
        'success' => true,
        'message' => 'Result/report settings saved successfully',
        'data' => [
            'terms' => $terms,
            'exam_cycles' => count($terms),
            'marks_policy' => [
                'min_marks' => $minMarks,
                'max_marks' => $maxMarks
            ],
            'grade_rules' => $gradeRules
        ]
    ]);
} catch (Throwable $e) {
    error_log('api/tezzerp/report-card-settings-manage failed: ' . $e->getMessage());
    jsonResponse(500, [
        'success' => false,
        'message' => 'Failed to save result/report settings'
    ]);
}
