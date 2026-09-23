<?php
// index.php - TezzNative Homepage (Premier Programming Language Web Portal)
$page_title = "TezzNative - Ultra-Fast Native AI, Tensors & Systems Language | TezzCorp Pvt Ltd.";
$active_nav = "home";
require_once __DIR__ . '/includes/header.php';
require_once __DIR__ . '/includes/nav.php';
?>

<!-- Hero Section -->
<section class="hero-section">
  <div class="hero-glow"></div>
  <div class="hero-container">
    <!-- Left Hero Column -->
    <div class="hero-content">
      <div class="hero-pill-badge">
        <span class="pulse-dot" style="background: #ff9933; box-shadow: 0 0 10px #ff9933;"></span>
        <span><?= __('hero_badge', 'TEZZNATIVE v2.2.1 PRODUCTION RELEASE LIVE • TEZZCORP PVT LTD') ?></span>
      </div>

      <h1 class="hero-title">
        <?= __('hero_title_1', 'Ultra-Fast Systems.') ?><br>
        <?= __('hero_title_2', 'Native Deep Learning.') ?><br>
        <span class="text-gradient"><?= __('hero_title_3', 'Engineered for Hardware.') ?></span>
      </h1>

      <p class="hero-subtitle">
        <?= __('hero_subtitle', 'A premier systems and AI programming language created by <strong>Rohit Pathak</strong> at <strong>TezzCorp Pvt Ltd</strong>. Compiling directly to zero-overhead standalone native executables with first-class tensor arithmetic, multi-threaded <code>async/await</code>, and scoped memory safety without garbage collection pauses.') ?>
      </p>

      <!-- Hero Action Buttons -->
      <div class="hero-cta-group">
        <a href="/download/" class="btn btn-primary btn-lg" id="btnDownloadHero">
          <svg class="btn-icon" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round">
            <polyline points="4 17 10 11 4 5"></polyline>
            <line x1="12" y1="19" x2="20" y2="19"></line>
          </svg>
          <span><?= __('hero_btn_install', 'Install TezzNative') ?></span>
        </a>

        <a href="/docs/lsp" class="btn btn-secondary btn-lg">
          <svg class="btn-icon" width="19" height="19" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <polyline points="4 17 10 11 4 5"></polyline>
            <line x1="12" y1="19" x2="20" y2="19"></line>
          </svg>
          <span><?= __('hero_btn_lsp', 'LSP &amp; IDE Setup') ?></span>
        </a>

        <a href="/lib/" class="btn btn-outline btn-lg">
          <svg class="btn-icon" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"></path>
            <polyline points="3.27 6.96 12 12.01 20.73 6.96"></polyline>
            <line x1="12" y1="22.08" x2="12" y2="12"></line>
          </svg>
          <span><?= __('hero_btn_explore', 'Explore 40+ Packages') ?></span>
        </a>
      </div>

      <!-- Quick Stats Ribbon -->
      <div class="hero-stats-row">
        <div class="hero-stat-item">
          <span class="stat-value">0.9 ms</span>
          <span class="stat-label">Cold-Start Latency</span>
        </div>
        <div class="hero-stat-item">
          <span class="stat-value">140K+</span>
          <span class="stat-label">Req/sec Loopback HTTP</span>
        </div>
        <div class="hero-stat-item">
          <span class="stat-value">0 GC</span>
          <span class="stat-label">Deterministic Defer</span>
        </div>
        <div class="hero-stat-item">
          <span class="stat-value">40+</span>
          <span class="stat-label">Native Core Modules</span>
        </div>
      </div>
    </div>

    <!-- Right Hero Column: Interactive Quickstart Terminal -->
    <div class="hero-terminal-col">
      <div class="hero-terminal-card">
        <div class="terminal-header">
          <div class="terminal-dots">
            <span class="dot red"></span>
            <span class="dot yellow"></span>
            <span class="dot green"></span>
          </div>
          <span class="terminal-title">PowerShell / Command Prompt</span>
          <div style="width: 30px;"></div>
        </div>
        <div class="terminal-body">
          <div class="terminal-comment"># 1. Install via Windows PowerShell in one command:</div>
          <div class="terminal-cmd">
            <code>irm https://tezznative.org/install.ps1 | iex</code>
            <button class="copy-btn" title="Copy command" aria-label="Copy Command">
              <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
            </button>
          </div>

          <div class="terminal-comment"># 2. Or initialize and run via Tezz CLI:</div>
          <div class="terminal-cmd">
            <code>tezz init my-ai-project &amp;&amp; cd my-ai-project</code>
            <button class="copy-btn" title="Copy command" aria-label="Copy Command">
              <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
            </button>
          </div>

          <div class="terminal-comment"># 3. Add project libraries or auto-sync from imports:</div>
          <div class="terminal-cmd">
            <code>tezz add mind &amp;&amp; tezz sync</code>
            <button class="copy-btn" title="Copy command" aria-label="Copy Command">
              <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
            </button>
          </div>

          <div class="terminal-comment"># 4. Build standalone zero-overhead native binary:</div>
          <div class="terminal-cmd" style="margin-bottom: 0;">
            <code>tezz build --release</code>
            <button class="copy-btn" title="Copy command" aria-label="Copy Command">
              <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
            </button>
          </div>
        </div>
      </div>
    </div>
  </div>
</section>

<!-- Performance Benchmarks Section -->
<section class="section section-alt" id="benchmarks">
  <div class="section-container">
    <div class="section-header">
      <span class="section-pill">UNCOMPROMISING PERFORMANCE</span>
      <h2 class="section-title">Engineered for Raw Bare-Metal Speed</h2>
      <p class="section-desc">
        TezzNative compiles directly into native CPU machine instructions (AVX2, FMA, NEON) with zero garbage-collection pauses and zero interpreter overhead.
      </p>
    </div>

    <div class="benchmarks-grid">
      <!-- Benchmark 1: Matrix Multiplication -->
      <div class="benchmark-card">
        <div class="bench-header">
          <span class="bench-title">Matrix Multiply (1024&times;1024)</span>
          <span class="bench-metric">Lower is Better</span>
        </div>
        <div class="bench-row">
          <div class="bench-lang-info">
            <span class="bench-lang-name highlight">TezzNative (AVX2)</span>
            <span class="bench-val">32 ms</span>
          </div>
          <div class="bench-bar-track"><div class="bench-bar-fill tezz" style="width: 10%;"></div></div>
        </div>
        <div class="bench-row">
          <div class="bench-lang-info">
            <span class="bench-lang-name">C (GCC -O3)</span>
            <span class="bench-val">31 ms</span>
          </div>
          <div class="bench-bar-track"><div class="bench-bar-fill c" style="width: 10%;"></div></div>
        </div>
        <div class="bench-row">
          <div class="bench-lang-info">
            <span class="bench-lang-name">Rust (Release)</span>
            <span class="bench-val">33 ms</span>
          </div>
          <div class="bench-bar-track"><div class="bench-bar-fill rust" style="width: 11%;"></div></div>
        </div>
        <div class="bench-row">
          <div class="bench-lang-info">
            <span class="bench-lang-name">Python (NumPy C)</span>
            <span class="bench-val">58 ms</span>
          </div>
          <div class="bench-bar-track"><div class="bench-bar-fill python" style="width: 25%;"></div></div>
        </div>
      </div>

      <!-- Benchmark 2: HTTP Web Requests -->
      <div class="benchmark-card">
        <div class="bench-header">
          <span class="bench-title">HTTP Requests / Second</span>
          <span class="bench-metric">Higher is Better</span>
        </div>
        <div class="bench-row">
          <div class="bench-lang-info">
            <span class="bench-lang-name highlight">TezzNative (net)</span>
            <span class="bench-val">142,480 /s</span>
          </div>
          <div class="bench-bar-track"><div class="bench-bar-fill tezz" style="width: 92%;"></div></div>
        </div>
        <div class="bench-row">
          <div class="bench-lang-info">
            <span class="bench-lang-name">Rust (Actix)</span>
            <span class="bench-val">155,000 /s</span>
          </div>
          <div class="bench-bar-track"><div class="bench-bar-fill rust" style="width: 100%;"></div></div>
        </div>
        <div class="bench-row">
          <div class="bench-lang-info">
            <span class="bench-lang-name">Go (net/http)</span>
            <span class="bench-val">118,200 /s</span>
          </div>
          <div class="bench-bar-track"><div class="bench-bar-fill go" style="width: 76%;"></div></div>
        </div>
        <div class="bench-row">
          <div class="bench-lang-info">
            <span class="bench-lang-name">Python (FastAPI)</span>
            <span class="bench-val">12,500 /s</span>
          </div>
          <div class="bench-bar-track"><div class="bench-bar-fill python" style="width: 12%;"></div></div>
        </div>
      </div>

      <!-- Benchmark 3: Cold-Start Latency -->
      <div class="benchmark-card">
        <div class="bench-header">
          <span class="bench-title">Process Cold-Start Time</span>
          <span class="bench-metric">Lower is Better</span>
        </div>
        <div class="bench-row">
          <div class="bench-lang-info">
            <span class="bench-lang-name highlight">TezzNative</span>
            <span class="bench-val">0.9 ms</span>
          </div>
          <div class="bench-bar-track"><div class="bench-bar-fill tezz" style="width: 5%;"></div></div>
        </div>
        <div class="bench-row">
          <div class="bench-lang-info">
            <span class="bench-lang-name">C (Native PE)</span>
            <span class="bench-val">0.8 ms</span>
          </div>
          <div class="bench-bar-track"><div class="bench-bar-fill c" style="width: 4%;"></div></div>
        </div>
        <div class="bench-row">
          <div class="bench-lang-info">
            <span class="bench-lang-name">Go</span>
            <span class="bench-val">4.2 ms</span>
          </div>
          <div class="bench-bar-track"><div class="bench-bar-fill go" style="width: 16%;"></div></div>
        </div>
        <div class="bench-row">
          <div class="bench-lang-info">
            <span class="bench-lang-name">Python 3.12</span>
            <span class="bench-val">38.5 ms</span>
          </div>
          <div class="bench-bar-track"><div class="bench-bar-fill python" style="width: 95%;"></div></div>
        </div>
      </div>

      <!-- Benchmark 4: Memory at Idle -->
      <div class="benchmark-card">
        <div class="bench-header">
          <span class="bench-title">Idle Memory Footprint</span>
          <span class="bench-metric">Lower is Better</span>
        </div>
        <div class="bench-row">
          <div class="bench-lang-info">
            <span class="bench-lang-name highlight">TezzNative</span>
            <span class="bench-val">1.2 MB</span>
          </div>
          <div class="bench-bar-track"><div class="bench-bar-fill tezz" style="width: 8%;"></div></div>
        </div>
        <div class="bench-row">
          <div class="bench-lang-info">
            <span class="bench-lang-name">Rust</span>
            <span class="bench-val">1.8 MB</span>
          </div>
          <div class="bench-bar-track"><div class="bench-bar-fill rust" style="width: 12%;"></div></div>
        </div>
        <div class="bench-row">
          <div class="bench-lang-info">
            <span class="bench-lang-name">Go</span>
            <span class="bench-val">14.5 MB</span>
          </div>
          <div class="bench-bar-track"><div class="bench-bar-fill go" style="width: 60%;"></div></div>
        </div>
        <div class="bench-row">
          <div class="bench-lang-info">
            <span class="bench-lang-name">Python</span>
            <span class="bench-val">22.0 MB</span>
          </div>
          <div class="bench-bar-track"><div class="bench-bar-fill python" style="width: 90%;"></div></div>
        </div>
      </div>
    </div>
  </div>
</section>

<!-- Interactive Live Code Playground -->
<section class="section" id="playground">
  <div class="section-container">
    <div class="section-header">
      <span class="section-pill">LIVE CODE SHOWCASE</span>
      <h2 class="section-title">Experience the Syntax in Action</h2>
      <p class="section-desc">
        Explore interactive code examples showcasing native async coroutines, deep learning autograd, GGUF binary loading, and scoped memory cleanup.
      </p>
    </div>

    <div class="playground-wrapper">
      <!-- Tabs Navigation -->
      <div class="playground-nav-tabs">
        <button class="tab-btn active" data-tab="async">
          <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2"></polygon></svg>
          <span>1. Async / Await Coroutines</span>
        </button>
        <button class="tab-btn" data-tab="xor">
          <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"></circle><line x1="12" y1="8" x2="12" y2="12"></line><line x1="12" y1="16" x2="12.01" y2="16"></line></svg>
          <span>2. Infix @ &amp; Autodiff MLP</span>
        </button>
        <button class="tab-btn" data-tab="gguf">
          <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="4" y="4" width="16" height="16" rx="2" ry="2"></rect><rect x="9" y="9" width="6" height="6"></rect></svg>
          <span>3. GGUF Q4_0 Engine</span>
        </button>
        <button class="tab-btn" data-tab="defer_sugar">
          <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"></path></svg>
          <span>4. Scoped 'defer' &amp; 4D Slices</span>
        </button>
        <button class="tab-btn" data-tab="http_serve">
          <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"></circle><line x1="2" y1="12" x2="22" y2="12"></line><path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"></path></svg>
          <span>5. High-Throughput HTTP</span>
        </button>
      </div>

      <!-- Playground Content Pane -->
      <div class="playground-content-grid">
        <!-- Editor Left -->
        <div class="editor-pane">
          <pre class="code-view" id="codeView"><span class="cmt">// Native Async/Await Concurrency in TezzNative</span>
<span class="kw">import</span> <span class="str">"task"</span>
<span class="kw">import</span> <span class="str">"net"</span>

<span class="kw">async fn</span> <span class="fn">fetch_user_profile</span>(uid: <span class="ty">int</span>) -&gt; <span class="ty">int</span>:
  <span class="kw">let</span> client = net.http_client()
  <span class="kw">defer</span> client.close()
  <span class="kw">let</span> res = client.get(<span class="str">"https://api.tezzcorp.com/v1/user"</span>)
  <span class="kw">ret</span> res.status_code

<span class="kw">fn</span> <span class="fn">main</span>() -&gt; <span class="ty">int</span>:
  <span class="fn">say</span> <span class="str">"Spawning concurrent async worker tasks..."</span>
  <span class="kw">let</span> t1 = task.spawn_arg(fetch_user_profile, <span class="num">101</span>)
  <span class="kw">let</span> t2 = task.spawn_arg(fetch_user_profile, <span class="num">102</span>)

  <span class="cmt">// Native non-blocking join on event loop</span>
  <span class="kw">let</span> s1: <span class="ty">int</span> = <span class="kw">await</span> t1
  <span class="kw">let</span> s2: <span class="ty">int</span> = <span class="kw">await</span> t2

  <span class="fn">say</span> <span class="str">"Task 1 HTTP status:"</span>, s1
  <span class="fn">say</span> <span class="str">"Task 2 HTTP status:"</span>, s2
  <span class="kw">ret</span> <span class="num">0</span></pre>
        </div>

        <!-- Terminal Output Right -->
        <div class="run-pane">
          <div class="run-header">
            <span class="run-status">Executable Output (Native x64)</span>
            <span style="font-size: 0.76rem; color: #94a3b8; font-family: var(--font-mono);">Exit: 0</span>
          </div>
          <div class="terminal-output" id="terminalOutput">
            <div class="output-line">[tezzc] Compiling main.tn to standalone native x64 binary...</div>
            <div class="output-line">[tezzc] Native compilation finished in 4.2ms. Zero runtime overhead.</div>
            <div class="output-line">Spawning concurrent async worker tasks...</div>
            <div class="output-line">[Worker #1] HTTP GET https://api.tezzcorp.com/v1/user [uid=101] -&gt; 200 OK</div>
            <div class="output-line">[Worker #2] HTTP GET https://api.tezzcorp.com/v1/user [uid=102] -&gt; 200 OK</div>
            <div class="output-line success">Task 1 HTTP status: 200</div>
            <div class="output-line success">Task 2 HTTP status: 200</div>
            <div class="output-line perf">Execution time: 0.94ms | Peak RAM: 1.4 MB</div>
          </div>
        </div>
      </div>
    </div>
  </div>
</section>

<!-- Core Language Pillars (6 Feature Cards) -->
<section class="section section-alt" id="features">
  <div class="section-container">
    <div class="section-header">
      <span class="section-pill">CORE LANGUAGE PILLARS</span>
      <h2 class="section-title">Designed for the Next Decade of Computing</h2>
      <p class="section-desc">
        TezzNative eliminates the historic divide between rapid Python prototyping and low-level C systems programming.
      </p>
    </div>

    <div class="features-grid">
      <!-- Feature 1 -->
      <div class="feature-card">
        <div class="feature-icon-wrapper">
          <svg width="26" height="26" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"></path><polyline points="14 2 14 8 20 8"></polyline><line x1="16" y1="13" x2="8" y2="13"></line><line x1="16" y1="17" x2="8" y2="17"></line><polyline points="10 9 9 9 8 9"></polyline></svg>
        </div>
        <h3 class="feature-title">Pythonic Indentation &amp; Static Types</h3>
        <p class="feature-desc">
          Write elegant indentation-based code that feels effortless to read, while enjoying compile-time type safety, exhaustive pattern matching, and zero runtime type tags.
        </p>
        <div class="feature-code-snippet">
          <code>fn fib(n: int) -&gt; int:<br>&nbsp;&nbsp;if n &lt;= 1: ret n<br>&nbsp;&nbsp;ret fib(n-1) + fib(n-2)</code>
        </div>
      </div>

      <!-- Feature 2 -->
      <div class="feature-card">
        <div class="feature-icon-wrapper">
          <svg width="26" height="26" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"></circle><polyline points="12 6 12 12 16 14"></polyline></svg>
        </div>
        <h3 class="feature-title">Native Async / Await Coroutines</h3>
        <p class="feature-desc">
          Spawn hardware-threaded async workers and await asynchronous I/O and matrix compute non-blockingly on an integrated event loop with zero function-color baggage.
        </p>
        <div class="feature-code-snippet">
          <code>let task: AsyncTask = task.spawn_arg(worker, 42)<br>let result: int = await task</code>
        </div>
      </div>

      <!-- Feature 3 -->
      <div class="feature-card">
        <div class="feature-icon-wrapper">
          <svg width="26" height="26" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="4"></circle><path d="M16 8v5a3 3 0 0 0 6 0v-1a10 10 0 1 0-3.92 7.94"></path></svg>
        </div>
        <h3 class="feature-title">First-Class Matrix Ops &amp; Infix @</h3>
        <p class="feature-desc">
          Tensors are first-class language constructs. Execute matrix multiplications with the infix <code>@</code> operator, slice 4D tensor volumes seamlessly, and backpropagate with automatic differentiation.
        </p>
        <div class="feature-code-snippet">
          <code>let H = (X @ W1).relu()<br>let slice = tensor[0, 1, 10:20, 10:20]</code>
        </div>
      </div>

      <!-- Feature 4 -->
      <div class="feature-card">
        <div class="feature-icon-wrapper">
          <svg width="26" height="26" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"></path></svg>
        </div>
        <h3 class="feature-title">Deterministic Scoped 'defer'</h3>
        <p class="feature-desc">
          Resource handles, sockets, files, and CUDA contexts are automatically released in LIFO reverse order upon scope exit. Zero garbage collection pauses, zero memory leaks.
        </p>
        <div class="feature-code-snippet">
          <code>let f = io.open("data.bin", "rb")<br>defer f.close() // Executed on exit</code>
        </div>
      </div>

      <!-- Feature 5 -->
      <div class="feature-card">
        <div class="feature-icon-wrapper">
          <svg width="26" height="26" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="4" y="4" width="16" height="16" rx="2" ry="2"></rect><rect x="9" y="9" width="6" height="6"></rect><line x1="9" y1="1" x2="9" y2="4"></line><line x1="15" y1="1" x2="15" y2="4"></line><line x1="9" y1="20" x2="9" y2="23"></line><line x1="15" y1="20" x2="15" y2="23"></line><line x1="20" y1="9" x2="23" y2="9"></line><line x1="20" y1="14" x2="23" y2="14"></line><line x1="1" y1="9" x2="4" y2="9"></line><line x1="1" y1="14" x2="4" y2="14"></line></svg>
        </div>
        <h3 class="feature-title">Zero-Dependency Native Binaries</h3>
        <p class="feature-desc">
          Compile single standalone Windows PE <code>.exe</code> or Linux ELF binaries that require zero runtime installs, zero dynamic interpreters, and start in under 1 millisecond.
        </p>
        <div class="feature-code-snippet">
          <code>tezzc buildexe main.tn app.exe<br># Outputs standalone native binary</code>
        </div>
      </div>

      <!-- Feature 6 -->
      <div class="feature-card">
        <div class="feature-icon-wrapper">
          <svg width="26" height="26" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="16 18 22 12 16 6"></polyline><polyline points="8 6 2 12 8 18"></polyline></svg>
        </div>
        <h3 class="feature-title">Native Language Server (LSP)</h3>
        <p class="feature-desc">
          Official LSP v3.17 daemon (<code>tezz_lsp.exe</code>) brings hover documentation, Go to Definition, auto-completion, and real-time syntax checking to VS Code, Neovim, Zed, and Cursor.
        </p>
        <div class="feature-code-snippet">
          <code>tezzc lsp # Starts standard stdio JSON-RPC daemon</code>
        </div>
      </div>
    </div>
  </div>
</section>

<!-- LSP & IDE Integration Showcase -->
<section class="section" id="lsp-showcase">
  <div class="section-container">
    <div class="lsp-container">
      <div>
        <span class="section-pill">DEVELOPER EXPERIENCE</span>
        <h2 class="section-title">Zero-Config IDE Integration</h2>
        <p class="section-desc" style="margin-bottom: 24px;">
          TezzNative includes a first-party Language Server Protocol daemon (<code>tezz_lsp</code>) built directly in TezzNative. Enjoy rich IDE features with zero configuration across your favorite code editor.
        </p>
        <div style="display: flex; gap: 12px; flex-wrap: wrap; margin-bottom: 30px;">
          <span class="f-badge" style="padding: 6px 12px; font-size: 0.85rem;">VS Code</span>
          <span class="f-badge" style="padding: 6px 12px; font-size: 0.85rem;">Neovim / Lua</span>
          <span class="f-badge" style="padding: 6px 12px; font-size: 0.85rem;">Zed Editor</span>
          <span class="f-badge" style="padding: 6px 12px; font-size: 0.85rem;">Cursor AI</span>
          <span class="f-badge" style="padding: 6px 12px; font-size: 0.85rem;">Sublime Text</span>
        </div>
        <a href="/docs/lsp" class="btn btn-primary">
          <span>View LSP Configuration Guide &rarr;</span>
        </a>
      </div>

      <!-- IDE Visual Mockup -->
      <div class="ide-mockup">
        <div class="ide-header">
          <div class="terminal-dots">
            <span class="dot red"></span>
            <span class="dot yellow"></span>
            <span class="dot green"></span>
          </div>
          <div class="ide-tabs">
            <span class="ide-tab">neural_net.tn</span>
          </div>
        </div>
        <div class="ide-editor-body">
          <pre><code><span class="kw">import</span> <span class="str">"tztensor"</span>
<span class="kw">import</span> <span class="str">"tzautodiff"</span>

<span class="kw">fn</span> <span class="fn">forward_pass</span>(x: <span class="ty">Tensor</span>, w: <span class="ty">Tensor</span>):
  <span class="kw">let</span> <span style="text-decoration: underline wavy #38bdf8; text-underline-offset: 4px;">y_hat</span> = (x @ w).relu()
  <span class="kw">ret</span> y_hat</code></pre>
          
          <!-- Mock Hover Tooltip -->
          <div class="lsp-hover-tooltip">
            <div class="tooltip-type">Tensor::relu(&self) -&gt; Tensor</div>
            <div class="tooltip-doc">Applies elementwise Rectified Linear Unit activation. Zero-copy AVX2 SIMD pass.</div>
          </div>
        </div>
      </div>
    </div>
  </div>
</section>

<!-- Call to Action Banner -->
<section class="section section-alt" style="text-align: center; padding: 70px 24px;">
  <div class="section-container" style="max-width: 800px;">
    <h2 class="section-title" style="font-size: 2.6rem;">Start Building with TezzNative Today</h2>
    <p class="section-desc" style="margin-bottom: 34px;">
      Join developers creating high-throughput AI backends, native desktop GUIs, and zero-overhead systems software.
    </p>
    <div style="display: flex; justify-content: center; gap: 16px; flex-wrap: wrap;">
      <a href="/download/" class="btn btn-primary btn-lg">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="4 17 10 11 4 5"></polyline><line x1="12" y1="19" x2="20" y2="19"></line></svg>
        <span><?= __('hero_btn_install', 'Install TezzNative') ?></span>
      </a>
      <a href="/docs/" class="btn btn-secondary btn-lg">
        <span>Read the Documentation</span>
      </a>
    </div>
  </div>
</section>

<?php require_once __DIR__ . '/includes/footer.php'; ?>
