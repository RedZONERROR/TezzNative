<?php
declare(strict_types=1);
require_once __DIR__ . '/_common.php';
require_once __DIR__ . '/../lib/docs_indexer.php';
tn_doc_boot('/docs/sdk', 'sdk');
tn_head('SDK Reference', 'TezzNative SDK module reference generated from local library files when available.', 'docs', '/docs/sdk');
$modules = [];
try {
    $modules = tezz_collect_sdk_modules(tezz_sdk_lib_dir());
} catch (Throwable $_e) {
}
tn_page_shell_start('SDK reference', 'Generated module inventory for the SDK.', 'Search modules and functions. Stable production docs should grow from this generated base.');
?>
  <section class="tn-section">
    <div class="tn-container tn-doc-layout">
      <?php tn_doc_nav(); ?>
      <div>
        <input class="tn-search" data-filter-input="[data-sdk-card]" placeholder="Search SDK modules">
        <div class="tn-grid" style="margin-top:1rem">
          <?php if (!$modules): ?>
          <article class="tn-panel"><h2>No modules indexed</h2><p>The SDK library path was not available in this deployment.</p></article>
          <?php else: ?>
          <?php foreach ($modules as $module): ?>
          <article class="tn-panel" data-sdk-card>
            <h2><?= htmlspecialchars((string)$module['module']) ?></h2>
            <p><?= htmlspecialchars((string)$module['description']) ?></p>
            <p><?= (int)$module['function_count'] ?> functions detected.</p>
          </article>
          <?php endforeach; ?>
          <?php endif; ?>
        </div>
      </div>
    </div>
  </section>
<?php tn_page_shell_end(); tn_footer(); ?>
