<?php
declare(strict_types=1);
require_once __DIR__ . '/_common.php';
tn_doc_boot('/docs/tezzdb', 'tezzdb');
tn_head('TezzDB Guide', 'TezzDB guide for embedded database usage inside TezzNative applications.', 'docs', '/docs/tezzdb');
tn_page_shell_start('TezzDB guide', 'Embedded data for TezzNative applications.', 'TezzDB is a beta surface. Use it deliberately, test migrations, and pin SDK versions for production deployments.');
?>
  <section class="tn-section">
    <div class="tn-container tn-doc-layout">
      <?php tn_doc_nav(); ?>
      <div class="tn-grid">
        <?php tn_code('Basic flow', "import \"tezzdb\"\n\nfn main():\n  db:*TezzDb = tezzdb.open(\"app.tdb\")\n  tezzdb.set(db, \"name\", \"TezzNative\")\n  say tezzdb.get(db, \"name\")\n  tezzdb.close(db)"); ?>
        <article class="tn-panel"><h2>Production notes</h2><ul class="tn-list"><li>Keep backups before schema migrations.</li><li>Use release-pinned SDKs.</li><li>Add database smoke tests for application workflows.</li></ul></article>
      </div>
    </div>
  </section>
<?php tn_page_shell_end(); tn_footer(); ?>
