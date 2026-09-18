<?php
// download/index.php - TezzNative Download Portal
$page_title = "Download TezzNative SDK, GUI Installer & LSP";
$active_nav = "download";
require_once __DIR__ . '/../includes/header.php';
require_once __DIR__ . '/../includes/nav.php';
?>

<section class="section">
  <div class="section-container">
    <div class="section-header" style="text-align: left; margin-bottom: 40px;">
      <span class="section-pill">OFFICIAL RELEASES</span>
      <h1 class="section-title" style="font-size: 2.8rem;">Download TezzNative v2.2</h1>
      <p class="section-desc">
        Download the official installer, standalone CLI compiler, Language Server Protocol daemon, or complete standard library SDK.
      </p>
    </div>

    <!-- Download Cards Grid -->
    <div class="download-grid" style="margin-bottom: 50px;">
      <!-- Card 1: Windows GUI Installer (Featured) -->
      <div class="download-card featured">
        <span class="featured-ribbon">Recommended</span>
        <div class="download-icon">
          <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="3" width="20" height="14" rx="2" ry="2"></rect><line x1="8" y1="21" x2="16" y2="21"></line><line x1="12" y1="17" x2="12" y2="21"></line></svg>
        </div>
        <h2 class="download-title">Native GUI Installer</h2>
        <div class="download-meta">Windows 10 / 11 (x64) &bull; Standalone PE Executable</div>
        <p class="download-desc">
          Interactive desktop installation wizard. Configures the TezzNative compiler, sets up environment PATH, installs the standard library modules, and registers the VS Code / Cursor IDE extensions.
        </p>
        <a href="/download/TezzNativeInstaller.exe" class="btn btn-primary btn-lg" style="width: 100%;">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="7 10 12 15 17 10"></polyline><line x1="12" y1="15" x2="12" y2="3"></line></svg>
          <span>Download TezzNativeInstaller.exe</span>
        </a>
      </div>

      <!-- Card 2: Language Server Daemon -->
      <div class="download-card">
        <div class="download-icon">
          <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="16 18 22 12 16 6"></polyline><polyline points="8 6 2 12 8 18"></polyline></svg>
        </div>
        <h2 class="download-title">Language Server (tezz_lsp)</h2>
        <div class="download-meta">LSP v3.17 &bull; Windows x64 Native</div>
        <p class="download-desc">
          Official JSON-RPC Language Server daemon for IDEs including Neovim, Zed, VS Code, and Cursor AI. Provides real-time syntax checking and hover type inspection.
        </p>
        <a href="/download/tezz_lsp.exe" class="btn btn-secondary btn-lg" style="width: 100%;">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="7 10 12 15 17 10"></polyline><line x1="12" y1="15" x2="12" y2="3"></line></svg>
          <span>Download tezz_lsp.exe</span>
        </a>
      </div>

      <!-- Card 3: CLI Compiler & Toolchain -->
      <div class="download-card">
        <div class="download-icon">
          <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="4 17 10 11 4 5"></polyline><line x1="12" y1="19" x2="20" y2="19"></line></svg>
        </div>
        <h2 class="download-title">Command-Line Compiler</h2>
        <div class="download-meta">tezzc.exe &bull; Windows x64 Native</div>
        <p class="download-desc">
          High-speed native optimizing compiler. Features standalone PE binary codegen, live REPL, bytecode runner, and C header exporter.
        </p>
        <a href="/download/tezzc.exe" class="btn btn-secondary btn-lg" style="width: 100%;">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="7 10 12 15 17 10"></polyline><line x1="12" y1="15" x2="12" y2="3"></line></svg>
          <span>Download tezzc.exe</span>
        </a>
      </div>
    </div>

    <!-- Quick Installation Scripts -->
    <div class="section-header" style="text-align: left; margin-bottom: 20px;">
      <h2 class="section-title" style="font-size: 1.8rem;">Command-Line Quick Installation</h2>
    </div>

    <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-lg); padding: 24px; margin-bottom: 40px;">
      <div class="terminal-comment"># Windows PowerShell (Automated Setup):</div>
      <div class="terminal-cmd">
        <code>irm https://tn.tezzcorp.com/download/install.ps1 | iex</code>
        <button class="copy-btn" title="Copy command" aria-label="Copy Command">
          <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
        </button>
      </div>

      <div class="terminal-comment"># Linux / macOS (Bash Installation):</div>
      <div class="terminal-cmd" style="margin-bottom: 0;">
        <code>curl -fsSL https://tn.tezzcorp.com/download/install.sh | bash</code>
        <button class="copy-btn" title="Copy command" aria-label="Copy Command">
          <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
        </button>
      </div>
    </div>
  </div>
</section>

<?php require_once __DIR__ . '/../includes/footer.php'; ?>
