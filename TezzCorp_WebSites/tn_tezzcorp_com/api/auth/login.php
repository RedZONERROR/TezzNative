<?php
require_once '../../config/config.php';

header('Content-Type: application/json; charset=utf-8');

$input = json_decode(file_get_contents('php://input'), true) ?: [];

// CSRF token from header
$csrfToken = $_SERVER['HTTP_X_CSRF_TOKEN'] ?? '';
if (!validateCsrfToken($csrfToken)) {
    jsonResponse(['success' => false, 'message' => 'Invalid CSRF token'], 403);
}

$email = strtolower(trim((string)($input['email'] ?? '')));
$password = (string)($input['password'] ?? '');
$remember = (bool)($input['remember'] ?? false);

if ($email === '' || $password === '') {
    jsonResponse(['success' => false, 'message' => 'Email and password are required'], 422);
}

try {
    $pdo = getDBConnection();

    // Find user (your schema: password_hash)
    $stmt = $pdo->prepare("
        SELECT id, organization_id, email, password_hash, name, status
        FROM users
        WHERE email = ?
          AND deleted_at IS NULL
        ORDER BY created_at DESC
        LIMIT 1
    ");
    $stmt->execute([$email]);
    $user = $stmt->fetch();

    // Not found or not active
    if (!$user || $user['status'] !== 'active') {
        // optional security log
        try {
            $pdo->prepare("
                INSERT INTO security_events
                (id, organization_id, product_id, event_type, reference, ip_address, user_agent, severity, message, created_at)
                VALUES
                (?, NULL, NULL, 'bruteforce_attempt', ?, ?, ?, 'low', ?, UTC_TIMESTAMP(3))
            ")->execute([
                uuid32(),
                $email,
                $_SERVER['REMOTE_ADDR'] ?? null,
                $_SERVER['HTTP_USER_AGENT'] ?? null,
                'Login failed: user not found or inactive'
            ]);
        } catch (Throwable $e) { /* ignore */ }

        jsonResponse(['success' => false, 'message' => 'Invalid email or password'], 401);
    }

    // Verify password
    if (!password_verify($password, $user['password_hash'])) {
        // optional security log
        try {
            $pdo->prepare("
                INSERT INTO security_events
                (id, organization_id, product_id, event_type, reference, ip_address, user_agent, severity, message, created_at)
                VALUES
                (?, ?, NULL, 'bruteforce_attempt', ?, ?, ?, 'medium', ?, UTC_TIMESTAMP(3))
            ")->execute([
                uuid32(),
                $user['organization_id'],
                $email,
                $_SERVER['REMOTE_ADDR'] ?? null,
                $_SERVER['HTTP_USER_AGENT'] ?? null,
                'Login failed: wrong password'
            ]);
        } catch (Throwable $e) { /* ignore */ }

        jsonResponse(['success' => false, 'message' => 'Invalid email or password'], 401);
    }

    // Backfill role mapping for legacy users with no assigned role.
    try {
        $roleCountSt = $pdo->prepare("SELECT COUNT(*) FROM user_roles WHERE user_id = ?");
        $roleCountSt->execute([$user['id']]);
        $roleCount = (int)$roleCountSt->fetchColumn();
        if ($roleCount === 0) {
            $fallbackRoleSt = $pdo->prepare("
                SELECT id
                FROM roles
                WHERE organization_id = ?
                ORDER BY FIELD(name, 'Owner', 'Admin', 'Manager', 'Staff', 'Support', 'Viewer'), name
                LIMIT 1
            ");
            $fallbackRoleSt->execute([$user['organization_id']]);
            $fallbackRoleId = (string)($fallbackRoleSt->fetchColumn() ?: '');
            if ($fallbackRoleId !== '') {
                $pdo->prepare("
                    INSERT INTO user_roles (user_id, role_id)
                    VALUES (?, ?)
                    ON DUPLICATE KEY UPDATE user_id = VALUES(user_id)
                ")->execute([$user['id'], $fallbackRoleId]);
            }
        }
    } catch (Throwable $e) {
        // Do not block login if role backfill fails.
    }

    // Success (prevent session fixation)
    session_regenerate_id(true);

    $_SESSION['user_id'] = $user['id'];
    $_SESSION['organization_id'] = $user['organization_id'];
    $_SESSION['user_type'] = 'user';

    // Update last login
    $pdo->prepare("
        UPDATE users
        SET last_login_at = UTC_TIMESTAMP(3),
            last_login_ip = ?
        WHERE id = ?
    ")->execute([$_SERVER['REMOTE_ADDR'] ?? null, $user['id']]);

    // Remember me: set token in DB + secure cookie
    if ($remember) {
        $token = bin2hex(random_bytes(32)); // 64 chars hex
        $expiresAt = (new DateTimeImmutable('now', new DateTimeZone('UTC')))
            ->modify('+30 days')
            ->format('Y-m-d H:i:s.u');

        $pdo->prepare("
            UPDATE users
            SET remember_token = ?,
                remember_expires_at = ?
            WHERE id = ?
        ")->execute([$token, $expiresAt, $user['id']]);

        setcookie('remember_token', $token, [
            'expires'  => time() + (86400 * 30),
            'path'     => '/',
            'domain'   => cookieDomain(),
            'secure'   => isHttps(),
            'httponly' => true,
            'samesite' => 'Lax',
        ]);
    } else {
        // optional: clear old remember token on normal login
        $pdo->prepare("
            UPDATE users
            SET remember_token = NULL,
                remember_expires_at = NULL
            WHERE id = ?
        ")->execute([$user['id']]);

        // clear cookie
        setcookie('remember_token', '', [
            'expires'  => time() - 3600,
            'path'     => '/',
            'domain'   => cookieDomain(),
            'secure'   => isHttps(),
            'httponly' => true,
            'samesite' => 'Lax',
        ]);
    }

    jsonResponse([
        'success' => true,
        'redirect' => CRM_URL . '/'
    ]);

} catch (Throwable $e) {
    error_log("Login error: " . $e->getMessage() . PHP_EOL, 3, LOG_PATH . '/error.log');
    jsonResponse(['success' => false, 'message' => 'Server error. Please try again.'], 500);
}
?>
