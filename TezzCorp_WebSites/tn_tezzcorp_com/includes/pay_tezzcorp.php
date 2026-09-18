<?php
declare(strict_types=1);

require_once __DIR__ . '/../config/config.php';

function payTezzApiBaseUrl(): string {
    $base = trim((string)env('PAY_TEZZCORP_API_BASE', 'https://pay.tezzcorp.in/api/v1'));
    if ($base === '') {
        $base = 'https://pay.tezzcorp.in/api/v1';
    }
    return rtrim($base, '/');
}

function payTezzPortalBaseUrl(): string {
    $base = trim((string)env('PAY_TEZZCORP_URL', 'https://pay.tezzcorp.in/'));
    if ($base === '') {
        $base = 'https://pay.tezzcorp.in/';
    }
    return rtrim($base, '/') . '/';
}

function payTezzApiKey(): string {
    return trim((string)env('PAY_TEZZCORP_API_KEY', ''));
}

function payTezzApiSecret(): string {
    return trim((string)env('PAY_TEZZCORP_API_SECRET', ''));
}

function payTezzAdminApiBaseUrl(): string {
    $base = trim((string)env('PAY_TEZZCORP_ADMIN_API_BASE', ''));
    if ($base === '') {
        $base = payTezzApiBaseUrl();
    }
    return rtrim($base, '/');
}

function payTezzAdminApiKey(): string {
    return trim((string)env('PAY_TEZZCORP_ADMIN_API_KEY', ''));
}

function payTezzAdminConfigured(): bool {
    return payTezzAdminApiKey() !== '';
}

function payTezzFlagEnabled(string $raw, bool $default = false): bool {
    $value = strtolower(trim($raw));
    if ($value === '') {
        return $default;
    }
    return !in_array($value, ['0', 'false', 'no', 'off'], true);
}

function payTezzAppEnv(): string {
    return strtolower(trim((string)env('APP_ENV', 'production')));
}

function payTezzIsProductionEnv(): bool {
    return !in_array(payTezzAppEnv(), ['local', 'dev', 'development', 'test', 'testing'], true);
}

function payTezzWebhookSecret(): string {
    $secret = trim((string)env('PAY_TEZZCORP_WEBHOOK_SECRET', ''));
    if ($secret !== '') {
        return $secret;
    }

    $useApiSecret = payTezzFlagEnabled((string)env('PAY_TEZZCORP_WEBHOOK_USE_API_SECRET', '1'), true);
    if ($useApiSecret) {
        return payTezzApiSecret();
    }

    return '';
}

function payTezzWebhookSignatureRequired(): bool {
    $raw = trim((string)env('PAY_TEZZCORP_WEBHOOK_SIGNATURE_REQUIRED', ''));
    if ($raw === '') {
        return payTezzIsProductionEnv();
    }
    return payTezzFlagEnabled($raw, payTezzIsProductionEnv());
}

function payTezzTlsAllowInsecure(): bool {
    return payTezzFlagEnabled((string)env('PAY_TEZZCORP_TLS_ALLOW_INSECURE', '0'), false);
}

function payTezzTlsVerifyPeer(): bool {
    if (payTezzIsProductionEnv() && !payTezzTlsAllowInsecure()) {
        return true;
    }
    return payTezzFlagEnabled((string)env('PAY_TEZZCORP_TLS_VERIFY_PEER', '1'), true);
}

function payTezzTlsCandidateCaBundlePaths(): array {
    $candidates = [];

    $curlCainfo = trim((string)ini_get('curl.cainfo'));
    if ($curlCainfo !== '') {
        $candidates[] = $curlCainfo;
    }

    $opensslCafile = trim((string)ini_get('openssl.cafile'));
    if ($opensslCafile !== '') {
        $candidates[] = $opensslCafile;
    }

    $candidates = array_merge($candidates, [
        '/etc/ssl/certs/ca-certificates.crt',
        '/etc/pki/tls/certs/ca-bundle.crt',
        '/etc/ssl/ca-bundle.pem',
        'C:\\Windows\\System32\\curl-ca-bundle.crt',
        'C:\\Program Files\\Git\\mingw64\\ssl\\certs\\ca-bundle.crt',
        'C:\\Program Files\\Git\\usr\\ssl\\certs\\ca-bundle.crt',
    ]);

    $clean = [];
    foreach ($candidates as $path) {
        $p = trim((string)$path);
        if ($p === '') {
            continue;
        }
        $clean[] = $p;
    }
    return array_values(array_unique($clean));
}

function payTezzTlsCaBundlePath(): string {
    $path = trim((string)env('PAY_TEZZCORP_CA_BUNDLE', ''));
    if ($path !== '' && is_readable($path)) {
        return $path;
    }

    foreach (payTezzTlsCandidateCaBundlePaths() as $candidate) {
        if (is_readable($candidate)) {
            return $candidate;
        }
    }

    return '';
}

function payTezzConfigured(): bool {
    return payTezzApiKey() !== '' && payTezzApiSecret() !== '';
}

function payTezzBuildSignature(string $timestamp, string $rawBody, string $secret): string {
    return hash_hmac('sha256', $timestamp . '.' . $rawBody, $secret);
}

/**
 * @return array{ok:bool,remote_ok:bool,http_status:int,url:string,body:string,json:array,headers:array,error:string,auth_mode:string}
 */
function payTezzRequest(string $method, string $path, ?array $payload = null, string $authMode = 'signed', int $timeoutSeconds = 20): array {
    $method = strtoupper(trim($method));
    if ($method === '') {
        $method = 'GET';
    }

    $url = trim($path);
    if (!preg_match('#^https?://#i', $url)) {
        $url = payTezzApiBaseUrl() . '/' . ltrim($url, '/');
    }

    $headers = ['Accept: application/json'];
    $rawBody = '';

    if ($payload !== null) {
        $rawBody = json_encode($payload, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
        if (!is_string($rawBody)) {
            return [
                'ok' => false,
                'remote_ok' => false,
                'http_status' => 0,
                'url' => $url,
                'body' => '',
                'json' => [],
                'headers' => [],
                'error' => 'json_encode_failed',
                'auth_mode' => $authMode,
            ];
        }
        $headers[] = 'Content-Type: application/json';
    }

    $apiKey = payTezzApiKey();
    $apiSecret = payTezzApiSecret();

    $authMode = strtolower(trim($authMode));
    if (!in_array($authMode, ['none', 'key', 'status', 'signed'], true)) {
        $authMode = 'signed';
    }

    if ($authMode !== 'none' && $apiKey === '') {
        return [
            'ok' => false,
            'remote_ok' => false,
            'http_status' => 0,
            'url' => $url,
            'body' => '',
            'json' => [],
            'headers' => [],
            'error' => 'missing_api_key',
            'auth_mode' => $authMode,
        ];
    }

    if (in_array($authMode, ['status', 'signed'], true) && $apiSecret === '') {
        return [
            'ok' => false,
            'remote_ok' => false,
            'http_status' => 0,
            'url' => $url,
            'body' => '',
            'json' => [],
            'headers' => [],
            'error' => 'missing_api_secret',
            'auth_mode' => $authMode,
        ];
    }

    if ($authMode === 'key' || $authMode === 'status' || $authMode === 'signed') {
        $headers[] = 'X-API-KEY: ' . $apiKey;
    }
    if ($authMode === 'status') {
        $headers[] = 'X-API-SECRET: ' . $apiSecret;
    }
    if ($authMode === 'signed') {
        $timestamp = (string)time();
        $headers[] = 'X-Timestamp: ' . $timestamp;
        $headers[] = 'X-Signature: ' . payTezzBuildSignature($timestamp, $rawBody, $apiSecret);
    }

    $ch = curl_init();
    curl_setopt($ch, CURLOPT_URL, $url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt($ch, CURLOPT_HEADER, true);
    curl_setopt($ch, CURLOPT_TIMEOUT, max(3, $timeoutSeconds));
    curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, max(3, min(10, $timeoutSeconds)));
    curl_setopt($ch, CURLOPT_FOLLOWLOCATION, true);
    curl_setopt($ch, CURLOPT_MAXREDIRS, 4);
    curl_setopt($ch, CURLOPT_CUSTOMREQUEST, $method);
    curl_setopt($ch, CURLOPT_HTTPHEADER, $headers);

    $verifyPeer = payTezzTlsVerifyPeer();
    curl_setopt($ch, CURLOPT_SSL_VERIFYPEER, $verifyPeer);
    curl_setopt($ch, CURLOPT_SSL_VERIFYHOST, $verifyPeer ? 2 : 0);
    if ($verifyPeer) {
        $caBundle = payTezzTlsCaBundlePath();
        if ($caBundle !== '' && is_readable($caBundle)) {
            curl_setopt($ch, CURLOPT_CAINFO, $caBundle);
        }
    }

    if ($rawBody !== '') {
        curl_setopt($ch, CURLOPT_POSTFIELDS, $rawBody);
    }

    $responseRaw = curl_exec($ch);
    $curlErrNo = curl_errno($ch);
    $curlErr = $curlErrNo !== 0 ? (string)curl_error($ch) : '';
    $httpStatus = (int)curl_getinfo($ch, CURLINFO_HTTP_CODE);
    $headerSize = (int)curl_getinfo($ch, CURLINFO_HEADER_SIZE);
    curl_close($ch);

    if (!is_string($responseRaw)) {
        $responseRaw = '';
    }

    $rawHeaders = $headerSize > 0 ? substr($responseRaw, 0, $headerSize) : '';
    $body = $headerSize > 0 ? substr($responseRaw, $headerSize) : $responseRaw;
    if (!is_string($body)) {
        $body = '';
    }

    $headersOut = [];
    foreach (preg_split('/\r\n|\r|\n/', (string)$rawHeaders) ?: [] as $line) {
        $line = trim((string)$line);
        if ($line !== '') {
            $headersOut[] = $line;
        }
    }

    $json = json_decode($body, true);
    if (!is_array($json)) {
        $json = [];
    }

    $httpOk = $httpStatus >= 200 && $httpStatus < 300;
    $remoteOk = array_key_exists('ok', $json) ? !empty($json['ok']) : $httpOk;
    $ok = $curlErrNo === 0 && $httpOk;

    return [
        'ok' => $ok,
        'remote_ok' => $remoteOk,
        'http_status' => $httpStatus,
        'url' => $url,
        'body' => $body,
        'json' => $json,
        'headers' => $headersOut,
        'error' => $curlErr,
        'auth_mode' => $authMode,
    ];
}

function payTezzErrorMessage(array $response): string {
    $json = is_array($response['json'] ?? null) ? $response['json'] : [];
    if (!empty($response['error'])) {
        return (string)$response['error'];
    }
    $msg = payTezzExtractPath($json, ['error.message', 'message', 'error', 'status_message'], '');
    $msg = trim((string)$msg);
    if ($msg !== '') {
        return $msg;
    }
    $status = (int)($response['http_status'] ?? 0);
    return $status > 0 ? ('gateway_http_' . $status) : 'gateway_request_failed';
}

function payTezzExtractPath(array $source, array $paths, $default = null) {
    foreach ($paths as $path) {
        $cursor = $source;
        $ok = true;
        foreach (explode('.', (string)$path) as $segment) {
            if (!is_array($cursor) || !array_key_exists($segment, $cursor)) {
                $ok = false;
                break;
            }
            $cursor = $cursor[$segment];
        }
        if ($ok && $cursor !== null && $cursor !== '') {
            return $cursor;
        }
    }
    return $default;
}

function payTezzNormalizeStatus(string $status): string {
    $s = strtolower(trim($status));
    if ($s === '') {
        return 'unknown';
    }
    if (in_array($s, ['paid', 'success', 'completed', 'captured', 'settled'], true)) {
        return 'paid';
    }
    if (in_array($s, ['created', 'initiated', 'pending', 'processing', 'active'], true)) {
        return 'pending';
    }
    if (in_array($s, ['failed', 'failure', 'expired', 'cancelled', 'canceled', 'rejected'], true)) {
        return 'failed';
    }
    return $s;
}

function payTezzIsPaidStatus(string $status): bool {
    return payTezzNormalizeStatus($status) === 'paid';
}

function payTezzExtractPaymentSummary(array $responseJson): array {
    $statusRaw = (string)payTezzExtractPath($responseJson, [
        'data.status',
        'data.payment.status',
        'status',
        'payment_status',
    ], '');

    $summary = [
        'order_id' => (string)payTezzExtractPath($responseJson, [
            'data.order_id',
            'data.payment.order_id',
            'order_id',
        ], ''),
        'checkout_session_id' => (string)payTezzExtractPath($responseJson, [
            'data.checkout.session_id',
            'data.checkout_session_id',
            'data.session_id',
            'session_id',
        ], ''),
        'provider_payment_id' => (string)payTezzExtractPath($responseJson, [
            'data.payment_id',
            'data.payment.id',
            'payment_id',
            'data.id',
        ], ''),
        'utr' => (string)payTezzExtractPath($responseJson, [
            'data.utr',
            'data.payment.utr',
            'data.rrn',
            'rrn',
        ], ''),
        'status_raw' => $statusRaw,
        'status' => payTezzNormalizeStatus($statusRaw),
        'amount' => (float)payTezzExtractPath($responseJson, [
            'data.amount',
            'data.payment.amount',
            'amount',
        ], 0),
        'currency' => strtoupper((string)payTezzExtractPath($responseJson, [
            'data.currency',
            'data.payment.currency',
            'currency',
        ], 'INR')),
        'upi_uri' => (string)payTezzExtractPath($responseJson, [
            'data.upi.uri',
            'data.upi.upi_uri',
            'data.upi_uri',
            'upi.uri',
        ], ''),
        'qr_code_url' => (string)payTezzExtractPath($responseJson, [
            'data.upi.qr_code_url',
            'data.upi.qr_url',
            'data.qr_code_url',
            'qr_code_url',
        ], ''),
        'payment_url' => (string)payTezzExtractPath($responseJson, [
            'data.checkout.url',
            'data.checkout_url',
            'data.payment_url',
            'payment_url',
        ], ''),
        'receipt_url' => (string)payTezzExtractPath($responseJson, [
            'data.receipt_url',
            'data.receipt.url',
            'receipt_url',
        ], ''),
        'message' => (string)payTezzExtractPath($responseJson, [
            'message',
            'data.message',
            'error.message',
        ], ''),
    ];

    if ($summary['currency'] === '' || strlen($summary['currency']) !== 3) {
        $summary['currency'] = 'INR';
    }

    return $summary;
}

function payTezzCreatePaymentIntent(array $payload): array {
    return payTezzRequest('POST', '/payments/intents', $payload, 'signed');
}

function payTezzGetPaymentStatus(string $orderId): array {
    return payTezzRequest('GET', '/payments/' . rawurlencode($orderId), null, 'status');
}

function payTezzVerifyUtr(array $payload): array {
    return payTezzRequest('POST', '/payments/verify-utr', $payload, 'signed');
}

function payTezzCreateCheckoutSession(array $payload): array {
    return payTezzRequest('POST', '/payments/checkout-sessions', $payload, 'signed');
}

function payTezzGetCheckoutSession(string $sessionId): array {
    return payTezzRequest('GET', '/payments/checkout-sessions/' . rawurlencode($sessionId), null, 'status');
}

function payTezzGetCheckout(string $sessionId): array {
    return payTezzRequest('GET', '/checkout/' . rawurlencode($sessionId), null, 'none');
}

function payTezzStartCheckout(string $sessionId, array $payload = []): array {
    return payTezzRequest('POST', '/checkout/' . rawurlencode($sessionId) . '/start', $payload, 'none');
}

function payTezzGetCheckoutStatus(string $sessionId): array {
    return payTezzRequest('GET', '/checkout/' . rawurlencode($sessionId) . '/status', null, 'none');
}

function payTezzGetCheckoutReceipt(string $sessionId): array {
    return payTezzRequest('GET', '/checkout/' . rawurlencode($sessionId) . '/receipt', null, 'none');
}

function payTezzOnboardingStart(array $payload): array {
    return payTezzRequest('POST', '/public/onboarding/start', $payload, 'none');
}

function payTezzOnboardingStatus(string $ref): array {
    return payTezzRequest('GET', '/public/onboarding/' . rawurlencode($ref) . '/status', null, 'none');
}

/**
 * @return array{ok:bool,remote_ok:bool,http_status:int,url:string,body:string,json:array,headers:array,error:string,auth_mode:string}
 */
function payTezzAdminRequest(string $method, string $path, ?array $payload = null, int $timeoutSeconds = 20): array {
    $method = strtoupper(trim($method));
    if ($method === '') {
        $method = 'GET';
    }

    $url = trim($path);
    if (!preg_match('#^https?://#i', $url)) {
        $url = payTezzAdminApiBaseUrl() . '/' . ltrim($url, '/');
    }

    $apiKey = payTezzAdminApiKey();
    if ($apiKey === '') {
        return [
            'ok' => false,
            'remote_ok' => false,
            'http_status' => 0,
            'url' => $url,
            'body' => '',
            'json' => [],
            'headers' => [],
            'error' => 'missing_admin_api_key',
            'auth_mode' => 'admin_key',
        ];
    }

    $headers = [
        'Accept: application/json',
        'X-ADMIN-API-KEY: ' . $apiKey,
    ];
    $rawBody = '';
    if ($payload !== null) {
        $rawBody = json_encode($payload, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
        if (!is_string($rawBody)) {
            return [
                'ok' => false,
                'remote_ok' => false,
                'http_status' => 0,
                'url' => $url,
                'body' => '',
                'json' => [],
                'headers' => [],
                'error' => 'json_encode_failed',
                'auth_mode' => 'admin_key',
            ];
        }
        $headers[] = 'Content-Type: application/json';
    }

    $ch = curl_init();
    curl_setopt($ch, CURLOPT_URL, $url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt($ch, CURLOPT_HEADER, true);
    curl_setopt($ch, CURLOPT_TIMEOUT, max(3, $timeoutSeconds));
    curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, max(3, min(10, $timeoutSeconds)));
    curl_setopt($ch, CURLOPT_FOLLOWLOCATION, true);
    curl_setopt($ch, CURLOPT_MAXREDIRS, 4);
    curl_setopt($ch, CURLOPT_CUSTOMREQUEST, $method);
    curl_setopt($ch, CURLOPT_HTTPHEADER, $headers);

    $verifyPeer = payTezzTlsVerifyPeer();
    curl_setopt($ch, CURLOPT_SSL_VERIFYPEER, $verifyPeer);
    curl_setopt($ch, CURLOPT_SSL_VERIFYHOST, $verifyPeer ? 2 : 0);
    if ($verifyPeer) {
        $caBundle = payTezzTlsCaBundlePath();
        if ($caBundle !== '' && is_readable($caBundle)) {
            curl_setopt($ch, CURLOPT_CAINFO, $caBundle);
        }
    }

    if ($rawBody !== '') {
        curl_setopt($ch, CURLOPT_POSTFIELDS, $rawBody);
    }

    $responseRaw = curl_exec($ch);
    $curlErrNo = curl_errno($ch);
    $curlErr = $curlErrNo !== 0 ? (string)curl_error($ch) : '';
    $httpStatus = (int)curl_getinfo($ch, CURLINFO_HTTP_CODE);
    $headerSize = (int)curl_getinfo($ch, CURLINFO_HEADER_SIZE);
    curl_close($ch);

    if (!is_string($responseRaw)) {
        $responseRaw = '';
    }

    $rawHeaders = $headerSize > 0 ? substr($responseRaw, 0, $headerSize) : '';
    $body = $headerSize > 0 ? substr($responseRaw, $headerSize) : $responseRaw;
    if (!is_string($body)) {
        $body = '';
    }

    $headersOut = [];
    foreach (preg_split('/\r\n|\r|\n/', (string)$rawHeaders) ?: [] as $line) {
        $line = trim((string)$line);
        if ($line !== '') {
            $headersOut[] = $line;
        }
    }

    $json = json_decode($body, true);
    if (!is_array($json)) {
        $json = [];
    }

    $httpOk = $httpStatus >= 200 && $httpStatus < 300;
    $remoteOk = array_key_exists('ok', $json) ? !empty($json['ok']) : $httpOk;

    return [
        'ok' => $curlErrNo === 0 && $httpOk,
        'remote_ok' => $remoteOk,
        'http_status' => $httpStatus,
        'url' => $url,
        'body' => $body,
        'json' => $json,
        'headers' => $headersOut,
        'error' => $curlErr,
        'auth_mode' => 'admin_key',
    ];
}

function payTezzAdminUpsertMerchant(array $payload): array {
    return payTezzAdminRequest('POST', '/admin/merchants/upsert-external', $payload);
}

function payTezzAdminLinkMerchant(int $merchantId, array $payload): array {
    return payTezzAdminRequest('POST', '/admin/merchants/' . max(1, $merchantId) . '/external-link', $payload);
}

function payTezzAdminGetMerchantByExternal(string $externalSystem, string $externalRef): array {
    return payTezzAdminRequest(
        'GET',
        '/admin/merchants/external/' . rawurlencode($externalSystem) . '/' . rawurlencode($externalRef),
        null
    );
}
