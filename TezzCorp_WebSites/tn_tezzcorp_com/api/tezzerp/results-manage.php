<?php
declare(strict_types=1);

require_once __DIR__ . '/_common.php';

ensureMethod('POST');
$pdo = apiDb();
apiEnsureResultsSchema($pdo);
$userId = requireAuthenticatedUserId();
$user = fetchUserContext($pdo, $userId);
requireAnyPermission($user, ['set-results', 'report-card']);

$action = strtolower(trim((string) ($_POST['action'] ?? 'upsert')));
if (!in_array($action, ['upsert', 'delete'], true)) {
    jsonResponse(400, [
        'success' => false,
        'message' => 'Unsupported action'
    ]);
}

function fetchResultRow(PDO $pdo, int $id): array
{
    $stmt = $pdo->prepare(
        "SELECT r.id,
                r.uid,
                r.class_id,
                r.session,
                r.term,
                r.subject_id,
                COALESCE(NULLIF(s.subject, ''), CONCAT('Subject ', r.subject_id)) AS subject_name,
                COALESCE(r.marks_obtained, 0) AS marks_obtained,
                COALESCE(r.max_marks, 0) AS max_marks,
                CASE
                    WHEN COALESCE(r.max_marks, 0) > 0 THEN ROUND((COALESCE(r.marks_obtained, 0) * 100) / r.max_marks, 2)
                    ELSE 0
                END AS percentage,
                r.updated_at
         FROM results r
         LEFT JOIN subject s ON s.id = r.subject_id
         WHERE r.id = :id
         LIMIT 1"
    );
    $stmt->execute([':id' => $id]);
    return $stmt->fetch(PDO::FETCH_ASSOC) ?: [];
}

function resultConfiguredTerms(PDO $pdo): array
{
    $raw = apiSystemSettingsGetFirst($pdo, [
        'results_term_labels_json',
        'result_terms_json',
        'exam_terms_json'
    ]);
    if ($raw === '') {
        return ['FIRST TERMINAL', 'SECOND TERMINAL', 'ANNUAL'];
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

    return $terms !== [] ? $terms : ['FIRST TERMINAL', 'SECOND TERMINAL', 'ANNUAL'];
}

function resultMarksPolicy(PDO $pdo): array
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

if ($action === 'delete') {
    $id = (int) ($_POST['id'] ?? 0);
    if ($id <= 0) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Result ID is required'
        ]);
    }

    try {
        $deleteStmt = $pdo->prepare('DELETE FROM results WHERE id = :id');
        $deleteStmt->execute([':id' => $id]);
        if ($deleteStmt->rowCount() < 1) {
            jsonResponse(404, [
                'success' => false,
                'message' => 'Result record not found'
            ]);
        }

        erpSendAdminNotification(
            $pdo,
            'Result deleted',
            'Result record #' . $id . ' was deleted from report card entries.',
            'warning'
        );

        jsonResponse(200, [
            'success' => true,
            'message' => 'Result deleted successfully'
        ]);
    } catch (Throwable $e) {
        error_log('api/v2/results-manage delete failed: ' . $e->getMessage());
        jsonResponse(500, [
            'success' => false,
            'message' => 'Failed to delete result'
        ]);
    }
}

$uid = trim((string) ($_POST['uid'] ?? ''));
$classId = (int) ($_POST['class_id'] ?? 0);
$session = trim((string) ($_POST['session'] ?? ''));
$term = trim((string) ($_POST['term'] ?? ''));
$subjectId = (int) ($_POST['subject_id'] ?? 0);
$marksObtained = (int) ($_POST['marks_obtained'] ?? -1);
$maxMarks = (int) ($_POST['max_marks'] ?? -1);

if ($uid === '') {
    jsonResponse(400, [
        'success' => false,
        'message' => 'Student UID is required'
    ]);
}
if ($classId <= 0) {
    jsonResponse(400, [
        'success' => false,
        'message' => 'Class is required'
    ]);
}
if ($session === '' || mb_strlen($session) > 20) {
    jsonResponse(400, [
        'success' => false,
        'message' => 'Session is required'
    ]);
}
$allowedTerms = resultConfiguredTerms($pdo);
if (!in_array($term, $allowedTerms, true)) {
    jsonResponse(400, [
        'success' => false,
        'message' => 'Invalid term. Configure exam cycles from result settings.'
    ]);
}
if ($subjectId <= 0) {
    jsonResponse(400, [
        'success' => false,
        'message' => 'Subject is required'
    ]);
}
$marksPolicy = resultMarksPolicy($pdo);
if ($maxMarks <= 0) {
    jsonResponse(400, [
        'success' => false,
        'message' => 'Max marks must be greater than zero'
    ]);
}
if ($maxMarks > (int) $marksPolicy['max_marks']) {
    jsonResponse(400, [
        'success' => false,
        'message' => 'Max marks exceeds configured school limit of ' . (int) $marksPolicy['max_marks']
    ]);
}
if ($marksObtained < (int) $marksPolicy['min_marks'] || $marksObtained > $maxMarks) {
    jsonResponse(400, [
        'success' => false,
        'message' => 'Marks obtained must be between '
            . (int) $marksPolicy['min_marks']
            . ' and max marks'
    ]);
}

try {
    $className = apiClassNameById($pdo, $classId);
    $studentStmt = $pdo->prepare('SELECT id, uid, class FROM students WHERE uid = :uid LIMIT 1');
    $studentStmt->execute([':uid' => $uid]);
    $student = $studentStmt->fetch(PDO::FETCH_ASSOC);
    if (!$student) {
        jsonResponse(404, [
            'success' => false,
            'message' => 'Student not found'
        ]);
    }

    $studentClassId = apiResolveClassId($pdo, $student['class'] ?? '');
    if ($studentClassId > 0 && $studentClassId !== $classId && $className !== (string) ($student['class'] ?? '')) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Selected class does not match student class'
        ]);
    }

    $subjectStmt = $pdo->prepare('SELECT id FROM subject WHERE id = :id AND class = :class_id LIMIT 1');
    $subjectStmt->execute([
        ':id' => $subjectId,
        ':class_id' => $classId
    ]);
    if (!$subjectStmt->fetch(PDO::FETCH_ASSOC)) {
        jsonResponse(404, [
            'success' => false,
            'message' => 'Subject not found for selected class'
        ]);
    }

    $sessionVariants = apiAcademicSessionVariants($session);
    if (!$sessionVariants) {
        $sessionVariants = [$session];
    }
    $canonicalSession = $sessionVariants[0];

    $lookupWhere = ['uid = :uid', 'class_id = :class_id', 'term = :term', 'subject_id = :subject_id'];
    $lookupParams = [
        ':uid' => $uid,
        ':class_id' => $classId,
        ':term' => $term,
        ':subject_id' => $subjectId
    ];
    $sessionParts = [];
    foreach ($sessionVariants as $index => $sessionVariant) {
        $key = ':lookup_session_' . $index;
        $sessionParts[] = 'session = ' . $key;
        $lookupParams[$key] = $sessionVariant;
    }
    $lookupWhere[] = '(' . implode(' OR ', $sessionParts) . ')';

    $lookupStmt = $pdo->prepare('SELECT id FROM results WHERE ' . implode(' AND ', $lookupWhere) . ' LIMIT 1');
    foreach ($lookupParams as $key => $value) {
        $lookupStmt->bindValue($key, $value);
    }
    $lookupStmt->execute();
    $existingId = (int) ($lookupStmt->fetchColumn() ?: 0);

    if ($existingId > 0) {
        $updateStmt = $pdo->prepare(
            'UPDATE results
             SET marks_obtained = :marks_obtained,
                 max_marks = :max_marks,
                 session = :session,
                 updated_at = NOW()
             WHERE id = :id'
        );
        $updateStmt->execute([
            ':id' => $existingId,
            ':marks_obtained' => $marksObtained,
            ':max_marks' => $maxMarks,
            ':session' => $canonicalSession
        ]);

        erpSendAdminNotification(
            $pdo,
            'Result updated',
            'Result updated for UID ' . $uid . ' in term ' . $term . ' (Class ' . $classId . ').',
            'info'
        );

        jsonResponse(200, [
            'success' => true,
            'message' => 'Result updated successfully',
            'data' => [
                'item' => fetchResultRow($pdo, $existingId)
            ]
        ]);
    }

    $insertStmt = $pdo->prepare(
        'INSERT INTO results (
            uid,
            class_id,
            session,
            term,
            subject_id,
            marks_obtained,
            max_marks,
            created_at
        ) VALUES (
            :uid,
            :class_id,
            :session,
            :term,
            :subject_id,
            :marks_obtained,
            :max_marks,
            NOW()
        )'
    );
    $insertStmt->execute([
        ':uid' => $uid,
        ':class_id' => $classId,
        ':session' => $canonicalSession,
        ':term' => $term,
        ':subject_id' => $subjectId,
        ':marks_obtained' => $marksObtained,
        ':max_marks' => $maxMarks
    ]);

    $createdId = (int) $pdo->lastInsertId();
    erpSendAdminNotification(
        $pdo,
        'Result saved',
        'New result saved for UID ' . $uid . ' in term ' . $term . ' (Class ' . $classId . ').',
        'good'
    );
    jsonResponse(201, [
        'success' => true,
        'message' => 'Result saved successfully',
        'data' => [
            'item' => fetchResultRow($pdo, $createdId)
        ]
    ]);
} catch (Throwable $e) {
    error_log('api/v2/results-manage failed: ' . $e->getMessage());
    jsonResponse(500, [
        'success' => false,
        'message' => 'Failed to save result'
    ]);
}
