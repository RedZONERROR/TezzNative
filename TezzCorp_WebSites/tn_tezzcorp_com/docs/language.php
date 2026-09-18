<?php
declare(strict_types=1);
require_once __DIR__ . '/_common.php';
tn_doc_boot('/docs/language', 'language');
tn_head('TezzNative Language Reference & Syntax Tour', 'Complete official syntax, type system, tensors, coroutines, memory safety, GUI, audio, and language specification for TezzNative v2.2.', 'docs', '/docs/language');
tn_page_shell_start('Language Reference Manual', 'Complete Syntax & Architecture Specification', 'TezzNative combines the clean clarity of Python with bare-metal C performance, native tensors, coroutines, and zero garbage collection pauses.');
?>

<div style="display: grid; grid-template-columns: 260px 1fr; gap: 40px; margin-top: 30px;" class="docs-main-layout">
  <!-- Table of Contents Sidebar -->
  <aside style="position: sticky; top: 90px; height: fit-content; background: var(--bg-surface); border: 1px solid var(--border-subtle); border-radius: var(--radius-md); padding: 20px;">
    <h4 style="font-size: 0.95rem; font-weight: 700; margin-bottom: 14px; color: var(--text-primary);">Table of Contents</h4>
    <ul style="list-style: none; padding: 0; margin: 0; font-size: 0.88rem; display: flex; flex-direction: column; gap: 8px;">
      <li><a href="#types" style="color: var(--text-secondary); text-decoration: none;">1. Type System</a></li>
      <li><a href="#vars" style="color: var(--text-secondary); text-decoration: none;">2. Variables &amp; Scoping</a></li>
      <li><a href="#control" style="color: var(--text-secondary); text-decoration: none;">3. Control Flow</a></li>
      <li><a href="#functions" style="color: var(--text-secondary); text-decoration: none;">4. Functions &amp; TCO</a></li>
      <li><a href="#structs" style="color: var(--text-secondary); text-decoration: none;">5. Structs &amp; Dot Access</a></li>
      <li><a href="#async" style="color: var(--text-secondary); text-decoration: none;">6. Async / Await Coroutines</a></li>
      <li><a href="#tensors" style="color: var(--text-secondary); text-decoration: none;">7. Native Tensors &amp; Autodiff</a></li>
      <li><a href="#audio" style="color: var(--text-secondary); text-decoration: none;">8. Audio (TTS &amp; STT)</a></li>
      <li><a href="#gui" style="color: var(--text-secondary); text-decoration: none;">9. Native GUI Subsystem</a></li>
      <li><a href="#unsafe" style="color: var(--text-secondary); text-decoration: none;">10. Unsafe &amp; Memory Safety</a></li>
    </ul>
  </aside>

  <!-- Main Content Area -->
  <div class="docs-content" style="display: flex; flex-direction: column; gap: 40px;">

    <!-- 1. Type System -->
    <section id="types">
      <h2 style="font-size: 1.6rem; color: var(--text-primary); margin-bottom: 12px;">1. Type System</h2>
      <p style="color: var(--text-secondary); line-height: 1.6; margin-bottom: 16px;">
        TezzNative is statically typed with strong compile-time inference and zero runtime type overhead. Built-in types map directly to hardware registers.
      </p>
      <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-md); padding: 20px; overflow-x: auto;">
        <pre><code style="font-family: var(--font-mono); font-size: 0.9rem; color: var(--text-primary);"><span style="color:#64748b;">// Primitive scalar types</span>
<span style="color:#f43f5e;">let</span> a: <span style="color:#38bdf8;">int</span> = <span style="color:#fbbf24;">42</span>              <span style="color:#64748b;">// 64-bit signed integer (i64)</span>
<span style="color:#f43f5e;">let</span> b: <span style="color:#38bdf8;">float</span> = <span style="color:#fbbf24;">3.14159</span>       <span style="color:#64748b;">// 64-bit double precision float (f64)</span>
<span style="color:#f43f5e;">let</span> c: <span style="color:#38bdf8;">char</span> = <span style="color:#fbbf24;">'Z'</span>            <span style="color:#64748b;">// 8-bit unsigned character</span>
<span style="color:#f43f5e;">let</span> s: <span style="color:#38bdf8;">str</span> = <span style="color:#10b981;">"TezzNative"</span>   <span style="color:#64748b;">// Null-terminated UTF-8 string slice</span>

<span style="color:#64748b;">// Fixed SIMD &amp; Tensor Vector Types</span>
<span style="color:#f43f5e;">typedef</span> [<span style="color:#38bdf8;">float</span>; <span style="color:#fbbf24;">4</span>] Vec4f     <span style="color:#64748b;">// 128-bit SSE vector (4 x f32)</span>
<span style="color:#f43f5e;">typedef</span> [<span style="color:#38bdf8;">float</span>; <span style="color:#fbbf24;">8</span>] Vec8f     <span style="color:#64748b;">// 256-bit AVX2 vector (8 x f32)</span>
<span style="color:#f43f5e;">typedef</span> [<span style="color:#38bdf8;">int</span>; <span style="color:#fbbf24;">8</span>]   Vec8i     <span style="color:#64748b;">// 256-bit integer vector (8 x i32)</span></code></pre>
      </div>
    </section>

    <!-- 2. Variables & Scoping -->
    <section id="vars">
      <h2 style="font-size: 1.6rem; color: var(--text-primary); margin-bottom: 12px;">2. Variables, Constants &amp; Defer</h2>
      <p style="color: var(--text-secondary); line-height: 1.6; margin-bottom: 16px;">
        Variables are declared with <code>let</code>. The <code>defer</code> keyword guarantees that resources (files, sockets, memory) are closed automatically at scope exit without garbage collection pauses.
      </p>
      <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-md); padding: 20px; overflow-x: auto;">
        <pre><code style="font-family: var(--font-mono); font-size: 0.9rem; color: var(--text-primary);"><span style="color:#f43f5e;">import</span> <span style="color:#10b981;">"io"</span>

<span style="color:#f43f5e;">fn</span> <span style="color:#ff9933;">process_file</span>(path:<span style="color:#38bdf8;">str</span>) -&gt; <span style="color:#38bdf8;">int</span>:
  <span style="color:#f43f5e;">let</span> h = io.open_file(path, <span style="color:#10b981;">"r"</span>)
  <span style="color:#f43f5e;">if</span> h == <span style="color:#fbbf24;">0</span>: <span style="color:#f43f5e;">ret</span> <span style="color:#fbbf24;">0</span> - <span style="color:#fbbf24;">1</span>

  <span style="color:#f43f5e;">defer</span> io.close_file(h)  <span style="color:#64748b;">// Executed automatically when function returns</span>
  <span style="color:#f43f5e;">let</span> content = io.read_all(h)
  <span style="color:#ff9933;">say</span> <span style="color:#10b981;">"File size:"</span>, len(content)
  <span style="color:#f43f5e;">ret</span> <span style="color:#fbbf24;">0</span></code></pre>
      </div>
    </section>

    <!-- 3. Control Flow -->
    <section id="control">
      <h2 style="font-size: 1.6rem; color: var(--text-primary); margin-bottom: 12px;">3. Control Flow</h2>
      <p style="color: var(--text-secondary); line-height: 1.6; margin-bottom: 16px;">
        Clean Pythonic indentation-based blocks with <code>if / elif / else</code>, <code>while</code>, <code>for</code> loops, and pattern matching.
      </p>
      <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-md); padding: 20px; overflow-x: auto;">
        <pre><code style="font-family: var(--font-mono); font-size: 0.9rem; color: var(--text-primary);"><span style="color:#f43f5e;">let</span> count: <span style="color:#38bdf8;">int</span> = <span style="color:#fbbf24;">0</span>
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
        Functions compile to standard x86-64 / ARM64 calling conventions with register-passed arguments (`RCX, RDX, R8, R9` on Win64). Tail-recursive functions are automatically optimized into loops without stack growth.
      </p>
      <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-md); padding: 20px; overflow-x: auto;">
        <pre><code style="font-family: var(--font-mono); font-size: 0.9rem; color: var(--text-primary);"><span style="color:#64748b;">// Guaranteed Tail-Call Optimized Fibonacci</span>
<span style="color:#f43f5e;">fn</span> <span style="color:#ff9933;">fib_tail</span>(n:<span style="color:#38bdf8;">int</span>, a:<span style="color:#38bdf8;">int</span>, b:<span style="color:#38bdf8;">int</span>) -&gt; <span style="color:#38bdf8;">int</span>:
  <span style="color:#f43f5e;">if</span> n == <span style="color:#fbbf24;">0</span>: <span style="color:#f43f5e;">ret</span> a
  <span style="color:#f43f5e;">if</span> n == <span style="color:#fbbf24;">1</span>: <span style="color:#f43f5e;">ret</span> b
  <span style="color:#f43f5e;">ret</span> fib_tail(n - <span style="color:#fbbf24;">1</span>, b, a + b)  <span style="color:#64748b;">// Lowered to zero-overhead JMP</span></code></pre>
      </div>
    </section>

    <!-- 5. Structs & Dot Access -->
    <section id="structs">
      <h2 style="font-size: 1.6rem; color: var(--text-primary); margin-bottom: 12px;">5. Structs &amp; Automatic Pointer Dereferencing</h2>
      <p style="color: var(--text-secondary); line-height: 1.6; margin-bottom: 16px;">
        Structs lay out fields continuously in memory without padding surprises. Dot access works seamlessly on both values (<code>obj.field</code>) and pointers (<code>ptr.field</code>).
      </p>
      <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-md); padding: 20px; overflow-x: auto;">
        <pre><code style="font-family: var(--font-mono); font-size: 0.9rem; color: var(--text-primary);"><span style="color:#f43f5e;">struct</span> <span style="color:#38bdf8;">TensorShape</span>:
  ndim: <span style="color:#38bdf8;">int</span>
  dims: [<span style="color:#38bdf8;">int</span>; <span style="color:#fbbf24;">4</span>]
  total_elements: <span style="color:#38bdf8;">int</span>

<span style="color:#f43f5e;">fn</span> <span style="color:#ff9933;">create_matrix</span>(rows:<span style="color:#38bdf8;">int</span>, cols:<span style="color:#38bdf8;">int</span>) -&gt; <span style="color:#38bdf8;">TensorShape</span>:
  <span style="color:#f43f5e;">let</span> shape: <span style="color:#38bdf8;">TensorShape</span>
  shape.ndim = <span style="color:#fbbf24;">2</span>
  shape.dims[<span style="color:#fbbf24;">0</span>] = rows
  shape.dims[<span style="color:#fbbf24;">1</span>] = cols
  shape.total_elements = rows * cols
  <span style="color:#f43f5e;">ret</span> shape</code></pre>
      </div>
    </section>

    <!-- 6. Async / Await -->
    <section id="async">
      <h2 style="font-size: 1.6rem; color: var(--text-primary); margin-bottom: 12px;">6. Native Async / Await Coroutines</h2>
      <p style="color: var(--text-secondary); line-height: 1.6; margin-bottom: 16px;">
        TezzNative coroutines are true non-blocking lightweight tasks scheduled across native I/O completion ports (IOCP on Windows, epoll on Linux).
      </p>
      <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-md); padding: 20px; overflow-x: auto;">
        <pre><code style="font-family: var(--font-mono); font-size: 0.9rem; color: var(--text-primary);"><span style="color:#f43f5e;">import</span> <span style="color:#10b981;">"task"</span>
<span style="color:#f43f5e;">import</span> <span style="color:#10b981;">"net"</span>

<span style="color:#f43f5e;">async fn</span> <span style="color:#ff9933;">fetch_api_data</span>(endpoint:<span style="color:#38bdf8;">str</span>) -&gt; <span style="color:#38bdf8;">int</span>:
  <span style="color:#f43f5e;">let</span> client = net.http_client()
  <span style="color:#f43f5e;">defer</span> client.close()
  <span style="color:#f43f5e;">let</span> resp = client.get(endpoint)
  <span style="color:#f43f5e;">ret</span> resp.status_code

<span style="color:#f43f5e;">fn</span> <span style="color:#ff9933;">main</span>() -&gt; <span style="color:#38bdf8;">int</span>:
  <span style="color:#f43f5e;">let</span> t1 = task.spawn_arg(fetch_api_data, <span style="color:#10b981;">"https://api.tezzcorp.com/v1/metrics"</span>)
  <span style="color:#f43f5e;">let</span> status: <span style="color:#38bdf8;">int</span> = <span style="color:#f43f5e;">await</span> t1
  <span style="color:#ff9933;">say</span> <span style="color:#10b981;">"HTTP Response:"</span>, status
  <span style="color:#f43f5e;">ret</span> <span style="color:#fbbf24;">0</span></code></pre>
      </div>
    </section>

    <!-- 7. Tensors & Autodiff -->
    <section id="tensors">
      <h2 style="font-size: 1.6rem; color: var(--text-primary); margin-bottom: 12px;">7. Native Deep Learning Tensors &amp; Autograd</h2>
      <p style="color: var(--text-secondary); line-height: 1.6; margin-bottom: 16px;">
        First-class tensor algebra with infix <code>@</code> for matrix multiplications, SIMD AVX-512 kernels, and CUDA GPU dispatch via <code>tzgpu</code>.
      </p>
      <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-md); padding: 20px; overflow-x: auto;">
        <pre><code style="font-family: var(--font-mono); font-size: 0.9rem; color: var(--text-primary);"><span style="color:#f43f5e;">import</span> <span style="color:#10b981;">"tztensor"</span>
<span style="color:#f43f5e;">import</span> <span style="color:#10b981;">"tzgpu"</span>

<span style="color:#f43f5e;">fn</span> <span style="color:#ff9933;">main</span>() -&gt; <span style="color:#38bdf8;">int</span>:
  <span style="color:#64748b;">// Initialize 2D tensors [Batch x Hidden]</span>
  <span style="color:#f43f5e;">let</span> A = tztensor.zeros_2d(<span style="color:#fbbf24;">128</span>, <span style="color:#fbbf24;">512</span>)
  <span style="color:#f43f5e;">let</span> W = tztensor.xavier_2d(<span style="color:#fbbf24;">512</span>, <span style="color:#fbbf24;">1024</span>)

  <span style="color:#64748b;">// Ultra-fast Hardware GEMM (dispatched to CUDA GPU if available)</span>
  <span style="color:#f43f5e;">let</span> C = tztensor.matmul(A, W)
  <span style="color:#ff9933;">say</span> <span style="color:#10b981;">"Computed output tensor shape:"</span>, C.rows, <span style="color:#10b981;">"x"</span>, C.cols
  <span style="color:#f43f5e;">ret</span> <span style="color:#fbbf24;">0</span></code></pre>
      </div>
    </section>

    <!-- 8. Audio (TTS & STT) -->
    <section id="audio">
      <h2 style="font-size: 1.6rem; color: var(--text-primary); margin-bottom: 12px;">8. Native Audio Subsystem (TTS &amp; STT)</h2>
      <p style="color: var(--text-secondary); line-height: 1.6; margin-bottom: 16px;">
        Zero-dependency native speech synthesis and microphone audio capture powered by <code>tzgui.dll</code> and Windows SAPI / WinMM drivers.
      </p>
      <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-md); padding: 20px; overflow-x: auto;">
        <pre><code style="font-family: var(--font-mono); font-size: 0.9rem; color: var(--text-primary);"><span style="color:#f43f5e;">import</span> <span style="color:#10b981;">"tts"</span>
<span style="color:#f43f5e;">import</span> <span style="color:#10b981;">"stt"</span>

<span style="color:#f43f5e;">fn</span> <span style="color:#ff9933;">main</span>() -&gt; <span style="color:#38bdf8;">int</span>:
  <span style="color:#64748b;">// 1. Crystal-clear Text-to-Speech</span>
  <span style="color:#f43f5e;">let</span> eng = tts.tts_new()
  tts.tts_speak(eng, <span style="color:#10b981;">"Welcome to TezzNative voice assistant."</span>)
  tts.tts_free(eng)

  <span style="color:#64748b;">// 2. Microphone Capture &amp; Whisper Mel-Spectrogram Extraction</span>
  <span style="color:#f43f5e;">let</span> stt_eng = stt.stt_new()
  <span style="color:#f43f5e;">let</span> transcription = stt.stt_from_mic(stt_eng, <span style="color:#fbbf24;">3000</span>) <span style="color:#64748b;">// 3000ms audio</span>
  <span style="color:#ff9933;">say</span> <span style="color:#10b981;">"Transcribed text:"</span>, transcription
  stt.stt_free(stt_eng)
  <span style="color:#f43f5e;">ret</span> <span style="color:#fbbf24;">0</span></code></pre>
      </div>
    </section>

    <!-- 9. Native GUI -->
    <section id="gui">
      <h2 style="font-size: 1.6rem; color: var(--text-primary); margin-bottom: 12px;">9. High-Performance GUI Framework</h2>
      <p style="color: var(--text-secondary); line-height: 1.6; margin-bottom: 16px;">
        Native 60 FPS retained &amp; immediate-mode GUI with anti-aliased text, rounded rectangles, wallpaper rendering, and mouse event dispatching.
      </p>
      <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-md); padding: 20px; overflow-x: auto;">
        <pre><code style="font-family: var(--font-mono); font-size: 0.9rem; color: var(--text-primary);"><span style="color:#f43f5e;">import</span> <span style="color:#10b981;">"tezzui"</span>

<span style="color:#f43f5e;">fn</span> <span style="color:#ff9933;">main</span>() -&gt; <span style="color:#38bdf8;">int</span>:
  tezzui.window(<span style="color:#10b981;">"TezzNative Desktop App"</span>, <span style="color:#fbbf24;">800</span>, <span style="color:#fbbf24;">600</span>)
  <span style="color:#f43f5e;">while</span> tezzui.running():
    tezzui.fill(<span style="color:#fbbf24;">0x080B11</span>)  <span style="color:#64748b;">// Deep Dark Background</span>
    tezzui.fill_rounded(<span style="color:#fbbf24;">50</span>, <span style="color:#fbbf24;">50</span>, <span style="color:#fbbf24;">700</span>, <span style="color:#fbbf24;">100</span>, <span style="color:#fbbf24;">16</span>, <span style="color:#fbbf24;">0x1E2842</span>)
    tezzui.text(<span style="color:#fbbf24;">80</span>, <span style="color:#fbbf24;">85</span>, <span style="color:#10b981;">"Hello TezzNative Native GUI!"</span>, <span style="color:#fbbf24;">0xFF9933</span>)
    tezzui.present()
  <span style="color:#f43f5e;">ret</span> <span style="color:#fbbf24;">0</span></code></pre>
      </div>
    </section>

    <!-- 10. Memory Safety -->
    <section id="unsafe">
      <h2 style="font-size: 1.6rem; color: var(--text-primary); margin-bottom: 12px;">10. Unsafe Blocks &amp; Low-Level Hardware Access</h2>
      <p style="color: var(--text-secondary); line-height: 1.6; margin-bottom: 16px;">
        TezzNative enforces safe bounds on standard code. When bare-metal pointer arithmetic or OS kernel interaction is needed, explicit <code>unsafe:</code> blocks isolate raw pointers.
      </p>
      <div style="background: var(--code-bg); border: 1px solid var(--code-border); border-radius: var(--radius-md); padding: 20px; overflow-x: auto;">
        <pre><code style="font-family: var(--font-mono); font-size: 0.9rem; color: var(--text-primary);"><span style="color:#f43f5e;">fn</span> <span style="color:#ff9933;">peek_raw_byte</span>(addr:<span style="color:#38bdf8;">int</span>) -&gt; <span style="color:#38bdf8;">int</span>:
  <span style="color:#f43f5e;">let</span> byte_val: <span style="color:#38bdf8;">int</span> = <span style="color:#fbbf24;">0</span>
  <span style="color:#f43f5e;">unsafe</span>:
    <span style="color:#f43f5e;">let</span> ptr:*<span style="color:#38bdf8;">char</span> = addr <span style="color:#f43f5e;">as</span> *<span style="color:#38bdf8;">char</span>
    byte_val = ptr[<span style="color:#fbbf24;">0</span>] <span style="color:#f43f5e;">as</span> <span style="color:#38bdf8;">int</span>
  <span style="color:#f43f5e;">ret</span> byte_val</code></pre>
      </div>
    </section>

  </div>
</div>

<?php tn_page_shell_end(); tn_footer(); ?>
