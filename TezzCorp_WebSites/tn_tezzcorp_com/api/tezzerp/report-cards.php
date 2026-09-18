<?php
declare(strict_types=1);

require_once __DIR__ . '/_common.php';

ensureMethod('GET');
$pdo = apiDb();
$userId = requireAuthenticatedUserId();
$user = fetchUserContext($pdo, $userId);
requireAnyPermission($user, ['report-card', 'students', 'set-results']);

$scope = queryEnumParam('scope', ['options', 'students'], 'options');

function safeSessionValue(?string $value): string
{
    $normalized = trim((string) $value);
    if ($normalized === '') {
        return '';
    }
    if (strlen($normalized) > 20) {
        return substr($normalized, 0, 20);
    }
    return $normalized;
}

function reportResultsSessionColumn(PDO $pdo): string
{
    if (apiColumnExists($pdo, 'results', 'session')) {
        return 'session';
    }
    if (apiColumnExists($pdo, 'results', 'academic_session')) {
        return 'academic_session';
    }
    return '';
}

function reportCardConfiguredTerms(PDO $pdo): array
{
    $raw = apiSystemSettingsGetFirst($pdo, [
        'results_term_labels_json',
        'result_terms_json',
        'exam_terms_json'
    ]);
    if ($raw === '') {
        return [];
    }

    $decoded = json_decode($raw, true);
    if (!is_array($decoded)) {
        $decoded = preg_split('/\s*,\s*/', $raw) ?: [];
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
    return $terms;
}

function reportCardMarksPolicy(PDO $pdo): array
{
    $minRaw = apiSystemSettingsGetFirst($pdo, ['results_min_marks', 'report_min_marks']);
    $maxRaw = apiSystemSettingsGetFirst($pdo, ['results_max_marks', 'report_max_marks']);

    $minMarks = is_numeric($minRaw) ? (int) $minRaw : 0;
    $maxMarks = is_numeric($maxRaw) ? (int) $maxRaw : 1000;

    if ($minMarks < 0) {
        $minMarks = 0;
    }
    if ($maxMarks <= $minMarks) {
        $maxMarks = max(100, $minMarks + 1);
    }

    return [
        'min_marks' => $minMarks,
        'max_marks' => $maxMarks
    ];
}

function reportCardGradeRulesRaw(PDO $pdo): string
{
    $raw = apiSystemSettingsGetFirst($pdo, [
        'report_card_grade_rules_json',
        'grading_rules_json',
        'result_grade_rules_json'
    ]);
    return trim($raw);
}

try {
    $classHasStatus = apiColumnExists($pdo, 'class', 'status');
    $studentsHasStatus = apiColumnExists($pdo, 'students', 'status');
    $resultsTableExists = apiTableExists($pdo, 'results');

    $resultsSessionColumn = $resultsTableExists ? reportResultsSessionColumn($pdo) : '';
    $resultsHasTerm = $resultsTableExists && apiColumnExists($pdo, 'results', 'term');
    $resultsHasClassId = $resultsTableExists && apiColumnExists($pdo, 'results', 'class_id');
    $resultsHasClass = $resultsTableExists && apiColumnExists($pdo, 'results', 'class');
    $resultsHasUid = $resultsTableExists && apiColumnExists($pdo, 'results', 'uid');
    $resultsHasMarksObtained = $resultsTableExists && apiColumnExists($pdo, 'results', 'marks_obtained');
    $resultsHasMaxMarks = $resultsTableExists && apiColumnExists($pdo, 'results', 'max_marks');
    $canAggregateResults = $resultsTableExists
        && $resultsHasUid
        && ($resultsHasClassId || $resultsHasClass)
        && $resultsHasMarksObtained
        && $resultsHasMaxMarks;

    if ($scope === 'options') {
        $classWhere = $classHasStatus
            ? "WHERE COALESCE(NULLIF(status, ''), 'active') IN ('active', 'Active', '1')"
            : '';
        $classStmt = $pdo->query(
            "SELECT id, COALESCE(NULLIF(class_name, ''), CONCAT('Class ', id)) AS class_name
             FROM class
             {$classWhere}
             ORDER BY id ASC"
        );
        $classes = $classStmt->fetchAll(PDO::FETCH_ASSOC);

        $sessions = [];
        if ($resultsTableExists && $resultsSessionColumn !== '') {
            $sessionStmt = $pdo->query(
                "SELECT DISTINCT {$resultsSessionColumn} AS session_value
                 FROM results
                 WHERE {$resultsSessionColumn} IS NOT NULL AND {$resultsSessionColumn} != ''
                 ORDER BY {$resultsSessionColumn} DESC"
            );
            $sessions = array_values(
                array_filter(
                    array_map(
                        static fn(array $row) => safeSessionValue((string) ($row['session_value'] ?? '')),
                        $sessionStmt->fetchAll(PDO::FETCH_ASSOC)
                    ),
                    static fn(string $value): bool => $value !== ''
                )
            );
        }

        $terms = [];
        if ($resultsTableExists && $resultsHasTerm) {
            $termStmt = $pdo->query(
                "SELECT DISTINCT term
                 FROM results
                 WHERE term IS NOT NULL AND term != ''
                 ORDER BY FIELD(term, 'FIRST TERMINAL', 'SECOND TERMINAL', 'ANNUAL'), term"
            );
            $terms = array_values(
                array_filter(
                    array_map(
                        static fn(array $row) => trim((string) ($row['term'] ?? '')),
                        $termStmt->fetchAll(PDO::FETCH_ASSOC)
                    ),
                    static fn(string $value): bool => $value !== ''
                )
            );
        }

        $configuredTerms = reportCardConfiguredTerms($pdo);
        if ($configuredTerms !== []) {
            $terms = $configuredTerms;
        } elseif (!$terms) {
            $terms = ['FIRST TERMINAL', 'SECOND TERMINAL', 'ANNUAL'];
        }

        $defaultSession = safeSessionValue(apiDefaultAcademicSession($pdo));
        if (!in_array($defaultSession, $sessions, true)) {
            array_unshift($sessions, $defaultSession);
            $sessions = array_values(array_unique($sessions));
        }
        $templatePreferences = apiTemplatePreferencesForType($pdo, 'report-card');

        jsonResponse(200, [
            'success' => true,
            'message' => 'Report card options fetched',
            'data' => [
                'classes' => $classes,
                'sessions' => $sessions,
                'terms' => $terms,
                'exam_cycles' => count($terms),
                'marks_policy' => reportCardMarksPolicy($pdo),
                'grade_rules_raw' => reportCardGradeRulesRaw($pdo),
                'default_session' => $defaultSession,
                'template_preferences' => $templatePreferences
            ]
        ]);
    }

    $classId = (int) ($_GET['class_id'] ?? 0);
    if ($classId <= 0) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'class_id is required'
        ]);
    }
    $className = apiClassNameById($pdo, $classId);

    $page = queryIntParam('page', 1, 1, 100000);
    $perPage = queryIntParam('per_page', 100, 1, 500);
    $search = queryStringParam('q', '', 120);
    $session = safeSessionValue(queryStringParam('session', '', 20));
    $term = queryStringParam('term', '', 32);
    $offset = ($page - 1) * $perPage;

    $where = [];
    if ($studentsHasStatus) {
        $where[] = "COALESCE(NULLIF(s.status, ''), 'active') != 'deleted'";
    } else {
        $where[] = '1=1';
    }
    $params = [
        ':class_id_str' => (string) $classId
    ];
    if ($className !== '') {
        $where[] = '(s.class = :class_id_str OR s.class = :class_name)';
        $params[':class_name'] = $className;
    } else {
        $where[] = 's.class = :class_id_str';
    }
    if ($search !== '') {
        $where[] = '(s.uid LIKE :q OR s.full_name LIKE :q OR s.roll_no LIKE :q)';
        $params[':q'] = '%' . $search . '%';
    }
    $whereSql = implode(' AND ', $where);

    $countStmt = $pdo->prepare("SELECT COUNT(*) FROM students s WHERE {$whereSql}");
    foreach ($params as $name => $value) {
        $countStmt->bindValue($name, $value);
    }
    $countStmt->execute();
    $total = (int) $countStmt->fetchColumn();

    if ($canAggregateResults) {
        $aggWhere = [];
        $aggParams = [
            ':agg_class_id_int' => $classId,
            ':agg_class_id_str' => (string) $classId
        ];
        if ($resultsHasClassId) {
            $aggWhere[] = '(r.class_id = :agg_class_id_int OR r.class_id = :agg_class_id_str' . ($className !== '' ? ' OR r.class_id = :agg_class_name' : '') . ')';
        } else {
            $aggWhere[] = '(r.class = :agg_class_id_str' . ($className !== '' ? ' OR r.class = :agg_class_name' : '') . ')';
        }
        if ($className !== '') {
            $aggParams[':agg_class_name'] = $className;
        }

        if ($resultsSessionColumn !== '' && $session !== '') {
            $sessionVariants = apiAcademicSessionVariants($session);
            if ($sessionVariants) {
                $sessionParts = [];
                foreach ($sessionVariants as $index => $sessionVariant) {
                    $key = ':agg_session_' . $index;
                    $sessionParts[] = 'r.' . $resultsSessionColumn . ' = ' . $key;
                    $aggParams[$key] = $sessionVariant;
                }
                $aggWhere[] = '(' . implode(' OR ', $sessionParts) . ')';
            }
        }
        if ($resultsHasTerm && $term !== '') {
            $aggWhere[] = 'r.term = :agg_term';
            $aggParams[':agg_term'] = $term;
        }
        $aggWhereSql = implode(' AND ', $aggWhere);

        $sql = "
            SELECT s.id,
                   s.uid,
                   COALESCE(NULLIF(s.full_name, ''), 'Unknown Student') AS full_name,
                   COALESCE(NULLIF(s.roll_no, ''), '-') AS roll_no,
                   COALESCE(NULLIF(c.class_name, ''), NULLIF(s.class, ''), CONCAT('Class ', :class_id_for_name)) AS class_name,
                   COALESCE(a.subjects_count, 0) AS subjects_count,
                   COALESCE(a.total_marks, 0) AS total_marks,
                   COALESCE(a.max_marks, 0) AS max_marks,
                   CASE
                       WHEN COALESCE(a.max_marks, 0) > 0 THEN ROUND((COALESCE(a.total_marks, 0) * 100) / a.max_marks, 2)
                       ELSE 0
                   END AS percentage
            FROM students s
            LEFT JOIN class c ON (c.id = CAST(s.class AS UNSIGNED) OR c.class_name = s.class)
            LEFT JOIN (
                SELECT r.uid,
                       COUNT(*) AS subjects_count,
                       SUM(COALESCE(r.marks_obtained, 0)) AS total_marks,
                       SUM(COALESCE(r.max_marks, 0)) AS max_marks
                FROM results r
                WHERE {$aggWhereSql}
                GROUP BY r.uid
            ) a ON a.uid = s.uid
            WHERE {$whereSql}
            ORDER BY
                CASE
                    WHEN s.roll_no REGEXP '^[0-9]+$' THEN CAST(s.roll_no AS UNSIGNED)
                    ELSE 999999
                END ASC,
                s.roll_no ASC,
                s.full_name ASC
            LIMIT :limit OFFSET :offset
        ";

        $listStmt = $pdo->prepare($sql);
        $listStmt->bindValue(':class_id_for_name', $classId, PDO::PARAM_INT);
        foreach ($aggParams as $name => $value) {
            $listStmt->bindValue($name, $value);
        }
    } else {
        $sql = "
            SELECT s.id,
                   s.uid,
                   COALESCE(NULLIF(s.full_name, ''), 'Unknown Student') AS full_name,
                   COALESCE(NULLIF(s.roll_no, ''), '-') AS roll_no,
                   COALESCE(NULLIF(c.class_name, ''), NULLIF(s.class, ''), CONCAT('Class ', :class_id_for_name)) AS class_name,
                   0 AS subjects_count,
                   0 AS total_marks,
                   0 AS max_marks,
                   0 AS percentage
            FROM students s
            LEFT JOIN class c ON (c.id = CAST(s.class AS UNSIGNED) OR c.class_name = s.class)
            WHERE {$whereSql}
            ORDER BY
                CASE
                    WHEN s.roll_no REGEXP '^[0-9]+$' THEN CAST(s.roll_no AS UNSIGNED)
                    ELSE 999999
                END ASC,
                s.roll_no ASC,
                s.full_name ASC
            LIMIT :limit OFFSET :offset
        ";
        $listStmt = $pdo->prepare($sql);
        $listStmt->bindValue(':class_id_for_name', $classId, PDO::PARAM_INT);
    }

    foreach ($params as $name => $value) {
        $listStmt->bindValue($name, $value);
    }
    $listStmt->bindValue(':limit', $perPage, PDO::PARAM_INT);
    $listStmt->bindValue(':offset', $offset, PDO::PARAM_INT);
    $listStmt->execute();
    $items = $listStmt->fetchAll(PDO::FETCH_ASSOC);

    $selectedCount = 0;
    foreach ($items as $item) {
        if ((int) ($item['subjects_count'] ?? 0) > 0) {
            $selectedCount += 1;
        }
    }

    jsonResponse(200, [
        'success' => true,
        'message' => 'Report card students fetched',
        'data' => [
            'items' => $items,
            'summary' => [
                'total' => $total,
                'with_results' => $selectedCount
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
                'session' => $session,
                'term' => $term
            ]
        ]
    ]);
} catch (Throwable $e) {
    error_log('api/v2/report-cards failed: ' . $e->getMessage());
    jsonResponse(500, [
        'success' => false,
        'message' => 'Failed to load report card workspace'
    ]);
}
