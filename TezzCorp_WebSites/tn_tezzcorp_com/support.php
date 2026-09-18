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
  <div class="support-container">
    <?php if ($notice !== ''): ?><p class="tn-notice <?= $noticeOk ? 'tn-notice-ok' : 'tn-notice-error' ?>"><?= htmlspecialchars($notice) ?></p><?php endif; ?>
    <div class="support-grid">
      <article class="feature-card glass-panel support-card">
        <h2 style="font-size: 1.4rem; font-weight: 700; color: var(--text-primary); margin-bottom: 12px;">Support Request</h2>
        <form class="tn-form" method="post" enctype="multipart/form-data">
          <input type="hidden" name="form_type" value="support">
          <div class="form-row-2">
            <label class="form-label">Type <select name="support_type" class="form-input"><option>support</option><option>bug</option><option>feature</option><option>enterprise</option></select></label>
            <label class="form-label">Name <input name="user_name" class="form-input" maxlength="100" required></label>
          </div>
          <label class="form-label">Email <input type="email" name="email" class="form-input" maxlength="190" required></label>
          <label class="form-label">Title <input name="title" class="form-input" maxlength="160" required></label>
          <label class="form-label">Message <textarea name="message" class="form-input" maxlength="5000" rows="4" required></textarea></label>
          <label class="form-label">Attachment <input type="file" name="attachment" class="form-input form-file-input"></label>
          <button class="btn btn-primary" type="submit" style="width: 100%;">Submit Support Request</button>
        </form>
      </article>

      <article class="feature-card glass-panel support-card">
        <h2 style="font-size: 1.4rem; font-weight: 700; color: var(--text-primary); margin-bottom: 12px;">Compiler Error Report</h2>
        <form class="tn-form" method="post">
          <input type="hidden" name="form_type" value="error">
          <div class="form-row-2">
            <label class="form-label">Name <input name="user_name" class="form-input" maxlength="100" required></label>
            <label class="form-label">Email <input type="email" name="email" class="form-input" maxlength="190" required></label>
          </div>
          <div class="form-row-2">
            <label class="form-label">Platform <input name="platform" class="form-input" placeholder="windows-x64" maxlength="60" required></label>
            <label class="form-label">CLI Version <input name="cli_version" class="form-input" placeholder="2.2.1" maxlength="60" required></label>
          </div>
          <label class="form-label">Command <input name="command_text" class="form-input" placeholder="tezzc build app.tn" maxlength="255" required></label>
          <label class="form-label">Error Output <textarea name="error_text" class="form-input" maxlength="8000" rows="4" required style="font-family: var(--font-mono); font-size: 0.85rem;"></textarea></label>
          <button class="btn btn-outline" type="submit" style="width: 100%;">Submit Compiler Error</button>
        </form>
      </article>
    </div>
  </div>
<?php tn_page_shell_end(); tn_footer(); ?>
