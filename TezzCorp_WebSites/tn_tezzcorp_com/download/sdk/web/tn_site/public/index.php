<?php
$version_path = __DIR__ . '/../../../version.json';
$version_data = [
  'language' => 'TezzNative',
  'version' => 'dev',
  'channel' => 'release-candidate',
  'api' => 'v1'
];
if (is_file($version_path)) {
  $raw = file_get_contents($version_path);
  if ($raw !== false) {
    $json = json_decode($raw, true);
    if (is_array($json)) {
      $version_data = array_merge($version_data, $json);
    }
  }
}
$language = htmlspecialchars((string)$version_data['language'], ENT_QUOTES, 'UTF-8');
$version = htmlspecialchars((string)$version_data['version'], ENT_QUOTES, 'UTF-8');
$channel = htmlspecialchars((string)$version_data['channel'], ENT_QUOTES, 'UTF-8');
$api = htmlspecialchars((string)$version_data['api'], ENT_QUOTES, 'UTF-8');
$build_date = gmdate('Y-m-d');
?>
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title><?php echo $language; ?> Production Lane</title>
  <meta name="description" content="TezzNative production lane for compiler releases, SDK downloads, and CI status.">
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Space+Grotesk:wght@400;500;700&family=IBM+Plex+Mono:wght@400;600&display=swap" rel="stylesheet">
  <link rel="stylesheet" href="/assets/site.css">
</head>
<body>
  <div class="bg-grid" aria-hidden="true"></div>
  <main class="shell">
    <header class="hero reveal">
      <p class="eyebrow">TezzCorp Language Platform</p>
      <h1><?php echo $language; ?> Production Lane</h1>
      <p class="lede">
        Stable compiler lane with reproducible builds, signed release manifests, and website-distributed SDK bundles.
      </p>
      <div class="meta-row">
        <span class="chip">Version <?php echo $version; ?></span>
        <span class="chip">Channel <?php echo $channel; ?></span>
        <span class="chip">API <?php echo $api; ?></span>
        <span class="chip">UTC <?php echo $build_date; ?></span>
      </div>
      <div class="cta-row">
        <a class="btn btn-primary" href="/download/install.sh">Install on Linux/macOS</a>
        <a class="btn btn-secondary" href="/download/install.ps1">Install on Windows</a>
        <a class="btn btn-link" href="https://github.com/TezzCorp/TezzNative" target="_blank" rel="noopener">GitHub Repository</a>
      </div>
    </header>

    <section class="grid reveal" aria-label="Production Gates">
      <article class="card">
        <h2>Gate Stack</h2>
        <ul>
          <li>`release-policy-check` + non-empty manifest enforcement</li>
          <li>`reprocheck` + `release-artifacts --verify-repro`</li>
          <li>Linux/Windows/macOS CI for conformance + runtime lanes</li>
        </ul>
      </article>
      <article class="card">
        <h2>Binary Layout</h2>
        <ul>
          <li>Canonical compiler outputs in `bin/`</li>
          <li>Compatibility mirrors in `build/`</li>
          <li>Launchers and SDK packaging resolve `bin/` first</li>
        </ul>
      </article>
      <article class="card">
        <h2>Download Bundle</h2>
        <ul>
          <li>SDK archives published under `/download`</li>
          <li>Per-file SHA256 checksum sidecars</li>
          <li>Release + reproducibility manifests exposed for audit</li>
        </ul>
      </article>
    </section>

    <section class="panel reveal" aria-label="Quick Start">
      <h2>Quick Start</h2>
      <pre><code>curl -fsSL https://tn.tezzcorp.com/download/install.sh | bash
powershell -ExecutionPolicy Bypass -File install.ps1</code></pre>
      <p>
        Add your TezzNative `bin` directory to PATH for direct `tezz` and `tezzc` access from any shell.
      </p>
    </section>
  </main>

  <footer class="footer reveal">
    <p>TezzNative production lane, shipped by TezzCorp.</p>
  </footer>
  <script src="/assets/site.js"></script>
</body>
</html>
