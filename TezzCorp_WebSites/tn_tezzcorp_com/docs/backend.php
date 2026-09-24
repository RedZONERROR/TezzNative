<?php
declare(strict_types=1);
require_once __DIR__ . '/_common.php';
tn_doc_boot('/docs/backend', 'backend');
tn_head('Backend Architecture', 'TezzNative backend architecture overview for checks, IR, native codegen, and runtime support.', 'docs', '/docs/backend');
tn_page_shell_start('Backend architecture', 'Compiler reliability before target expansion.', 'Native backend work should be gated by deterministic IR checks, platform smoke tests, and clear unsupported-target errors.');
?>
  <section class="tn-section">
    <div class="tn-container tn-doc-layout">
      <?php tn_doc_nav(); ?>
      <div class="tn-card-grid">
        <article class="tn-card"><h3>Front end</h3><p>Lexer, parser, semantic checks, and deterministic diagnostics.</p></article>
        <article class="tn-card"><h3>IR</h3><p>Control flow, loads, stores, conversions, calls, and verifier coverage.</p></article>
        <article class="tn-card"><h3>Native targets</h3><p>x86_64 Windows and Linux are the primary hardening lanes.</p></article>
        <article class="tn-card"><h3>Runtime</h3><p>IO, networking, GUI, TLS, database, and experimental accelerators are separated by stability labels.</p></article>
      </div>
    </div>
  </section>
<?php tn_page_shell_end(); tn_footer(); ?>
