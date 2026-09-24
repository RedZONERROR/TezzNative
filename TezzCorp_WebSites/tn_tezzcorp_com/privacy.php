<?php
declare(strict_types=1);

require_once __DIR__ . '/includes/nav.php';

$boot = tn_boot('/privacy');
$GLOBALS['tn_config'] = $boot['config'];
$effectiveDate = 'March 15, 2026';

tn_head('Privacy Policy | TezzNative', 'TezzNative privacy policy and telemetry disclosures for the native compiler toolchain.', 'privacy', '/privacy');
tn_page_shell_start('Privacy Policy', 'DATA PRIVACY & COMPLIANCE', 'Learn how TezzNative and TezzCorp protect developer data, crash logs, and telemetry.');
?>

<div style="max-width: 860px; margin: 30px auto; background: var(--bg-surface); border: 1px solid var(--border-subtle); border-radius: var(--radius-md); padding: 32px;">
  <p style="color: var(--text-tertiary); font-size: 0.88rem; margin-bottom: 24px;">Effective date: <?= htmlspecialchars($effectiveDate) ?> &bull; Published by TezzCorp Pvt Ltd.</p>

  <section style="margin-bottom: 24px;">
    <h3 style="color: var(--text-primary); margin-bottom: 8px;">1. Introduction</h3>
    <p style="color: var(--text-secondary); line-height: 1.6;">TezzCorp Pvt Ltd respects developer privacy. This Privacy Policy explains how information is handled when downloading, installing, or interacting with TezzNative services.</p>
  </section>

  <section style="margin-bottom: 24px;">
    <h3 style="color: var(--text-primary); margin-bottom: 8px;">2. Offline by Default</h3>
    <p style="color: var(--text-secondary); line-height: 1.6;">The TezzNative native compiler (<code>tezzc</code>), bytecode runner (<code>tezzvm</code>), and command-line driver (<code>tezz</code>) operate entirely locally on your computer. Your source code, proprietary algorithms, and datasets are never transmitted to external servers during builds.</p>
  </section>

  <section style="margin-bottom: 24px;">
    <h3 style="color: var(--text-primary); margin-bottom: 8px;">3. Network Interactions</h3>
    <p style="color: var(--text-secondary); line-height: 1.6;">The only network calls initiated by the toolchain occur when explicitly requested by you, such as downloading official packages (<code>tezz get</code>), querying version updates (<code>tezz update</code>), or submitting crash/error reports (<code>tezz doctor --fix</code>).</p>
  </section>

  <section style="margin-bottom: 24px;">
    <h3 style="color: var(--text-primary); margin-bottom: 8px;">4. Telemetry and Crash Reports</h3>
    <p style="color: var(--text-secondary); line-height: 1.6;">Crash logs and compiler error reports sent via the support interface contain only error strings, OS architecture, and compiler versions to assist engineering resolution. No personal identifiable information is harvested.</p>
  </section>

  <section style="margin-bottom: 24px;">
    <h3 style="color: var(--text-primary); margin-bottom: 8px;">5. Data Protection</h3>
    <p style="color: var(--text-secondary); line-height: 1.6;">TezzCorp never sells developer data. Technical telemetry is stored securely and retained only for diagnostic analysis.</p>
  </section>

  <section>
    <h3 style="color: var(--text-primary); margin-bottom: 8px;">6. Contact</h3>
    <p style="color: var(--text-secondary); line-height: 1.6;">For privacy inquiries or data requests, contact <a href="mailto:privacy@tezzcorp.com" style="color: var(--primary);">privacy@tezzcorp.com</a>.</p>
  </section>
</div>

<?php tn_page_shell_end(); tn_footer(); ?>
