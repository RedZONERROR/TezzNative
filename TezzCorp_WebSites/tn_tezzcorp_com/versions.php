<?php
declare(strict_types=1);

require_once __DIR__ . '/includes/nav.php';

$boot = tn_boot('/versions/');
$GLOBALS['tn_config'] = $boot['config'];
$pdo = $boot['pdo'];
$version = $boot['version'];
$notice = '';
$noticeOk = false;

if (strtoupper((string)($_SERVER['REQUEST_METHOD'] ?? 'GET')) === 'POST') {
    try {
        if ((string)($_POST['form_type'] ?? '') === 'release') {
            $result = tezz_upsert_release_version($pdo, (string)($_POST['version'] ?? ''), (string)($_POST['channel'] ?? ''), (string)($_POST['notes'] ?? ''));
        } else {
            $result = tezz_submit_cli_report($pdo, $_POST);
        }
        $notice = (string)$result['message'];
        $noticeOk = (bool)$result['ok'];
    } catch (Throwable $_e) {
        $notice = 'Version database is temporarily unavailable.';
    }
}

$versions = [];
$reports = [];
try {
    $versions = tezz_fetch_versions($pdo, 20);
    $reports = tezz_fetch_cli_reports($pdo, 12);
} catch (Throwable $_e) {
}

tn_head('TezzNative Releases', 'TezzNative release registry, CLI report intake, and version history.', 'versions', '/versions/');
tn_nav('versions');
tn_page_shell_start('Releases', 'Version registry and CLI validation reports.', 'The release page is database-backed so SDK versions, CLI checks, and platform health reports stay visible.');
?>
  <div style="margin-top: 30px;">
    <?php if ($notice !== ''): ?><p class="tn-notice <?= $noticeOk ? 'tn-notice-ok' : 'tn-notice-error' ?>" style="padding: 12px; border-radius: var(--radius-sm); margin-bottom: 20px; background: rgba(255, 153, 51, 0.15); color: #ff9933; border: 1px solid rgba(255, 153, 51, 0.3);"><?= htmlspecialchars($notice) ?></p><?php endif; ?>
    <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); gap: 24px;">
      <article class="feature-card glass-panel" style="padding: 32px;">
        <h2 style="font-size: 1.4rem; font-weight: 700; color: var(--text-primary); margin-bottom: 12px;">Active Release Channel</h2>
        <div style="padding: 16px; background: var(--bg-surface-raised); border-radius: var(--radius-sm); border: 1px solid var(--border-subtle); margin-bottom: 20px;">
          <div style="font-size: 0.8rem; color: #ff9933; font-weight: 700; text-transform: uppercase;">Production Stable</div>
          <div style="font-size: 1.3rem; font-weight: 800; color: var(--text-primary); margin-top: 4px;">TezzNative v2.2.1</div>
          <div style="font-size: 0.85rem; color: var(--text-secondary); margin-top: 4px;">Created by Rohit Pathak &bull; TezzCorp Pvt Ltd.</div>
        </div>
        <h3 style="font-size: 1.1rem; font-weight: 700; color: var(--text-primary); margin-bottom: 8px;">Automated Update Check</h3>
        <div style="background: var(--code-bg); padding: 14px; border-radius: var(--radius-sm); font-family: var(--font-mono); font-size: 0.85rem; color: #ff9933; overflow-x: auto; border: 1px solid var(--border-subtle);">
          curl "https://tn.tezzcorp.com/api/update_check.php?platform=windows-x64&version=2.2.1&mode=check"
        </div>
      </article>

      <article class="feature-card glass-panel" style="padding: 32px;">
        <h2 style="font-size: 1.4rem; font-weight: 700; color: var(--text-primary); margin-bottom: 12px;">Submit CLI Platform Report</h2>
        <form class="tn-form" method="post" style="display: flex; flex-direction: column; gap: 14px;">
          <input type="hidden" name="form_type" value="cli">
          <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px;">
            <label style="display: flex; flex-direction: column; gap: 6px; font-size: 0.85rem; color: var(--text-secondary);">Name <input name="user_name" required style="padding: 10px 14px; border-radius: var(--radius-sm); background: var(--bg-surface-raised); border: 1px solid var(--border-subtle); color: var(--text-primary);"></label>
            <label style="display: flex; flex-direction: column; gap: 6px; font-size: 0.85rem; color: var(--text-secondary);">Email <input type="email" name="email" required style="padding: 10px 14px; border-radius: var(--radius-sm); background: var(--bg-surface-raised); border: 1px solid var(--border-subtle); color: var(--text-primary);"></label>
          </div>
          <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px;">
            <label style="display: flex; flex-direction: column; gap: 6px; font-size: 0.85rem; color: var(--text-secondary);">CLI Version <input name="cli_version" value="2.2.1" required style="padding: 10px 14px; border-radius: var(--radius-sm); background: var(--bg-surface-raised); border: 1px solid var(--border-subtle); color: var(--text-primary);"></label>
            <label style="display: flex; flex-direction: column; gap: 6px; font-size: 0.85rem; color: var(--text-secondary);">Platform <input name="platform" value="windows-x64" required style="padding: 10px 14px; border-radius: var(--radius-sm); background: var(--bg-surface-raised); border: 1px solid var(--border-subtle); color: var(--text-primary);"></label>
          </div>
          <div style="display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 12px;">
            <label style="display: flex; flex-direction: column; gap: 6px; font-size: 0.85rem; color: var(--text-secondary);">Doctor <select name="doctor_status" style="padding: 10px; border-radius: var(--radius-sm); background: var(--bg-surface-raised); border: 1px solid var(--border-subtle); color: var(--text-primary);"><option>pass</option><option>warn</option><option>fail</option></select></label>
            <label style="display: flex; flex-direction: column; gap: 6px; font-size: 0.85rem; color: var(--text-secondary);">Mode <select name="test_mode" style="padding: 10px; border-radius: var(--radius-sm); background: var(--bg-surface-raised); border: 1px solid var(--border-subtle); color: var(--text-primary);"><option>buildexe</option><option>run</option><option>check</option><option>doctor</option></select></label>
            <label style="display: flex; flex-direction: column; gap: 6px; font-size: 0.85rem; color: var(--text-secondary);">Status <select name="test_status" style="padding: 10px; border-radius: var(--radius-sm); background: var(--bg-surface-raised); border: 1px solid var(--border-subtle); color: var(--text-primary);"><option>pass</option><option>fail</option></select></label>
          </div>
          <button class="btn btn-primary" type="submit">Submit Verification Report</button>
        </form>
      </article>
    </div>
  </div>
<?php tn_page_shell_end(); tn_footer(); ?>
