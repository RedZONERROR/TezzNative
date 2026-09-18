<?php
declare(strict_types=1);

require dirname(__DIR__) . '/lib/bootstrap.php';

$config = tezz_load_config();
tezz_configure_timezone($config);

$version = array_merge(tezz_current_version_info(), [
    'runtime' => 'production',
    'site' => (string)($config['site_name'] ?? 'TezzNative'),
    'site_url' => (string)($config['site_url'] ?? ''),
    'build_date' => date('Y-m-d'),
]);
$update = tezz_update_payload($config, tezz_clean_text((string)($_GET['platform'] ?? ''), 80), tezz_clean_text((string)($_GET['version'] ?? ''), 60), 'version');

$releases = [];
try {
    $pdo = tezz_pdo($config);
    tezz_bootstrap_site($pdo);
    tezz_seed_release_from_config($pdo);
    $releases = tezz_fetch_versions($pdo, 10);
} catch (Throwable $_e) {
}

tezz_json([
    'ok' => true,
    'data' => $version,
    'update' => $update,
    'release_history' => $releases,
]);
