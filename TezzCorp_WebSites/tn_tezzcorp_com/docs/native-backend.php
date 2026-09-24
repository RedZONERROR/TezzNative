<?php
declare(strict_types=1);
require_once __DIR__ . '/_common.php';
tn_doc_boot('/docs/native-backend', 'native-backend');
tn_head('Native Backend Reliability', 'TezzNative native backend reliability matrix, reproducibility gates, and target support boundaries.', 'docs', '/docs/native-backend');
tn_page_shell_start('Native backend reliability', 'Windows/Linux x64 first, with evidence.', 'Native claims are gated by smoke tests, byte-reproducibility checks, release manifests, and explicit unsupported-target failures.');
$targets = [
    ['Windows x64', 'x86_64', 'PE/COFF executable', 'GitHub Actions windows-2022 plus local smoke'],
    ['Linux x64', 'linux', 'ELF executable', 'GitHub Actions ubuntu-22.04 plus Debian/WSL smoke'],
];
$gates = [
    ['Native smoke', 'run-native-smoke.*', 'Build, verify, execute, and compare output for native feature coverage.'],
    ['Native reliability', 'run-native-reliability.*', 'Build deterministic native artifacts twice and compare SHA-256 output.'],
    ['Unsupported target', 'Included in reliability', 'Unknown targets fail closed without emitting an artifact.'],
    ['Release manifest', 'verify_release_manifest.ps1', 'Published SDK artifacts and checksums verify before install.'],
];
$features = [
    ['Executable format', 'Gated', 'Gated', 'PE/COFF and ELF verification run with --verify.'],
    ['Entrypoint, loops, calls', 'Gated', 'Gated', 'Hello, loop/math, stack arguments, and nested calls.'],
    ['Strings and globals', 'Gated', 'Gated', 'String ops/transforms and global string data.'],
    ['Structs, vectors, arenas', 'Gated', 'Gated', 'Struct arrays, vector helpers, and arena allocation/reset.'],
    ['IO/path/process/time', 'Gated', 'Gated', 'File wrappers, directory listing/glob, process output, and time helpers.'],
    ['Local networking/HTTP', 'Gated', 'Gated', 'Loopback TCP, socket options, route helpers, and keep-alive response reads.'],
    ['DNS-backed sockets', 'Beta gap', 'Beta gap', 'Endpoint parsing is gated; real DNS socket execution is not promoted yet.'],
    ['Public-network HTTP', 'Beta gap', 'Beta gap', 'Local loopback is gated; public network execution is not promoted yet.'],
    ['macOS and ARM64', 'Planned', 'Planned', 'Not a primary release promise until hosted gates exist.'],
];
?>
  <section class="tn-section">
    <div class="tn-container tn-doc-layout">
      <?php tn_doc_nav(); ?>
      <div class="tn-grid">
        <article class="tn-panel">
          <h2>Primary Targets</h2>
          <div class="tn-table-wrap">
            <table class="tn-table">
              <thead><tr><th>Target</th><th>Build Target</th><th>Artifact</th><th>Gate</th></tr></thead>
              <tbody>
              <?php foreach ($targets as [$target, $build, $artifact, $gate]): ?>
                <tr><td><?= htmlspecialchars($target) ?></td><td><?= htmlspecialchars($build) ?></td><td><?= htmlspecialchars($artifact) ?></td><td><?= htmlspecialchars($gate) ?></td></tr>
              <?php endforeach; ?>
              </tbody>
            </table>
          </div>
        </article>
        <article class="tn-panel">
          <h2>Reliability Gates</h2>
          <div class="tn-table-wrap">
            <table class="tn-table">
              <thead><tr><th>Gate</th><th>Command</th><th>Purpose</th></tr></thead>
              <tbody>
              <?php foreach ($gates as [$gate, $command, $purpose]): ?>
                <tr><td><?= htmlspecialchars($gate) ?></td><td><?= htmlspecialchars($command) ?></td><td><?= htmlspecialchars($purpose) ?></td></tr>
              <?php endforeach; ?>
              </tbody>
            </table>
          </div>
        </article>
        <article class="tn-panel">
          <h2>Feature Matrix</h2>
          <div class="tn-table-wrap">
            <table class="tn-table">
              <thead><tr><th>Area</th><th>Windows x64</th><th>Linux x64</th><th>Notes</th></tr></thead>
              <tbody>
              <?php foreach ($features as [$area, $windows, $linux, $notes]): ?>
                <tr><td><?= htmlspecialchars($area) ?></td><td><?= htmlspecialchars($windows) ?></td><td><?= htmlspecialchars($linux) ?></td><td><?= htmlspecialchars($notes) ?></td></tr>
              <?php endforeach; ?>
              </tbody>
            </table>
          </div>
        </article>
      </div>
    </div>
  </section>
<?php tn_page_shell_end(); tn_footer(); ?>
