<?php
// includes/footer.php - Footer component for TezzNative portal
require_once __DIR__ . '/i18n.php';
?>
<footer class="main-footer">
  <div class="footer-container">
    <div class="footer-grid">
      <!-- Col 1: Brand & Bio -->
      <div class="footer-brand-col">
        <div class="footer-brand">
          <div class="logo-symbol small" style="display: flex; align-items: center; justify-content: center; width: 32px; height: 32px; border-radius: 8px; overflow: hidden; background: rgba(255, 103, 31, 0.1); border: 1px solid rgba(255, 153, 51, 0.3);">
            <img src="/assets/logo.png" alt="TezzCorp Logo" style="width: 100%; height: 100%; object-fit: contain;" />
          </div>
          <span class="footer-brand-name">TezzNative</span>
        </div>
        <p class="footer-bio">
          <?= __('footer_bio', 'Ultra-fast native AI, tensor arithmetic, and systems programming language founded on <strong>18 Oct 2022</strong> by <strong>Rohit Pathak</strong> at <strong>TezzCorp Pvt Ltd.</strong> Featuring zero runtime GC pauses, standalone PE/ELF binaries, first-class async/await concurrency, and native GGUF LLM inference.') ?>
        </p>
        <div class="footer-badges">
          <span class="f-badge">Founded Oct 2022</span>
          <span class="f-badge">Zero-GC Pauses</span>
          <span class="f-badge">Native LSP v3.17</span>
          <span class="f-badge">v1.1.0 Production</span>
        </div>
      </div>

      <!-- Col 2: Documentation -->
      <div class="footer-links-col">
        <h4 class="footer-heading">Documentation</h4>
        <ul class="footer-links">
          <li><a href="/docs/">Getting Started</a></li>
          <li><a href="/docs/language">Language Reference</a></li>
          <li><a href="/docs/lsp">Language Server (LSP)</a></li>
          <li><a href="/frameworks/">Framework Ecosystem</a></li>
          <li><a href="/about/">About TezzNative</a></li>
          <li><a href="/docs/sdk">SDK Reference</a></li>
        </ul>
      </div>

      <!-- Col 3: Standard Libraries -->
      <div class="footer-links-col">
        <h4 class="footer-heading">Standard Packages</h4>
        <ul class="footer-links">
          <li><a href="/lib/#tensor"><code>tensor</code> (Tensors &amp; Matrix)</a></li>
          <li><a href="/lib/#llm_core"><code>llm_core</code> (LLM Engine)</a></li>
          <li><a href="/lib/#net"><code>net</code> (Async HTTP)</a></li>
          <li><a href="/lib/#task"><code>task</code> (Async/Await Runtime)</a></li>
          <li><a href="/lib/#tezzui"><code>tezzui</code> (Native GUI)</a></li>
          <li><a href="/lib/">View all 40+ packages &rarr;</a></li>
        </ul>
      </div>

      <!-- Col 4: Community & Download -->
      <div class="footer-links-col">
        <h4 class="footer-heading">Community &amp; Downloads</h4>
        <ul class="footer-links">
          <li><a href="/download/install.ps1">PowerShell Installer (.ps1)</a></li>
          <li><a href="/download/install.sh">Bash Installer (.sh)</a></li>
          <li><a href="/download/">SDK &amp; All Builds</a></li>
          <li><a href="/community">Developer Forum</a></li>
          <li><a href="/support">Support</a></li>
          <li><a href="https://github.com/TezzCorp/TezzNative" target="_blank" rel="noopener">GitHub Repository</a></li>
          <li><a href="https://tezzcorp.com" target="_blank" rel="noopener">TezzCorp Pvt Ltd.</a></li>
        </ul>
      </div>
    </div>

    <!-- Active Core Development Team -->
    <div style="border-top: 1px solid var(--border-subtle); padding-top: 28px; margin-bottom: 24px;">
      <h4 style="font-size: 0.78rem; font-weight: 700; text-transform: uppercase; letter-spacing: 1px; color: var(--text-tertiary); margin-bottom: 18px;">Active Core Development Team</h4>
      <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(210px, 1fr)); gap: 14px;">
        <div class="team-card" style="padding: 16px 18px; gap: 4px;">
          <div class="team-avatar" style="width: 42px; height: 42px; font-size: 1rem; margin-bottom: 6px;">RP</div>
          <div class="team-name" style="font-size: 0.98rem;">Rohit Pathak</div>
          <div class="team-role">Creator &amp; Lead Architect</div>
          <div class="team-since">Since 18 Oct 2022 &bull; TezzNative v0.1 → v2.2.1</div>
        </div>
        <div class="team-card" style="padding: 16px 18px; gap: 4px;">
          <div class="team-avatar" style="width: 42px; height: 42px; font-size: 1rem; background: linear-gradient(135deg, #0284c7, #0ea5e9); margin-bottom: 6px;">VS</div>
          <div class="team-name" style="font-size: 0.98rem;">Vikash Sharma</div>
          <div class="team-role">UI &amp; UX Designer</div>
          <div class="team-since">Since 01 Jan 2025</div>
        </div>
        <div class="team-card" style="padding: 16px 18px; gap: 4px;">
          <div class="team-avatar" style="width: 42px; height: 42px; font-size: 1rem; background: linear-gradient(135deg, #7c3aed, #a855f7); margin-bottom: 6px;">SM</div>
          <div class="team-name" style="font-size: 0.98rem;">Suman Mandal</div>
          <div class="team-role">Runtime &amp; SDK Engineer</div>
          <div class="team-since">Since 21 Sep 2026 &bull; Native Runtime</div>
        </div>
      </div>
    </div>

    <!-- Bottom Copyright & Status -->
    <div class="footer-bottom">
      <div class="footer-copy">
        &copy; <?= date('Y') ?> <strong>TezzCorp Pvt Ltd.</strong> &mdash; <?= __('footer_copyright', 'TezzNative, founded <strong>18 Oct 2022</strong> by <strong>Rohit Pathak</strong>. All rights reserved.') ?>
      </div>
      <div class="footer-status">
        <span class="status-indicator" style="background: #ff9933; box-shadow: 0 0 8px #ff9933;"></span>
        <span><?= __('footer_operational', 'TezzNative v1.1.0 Production Operational') ?></span>
      </div>
    </div>
  </div>
</footer>

</div> <!-- .site-wrapper -->

<!-- Scripts -->
<script src="/assets/app.js?v=2.2.6"></script>
</body>
</html>
