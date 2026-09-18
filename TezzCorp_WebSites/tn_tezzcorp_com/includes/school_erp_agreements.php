<?php
declare(strict_types=1);

require_once __DIR__ . '/../config/config.php';

function schoolErpAgreementTablesReady(PDO $pdo): bool {
    static $ready = null;
    if ($ready !== null) {
        return $ready;
    }

    $required = [
        'erp_school_service_agreements',
        'erp_school_service_agreement_events',
    ];

    try {
        $st = $pdo->prepare("
            SELECT COUNT(*)
            FROM information_schema.TABLES
            WHERE TABLE_SCHEMA = DATABASE()
              AND TABLE_NAME = :table
        ");
        foreach ($required as $table) {
            $st->execute([':table' => $table]);
            if ((int)$st->fetchColumn() <= 0) {
                $ready = false;
                return false;
            }
        }
        $ready = true;
    } catch (Throwable $e) {
        $ready = false;
    }

    return $ready;
}

function schoolErpAgreementGenerateNumber(PDO $pdo, string $orgId): string {
    $orgPrefix = strtoupper(substr(preg_replace('/[^A-Za-z0-9]/', '', $orgId), 0, 4) ?: 'ORG');
    for ($i = 0; $i < 15; $i++) {
        $candidate = 'AGR-' . gmdate('Ymd') . '-' . $orgPrefix . '-' . strtoupper(substr(bin2hex(random_bytes(2)), 0, 4));
        try {
            $st = $pdo->prepare("
                SELECT 1
                FROM erp_school_service_agreements
                WHERE organization_id = :org
                  AND agreement_number = :agreement_number
                LIMIT 1
            ");
            $st->execute([
                ':org' => $orgId,
                ':agreement_number' => $candidate,
            ]);
            if (!$st->fetchColumn()) {
                return $candidate;
            }
        } catch (Throwable $e) {
            return $candidate;
        }
    }

    return 'AGR-' . gmdate('YmdHis') . '-' . $orgPrefix;
}

function schoolErpAgreementGenerateToken(PDO $pdo): string {
    for ($i = 0; $i < 15; $i++) {
        $token = bin2hex(random_bytes(24));
        try {
            $st = $pdo->prepare("
                SELECT 1
                FROM erp_school_service_agreements
                WHERE share_token = :token
                LIMIT 1
            ");
            $st->execute([':token' => $token]);
            if (!$st->fetchColumn()) {
                return $token;
            }
        } catch (Throwable $e) {
            return $token;
        }
    }
    return bin2hex(random_bytes(24));
}

function schoolErpAgreementClientIp(): string {
    $headers = [
        'HTTP_CF_CONNECTING_IP',
        'HTTP_X_FORWARDED_FOR',
        'HTTP_X_REAL_IP',
        'REMOTE_ADDR',
    ];
    foreach ($headers as $key) {
        $value = trim((string)($_SERVER[$key] ?? ''));
        if ($value === '') {
            continue;
        }
        if ($key === 'HTTP_X_FORWARDED_FOR') {
            $parts = explode(',', $value);
            $value = trim((string)($parts[0] ?? ''));
        }
        if (filter_var($value, FILTER_VALIDATE_IP)) {
            return $value;
        }
    }
    return '';
}

function schoolErpAgreementClientUserAgent(): string {
    return substr(trim((string)($_SERVER['HTTP_USER_AGENT'] ?? '')), 0, 255);
}

function schoolErpAgreementAcceptUrl(string $shareToken): string {
    $token = rawurlencode(trim($shareToken));
    return rtrim(CRM_URL, '/') . '/school-erp-agreement-accept.php?token=' . $token;
}

function schoolErpPublicAssetUrl(string $relativePath): string {
    $path = trim($relativePath);
    if ($path === '') {
        return rtrim(APP_URL, '/') . '/';
    }
    if (preg_match('#^https?://#i', $path)) {
        return $path;
    }
    return rtrim(APP_URL, '/') . '/' . ltrim($path, '/');
}

/**
 * @param array{
 *   agreement_number?:string,
 *   title?:string,
 *   version_label?:string,
 *   school_name?:string,
 *   school_code?:string,
 *   board_type?:string,
 *   effective_date?:string,
 *   valid_until?:string,
 *   billing_cycle?:string,
 *   amount?:float|int|string,
 *   currency?:string,
 *   terms?:array|string,
 *   provider_name?:string,
 *   provider_email?:string,
 *   provider_phone?:string,
 *   recipient_name?:string,
 *   recipient_email?:string
 * } $ctx
 */
function schoolErpAgreementRenderHtml(array $ctx): string {
    $agreementNumber = htmlspecialchars(trim((string)($ctx['agreement_number'] ?? '-')), ENT_QUOTES, 'UTF-8');
    $title = htmlspecialchars(trim((string)($ctx['title'] ?? 'School ERP Service Agreement')), ENT_QUOTES, 'UTF-8');
    $version = htmlspecialchars(trim((string)($ctx['version_label'] ?? 'v1')), ENT_QUOTES, 'UTF-8');
    $schoolName = htmlspecialchars(trim((string)($ctx['school_name'] ?? 'School')), ENT_QUOTES, 'UTF-8');
    $schoolCode = htmlspecialchars(trim((string)($ctx['school_code'] ?? '-')), ENT_QUOTES, 'UTF-8');
    $boardType = htmlspecialchars(trim((string)($ctx['board_type'] ?? '-')), ENT_QUOTES, 'UTF-8');
    $effectiveDate = htmlspecialchars(trim((string)($ctx['effective_date'] ?? '-')), ENT_QUOTES, 'UTF-8');
    $validUntil = htmlspecialchars(trim((string)($ctx['valid_until'] ?? '-')), ENT_QUOTES, 'UTF-8');
    $billingCycle = htmlspecialchars(trim((string)($ctx['billing_cycle'] ?? '-')), ENT_QUOTES, 'UTF-8');
    $currency = strtoupper(trim((string)($ctx['currency'] ?? 'INR')));
    if ($currency === '' || strlen($currency) !== 3) {
        $currency = 'INR';
    }
    $amount = number_format((float)($ctx['amount'] ?? 0), 2, '.', '');

    $providerName = htmlspecialchars(trim((string)($ctx['provider_name'] ?? env('SITE_NAME', 'Tezz Corp'))), ENT_QUOTES, 'UTF-8');
    $providerEmail = htmlspecialchars(trim((string)($ctx['provider_email'] ?? env('EMAIL_FROM', 'info@tezzcorp.com'))), ENT_QUOTES, 'UTF-8');
    $providerPhone = htmlspecialchars(trim((string)($ctx['provider_phone'] ?? env('SUPPORT_PHONE', '+91-00000-00000'))), ENT_QUOTES, 'UTF-8');
    $recipientName = htmlspecialchars(trim((string)($ctx['recipient_name'] ?? 'Authorized Signatory')), ENT_QUOTES, 'UTF-8');
    $recipientEmail = htmlspecialchars(trim((string)($ctx['recipient_email'] ?? '')), ENT_QUOTES, 'UTF-8');

    $termsInput = $ctx['terms'] ?? [];
    $terms = [];
    if (is_array($termsInput)) {
        foreach ($termsInput as $item) {
            $line = trim((string)$item);
            if ($line !== '') {
                $terms[] = $line;
            }
        }
    } else {
        $raw = trim((string)$termsInput);
        if ($raw !== '') {
            $parts = preg_split('/\r\n|\r|\n/', $raw) ?: [];
            foreach ($parts as $line) {
                $line = trim((string)$line);
                if ($line !== '') {
                    $terms[] = $line;
                }
            }
        }
    }
    if (empty($terms)) {
        $terms = [
            'Provider will deliver School ERP maintenance, support, and release operations as per subscribed scope.',
            'Billing is due as per cycle and due date mentioned in this agreement and associated invoices.',
            'School will provide authorized contacts and timely operational inputs for smooth support delivery.',
            'Either party may request termination with a written notice subject to settled outstanding dues.',
            'Digital acceptance of this agreement is treated as legally valid consent.',
        ];
    }

    $termsHtml = '';
    foreach ($terms as $term) {
        $termsHtml .= '<li>' . htmlspecialchars($term, ENT_QUOTES, 'UTF-8') . '</li>';
    }

    return '<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>' . $title . '</title>
  <style>
    body{font-family:DejaVu Sans, Arial, sans-serif; color:#1f2937; font-size:12px; line-height:1.5; margin:24px;}
    h1{font-size:20px; margin:0 0 4px 0;}
    h2{font-size:14px; margin:20px 0 8px 0;}
    .meta{margin-bottom:12px; color:#4b5563;}
    .panel{border:1px solid #d1d5db; border-radius:8px; padding:10px 12px; margin-top:10px;}
    .grid{width:100%; border-collapse:collapse; margin-top:10px;}
    .grid td{border:1px solid #d1d5db; padding:8px; vertical-align:top;}
    .label{width:180px; color:#6b7280;}
    .foot{margin-top:24px; font-size:11px; color:#6b7280;}
    .sign{margin-top:26px;}
  </style>
</head>
<body>
  <h1>' . $title . '</h1>
  <div class="meta">Agreement No: <strong>' . $agreementNumber . '</strong> | Version: <strong>' . $version . '</strong></div>

  <div class="panel">
    This service agreement is executed between <strong>' . $providerName . '</strong> and <strong>' . $schoolName . '</strong> for School ERP operations.
  </div>

  <table class="grid" aria-label="agreement-summary">
    <tr><td class="label">School Name</td><td>' . $schoolName . '</td></tr>
    <tr><td class="label">School Code</td><td>' . $schoolCode . '</td></tr>
    <tr><td class="label">Board Type</td><td>' . $boardType . '</td></tr>
    <tr><td class="label">Effective Date</td><td>' . $effectiveDate . '</td></tr>
    <tr><td class="label">Valid Until</td><td>' . $validUntil . '</td></tr>
    <tr><td class="label">Billing Cycle</td><td>' . $billingCycle . '</td></tr>
    <tr><td class="label">Contract Amount</td><td>' . $currency . ' ' . $amount . '</td></tr>
    <tr><td class="label">School Contact</td><td>' . $recipientName . ($recipientEmail !== '' ? (' (' . $recipientEmail . ')') : '') . '</td></tr>
  </table>

  <h2>Terms and Conditions</h2>
  <ol>' . $termsHtml . '</ol>

  <div class="sign">
    <strong>Provider:</strong> ' . $providerName . '<br>
    <strong>Email:</strong> ' . $providerEmail . '<br>
    <strong>Phone:</strong> ' . $providerPhone . '
  </div>

  <div class="foot">
    This document was generated electronically. Digital acceptance from the school authorized representative is recorded with timestamp, IP address, and user agent.
  </div>
</body>
</html>';
}

/**
 * @param array{
 *   school_name?:string,
 *   agreement_number?:string,
 *   acceptance_url?:string,
 *   share_expires_at?:string
 * } $ctx
 */
function schoolErpAgreementRenderEmailHtml(array $ctx): string {
    $schoolName = htmlspecialchars(trim((string)($ctx['school_name'] ?? 'School')), ENT_QUOTES, 'UTF-8');
    $agreementNumber = htmlspecialchars(trim((string)($ctx['agreement_number'] ?? '-')), ENT_QUOTES, 'UTF-8');
    $acceptanceUrl = htmlspecialchars(trim((string)($ctx['acceptance_url'] ?? '#')), ENT_QUOTES, 'UTF-8');
    $expiresAt = htmlspecialchars(trim((string)($ctx['share_expires_at'] ?? '')), ENT_QUOTES, 'UTF-8');

    return '<!doctype html><html><body style="font-family:Arial,sans-serif;color:#1f2937;line-height:1.5;">
<h2 style="margin:0 0 8px 0;">School ERP Service Agreement</h2>
<p>Hello ' . $schoolName . ',</p>
<p>Your service agreement <strong>' . $agreementNumber . '</strong> is ready for digital acceptance.</p>
<p><a href="' . $acceptanceUrl . '" style="display:inline-block;padding:10px 14px;background:#0a4fb4;color:#fff;text-decoration:none;border-radius:6px;">Review and Accept Agreement</a></p>
' . ($expiresAt !== '' ? '<p style="color:#6b7280;font-size:12px;">This link expires on ' . $expiresAt . '.</p>' : '') . '
<p>Regards,<br>TezzCorp CRM</p>
</body></html>';
}

/**
 * @return array{ok:bool,path:string,error:string}
 */
function schoolErpAgreementStorePdf(string $agreementHtml, string $orgId, string $agreementId): array {
    $root = dirname(__DIR__);
    $orgPart = preg_replace('/[^a-zA-Z0-9]/', '', $orgId) ?: 'org';
    $agreementPart = preg_replace('/[^a-zA-Z0-9]/', '', $agreementId) ?: bin2hex(random_bytes(8));
    $relativeDir = 'uploads/erp/agreements/' . $orgPart;
    $absoluteDir = $root . '/' . $relativeDir;
    $relativePath = $relativeDir . '/' . $agreementPart . '.pdf';
    $absolutePath = $root . '/' . $relativePath;

    if (!is_dir($absoluteDir) && !@mkdir($absoluteDir, 0775, true) && !is_dir($absoluteDir)) {
        return ['ok' => false, 'path' => '', 'error' => 'mkdir_failed'];
    }

    if (!class_exists(\Dompdf\Dompdf::class)) {
        $autoload = $root . '/vendor/autoload.php';
        if (is_readable($autoload)) {
            require_once $autoload;
        }
    }
    if (!class_exists(\Dompdf\Dompdf::class)) {
        return ['ok' => false, 'path' => '', 'error' => 'dompdf_missing'];
    }

    try {
        $options = new \Dompdf\Options();
        $options->set('isRemoteEnabled', false);
        $options->setDefaultFont('DejaVu Sans');

        $dompdf = new \Dompdf\Dompdf($options);
        $dompdf->loadHtml($agreementHtml, 'UTF-8');
        $dompdf->setPaper('A4', 'portrait');
        $dompdf->render();
        $pdfData = $dompdf->output();
        if (@file_put_contents($absolutePath, $pdfData) === false) {
            return ['ok' => false, 'path' => '', 'error' => 'file_write_failed'];
        }
        return ['ok' => true, 'path' => $relativePath, 'error' => ''];
    } catch (Throwable $e) {
        error_log('[schoolErpAgreementStorePdf] ' . $e->getMessage() . PHP_EOL, 3, LOG_PATH . '/error.log');
        return ['ok' => false, 'path' => '', 'error' => 'pdf_render_failed'];
    }
}

function schoolErpAgreementLogEvent(
    PDO $pdo,
    string $orgId,
    string $agreementId,
    string $eventType,
    array $eventData = [],
    ?string $actorUserId = null,
    ?string $ipAddress = null,
    ?string $userAgent = null
): void {
    $eventType = strtolower(trim($eventType));
    $allowed = ['generated', 'emailed', 'opened', 'accepted', 'rejected', 'expired', 'cancelled'];
    if (!in_array($eventType, $allowed, true)) {
        return;
    }

    $eventDataJson = !empty($eventData)
        ? json_encode($eventData, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE)
        : null;

    try {
        $st = $pdo->prepare("
            INSERT INTO erp_school_service_agreement_events
              (id, organization_id, agreement_id, event_type, event_data_json, actor_user_id, ip_address, user_agent, created_at)
            VALUES
              (:id, :org, :agreement_id, :event_type, :event_data_json, :actor_user_id, :ip_address, :user_agent, UTC_TIMESTAMP(3))
        ");
        $st->execute([
            ':id' => uuid32(),
            ':org' => $orgId,
            ':agreement_id' => $agreementId,
            ':event_type' => $eventType,
            ':event_data_json' => $eventDataJson !== false ? $eventDataJson : null,
            ':actor_user_id' => $actorUserId !== null && trim($actorUserId) !== '' ? trim($actorUserId) : null,
            ':ip_address' => $ipAddress !== null && trim($ipAddress) !== '' ? trim($ipAddress) : null,
            ':user_agent' => $userAgent !== null && trim($userAgent) !== '' ? trim($userAgent) : null,
        ]);
    } catch (Throwable $e) {
        error_log('[schoolErpAgreementLogEvent] ' . $e->getMessage() . PHP_EOL, 3, LOG_PATH . '/error.log');
    }
}

