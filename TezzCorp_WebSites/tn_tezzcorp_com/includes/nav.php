<?php
// includes/nav.php - Navigation, helper wrappers, and UI layout functions

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
      'site_url' => 'https://tn.tezzcorp.com',
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
  require __DIR__ . '/header.php';
}

function tn_nav(string $active = 'home'): void {
?>
<!-- Top Announcement Ribbon -->
<div class="announcement-banner">
  <div class="banner-container">
    <span class="banner-badge">NEW IN v2.2</span>
    <span class="banner-text">Native <code>async/await</code> Coroutines, Scoped <code>defer</code>, 4D Tensors & GGUF Ingestion Are Live!</span>
    <a href="/download/" class="banner-link">
      Get TezzNative v2.2
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
      <a href="/#features" class="nav-link <?= ($active === 'features') ? 'active' : '' ?>">Features</a>
      <a href="/#benchmarks" class="nav-link <?= ($active === 'benchmarks') ? 'active' : '' ?>">Benchmarks</a>
      <a href="/#playground" class="nav-link <?= ($active === 'playground') ? 'active' : '' ?>">Playground</a>
      <a href="/lib/" class="nav-link <?= ($active === 'packages' || $active === 'lib') ? 'active' : '' ?>">Packages (40+)</a>
      <a href="/docs/lsp" class="nav-link <?= ($active === 'lsp') ? 'active' : '' ?>">LSP & IDEs</a>
      <a href="/docs/" class="nav-link <?= ($active === 'docs') ? 'active' : '' ?>">Docs</a>
      <a href="/download/" class="nav-link <?= ($active === 'download') ? 'active' : '' ?>">Downloads</a>
    </nav>

    <!-- Header Actions -->
    <div class="nav-actions">
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

      <!-- Primary Action CTA -->
      <a href="/download/TezzNativeInstaller.exe" class="btn btn-primary btn-sm">
        <svg class="btn-icon" width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
          <polyline points="7 10 12 15 17 10"></polyline>
          <line x1="12" y1="15" x2="12" y2="3"></line>
        </svg>
        <span>Install Tezz</span>
      </a>

      <!-- Mobile Menu Toggle Button -->
      <button class="mobile-toggle" id="mobileToggle" aria-label="Open Navigation Menu">
        <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <line x1="3" y1="12" x2="21" y2="12"></line>
          <line x1="3" y1="6" x2="21" y2="6"></line>
          <line x1="3" y1="18" x2="21" y2="18"></line>
        </svg>
      </button>
    </div>
  </div>
</header>
<?php
}

function tn_page_shell_start(string $title, string $tagline = '', string $lead = ''): void {
?>
<section class="section" style="padding-top: 50px;">
  <div class="section-container">
    <div class="section-header" style="text-align: left; margin-bottom: 40px;">
      <span class="section-pill"><?= htmlspecialchars(strtoupper($tagline ?: 'TEZZNATIVE ECOSYSTEM')) ?></span>
      <h1 class="section-title" style="font-size: 2.8rem;"><?= htmlspecialchars($title) ?></h1>
      <?php if ($lead): ?>
        <p class="section-desc"><?= htmlspecialchars($lead) ?></p>
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
