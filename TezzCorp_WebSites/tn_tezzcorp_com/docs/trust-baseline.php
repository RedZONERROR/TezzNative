<?php
declare(strict_types=1);
require_once __DIR__ . '/_common.php';
tn_doc_boot('/docs/trust-baseline', 'trust');
tn_head('Public Trust Baseline', 'TezzNative public trust baseline, verified claims, claim boundaries, and publication rules.', 'docs', '/docs/trust-baseline');
tn_page_shell_start('Public trust baseline', 'Honest claims users can verify.', 'TezzNative should be ambitious, but every public claim needs a matching doc, test, benchmark, or release artifact.');
$verified = [
    ['Stable-core checks', 'docs/CONFORMANCE.md and tests/conformance/run.*'],
    ['Windows/Linux native backend', 'docs/NATIVE_BACKEND.md plus run-native-smoke.* and run-native-reliability.*'],
    ['Starter C ABI', 'docs/C_ABI.md and tests/conformance/run-abi.*'],
    ['Benchmarks', 'docs/BENCHMARKS.md and benchmarks/'],
    ['Release integrity', 'docs/RELEASE_ENGINEERING.md and release_manifest.json'],
    ['Stability labels', 'docs/STABILITY.md, PLATFORM_SUPPORT.md, and STDLIB_INVENTORY.md'],
];
$boundaries = [
    'No broad replacement claim for every Python, C, Go, Rust, or Node.js use case.',
    'GPU, NPU, LLM, kernel, OS, embedded, and GUI surfaces stay experimental until backend proof exists.',
    'Public-network HTTP, TLS, DNS-backed sockets, and database production readiness remain beta or preview unless gated.',
    'Performance claims require generated CSV and metadata from the benchmark harness.',
    'ABI claims stay inside documented and tested starter layouts.',
];
?>
  <section class="tn-section">
    <div class="tn-container tn-doc-layout">
      <?php tn_doc_nav(); ?>
      <div class="tn-grid">
        <article class="tn-panel">
          <h2>Current Position</h2>
          <p>TezzNative is strongest today for CLI tools, automation scripts, native utilities, small services, C interop experiments, and Windows/Linux x64 backend hardening.</p>
          <p>The public message is: use TezzNative where Python feels slow and C feels painful.</p>
        </article>
        <article class="tn-panel">
          <h2>Verified Claims</h2>
          <div class="tn-table-wrap">
            <table class="tn-table">
              <thead><tr><th>Claim</th><th>Evidence</th></tr></thead>
              <tbody>
              <?php foreach ($verified as [$claim, $evidence]): ?>
                <tr><td><?= htmlspecialchars($claim) ?></td><td><?= htmlspecialchars($evidence) ?></td></tr>
              <?php endforeach; ?>
              </tbody>
            </table>
          </div>
        </article>
        <article class="tn-panel">
          <h2>Boundaries</h2>
          <ul class="tn-list">
            <?php foreach ($boundaries as $item): ?>
            <li><?= htmlspecialchars($item) ?></li>
            <?php endforeach; ?>
          </ul>
        </article>
      </div>
    </div>
  </section>
<?php tn_page_shell_end(); tn_footer(); ?>
