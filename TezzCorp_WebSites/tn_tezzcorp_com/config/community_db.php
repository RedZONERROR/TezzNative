<?php
declare(strict_types=1);

/**
 * config/community_db.php — TezzNative Community Database Module
 * Handles community user registration, contributor listing, and table setup.
 *
 * Requires: config/db.php (tn_pdo())
 */

if (!function_exists('tn_pdo')) {
    require_once __DIR__ . '/db.php';
}

// ── Table bootstrap: run once on first community page load ───────────────────
function tn_community_boot(PDO $pdo): void {
    $pdo->exec("CREATE TABLE IF NOT EXISTS tn_community_users (
        id           INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
        display_name VARCHAR(100)  NOT NULL,
        email        VARCHAR(190)  NOT NULL UNIQUE,
        password_hash VARCHAR(255) NOT NULL,
        bio          TEXT          DEFAULT NULL,
        github_url   VARCHAR(255)  DEFAULT NULL,
        role         ENUM('developer','contributor','tester','founder') DEFAULT 'developer',
        is_verified  TINYINT(1)    NOT NULL DEFAULT 0,
        join_date    DATE          NOT NULL DEFAULT (CURDATE()),
        created_at   DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP,
        updated_at   DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
        INDEX idx_email (email),
        INDEX idx_created (created_at),
        INDEX idx_role (role)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci");

    $pdo->exec("CREATE TABLE IF NOT EXISTS tn_community_posts (
        id          INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
        user_id     INT UNSIGNED NOT NULL,
        title       VARCHAR(255) NOT NULL,
        body        TEXT         NOT NULL,
        category    ENUM('showcase','question','bug-report','announcement','general') DEFAULT 'general',
        upvotes     INT UNSIGNED NOT NULL DEFAULT 0,
        created_at  DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
        FOREIGN KEY (user_id) REFERENCES tn_community_users(id) ON DELETE CASCADE,
        INDEX idx_category (category),
        INDEX idx_created (created_at)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci");
}

// ── Register a new community user ────────────────────────────────────────────
function tezz_register_user(PDO $pdo, array $post): array {
    // Ensure table exists
    tn_community_boot($pdo);

    $name  = trim((string)($post['display_name'] ?? ''));
    $email = strtolower(trim((string)($post['email'] ?? '')));
    $pass  = (string)($post['password'] ?? '');
    $bio   = trim((string)($post['bio'] ?? ''));
    $github = trim((string)($post['github_url'] ?? ''));

    // Validation
    if (strlen($name) < 2 || strlen($name) > 100) {
        return ['ok' => false, 'message' => 'Name must be between 2–100 characters.'];
    }
    if (!filter_var($email, FILTER_VALIDATE_EMAIL)) {
        return ['ok' => false, 'message' => 'Please enter a valid email address.'];
    }
    if (strlen($pass) < 8) {
        return ['ok' => false, 'message' => 'Password must be at least 8 characters.'];
    }
    if ($github !== '' && !filter_var($github, FILTER_VALIDATE_URL)) {
        return ['ok' => false, 'message' => 'GitHub URL must be a valid URL (or leave blank).'];
    }

    // Duplicate check
    $chk = $pdo->prepare("SELECT id FROM tn_community_users WHERE email = ? LIMIT 1");
    $chk->execute([$email]);
    if ($chk->fetch()) {
        return ['ok' => false, 'message' => 'That email is already registered. Welcome back!'];
    }

    // Insert
    $hash = password_hash($pass, PASSWORD_BCRYPT, ['cost' => 12]);
    $stmt = $pdo->prepare("INSERT INTO tn_community_users
        (display_name, email, password_hash, bio, github_url, created_at)
        VALUES (?, ?, ?, ?, ?, NOW())");

    $stmt->execute([
        $name,
        $email,
        $hash,
        $bio ?: null,
        $github ?: null,
    ]);

    return [
        'ok'      => true,
        'message' => "Welcome, {$name}! You've joined the TezzNative community. 🎉",
        'user_id' => (int)$pdo->lastInsertId(),
    ];
}

// ── Fetch top N contributors ─────────────────────────────────────────────────
function tezz_fetch_top_contributors(PDO $pdo, int $limit = 8): array {
    try {
        tn_community_boot($pdo);
        $stmt = $pdo->prepare("SELECT display_name, role, bio, github_url,
            DATE_FORMAT(created_at, '%d %b %Y') AS joined
            FROM tn_community_users
            ORDER BY created_at ASC
            LIMIT ?");
        $stmt->bindValue(1, $limit, PDO::PARAM_INT);
        $stmt->execute();
        return $stmt->fetchAll(PDO::FETCH_ASSOC);
    } catch (Throwable $e) {
        return [];
    }
}

// ── Get total community member count ─────────────────────────────────────────
function tezz_community_count(PDO $pdo): int {
    try {
        $stmt = $pdo->query("SELECT COUNT(*) FROM tn_community_users");
        return (int)$stmt->fetchColumn();
    } catch (Throwable $e) {
        return 0;
    }
}
