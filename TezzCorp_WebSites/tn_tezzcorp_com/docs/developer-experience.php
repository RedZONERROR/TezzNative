<?php
declare(strict_types=1);
require_once __DIR__ . '/_common.php';
tn_doc_boot('/docs/developer-experience', 'developer-experience');
tn_head('Developer Experience', 'TezzNative developer experience gates for diagnostics, fmt, lint, examples, LSP, snippets, run, and native build workflows.', 'docs', '/docs/developer-experience');
tn_page_shell_start('Developer experience', 'The first-user workflow is now gated.', 'Milestone 4 checks diagnostics, formatting, linting, examples, editor snippets, LSP source health, run, and native build behavior.');
$checks = [
    ['Diagnostics', 'Unknown-name and wrong-arity errors include file/line/column, snippet, caret, expected/actual detail where available, and a short help line.'],
    ['Formatter', '`tezzc fmt` normalizes control flow and stays idempotent on the checked fixture.'],
    ['Lint', '`tezzc lint` emits rule IDs for unused and shadowed variables, and the clean suppress fixture stays quiet.'],
    ['Examples', '`examples/dx` covers hello, CLI-style flags, file IO, HTTP parsing, route matching, C extern calls, native builds, and TezzDB beta starter code.'],
    ['Run and build', '`hello.tn` runs through `tezzc run`; `native_build.tn` builds with `buildexe --verify` and executes.'],
    ['Editor surface', 'The TezzNative LSP source type-checks and VS Code snippets stay inside supported syntax and public examples.'],
];
?>
  <section class="tn-section">
    <div class="tn-container tn-doc-layout">
      <?php tn_doc_nav(); ?>
      <div class="tn-grid">
        <article class="tn-panel">
          <h2>Gate</h2>
          <?php tn_code('Windows', 'powershell -ExecutionPolicy Bypass -File .\tests\conformance\run-dx.ps1'); ?>
          <?php tn_code('Linux / WSL', 'bash tests/conformance/run-dx.sh ./TezzNative-language/bin/tezzc-linux-x64'); ?>
          <?php tn_code('Expected', 'DX_SUMMARY passed=19 failed=0'); ?>
        </article>
        <article class="tn-panel">
          <h2>Checks</h2>
          <div class="tn-table-wrap">
            <table class="tn-table">
              <thead><tr><th>Area</th><th>Contract</th></tr></thead>
              <tbody>
              <?php foreach ($checks as [$area, $contract]): ?>
                <tr><td><?= htmlspecialchars($area) ?></td><td><?= htmlspecialchars($contract) ?></td></tr>
              <?php endforeach; ?>
              </tbody>
            </table>
          </div>
        </article>
        <article class="tn-panel">
          <h2>Example Set</h2>
          <ul class="tn-list">
            <li><code>examples/dx/hello.tn</code> runs through the bytecode path.</li>
            <li><code>examples/dx/native_build.tn</code> builds and executes as a native binary.</li>
            <li><code>examples/dx/file_read_write.tn</code>, <code>http_request_parse.tn</code>, and <code>http_server_route_once.tn</code> show stable-candidate stdlib flows.</li>
            <li><code>examples/dx/c_extern_call.tn</code> and <code>tezzdb_small.tn</code> show beta interop/database starter shapes.</li>
          </ul>
        </article>
      </div>
    </div>
  </section>
<?php tn_page_shell_end(); tn_footer(); ?>
