<?php
declare(strict_types=1);
require_once __DIR__ . '/../includes/nav.php';
?>
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>TezzNative Language Reference — Complete Documentation v1.0</title>
  <meta name="description" content="Complete TezzNative language reference. Verified working syntax for say, functions, imports, types, control flow, math, str, gui, and more.">
  <meta name="robots" content="index, follow">
  <link rel="canonical" href="https://tn.tezzcorp.com/docs/">
  <meta property="og:title" content="TezzNative Documentation">
  <meta property="og:image" content="https://tn.tezzcorp.com/assets/tezz-corp-logo.png">
  <style>
  .docs-layout{display:flex;min-height:calc(100vh - 64px)}
  .sidebar{width:250px;flex-shrink:0;border-right:1px solid var(--border);padding:24px 0;position:sticky;top:64px;height:calc(100vh - 64px);overflow-y:auto;background:var(--bg4)}
  .sidebar-section{padding:16px 20px 4px;font-size:11px;font-weight:700;letter-spacing:.1em;text-transform:uppercase;color:var(--dim)}
  .sidebar a{display:block;color:var(--muted);font-size:14px;padding:7px 20px;border-left:2px solid transparent;transition:all .15s;text-decoration:none}
  .sidebar a:hover,.sidebar a.active{color:var(--green);border-color:var(--green);background:rgba(16,185,129,.05)}
  .doc-main{flex:1;padding:48px 60px;min-width:0;max-width:900px}
  .doc-main h1{font-size:36px;font-weight:900;color:var(--white);margin-bottom:10px;letter-spacing:-1px}
  .doc-main h2{font-size:24px;font-weight:700;color:var(--white);margin:52px 0 16px;padding-bottom:10px;border-bottom:1px solid var(--border)}
  .doc-main h3{font-size:17px;font-weight:600;color:var(--text);margin:28px 0 10px}
  .doc-main p{color:var(--muted);line-height:1.8;margin-bottom:14px}
  .doc-main ul,.doc-main ol{color:var(--muted);line-height:1.8;padding-left:22px;margin-bottom:14px}
  .doc-main li{margin-bottom:6px}
  .note{background:#0D2218;border:1px solid #0D4438;border-radius:var(--radius);padding:14px 18px;color:#86EFAC;font-size:14px;margin:16px 0}
  .note strong{color:var(--green)}
  .warn{background:#1C130A;border:1px solid #451A03;border-radius:var(--radius);padding:14px 18px;color:#FCD34D;font-size:14px;margin:16px 0}
  .api-row{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin:16px 0}
  .api-item{background:var(--bg2);border:1px solid var(--border);border-radius:var(--radius);padding:14px 16px}
  .api-item code{display:block;color:var(--green);font-size:13px;margin-bottom:4px}
  .api-item span{color:var(--muted);font-size:13px}
  @media(max-width:900px){
    .docs-layout{flex-direction:column}
    .sidebar{width:100%;height:auto;position:relative;top:0;border-right:none;border-bottom:1px solid var(--border)}
    .doc-main{padding:28px 20px}
  }
  </style>
</head>
<body>
<?php tn_nav('docs'); ?>
<div class="docs-layout">
  <aside class="sidebar">
    <div class="sidebar-section">Getting Started</div>
    <a href="#install">Installation</a>
    <a href="#hello">Hello World</a>
    <a href="#run">Running Programs</a>
    <div class="sidebar-section">Language</div>
    <a href="#say">say — Output</a>
    <a href="#types">Types</a>
    <a href="#vars">Variables</a>
    <a href="#functions">Functions</a>
    <a href="#control">Control Flow</a>
    <a href="#imports">Imports</a>
    <a href="#structs">Structs</a>
    <a href="#pointers">Pointers</a>
    <div class="sidebar-section">Standard Library</div>
    <a href="#math">math</a>
    <a href="#str">str</a>
    <a href="#io">io</a>
    <a href="#os">os</a>
    <div class="sidebar-section">GUI</div>
    <a href="#tezzui">tezzui</a>
    <a href="#gui_win">gui_win</a>
    <div class="sidebar-section">More</div>
    <a href="#net">net / tls</a>
    <a href="#tezzdb">tezzdb</a>
    <a href="#cli">tezz CLI</a>
  </aside>

  <main class="doc-main">
    <h1>TezzNative Language Reference</h1>
    <p>Complete reference for TezzNative v1.0. All examples are verified working code from the language test suite.</p>

    <h2 id="install">Installation</h2>
    <p>Download <a href="/download/">TezzNativeSetup.exe</a> and run it. It will:</p>
    <ul>
      <li>Request administrator privileges (UAC prompt)</li>
      <li>Extract the compiler to <code>C:\Program Files\TezzNative\</code></li>
      <li>Add the compiler to your system PATH (optional, recommended)</li>
      <li>Set <code>TEZZ_HOME</code> so the stdlib is always found from any directory</li>
    </ul>
    <div class="note"><strong>Tip:</strong> After install, open a <em>new</em> terminal — the PATH and TEZZ_HOME updates take effect in new shells only.</div>

    <h2 id="hello">Hello World</h2>
    <p>The simplest TezzNative program — no imports needed:</p>
    <pre><span class="cm">// hello.tn</span>
<span class="kw">fn</span> <span class="fn">main</span>() -> <span class="kw">int</span>:
  say <span class="st">"Hello from TezzNative!"</span>
  <span class="kw">ret</span> 0</pre>
    <div class="warn"><strong>Important:</strong> <code>say</code> is a <strong>keyword</strong>, not a function. Do not write <code>say("text")</code> — write <code>say "text"</code> with no parentheses. <code>say</code> can print strings, integers, and floats directly.</div>

    <h2 id="run">Running Programs</h2>
    <pre><span class="cm"># Run via bytecode VM (fast, no linker step)</span>
tezz run hello.tn
tezzc run hello.tn --bc

<span class="cm"># Compile to a standalone Windows .exe</span>
tezz buildexe hello.tn Hello.exe
tezzc buildexe hello.tn Hello.exe</pre>

    <h2 id="say">say — Built-in Output</h2>
    <p><code>say</code> is TezzNative's built-in output keyword. It works without any import and can print any type:</p>
    <pre><span class="kw">fn</span> <span class="fn">main</span>():
  say <span class="st">"Hello World"</span>           <span class="cm">// string literal</span>
  say <span class="num">42</span>                      <span class="cm">// integer</span>
  say <span class="num">3.14</span>                    <span class="cm">// float</span>
  x:<span class="kw">int</span> = <span class="num">100</span>
  say x                       <span class="cm">// variable</span>
  say <span class="st">"PI = "</span>
  say math.PI()               <span class="cm">// after import "math"</span></pre>
    <div class="note"><strong>Note:</strong> <code>say</code> prints one value per call. To print multiple values on one line, use multiple <code>say</code> statements or format a string with <code>str</code> module helpers.</div>

    <h2 id="types">Types</h2>
    <div class="api-row">
      <div class="api-item"><code>int</code><span>64-bit signed integer</span></div>
      <div class="api-item"><code>float</code><span>64-bit double precision</span></div>
      <div class="api-item"><code>str</code><span>String (null-terminated pointer)</span></div>
      <div class="api-item"><code>bool</code><span>Boolean: true / false</span></div>
      <div class="api-item"><code>char</code><span>Single byte character</span></div>
      <div class="api-item"><code>*T</code><span>Pointer to type T</span></div>
    </div>
    <pre><span class="cm">// Type casting</span>
n:<span class="kw">int</span> = <span class="num">65</span>
c:<span class="kw">char</span> = n <span class="kw">as char</span>     <span class="cm">// cast int to char</span>
f:<span class="kw">float</span> = n <span class="kw">as float</span>  <span class="cm">// cast int to float</span>
i:<span class="kw">int</span> = f <span class="kw">as int</span>      <span class="cm">// cast float to int (truncates)</span></pre>

    <h2 id="vars">Variables</h2>
    <pre><span class="cm">// Type-annotated variable declaration</span>
x:<span class="kw">int</span> = <span class="num">42</span>
name:<span class="kw">str</span> = <span class="st">"TezzNative"</span>
pi:<span class="kw">float</span> = <span class="num">3.14159</span>

<span class="cm">// Module-level constants</span>
<span class="kw">let</span> MAX_SIZE:<span class="kw">int</span> = <span class="num">1024</span>
<span class="kw">let</span> BG_COLOR:<span class="kw">int</span> = <span class="num">0x050810</span>

<span class="cm">// Global mutable state</span>
<span class="kw">static let</span> g_count:<span class="kw">int</span> = <span class="num">0</span>
<span class="kw">static let</span> g_name:<span class="kw">str</span> = <span class="st">"default"</span></pre>

    <h2 id="functions">Functions</h2>
    <pre><span class="cm">// Basic function</span>
<span class="kw">fn</span> <span class="fn">add</span>(a:<span class="kw">int</span>, b:<span class="kw">int</span>) -> <span class="kw">int</span>:
  <span class="kw">ret</span> a + b

<span class="cm">// Function with no return value</span>
<span class="kw">fn</span> <span class="fn">greet</span>(name:<span class="kw">str</span>):
  say <span class="st">"Hello, "</span>
  say name

<span class="cm">// Main entry point (two valid forms)</span>
<span class="kw">fn</span> <span class="fn">main</span>():           <span class="cm">// no return type required</span>
  say <span class="st">"ok"</span>

<span class="kw">fn</span> <span class="fn">main</span>() -> <span class="kw">int</span>:    <span class="cm">// explicit return</span>
  <span class="kw">ret</span> 0

<span class="cm">// Calling functions</span>
<span class="kw">fn</span> <span class="fn">main</span>():
  result:<span class="kw">int</span> = <span class="fn">add</span>(<span class="num">10</span>, <span class="num">20</span>)
  say result
  <span class="fn">greet</span>(<span class="st">"World"</span>)</pre>

    <h2 id="control">Control Flow</h2>
    <pre><span class="cm">// if / else</span>
<span class="kw">if</span> x > <span class="num">10</span>:
  say <span class="st">"big"</span>
<span class="kw">else if</span> x > <span class="num">5</span>:
  say <span class="st">"medium"</span>
<span class="kw">else</span>:
  say <span class="st">"small"</span>

<span class="cm">// while loop</span>
i:<span class="kw">int</span> = <span class="num">0</span>
<span class="kw">while</span> i < <span class="num">10</span>:
  say i
  i = i + <span class="num">1</span>

<span class="cm">// for loop (infinite with break)</span>
<span class="kw">for</span> ;; :
  <span class="kw">if</span> i >= <span class="num">10</span>:
    <span class="kw">break</span>
  i += <span class="num">1</span></pre>

    <h2 id="imports">Imports</h2>
    <p>Use <code>import "module_name"</code> to load standard library modules. After install, <code>TEZZ_HOME</code> is set so the compiler always finds them — no project-level <code>lib/</code> folder needed.</p>
    <pre><span class="kw">import</span> <span class="st">"math"</span>   <span class="cm">// math.sin, math.cos, math.sqrt, math.PI() ...</span>
<span class="kw">import</span> <span class="st">"str"</span>    <span class="cm">// str.str_len, str.str_to_int, str.str_concat ...</span>
<span class="kw">import</span> <span class="st">"io"</span>     <span class="cm">// io.read_all, io.write_all, io.file_open ...</span>
<span class="kw">import</span> <span class="st">"os"</span>     <span class="cm">// os.getenv, os.exit ...</span>
<span class="kw">import</span> <span class="st">"tezzui"</span> <span class="cm">// native GUI framework</span>
<span class="kw">import</span> <span class="st">"gui_win"</span><span class="cm">// screen size, OS calls</span></pre>

    <h2 id="structs">Structs</h2>
    <pre><span class="kw">struct</span> Point:
  x:<span class="kw">int</span>
  y:<span class="kw">int</span>

<span class="kw">fn</span> <span class="fn">main</span>():
  p:Point
  p.x = <span class="num">10</span>
  p.y = <span class="num">20</span>
  say p.x
  say p.y</pre>

    <h2 id="pointers">Pointers & Unsafe</h2>
    <pre><span class="kw">fn</span> <span class="fn">set_val</span>(p:*<span class="kw">int</span>, v:<span class="kw">int</span>):
  <span class="kw">unsafe</span>:
    *p = v

<span class="kw">fn</span> <span class="fn">main</span>():
  x:<span class="kw">int</span> = <span class="num">0</span>
  <span class="fn">set_val</span>(&x, <span class="num">42</span>)
  say x   <span class="cm">// prints 42</span></pre>

    <h2 id="math">import "math"</h2>
    <p>Pure TezzNative math library with polynomial approximations — no C math.h dependency.</p>
    <pre><span class="kw">import</span> <span class="st">"math"</span>

<span class="kw">fn</span> <span class="fn">main</span>():
  say math.<span class="fn">PI</span>()             <span class="cm">// 3.14159...</span>
  say math.<span class="fn">sin</span>(<span class="num">0.5</span>)         <span class="cm">// 0.4794...</span>
  say math.<span class="fn">cos</span>(<span class="num">0.0</span>)         <span class="cm">// 1.0</span>
  say math.<span class="fn">sqrt</span>(<span class="num">2.0</span>)        <span class="cm">// 1.4142...</span>
  say math.<span class="fn">pow</span>(<span class="num">2.0</span>, <span class="num">10.0</span>)  <span class="cm">// 1024.0</span>
  say math.<span class="fn">log</span>(math.<span class="fn">E</span>())   <span class="cm">// 1.0</span>
  say math.<span class="fn">gcd</span>(<span class="num">48</span>, <span class="num">18</span>)     <span class="cm">// 6</span>
  say math.<span class="fn">abs</span>(<span class="num">0</span>-<span class="num">5</span>)        <span class="cm">// 5</span>
  say math.<span class="fn">max</span>(<span class="num">3</span>, <span class="num">7</span>)       <span class="cm">// 7</span>
  say math.<span class="fn">min</span>(<span class="num">3</span>, <span class="num">7</span>)       <span class="cm">// 3</span>
  say math.<span class="fn">clamp</span>(<span class="num">15</span>,<span class="num">0</span>,<span class="num">10</span>) <span class="cm">// 10</span></pre>

    <h2 id="str">import "str"</h2>
    <pre><span class="kw">import</span> <span class="st">"str"</span>

<span class="kw">fn</span> <span class="fn">main</span>():
  s:<span class="kw">str</span> = <span class="st">"hello world"</span>
  say str.<span class="fn">str_len</span>(s)           <span class="cm">// 11</span>
  say str.<span class="fn">str_to_int</span>(<span class="st">"42"</span>)    <span class="cm">// 42</span>
  say str.<span class="fn">str_dup</span>(s)           <span class="cm">// "hello world" (heap copy)</span>
  say str.<span class="fn">str_empty</span>(<span class="st">""</span>)        <span class="cm">// 1 (true)</span>
  say str.<span class="fn">str_empty</span>(s)        <span class="cm">// 0 (false)</span></pre>

    <h2 id="io">import "io"</h2>
    <p>File and stream I/O. Uses explicit file handles.</p>
    <pre><span class="kw">import</span> <span class="st">"io"</span>

<span class="kw">fn</span> <span class="fn">main</span>():
  <span class="cm">// Read a whole file</span>
  data:<span class="kw">str</span> = io.<span class="fn">read_all</span>(<span class="st">"input.txt"</span>)
  say data

  <span class="cm">// Write to a file</span>
  io.<span class="fn">write_all</span>(<span class="st">"out.txt"</span>, <span class="st">"Hello!"</span>)

  <span class="cm">// Open, read line by line</span>
  f:*io.File = io.<span class="fn">open_r</span>(<span class="st">"input.txt"</span>)
  line:<span class="kw">str</span> = io.<span class="fn">file_read_line</span>(f)
  say line
  io.<span class="fn">file_close</span>(f)</pre>

    <h2 id="tezzui">import "tezzui" — Native GUI</h2>
    <p>Immediate-mode native Win32 GUI. No Electron, no web browser — pure pixel rendering.</p>
    <pre><span class="kw">import</span> <span class="st">"tezzui"</span>

<span class="kw">fn</span> <span class="fn">main</span>() -> <span class="kw">int</span>:
  ctx: TzUI
  <span class="kw">unsafe</span>:
    tezzui.<span class="fn">ui_create</span>(&ctx, <span class="st">"My App"</span>, <span class="num">800</span>, <span class="num">600</span>)
    <span class="kw">while</span> tezzui.<span class="fn">ui_alive</span>(&ctx) != <span class="num">0</span>:
      tezzui.<span class="fn">ui_begin</span>(&ctx)
      <span class="cm">// Draw your UI here</span>
      tezzui.<span class="fn">ui_label</span>(&ctx, <span class="num">20</span>, <span class="num">20</span>, <span class="st">"Hello!"</span>, <span class="num">0xFFFFFF</span>, <span class="num">2</span>, <span class="num">1</span>)
      tezzui.<span class="fn">ui_button</span>(&ctx, <span class="num">20</span>, <span class="num">60</span>, <span class="num">120</span>, <span class="num">30</span>, <span class="st">"Click Me"</span>)
      tezzui.<span class="fn">ui_end</span>(&ctx)
  <span class="kw">ret</span> 0</pre>
    <div class="api-row">
      <div class="api-item"><code>tezzui.ui_create(&ctx, title, w, h)</code><span>Create window</span></div>
      <div class="api-item"><code>tezzui.ui_alive(&ctx)</code><span>Returns 1 while window open</span></div>
      <div class="api-item"><code>tezzui.ui_begin(&ctx)</code><span>Start frame</span></div>
      <div class="api-item"><code>tezzui.ui_end(&ctx)</code><span>End frame (renders)</span></div>
      <div class="api-item"><code>tezzui.ui_button(&ctx, x, y, w, h, label)</code><span>Clickable button → 1 if clicked</span></div>
      <div class="api-item"><code>tezzui.ui_label(&ctx, x, y, text, color, scale, bold)</code><span>Text label</span></div>
      <div class="api-item"><code>tezzui.ui_progress(&ctx, x, y, w, h, val, color)</code><span>Progress bar (0–100)</span></div>
      <div class="api-item"><code>tezzui.ui_checkbox(&ctx, x, y, label, state)</code><span>Checkbox → new state</span></div>
      <div class="api-item"><code>tezzui.ui_slider(&ctx, x, y, w, min, max, val)</code><span>Slider → new value</span></div>
      <div class="api-item"><code>tezzui.ui_input(&ctx, x, y, w, h, placeholder)</code><span>Text input field</span></div>
    </div>

    <h2 id="gui_win">import "gui_win" — OS Primitives</h2>
    <pre><span class="kw">import</span> <span class="st">"gui_win"</span>

<span class="kw">fn</span> <span class="fn">main</span>():
  w:<span class="kw">int</span> = gui_win.<span class="fn">screen_width</span>()
  h:<span class="kw">int</span> = gui_win.<span class="fn">screen_height</span>()
  say <span class="st">"Screen:"</span>
  say w
  say h
  gui_win.<span class="fn">os_system</span>(<span class="st">"echo Hello from OS"</span>)</pre>

    <h2 id="net">import "net" / "tls"</h2>
    <p>Full TCP/HTTP networking with optional TLS. Used by <code>tezzserve</code> and <code>tezzapi</code> internally.</p>

    <h2 id="tezzdb">import "tezzdb"</h2>
    <p>Zero-setup embedded key-value database. Store and retrieve data without any external server.</p>

    <h2 id="cli">tezz CLI Reference</h2>
    <pre><span class="cm"># Show help and all commands</span>
tezz

<span class="cm"># Run a program via bytecode VM</span>
tezz run hello.tn

<span class="cm"># Compile to native Windows executable</span>
tezz buildexe hello.tn Hello.exe

<span class="cm"># Show version info</span>
tezz version</pre>
  </main>
</div>
<?php tn_footer(); ?>
</body>
</html>
