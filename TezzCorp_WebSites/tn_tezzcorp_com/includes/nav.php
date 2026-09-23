<?php
// includes/nav.php - Navigation, helper wrappers, and UI layout functions
require_once __DIR__ . '/i18n.php';

function tn_boot(string $path = '/'): array {
  // Load config + .env (idempotent)
  static $booted = false;
  if (!$booted) {
    $cfgFile = __DIR__ . '/../config/config.php';
    if (is_file($cfgFile)) {
      require_once $cfgFile;
    }
    $booted = true;
  }

  // Load DB layer
  if (!function_exists('tn_pdo')) {
    require_once __DIR__ . '/../config/db.php';
  }

  // Load community DB functions
  if (!function_exists('tezz_register_user')) {
    require_once __DIR__ . '/../config/community_db.php';
  }

  // Get PDO (may throw — callers should catch)
  $pdo = null;
  try {
    $pdo = tn_pdo();
  } catch (Throwable $e) {
    // Silently degrade — page will render without DB features
  }

  return [
    'config' => [
      'site_name' => 'TezzNative',
      'site_url' => 'https://tezznative.org',
      'version' => '2.2.1'
    ],
    'version' => [
      'version' => '2.2.1',
      'build' => '2026.09',
      'status' => 'Production Mature'
    ],
    'pdo' => $pdo,
  ];
}

function tn_head(string $title = '', string $desc = '', string $active = 'home', string $path = '/'): void {
  $page_title = $title;
  $page_desc = $desc;
  $active_nav = $active;
  $canonical_path = $path;
  require __DIR__ . '/header.php';
}

function tn_nav(string $active = 'home'): void {
?>
<!-- Top Announcement Ribbon -->
<div class="announcement-banner">
  <div class="banner-container">
    <span class="banner-badge"><?= __('banner_badge', 'NEW IN v2.2') ?></span>
    <span class="banner-text"><?= __('banner_text', 'Native async/await Coroutines, Scoped defer, 4D Tensors & GGUF Ingestion Are Live!') ?></span>
    <a href="/download/" class="banner-link">
      <?= __('banner_link', 'Get TezzNative v2.2') ?>
      <svg class="icon-inline" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12h14M12 5l7 7-7 7"/></svg>
    </a>
  </div>
</div>

<!-- Primary Navigation Header -->
<header class="main-header" id="mainHeader">
  <div class="nav-container">
    <!-- Brand Logo -->
    <a href="/" class="brand-logo" aria-label="TezzNative Home">
      <div class="logo-symbol" style="display: flex; align-items: center; justify-content: center; width: 36px; height: 36px; border-radius: 8px; overflow: hidden; background: rgba(255, 103, 31, 0.1); border: 1px solid rgba(255, 153, 51, 0.3);">
        <img src="/assets/logo.png" alt="TezzCorp Logo" style="width: 100%; height: 100%; object-fit: contain;" />
      </div>
      <div style="display: flex; flex-direction: column; line-height: 1.1;">
        <span class="brand-text">Tezz<span class="brand-accent">Native</span></span>
        <span style="font-size: 0.65rem; color: #94a3b8; font-weight: 500;">TezzCorp Pvt Ltd</span>
      </div>
      <span class="version-tag">v2.2.1</span>
    </a>

    <!-- Desktop Navigation Links -->
    <nav class="nav-menu" id="navMenu">
      <a href="/#features" class="nav-link <?= ($active === 'features') ? 'active' : '' ?>"><?= __('nav_features', 'Features') ?></a>
      <a href="/#benchmarks" class="nav-link <?= ($active === 'benchmarks') ? 'active' : '' ?>"><?= __('nav_benchmarks', 'Benchmarks') ?></a>
      <a href="/#playground" class="nav-link <?= ($active === 'playground') ? 'active' : '' ?>"><?= __('nav_playground', 'Playground') ?></a>
      <a href="/lib/" class="nav-link <?= ($active === 'packages' || $active === 'lib') ? 'active' : '' ?>"><?= __('nav_packages', 'Packages (40+)') ?></a>
      <a href="/docs/lsp" class="nav-link <?= ($active === 'lsp') ? 'active' : '' ?>"><?= __('nav_lsp', 'LSP & IDEs') ?></a>
      <a href="/docs/" class="nav-link <?= ($active === 'docs') ? 'active' : '' ?>"><?= __('nav_docs', 'Docs') ?></a>
      <a href="/download/" class="nav-link <?= ($active === 'download') ? 'active' : '' ?>"><?= __('nav_downloads', 'Downloads') ?></a>
    </nav>

    <!-- Header Actions -->
    <div class="nav-actions">
      <!-- Language Selector -->
      <?= function_exists('tn_lang_switcher_html') ? tn_lang_switcher_html() : '' ?>

      <!-- Theme Switcher -->
      <button class="theme-toggle" id="themeToggle" title="Toggle Light/Dark Theme" aria-label="Toggle Theme">
        <!-- Sun Icon (for Dark mode) -->
        <svg class="sun-icon" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <circle cx="12" cy="12" r="5"></circle>
          <line x1="12" y1="1" x2="12" y2="3"></line>
          <line x1="12" y1="21" x2="12" y2="23"></line>
          <line x1="4.22" y1="4.22" x2="5.64" y2="5.64"></line>
          <line x1="18.36" y1="18.36" x2="19.78" y2="19.78"></line>
          <line x1="1" y1="12" x2="3" y2="12"></line>
          <line x1="21" y1="12" x2="23" y2="12"></line>
          <line x1="4.22" y1="19.78" x2="5.64" y2="18.36"></line>
          <line x1="18.36" y1="5.64" x2="19.78" y2="4.22"></line>
        </svg>
        <!-- Moon Icon (for Light mode) -->
        <svg class="moon-icon" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"></path>
        </svg>
      </button>

      <!-- Primary Action CTA (Desktop only - hidden on mobile) -->
      <a href="/download/" class="btn btn-primary btn-sm btn-install-desktop">
        <svg class="btn-icon" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <polyline points="4 17 10 11 4 5"></polyline>
          <line x1="12" y1="19" x2="20" y2="19"></line>
        </svg>
        <span><?= __('nav_install_cli', 'Install Tezz') ?></span>
      </a>

      <!-- Mobile Menu Toggle Button (Hamburger) -->
      <button class="mobile-toggle" id="mobileToggle" aria-label="Open Navigation Menu" aria-controls="mobileSidebar" aria-expanded="false">
        <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <line x1="3" y1="12" x2="21" y2="12"></line>
          <line x1="3" y1="6" x2="21" y2="6"></line>
          <line x1="3" y1="18" x2="21" y2="18"></line>
        </svg>
      </button>
    </div>
  </div>
</header>

<!-- Mobile Sidebar Backdrop Overlay -->
<div class="sidebar-overlay" id="sidebarOverlay" aria-hidden="true"></div>

<!-- Mobile Off-Canvas Sidebar Drawer -->
<aside class="mobile-sidebar" id="mobileSidebar" aria-label="Mobile Navigation" aria-hidden="true">
  <div class="sidebar-header">
    <a href="/" class="brand-logo" aria-label="TezzNative Home">
      <div class="logo-symbol" style="display: flex; align-items: center; justify-content: center; width: 32px; height: 32px; border-radius: 8px; overflow: hidden; background: rgba(255, 103, 31, 0.1); border: 1px solid rgba(255, 153, 51, 0.3);">
        <img src="/assets/logo.png" alt="TezzCorp Logo" style="width: 100%; height: 100%; object-fit: contain;" />
      </div>
      <div style="display: flex; flex-direction: column; line-height: 1.1;">
        <span class="brand-text" style="font-size: 1.15rem;">Tezz<span class="brand-accent">Native</span></span>
        <span style="font-size: 0.62rem; color: #94a3b8; font-weight: 500;">TezzCorp Pvt Ltd</span>
      </div>
      <span class="version-tag">v2.2.1</span>
    </a>
    <button class="sidebar-close-btn" id="sidebarClose" aria-label="Close Navigation Menu">
      <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
        <line x1="18" y1="6" x2="6" y2="18"></line>
        <line x1="6" y1="6" x2="18" y2="18"></line>
      </svg>
    </button>
  </div>

  <div class="sidebar-body">
    <div style="padding: 10px 16px; border-bottom: 1px solid var(--border-subtle); display: flex; align-items: center; justify-content: space-between;">
      <span style="font-size: 0.8rem; color: var(--text-tertiary); font-weight: 600; text-transform: uppercase;"><?= __('lang_selector', 'Language') ?></span>
      <?= function_exists('tn_lang_switcher_html') ? tn_lang_switcher_html() : '' ?>
    </div>

    <div class="sidebar-section-label" style="padding-top: 14px;">Navigation Menu</div>
    <nav class="sidebar-nav">
      <a href="/#features" class="sidebar-link <?= ($active === 'features') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2"/></svg>
        <span><?= __('nav_features', 'Features') ?></span>
      </a>
      <a href="/#benchmarks" class="sidebar-link <?= ($active === 'benchmarks') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><line x1="18" y1="20" x2="18" y2="10"/><line x1="12" y1="20" x2="12" y2="4"/><line x1="6" y1="20" x2="6" y2="14"/></svg>
        <span><?= __('nav_benchmarks', 'Benchmarks') ?></span>
      </a>
      <a href="/#playground" class="sidebar-link <?= ($active === 'playground') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="16 18 22 12 16 6"/><polyline points="8 6 2 12 8 18"/></svg>
        <span><?= __('nav_playground', 'Playground') ?></span>
      </a>
      <a href="/lib/" class="sidebar-link <?= ($active === 'packages' || $active === 'lib') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"/></svg>
        <span><?= __('nav_packages', 'Packages') ?> <span class="sidebar-badge">40+</span></span>
      </a>
      <a href="/docs/lsp" class="sidebar-link <?= ($active === 'lsp') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="2" y="3" width="20" height="14" rx="2" ry="2"/><line x1="8" y1="21" x2="16" y2="21"/><line x1="12" y1="17" x2="12" y2="21"/></svg>
        <span><?= __('nav_lsp', 'LSP & IDEs') ?></span>
      </a>
      <a href="/docs/" class="sidebar-link <?= ($active === 'docs') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"/><path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"/></svg>
        <span><?= __('nav_docs', 'Documentation') ?></span>
      </a>
      <a href="/community/" class="sidebar-link <?= ($active === 'community') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2"/><circle cx="9" cy="7" r="4"/><path d="M23 21v-2a4 4 0 0 0-3-3.87"/><path d="M16 3.13a4 4 0 0 1 0 7.75"/></svg>
        <span>Community</span>
      </a>
      <a href="/support.php" class="sidebar-link <?= ($active === 'support') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><path d="M9.09 9a3 3 0 0 1 5.83 1c0 2-3 3-3 3"/><line x1="12" y1="17" x2="12.01" y2="17"/></svg>
        <span>Support</span>
      </a>
      <a href="/download/" class="sidebar-link <?= ($active === 'download') ? 'active' : '' ?>">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>
        <span><?= __('nav_downloads', 'All Downloads') ?></span>
      </a>
    </nav>

    <!-- Prominent Mobile CTA Box -->
    <div class="sidebar-cta-box">
      <div class="sidebar-cta-title">Install TezzNative v2.2.1</div>
      <p class="sidebar-cta-desc">Standalone C speed, native tensors & async runtime on your machine.</p>
      <a href="/download/" class="btn btn-primary sidebar-btn-install">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <polyline points="4 17 10 11 4 5"></polyline>
          <line x1="12" y1="19" x2="20" y2="19"></line>
        </svg>
        <span><?= __('nav_install_cli', 'Install TezzNative (CLI)') ?></span>
      </a>
      <div class="sidebar-cta-links">
        <a href="/download/tezznative-sdk-linux.tar.gz" class="sidebar-sub-link">Linux SDK (.tar.gz) &rarr;</a>
        <a href="/download/" class="sidebar-sub-link">Manifest & SHA256 &rarr;</a>
      </div>
    </div>
  </div>

  <div class="sidebar-footer">
    <div class="sidebar-footer-links">
      <a href="https://github.com/TezzCorp/TezzNative" target="_blank" rel="noopener">GitHub</a>
      <span>&bull;</span>
      <a href="/community/">Forum</a>
      <span>&bull;</span>
      <a href="/docs/">Docs</a>
    </div>
    <div class="sidebar-copy">&copy; <?= date('Y') ?> TezzCorp Pvt Ltd &bull; Production Ready</div>
  </div>
</aside>
<?php
}

function tn_page_shell_start(string $title, string $tagline = '', string $lead = ''): void {
?>
<section class="section page-shell-section">
  <div class="section-container">
    <div class="section-header page-shell-header">
      <span class="section-pill"><?= htmlspecialchars(strtoupper($tagline ?: 'TEZZNATIVE ECOSYSTEM')) ?></span>
      <h1 class="section-title page-shell-title"><?= htmlspecialchars($title) ?></h1>
      <?php if ($lead): ?>
        <p class="section-desc page-shell-desc"><?= htmlspecialchars($lead) ?></p>
      <?php endif; ?>
    </div>
<?php
}

function tn_page_shell_end(): void {
?>
  </div>
</section>
<?php
}

function tn_footer(): void {
  require __DIR__ . '/footer.php';
}
?>
