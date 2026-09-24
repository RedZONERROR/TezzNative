<?php
declare(strict_types=1);
require_once __DIR__ . '/_common.php';
tn_doc_boot('/docs/python-bridge', 'python-bridge');
tn_head('Python Bridge', 'TezzNative Python bridge documentation for tezzc pyext, CPython wrapper generation, primitive mapping, buffer ownership, and conformance gates.', 'docs', '/docs/python-bridge');
tn_page_shell_start('Python Bridge', 'Use Python for orchestration and TezzNative for native hot paths.', 'Milestone 6 adds a gated CPython extension scaffold generator for ABI-safe TezzNative functions.');
$rows = [
    ['int / i64', 'Python int', 'Python int'],
    ['u8', 'Python int 0..255', 'Python int'],
    ['float / f64', 'Python float', 'Python float'],
    ['*char / *u8 parameter', 'Contiguous buffer object', 'Not returned'],
    ['void return', 'n/a', 'None'],
];
?>
  <section class="tn-section">
    <div class="tn-container tn-doc-layout">
      <?php tn_doc_nav(); ?>
      <div class="tn-grid">
        <article class="tn-panel">
          <h2>Command</h2>
          <?php tn_code('Generate', 'tezzc pyext module.tn [out_dir] [--module name]'); ?>
          <p>The generator emits CPython wrapper C, an ABI header, setup metadata, ownership docs, and a deterministic wrapped/skipped manifest.</p>
        </article>
        <article class="tn-panel">
          <h2>Generated Files</h2>
          <ul class="tn-clean-list">
            <li><code>&lt;module&gt;_pyext.c</code> CPython wrapper source.</li>
            <li><code>&lt;module&gt;.h</code> ABI declarations for wrapped functions.</li>
            <li><code>setup.py</code> setuptools scaffold using native objects or libraries.</li>
            <li><code>README.md</code> ownership and build notes.</li>
            <li><code>pyext_manifest.tnx</code> wrapped/skipped function summary.</li>
          </ul>
        </article>
        <article class="tn-panel">
          <h2>Type Mapping</h2>
          <div class="tn-table-wrap">
            <table class="tn-table">
              <thead><tr><th>TezzNative</th><th>Python Input</th><th>Python Return</th></tr></thead>
              <tbody>
              <?php foreach ($rows as [$tn, $input, $ret]): ?>
                <tr><td><code><?= htmlspecialchars($tn) ?></code></td><td><?= htmlspecialchars($input) ?></td><td><?= htmlspecialchars($ret) ?></td></tr>
              <?php endforeach; ?>
              </tbody>
            </table>
          </div>
        </article>
        <article class="tn-panel">
          <h2>Ownership</h2>
          <ul class="tn-clean-list">
            <li>Primitive values are copied between Python and TezzNative.</li>
            <li>Buffer parameters are borrowed with <code>PyObject_GetBuffer</code> only for the call duration.</li>
            <li>Borrowed buffers are released on success and error paths.</li>
            <li>Pointer returns, structs, and unsafe functions are skipped in the starter bridge.</li>
          </ul>
        </article>
        <article class="tn-panel">
          <h2>Gate</h2>
          <?php tn_code('Windows', 'powershell -ExecutionPolicy Bypass -File .\tests\conformance\run-python-bridge.ps1 -Tezzc .\TezzNative-language\build\tezzc.exe'); ?>
          <?php tn_code('Linux / WSL', 'bash tests/conformance/run-python-bridge.sh ./TezzNative-language/bin/tezzc-linux-x64'); ?>
          <?php tn_code('Expected', 'PYBRIDGE_SUMMARY passed=8 failed=0'); ?>
        </article>
      </div>
    </div>
  </section>
<?php tn_page_shell_end(); tn_footer(); ?>
