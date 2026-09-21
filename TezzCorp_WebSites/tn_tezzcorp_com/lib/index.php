<?php
// lib/index.php - TezzNative Package & Standard Library Registry
$page_title = "Standard Library & Package Registry";
$active_nav = "packages";
require_once __DIR__ . '/../includes/header.php';
require_once __DIR__ . '/../includes/nav.php';

// Include the packages array from API
require_once __DIR__ . '/../api/packages.php';
?>

<section class="section">
  <div class="section-container">
    <div class="section-header" style="text-align: left; margin-bottom: 36px;">
      <span class="section-pill">OFFICIAL REPOSITORY</span>
      <h1 class="section-title" style="font-size: 2.8rem;">Standard Libraries &amp; Packages</h1>
      <p class="section-desc">
        Explore TezzNative's curated ecosystem of high-performance modules for machine learning, asynchronous networking, desktop GUIs, and bare-metal systems programming.
      </p>
    </div>

    <!-- AJAX Search Bar -->
    <div class="registry-search-bar">
      <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="#64748b" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
        <circle cx="11" cy="11" r="8"></circle>
        <line x1="21" y1="21" x2="16.65" y2="16.65"></line>
      </svg>
      <input type="text" id="pkgSearchInput" class="search-input" placeholder="Search 40+ modules by name or keyword (e.g. tensor, async, gguf, http)..." autocomplete="off">
    </div>

    <!-- Category Filter Chips -->
    <div class="category-filter-chips">
      <button class="chip-btn active" data-cat="all">All Packages (40+)</button>
      <button class="chip-btn" data-cat="ai">AI &amp; Machine Learning</button>
      <button class="chip-btn" data-cat="async">Concurrency &amp; I/O</button>
      <button class="chip-btn" data-cat="web">Networking &amp; Web</button>
      <button class="chip-btn" data-cat="system">Systems &amp; Low-level</button>
      <button class="chip-btn" data-cat="gui">GUI &amp; Graphics</button>
      <button class="chip-btn" data-cat="data">Data &amp; Storage</button>
    </div>

    <!-- Packages Grid -->
    <div class="packages-grid" id="packagesGrid">
      <?php foreach ($packages as $pkg): ?>
        <div class="package-card" data-name="<?= htmlspecialchars(strtolower($pkg['name'])) ?>" data-cat="<?= htmlspecialchars($pkg['category']) ?>" id="<?= htmlspecialchars($pkg['name']) ?>">
          <div class="pkg-header">
            <span class="pkg-name"><?= htmlspecialchars($pkg['name']) ?></span>
            <span class="pkg-ver">v<?= htmlspecialchars($pkg['version']) ?></span>
          </div>
          <span class="f-badge" style="align-self: flex-start; margin-bottom: 12px; font-size: 0.75rem;"><?= htmlspecialchars($pkg['category_name']) ?></span>
          <p class="pkg-desc"><?= htmlspecialchars($pkg['desc']) ?></p>
          <div class="pkg-actions">
            <code class="pkg-cmd"><?= htmlspecialchars($pkg['install']) ?></code>
            <button class="copy-btn" data-copy="<?= htmlspecialchars($pkg['install']) ?>" title="Copy install command" aria-label="Copy install command">
              <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect>
                <path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path>
              </svg>
            </button>
          </div>
        </div>
      <?php endforeach; ?>
      <!-- No results message (shown by JS when filter returns nothing) -->
      <div id="pkgNoResult" style="display:none; grid-column: 1/-1; text-align:center; padding: 60px 20px; color: var(--text-tertiary);">
        <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" style="opacity:0.4; margin-bottom: 16px; display: block; margin-left: auto; margin-right: auto;"><circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/></svg>
        <p style="font-size: 1.05rem; font-weight: 600; margin-bottom: 6px;">No packages found</p>
        <p style="font-size: 0.9rem;">Try a different search term or category filter.</p>
      </div>
    </div>
  </div>
</section>

<?php require_once __DIR__ . '/../includes/footer.php'; ?>
