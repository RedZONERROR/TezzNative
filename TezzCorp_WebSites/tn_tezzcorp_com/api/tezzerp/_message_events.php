<?php
declare(strict_types=1);

require_once __DIR__ . '/_erp_sync.php';

if (!function_exists('erpMessagingPlanCode')) {
    function erpMessagingPlanCode(PDO $pdo): string
    {
        if (!function_exists('apiGetLicenseState')) {
            return '';
        }
        $license = apiGetLicenseState($pdo);
        return strtolower(trim((string)($license['plan_code'] ?? '')));
    }
}

if (!function_exists('erpMessagingIsGold')) {
    function erpMessagingIsGold(PDO $pdo): bool
    {
        return erpMessagingPlanCode($pdo) === 'gold';
    }
}

if (!function_exists('erpMessagingBuildSyncContext')) {
    function erpMessagingBuildSyncContext(PDO $pdo, array $payload = []): array
    {
        $settings = erpSyncReadSettings($pdo);
        return [
            'crm_base_url' => erpSyncCrmBaseUrl($settings),
            'sync_token' => erpSyncResolveToken($settings, $payload),
            'school_id' => erpSyncResolveSchoolId($settings, $payload),
            'plan_code' => erpMessagingPlanCode($pdo),
        ];
    }
}

if (!function_exists('erpMessageTrigger')) {
    function erpMessageTrigger(
        PDO $pdo,
        string $eventKey,
        array $eventPayload,
        array $recipients = [],
        array $options = []
    ): array {
        $eventKey = strtolower(trim($eventKey));
        if ($eventKey === '') {
            return ['success' => false, 'message' => 'event_key is required'];
        }

        $ctx = erpMessagingBuildSyncContext($pdo, $options);
        $syncToken = trim((string)($ctx['sync_token'] ?? ''));
        $schoolId = trim((string)($ctx['school_id'] ?? ''));
        $baseUrl = trim((string)($ctx['crm_base_url'] ?? ''));

        if ($syncToken === '' || $baseUrl === '') {
            return [
                'success' => false,
                'skipped' => true,
                'message' => 'Messaging sync token/base URL missing',
            ];
        }

        if (!erpMessagingIsGold($pdo)) {
            return [
                'success' => true,
                'skipped' => true,
                'message' => 'Messaging is available only for gold plan',
            ];
        }

        $payload = [
            'action' => 'trigger_event',
            'sync_token' => $syncToken,
            'event_key' => $eventKey,
            'payload' => $eventPayload,
            'recipients' => $recipients,
            'reference_type' => trim((string)($options['reference_type'] ?? 'event')),
            'reference_id' => trim((string)($options['reference_id'] ?? '')),
            'actor_id' => trim((string)($options['actor_id'] ?? 'ERP_RUNTIME')),
        ];
        if ($schoolId !== '') {
            $payload['school_id'] = $schoolId;
        }
        $channel = strtolower(trim((string)($options['channel_code'] ?? $options['channel'] ?? '')));
        if (in_array($channel, ['sms', 'whatsapp', 'rcs', 'voice'], true)) {
            $payload['channel_code'] = $channel;
        }

        $url = rtrim($baseUrl, '/') . '/school-messages.php';
        $remote = erpSyncRequest('POST', $url, $payload, [
            'X-License-Sync-Token: ' . $syncToken,
        ]);

        if (!$remote['ok']) {
            $message = trim((string)($remote['error'] ?? ''));
            if ($message === '') {
                $message = 'Messaging request failed';
            }
            return [
                'success' => false,
                'message' => $message,
                'http_status' => (int)($remote['http_status'] ?? 0),
                'remote' => is_array($remote['json'] ?? null) ? $remote['json'] : [],
            ];
        }

        $json = is_array($remote['json'] ?? null) ? $remote['json'] : [];
        return [
            'success' => !empty($json['success']),
            'message' => trim((string)($json['message'] ?? 'Messaging processed')),
            'data' => is_array($json['data'] ?? null) ? $json['data'] : [],
            'http_status' => (int)($remote['http_status'] ?? 0),
        ];
    }
}
