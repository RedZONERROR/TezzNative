<?php
declare(strict_types=1);
require_once __DIR__ . '/_common.php';
tn_doc_boot('/docs/stdlib-core', 'stdlib-core');
tn_head('Stable Standard Library Core', 'TezzNative stable-candidate standard library ownership, failure behavior, platform notes, and verification gates.', 'docs', '/docs/stdlib-core');
tn_nav('docs');
tn_page_shell_start('Stable stdlib core', 'Small, practical, and gated.', 'The current stable-candidate modules are documented by ownership rules, failure behavior, platform notes, and Windows/Linux x64 native smoke gates.');
$modules = [
    ['std', 'Common prelude helpers', 'Stable import smoke'],
    ['io', 'Files, directories, paths, streams, process helpers', 'Native IO/path/process smoke'],
    ['str', 'Search, slice, trim/case, replace, repeat, parse, padding', 'Native string smoke plus empty-replace edge'],
    ['math', 'Integer, float, aggregate, interpolation, trig helpers', 'Native math smoke plus divmod/smoothstep/atan2 edges'],
    ['time', 'Clock, sleep, UTC/local date formatting', 'Native clock/sleep/date smoke'],
    ['vec', 'Type-erased vectors and typed convenience wrappers', 'Native vector smoke plus reserve/fill guards'],
    ['arena', 'Bump allocation, mark/release, wrapped buffers', 'Native arena smoke plus alignment/release guards'],
];
$rules = [
    'Returned strings are usually heap allocated; callers own non-null results.',
    'File, vector, and arena handles are explicit resources and should be closed or freed.',
    'Mutating resource functions return 0 on success and -1 on invalid handles, invalid arguments, or allocation failure.',
    'Allocating functions return null on allocation failure.',
    'Arena wrapped buffers remain caller-owned; freeing the arena handle does not free the external buffer.',
    'Runtime-backed helpers must state whether they use direct native lowering, host runtime calls, process-backed fallback, or unsupported stubs.',
];
?>
  <section class="tn-section">
    <div class="tn-container tn-doc-layout">
      <?php tn_doc_nav(); ?>
      <div class="tn-grid">
        <article class="tn-panel">
          <h2>Stable Candidates</h2>
          <div class="tn-table-wrap">
            <table class="tn-table">
              <thead><tr><th>Module</th><th>Role</th><th>Gate</th></tr></thead>
              <tbody>
              <?php foreach ($modules as [$module, $role, $gate]): ?>
                <tr><td><code><?= htmlspecialchars($module) ?></code></td><td><?= htmlspecialchars($role) ?></td><td><?= htmlspecialchars($gate) ?></td></tr>
              <?php endforeach; ?>
              </tbody>
            </table>
          </div>
        </article>
        <article class="tn-panel">
          <h2>Failure And Ownership</h2>
          <ul class="tn-list">
            <?php foreach ($rules as $rule): ?>
              <li><?= htmlspecialchars($rule) ?></li>
            <?php endforeach; ?>
          </ul>
        </article>
        <article class="tn-panel">
          <h2>Verification</h2>
          <p>Windows:</p>
          <pre><code>powershell -ExecutionPolicy Bypass -File .\tests\conformance\run-native-smoke.ps1</code></pre>
          <p>Linux or WSL:</p>
          <pre><code>bash tests/conformance/run-native-smoke.sh ./TezzNative-language/bin/tezzc-linux-x64</code></pre>
          <p>The focused stdlib edge fixtures are <code>stdlib_math_edges.tn</code> and <code>stdlib_collections_edges.tn</code>.</p>
        </article>
      </div>
    </div>
  </section>
<?php tn_page_shell_end(); tn_footer(); ?>
