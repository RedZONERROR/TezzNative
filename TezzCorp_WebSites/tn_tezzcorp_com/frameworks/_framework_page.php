<?php
declare(strict_types=1);

require_once __DIR__ . '/../includes/nav.php';

$pages = [
    'tezzapi' => ['TezzApi', 'Beta', 'API route tables, CRUD helpers, TNXB payloads, and service contracts.', "import \"tezzapi\"\n\nfn main() -> int:\n  api:*TezzApi = tezzapi.api_new()\n  tezzapi.api_route(api, \"GET\", \"/health\", health_handler)\n  ret 0"],
    'tezzserve' => ['TezzServe', 'Beta', 'HTTP server middleware, tenant policy checks, and production service loops.', "import \"tezzserve\"\n\nfn handler(ctx:*ServeCtx):\n  tezzserve.response(ctx, 200, \"ok\")\n\nfn main():\n  tezzserve.listen(8080, handler)"],
    'tezzai' => ['TezzAI', 'Experimental', 'Local corpus learning, model persistence, and inference endpoint experiments.', "import \"llm\"\n\nfn main() -> int:\n  say \"TezzAI experimental lane\"\n  ret 0"],
    'tsm' => ['TezzServiceManager', 'Beta', 'Module registry, service lifecycle, and project dependency direction.', "tezz init cli demo\ntezz get std@0.1\ntezz doctor"],
    'tezzui' => ['TezzUI', 'Experimental', 'Window tree, focus graph, input routing, and compositor-facing UI primitives.', "import \"tnui\"\n\nfn main() -> int:\n  say \"GUI backend requires host support\"\n  ret 0"],
    'tezzdb' => ['TezzDB Framework', 'Beta', 'Embedded database helpers for local application state and query workflows.', "import \"tezzdb\"\n\nfn main():\n  db:*TezzDb = tezzdb.open(\"app.tdb\")\n  tezzdb.close(db)"],
    'tezzos-sdk' => ['TezzOS SDK', 'Experimental', 'OS-level services, shell integration, GUI hooks, and install/runtime guidance.', "fn main() -> int:\n  say \"TezzOS SDK experimental\"\n  ret 0"],
];

$slug = isset($slug) ? (string)$slug : basename((string)($_SERVER['SCRIPT_NAME'] ?? ''));
$slug = preg_replace('/\.php$/', '', $slug) ?? $slug;
$page = $pages[$slug] ?? $pages['tezzapi'];
[$name, $status, $description, $example] = $page;

$boot = tn_boot('/frameworks/' . $slug);
$GLOBALS['tn_config'] = $boot['config'];

tn_head($name . ' Framework', $description, 'frameworks', '/frameworks/' . $slug);
tn_nav('frameworks');
tn_page_shell_start('Framework', $name, $description);
?>
  <section class="tn-section">
    <div class="tn-container tn-grid-2">
      <article class="tn-panel">
        <?= tn_badge($status, $status === 'Beta' ? 'blue' : 'amber') ?>
        <h2>Production boundary</h2>
        <p>This framework is documented with a maturity label. Stable applications should pin SDK versions and add smoke tests before relying on beta or experimental behavior.</p>
        <ul class="tn-list">
          <li>Use explicit imports.</li>
          <li>Track platform behavior in release notes.</li>
          <li>Report failures through `/support/` or CLI report forms.</li>
        </ul>
      </article>
      <?php tn_code($name . ' example', $example); ?>
    </div>
  </section>
<?php tn_page_shell_end(); tn_footer(); ?>
