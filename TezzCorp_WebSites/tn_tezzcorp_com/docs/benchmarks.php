<?php
declare(strict_types=1);
require_once __DIR__ . '/_common.php';
tn_doc_boot('/docs/benchmarks', 'benchmarks');
tn_head('Benchmarks', 'TezzNative benchmark harness, workload matrix, comparison fixtures, and honest publishing rules.', 'docs', '/docs/benchmarks');
tn_page_shell_start('Benchmarks', 'Repeatable performance proof, not marketing numbers.', 'The public harness separates check, bytecode, native build, native run, and optional external language comparisons.');
$workloads = [
    ['startup', 'Startup time', 'Minimal process launch and program startup.'],
    ['sum_loop', 'Numeric loop', 'Integer loop lowering and arithmetic throughput.'],
    ['string_scan', 'String processing', 'Byte scanning, nested loops, and accumulation.'],
    ['file_io', 'File read/write', 'Binary write, size check, readback, accumulation, and cleanup.'],
];
$languages = ['TezzNative', 'Python', 'C', 'Node.js', 'Go', 'Rust'];
$notClaimed = [
    ['JSON parsing', 'Waiting for a stable JSON fixture.'],
    ['HTTP throughput', 'Waiting for a local load driver and stable server fixture.'],
    ['Matrix math', 'Waiting for a real supported numeric backend gate.'],
    ['GPU/NPU/LLM', 'Experimental only; fallback results are not acceleration results.'],
];
?>
  <section class="tn-section">
    <div class="tn-container tn-doc-layout">
      <?php tn_doc_nav(); ?>
      <div class="tn-grid">
        <div class="tn-card-grid">
          <?php foreach ($workloads as [$name, $title, $body]): ?>
          <article class="tn-card"><?= tn_badge($name, 'green') ?><h3><?= htmlspecialchars($title) ?></h3><p><?= htmlspecialchars($body) ?></p></article>
          <?php endforeach; ?>
        </div>

        <section class="tn-panel">
          <h2>Comparison Fixtures</h2>
          <p>Comparison sources are visible in the repository. Missing optional toolchains are reported as skipped, not as benchmark results.</p>
          <div class="tn-table-wrap">
            <table class="tn-table">
              <thead><tr><th>Language</th><th>Status</th></tr></thead>
              <tbody>
              <?php foreach ($languages as $language): ?>
                <tr><td><?= htmlspecialchars($language) ?></td><td>Source-visible fixture set</td></tr>
              <?php endforeach; ?>
              </tbody>
            </table>
          </div>
        </section>

        <section class="tn-panel">
          <h2>Run Locally</h2>
          <?php tn_code('Windows', 'powershell -ExecutionPolicy Bypass -File .\benchmarks\run.ps1 -IncludeExternal'); ?>
          <?php tn_code('Linux / WSL', 'bash benchmarks/run.sh ./TezzNative-language/bin/tezzc-linux-x64 --iterations 3 --include-external'); ?>
        </section>

        <section class="tn-panel">
          <h2>Result Contract</h2>
          <p>CSV rows use <code>tezznative.benchmark-result.v1</code>. Metadata files use <code>tezznative.benchmark-run.v1</code>.</p>
          <div class="tn-card-grid">
            <article class="tn-card"><h3>Measured</h3><p>Elapsed time, peak memory where available, binary size, command, exit code, timeout status, and output hash.</p></article>
            <article class="tn-card"><h3>Separated</h3><p>Bytecode, native build, native run, and external language runs are separate rows.</p></article>
          </div>
        </section>

        <section class="tn-panel">
          <h2>Not Claimed Yet</h2>
          <div class="tn-table-wrap">
            <table class="tn-table">
              <thead><tr><th>Category</th><th>Gate</th></tr></thead>
              <tbody>
              <?php foreach ($notClaimed as [$category, $gate]): ?>
                <tr><td><?= htmlspecialchars($category) ?></td><td><?= htmlspecialchars($gate) ?></td></tr>
              <?php endforeach; ?>
              </tbody>
            </table>
          </div>
        </section>
      </div>
    </div>
  </section>
<?php tn_page_shell_end(); tn_footer(); ?>
