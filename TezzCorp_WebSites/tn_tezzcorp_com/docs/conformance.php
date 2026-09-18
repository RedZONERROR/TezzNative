<?php
declare(strict_types=1);
require_once __DIR__ . '/_common.php';
tn_doc_boot('/docs/conformance', 'conformance');
tn_head('Conformance', 'TezzNative stable-core conformance suites for parser, type checking, diagnostics, stdlib imports, native smoke, and ABI checks.', 'docs', '/docs/conformance');
tn_nav('docs');
tn_page_shell_start('Conformance', 'A small compatibility contract that fails loudly.', 'The stable-core, DX, package trust, ABI, native smoke, and reliability runners gate the current public surface on Windows and Linux.');
$suites = [
    ['Stable core', 'valid/', 'Arithmetic, control flow, structs, arrays, indexing, sizeof/alignof, and unsafe pointer basics.'],
    ['Parser', 'parser/valid + parser/invalid', 'Comments, literals, nested blocks, struct syntax, bad indentation, missing block markers, and unterminated strings.'],
    ['Typecheck', 'typecheck/valid + typecheck/invalid', 'Function calls/returns, pointer-array roundtrip, struct flow, index type errors, and assignment mismatch.'],
    ['Diagnostics', 'diagnostics/', 'Expected snippets for invalid programs, including parser and typecheck subdirectories.'],
    ['Stdlib import', 'stdlib/', 'Stable-candidate module import smoke.'],
    ['Developer experience', 'run-dx.* + dx/', 'Actionable help diagnostics, fmt idempotence, lint rule IDs, examples, LSP source, and snippet drift checks.'],
    ['Package trust', 'run-package-trust.*', 'Public tezz launchers/tool source, SemVer metadata, lock/registry parity, package checksums, generated package docs, and first-party target docs.'],
    ['Python bridge', 'run-python-bridge.* + python_bridge/', 'tezzc pyext generation, CPython wrapper contents, primitive/buffer mapping, ownership docs, and wrapped/skipped manifests.'],
    ['Native smoke', 'native/', 'Windows/Linux x64 executable smoke for IO, paths, strings, math, vectors, arenas, time, process, net, and HTTP helpers.'],
    ['Stdlib edge', 'native/stdlib_*_edges.tn', 'Math divide/null/smoothstep/atan2 behavior plus string/vector/arena ownership and guard behavior.'],
];
?>
  <section class="tn-section">
    <div class="tn-container tn-doc-layout">
      <?php tn_doc_nav(); ?>
      <div class="tn-grid">
        <article class="tn-panel">
          <h2>Current Gate</h2>
          <p>26 stable-core checks run on Windows and Linux: 13 valid programs must pass, and 13 invalid programs must fail with deterministic snippets. The developer-experience gate adds 19 first-user workflow checks, package trust adds 12 reproducibility checks, and the Python bridge gate adds 8 CPython scaffold checks.</p>
          <?php tn_code('Summary', 'CONFORMANCE_SUMMARY passed=26 failed=0'); ?>
          <?php tn_code('DX', 'DX_SUMMARY passed=19 failed=0'); ?>
          <?php tn_code('Package trust', 'PACKAGE_TRUST_SUMMARY passed=12 failed=0'); ?>
          <?php tn_code('Python bridge', 'PYBRIDGE_SUMMARY passed=8 failed=0'); ?>
        </article>
        <article class="tn-panel">
          <h2>Commands</h2>
          <?php tn_code('Windows', 'powershell -ExecutionPolicy Bypass -File .\tests\conformance\run.ps1'); ?>
          <?php tn_code('Linux / WSL', 'bash tests/conformance/run.sh ./TezzNative-language/bin/tezzc-linux-x64'); ?>
          <?php tn_code('DX Windows', 'powershell -ExecutionPolicy Bypass -File .\tests\conformance\run-dx.ps1'); ?>
          <?php tn_code('DX Linux / WSL', 'bash tests/conformance/run-dx.sh ./TezzNative-language/bin/tezzc-linux-x64'); ?>
          <?php tn_code('Package Trust Windows', 'powershell -ExecutionPolicy Bypass -File .\tests\conformance\run-package-trust.ps1 -Tezzc .\TezzNative-language\build\tezzc.exe'); ?>
          <?php tn_code('Package Trust Linux / WSL', 'bash tests/conformance/run-package-trust.sh ./TezzNative-language/bin/tezzc-linux-x64'); ?>
          <?php tn_code('Python Bridge Windows', 'powershell -ExecutionPolicy Bypass -File .\tests\conformance\run-python-bridge.ps1 -Tezzc .\TezzNative-language\build\tezzc.exe'); ?>
          <?php tn_code('Python Bridge Linux / WSL', 'bash tests/conformance/run-python-bridge.sh ./TezzNative-language/bin/tezzc-linux-x64'); ?>
        </article>
        <article class="tn-panel">
          <h2>Suites</h2>
          <div class="tn-table-wrap">
            <table class="tn-table">
              <thead><tr><th>Suite</th><th>Path</th><th>Coverage</th></tr></thead>
              <tbody>
              <?php foreach ($suites as [$name, $path, $coverage]): ?>
                <tr><td><?= htmlspecialchars($name) ?></td><td><code><?= htmlspecialchars($path) ?></code></td><td><?= htmlspecialchars($coverage) ?></td></tr>
              <?php endforeach; ?>
              </tbody>
            </table>
          </div>
        </article>
      </div>
    </div>
  </section>
<?php tn_page_shell_end(); tn_footer(); ?>

