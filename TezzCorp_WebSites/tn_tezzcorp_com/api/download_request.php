<?php
declare(strict_types=1);

require dirname(__DIR__) . '/lib/bootstrap.php';

$config = tezz_load_config();
tezz_configure_timezone($config);
$root = dirname(__DIR__);
$artifactKey = tezz_clean_text((string)($_GET['artifact'] ?? $_POST['artifact'] ?? 'windows-sdk'), 80);
$source = tezz_clean_text((string)($_GET['source'] ?? $_POST['source'] ?? 'website'), 80);
$format = strtolower(tezz_clean_text((string)($_GET['format'] ?? $_POST['format'] ?? ''), 20));

$artifact = tezz_find_download_artifact($root, $artifactKey);
if (!$artifact) {
    tezz_json([
        'ok' => false,
        'error' => 'unknown artifact',
        'artifacts' => array_map(static fn(array $item): string => (string)$item['key'], tezz_download_artifacts($root)),
    ], 404);
}

try {
    $pdo = tezz_pdo($config);
    tezz_record_download_request($pdo, $config, $artifact, $source);
} catch (Throwable $_e) {
}

if ($format === 'json') {
    tezz_json([
        'ok' => true,
        'artifact' => $artifact,
    ]);
}

header('Location: ' . (string)$artifact['href'], true, 302);
exit;
