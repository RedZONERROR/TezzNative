<?php
declare(strict_types=1);

require_once __DIR__ . '/includes/nav.php';

$boot = tn_boot('/terms');
$GLOBALS['tn_config'] = $boot['config'];
$effectiveDate = 'March 15, 2026';

tn_head('Terms of Service | TezzNative', 'TezzNative terms of service, platform usage, compiler licenses, and developer guidelines.', 'terms', '/terms');
tn_page_shell_start('Terms of Service', 'TEZZCORP PVT LTD LEGAL', 'These terms govern your use of the TezzNative compiler, package registry, and official online services.');
?>

<div style="max-width: 860px; margin: 30px auto; background: var(--bg-surface); border: 1px solid var(--border-subtle); border-radius: var(--radius-md); padding: 32px;">
  <p style="color: var(--text-tertiary); font-size: 0.88rem; margin-bottom: 24px;">Effective date: <?= htmlspecialchars($effectiveDate) ?> &bull; Published by TezzCorp Pvt Ltd.</p>

  <section style="margin-bottom: 24px;">
    <h3 style="color: var(--text-primary); margin-bottom: 8px;">1. Acceptance of Terms</h3>
    <p style="color: var(--text-secondary); line-height: 1.6;">By downloading, installing, or compiling software with TezzNative, or by accessing tezznative.org services and registries, you agree to these Terms of Service.</p>
  </section>

  <section style="margin-bottom: 24px;">
    <h3 style="color: var(--text-primary); margin-bottom: 8px;">2. Open Source &amp; Toolchain License</h3>
    <p style="color: var(--text-secondary); line-height: 1.6;">The TezzNative native compiler suite, standard libraries, and tooling are distributed under open licenses. You are free to build commercial, closed-source, or open-source software without royalty or runtime fee obligations.</p>
  </section>

  <section style="margin-bottom: 24px;">
    <h3 style="color: var(--text-primary); margin-bottom: 8px;">3. Package Registry Usage</h3>
    <p style="color: var(--text-secondary); line-height: 1.6;">The package registry (<code>registry.tnx</code>) is provided for community module distribution. Packages must not contain malicious code, exploits, or copyrighted material without authorization.</p>
  </section>

  <section style="margin-bottom: 24px;">
    <h3 style="color: var(--text-primary); margin-bottom: 8px;">4. Intellectual Property</h3>
    <p style="color: var(--text-secondary); line-height: 1.6;">TezzNative is a trademark of TezzCorp Pvt Ltd, created and founded by Rohit Pathak. Third-party packages remain the intellectual property of their respective creators.</p>
  </section>

  <section style="margin-bottom: 24px;">
    <h3 style="color: var(--text-primary); margin-bottom: 8px;">5. Limitation of Liability</h3>
    <p style="color: var(--text-secondary); line-height: 1.6;">The compiler toolchain and web services are provided "AS IS", without warranties of any kind. TezzCorp Pvt Ltd is not liable for indirect, incidental, or consequential damages arising from software compilation or deployment.</p>
  </section>

  <section>
    <h3 style="color: var(--text-primary); margin-bottom: 8px;">6. Contact</h3>
    <p style="color: var(--text-secondary); line-height: 1.6;">For legal, licensing, or compliance inquiries, contact <a href="mailto:info@tezzcorp.com" style="color: var(--primary);">info@tezzcorp.com</a>.</p>
  </section>
</div>

<?php tn_page_shell_end(); tn_footer(); ?>
