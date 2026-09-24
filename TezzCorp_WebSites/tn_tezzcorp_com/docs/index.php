<?php
declare(strict_types=1);
require_once __DIR__ . '/_common.php';
tn_doc_boot('/docs/', 'docs');
tn_head('TezzNative Documentation', 'TezzNative documentation overview with install, syntax, standard library, stability, and production roadmap links.', 'docs', '/docs/');
tn_page_shell_start('Documentation', 'Everything needed to evaluate the language honestly.', 'Start with install, language fundamentals, examples, stability labels, and the optimization roadmap.');
?>
  <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 20px; margin-top: 30px;">
    <article class="feature-card glass-panel" style="padding: 26px;"><h3>Getting Started</h3><p style="color: var(--text-secondary); font-size: 0.92rem; margin: 10px 0 16px;">Install the SDK, configure your IDE, and build your first native application.</p><a class="btn btn-outline btn-sm" href="/docs/getting-started">Open Guide &rarr;</a></article>
    <article class="feature-card glass-panel" style="padding: 26px;"><h3>Language Server (LSP)</h3><p style="color: var(--text-secondary); font-size: 0.92rem; margin: 10px 0 16px;">Configure VS Code, Neovim, Zed, and Cursor with autocomplete and diagnostics.</p><a class="btn btn-outline btn-sm" href="/docs/lsp">Open Setup &rarr;</a></article>
    <article class="feature-card glass-panel" style="padding: 26px;"><h3>Language Tour</h3><p style="color: var(--text-secondary); font-size: 0.92rem; margin: 10px 0 16px;">Clean Pythonic syntax, native tensors, SIMD vectors, actor supervision, and TCO.</p><a class="btn btn-outline btn-sm" href="/docs/language">Open Tour &rarr;</a></article>
    <article class="feature-card glass-panel" style="padding: 26px;"><h3>GGUF Model Loading</h3><p style="color: var(--text-secondary); font-size: 0.92rem; margin: 10px 0 16px;">Native zero-dependency Q4_0, Q8_0, and f16 quantized transformer execution.</p><a class="btn btn-outline btn-sm" href="/docs/examples">Open Examples &rarr;</a></article>
    <article class="feature-card glass-panel" style="padding: 26px;"><h3>Standard Library (50+)</h3><p style="color: var(--text-secondary); font-size: 0.92rem; margin: 10px 0 16px;">Full API inventory for <code>tensor</code>, <code>actor</code>, <code>task</code>, <code>net</code>, <code>io</code>, and <code>tezzui</code>.</p><a class="btn btn-outline btn-sm" href="/lib/">Explore Packages &rarr;</a></article>
    <article class="feature-card glass-panel" style="padding: 26px;"><h3>Native Codegen Matrix</h3><p style="color: var(--text-secondary); font-size: 0.92rem; margin: 10px 0 16px;">Standalone Win64 PE and Linux ELF x86-64 binary compilation pipelines.</p><a class="btn btn-outline btn-sm" href="/docs/native-backend">Open Backend &rarr;</a></article>
  </div>
<?php tn_page_shell_end(); tn_footer(); ?>
