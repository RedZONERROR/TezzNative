<?php
declare(strict_types=1);
if (!isset($slug)) {
  $slug = basename(dirname($_SERVER['PHP_SELF']));
}
$frames  = [
  'tezzui'    => ['TezzUI',    '🖥️', 'Immediate-mode native GUI framework',
    'Build pixel-perfect Win32 applications with an immediate-mode API. No retained object tree — just call widget functions each frame.',
    [
      ['tezzui.ui_create(&ctx, title, w, h)','Create a window'],
      ['tezzui.ui_alive(&ctx)','Returns 1 while window is open'],
      ['tezzui.ui_begin(&ctx)','Start a new frame'],
      ['tezzui.ui_end(&ctx)','Render and present the frame'],
      ['tezzui.ui_button(&ctx, x, y, w, h, label)','Button — returns 1 when clicked'],
      ['tezzui.ui_label(&ctx, x, y, text, color, scale, bold)','Text label'],
      ['tezzui.ui_progress(&ctx, x, y, w, h, val, color)','Progress bar (0–100)'],
      ['tezzui.ui_checkbox(&ctx, x, y, label, state)','Checkbox — returns new state'],
      ['tezzui.ui_radio(&ctx, x, y, label, active)','Radio button — returns 1 if selected'],
      ['tezzui.ui_slider(&ctx, x, y, w, min, max, val)','Horizontal slider — returns new value'],
      ['tezzui.ui_input(&ctx, x, y, w, h, placeholder)','Text input field'],
      ['tezzui.ui_badge(&ctx, x, y, text, color)','Colored badge/tag'],
      ['tezzui.ui_stat(&ctx, x, y, w, h, val, label, color)','Stat card'],
      ['tezzui.ui_toast(&ctx, text, color)','Toast notification'],
      ['tezzui.ui_sep(&ctx, x, y, w)','Horizontal separator line'],
      ['tezzui.ui_card(&ctx, x, y, w, h)','Card background rectangle'],
      ['tezzui.ui_loader(&ctx, x, y, w)','Animated loading bar'],
      ['tezzui.ui_dropdown(&ctx, x, y, w, label)','Dropdown button'],
      ['tezzui.ui_taskbar(&ctx, title)','Window taskbar at bottom'],
      ['tezzui.ui_nav(&ctx, x, y, w, icon, active)','Sidebar navigation item'],
      ['tezzui._R(x,y,w,h,color)','Draw filled rectangle'],
      ['tezzui._RR(x,y,w,h,color,r)','Draw rounded rectangle'],
      ['tezzui._T(x,y,text,color)','Draw text (small)'],
      ['tezzui._TX(x,y,text,color,scale,bold)','Draw text (scaled)'],
    ],
    'import "tezzui"

fn main() -> int:
  ctx: TzUI
  unsafe:
    tezzui.ui_create(&ctx, "My App", 800, 600)
    while tezzui.ui_alive(&ctx) != 0:
      tezzui.ui_begin(&ctx)
      tezzui.ui_label(&ctx, 20, 20, "Hello!", 0xFFFFFF, 2, 1)
      if tezzui.ui_button(&ctx, 20, 60, 120, 30, "Click Me") != 0:
        say "Clicked!"
      tezzui.ui_end(&ctx)
  ret 0'],
  'tezzdb'    => ['TezzDB',   '🗄️', 'Embedded database and query engine',
    'A zero-setup embedded key-value database. Store structured data without any external server, DLL, or configuration.',
    [
      ['tezzdb.open(path)','Open or create a database file'],
      ['tezzdb.set(db, key, value)','Set a string key-value pair'],
      ['tezzdb.get(db, key)','Get value by key (returns str)'],
      ['tezzdb.del(db, key)','Delete a key'],
      ['tezzdb.close(db)','Close and flush the database'],
    ],
    'import "tezzdb"

fn main():
  db:*TezzDB = tezzdb.open("data.tdb")
  tezzdb.set(db, "name", "TezzNative")
  val:str = tezzdb.get(db, "name")
  say val
  tezzdb.close(db)'],
  'tezzserve' => ['TezzServe','🌐', 'HTTP server and REST API framework',
    'Build production HTTP servers with routing, middleware, and JSON support — pure TezzNative, zero external dependencies.',
    [
      ['tezzserve.listen(port, handler)','Start HTTP server on port'],
      ['tezzserve.response(ctx, status, body)','Send an HTTP response'],
      ['tezzserve.header(ctx, key, value)','Set a response header'],
      ['tezzserve.method(ctx)','Get request method (GET/POST…)'],
      ['tezzserve.path(ctx)','Get request URL path'],
      ['tezzserve.body(ctx)','Get request body as str'],
    ],
    'import "tezzserve"

fn handler(ctx:*ServeCtx):
  tezzserve.header(ctx, "Content-Type", "text/plain")
  tezzserve.response(ctx, 200, "Hello from TezzNative!")

fn main():
  say "Listening on :8080"
  tezzserve.listen(8080, handler)'],
  'tezzapi'   => ['TezzAPI',  '⚡', 'Native HTTP client with TLS support',
    'Make secure HTTPS requests and consume REST APIs from any TezzNative application.',
    [
      ['tezzapi.get(url)','HTTP GET — returns response str'],
      ['tezzapi.post(url, body)','HTTP POST with body'],
      ['tezzapi.status(resp)','Get HTTP status code'],
      ['tezzapi.body(resp)','Get response body'],
    ],
    'import "tezzapi"

fn main():
  resp:*ApiResp = tezzapi.get("https://api.example.com/data")
  say tezzapi.status(resp)
  say tezzapi.body(resp)'],
  'tsm'       => ['TSM',      '📦', 'TezzNative system module manager',
    'Manage system-level modules, package resolution, and version pinning with the TezzNative module system.',
    [
      ['tezz.mod','Project manifest — lists module_root and dep.<name>'],
      ['tezz.lock','Lock file — pins exact module versions'],
      ['dep.<name> = <version>','Declare a dependency'],
      ['module_root = lib','Set the local module search root'],
    ],
    '# tezz.mod — project manifest
module_root = lib
dep.mymodule = 1.0.0

# tezz.lock — version pins
mymodule@1.0.0 https://...'],
];
$fw = $frames[$slug] ?? null;
if(!$fw){
  // Unknown slug — redirect to list
  header('Location: /frameworks/'); exit;
}
[$name, $icon, $tagline, $desc, $api, $example] = $fw;
require_once __DIR__ . '/../includes/nav.php';
?>
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title><?= htmlspecialchars($name) ?> — TezzNative Framework Reference</title>
  <meta name="description" content="<?= htmlspecialchars($tagline) ?> — TezzNative framework documentation.">
  <meta name="robots" content="index, follow">
  <link rel="canonical" href="https://tn.tezzcorp.com/frameworks/<?= $slug ?>/">
  <style>
  .fw-hero{padding:70px 50px 50px;display:flex;align-items:center;gap:24px}
  .fw-hero-icon{font-size:56px}
  .fw-hero h1{font-size:clamp(28px,4vw,42px);font-weight:900;color:var(--white);letter-spacing:-1px;margin-bottom:6px}
  .fw-hero .tagline{color:var(--green);font-size:16px;font-weight:600;text-transform:uppercase;letter-spacing:.05em}
  .fw-hero p{color:var(--muted);font-size:16px;max-width:600px;margin-top:10px;line-height:1.7}
  .fw-body{max-width:960px;margin:0 auto;padding:0 50px 80px}
  .fw-body h2{font-size:22px;font-weight:700;color:var(--white);margin:44px 0 16px;padding-bottom:10px;border-bottom:1px solid var(--border)}
  .api-table{width:100%;border-collapse:collapse;margin:16px 0}
  .api-table th{text-align:left;padding:10px 14px;background:var(--bg3);color:var(--dim);font-size:12px;font-weight:700;text-transform:uppercase;letter-spacing:.05em}
  .api-table td{padding:11px 14px;border-top:1px solid var(--border);font-size:14px;color:var(--muted);vertical-align:top}
  .api-table td:first-child{font-family:var(--mono);color:var(--green);white-space:nowrap}
  .breadcrumb{padding:16px 50px 0;font-size:14px;color:var(--dim)}
  .breadcrumb a{color:var(--dim)}
  .breadcrumb a:hover{color:var(--green)}
  @media(max-width:768px){
    .fw-hero{flex-direction:column;align-items:flex-start;padding:40px 20px 30px}
    .fw-body,.breadcrumb{padding-left:20px;padding-right:20px}
  }
  </style>
</head>
<body>
<?php tn_nav('frameworks'); ?>

<div class="breadcrumb">
  <a href="/frameworks/">Frameworks</a> / <?= htmlspecialchars($name) ?>
</div>

<section class="fw-hero">
  <div class="fw-hero-icon"><?= $icon ?></div>
  <div>
    <div class="tagline"><?= htmlspecialchars($tagline) ?></div>
    <h1><?= htmlspecialchars($name) ?></h1>
    <p><?= htmlspecialchars($desc) ?></p>
  </div>
</section>

<div class="fw-body">
  <h2>Quick Example</h2>
  <pre><?= htmlspecialchars($example) ?></pre>

  <h2>API Reference</h2>
  <table class="api-table">
    <thead><tr><th>Function / Key</th><th>Description</th></tr></thead>
    <tbody>
      <?php foreach($api as [$fn, $d]): ?>
      <tr><td><?= htmlspecialchars($fn) ?></td><td><?= htmlspecialchars($d) ?></td></tr>
      <?php endforeach; ?>
    </tbody>
  </table>

  <h2>Import</h2>
  <pre><span class="kw">import</span> <span class="st">"<?= htmlspecialchars(strtolower($name)) ?>"</span></pre>
</div>

<?php tn_footer(); ?>
</body>
</html>
