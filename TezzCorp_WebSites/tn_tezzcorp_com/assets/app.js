/**
 * assets/app.js — TezzNative Web Portal (Production JS)
 * Engineered for TezzCorp Pvt Ltd. | Created by Rohit Pathak
 * Version: 2.2.1 | 100% Pure Vanilla JS — Zero External Dependencies
 */

(function () {
  'use strict';

  // ── 1. Theme Management (Light / Dark Mode) ──────────────────────────────────
  function initTheme() {
    var root = document.documentElement;
    var stored = localStorage.getItem('tn_theme') || 'dark';
    root.classList.toggle('light', stored === 'light');
    root.classList.toggle('dark',  stored !== 'light');

    var btn = document.getElementById('themeToggle');
    if (!btn) return;
    btn.addEventListener('click', function (e) {
      e.preventDefault();
      var isLight = root.classList.contains('light');
      root.classList.toggle('light', !isLight);
      root.classList.toggle('dark',   isLight);
      localStorage.setItem('tn_theme', isLight ? 'dark' : 'light');
    });
  }

  // ── 2. Mobile Navigation & Off-Canvas Sidebar ────────────────────────────────
  function initMobileNav() {
    var toggleBtn  = document.getElementById('mobileToggle');
    var sidebar    = document.getElementById('mobileSidebar');
    var overlay    = document.getElementById('sidebarOverlay');
    var closeBtn   = document.getElementById('sidebarClose');
    var navLinks   = document.querySelectorAll('.sidebar-link, .sidebar-btn-install');

    if (!toggleBtn || !sidebar) return;

    function openSidebar() {
      sidebar.classList.add('open');
      if (overlay) overlay.classList.add('active');
      toggleBtn.setAttribute('aria-expanded', 'true');
      sidebar.setAttribute('aria-hidden', 'false');
      if (overlay) overlay.setAttribute('aria-hidden', 'false');
      document.body.classList.add('sidebar-locked');
    }

    function closeSidebar() {
      sidebar.classList.remove('open');
      if (overlay) overlay.classList.remove('active');
      toggleBtn.setAttribute('aria-expanded', 'false');
      sidebar.setAttribute('aria-hidden', 'true');
      if (overlay) overlay.setAttribute('aria-hidden', 'true');
      document.body.classList.remove('sidebar-locked');
    }

    toggleBtn.addEventListener('click', function (e) {
      e.preventDefault();
      e.stopPropagation();
      sidebar.classList.contains('open') ? closeSidebar() : openSidebar();
    });

    if (closeBtn) {
      closeBtn.addEventListener('click', function (e) {
        e.preventDefault();
        closeSidebar();
      });
    }

    if (overlay) {
      overlay.addEventListener('click', closeSidebar);
    }

    document.addEventListener('keydown', function (e) {
      if (e.key === 'Escape' && sidebar.classList.contains('open')) {
        closeSidebar();
      }
    });

    navLinks.forEach(function (link) {
      link.addEventListener('click', function () {
        closeSidebar();
      });
    });
  }

  // ── 3. Sticky Header Shadow on Scroll ────────────────────────────────────────
  function initHeaderScroll() {
    var header = document.getElementById('mainHeader') || document.querySelector('.main-header');
    if (!header) return;
    function onScroll() {
      if (window.scrollY > 20) {
        header.style.boxShadow = '0 4px 24px rgba(0,0,0,.45)';
      } else {
        header.style.boxShadow = '';
      }
    }
    window.addEventListener('scroll', onScroll, { passive: true });
    onScroll();
  }

  // ── 4. Copy-to-Clipboard ──────────────────────────────────────────────────────
  function initCopyButtons() {
    document.addEventListener('click', function (e) {
      var btn = e.target.closest('.copy-btn, .btn-copy, [data-copy]');
      if (!btn) return;
      e.preventDefault();

      var text = btn.getAttribute('data-copy');
      if (!text) {
        var code = btn.parentElement ? btn.parentElement.querySelector('code, pre') : null;
        if (code) text = code.textContent;
      }

      if (!text || !navigator.clipboard) return;

      navigator.clipboard.writeText(text.trim()).then(function () {
        var orig = btn.innerHTML;
        btn.innerHTML = '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="#10b981" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"/></svg>';
        btn.style.color = '#10b981';
        setTimeout(function () { btn.innerHTML = orig; btn.style.color = ''; }, 2000);
      }).catch(function (err) { console.warn('Copy failed:', err); });
    });
  }

  // ── 5. Code Playground Tab Switcher ─────────────────────────────────────────
  var codeDemos = {
    async: {
      title: "Native Async / Await Coroutines",
      code:
`// Native Async/Await Concurrency in TezzNative
import "task"
import "net"

async fn fetch_user_profile(uid: int) -> int:
  let client = net.http_client()
  defer client.close()
  let res = client.get("https://api.tezzcorp.com/v1/user")
  ret res.status_code

fn main() -> int:
  say "Spawning concurrent async worker tasks..."
  let t1 = task.spawn_arg(fetch_user_profile, 101)
  let t2 = task.spawn_arg(fetch_user_profile, 102)

  // Native non-blocking join on event loop
  let s1: int = await t1
  let s2: int = await t2

  say "Task 1 HTTP status:", s1
  say "Task 2 HTTP status:", s2
  ret 0`,
      output: [
        { text: "[tezzc] Compiling async_demo.tn → x86-64 native PE...", cls: "muted" },
        { text: "[tezzc] Binary generated: async_demo.exe (18.4 KB) in 4.1ms", cls: "muted" },
        { text: "Spawning concurrent async worker tasks...", cls: "" },
        { text: "[Worker #1] HTTP GET https://api.tezzcorp.com/v1/user → 200 OK", cls: "" },
        { text: "[Worker #2] HTTP GET https://api.tezzcorp.com/v1/user → 200 OK", cls: "" },
        { text: "Task 1 HTTP status: 200", cls: "success" },
        { text: "Task 2 HTTP status: 200", cls: "success" },
        { text: "[process exited code 0 in 1.4ms | Peak RAM: 1.4 MB]", cls: "perf" }
      ]
    },
    xor: {
      title: "Infix @ Matrix Multiply & Autodiff MLP",
      code:
`// Infix @ Operator + Autograd MLP in TezzNative
import "tztensor"
import "tzautodiff"

fn main() -> int:
  // Xavier-initialized weight matrices
  let W1 = tztensor.xavier_uniform(784, 256)
  let W2 = tztensor.xavier_uniform(256, 10)
  let X  = tztensor.rand_normal(32, 784)   // batch of 32

  // Forward pass: X @ W1 → ReLU → @ W2 → Softmax
  let H    = (X @ W1).relu()
  let logits = H @ W2

  // Autograd backward pass
  let ad = tzautodiff.tape()
  let loss = tzautodiff.cross_entropy(ad, logits, tztensor.rand_labels(32, 10))
  tzautodiff.backward(ad, loss)

  say "Loss:", tzautodiff.item(loss)
  say "W1 grad norm:", tztensor.norm(tzautodiff.grad(ad, W1))
  ret 0`,
      output: [
        { text: "[tezzc] Compiling mlp_demo.tn → x86-64 native (AVX2)...", cls: "muted" },
        { text: "[tezzc] Binary generated: mlp_demo.exe (22.8 KB) in 3.7ms", cls: "muted" },
        { text: "Loss: 2.3026", cls: "success" },
        { text: "W1 grad norm: 0.0412", cls: "success" },
        { text: "[process exited code 0 in 0.8ms | Peak RAM: 3.2 MB]", cls: "perf" }
      ]
    },
    gguf: {
      title: "GGUF Q4_0 LLM Engine",
      code:
`// Direct GGUF Q4_0 Model Loading — No Python Needed
import "tzgguf"
import "tokenizer"

fn main() -> int:
  say "Loading TinyLlama-1.1B Q4_0 quantized weights..."
  let model = tzgguf.load_model("tinyllama-1.1b-q4_0.gguf")
  defer tzgguf.free_model(model)

  let tok = tokenizer.load_bpe("tokenizer.model")
  defer tokenizer.free(tok)

  let tokens = tokenizer.encode(tok, "TezzNative is engineered for")
  say "Prompt tokens:", tokens.len

  // CPU gemv kernel with AVX2 Q4_0 dequant
  let out = tzgguf.generate_next_token(model, tokens)
  say "Generated:", tokenizer.decode_single(tok, out)
  ret 0`,
      output: [
        { text: "[tezzc] Compiling gguf_demo.tn → x86-64 native...", cls: "muted" },
        { text: "[tezzc] Binary generated: gguf_demo.exe (31.2 KB) in 5.2ms", cls: "muted" },
        { text: "Loading TinyLlama-1.1B Q4_0 quantized weights...", cls: "" },
        { text: "Model metadata: arch=llama n_layers=22 q_type=Q4_0", cls: "" },
        { text: "Prompt tokens: 6", cls: "" },
        { text: "Generated: ' ultra-fast native AI inference.'", cls: "success" },
        { text: "[process exited code 0 in 4.2ms | Peak RAM: 680 MB]", cls: "perf" }
      ]
    },
    defer_sugar: {
      title: "Scoped 'defer' & 4D Tensor Slices",
      code:
`// Deterministic Scoped Defer + 4D Tensor Indexing
import "io"
import "tztensor"

fn process_batch(path: str) -> int:
  let f = io.open(path, "rb")
  defer f.close()       // auto-released on scope exit

  let data = io.read_all(f)
  // Reshape raw bytes into 4D tensor: [batch, channels, H, W]
  let t = tztensor.from_bytes_4d(data, 4, 3, 64, 64)

  // Slice: batch[0], all channels, rows 10-20, cols 10-20
  let patch = t[0, :, 10:20, 10:20]
  say "Patch shape:", tztensor.shape_str(patch)

  // RMSNorm across channel dim
  let normed = tztensor.rmsnorm(patch, 1e-5)
  say "Norm mean:", tztensor.mean(normed)
  ret 0

fn main() -> int:
  ret process_batch("batch_raw.bin")`,
      output: [
        { text: "[tezzc] Compiling defer_demo.tn → x86-64 native...", cls: "muted" },
        { text: "[tezzc] Binary generated: defer_demo.exe (20.1 KB) in 3.9ms", cls: "muted" },
        { text: "Patch shape: [3, 10, 10]", cls: "success" },
        { text: "Norm mean: 0.000142", cls: "success" },
        { text: "[defer] f.close() executed on scope exit", cls: "muted" },
        { text: "[process exited code 0 in 0.7ms | Peak RAM: 1.1 MB]", cls: "perf" }
      ]
    },
    http_serve: {
      title: "High-Throughput HTTP Server",
      code:
`// 140K+ req/s Async HTTP Server in TezzNative
import "net"
import "task"

async fn handle(ctx: net.Context) -> int:
  let body = "Hello from TezzNative native HTTP!"
  ctx.set_header("Content-Type", "text/plain; charset=utf-8")
  ctx.set_header("X-Powered-By", "TezzNative/2.2.1")
  ctx.respond(200, body)
  ret 0

fn main() -> int:
  let srv = net.http_server("0.0.0.0", 8080)
  srv.route("GET", "/", handle)
  srv.route("GET", "/health", async fn(ctx) -> int:
    ctx.respond(200, "{\"status\":\"ok\"}")
    ret 0
  )
  say "TezzNative HTTP server listening on :8080"
  net.serve(srv)
  ret 0`,
      output: [
        { text: "[tezzc] Compiling http_server.tn → x86-64 native...", cls: "muted" },
        { text: "[tezzc] Binary generated: http_server.exe (24.3 KB) in 4.8ms", cls: "muted" },
        { text: "TezzNative HTTP server listening on :8080", cls: "success" },
        { text: "[bench] wrk -t12 -c400 -d30s → 142,480 req/s", cls: "perf" },
        { text: "[bench] Latency: avg 2.8ms  p99 5.1ms  p999 12ms", cls: "perf" },
        { text: "[bench] Throughput: 9.87 GB/s | 0 errors | 0 timeouts", cls: "success" }
      ]
    }
  };

  function initPlayground() {
    // Supports both .tab-btn (index.php) and .playground-tab (legacy)
    var tabs    = document.querySelectorAll('.tab-btn, .playground-tab');
    var codeView  = document.getElementById('codeView');
    var termOut   = document.getElementById('terminalOutput');
    var runBtn    = document.getElementById('playgroundRunBtn');

    if (!codeView && !termOut) return; // Not on homepage

    function escHtml(str) {
      return str.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
    }

    function syntaxHL(code) {
      // Lightweight TezzNative syntax highlighter
      return escHtml(code)
        .replace(/\b(import|fn|async|let|ret|defer|await|if|else|while|for|in)\b/g,
          '<span class="kw">$1</span>')
        .replace(/"([^"]*)"/g, '<span class="str">"$1"</span>')
        .replace(/\b(\d+(?:\.\d+)?)\b/g, '<span class="num">$1</span>')
        .replace(/(\/\/[^\n]*)/g, '<span class="cmt">$1</span>');
    }

    function renderDemo(key) {
      var demo = codeDemos[key] || codeDemos.async;
      if (codeView) codeView.innerHTML = syntaxHL(demo.code);
      if (termOut) {
        termOut.innerHTML = demo.output.map(function(line) {
          var cls = '';
          if (line.cls === 'muted')   cls = 'style="color:#64748b"';
          if (line.cls === 'success') cls = 'style="color:#10b981"';
          if (line.cls === 'perf')    cls = 'style="color:#ff9933"';
          return '<div class="output-line" ' + cls + '>' + escHtml(line.text) + '</div>';
        }).join('');
      }
    }

    // Tab switching
    tabs.forEach(function (tab) {
      tab.addEventListener('click', function () {
        tabs.forEach(function (t) { t.classList.remove('active'); });
        tab.classList.add('active');
        var key = tab.getAttribute('data-tab') || tab.getAttribute('data-demo') || 'async';
        renderDemo(key);
      });
    });

    // Run button animation
    if (runBtn) {
      runBtn.addEventListener('click', function () {
        var activeTab = document.querySelector('.tab-btn.active, .playground-tab.active');
        var key = activeTab ? (activeTab.getAttribute('data-tab') || activeTab.getAttribute('data-demo') || 'async') : 'async';
        if (termOut) {
          termOut.innerHTML = '<div class="output-line" style="color:#ff9933">[tezzc] Compiling and linking native binary...</div>';
          setTimeout(function () { renderDemo(key); }, 400);
        }
      });
    }

    // Render initial (first) demo
    var firstActive = document.querySelector('.tab-btn.active, .playground-tab.active');
    var initKey = firstActive ? (firstActive.getAttribute('data-tab') || firstActive.getAttribute('data-demo') || 'async') : 'async';
    renderDemo(initKey);
  }

  // ── 6. Benchmark Bar Scroll Animations ───────────────────────────────────────
  function initBenchmarkAnims() {
    var bars = document.querySelectorAll('.bench-bar-fill');
    if (!bars.length) return;

    // Store target widths, reset to 0
    var targets = [];
    bars.forEach(function (bar) {
      targets.push(bar.style.width || '0%');
      bar.style.width = '0%';
      bar.style.transition = 'width 1s cubic-bezier(0.16,1,0.3,1)';
    });

    if ('IntersectionObserver' in window) {
      var obs = new IntersectionObserver(function (entries) {
        entries.forEach(function (entry) {
          if (!entry.isIntersecting) return;
          bars.forEach(function (bar, i) {
            setTimeout(function () { bar.style.width = targets[i]; }, i * 60);
          });
          obs.disconnect();
        });
      }, { threshold: 0.25 });

      var section = document.getElementById('benchmarks') || document.querySelector('.benchmarks-grid');
      if (section) obs.observe(section);
    } else {
      // Fallback: animate immediately
      bars.forEach(function (bar, i) {
        setTimeout(function () { bar.style.width = targets[i]; }, i * 60);
      });
    }
  }

  // ── 7. Feature Card Entrance Animations ──────────────────────────────────────
  function initCardAnims() {
    var cards = document.querySelectorAll('.feature-card, .benchmark-card');
    if (!cards.length || !('IntersectionObserver' in window)) return;

    cards.forEach(function (card) {
      card.style.opacity = '0';
      card.style.transform = 'translateY(20px)';
      card.style.transition = 'opacity .5s ease, transform .5s ease';
    });

    var obs = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (!entry.isIntersecting) return;
        entry.target.style.opacity = '1';
        entry.target.style.transform = 'translateY(0)';
        obs.unobserve(entry.target);
      });
    }, { threshold: 0.1 });

    cards.forEach(function (card) { obs.observe(card); });
  }

  // ── 8. Smooth Anchor Scroll ───────────────────────────────────────────────────
  function initSmoothScroll() {
    document.querySelectorAll('a[href^="#"]').forEach(function (a) {
      a.addEventListener('click', function (e) {
        var href = a.getAttribute('href');
        if (href.length > 1) {
          var target = document.querySelector(href);
          if (target) {
            e.preventDefault();
            target.scrollIntoView({ behavior: 'smooth', block: 'start' });
          }
        }
      });
    });
  }

  // ── 9. Active Nav Link (scroll spy) ──────────────────────────────────────────
  function initScrollSpy() {
    var sections = document.querySelectorAll('section[id]');
    var navLinks = document.querySelectorAll('.nav-link');
    if (!sections.length || !navLinks.length) return;

    var obs = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (!entry.isIntersecting) return;
        var id = entry.target.id;
        navLinks.forEach(function (link) {
          var href = link.getAttribute('href') || '';
          link.classList.toggle('active', href.endsWith('#' + id));
        });
      });
    }, { threshold: 0.4 });

    sections.forEach(function (s) { obs.observe(s); });
  }

  // ── 10. Library / Package Registry Filter ────────────────────────────────────
  function initLibFilter() {
    var searchInput = document.getElementById('pkgSearchInput');
    var chipBtns    = document.querySelectorAll('.chip-btn');
    var cards       = document.querySelectorAll('.package-card');

    if (!cards.length) return;

    var activeCat = 'all';

    function filterCards() {
      var query = searchInput ? searchInput.value.trim().toLowerCase() : '';
      var visible = 0;
      cards.forEach(function (card) {
        var name = (card.getAttribute('data-name') || '').toLowerCase();
        var cat  = (card.getAttribute('data-cat')  || '').toLowerCase();
        var desc = (card.querySelector('.pkg-desc')  ? card.querySelector('.pkg-desc').textContent  : '').toLowerCase();
        var matchCat   = (activeCat === 'all' || cat === activeCat);
        var matchQuery = (!query || name.indexOf(query) !== -1 || desc.indexOf(query) !== -1 || cat.indexOf(query) !== -1);
        var show = matchCat && matchQuery;
        card.style.display = show ? '' : 'none';
        if (show) visible++;
      });

      // Show "no results" message if needed
      var noResult = document.getElementById('pkgNoResult');
      if (noResult) {
        noResult.style.display = visible === 0 ? 'block' : 'none';
      }
    }

    // Chip button clicks
    chipBtns.forEach(function (btn) {
      btn.addEventListener('click', function () {
        chipBtns.forEach(function (b) { b.classList.remove('active'); });
        btn.classList.add('active');
        activeCat = (btn.getAttribute('data-cat') || 'all').toLowerCase();
        filterCards();
      });
    });

    // Search input
    if (searchInput) {
      searchInput.addEventListener('input', filterCards);
      searchInput.addEventListener('search', filterCards);
    }

    // Initial render
    filterCards();
  }

  // ── 11. Auto-inject Copy Buttons on all <pre><code> blocks ───────────────────
  function initCodeBlockCopyButtons() {
    var pres = document.querySelectorAll('pre:not(.has-copy-btn)');
    pres.forEach(function (pre) {
      var code = pre.querySelector('code');
      if (!code) return;

      // Make pre relative for positioning
      pre.style.position = 'relative';
      pre.classList.add('has-copy-btn');

      var btn = document.createElement('button');
      btn.className = 'code-copy-btn copy-btn';
      btn.setAttribute('title', 'Copy code');
      btn.setAttribute('aria-label', 'Copy code to clipboard');
      btn.setAttribute('data-copy', code.textContent || '');
      btn.innerHTML = '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>';
      pre.appendChild(btn);
    });
  }

  // ── Bootstrap ────────────────────────────────────────────────────────────────
  function boot() {
    initTheme();
    initMobileNav();
    initHeaderScroll();
    initCopyButtons();
    initCodeBlockCopyButtons();
    initPlayground();
    initBenchmarkAnims();
    initCardAnims();
    initSmoothScroll();
    initScrollSpy();
    initLibFilter();
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', boot);
  } else {
    boot();
  }

})();
