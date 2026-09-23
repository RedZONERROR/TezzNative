<?php
// download/index.php - TezzNative Download & Installation Portal
$page_title = "Install TezzNative SDK, CLI Toolchain & LSP";
$active_nav = "download";
require_once __DIR__ . '/../includes/header.php';
require_once __DIR__ . '/../includes/nav.php';
?>

<section class="section">
  <div class="section-container">
    <div class="section-header" style="text-align: left; margin-bottom: 40px;">
      <span class="section-pill">OFFICIAL RELEASES</span>
      <h1 class="section-title" style="font-size: 2.8rem;">Install TezzNative v2.2</h1>
      <p class="section-desc">
        Install the complete toolchain via our fast CLI one-liners or download standalone compiler binaries, Language Server Protocol daemon, and SDK archives.
      </p>
    </div>

    <!-- Quick Installation Scripts Ribbon -->
    <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-lg); padding: 24px; margin-bottom: 40px;">
      <div class="terminal-comment"># Windows PowerShell (Automated Setup - Adds to PATH &amp; Environment):</div>
      <div class="terminal-cmd" style="margin-bottom: 18px;">
        <code>irm https://tn.tezzcorp.com/install.ps1 | iex</code>
        <button class="copy-btn" title="Copy command" aria-label="Copy Command">
          <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
        </button>
      </div>

      <div class="terminal-comment"># Linux / macOS (Automated Bash Setup):</div>
      <div class="terminal-cmd" style="margin-bottom: 18px;">
        <code>curl -fsSL https://tn.tezzcorp.com/install.sh | bash</code>
        <button class="copy-btn" title="Copy command" aria-label="Copy Command">
          <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
        </button>
      </div>

      <div class="terminal-comment"># Windows Command Prompt (cmd.exe):</div>
      <div class="terminal-cmd" style="margin-bottom: 0;">
        <code>curl -fsSL https://tn.tezzcorp.com/install.cmd -o install.cmd &amp;&amp; install.cmd</code>
        <button class="copy-btn" title="Copy command" aria-label="Copy Command">
          <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
        </button>
      </div>
    </div>

    <!-- Download Cards Grid -->
    <div class="download-grid" style="margin-bottom: 50px;">
      <!-- Card 1: Windows SDK Archive -->
      <div class="download-card featured">
        <span class="featured-ribbon">Recommended</span>
        <div class="download-icon">
          <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="4 17 10 11 4 5"></polyline><line x1="12" y1="19" x2="20" y2="19"></line></svg>
        </div>
        <h2 class="download-title">Windows SDK Package</h2>
        <div class="download-meta">Windows 10 / 11 (x64) &bull; Complete SDK ZIP</div>
        <p class="download-desc">
          Complete standalone bundle with <code>tezzc.exe</code> compiler, <code>tezz</code> project manager, all 40+ standard library modules, AI kernels, and launch scripts.
        </p>
        <a href="/download/tezznative-sdk.zip" class="btn btn-primary btn-lg" style="width: 100%;">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="7 10 12 15 17 10"></polyline><line x1="12" y1="15" x2="12" y2="3"></line></svg>
          <span>Download tezznative-sdk.zip</span>
        </a>
      </div>

      <!-- Card 2: Linux SDK Archive -->
      <div class="download-card">
        <div class="download-icon">
          <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="3" width="20" height="14" rx="2" ry="2"></rect><line x1="8" y1="21" x2="16" y2="21"></line><line x1="12" y1="17" x2="12" y2="21"></line></svg>
        </div>
        <h2 class="download-title">Linux SDK Package</h2>
        <div class="download-meta">Linux x86_64 &bull; glibc 2.31+ &bull; Tarball</div>
        <p class="download-desc">
          Official Linux toolchain bundle with native ELF compiler, POSIX standard libraries, async networking runtime, and package management tools.
        </p>
        <a href="/download/tezznative-sdk-linux.tar.gz" class="btn btn-secondary btn-lg" style="width: 100%;">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="7 10 12 15 17 10"></polyline><line x1="12" y1="15" x2="12" y2="3"></line></svg>
          <span>Download tezznative-sdk-linux.tar.gz</span>
        </a>
      </div>

      <!-- Card 3: Language Server Daemon -->
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

      <!-- Card 4: CLI Compiler -->
      <div class="download-card">
        <div class="download-icon">
          <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="4 17 10 11 4 5"></polyline><line x1="12" y1="19" x2="20" y2="19"></line></svg>
        </div>
        <h2 class="download-title">Standalone Compiler</h2>
        <div class="download-meta">tezzc.exe &bull; Windows x64 Native</div>
        <p class="download-desc">
          High-speed standalone optimizing compiler. Features PE binary codegen, live REPL, bytecode runner, and C header exporter.
        </p>
        <a href="/download/tezzc.exe" class="btn btn-secondary btn-lg" style="width: 100%;">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="7 10 12 15 17 10"></polyline><line x1="12" y1="15" x2="12" y2="3"></line></svg>
          <span>Download tezzc.exe</span>
        </a>
      </div>
    </div>
  </div>
</section>

<?php require_once __DIR__ . '/../includes/footer.php'; ?>
