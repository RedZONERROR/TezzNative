<?php
// includes/footer.php - Footer component for TezzNative portal
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
          Ultra-fast native AI, tensor arithmetic, and systems programming language engineered by <strong>TezzCorp Pvt Ltd.</strong> and created by <strong>Rohit Pathak</strong>. Featuring zero runtime garbage collection pauses, standalone PE/ELF binaries, and first-class async concurrency.
        </p>
        <div class="footer-badges">
          <span class="f-badge">Zero-GC Pauses</span>
          <span class="f-badge">Standalone Executables</span>
          <span class="f-badge">Native LSP v3.17</span>
        </div>
      </div>

      <!-- Col 2: Documentation & Tooling -->
      <div class="footer-links-col">
        <h4 class="footer-heading">Documentation</h4>
        <ul class="footer-links">
          <li><a href="/docs/">Getting Started</a></li>
          <li><a href="/docs/lsp">Language Server (LSP)</a></li>
          <li><a href="/#playground">Language Tour & Syntax</a></li>
          <li><a href="/docs/">Standard Library Index</a></li>
          <li><a href="/docs/">GGUF Model Loading Guide</a></li>
        </ul>
      </div>

      <!-- Col 3: Standard Libraries -->
      <div class="footer-links-col">
        <h4 class="footer-heading">Standard Packages</h4>
        <ul class="footer-links">
          <li><a href="/lib/#tztensor"><code>tztensor</code> (Tensors & Matrix)</a></li>
          <li><a href="/lib/#tzautodiff"><code>tzautodiff</code> (Autograd Tape)</a></li>
          <li><a href="/lib/#tzgguf"><code>tzgguf</code> (Q4_0/Q8_0 Engine)</a></li>
          <li><a href="/lib/#task"><code>task</code> (Async/Await Runtime)</a></li>
          <li><a href="/lib/#tezzui"><code>tezzui</code> (Native GUI Framework)</a></li>
          <li><a href="/lib/">View all 40+ packages &rarr;</a></li>
        </ul>
      </div>

      <!-- Col 4: Tooling & TezzCorp Cloud -->
      <div class="footer-links-col">
        <h4 class="footer-heading">Ecosystem & Download</h4>
        <ul class="footer-links">
          <li><a href="/download/TezzNativeInstaller.exe">Native GUI Installer (.exe)</a></li>
          <li><a href="/download/tezz_lsp.exe">LSP Daemon Binary (.exe)</a></li>
          <li><a href="/download/">Windows & Linux SDK Archives</a></li>
          <li><a href="https://tezzcorp.com" target="_blank" rel="noopener">TezzCorp Pvt Ltd.</a></li>
          <li><a href="/support.php">Developer Community & Support</a></li>
        </ul>
      </div>
    </div>

    <!-- Bottom Copyright & Status -->
    <div class="footer-bottom">
      <div class="footer-copy">
        &copy; <?= date('Y') ?> <strong>TezzCorp Pvt Ltd.</strong> Created by <strong>Rohit Pathak</strong>. All rights reserved.
      </div>
      <div class="footer-status">
        <span class="status-indicator" style="background: #ff9933; box-shadow: 0 0 8px #ff9933;"></span>
        <span>TezzNative v2.2.1 Production Operational</span>
      </div>
    </div>
  </div>
</footer>

</div> <!-- .site-wrapper -->

<!-- Scripts -->
<script src="/assets/app.js?v=2.2.2"></script>
</body>
</html>
