<?php
declare(strict_types=1);

require_once '../config/config.php';

header('Content-Type: application/json; charset=utf-8');

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    http_response_code(405);
    echo json_encode(['ok' => false, 'message' => 'method_not_allowed']);
    exit;
}

try {
    $raw = file_get_contents('php://input') ?: '';
    $data = json_decode($raw, true);
    if (!is_array($data)) {
        $data = $_POST;
    }

    $name = trim((string)($data['name'] ?? ''));
    $email = trim((string)($data['email'] ?? ''));
    $phone = trim((string)($data['phone'] ?? ''));
    $subject = trim((string)($data['subject'] ?? ''));
    $message = trim((string)($data['message'] ?? ''));

    if ($name === '' || $email === '' || $message === '') {
        http_response_code(422);
        echo json_encode(['ok' => false, 'message' => 'name_email_message_required']);
        exit;
    }
    if (!filter_var($email, FILTER_VALIDATE_EMAIL)) {
        http_response_code(422);
        echo json_encode(['ok' => false, 'message' => 'invalid_email']);
        exit;
    }

    $phoneClean = preg_replace('/[^\d+]/', '', $phone);
    if ($phoneClean !== '' && strlen(preg_replace('/\D+/', '', $phoneClean)) < 8) {
        http_response_code(422);
        echo json_encode(['ok' => false, 'message' => 'invalid_phone']);
        exit;
    }

    $pdo = db();
    $orgStmt = $pdo->query("
        SELECT id
        FROM organizations
        WHERE status = 'active'
        ORDER BY CASE WHEN domain = 'tezzcorp.com' THEN 0 ELSE 1 END, created_at ASC
        LIMIT 1
    ");
    $orgId = (string)($orgStmt->fetchColumn() ?: '');
    if ($orgId === '') {
        throw new RuntimeException('No active organization found');
    }

    $leadId = uuid32();
    $source = 'website_contact';
    $notes = trim($subject . "\n\n" . $message);

    $stmt = $pdo->prepare("
        INSERT INTO leads
        (id, organization_id, source, status, company_name, contact_name, email, phone, assigned_to, notes, created_at, updated_at)
        VALUES
        (:id, :org, :source, 'new', NULL, :contact_name, :email, :phone, NULL, :notes, UTC_TIMESTAMP(3), UTC_TIMESTAMP(3))
    ");
    $stmt->execute([
        ':id' => $leadId,
        ':org' => $orgId,
        ':source' => $source,
        ':contact_name' => $name,
        ':email' => $email,
        ':phone' => ($phoneClean !== '' ? $phoneClean : null),
        ':notes' => $notes,
    ]);

    echo json_encode([
        'ok' => true,
        'message' => 'message_received',
        'lead_id' => $leadId,
    ], JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
} catch (Throwable $e) {
    error_log('[contact.api] ' . $e->getMessage() . PHP_EOL, 3, LOG_PATH . '/error.log');
    http_response_code(500);
    echo json_encode(['ok' => false, 'message' => 'server_error']);
}
?>
