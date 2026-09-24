<?php
declare(strict_types=1);
require_once __DIR__ . '/_common.php';
tn_doc_boot('/docs/language', 'language');
tn_head('TezzNative Language Reference & Syntax Tour', 'Complete official syntax, type system, tensors, actor concurrency, memory safety, GUI, audio, and language specification for TezzNative v1.1.0.', 'docs', '/docs/language');
tn_page_shell_start('Language Reference Manual', 'Complete Syntax & Architecture Specification', 'TezzNative combines the clean clarity of Python with bare-metal native performance, built-in SIMD, native tensors, Erlang-style actor supervision, and zero garbage collection pauses.');
?>

<div style="display: grid; grid-template-columns: 260px 1fr; gap: 40px; margin-top: 30px;" class="docs-main-layout">
  <!-- Table of Contents Sidebar -->
  <aside style="position: sticky; top: 90px; height: fit-content; background: var(--bg-surface); border: 1px solid var(--border-subtle); border-radius: var(--radius-md); padding: 20px;">
    <h4 style="font-size: 0.95rem; font-weight: 700; margin-bottom: 14px; color: var(--text-primary);">Table of Contents</h4>
    <ul style="list-style: none; padding: 0; margin: 0; font-size: 0.88rem; display: flex; flex-direction: column; gap: 8px;">
      <li><a href="#types" style="color: var(--text-secondary); text-decoration: none;">1. Type System</a></li>
      <li><a href="#vars" style="color: var(--text-secondary); text-decoration: none;">2. Variables &amp; File I/O</a></li>
      <li><a href="#control" style="color: var(--text-secondary); text-decoration: none;">3. Control Flow</a></li>
      <li><a href="#functions" style="color: var(--text-secondary); text-decoration: none;">4. Functions &amp; TCO</a></li>
      <li><a href="#structs" style="color: var(--text-secondary); text-decoration: none;">5. Structs &amp; Memory Layout</a></li>
      <li><a href="#concurrency" style="color: var(--text-secondary); text-decoration: none;">6. Concurrency &amp; Actors</a></li>
      <li><a href="#tensors" style="color: var(--text-secondary); text-decoration: none;">7. Native Tensors &amp; SIMD</a></li>
      <li><a href="#audio" style="color: var(--text-secondary); text-decoration: none;">8. Native Audio (TTS)</a></li>
      <li><a href="#gui" style="color: var(--text-secondary); text-decoration: none;">9. Native GUI Subsystem</a></li>
      <li><a href="#unsafe" style="color: var(--text-secondary); text-decoration: none;">10. Memory Safety &amp; Unsafe</a></li>
    </ul>
  </aside>

  <!-- Main Content Area -->
  <div class="docs-content" style="display: flex; flex-direction: column; gap: 40px;">

    <!-- 1. Type System -->
    <section id="types">
      <h2 style="font-size: 1.6rem; color: var(--text-primary); margin-bottom: 12px;">1. Type System</h2>
      <p style="color: var(--text-secondary); line-height: 1.6; margin-bottom: 16px;">
        TezzNative is statically typed with compile-time verification and zero runtime overhead. Built-in primitive types map directly to CPU registers and native SIMD vectors.
      </p>
      <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-md); padding: 20px; overflow-x: auto;">
        <pre><code style="font-family: var(--font-mono); font-size: 0.9rem; color: var(--text-primary);"><span style="color:#64748b;">// Primitive scalar types</span>
<span style="color:#f43f5e;">let</span> a:<span style="color:#38bdf8;">int</span> = <span style="color:#fbbf24;">42</span>              <span style="color:#64748b;">// 64-bit signed integer</span>
<span style="color:#f43f5e;">let</span> b:<span style="color:#38bdf8;">float</span> = <span style="color:#fbbf24;">3.14159</span>       <span style="color:#64748b;">// 64-bit double precision float</span>
<span style="color:#f43f5e;">let</span> c:<span style="color:#38bdf8;">char</span> = <span style="color:#fbbf24;">'Z'</span> <span style="color:#f43f5e;">as</span> <span style="color:#38bdf8;">char</span>    <span style="color:#64748b;">// 8-bit character</span>
<span style="color:#f43f5e;">let</span> s:<span style="color:#38bdf8;">str</span> = <span style="color:#10b981;">"TezzNative"</span>    <span style="color:#64748b;">// UTF-8 string slice</span>

<span style="color:#64748b;">// Built-in SIMD Vector Types</span>
v4:<span style="color:#38bdf8;">Vec4f</span>                    <span style="color:#64748b;">// 128-bit SIMD vector (4 x f32)</span>
v8:<span style="color:#38bdf8;">Vec8f</span>                    <span style="color:#64748b;">// 256-bit AVX vector (8 x f32)</span></code></pre>
      </div>
    </section>

    <!-- 2. Variables & File I/O -->
    <section id="vars">
      <h2 style="font-size: 1.6rem; color: var(--text-primary); margin-bottom: 12px;">2. Variables, Scoping &amp; File I/O</h2>
      <p style="color: var(--text-secondary); line-height: 1.6; margin-bottom: 16px;">
        Variables are declared with optional <code>let</code> or direct type annotation <code>name:type = value</code>. The standard <code>io</code> module provides high-throughput buffered and streaming file operations.
      </p>
      <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-md); padding: 20px; overflow-x: auto;">
        <pre><code style="font-family: var(--font-mono); font-size: 0.9rem; color: var(--text-primary);"><span style="color:#f43f5e;">import</span> <span style="color:#10b981;">"io"</span>

<span style="color:#f43f5e;">fn</span> <span style="color:#ff9933;">process_file</span>(path:<span style="color:#38bdf8;">str</span>) -&gt; <span style="color:#38bdf8;">int</span>:
  h:<span style="color:#38bdf8;">*File</span> = io.file_open(path, <span style="color:#10b981;">"w"</span>)
  <span style="color:#f43f5e;">if</span> h == <span style="color:#fbbf24;">0</span>:
    <span style="color:#f43f5e;">ret</span> <span style="color:#fbbf24;">0</span> - <span style="color:#fbbf24;">1</span>

  io.file_write_line(h, <span style="color:#10b981;">"TezzNative High-Performance I/O"</span>)
  io.file_close(h)
  <span style="color:#ff9933;">say</span> <span style="color:#10b981;">"File written successfully to:"</span>, path
  <span style="color:#f43f5e;">ret</span> <span style="color:#fbbf24;">0</span></code></pre>
      </div>
    </section>

    <!-- 3. Control Flow -->
    <section id="control">
      <h2 style="font-size: 1.6rem; color: var(--text-primary); margin-bottom: 12px;">3. Control Flow</h2>
      <p style="color: var(--text-secondary); line-height: 1.6; margin-bottom: 16px;">
        Clean indentation-based blocks with <code>if / else</code>, <code>while</code> loops, and arithmetic modulo operators.
      </p>
      <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-md); padding: 20px; overflow-x: auto;">
        <pre><code style="font-family: var(--font-mono); font-size: 0.9rem; color: var(--text-primary);"><span style="color:#f43f5e;">let</span> count:<span style="color:#38bdf8;">int</span> = <span style="color:#fbbf24;">0</span>
<span style="color:#f43f5e;">while</span> count &lt; <span style="color:#fbbf24;">10</span>:
  <span style="color:#f43f5e;">if</span> count % <span style="color:#fbbf24;">2</span> == <span style="color:#fbbf24;">0</span>:
    <span style="color:#ff9933;">say</span> count, <span style="color:#10b981;">"is even"</span>
  <span style="color:#f43f5e;">else</span>:
    <span style="color:#ff9933;">say</span> count, <span style="color:#10b981;">"is odd"</span>
  count = count + <span style="color:#fbbf24;">1</span></code></pre>
      </div>
    </section>

    <!-- 4. Functions & TCO -->
    <section id="functions">
      <h2 style="font-size: 1.6rem; color: var(--text-primary); margin-bottom: 12px;">4. Functions &amp; Tail-Call Optimization (TCO)</h2>
      <p style="color: var(--text-secondary); line-height: 1.6; margin-bottom: 16px;">
        Functions compile to standard native calling conventions with register-passed arguments. Tail-recursive functions are automatically optimized into zero-overhead loop jumps without stack growth.
      </p>
      <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-md); padding: 20px; overflow-x: auto;">
        <pre><code style="font-family: var(--font-mono); font-size: 0.9rem; color: var(--text-primary);"><span style="color:#64748b;">// Guaranteed Tail-Call Optimized Fibonacci</span>
<span style="color:#f43f5e;">fn</span> <span style="color:#ff9933;">fib_tail</span>(n:<span style="color:#38bdf8;">int</span>, a:<span style="color:#38bdf8;">int</span>, b:<span style="color:#38bdf8;">int</span>) -&gt; <span style="color:#38bdf8;">int</span>:
  <span style="color:#f43f5e;">if</span> n == <span style="color:#fbbf24;">0</span>: <span style="color:#f43f5e;">ret</span> a
  <span style="color:#f43f5e;">if</span> n == <span style="color:#fbbf24;">1</span>: <span style="color:#f43f5e;">ret</span> b
  <span style="color:#f43f5e;">ret</span> fib_tail(n - <span style="color:#fbbf24;">1</span>, b, a + b)  <span style="color:#64748b;">// Lowered to zero-overhead JMP</span>

<span style="color:#f43f5e;">fn</span> <span style="color:#ff9933;">main</span>() -&gt; <span style="color:#38bdf8;">int</span>:
  <span style="color:#ff9933;">say</span> <span style="color:#10b981;">"fib(10) ="</span>, fib_tail(<span style="color:#fbbf24;">10</span>, <span style="color:#fbbf24;">0</span>, <span style="color:#fbbf24;">1</span>)
  <span style="color:#f43f5e;">ret</span> <span style="color:#fbbf24;">0</span></code></pre>
      </div>
    </section>

    <!-- 5. Structs & Dot Access -->
    <section id="structs">
      <h2 style="font-size: 1.6rem; color: var(--text-primary); margin-bottom: 12px;">5. Structs &amp; Memory Layout</h2>
      <p style="color: var(--text-secondary); line-height: 1.6; margin-bottom: 16px;">
        Structs lay out fields continuously in memory without hidden padding. Dot syntax works seamlessly on values (<code>val.field</code>) and pointers (<code>ptr.field</code>).
      </p>
      <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-md); padding: 20px; overflow-x: auto;">
        <pre><code style="font-family: var(--font-mono); font-size: 0.9rem; color: var(--text-primary);"><span style="color:#f43f5e;">struct</span> <span style="color:#38bdf8;">TensorShape</span>:
  ndim:<span style="color:#38bdf8;">int</span>
  rows:<span style="color:#38bdf8;">int</span>
  cols:<span style="color:#38bdf8;">int</span>
  total_elements:<span style="color:#38bdf8;">int</span>

<span style="color:#f43f5e;">fn</span> <span style="color:#ff9933;">create_matrix</span>(rows:<span style="color:#38bdf8;">int</span>, cols:<span style="color:#38bdf8;">int</span>) -&gt; <span style="color:#38bdf8;">TensorShape</span>:
  shape:<span style="color:#38bdf8;">TensorShape</span>
  shape.ndim = <span style="color:#fbbf24;">2</span>
  shape.rows = rows
  shape.cols = cols
  shape.total_elements = rows * cols
  <span style="color:#f43f5e;">ret</span> shape</code></pre>
      </div>
    </section>

    <!-- 6. Concurrency & Actors -->
    <section id="concurrency">
      <h2 style="font-size: 1.6rem; color: var(--text-primary); margin-bottom: 12px;">6. Concurrency &amp; Erlang-Style Actor Supervision</h2>
      <p style="color: var(--text-secondary); line-height: 1.6; margin-bottom: 16px;">
        TezzNative features robust Erlang-style process supervision with isolated mailboxes, message passing, restart policies, and fault tolerance built into the core runtime.
      </p>
      <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-md); padding: 20px; overflow-x: auto;">
        <pre><code style="font-family: var(--font-mono); font-size: 0.9rem; color: var(--text-primary);"><span style="color:#f43f5e;">import</span> <span style="color:#10b981;">"actor"</span>

<span style="color:#f43f5e;">fn</span> <span style="color:#ff9933;">main</span>() -&gt; <span style="color:#38bdf8;">int</span>:
  sys:<span style="color:#38bdf8;">*ActorSystem</span> = actor.system_new(<span style="color:#fbbf24;">4</span>, <span style="color:#fbbf24;">2</span>, <span style="color:#fbbf24;">2</span>)
  <span style="color:#f43f5e;">if</span> sys == <span style="color:#fbbf24;">0</span>:
    <span style="color:#f43f5e;">ret</span> <span style="color:#fbbf24;">1</span>

  worker:<span style="color:#38bdf8;">int</span> = actor.spawn(sys, <span style="color:#10b981;">"worker"</span>)
  actor.send(sys, worker, <span style="color:#fbbf24;">10</span>, <span style="color:#fbbf24;">1</span>, <span style="color:#fbbf24;">0</span>, <span style="color:#fbbf24;">0</span>, <span style="color:#fbbf24;">0</span>)
  <span style="color:#ff9933;">say</span> <span style="color:#10b981;">"Actor spawned with ID:"</span>, worker
  <span style="color:#ff9933;">say</span> <span style="color:#10b981;">"Mailbox length:"</span>, actor.mailbox_len(sys, worker)

  actor.system_free(sys)
  <span style="color:#f43f5e;">ret</span> <span style="color:#fbbf24;">0</span></code></pre>
      </div>
    </section>

    <!-- 7. Tensors & SIMD -->
    <section id="tensors">
      <h2 style="font-size: 1.6rem; color: var(--text-primary); margin-bottom: 12px;">7. Native Deep Learning Tensors &amp; SIMD Kernels</h2>
      <p style="color: var(--text-secondary); line-height: 1.6; margin-bottom: 16px;">
        First-class tensor primitives with SIMD AVX-512 and SSE accelerated kernels. Supports multidimensional representations, stride manipulation, and hardware-accelerated computation.
      </p>
      <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-md); padding: 20px; overflow-x: auto;">
        <pre><code style="font-family: var(--font-mono); font-size: 0.9rem; color: var(--text-primary);"><span style="color:#f43f5e;">import</span> <span style="color:#10b981;">"tensor"</span>

<span style="color:#f43f5e;">fn</span> <span style="color:#ff9933;">main</span>() -&gt; <span style="color:#38bdf8;">int</span>:
  <span style="color:#64748b;">// Allocate SIMD-aligned 2D tensor descriptor [128 x 512]</span>
  desc:<span style="color:#38bdf8;">TensorDesc</span> = tensor.tensor_desc(<span style="color:#fbbf24;">128</span>, <span style="color:#fbbf24;">512</span>, tensor.tensor_dtype_f32(), tensor.tensor_device_cpu(), <span style="color:#fbbf24;">64</span>)
  <span style="color:#ff9933;">say</span> <span style="color:#10b981;">"Tensor shape rows:"</span>, desc.rows, <span style="color:#10b981;">"cols:"</span>, desc.cols
  <span style="color:#ff9933;">say</span> <span style="color:#10b981;">"Total elements:"</span>, desc.size, <span style="color:#10b981;">"allocated bytes:"</span>, desc.bytes
  <span style="color:#f43f5e;">ret</span> <span style="color:#fbbf24;">0</span></code></pre>
      </div>
    </section>

    <!-- 8. Audio (TTS) -->
    <section id="audio">
      <h2 style="font-size: 1.6rem; color: var(--text-primary); margin-bottom: 12px;">8. Native Audio Subsystem (TTS Engine)</h2>
      <p style="color: var(--text-secondary); line-height: 1.6; margin-bottom: 16px;">
        Integrated native text-to-speech synthesis with zero external dependencies. Supports customizable voice profiles, speech rate, pitch tuning, and direct speaker output.
      </p>
      <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-md); padding: 20px; overflow-x: auto;">
        <pre><code style="font-family: var(--font-mono); font-size: 0.9rem; color: var(--text-primary);"><span style="color:#f43f5e;">import</span> <span style="color:#10b981;">"tts"</span>

<span style="color:#f43f5e;">fn</span> <span style="color:#ff9933;">main</span>() -&gt; <span style="color:#38bdf8;">int</span>:
  <span style="color:#64748b;">// Initialize native neural/rule-based TTS engine</span>
  eng:<span style="color:#38bdf8;">*TtsEngine</span> = tts.tts_new()
  tts.tts_speak(eng, <span style="color:#10b981;">"Welcome to TezzNative voice assistant."</span>)
  tts.tts_free(eng)
  <span style="color:#f43f5e;">ret</span> <span style="color:#fbbf24;">0</span></code></pre>
      </div>
    </section>

    <!-- 9. Native GUI -->
    <section id="gui">
      <h2 style="font-size: 1.6rem; color: var(--text-primary); margin-bottom: 12px;">9. High-Performance GUI Framework</h2>
      <p style="color: var(--text-secondary); line-height: 1.6; margin-bottom: 16px;">
        Native 60 FPS immediate-mode desktop GUI framework with rounded surfaces, anti-aliased font rendering, theme palettes, and input event dispatching.
      </p>
      <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-md); padding: 20px; overflow-x: auto;">
        <pre><code style="font-family: var(--font-mono); font-size: 0.9rem; color: var(--text-primary);"><span style="color:#f43f5e;">import</span> <span style="color:#10b981;">"tezzui"</span>

<span style="color:#f43f5e;">fn</span> <span style="color:#ff9933;">main</span>() -&gt; <span style="color:#38bdf8;">int</span>:
  ui:<span style="color:#38bdf8;">TzUI</span>
  ui_create(&amp;ui, <span style="color:#10b981;">"TezzNative Desktop App"</span>, <span style="color:#fbbf24;">800</span>, <span style="color:#fbbf24;">600</span>)
  <span style="color:#f43f5e;">while</span> ui_alive(&amp;ui) != <span style="color:#fbbf24;">0</span>:
    ui_begin(&amp;ui)
    ui_rect_r(&amp;ui, <span style="color:#fbbf24;">50</span>, <span style="color:#fbbf24;">50</span>, <span style="color:#fbbf24;">700</span>, <span style="color:#fbbf24;">100</span>, TZ_SURFACE, <span style="color:#fbbf24;">16</span>)
    ui_label(&amp;ui, <span style="color:#fbbf24;">80</span>, <span style="color:#fbbf24;">85</span>, <span style="color:#10b981;">"Hello TezzNative Native GUI!"</span>, TZ_ACCENT, <span style="color:#fbbf24;">2</span>, <span style="color:#fbbf24;">1</span>)
    ui_end(&amp;ui)
  <span style="color:#f43f5e;">ret</span> <span style="color:#fbbf24;">0</span></code></pre>
      </div>
    </section>

    <!-- 10. Memory Safety -->
    <section id="unsafe">
      <h2 style="font-size: 1.6rem; color: var(--text-primary); margin-bottom: 12px;">10. Unsafe Blocks &amp; Low-Level Hardware Access</h2>
      <p style="color: var(--text-secondary); line-height: 1.6; margin-bottom: 16px;">
        TezzNative guarantees safety for all regular code. When low-level hardware memory, pointer arithmetic, or OS driver interaction is required, explicit <code>unsafe:</code> blocks isolate raw pointers.
      </p>
      <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-md); padding: 20px; overflow-x: auto;">
        <pre><code style="font-family: var(--font-mono); font-size: 0.9rem; color: var(--text-primary);"><span style="color:#f43f5e;">fn</span> <span style="color:#ff9933;">peek_raw_byte</span>(addr:<span style="color:#38bdf8;">int</span>) -&gt; <span style="color:#38bdf8;">int</span>:
  byte_val:<span style="color:#38bdf8;">int</span> = <span style="color:#fbbf24;">0</span>
  <span style="color:#f43f5e;">unsafe</span>:
    ptr:<span style="color:#38bdf8;">*char</span> = addr <span style="color:#f43f5e;">as</span> <span style="color:#38bdf8;">*char</span>
    byte_val = ptr[<span style="color:#fbbf24;">0</span>] <span style="color:#f43f5e;">as</span> <span style="color:#38bdf8;">int</span>
  <span style="color:#f43f5e;">ret</span> byte_val</code></pre>
      </div>
    </section>

  </div>
</div>

<?php tn_page_shell_end(); tn_footer(); ?>
