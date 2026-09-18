<?php
// docs/lsp.php - Official TezzNative Language Server Protocol Guide
$page_title = "Language Server Protocol (LSP) & IDE Setup Guide";
$active_nav = "lsp";
require_once __DIR__ . '/../includes/header.php';
require_once __DIR__ . '/../includes/nav.php';
?>

<section class="section">
  <div class="section-container">
    <div class="section-header" style="text-align: left; margin-bottom: 40px;">
      <span class="section-pill">DEVELOPER TOOLING</span>
      <h1 class="section-title" style="font-size: 2.8rem;">Language Server Protocol (LSP)</h1>
      <p class="section-desc">
        TezzNative includes a high-performance native Language Server daemon (<code>tezz_lsp.exe</code>) implementing the LSP v3.17 specification for full autocompletion, hover diagnostics, and definition navigation in any editor.
      </p>
      <div style="display: flex; gap: 12px; margin-top: 20px; flex-wrap: wrap;">
        <a href="/download/tezz_lsp.exe" class="btn btn-primary">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="7 10 12 15 17 10"></polyline><line x1="12" y1="15" x2="12" y2="3"></line></svg>
          <span>Download tezz_lsp.exe</span>
        </a>
        <a href="/download/" class="btn btn-secondary">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="3" width="18" height="18" rx="2" ry="2"></rect><line x1="3" y1="9" x2="21" y2="9"></line><line x1="9" y1="21" x2="9" y2="9"></line></svg>
          <span>Get VS Code Extension</span>
        </a>
      </div>
    </div>

    <!-- 4 Capabilities Cards -->
    <div class="features-grid" style="margin-bottom: 50px;">
      <div class="feature-card">
        <div class="feature-icon-wrapper">
          <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"></circle><line x1="12" y1="8" x2="12" y2="12"></line><line x1="12" y1="16" x2="12.01" y2="16"></line></svg>
        </div>
        <h3 class="feature-title">Real-Time Diagnostics</h3>
        <p class="feature-desc">Instant syntax validation, missing colon detection, type mismatch warnings, and compile-time checks as you type.</p>
      </div>

      <div class="feature-card">
        <div class="feature-icon-wrapper">
          <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"></circle><path d="M9.09 9a3 3 0 0 1 5.83 1c0 2-3 3-3 3"></path><line x1="12" y1="17" x2="12.01" y2="17"></line></svg>
        </div>
        <h3 class="feature-title">Hover Type Inspection</h3>
        <p class="feature-desc">Hover over functions, variables, structs, or tensor shapes to view signature types and documentation.</p>
      </div>

      <div class="feature-card">
        <div class="feature-icon-wrapper">
          <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polygon points="3 11 22 2 13 21 11 13 3 11"></polygon></svg>
        </div>
        <h3 class="feature-title">Go to Definition (F12)</h3>
        <p class="feature-desc">Jump directly to function declarations, imported modules, structs, and constants across workspace files.</p>
      </div>

      <div class="feature-card">
        <div class="feature-icon-wrapper">
          <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="16 18 22 12 16 6"></polyline><polyline points="8 6 2 12 8 18"></polyline></svg>
        </div>
        <h3 class="feature-title">Smart Autocompletion</h3>
        <p class="feature-desc">Context-aware completions for keywords (<code>async</code>, <code>await</code>, <code>defer</code>), module exports, and tensor methods.</p>
      </div>
    </div>

    <!-- Editor Setup Instructions -->
    <div class="section-header" style="text-align: left; margin-bottom: 24px;">
      <h2 class="section-title" style="font-size: 2rem;">Editor Setup Instructions</h2>
    </div>

    <!-- Neovim Setup Box -->
    <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-lg); padding: 24px; margin-bottom: 28px;">
      <h3 style="font-size: 1.2rem; margin-bottom: 12px; color: #38bdf8;">1. Neovim (nvim-lspconfig)</h3>
      <p style="color: var(--text-secondary); margin-bottom: 14px; font-size: 0.9rem;">Add the following snippet to your <code>init.lua</code> or LSP configuration file:</p>
      <pre class="feature-code-snippet" style="font-size: 0.86rem;"><code>local lspconfig = require('lspconfig')
local configs = require('lspconfig.configs')

if not configs.tezz_lsp then
  configs.tezz_lsp = {
    default_config = {
      cmd = { "tezz_lsp.exe" },
      filetypes = { "tezz", "tn" },
      root_dir = lspconfig.util.root_pattern("tezz.mod", ".git"),
      settings = {},
    },
  }
end

lspconfig.tezz_lsp.setup({})</code></pre>
    </div>

    <!-- Zed Editor Setup Box -->
    <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-lg); padding: 24px; margin-bottom: 28px;">
      <h3 style="font-size: 1.2rem; margin-bottom: 12px; color: #38bdf8;">2. Zed Editor</h3>
      <p style="color: var(--text-secondary); margin-bottom: 14px; font-size: 0.9rem;">In <code>~/.config/zed/settings.json</code>, register the language server:</p>
      <pre class="feature-code-snippet" style="font-size: 0.86rem;"><code>{
  "languages": {
    "TezzNative": {
      "language_servers": ["tezz-lsp"]
    }
  },
  "lsp": {
    "tezz-lsp": {
      "binary": { "path": "tezz_lsp.exe" }
    }
  }
}</code></pre>
    </div>

    <!-- VS Code Setup Box -->
    <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-lg); padding: 24px;">
      <h3 style="font-size: 1.2rem; margin-bottom: 12px; color: #38bdf8;">3. Visual Studio Code &amp; Cursor AI</h3>
      <p style="color: var(--text-secondary); margin-bottom: 14px; font-size: 0.9rem;">Install the official <code>tezznative-vscode</code> extension package:</p>
      <pre class="feature-code-snippet" style="font-size: 0.86rem;"><code>code --install-extension tezznative-language-2.2.vsix
# Or launch the TezzNativeInstaller.exe GUI which automatically installs into VS Code and Cursor!</code></pre>
    </div>
  </div>
</section>

<?php require_once __DIR__ . '/../includes/footer.php'; ?>
