<?php
declare(strict_types=1);

require_once __DIR__ . '/_common.php';
require_once __DIR__ . '/_erp_sync.php';
require_once __DIR__ . '/_message_events.php';

$method = strtoupper((string)($_SERVER['REQUEST_METHOD'] ?? 'GET'));
if (!in_array($method, ['GET', 'POST'], true)) {
    jsonResponse(405, [
        'success' => false,
        'message' => 'Method not allowed',
    ]);
}

$pdo = apiDb();
$userId = requireAuthenticatedUserId();
$user = fetchUserContext($pdo, $userId);
requireAnyPermission($user, ['index', 'profile']);

$rawBody = file_get_contents('php://input');
$jsonBody = [];
if (is_string($rawBody) && trim($rawBody) !== '') {
    $decoded = json_decode($rawBody, true);
    if (is_array($decoded)) {
        $jsonBody = $decoded;
    }
}
$payload = array_merge($_GET, $_POST, $jsonBody);

$sync = erpMessagingBuildSyncContext($pdo, $payload);
$syncToken = trim((string)($sync['sync_token'] ?? ''));
$schoolId = trim((string)($sync['school_id'] ?? ''));
$baseUrl = trim((string)($sync['crm_base_url'] ?? ''));
$planCode = strtolower(trim((string)($sync['plan_code'] ?? '')));
$isGold = $planCode === 'gold';

$syncState = [
    'crm_base_url' => $baseUrl,
    'has_sync_token' => $syncToken !== '',
    'school_id' => $schoolId,
    'plan_code' => $planCode,
    'is_gold' => $isGold,
    'provider' => '2factor',
];

if ($method === 'GET') {
    $scope = strtolower(trim((string)($payload['scope'] ?? 'bootstrap')));
    if (!in_array($scope, ['bootstrap', 'logs'], true)) {
        jsonResponse(400, [
            'success' => false,
            'message' => 'Unsupported scope',
        ]);
    }

    if ($syncToken === '' || $baseUrl === '') {
        jsonResponse(200, [
            'success' => true,
            'message' => 'Messaging loaded (sync not configured)',
            'data' => [
                'sync' => $syncState,
                'channel_rates' => [],
                'wallets' => [],
                'rules' => [],
                'events' => [],
                'charge_notice' => 'Messaging is chargeable and available only for gold plan schools.',
            ],
        ]);
    }

    $url = rtrim($baseUrl, '/') . '/school-messages.php?scope=' . rawurlencode($scope)
        . '&sync_token=' . rawurlencode($syncToken);
    if ($schoolId !== '') {
        $url .= '&school_id=' . rawurlencode($schoolId);
    }
    if ($scope === 'logs' && isset($payload['limit'])) {
        $url .= '&limit=' . rawurlencode((string)$payload['limit']);
    }

    $remote = erpSyncRequest('GET', $url, [], [
        'X-License-Sync-Token: ' . $syncToken,
    ]);
    if (!$remote['ok']) {
        jsonResponse(502, [
            'success' => false,
            'message' => 'Failed to load messaging from EDU',
            'data' => [
                'sync' => $syncState,
                'http_status' => (int)($remote['http_status'] ?? 0),
                'error' => (string)($remote['error'] ?? ''),
            ],
        ]);
    }

    $json = is_array($remote['json'] ?? null) ? $remote['json'] : [];
    $data = is_array($json['data'] ?? null) ? $json['data'] : [];
    $data['sync'] = $syncState;

    jsonResponse(200, [
        'success' => !empty($json['success']),
        'message' => (string)($json['message'] ?? 'Messaging loaded'),
        'data' => $data,
    ]);
}

$action = strtolower(trim((string)($payload['action'] ?? '')));
if (!in_array($action, ['rule_upsert', 'bulk_send', 'purchase_checkout', 'trigger_event'], true)) {
    jsonResponse(400, [
        'success' => false,
        'message' => 'Unsupported action',
    ]);
}

if ($syncToken === '' || $baseUrl === '') {
    jsonResponse(409, [
        'success' => false,
        'message' => 'Messaging sync token/base URL is missing. Configure ERP sync first.',
        'data' => ['sync' => $syncState],
    ]);
}

if (!$isGold) {
    jsonResponse(403, [
        'success' => false,
        'message' => 'Messaging is available only for Gold plan schools',
        'data' => ['sync' => $syncState],
    ]);
}

$remotePayload = ['action' => $action, 'sync_token' => $syncToken, 'actor_id' => $userId];
if ($schoolId !== '') {
    $remotePayload['school_id'] = $schoolId;
}

$forwardKeys = [
    'event_key',
    'channel',
    'channel_code',
    'is_enabled',
    'template_text',
    'template_key',
    'priority_order',
    'message',
    'recipients',
    'payload',
    'reference_type',
    'reference_id',
    'units',
    'limit',
];
foreach ($forwardKeys as $key) {
    if (array_key_exists($key, $payload)) {
        $remotePayload[$key] = $payload[$key];
    }
}

$url = rtrim($baseUrl, '/') . '/school-messages.php';
$remote = erpSyncRequest('POST', $url, $remotePayload, [
    'X-License-Sync-Token: ' . $syncToken,
]);
if (!$remote['ok']) {
    $json = is_array($remote['json'] ?? null) ? $remote['json'] : [];
    $errorMessage = trim((string)($json['message'] ?? $json['error'] ?? $remote['error'] ?? 'Messaging request failed'));
    jsonResponse(502, [
        'success' => false,
        'message' => $errorMessage,
        'data' => [
            'sync' => $syncState,
            'http_status' => (int)($remote['http_status'] ?? 0),
        ],
    ]);
}

$json = is_array($remote['json'] ?? null) ? $remote['json'] : [];
$data = is_array($json['data'] ?? null) ? $json['data'] : [];
$data['sync'] = $syncState;

jsonResponse(200, [
    'success' => !empty($json['success']),
    'message' => (string)($json['message'] ?? 'Messaging action processed'),
    'data' => $data,
]);
