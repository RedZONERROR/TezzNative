<?php
declare(strict_types=1);
require_once __DIR__ . '/_common.php';
tn_doc_boot('/docs/getting-started', 'getting-started');
tn_head('Getting Started with TezzNative SDK v2.2', 'Complete quick start guide to installing TezzNative SDK, setting up IDE extensions, and compiling your first standalone native AI program.', 'docs', '/docs/getting-started');
tn_page_shell_start('Getting Started with TezzNative', 'Install SDK, Configure IDE, and Build Native Apps', 'Everything you need to set up TezzNative v2.2.1 on Windows or Linux in under 60 seconds.');
?>

<div style="display: flex; flex-direction: column; gap: 36px; max-width: 960px; margin: 30px auto 0;">

  <!-- Step 1: Installation -->
  <article class="glass-panel" style="padding: 28px; border-radius: var(--radius-lg);">
    <div style="display: flex; align-items: center; gap: 12px; margin-bottom: 16px;">
      <span style="background: var(--brand-gradient); color: #fff; font-weight: 800; width: 32px; height: 32px; display: flex; align-items: center; justify-content: center; border-radius: 50%;">1</span>
      <h2 style="font-size: 1.4rem; color: var(--text-primary); margin: 0;">Installation (Windows &amp; Linux)</h2>
    </div>
    <p style="color: var(--text-secondary); line-height: 1.6; margin-bottom: 16px;">
      Choose your preferred installation method. Both options automatically configure the native compiler (<code>tezzc</code>), command runner (<code>tezz</code>), and standard library packages (40+).
    </p>

    <!-- Tabs / Option Grid -->
    <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 16px;">
      <div style="background: var(--code-bg); border: 1px solid var(--border-medium); border-radius: var(--radius-md); padding: 18px;">
        <h4 style="color: var(--brand-saffron); margin-bottom: 8px;">Windows (PowerShell 1-Liner)</h4>
        <p style="font-size: 0.85rem; color: var(--text-secondary); margin-bottom: 12px;">Fastest automated user install. No admin required.</p>
        <pre><code style="font-family: var(--font-mono); font-size: 0.85rem; color: #10b981;">irm https://tn.tezzcorp.com/download/install.ps1 | iex</code></pre>
      </div>

      <div style="background: var(--code-bg); border: 1px solid var(--border-medium); border-radius: var(--radius-md); padding: 18px;">
        <h4 style="color: var(--brand-saffron); margin-bottom: 8px;">Windows Native GUI Installer</h4>
        <p style="font-size: 0.85rem; color: var(--text-secondary); margin-bottom: 12px;">Visual setup wizard with directory picker &amp; PATH setup.</p>
        <a href="/download/TezzNativeInstaller.exe" class="btn btn-primary btn-sm" style="margin-top: 4px;">
          Download TezzNativeInstaller.exe
        </a>
      </div>

      <div style="background: var(--code-bg); border: 1px solid var(--border-medium); border-radius: var(--radius-md); padding: 18px; grid-column: 1 / -1;">
        <h4 style="color: var(--brand-saffron); margin-bottom: 8px;">Linux &amp; macOS (Bash Installer)</h4>
        <pre><code style="font-family: var(--font-mono); font-size: 0.85rem; color: #10b981;">curl -fsSL https://tn.tezzcorp.com/download/install.sh | bash</code></pre>
      </div>
    </div>
  </article>

  <!-- Step 2: Verification -->
  <article class="glass-panel" style="padding: 28px; border-radius: var(--radius-lg);">
    <div style="display: flex; align-items: center; gap: 12px; margin-bottom: 16px;">
      <span style="background: var(--brand-gradient); color: #fff; font-weight: 800; width: 32px; height: 32px; display: flex; align-items: center; justify-content: center; border-radius: 50%;">2</span>
      <h2 style="font-size: 1.4rem; color: var(--text-primary); margin: 0;">Verify Installation</h2>
    </div>
    <p style="color: var(--text-secondary); line-height: 1.6; margin-bottom: 16px;">
      Open a new terminal and verify the compiler and CLI driver:
    </p>
    <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-md); padding: 18px;">
      <pre><code style="font-family: var(--font-mono); font-size: 0.88rem; color: var(--text-primary);"><span style="color:#64748b;"># Check version</span>
tezz --version
<span style="color:#64748b;"># Output: TezzNative v2.2.1 (x86_64 Windows) | TezzCorp Pvt Ltd. | Created by Rohit Pathak</span>

<span style="color:#64748b;"># Run diagnostic health check</span>
tezz doctor</code></pre>
    </div>
  </article>

  <!-- Step 3: Your First Program -->
  <article class="glass-panel" style="padding: 28px; border-radius: var(--radius-lg);">
    <div style="display: flex; align-items: center; gap: 12px; margin-bottom: 16px;">
      <span style="background: var(--brand-gradient); color: #fff; font-weight: 800; width: 32px; height: 32px; display: flex; align-items: center; justify-content: center; border-radius: 50%;">3</span>
      <h2 style="font-size: 1.4rem; color: var(--text-primary); margin: 0;">Write Your First Program</h2>
    </div>
    <p style="color: var(--text-secondary); line-height: 1.6; margin-bottom: 16px;">
      Create a file named <code>app.tn</code>:
    </p>
    <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-md); padding: 18px; margin-bottom: 16px;">
      <pre><code style="font-family: var(--font-mono); font-size: 0.88rem; color: var(--text-primary);"><span style="color:#f43f5e;">import</span> <span style="color:#10b981;">"io"</span>
<span style="color:#f43f5e;">import</span> <span style="color:#10b981;">"tts"</span>

<span style="color:#f43f5e;">fn</span> <span style="color:#ff9933;">main</span>() -&gt; <span style="color:#38bdf8;">int</span>:
  <span style="color:#ff9933;">say</span> <span style="color:#10b981;">"Hello from TezzNative v2.2.1!"</span>
  <span style="color:#ff9933;">say</span> <span style="color:#10b981;">"Engineered by TezzCorp Pvt Ltd. | Created by Rohit Pathak"</span>

  <span style="color:#64748b;">// Native speech synthesis</span>
  <span style="color:#f43f5e;">let</span> eng = tts.tts_new()
  tts.tts_speak(eng, <span style="color:#10b981;">"Hello world from TezzNative native AI language!"</span>)
  tts.tts_free(eng)
  <span style="color:#f43f5e;">ret</span> <span style="color:#fbbf24;">0</span></code></pre>
    </div>

    <p style="color: var(--text-secondary); line-height: 1.6; margin-bottom: 12px;">
      Run it immediately using JIT / bytecode or compile directly to a zero-overhead standalone binary:
    </p>
    <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-md); padding: 18px;">
      <pre><code style="font-family: var(--font-mono); font-size: 0.88rem; color: var(--text-primary);"><span style="color:#64748b;"># Run directly</span>
tezz run app.tn

<span style="color:#64748b;"># Compile to standalone native Windows executable</span>
tezzc buildexe app.tn app.exe
.\app.exe</code></pre>
    </div>
  </article>

  <!-- Step 4: Next Steps -->
  <article class="glass-panel" style="padding: 28px; border-radius: var(--radius-lg);">
    <div style="display: flex; align-items: center; gap: 12px; margin-bottom: 16px;">
      <span style="background: var(--brand-gradient); color: #fff; font-weight: 800; width: 32px; height: 32px; display: flex; align-items: center; justify-content: center; border-radius: 50%;">4</span>
      <h2 style="font-size: 1.4rem; color: var(--text-primary); margin: 0;">Explore Advanced Features</h2>
    </div>
    <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(260px, 1fr)); gap: 16px;">
      <a href="/docs/language" class="feature-card" style="text-decoration: none; padding: 16px; border: 1px solid var(--border-subtle); border-radius: var(--radius-md); background: var(--bg-surface-raised);">
        <h4 style="color: var(--brand-saffron); margin-bottom: 6px;">Language Syntax Tour &rarr;</h4>
        <p style="font-size: 0.85rem; color: var(--text-secondary);">Tensors, async coroutines, structs, pointers, and memory safety.</p>
      </a>
      <a href="/docs/lsp" class="feature-card" style="text-decoration: none; padding: 16px; border: 1px solid var(--border-subtle); border-radius: var(--radius-md); background: var(--bg-surface-raised);">
        <h4 style="color: var(--brand-saffron); margin-bottom: 6px;">VS Code &amp; Cursor LSP &rarr;</h4>
        <p style="font-size: 0.85rem; color: var(--text-secondary);">Setup autocomplete, syntax highlighting, and diagnostics.</p>
      </a>
      <a href="/lib/" class="feature-card" style="text-decoration: none; padding: 16px; border: 1px solid var(--border-subtle); border-radius: var(--radius-md); background: var(--bg-surface-raised);">
        <h4 style="color: var(--brand-saffron); margin-bottom: 6px;">Standard Packages (40+) &rarr;</h4>
        <p style="font-size: 0.85rem; color: var(--text-secondary);">Explore tztensor, tzautodiff, tzgguf, net, io, and tezzui.</p>
      </a>
    </div>
  </article>

</div>

<?php tn_page_shell_end(); tn_footer(); ?>
