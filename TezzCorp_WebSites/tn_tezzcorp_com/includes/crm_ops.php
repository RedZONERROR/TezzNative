<?php
require_once __DIR__ . '/../config/config.php';
require_once __DIR__ . '/school_erp.php';

$crmOpsAutoload = __DIR__ . '/../vendor/autoload.php';
if (is_readable($crmOpsAutoload)) {
    require_once $crmOpsAutoload;
}

use PHPMailer\PHPMailer\PHPMailer;
use PHPMailer\PHPMailer\Exception as PHPMailerException;

function crmOpsDecodeList($raw): array {
    if (is_array($raw)) {
        $items = $raw;
    } else {
        $text = trim((string)$raw);
        if ($text === '') {
            return [];
        }
        $json = json_decode($text, true);
        if (is_array($json)) {
            $items = $json;
        } else {
            $items = preg_split('/\r\n|\r|\n|,/', $text) ?: [];
        }
    }

    $out = [];
    foreach ($items as $item) {
        $value = trim((string)$item);
        if ($value !== '') {
            $out[] = $value;
        }
    }
    return array_values(array_unique($out));
}

function crmOpsNormalizeChannels($channels): array {
    $allowed = ['in_app', 'email', 'sms'];
    $values = crmOpsDecodeList($channels);
    if (empty($values)) {
        return ['in_app'];
    }

    $out = [];
    foreach ($values as $value) {
        $channel = strtolower(trim($value));
        if (in_array($channel, $allowed, true) && !in_array($channel, $out, true)) {
            $out[] = $channel;
        }
    }

    return !empty($out) ? $out : ['in_app'];
}

function crmOpsNowUtc(): string {
    return gmdate('Y-m-d H:i:s') . '.000';
}

function crmOpsHttpPostJson(string $url, array $payload, array $headers = [], int $timeoutSeconds = 12): array {
    $url = trim($url);
    if ($url === '') {
        return ['ok' => false, 'status_code' => 0, 'body' => '', 'headers' => [], 'error' => 'missing_url'];
    }

    $baseHeaders = ["Content-Type: application/json"];
    foreach ($headers as $header) {
        $headerText = trim((string)$header);
        if ($headerText !== '') {
            $baseHeaders[] = $headerText;
        }
    }

    $json = json_encode($payload, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
    if ($json === false) {
        return ['ok' => false, 'status_code' => 0, 'body' => '', 'headers' => [], 'error' => 'json_encode_failed'];
    }

    $responseBody = '';
    $responseHeaders = [];
    $statusCode = 0;
    $error = '';

    if (function_exists('curl_init')) {
        $ch = curl_init($url);
        if ($ch === false) {
            return ['ok' => false, 'status_code' => 0, 'body' => '', 'headers' => [], 'error' => 'curl_init_failed'];
        }
        curl_setopt_array($ch, [
            CURLOPT_POST => true,
            CURLOPT_RETURNTRANSFER => true,
            CURLOPT_HTTPHEADER => $baseHeaders,
            CURLOPT_POSTFIELDS => $json,
            CURLOPT_TIMEOUT => max(1, $timeoutSeconds),
            CURLOPT_CONNECTTIMEOUT => max(1, min($timeoutSeconds, 10)),
            CURLOPT_FOLLOWLOCATION => false,
            CURLOPT_HEADER => false,
        ]);
        $body = curl_exec($ch);
        if ($body === false) {
            $error = (string)curl_error($ch);
            $responseBody = '';
        } else {
            $responseBody = (string)$body;
        }
        $statusCode = (int)curl_getinfo($ch, CURLINFO_RESPONSE_CODE);
        curl_close($ch);
    } else {
        $context = stream_context_create([
            'http' => [
                'method' => 'POST',
                'header' => implode("\r\n", $baseHeaders),
                'content' => $json,
                'ignore_errors' => true,
                'timeout' => max(1, $timeoutSeconds),
            ],
        ]);

        $fp = @fopen($url, 'r', false, $context);
        if ($fp !== false) {
            $meta = stream_get_meta_data($fp);
            $responseHeaders = is_array($meta['wrapper_data'] ?? null) ? $meta['wrapper_data'] : [];
            $body = stream_get_contents($fp);
            $responseBody = $body === false ? '' : (string)$body;
            fclose($fp);
        } else {
            $body = false;
            $responseBody = '';
        }
        if (!empty($responseHeaders[0]) && preg_match('/\s(\d{3})\s/', (string)$responseHeaders[0], $m)) {
            $statusCode = (int)$m[1];
        }
        if ($body === false && $statusCode === 0) {
            $error = 'request_failed';
        }
    }

    return [
        'ok' => $statusCode >= 200 && $statusCode < 300,
        'status_code' => $statusCode,
        'body' => trim($responseBody),
        'headers' => $responseHeaders,
        'error' => $statusCode > 0 ? '' : ($error !== '' ? $error : 'request_failed'),
    ];
}

function crmOpsChannelMaxAttempts(string $channel): int {
    $channel = strtolower(trim($channel));
    if ($channel === 'email') {
        return max(1, min((int)env('EMAIL_RETRY_MAX_ATTEMPTS', '5'), 12));
    }
    if ($channel === 'sms') {
        return max(1, min((int)env('SMS_RETRY_MAX_ATTEMPTS', '5'), 12));
    }
    return 1;
}

function crmOpsRetryBackoffSeconds(int $attemptCount): int {
    $base = max(20, (int)env('NOTIFY_RETRY_BASE_SECONDS', '120'));
    $max = max($base, (int)env('NOTIFY_RETRY_MAX_SECONDS', '3600'));
    $pow = max(0, $attemptCount - 1);
    $delay = $base * (2 ** $pow);
    if ($delay > $max) {
        $delay = $max;
    }
    return (int)$delay;
}

function crmOpsNextRetryAt(int $attemptCount, int $maxAttempts): ?string {
    if ($attemptCount >= $maxAttempts) {
        return null;
    }
    $delay = crmOpsRetryBackoffSeconds($attemptCount);
    return gmdate('Y-m-d H:i:s', time() + $delay) . '.000';
}

function crmOpsWebhookMaxAttempts(): int {
    return max(1, min((int)env('WEBHOOK_RETRY_MAX_ATTEMPTS', '5'), 12));
}

function crmOpsEndpointWantsEvent(string $eventName, $eventsJson): bool {
    $rules = crmOpsDecodeList($eventsJson);
    if (empty($rules)) {
        return true;
    }

    foreach ($rules as $rule) {
        $rule = strtolower(trim($rule));
        if ($rule === '' || $rule === '*') {
            return true;
        }
        if ($rule === strtolower($eventName)) {
            return true;
        }
        if (str_ends_with($rule, '.*')) {
            $prefix = substr($rule, 0, -2);
            if ($prefix !== '' && str_starts_with(strtolower($eventName), $prefix . '.')) {
                return true;
            }
        }
    }
    return false;
}

function crmOpsWebhookEmit(PDO $pdo, string $orgId, string $eventName, ?string $entityType = null, ?string $entityId = null, array $payload = []): ?string {
    $orgId = trim($orgId);
    $eventName = strtolower(trim($eventName));
    if ($orgId === '' || $eventName === '') {
        return null;
    }

    $eventId = uuid32();
    $payloadJson = json_encode($payload, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
    if ($payloadJson === false) {
        $payloadJson = '{}';
    }

    try {
        $st = $pdo->prepare("
            INSERT INTO crm_webhook_events
              (id, organization_id, event_name, entity_type, entity_id, payload_json, status, attempt_count, max_attempts, next_retry_at, last_error, delivered_at, created_at, updated_at)
            VALUES
              (:id, :org, :event_name, :entity_type, :entity_id, :payload_json, 'queued', 0, :max_attempts, UTC_TIMESTAMP(3), NULL, NULL, UTC_TIMESTAMP(3), UTC_TIMESTAMP(3))
        ");
        $st->execute([
            ':id' => $eventId,
            ':org' => $orgId,
            ':event_name' => $eventName,
            ':entity_type' => $entityType !== null && trim($entityType) !== '' ? trim($entityType) : null,
            ':entity_id' => $entityId !== null && trim($entityId) !== '' ? trim($entityId) : null,
            ':payload_json' => $payloadJson,
            ':max_attempts' => crmOpsWebhookMaxAttempts(),
        ]);
        return $eventId;
    } catch (Throwable $e) {
        // Ignore when webhook tables are not available yet.
        error_log('[crmOpsWebhookEmit] ' . $e->getMessage() . PHP_EOL, 3, LOG_PATH . '/error.log');
        return null;
    }
}

function crmOpsAuditLog(PDO $pdo, string $orgId, ?string $actorUserId, string $action, string $entityType, ?string $entityId, array $oldData = [], array $newData = []): void {
    try {
        $stmt = $pdo->prepare("
            INSERT INTO audit_logs
              (id, organization_id, actor_user_id, action, entity_type, entity_id, ip_address, user_agent, old_data, new_data, created_at)
            VALUES
              (:id, :org, :actor, :action, :etype, :eid, :ip, :ua, :old_data, :new_data, UTC_TIMESTAMP(3))
        ");
        $stmt->execute([
            ':id' => uuid32(),
            ':org' => $orgId,
            ':actor' => $actorUserId ?: null,
            ':action' => $action,
            ':etype' => $entityType,
            ':eid' => $entityId ?: null,
            ':ip' => $_SERVER['REMOTE_ADDR'] ?? null,
            ':ua' => $_SERVER['HTTP_USER_AGENT'] ?? null,
            ':old_data' => !empty($oldData) ? json_encode($oldData, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE) : null,
            ':new_data' => !empty($newData) ? json_encode($newData, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE) : null,
        ]);
    } catch (Throwable $e) {
        error_log('[crmOpsAuditLog] ' . $e->getMessage() . PHP_EOL, 3, LOG_PATH . '/error.log');
    }
}

function crmOpsInsertDelivery(
    PDO $pdo,
    string $notificationId,
    string $channel,
    string $status,
    ?string $provider = null,
    ?string $responseText = null,
    ?string $externalId = null,
    int $attemptCount = 1,
    int $maxAttempts = 5,
    ?string $nextRetryAt = null,
    ?string $lastError = null
): void {
    $id = uuid32();
    $deliveredAt = in_array($status, ['sent', 'skipped'], true) ? crmOpsNowUtc() : null;
    $maxAttempts = max(1, $maxAttempts);
    $attemptCount = max(1, $attemptCount);
    $lastAttemptAt = crmOpsNowUtc();

    try {
        $stmt = $pdo->prepare("
            INSERT INTO crm_notification_deliveries
              (id, notification_id, channel, status, attempt_count, max_attempts, provider, external_id, response_text, next_retry_at, last_attempt_at, last_error, delivered_at, created_at, updated_at)
            VALUES
              (:id, :nid, :channel, :status, :attempt_count, :max_attempts, :provider, :external_id, :response_text, :next_retry_at, :last_attempt_at, :last_error, :delivered_at, UTC_TIMESTAMP(3), UTC_TIMESTAMP(3))
        ");
        $stmt->execute([
            ':id' => $id,
            ':nid' => $notificationId,
            ':channel' => $channel,
            ':status' => $status,
            ':attempt_count' => $attemptCount,
            ':max_attempts' => $maxAttempts,
            ':provider' => $provider,
            ':external_id' => $externalId,
            ':response_text' => $responseText,
            ':next_retry_at' => $nextRetryAt,
            ':last_attempt_at' => $lastAttemptAt,
            ':last_error' => $lastError,
            ':delivered_at' => $deliveredAt,
        ]);
        return;
    } catch (Throwable $e) {
        // Fallback for older schema (without retry metadata columns).
        $stmt = $pdo->prepare("
            INSERT INTO crm_notification_deliveries
              (id, notification_id, channel, status, provider, external_id, response_text, delivered_at, created_at)
            VALUES
              (:id, :nid, :channel, :status, :provider, :external_id, :response_text, :delivered_at, UTC_TIMESTAMP(3))
        ");
        $stmt->execute([
            ':id' => $id,
            ':nid' => $notificationId,
            ':channel' => $channel,
            ':status' => $status,
            ':provider' => $provider,
            ':external_id' => $externalId,
            ':response_text' => $responseText,
            ':delivered_at' => $deliveredAt,
        ]);
    }
}

function crmOpsSendEmailMessage(string $toEmail, string $toName, string $subject, string $html): array {
    $toEmail = trim($toEmail);
    if ($toEmail === '' || !filter_var($toEmail, FILTER_VALIDATE_EMAIL)) {
        return ['status' => 'skipped', 'provider' => 'smtp', 'response' => 'invalid_email'];
    }

    $host = env('SMTP_HOST', '');
    $port = (int)env('SMTP_PORT', '587');
    $username = env('SMTP_USERNAME', '');
    $password = env('SMTP_PASSWORD', '');
    $secure = strtolower(env('SMTP_SECURE', 'tls'));
    $fromEmail = env('EMAIL_FROM', 'no-reply@localhost');
    $fromName = env('EMAIL_FROM_NAME', 'TezzCorp CRM');

    if ($host === '' || $username === '' || $password === '') {
        return ['status' => 'skipped', 'provider' => 'smtp', 'response' => 'smtp_not_configured'];
    }

    if (!class_exists(PHPMailer::class)) {
        return ['status' => 'failed', 'provider' => 'smtp', 'response' => 'phpmailer_missing'];
    }

    $mail = new PHPMailer(true);
    try {
        $mail->isSMTP();
        $mail->Host = $host;
        $mail->SMTPAuth = true;
        $mail->Username = $username;
        $mail->Password = $password;
        $mail->Port = $port > 0 ? $port : 587;
        if ($secure === 'ssl') {
            $mail->SMTPSecure = PHPMailer::ENCRYPTION_SMTPS;
        } else {
            $mail->SMTPSecure = PHPMailer::ENCRYPTION_STARTTLS;
        }

        $mail->setFrom($fromEmail, $fromName);
        $mail->addAddress($toEmail, $toName);
        $mail->isHTML(true);
        $mail->Subject = $subject;
        $mail->Body = $html;
        $mail->AltBody = trim(strip_tags($html));
        $mail->send();

        return ['status' => 'sent', 'provider' => 'smtp', 'response' => 'ok'];
    } catch (PHPMailerException $e) {
        return ['status' => 'failed', 'provider' => 'smtp', 'response' => $e->getMessage()];
    } catch (Throwable $e) {
        return ['status' => 'failed', 'provider' => 'smtp', 'response' => $e->getMessage()];
    }
}

function crmOpsSendSmsMessage(string $phone, string $message): array {
    $phone = trim($phone);
    if ($phone === '') {
        return ['status' => 'skipped', 'provider' => 'sms', 'response' => 'missing_phone'];
    }

    $url = trim(env('SMS_API_URL', ''));
    $apiKey = trim(env('SMS_API_KEY', ''));
    $sender = trim(env('SMS_SENDER_ID', 'TEZZCR'));
    $provider = trim(env('SMS_PROVIDER', 'custom_sms_http'));

    if ($url === '') {
        return ['status' => 'skipped', 'provider' => $provider, 'response' => 'sms_not_configured'];
    }

    $payload = [
        'to' => $phone,
        'message' => $message,
        'sender' => $sender,
    ];

    $headers = ["Content-Type: application/json"];
    if ($apiKey !== '') {
        $headers[] = "Authorization: Bearer {$apiKey}";
        $headers[] = "X-API-Key: {$apiKey}";
    }

    $http = crmOpsHttpPostJson($url, $payload, $headers, 12);
    if (!empty($http['ok'])) {
        $body = trim((string)($http['body'] ?? ''));
        return ['status' => 'sent', 'provider' => $provider, 'response' => $body !== '' ? $body : 'ok'];
    }

    $statusCode = (int)($http['status_code'] ?? 0);
    $body = trim((string)($http['body'] ?? ''));
    return [
        'status' => 'failed',
        'provider' => $provider,
        'response' => ($statusCode > 0 ? "http_{$statusCode}" : 'request_failed') . ($body !== '' ? " {$body}" : ''),
    ];
}

function crmOpsDispatchNotification(PDO $pdo, string $notificationId): void {
    $st = $pdo->prepare("
        SELECT
          n.id,
          n.organization_id,
          n.user_id,
          n.title,
          n.message,
          n.type,
          n.link_url,
          n.channels_json,
          u.name AS user_name,
          u.email AS user_email,
          u.phone AS user_phone
        FROM crm_notifications n
        JOIN users u ON u.id = n.user_id
        WHERE n.id = :id
        LIMIT 1
    ");
    $st->execute([':id' => $notificationId]);
    $row = $st->fetch(PDO::FETCH_ASSOC);
    if (!$row) {
        return;
    }

    $channels = crmOpsNormalizeChannels($row['channels_json'] ?? '["in_app"]');

    foreach ($channels as $channel) {
        try {
            $maxAttempts = crmOpsChannelMaxAttempts($channel);
            if ($channel === 'in_app') {
                crmOpsInsertDelivery(
                    $pdo,
                    $notificationId,
                    'in_app',
                    'sent',
                    'crm_in_app',
                    'stored',
                    null,
                    1,
                    1,
                    null,
                    null
                );
                continue;
            }

            if ($channel === 'email') {
                $subject = '[TezzCorp CRM] ' . $row['title'];
                $body = '<h3>' . htmlspecialchars((string)$row['title'], ENT_QUOTES, 'UTF-8') . '</h3>'
                      . '<p>' . nl2br(htmlspecialchars((string)$row['message'], ENT_QUOTES, 'UTF-8')) . '</p>';
                if (!empty($row['link_url'])) {
                    $body .= '<p><a href="' . htmlspecialchars((string)$row['link_url'], ENT_QUOTES, 'UTF-8') . '">Open in CRM</a></p>';
                }
                $res = crmOpsSendEmailMessage((string)$row['user_email'], (string)$row['user_name'], $subject, $body);
                $status = (string)($res['status'] ?? 'failed');
                $attempt = 1;
                $nextRetry = $status === 'failed' ? crmOpsNextRetryAt($attempt, $maxAttempts) : null;
                $lastError = $status === 'failed' ? (string)($res['response'] ?? 'email_failed') : null;
                crmOpsInsertDelivery(
                    $pdo,
                    $notificationId,
                    'email',
                    $status,
                    $res['provider'] ?? 'smtp',
                    $res['response'] ?? '',
                    null,
                    $attempt,
                    $maxAttempts,
                    $nextRetry,
                    $lastError
                );
                continue;
            }

            if ($channel === 'sms') {
                $sms = (string)$row['title'] . ': ' . (string)$row['message'];
                if (!empty($row['link_url'])) {
                    $sms .= ' ' . (string)$row['link_url'];
                }
                $res = crmOpsSendSmsMessage((string)$row['user_phone'], $sms);
                $status = (string)($res['status'] ?? 'failed');
                $attempt = 1;
                $nextRetry = $status === 'failed' ? crmOpsNextRetryAt($attempt, $maxAttempts) : null;
                $lastError = $status === 'failed' ? (string)($res['response'] ?? 'sms_failed') : null;
                crmOpsInsertDelivery(
                    $pdo,
                    $notificationId,
                    'sms',
                    $status,
                    $res['provider'] ?? 'sms',
                    $res['response'] ?? '',
                    null,
                    $attempt,
                    $maxAttempts,
                    $nextRetry,
                    $lastError
                );
            }
        } catch (Throwable $e) {
            $attempt = 1;
            $maxAttempts = crmOpsChannelMaxAttempts($channel);
            crmOpsInsertDelivery(
                $pdo,
                $notificationId,
                $channel,
                'failed',
                'internal',
                $e->getMessage(),
                null,
                $attempt,
                $maxAttempts,
                crmOpsNextRetryAt($attempt, $maxAttempts),
                $e->getMessage()
            );
        }
    }
}

function crmOpsCreateNotification(PDO $pdo, string $orgId, string $userId, string $title, string $message, string $type = 'info', ?string $linkUrl = null, array $channels = ['in_app']): ?string {
    $title = trim($title);
    $message = trim($message);
    if ($orgId === '' || $userId === '' || $title === '' || $message === '') {
        return null;
    }

    $allowedTypes = ['info', 'success', 'warning', 'error', 'task', 'payment', 'system'];
    if (!in_array($type, $allowedTypes, true)) {
        $type = 'info';
    }

    $id = uuid32();
    $channelsNorm = crmOpsNormalizeChannels($channels);
    $channelsJson = json_encode($channelsNorm, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);

    $stmt = $pdo->prepare("
        INSERT INTO crm_notifications
          (id, organization_id, user_id, type, title, message, link_url, channels_json, is_read, created_at)
        VALUES
          (:id, :org, :user, :type, :title, :message, :link_url, :channels_json, 0, UTC_TIMESTAMP(3))
    ");
    $stmt->execute([
        ':id' => $id,
        ':org' => $orgId,
        ':user' => $userId,
        ':type' => $type,
        ':title' => $title,
        ':message' => $message,
        ':link_url' => $linkUrl !== null && trim($linkUrl) !== '' ? trim($linkUrl) : null,
        ':channels_json' => $channelsJson,
    ]);

    crmOpsDispatchNotification($pdo, $id);
    return $id;
}

function crmOpsNotifyUsersByRole(PDO $pdo, string $orgId, array $roleNames, string $title, string $message, string $type = 'info', ?string $linkUrl = null, array $channels = ['in_app']): int {
    if ($orgId === '' || empty($roleNames)) {
        return 0;
    }

    $in = implode(',', array_fill(0, count($roleNames), '?'));
    $sql = "
      SELECT DISTINCT u.id
      FROM users u
      JOIN user_roles ur ON ur.user_id = u.id
      JOIN roles r ON r.id = ur.role_id
      WHERE u.organization_id = ?
        AND u.status = 'active'
        AND u.deleted_at IS NULL
        AND r.name IN ({$in})
    ";
    $params = array_merge([$orgId], array_values($roleNames));
    $stmt = $pdo->prepare($sql);
    $stmt->execute($params);
    $userIds = $stmt->fetchAll(PDO::FETCH_COLUMN) ?: [];

    $count = 0;
    foreach ($userIds as $userId) {
        $id = crmOpsCreateNotification($pdo, $orgId, (string)$userId, $title, $message, $type, $linkUrl, $channels);
        if ($id) {
            $count++;
        }
    }
    return $count;
}

function crmOpsRunAutomationJob(PDO $pdo, string $orgId, array $job): array {
    $payload = json_decode((string)($job['payload'] ?? ''), true);
    if (!is_array($payload)) {
        $payload = [];
    }

    $notifyChannels = crmOpsNormalizeChannels($job['notify_channels_json'] ?? ['in_app']);
    $jobType = (string)($job['job_type'] ?? 'custom');
    $rule = strtolower((string)($payload['rule'] ?? $jobType));

    $summary = ['rule' => $rule, 'notifications' => 0, 'matched_records' => 0];

    if ($rule === 'due_task_reminder') {
        $windowHours = max(1, min((int)($payload['window_hours'] ?? 24), 168));
        $stmt = $pdo->prepare("
            SELECT t.id, t.title, t.due_at, t.assigned_to, p.name AS project_name
            FROM project_tasks t
            JOIN client_projects p ON p.id = t.project_id
            WHERE t.organization_id = :org
              AND t.deleted_at IS NULL
              AND t.assigned_to IS NOT NULL
              AND t.status IN ('todo','in_progress','review')
              AND t.due_at IS NOT NULL
              AND t.due_at <= DATE_ADD(UTC_TIMESTAMP(3), INTERVAL :window HOUR)
        ");
        $stmt->bindValue(':org', $orgId);
        $stmt->bindValue(':window', $windowHours, PDO::PARAM_INT);
        $stmt->execute();
        $tasks = $stmt->fetchAll(PDO::FETCH_ASSOC) ?: [];

        $summary['matched_records'] = count($tasks);
        foreach ($tasks as $task) {
            $title = 'Task Due Soon';
            $message = $task['project_name'] . ' - ' . $task['title'] . ' is due by ' . ($task['due_at'] ?? 'soon') . '.';
            $created = crmOpsCreateNotification(
                $pdo,
                $orgId,
                (string)$task['assigned_to'],
                $title,
                $message,
                'task',
                    'projects',
                    $notifyChannels
                );
            if ($created) {
                $summary['notifications']++;
            }
        }

        return $summary;
    }

    if ($rule === 'school_maintenance_billing') {
        $limit = max(1, min((int)($payload['limit'] ?? 50), 300));
        $run = schoolErpGenerateDueMaintenanceBills($pdo, $orgId, null, $limit, true);
        $summary['matched_records'] = (int)($run['processed'] ?? 0);
        $summary['generated'] = (int)($run['generated'] ?? 0);
        $summary['duplicates_skipped'] = (int)($run['duplicates_skipped'] ?? 0);
        $summary['failed'] = (int)($run['failed'] ?? 0);
        return $summary;
    }

    if ($rule === 'overdue_invoice_alert') {
        $stmt = $pdo->prepare("
            SELECT COUNT(*) AS overdue_count, COALESCE(SUM(total),0) AS overdue_total
            FROM invoices
            WHERE organization_id = :org
              AND deleted_at IS NULL
              AND status IN ('sent','partially_paid','overdue')
              AND due_date < CURDATE()
        ");
        $stmt->execute([':org' => $orgId]);
        $data = $stmt->fetch(PDO::FETCH_ASSOC) ?: ['overdue_count' => 0, 'overdue_total' => 0];

        $count = (int)($data['overdue_count'] ?? 0);
        $summary['matched_records'] = $count;
        if ($count > 0) {
            $amount = number_format((float)($data['overdue_total'] ?? 0), 2, '.', '');
            $title = 'Overdue Invoice Alert';
            $message = $count . ' overdue invoice(s) detected. Outstanding amount INR ' . $amount . '.';
            $summary['notifications'] = crmOpsNotifyUsersByRole(
                $pdo,
                $orgId,
                ['Owner', 'Admin', 'Manager'],
                $title,
                $message,
                'payment',
                    'invoices',
                    $notifyChannels
                );
        }
        return $summary;
    }

    if ($rule === 'client_activity_digest') {
        $st1 = $pdo->prepare("
            SELECT COUNT(*) FROM audit_logs
            WHERE organization_id = :org
              AND created_at >= DATE_SUB(UTC_TIMESTAMP(3), INTERVAL 24 HOUR)
        ");
        $st1->execute([':org' => $orgId]);
        $events = (int)$st1->fetchColumn();

        $st2 = $pdo->prepare("
            SELECT COUNT(*) FROM leads
            WHERE organization_id = :org
              AND deleted_at IS NULL
              AND created_at >= DATE_SUB(UTC_TIMESTAMP(3), INTERVAL 24 HOUR)
        ");
        $st2->execute([':org' => $orgId]);
        $newLeads = (int)$st2->fetchColumn();

        $summary['matched_records'] = $events;
        $title = 'Daily Activity Digest';
        $message = "Last 24h: {$events} audit event(s), {$newLeads} new lead(s).";
        $summary['notifications'] = crmOpsNotifyUsersByRole(
            $pdo,
            $orgId,
            ['Owner', 'Admin', 'Manager'],
            $title,
            $message,
            'system',
                'activity',
                $notifyChannels
            );
        return $summary;
    }

    return $summary;
}

function crmOpsRetryFailedDeliveries(PDO $pdo, int $limit = 50): array {
    $limit = max(1, min($limit, 500));
    $summary = [
        'processed' => 0,
        'sent' => 0,
        'still_failed' => 0,
        'terminal_failed' => 0,
        'skipped' => 0,
    ];

    try {
        $st = $pdo->prepare("
            SELECT
              d.id,
              d.notification_id,
              d.channel,
              COALESCE(d.attempt_count, 1) AS attempt_count,
              COALESCE(d.max_attempts, 5) AS max_attempts,
              n.user_id,
              n.title,
              n.message,
              n.link_url,
              u.name AS user_name,
              u.email AS user_email,
              u.phone AS user_phone
            FROM crm_notification_deliveries d
            JOIN crm_notifications n ON n.id = d.notification_id
            JOIN users u ON u.id = n.user_id
            WHERE d.channel IN ('email','sms')
              AND d.status = 'failed'
              AND COALESCE(d.attempt_count, 1) < COALESCE(d.max_attempts, 5)
              AND (d.next_retry_at IS NULL OR d.next_retry_at <= UTC_TIMESTAMP(3))
            ORDER BY d.next_retry_at ASC, d.created_at ASC
            LIMIT :limit
        ");
        $st->bindValue(':limit', $limit, PDO::PARAM_INT);
        $st->execute();
        $rows = $st->fetchAll(PDO::FETCH_ASSOC) ?: [];
    } catch (Throwable $e) {
        error_log('[crmOpsRetryFailedDeliveries.select] ' . $e->getMessage() . PHP_EOL, 3, LOG_PATH . '/error.log');
        return $summary;
    }

    foreach ($rows as $row) {
        $summary['processed']++;
        $id = (string)$row['id'];
        $channel = strtolower((string)$row['channel']);
        $attemptCount = (int)$row['attempt_count'] + 1;
        $maxAttempts = max(1, (int)$row['max_attempts']);

        try {
            if ($channel === 'email') {
                $subject = '[TezzCorp CRM] ' . (string)$row['title'];
                $body = '<h3>' . htmlspecialchars((string)$row['title'], ENT_QUOTES, 'UTF-8') . '</h3>'
                      . '<p>' . nl2br(htmlspecialchars((string)$row['message'], ENT_QUOTES, 'UTF-8')) . '</p>';
                if (!empty($row['link_url'])) {
                    $body .= '<p><a href="' . htmlspecialchars((string)$row['link_url'], ENT_QUOTES, 'UTF-8') . '">Open in CRM</a></p>';
                }
                $result = crmOpsSendEmailMessage((string)$row['user_email'], (string)$row['user_name'], $subject, $body);
            } elseif ($channel === 'sms') {
                $sms = (string)$row['title'] . ': ' . (string)$row['message'];
                if (!empty($row['link_url'])) {
                    $sms .= ' ' . (string)$row['link_url'];
                }
                $result = crmOpsSendSmsMessage((string)$row['user_phone'], $sms);
            } else {
                $result = ['status' => 'skipped', 'provider' => 'internal', 'response' => 'unsupported_channel'];
            }
        } catch (Throwable $e) {
            $result = ['status' => 'failed', 'provider' => 'internal', 'response' => $e->getMessage()];
        }

        $status = (string)($result['status'] ?? 'failed');
        $provider = (string)($result['provider'] ?? 'internal');
        $response = (string)($result['response'] ?? '');
        $now = crmOpsNowUtc();

        if ($status === 'sent' || $status === 'skipped') {
            $up = $pdo->prepare("
                UPDATE crm_notification_deliveries
                SET
                  status = :status,
                  attempt_count = :attempt_count,
                  provider = :provider,
                  response_text = :response_text,
                  next_retry_at = NULL,
                  last_attempt_at = :last_attempt_at,
                  last_error = NULL,
                  delivered_at = :delivered_at,
                  updated_at = UTC_TIMESTAMP(3)
                WHERE id = :id
                LIMIT 1
            ");
            $up->execute([
                ':status' => $status,
                ':attempt_count' => $attemptCount,
                ':provider' => $provider,
                ':response_text' => $response,
                ':last_attempt_at' => $now,
                ':delivered_at' => $now,
                ':id' => $id,
            ]);

            if ($status === 'sent') {
                $summary['sent']++;
            } else {
                $summary['skipped']++;
            }
            continue;
        }

        $nextRetry = crmOpsNextRetryAt($attemptCount, $maxAttempts);
        $terminal = $nextRetry === null;

        $up = $pdo->prepare("
            UPDATE crm_notification_deliveries
            SET
              status = 'failed',
              attempt_count = :attempt_count,
              provider = :provider,
              response_text = :response_text,
              next_retry_at = :next_retry_at,
              last_attempt_at = :last_attempt_at,
              last_error = :last_error,
              updated_at = UTC_TIMESTAMP(3)
            WHERE id = :id
            LIMIT 1
        ");
        $up->execute([
            ':attempt_count' => $attemptCount,
            ':provider' => $provider,
            ':response_text' => $response,
            ':next_retry_at' => $nextRetry,
            ':last_attempt_at' => $now,
            ':last_error' => $response !== '' ? $response : 'delivery_failed',
            ':id' => $id,
        ]);

        if ($terminal) {
            $summary['terminal_failed']++;
        } else {
            $summary['still_failed']++;
        }
    }

    return $summary;
}

function crmOpsDispatchWebhookEvents(PDO $pdo, int $limit = 25): array {
    $limit = max(1, min($limit, 200));
    $summary = [
        'processed' => 0,
        'delivered' => 0,
        'failed' => 0,
        'rescheduled' => 0,
        'skipped' => 0,
    ];

    try {
        $eventsSt = $pdo->prepare("
            SELECT
              id,
              organization_id,
              event_name,
              entity_type,
              entity_id,
              payload_json,
              status,
              attempt_count,
              max_attempts
            FROM crm_webhook_events
            WHERE status IN ('queued','failed')
              AND (next_retry_at IS NULL OR next_retry_at <= UTC_TIMESTAMP(3))
            ORDER BY created_at ASC
            LIMIT :limit
        ");
        $eventsSt->bindValue(':limit', $limit, PDO::PARAM_INT);
        $eventsSt->execute();
        $events = $eventsSt->fetchAll(PDO::FETCH_ASSOC) ?: [];
    } catch (Throwable $e) {
        error_log('[crmOpsDispatchWebhookEvents.select] ' . $e->getMessage() . PHP_EOL, 3, LOG_PATH . '/error.log');
        return $summary;
    }

    foreach ($events as $event) {
        $summary['processed']++;
        $eventId = (string)$event['id'];
        $orgId = (string)$event['organization_id'];
        $eventName = (string)$event['event_name'];
        $attemptCount = (int)$event['attempt_count'] + 1;
        $maxAttempts = max(1, (int)$event['max_attempts']);
        $payload = json_decode((string)$event['payload_json'], true);
        if (!is_array($payload)) {
            $payload = [];
        }

        $claim = $pdo->prepare("
            UPDATE crm_webhook_events
            SET status = 'processing', updated_at = UTC_TIMESTAMP(3)
            WHERE id = :id AND status IN ('queued','failed')
            LIMIT 1
        ");
        $claim->execute([':id' => $eventId]);
        if ($claim->rowCount() === 0) {
            continue;
        }

        $endpointsSt = $pdo->prepare("
            SELECT id, name, target_url, events_json, secret, timeout_seconds, max_retries
            FROM crm_webhook_endpoints
            WHERE organization_id = :org
              AND status = 'active'
              AND deleted_at IS NULL
            ORDER BY created_at ASC
        ");
        $endpointsSt->execute([':org' => $orgId]);
        $allEndpoints = $endpointsSt->fetchAll(PDO::FETCH_ASSOC) ?: [];

        $targets = [];
        $endpointMaxRetries = 1;
        foreach ($allEndpoints as $endpoint) {
            if (crmOpsEndpointWantsEvent($eventName, $endpoint['events_json'] ?? '[]')) {
                $targets[] = $endpoint;
                $endpointMaxRetries = max($endpointMaxRetries, (int)($endpoint['max_retries'] ?? 1));
            }
        }

        if (empty($targets)) {
            $done = $pdo->prepare("
                UPDATE crm_webhook_events
                SET
                  status = 'delivered',
                  attempt_count = :attempt_count,
                  delivered_at = UTC_TIMESTAMP(3),
                  next_retry_at = NULL,
                  last_error = 'no_endpoint',
                  updated_at = UTC_TIMESTAMP(3)
                WHERE id = :id
                LIMIT 1
            ");
            $done->execute([
                ':attempt_count' => $attemptCount,
                ':id' => $eventId,
            ]);
            $summary['skipped']++;
            continue;
        }

        $envelope = [
            'event_id' => $eventId,
            'event_name' => $eventName,
            'organization_id' => $orgId,
            'entity_type' => $event['entity_type'] ?? null,
            'entity_id' => $event['entity_id'] ?? null,
            'occurred_at' => crmOpsNowUtc(),
            'data' => $payload,
        ];
        $rawBody = json_encode($envelope, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
        if ($rawBody === false) {
            $rawBody = '{}';
        }

        $allSuccess = true;
        $errorMessages = [];
        foreach ($targets as $endpoint) {
            $endpointId = (string)$endpoint['id'];
            $targetUrl = trim((string)$endpoint['target_url']);
            $secret = (string)($endpoint['secret'] ?? '');
            $signature = $secret !== '' ? hash_hmac('sha256', $rawBody, $secret) : '';

            $headers = [
                'X-Tezz-Event: ' . $eventName,
                'X-Tezz-Event-Id: ' . $eventId,
            ];
            if ($signature !== '') {
                $headers[] = 'X-Tezz-Signature: sha256=' . $signature;
            }

            $http = crmOpsHttpPostJson(
                $targetUrl,
                $envelope,
                $headers,
                max(3, (int)($endpoint['timeout_seconds'] ?? 10))
            );

            $ok = !empty($http['ok']);
            $httpStatus = (int)($http['status_code'] ?? 0);
            $responseBody = (string)($http['body'] ?? '');
            $errorText = $ok ? null : (($httpStatus > 0 ? "http_{$httpStatus}" : 'request_failed') . ($responseBody !== '' ? " {$responseBody}" : ''));

            $attemptSt = $pdo->prepare("
                INSERT INTO crm_webhook_event_attempts
                  (id, event_id, endpoint_id, attempt_no, status, http_status, response_body, error_text, created_at)
                VALUES
                  (:id, :event_id, :endpoint_id, :attempt_no, :status, :http_status, :response_body, :error_text, UTC_TIMESTAMP(3))
            ");
            $attemptSt->execute([
                ':id' => uuid32(),
                ':event_id' => $eventId,
                ':endpoint_id' => $endpointId,
                ':attempt_no' => $attemptCount,
                ':status' => $ok ? 'success' : 'failed',
                ':http_status' => $httpStatus > 0 ? $httpStatus : null,
                ':response_body' => $responseBody !== '' ? $responseBody : null,
                ':error_text' => $errorText,
            ]);

            if ($ok) {
                $epUp = $pdo->prepare("UPDATE crm_webhook_endpoints SET last_success_at = UTC_TIMESTAMP(3), updated_at = UTC_TIMESTAMP(3) WHERE id = :id LIMIT 1");
                $epUp->execute([':id' => $endpointId]);
            } else {
                $allSuccess = false;
                $errorMessages[] = $errorText ?: 'webhook_failed';
                $epUp = $pdo->prepare("UPDATE crm_webhook_endpoints SET last_failure_at = UTC_TIMESTAMP(3), updated_at = UTC_TIMESTAMP(3) WHERE id = :id LIMIT 1");
                $epUp->execute([':id' => $endpointId]);
            }
        }

        if ($allSuccess) {
            $done = $pdo->prepare("
                UPDATE crm_webhook_events
                SET
                  status = 'delivered',
                  attempt_count = :attempt_count,
                  delivered_at = UTC_TIMESTAMP(3),
                  next_retry_at = NULL,
                  last_error = NULL,
                  updated_at = UTC_TIMESTAMP(3)
                WHERE id = :id
                LIMIT 1
            ");
            $done->execute([
                ':attempt_count' => $attemptCount,
                ':id' => $eventId,
            ]);
            $summary['delivered']++;
            continue;
        }

        $effectiveMaxAttempts = max($maxAttempts, $endpointMaxRetries);
        $nextRetry = crmOpsNextRetryAt($attemptCount, $effectiveMaxAttempts);
        if ($nextRetry === null) {
            $fail = $pdo->prepare("
                UPDATE crm_webhook_events
                SET
                  status = 'failed',
                  attempt_count = :attempt_count,
                  next_retry_at = NULL,
                  last_error = :last_error,
                  updated_at = UTC_TIMESTAMP(3)
                WHERE id = :id
                LIMIT 1
            ");
            $fail->execute([
                ':attempt_count' => $attemptCount,
                ':last_error' => implode(' | ', array_slice($errorMessages, 0, 5)),
                ':id' => $eventId,
            ]);
            $summary['failed']++;
        } else {
            $retry = $pdo->prepare("
                UPDATE crm_webhook_events
                SET
                  status = 'queued',
                  attempt_count = :attempt_count,
                  next_retry_at = :next_retry_at,
                  last_error = :last_error,
                  updated_at = UTC_TIMESTAMP(3)
                WHERE id = :id
                LIMIT 1
            ");
            $retry->execute([
                ':attempt_count' => $attemptCount,
                ':next_retry_at' => $nextRetry,
                ':last_error' => implode(' | ', array_slice($errorMessages, 0, 5)),
                ':id' => $eventId,
            ]);
            $summary['rescheduled']++;
        }
    }

    return $summary;
}
