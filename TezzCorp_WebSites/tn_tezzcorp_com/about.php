<?php
declare(strict_types=1);

require_once __DIR__ . '/includes/nav.php';

$boot = tn_boot('/about/');
$GLOBALS['tn_config'] = $boot['config'];

tn_head('About TezzNative | TezzCorp Pvt Ltd.', 'About the TezzNative language, created by Rohit Pathak at TezzCorp Pvt Ltd.', 'about', '/about/');
tn_page_shell_start('About TezzNative', 'ENGINEERED BY TEZZCORP PVT LTD', 'TezzNative is a high-performance native systems and AI programming language created by Rohit Pathak to deliver bare-metal execution speed, native multi-dimensional tensors, and built-in asynchronous coroutines without garbage collector pauses.');
?>
  <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); gap: 24px; margin-top: 30px;">
    <article class="feature-card glass-panel" style="padding: 32px;">
      <div class="card-icon-wrap" style="color: #ff9933; margin-bottom: 20px;">
        <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 2L2 7l10 5 10-5-10-5zM2 17l10 5 10-5M2 12l10 5 10-5"/></svg>
      </div>
      <h2 style="font-size: 1.5rem; font-weight: 700; margin-bottom: 12px; color: var(--text-primary);">Our Vision & Architecture</h2>
      <p style="color: var(--text-secondary); line-height: 1.7; margin-bottom: 16px;">
        TezzNative was founded by <strong>Rohit Pathak</strong> at <strong>TezzCorp Pvt Ltd.</strong> to eliminate the compromise between expressive syntax and raw hardware performance.
      </p>
      <ul style="list-style: none; padding: 0; display: flex; flex-direction: column; gap: 10px; color: var(--text-secondary);">
        <li style="display: flex; align-items: center; gap: 10px;"><span style="color: #ff9933;">&#10003;</span> Compile directly to native standalone Win64 PE and Linux ELF executables.</li>
        <li style="display: flex; align-items: center; gap: 10px;"><span style="color: #ff9933;">&#10003;</span> Native multi-dimensional tensor slicing and AVX2-vectorized matrix math.</li>
        <li style="display: flex; align-items: center; gap: 10px;"><span style="color: #ff9933;">&#10003;</span> First-class <code>async</code> and <code>await</code> coroutine runtime with thread pool worker dispatch.</li>
        <li style="display: flex; align-items: center; gap: 10px;"><span style="color: #ff9933;">&#10003;</span> Zero runtime GC overhead with deterministic scoped memory and <code>defer</code> unwinding.</li>
      </ul>
    </article>

    <article class="feature-card glass-panel" style="padding: 32px;">
      <div class="card-icon-wrap" style="color: #ff9933; margin-bottom: 20px;">
        <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"></circle><polyline points="12 6 12 12 14 14"></polyline></svg>
      </div>
      <h2 style="font-size: 1.5rem; font-weight: 700; margin-bottom: 12px; color: var(--text-primary);">Engineering Standard</h2>
      <p style="color: var(--text-secondary); line-height: 1.7; margin-bottom: 16px;">
        Every feature in TezzNative is backed by strict compiler conformance tests, CI performance gates, and language server protocol validation.
      </p>
      <div style="background: var(--bg-surface-raised); border: 1px solid var(--border-subtle); padding: 18px; border-radius: var(--radius-md); margin-top: 20px;">
        <div style="font-size: 0.85rem; color: #ff9933; font-weight: 700; text-transform: uppercase; margin-bottom: 6px;">Creator & Company</div>
        <div style="font-weight: 700; color: var(--text-primary); font-size: 1.1rem;">Rohit Pathak</div>
        <div style="font-size: 0.9rem; color: var(--text-secondary);">Founder & Lead Language Architect</div>
        <div style="font-size: 0.85rem; color: var(--text-tertiary); margin-top: 4px;">TezzCorp Pvt Ltd.</div>
      </div>
    </article>
  </div>
<?php tn_page_shell_end(); tn_footer(); ?>
