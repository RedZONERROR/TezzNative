<?php
declare(strict_types=1);

require_once __DIR__ . '/includes/nav.php';

$boot = tn_boot('/frameworks/');
$GLOBALS['tn_config'] = $boot['config'];
$frameworks = [
    ['TezzApi', '/frameworks/tezzapi', 'API services, route tables, CRUD helpers, and TNXB payloads.', 'Beta'],
    ['TezzServe', '/frameworks/tezzserve', 'HTTP server middleware, tenant policy checks, and service lifecycle hooks.', 'Beta'],
    ['TezzAI', '/frameworks/tezzai', 'Local corpus learning, model persistence, and inference endpoints.', 'Experimental'],
    ['TSM', '/frameworks/tsm', 'Module and service lifecycle manager for projects and deployments.', 'Beta'],
    ['TezzUI', '/frameworks/tezzui', 'Window tree, focus, input, and compositor-facing UI primitives.', 'Experimental'],
    ['TezzDB', '/frameworks/tezzdb', 'Embedded database and query surfaces for local applications.', 'Beta'],
    ['TezzOS SDK', '/frameworks/tezzos-sdk', 'OS-facing service, shell, and GUI framework guidance.', 'Experimental'],
];

tn_head('TezzNative Frameworks', 'TezzNative framework hub for APIs, services, GUI, database, AI, and OS experiments.', 'frameworks', '/frameworks/');
tn_nav('frameworks');
tn_page_shell_start('Frameworks', 'A language ecosystem with explicit maturity labels.', 'Frameworks are useful, but production trust depends on separating beta surfaces from experimental ambitions.');
?>
  <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 24px; margin-top: 30px;">
    <?php foreach ($frameworks as [$name, $href, $desc, $status]): ?>
    <article class="feature-card glass-panel" style="padding: 28px; display: flex; flex-direction: column; justify-content: space-between;">
      <div>
        <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 14px;">
          <span style="font-size: 0.75rem; font-weight: 700; padding: 3px 10px; border-radius: var(--radius-full); background: <?= $status === 'Beta' ? 'rgba(56, 189, 248, 0.15)' : 'rgba(255, 153, 51, 0.15)' ?>; color: <?= $status === 'Beta' ? '#38bdf8' : '#ff9933' ?>; border: 1px solid <?= $status === 'Beta' ? 'rgba(56, 189, 248, 0.3)' : 'rgba(255, 153, 51, 0.3)' ?>;">
            <?= htmlspecialchars(strtoupper($status)) ?>
          </span>
        </div>
        <h3 style="font-size: 1.35rem; font-weight: 700; color: var(--text-primary); margin-bottom: 10px;"><?= htmlspecialchars($name) ?></h3>
        <p style="color: var(--text-secondary); line-height: 1.6; font-size: 0.92rem; margin-bottom: 20px;"><?= htmlspecialchars($desc) ?></p>
      </div>
      <a class="btn btn-outline btn-sm" href="<?= htmlspecialchars($href) ?>" style="align-self: flex-start;">
        <span>Explore <?= htmlspecialchars($name) ?></span>
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M5 12h14M12 5l7 7-7 7"/></svg>
      </a>
    </article>
    <?php endforeach; ?>
  </div>
<?php tn_page_shell_end(); tn_footer(); ?>
