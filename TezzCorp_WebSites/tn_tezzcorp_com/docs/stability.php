<?php
declare(strict_types=1);
require_once __DIR__ . '/_common.php';
tn_doc_boot('/docs/stability', 'stability');
tn_head('Stability Map', 'TezzNative stability labels for stable, beta, experimental, and internal surfaces.', 'stability', '/docs/stability');
tn_nav('stability');
tn_page_shell_start('Stability map', 'Trust starts with honest labels.', 'Stable users should know what is ready, what is hardening, and what remains experimental.');
?>
  <section class="tn-section">
    <div class="tn-container tn-doc-layout">
      <?php tn_doc_nav(); ?>
      <div class="tn-card-grid">
        <article class="tn-card"><?= tn_badge('Stable', 'green') ?><h3>Core language</h3><p>Functions, variables, structs, imports, basic control flow, and common checks.</p></article>
        <article class="tn-card"><?= tn_badge('Beta', 'blue') ?><h3>Native builds and ABI</h3><p>Useful today, with layout, target, and smoke tests still expanding.</p></article>
        <article class="tn-card"><?= tn_badge('Experimental', 'amber') ?><h3>AI, GPU, NPU, OS</h3><p>Ambitious modules that need backend proof before production claims.</p></article>
        <article class="tn-card"><?= tn_badge('Internal', 'blue') ?><h3>Compiler internals</h3><p>No compatibility promise for implementation details and private runtime surfaces.</p></article>
      </div>
    </div>
  </section>
<?php tn_page_shell_end(); tn_footer(); ?>
