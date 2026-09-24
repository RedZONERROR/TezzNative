<?php
declare(strict_types=1);
require_once __DIR__ . '/_common.php';
tn_doc_boot('/docs/package-trust', 'package-trust');
tn_head('Package Trust', 'TezzNative package trust gates for tezz commands, semantic versions, lockfiles, registry metadata, package checksums, and generated package docs.', 'docs', '/docs/package-trust');
tn_page_shell_start('Package trust', 'Reproducible package metadata for the first-party SDK set.', 'Milestone 5 gates the public tezz tool, launchers, SemVer package pins, lock/registry parity, package checksums, and generated package inventory docs.');
$commands = [
    ['tezz init', 'Creates project metadata, cache directories, and template files with SemVer dependency pins.'],
    ['tezz add', 'Installs a package, verifies the package checksum, updates tezz.mod, and updates tezz.lock.'],
    ['tezz remove', 'Removes a dependency and rewrites tezz.mod plus tezz.lock deterministically.'],
    ['tezz update', 'Runs explicit SDK update modes: check, install, reinstall, or uninstall.'],
    ['tezz lock', 'Regenerates tezz.lock from tezz.mod and local first-party package sources.'],
    ['tezz publish', 'Regenerates lock and registry metadata without hidden network side effects.'],
    ['tezz test', 'Keeps the conformance, stdlib, tooling, runtime, and native lanes reachable from the tool.'],
    ['tezz build --release', 'Builds with release defaults and refreshes dependency locks when project metadata exists.'],
];
$targets = ['JSON', 'CLI argument parser', 'Logging', 'Config file support', 'Regex', 'SQLite binding', 'HTTP client/server polish', 'Testing assertions', 'Benchmark helpers'];
?>
  <section class="tn-section">
    <div class="tn-container tn-doc-layout">
      <?php tn_doc_nav(); ?>
      <div class="tn-grid">
        <article class="tn-panel">
          <h2>Gate</h2>
          <?php tn_code('Windows', 'powershell -ExecutionPolicy Bypass -File .\tests\conformance\run-package-trust.ps1 -Tezzc .\TezzNative-language\build\tezzc.exe'); ?>
          <?php tn_code('Linux / WSL', 'bash tests/conformance/run-package-trust.sh ./TezzNative-language/bin/tezzc-linux-x64'); ?>
          <?php tn_code('Expected', 'PACKAGE_TRUST_SUMMARY passed=12 failed=0'); ?>
        </article>
        <article class="tn-panel">
          <h2>Commands</h2>
          <div class="tn-table-wrap">
            <table class="tn-table">
              <thead><tr><th>Command</th><th>Contract</th></tr></thead>
              <tbody>
              <?php foreach ($commands as [$cmd, $contract]): ?>
                <tr><td><code><?= htmlspecialchars($cmd) ?></code></td><td><?= htmlspecialchars($contract) ?></td></tr>
              <?php endforeach; ?>
              </tbody>
            </table>
          </div>
        </article>
        <article class="tn-panel">
          <h2>Metadata</h2>
          <ul class="tn-list">
            <li><code>tezz.mod</code> declares lowercase package names, SemVer package pins, registry URL, and module root.</li>
            <li><code>tezz.lock</code> stores sorted <code>name@version CHECKSUM URL</code> rows with payload metadata.</li>
            <li><code>registry.tnx</code> stores sorted <code>name@version URL CHECKSUM</code> rows with matching payload metadata.</li>
            <li>Package checksums are deterministic 8-hex source-byte hashes with CRLF normalized to LF; release archives remain SHA-256 verified.</li>
          </ul>
        </article>
        <article class="tn-panel">
          <h2>First-Party Targets</h2>
          <div class="tn-pill-row">
            <?php foreach ($targets as $target): ?>
              <span class="tn-pill"><?= htmlspecialchars($target) ?></span>
            <?php endforeach; ?>
          </div>
        </article>
      </div>
    </div>
  </section>
<?php tn_page_shell_end(); tn_footer(); ?>

