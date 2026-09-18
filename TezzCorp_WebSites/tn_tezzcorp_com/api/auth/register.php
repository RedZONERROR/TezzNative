<?php
require_once __DIR__ . '/../../config/config.php';

header('Content-Type: application/json; charset=utf-8');

$input = json_decode(file_get_contents('php://input'), true) ?: [];

// CSRF token from header
$csrfToken = $_SERVER['HTTP_X_CSRF_TOKEN'] ?? '';
if (!validateCsrfToken($csrfToken)) {
    jsonResponse(['success' => false, 'message' => 'Invalid CSRF token. Please refresh and try again.'], 403);
}

$name     = trim((string)($input['name'] ?? ''));
$email    = strtolower(trim((string)($input['email'] ?? '')));
$phone    = trim((string)($input['phone'] ?? ''));
$password = (string)($input['password'] ?? '');

if ($name === '' || $email === '' || $phone === '' || $password === '') {
    jsonResponse(['success' => false, 'message' => 'All fields are required.'], 422);
}
if (!filter_var($email, FILTER_VALIDATE_EMAIL)) {
    jsonResponse(['success' => false, 'message' => 'Invalid email address.'], 422);
}
if (strlen($password) < 8) {
    jsonResponse(['success' => false, 'message' => 'Password must be at least 8 characters.'], 422);
}

// Normalize phone (keep + and digits)
$phoneNorm = preg_replace('/[^\d+]/', '', $phone);
if (strlen(preg_replace('/\D+/', '', $phoneNorm)) < 10) {
    jsonResponse(['success' => false, 'message' => 'Invalid phone number.'], 422);
}

try {
    $pdo = getDBConnection();
    $pdo->beginTransaction();

    /**
     * Choose org for your main product.
     * You can change these later if you add multiple orgs.
     */
    $orgDomain = 'tezzcorp.com';
    $orgName   = 'Tezz Corp';

    $orgStmt = $pdo->prepare("SELECT id FROM organizations WHERE domain = ? LIMIT 1");
    $orgStmt->execute([$orgDomain]);
    $org = $orgStmt->fetch();

    if (!$org) {
        $orgId = uuid32();
        $pdo->prepare("
            INSERT INTO organizations (id, name, legal_name, domain, status, created_at, updated_at)
            VALUES (?, ?, ?, ?, 'active', UTC_TIMESTAMP(3), UTC_TIMESTAMP(3))
        ")->execute([$orgId, $orgName, $orgName, $orgDomain]);
    } else {
        $orgId = $org['id'];
    }

    // Decide default role for the new user.
    $userCountSt = $pdo->prepare("
        SELECT COUNT(*)
        FROM users
        WHERE organization_id = ?
          AND deleted_at IS NULL
    ");
    $userCountSt->execute([$orgId]);
    $existingUserCount = (int)$userCountSt->fetchColumn();
    $defaultRoleName = $existingUserCount === 0 ? 'Owner' : 'Staff';

    // Check if email already exists in this org
    $chk = $pdo->prepare("SELECT id FROM users WHERE organization_id = ? AND email = ? AND deleted_at IS NULL LIMIT 1");
    $chk->execute([$orgId, $email]);
    if ($chk->fetch()) {
        $pdo->rollBack();
        jsonResponse(['success' => false, 'message' => 'Email already registered. Please login.'], 409);
    }

    $userId = uuid32();
    $hash = password_hash($password, PASSWORD_DEFAULT);

    // Insert user
    $pdo->prepare("
        INSERT INTO users
          (id, organization_id, email, phone, password_hash, name, status, created_at, updated_at)
        VALUES
          (?, ?, ?, ?, ?, ?, 'active', UTC_TIMESTAMP(3), UTC_TIMESTAMP(3))
    ")->execute([$userId, $orgId, $email, $phoneNorm, $hash, $name]);

    // Ensure user has at least one role for CRM access.
    $roleSt = $pdo->prepare("
        SELECT id
        FROM roles
        WHERE organization_id = ? AND name = ?
        LIMIT 1
    ");
    $roleSt->execute([$orgId, $defaultRoleName]);
    $roleId = (string)($roleSt->fetchColumn() ?: '');

    if ($roleId === '') {
        $fallbackRoleSt = $pdo->prepare("
            SELECT id
            FROM roles
            WHERE organization_id = ?
            ORDER BY FIELD(name, 'Owner', 'Admin', 'Manager', 'Staff', 'Support', 'Viewer'), name
            LIMIT 1
        ");
        $fallbackRoleSt->execute([$orgId]);
        $roleId = (string)($fallbackRoleSt->fetchColumn() ?: '');
    }

    if ($roleId !== '') {
        $pdo->prepare("
            INSERT INTO user_roles (user_id, role_id)
            VALUES (?, ?)
            ON DUPLICATE KEY UPDATE user_id = VALUES(user_id)
        ")->execute([$userId, $roleId]);
    }

    // Insert profile (optional)
    $profileId = uuid32();
    $pdo->prepare("
        INSERT INTO user_profiles (id, user_id, first_name, last_name, created_at, updated_at)
        VALUES (?, ?, ?, NULL, UTC_TIMESTAMP(3), UTC_TIMESTAMP(3))
    ")->execute([$profileId, $userId, $name]);

    // Audit log (optional)
    try {
        $pdo->prepare("
          INSERT INTO audit_logs
          (id, organization_id, actor_user_id, action, entity_type, entity_id, ip_address, user_agent, old_data, new_data, created_at)
          VALUES
          (?, ?, NULL, 'register', 'user', ?, ?, ?, NULL, ?, UTC_TIMESTAMP(3))
        ")->execute([
            uuid32(),
            $orgId,
            $userId,
            $_SERVER['REMOTE_ADDR'] ?? null,
            $_SERVER['HTTP_USER_AGENT'] ?? null,
            json_encode(['email' => $email, 'phone' => $phoneNorm, 'name' => $name], JSON_UNESCAPED_SLASHES|JSON_UNESCAPED_UNICODE)
        ]);
    } catch (Throwable $e) { /* ignore */ }

    $pdo->commit();

    // Auto login after register
    session_regenerate_id(true);
    $_SESSION['user_id'] = $userId;
    $_SESSION['organization_id'] = $orgId;
    $_SESSION['user_type'] = 'user';

    // Auto "remember me" on register (30 days)
    $token = bin2hex(random_bytes(32)); // 64 hex chars
    $expiresAtUtc = (new DateTimeImmutable('now', new DateTimeZone('UTC')))
        ->modify('+30 days')
        ->format('Y-m-d H:i:s.u');

    try {
        $pdo->prepare("
            UPDATE users
            SET remember_token = ?,
                remember_expires_at = ?
            WHERE id = ?
        ")->execute([$token, $expiresAtUtc, $userId]);

        setcookie('remember_token', $token, [
            'expires'  => time() + (86400 * 30),
            'path'     => '/',
            'domain'   => cookieDomain(),
            'secure'   => isHttps(),
            'httponly' => true,
            'samesite' => 'Lax',
        ]);
    } catch (Throwable $e) {
        // If cookie/token fails, registration still successful
    }

    jsonResponse([
        'success' => true,
        'message' => 'Registration successful',
        'redirect' => CRM_URL . '/index.php'
    ]);

} catch (Throwable $e) {
    if (isset($pdo) && $pdo instanceof PDO && $pdo->inTransaction()) {
        $pdo->rollBack();
    }
    error_log("Register error: " . $e->getMessage() . PHP_EOL, 3, LOG_PATH . '/error.log');
    jsonResponse(['success' => false, 'message' => 'Server error. Please try again.'], 500);
}
?>
