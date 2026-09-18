<?php
declare(strict_types=1);
$page_title = 'News & Announcements — TezzCorp Pvt Ltd';
$page_desc  = 'Latest news, press releases, and company announcements from TezzCorp Pvt Ltd — Bihar\'s leading technology company.';
$active_nav = 'news';
include 'includes/header.php';

$news_items = [
  [
    'date' => 'May 2026',
    'category' => 'Product Launch',
    'cat_color' => 'var(--blue)',
    'cat_bg' => 'rgba(59,130,246,.12)',
    'icon' => '🚀',
    'title' => 'TezzNative v1.0.0 Released — A Systems Programming Language Made in India',
    'excerpt' => 'TezzCorp officially releases TezzNative v1.0.0 for Windows x64. The production-grade systems programming language features a single-pass compiler, <50ms build times, built-in native GUI, and a complete ecosystem of zero-dependency frameworks.',
    'link' => 'blog/index.php',
  ],
  [
    'date' => 'April 2026',
    'category' => 'AI / Product',
    'cat_color' => 'var(--indigo)',
    'cat_bg' => 'rgba(99,102,241,.12)',
    'icon' => '🤖',
    'title' => 'TezzLLM v2.0 Enterprise — Federated On-Device AI for Corporate Environments',
    'excerpt' => 'TezzLLM Enterprise v2.0 launches with 1M+ parameters, 8-trainer parallel federated training, RoPE positional encoding, SwiGLU feedforward networks, and full corporate deployment support. Zero dependencies, 64MB RAM minimum.',
    'link' => 'https://ai.tezzcorp.com',
  ],
  [
    'date' => 'March 2026',
    'category' => 'Company',
    'cat_color' => 'var(--green)',
    'cat_bg' => 'rgba(16,185,129,.12)',
    'icon' => '🏢',
    'title' => 'TezzCorp Pvt Ltd Incorporated — CIN: U62011BR2026PTC083413',
    'excerpt' => 'TezzCorp Pvt Ltd is officially incorporated in Bihar, India (Jhiliya, Bettiah) under the Companies Act, 2013. The company formalizes its software engineering, AI research, and product development operations.',
    'link' => 'about.php',
  ],
  [
    'date' => 'February 2026',
    'category' => 'Framework',
    'cat_color' => 'var(--cyan)',
    'cat_bg' => 'rgba(6,182,212,.12)',
    'icon' => '🗄️',
    'title' => 'TezzDB — Embedded SQL Database Engine Ships with TezzNative Ecosystem',
    'excerpt' => 'TezzDB, a fully embedded SQL-compatible database engine with ACID transaction support and B-tree storage, is released as part of the TezzNative standard ecosystem — no external SQLite or database runtimes needed.',
    'link' => 'https://tn.tezzcorp.com/frameworks/tezzdb.php',
  ],
  [
    'date' => 'January 2026',
    'category' => 'Framework',
    'cat_color' => 'var(--purple)',
    'cat_bg' => 'rgba(139,92,246,.12)',
    'icon' => '🖥️',
    'title' => 'TezzUI Native GUI Toolkit — Win32 Desktop Apps in Pure TezzNative',
    'excerpt' => 'TezzUI releases as a stable native GUI framework for building full-featured Windows desktop applications using only TezzNative. Supports windows, controls, dialogs, custom rendering, and message loop management.',
    'link' => 'https://tn.tezzcorp.com/frameworks/tezzui.php',
  ],
  [
    'date' => 'December 2025',
    'category' => 'Milestone',
    'cat_color' => 'var(--orange)',
    'cat_bg' => 'rgba(245,158,11,.12)',
    'icon' => '🎯',
    'title' => '200+ Software Projects Delivered Across India — TezzCorp Milestone',
    'excerpt' => 'TezzCorp reaches the milestone of 200+ delivered software projects across Bihar and India. Projects span school ERP systems, business CRM platforms, API gateways, and custom web portals for clients across sectors.',
    'link' => 'portfolio.php',
  ],
];
?>

<main id="main-content">
  <!-- Page Hero -->
  <section style="padding:4.5rem 0 3.5rem;border-bottom:1px solid var(--border)">
    <div class="container">
      <div class="eyebrow">Company</div>
      <h1 id="news-page-heading">News &amp; Announcements</h1>
      <p style="font-size:1.125rem;margin-top:.75rem;max-width:560px">
        Latest updates, product launches, milestones, and official announcements from TezzCorp Pvt Ltd.
      </p>
    </div>
  </section>

  <!-- News List -->
  <section class="section" aria-labelledby="news-page-heading">
    <div class="container">
      <div style="max-width:800px;margin:0 auto;display:flex;flex-direction:column;gap:2rem">
        <?php foreach ($news_items as $i => $item): ?>
        <article
          class="card"
          id="news-item-<?= $i + 1 ?>"
          aria-labelledby="news-title-<?= $i + 1 ?>"
          style="display:flex;gap:2rem;align-items:flex-start;flex-wrap:wrap"
        >
          <!-- Icon -->
          <div style="width:64px;height:64px;min-width:64px;border-radius:16px;background:var(--bg2);border:1px solid var(--border);display:flex;align-items:center;justify-content:center;font-size:1.75rem">
            <?= $item['icon'] ?>
          </div>
          <!-- Body -->
          <div style="flex:1;min-width:200px">
            <div style="display:flex;align-items:center;gap:.75rem;margin-bottom:.75rem;flex-wrap:wrap">
              <span style="font-size:.6875rem;font-weight:700;text-transform:uppercase;letter-spacing:.08em;padding:.2rem .5rem;border-radius:4px;background:<?= $item['cat_bg'] ?>;color:<?= $item['cat_color'] ?>">
                <?= htmlspecialchars($item['category']) ?>
              </span>
              <span style="font-size:.8125rem;color:var(--dim)"><?= htmlspecialchars($item['date']) ?></span>
            </div>
            <h2 id="news-title-<?= $i + 1 ?>" style="font-size:1.1875rem;margin-bottom:.625rem;font-family:var(--font2)">
              <?= htmlspecialchars($item['title']) ?>
            </h2>
            <p style="font-size:.9rem;color:var(--muted);margin-bottom:1rem">
              <?= htmlspecialchars($item['excerpt']) ?>
            </p>
            <a href="<?= htmlspecialchars($item['link']) ?>"
               <?= str_starts_with($item['link'], 'http') ? 'target="_blank" rel="noopener"' : '' ?>
               class="btn btn-ghost" style="padding:.375rem 0;color:var(--blue);font-size:.875rem">
              Read More <i class="material-icons" aria-hidden="true" style="font-size:.9rem;vertical-align:middle;margin-left:0.2rem">arrow_forward</i>
            </a>
          </div>
        </article>
        <?php endforeach; ?>
      </div>
    </div>
  </section>

  <!-- Newsletter CTA -->
  <section class="section-sm">
    <div class="container">
      <div class="cta-box" style="max-width:640px;margin:0 auto">
        <div class="eyebrow" style="color:var(--blue)">Stay Updated</div>
        <h2 style="font-size:1.75rem">Never miss an update</h2>
        <p>Subscribe to TezzCorp announcements and get notified first about product launches and milestones.</p>
        <form style="display:flex;gap:.75rem;flex-wrap:wrap;justify-content:center;margin-top:1.5rem" onsubmit="return false" id="news-newsletter">
          <input type="email" placeholder="your@email.com" aria-label="Your email address"
            style="flex:1;min-width:220px;padding:.6875rem 1rem;background:var(--bg3);border:1px solid var(--border2);border-radius:var(--radius);color:var(--text);font-size:.9375rem;font-family:var(--font);outline:none">
          <button class="btn btn-primary" type="submit" id="news-subscribe-btn">
            <i class="material-icons" aria-hidden="true" style="margin-right:0.3rem;font-size:1.1rem;vertical-align:middle">notifications</i> Subscribe
          </button>
        </form>
      </div>
    </div>
  </section>
</main>
<script>
document.getElementById('news-newsletter').addEventListener('submit',function(e){
  e.preventDefault();
  var i=this.querySelector('input');
  var b=document.getElementById('news-subscribe-btn');
  if(i.value){
    b.innerHTML='<i class="material-icons" aria-hidden="true" style="margin-right:0.3rem">check</i> Subscribed!';
    i.value='';setTimeout(function(){b.innerHTML='<i class="material-icons" aria-hidden="true" style="margin-right:0.3rem">notifications</i> Subscribe';},3000);
  }
});
</script>
<?php include 'includes/footer.php'; ?>
