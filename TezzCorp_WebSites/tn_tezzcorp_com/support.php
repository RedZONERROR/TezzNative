<?php
declare(strict_types=1);

require_once __DIR__ . '/includes/nav.php';

$boot = tn_boot('/support/');
$GLOBALS['tn_config'] = $boot['config'];
$pdo = $boot['pdo'];
$notice = '';
$noticeOk = false;

if (strtoupper((string)($_SERVER['REQUEST_METHOD'] ?? 'GET')) === 'POST') {
    try {
        if ((string)($_POST['form_type'] ?? '') === 'error') {
            $result = tezz_submit_error_report($pdo, $_POST);
        } else {
            $result = tezz_submit_support_post($pdo, $_POST, $_FILES['attachment'] ?? null);
        }
        $notice = (string)$result['message'];
        $noticeOk = (bool)$result['ok'];
    } catch (Throwable $_e) {
        $notice = 'Support submission is temporarily unavailable.';
    }
}

tn_head('TezzNative Support', 'Submit TezzNative support requests and compiler error reports into the production support database.', 'support', '/support/');
tn_nav('support');
tn_page_shell_start('Support', 'Report problems with enough detail to fix them.', 'Support requests and compiler error reports are stored in the site database so real adoption pain can feed the optimization roadmap.');
?>
  <div style="margin-top: 30px;">
    <?php if ($notice !== ''): ?><p class="tn-notice <?= $noticeOk ? 'tn-notice-ok' : 'tn-notice-error' ?>" style="padding: 12px; border-radius: var(--radius-sm); margin-bottom: 20px; background: rgba(255, 153, 51, 0.15); color: #ff9933; border: 1px solid rgba(255, 153, 51, 0.3);"><?= htmlspecialchars($notice) ?></p><?php endif; ?>
    <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); gap: 24px;">
      <article class="feature-card glass-panel" style="padding: 32px;">
        <h2 style="font-size: 1.4rem; font-weight: 700; color: var(--text-primary); margin-bottom: 12px;">Support Request</h2>
        <form class="tn-form" method="post" enctype="multipart/form-data" style="display: flex; flex-direction: column; gap: 14px;">
          <input type="hidden" name="form_type" value="support">
          <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px;">
            <label style="display: flex; flex-direction: column; gap: 6px; font-size: 0.85rem; color: var(--text-secondary);">Type <select name="support_type" style="padding: 10px 14px; border-radius: var(--radius-sm); background: var(--bg-surface-raised); border: 1px solid var(--border-subtle); color: var(--text-primary);"><option>support</option><option>bug</option><option>feature</option><option>enterprise</option></select></label>
            <label style="display: flex; flex-direction: column; gap: 6px; font-size: 0.85rem; color: var(--text-secondary);">Name <input name="user_name" maxlength="100" required style="padding: 10px 14px; border-radius: var(--radius-sm); background: var(--bg-surface-raised); border: 1px solid var(--border-subtle); color: var(--text-primary);"></label>
          </div>
          <label style="display: flex; flex-direction: column; gap: 6px; font-size: 0.85rem; color: var(--text-secondary);">Email <input type="email" name="email" maxlength="190" required style="padding: 10px 14px; border-radius: var(--radius-sm); background: var(--bg-surface-raised); border: 1px solid var(--border-subtle); color: var(--text-primary);"></label>
          <label style="display: flex; flex-direction: column; gap: 6px; font-size: 0.85rem; color: var(--text-secondary);">Title <input name="title" maxlength="160" required style="padding: 10px 14px; border-radius: var(--radius-sm); background: var(--bg-surface-raised); border: 1px solid var(--border-subtle); color: var(--text-primary);"></label>
          <label style="display: flex; flex-direction: column; gap: 6px; font-size: 0.85rem; color: var(--text-secondary);">Message <textarea name="message" maxlength="5000" rows="4" required style="padding: 10px 14px; border-radius: var(--radius-sm); background: var(--bg-surface-raised); border: 1px solid var(--border-subtle); color: var(--text-primary);"></textarea></label>
          <label style="display: flex; flex-direction: column; gap: 6px; font-size: 0.85rem; color: var(--text-secondary);">Attachment <input type="file" name="attachment" style="padding: 8px; border-radius: var(--radius-sm); background: var(--bg-surface-raised); border: 1px solid var(--border-subtle); color: var(--text-primary);"></label>
          <button class="btn btn-primary" type="submit">Submit Support Request</button>
        </form>
      </article>

      <article class="feature-card glass-panel" style="padding: 32px;">
        <h2 style="font-size: 1.4rem; font-weight: 700; color: var(--text-primary); margin-bottom: 12px;">Compiler Error Report</h2>
        <form class="tn-form" method="post" style="display: flex; flex-direction: column; gap: 14px;">
          <input type="hidden" name="form_type" value="error">
          <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px;">
            <label style="display: flex; flex-direction: column; gap: 6px; font-size: 0.85rem; color: var(--text-secondary);">Name <input name="user_name" maxlength="100" required style="padding: 10px 14px; border-radius: var(--radius-sm); background: var(--bg-surface-raised); border: 1px solid var(--border-subtle); color: var(--text-primary);"></label>
            <label style="display: flex; flex-direction: column; gap: 6px; font-size: 0.85rem; color: var(--text-secondary);">Email <input type="email" name="email" maxlength="190" required style="padding: 10px 14px; border-radius: var(--radius-sm); background: var(--bg-surface-raised); border: 1px solid var(--border-subtle); color: var(--text-primary);"></label>
          </div>
          <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px;">
            <label style="display: flex; flex-direction: column; gap: 6px; font-size: 0.85rem; color: var(--text-secondary);">Platform <input name="platform" placeholder="windows-x64" maxlength="60" required style="padding: 10px 14px; border-radius: var(--radius-sm); background: var(--bg-surface-raised); border: 1px solid var(--border-subtle); color: var(--text-primary);"></label>
            <label style="display: flex; flex-direction: column; gap: 6px; font-size: 0.85rem; color: var(--text-secondary);">CLI Version <input name="cli_version" placeholder="2.2.1" maxlength="60" required style="padding: 10px 14px; border-radius: var(--radius-sm); background: var(--bg-surface-raised); border: 1px solid var(--border-subtle); color: var(--text-primary);"></label>
          </div>
          <label style="display: flex; flex-direction: column; gap: 6px; font-size: 0.85rem; color: var(--text-secondary);">Command <input name="command_text" placeholder="tezzc build app.tn" maxlength="255" required style="padding: 10px 14px; border-radius: var(--radius-sm); background: var(--bg-surface-raised); border: 1px solid var(--border-subtle); color: var(--text-primary);"></label>
          <label style="display: flex; flex-direction: column; gap: 6px; font-size: 0.85rem; color: var(--text-secondary);">Error Output <textarea name="error_text" maxlength="8000" rows="4" required style="padding: 10px 14px; border-radius: var(--radius-sm); background: var(--bg-surface-raised); border: 1px solid var(--border-subtle); color: var(--text-primary); font-family: var(--font-mono); font-size: 0.85rem;"></textarea></label>
          <button class="btn btn-outline" type="submit">Submit Compiler Error</button>
        </form>
      </article>
    </div>
  </div>
<?php tn_page_shell_end(); tn_footer(); ?>
