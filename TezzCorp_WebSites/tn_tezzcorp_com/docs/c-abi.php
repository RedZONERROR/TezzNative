<?php
declare(strict_types=1);
require_once __DIR__ . '/_common.php';
tn_doc_boot('/docs/c-abi', 'c-abi');
tn_head('C ABI', 'TezzNative C ABI layout rules, generated headers, structured ABI manifests, and verification commands.', 'docs', '/docs/c-abi');
tn_nav('docs');
tn_page_shell_start('C ABI', 'Predictable layout for C interop.', 'Generate headers, dump structured ABI manifests, and verify field offsets before shipping native boundaries.');
?>
  <section class="tn-section">
    <div class="tn-container tn-doc-layout">
      <?php tn_doc_nav(); ?>
      <div class="tn-grid">
        <article class="tn-panel">
          <h2>Current contract</h2>
          <ul class="tn-list">
            <li>`int` maps to `int64_t`, `float` maps to `double`, and `char` maps to `uint8_t`.</li>
            <li>Pointers are pointer-sized and 8-byte aligned on the current x64 release targets.</li>
            <li>Struct fields are laid out in declaration order with C-style alignment and tail padding.</li>
            <li>Fixed arrays are inline struct storage and keep the element alignment.</li>
          </ul>
        </article>
        <article class="tn-panel">
          <h2>Commands</h2>
          <?php tn_code('ABI workflow', "tezzc cheader tests/conformance/abi/starter_abi.tn build/starter_abi.h\ntezzc abidump tests/conformance/abi/starter_abi.tn build/starter_abi.tnx\ntezzc abiverify tests/conformance/abi/starter_abi.tn build/starter_abi.tnx"); ?>
        </article>
        <article class="tn-panel">
          <h2>Manifest gate</h2>
          <p>`abidump` emits schema `tezznative.abi.v1` with struct size, alignment, field offset, field type shape, function return type, and parameter type shape data.</p>
          <p>Windows and Linux CI run `cheader`, `abidump`, and full `abiverify` against the published SDK compiler.</p>
        </article>
      </div>
    </div>
  </section>
<?php tn_page_shell_end(); tn_footer(); ?>
