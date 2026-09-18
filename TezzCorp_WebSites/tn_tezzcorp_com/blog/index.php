<?php
declare(strict_types=1);
$page_title = 'TezzCorp Blog — Technology, Engineering & Company Updates';
$page_desc  = 'Articles on systems programming, AI, web engineering, and business technology from TezzCorp Pvt Ltd and Rohit Pathak.';
$active_nav = 'blog';
include '../includes/header.php';

$posts = [
  [
    'slug' => 'tezznative-v1-launch',
    'emoji' => '🚀',
    'cat' => 'Language',
    'cat_c' => 'var(--blue)',
    'cat_bg' => 'rgba(59,130,246,.12)',
    'date' => 'May 15, 2026',
    'author' => 'Rohit Pathak',
    'read' => '8 min',
    'title' => 'Introducing TezzNative v1.0 — A New Era of Systems Programming from India',
    'excerpt' => 'TezzNative reaches its first stable release: a high-performance compiled language with a single-pass compiler, <50ms build times, native Win32 GUI, and a full ecosystem — all zero-dependency. Here\'s the story behind it.',
  ],
  [
    'slug' => 'tezzllm-federated-ai',
    'emoji' => '🤖',
    'cat' => 'AI / ML',
    'cat_c' => 'var(--indigo)',
    'cat_bg' => 'rgba(99,102,241,.12)',
    'date' => 'April 28, 2026',
    'author' => 'Rohit Pathak',
    'read' => '12 min',
    'title' => 'How TezzLLM Achieves Zero-Dependency On-Device AI with Federated Training',
    'excerpt' => 'A deep technical dive into the architecture of TezzLLM v2.0: byte-level BPE tokenization, RoPE positional encoding, SwiGLU feedforward networks, federated averaging across 8 parallel trainers, and the design decisions behind a 1M-parameter model that runs in 64MB of RAM.',
  ],
  [
    'slug' => 'school-erp-bihar',
    'emoji' => '🏫',
    'cat' => 'Case Study',
    'cat_c' => 'var(--green)',
    'cat_bg' => 'rgba(16,185,129,.12)',
    'date' => 'March 10, 2026',
    'author' => 'Rohit Pathak',
    'read' => '6 min',
    'title' => 'Building a Full School ERP for Bihar — From Inception to Deployment in 30 Days',
    'excerpt' => 'How TezzCorp designed and deployed a complete school ERP system — student admissions, fee collection, staff payroll, attendance tracking, and exam management — for a network of Bihar schools under tight deadlines.',
  ],
  [
    'slug' => 'why-zero-dependencies',
    'emoji' => '⚡',
    'cat' => 'Engineering',
    'cat_c' => 'var(--orange)',
    'cat_bg' => 'rgba(245,158,11,.12)',
    'date' => 'February 20, 2026',
    'author' => 'Rohit Pathak',
    'read' => '10 min',
    'title' => 'Why Zero Dependencies Should Be Your Default — The Case for TezzNative\'s Philosophy',
    'excerpt' => 'Modern software engineering has a dependency problem. TezzNative was built around the radical idea that an entire production stack — compiler, GUI, network, database, AI — should ship as a single binary with no external requirements. Here\'s why.',
  ],
  [
    'slug' => 'crm-sales-automation',
    'emoji' => '📊',
    'cat' => 'Business',
    'cat_c' => 'var(--cyan)',
    'cat_bg' => 'rgba(6,182,212,.12)',
    'date' => 'January 15, 2026',
    'author' => 'Rohit Pathak',
    'read' => '5 min',
    'title' => 'CRM vs. Manual Sales: How Automation Doubled Revenue for Our Clients',
    'excerpt' => 'A breakdown of how TezzCorp\'s custom CRM implementations — with pipeline automation, lead scoring, and follow-up triggers — delivered 2x revenue improvement for small and medium businesses in India.',
  ],
  [
    'slug' => 'tezzdb-architecture',
    'emoji' => '🗄️',
    'cat' => 'Engineering',
    'cat_c' => 'var(--purple)',
    'cat_bg' => 'rgba(139,92,246,.12)',
    'date' => 'December 8, 2025',
    'author' => 'Rohit Pathak',
    'read' => '9 min',
    'title' => 'TezzDB Deep Dive — Building an ACID-Compliant Embedded Database in Pure TezzNative',
    'excerpt' => 'The internals of TezzDB: B-tree storage, write-ahead logging for ACID compliance, single-file database format, and how we achieved SQLite-level reliability without touching a line of C or Rust.',
  ],
];
?>
<main id="main-content">
  <!-- Hero -->
  <section style="padding:4.5rem 0 3.5rem;border-bottom:1px solid var(--border)">
    <div class="container">
      <div style="display:flex;align-items:flex-end;justify-content:space-between;flex-wrap:wrap;gap:1rem">
        <div>
          <div class="eyebrow">TezzCorp Blog</div>
          <h1 id="blog-page-heading">Technology, Engineering &amp; Updates</h1>
          <p style="font-size:1.0625rem;margin-top:.75rem;max-width:520px">
            Insights on systems programming, AI/ML, software architecture, and business technology from the TezzCorp team.
          </p>
        </div>
        <div style="display:flex;gap:.75rem;align-items:center">
          <form role="search" style="display:flex;gap:.5rem" onsubmit="return false" id="blog-search-form">
            <input type="search" placeholder="Search articles..." aria-label="Search blog posts"
              id="blog-search-input"
              style="padding:.5625rem 1rem;background:var(--bg2);border:1px solid var(--border);border-radius:var(--radius);color:var(--text);font-size:.875rem;font-family:var(--font);outline:none;width:220px;transition:border-color .2s"
              onfocus="this.style.borderColor='var(--blue)'" onblur="this.style.borderColor='var(--border)'">
            <button class="btn btn-secondary" type="submit" id="blog-search-btn">
              <i class="material-icons" aria-hidden="true" style="font-size:1.1rem">search</i>
            </button>
          </form>
        </div>
      </div>
    </div>
  </section>

  <!-- Posts Grid -->
  <section class="section" aria-labelledby="blog-page-heading">
    <div class="container">
      <div class="blog-grid" id="blog-posts-grid">
        <?php foreach ($posts as $i => $p): ?>
        <article class="blog-card" id="blog-post-<?= $i + 1 ?>" aria-labelledby="blog-title-<?= $i + 1 ?>" data-title="<?= htmlspecialchars(strtolower($p['title'])) ?>">
          <div class="blog-card-img" aria-hidden="true" style="font-size:2.5rem"><?= $p['emoji'] ?></div>
          <div class="blog-card-body">
            <div class="blog-meta">
              <span class="blog-category" style="background:<?= $p['cat_bg'] ?>;color:<?= $p['cat_c'] ?>">
                <?= htmlspecialchars($p['cat']) ?>
              </span>
              <span class="blog-date"><?= htmlspecialchars($p['date']) ?></span>
              <span class="blog-date">· <?= $p['read'] ?> read</span>
            </div>
            <h3 id="blog-title-<?= $i + 1 ?>"><?= htmlspecialchars($p['title']) ?></h3>
            <p><?= htmlspecialchars($p['excerpt']) ?></p>
            <div style="display:flex;align-items:center;justify-content:space-between;margin-top:1.25rem;flex-wrap:wrap;gap:.5rem">
              <span style="font-size:.8125rem;color:var(--dim);display:inline-flex;align-items:center">
                <i class="material-icons" aria-hidden="true" style="font-size:0.9rem;margin-right:0.25rem">person</i> <?= htmlspecialchars($p['author']) ?>
              </span>
              <a href="<?= htmlspecialchars($p['slug']) ?>.php" class="blog-card-link" id="blog-read-<?= $i + 1 ?>">
                Read Article →
              </a>
            </div>
          </div>
        </article>
        <?php endforeach; ?>
      </div>
      <p id="blog-no-results" style="display:none;text-align:center;color:var(--dim);padding:3rem 0">
        No articles found for your search.
      </p>
    </div>
  </section>

  <!-- Newsletter CTA -->
  <section class="section-sm" style="background:var(--bg2);border-top:1px solid var(--border)">
    <div class="container">
      <div class="cta-box" style="max-width:640px;margin:0 auto">
        <div class="eyebrow" style="color:var(--blue)">Get the Newsletter</div>
        <h2 style="font-size:1.75rem">Fresh articles every week</h2>
        <p>Subscribe and get TezzCorp's latest articles on systems programming, AI, and business technology delivered directly to your inbox.</p>
        <form style="display:flex;gap:.75rem;flex-wrap:wrap;justify-content:center;margin-top:1.5rem" onsubmit="return false" id="blog-newsletter-form">
          <input type="email" placeholder="your@email.com" aria-label="Email for newsletter"
            style="flex:1;min-width:220px;padding:.6875rem 1rem;background:var(--bg3);border:1px solid var(--border2);border-radius:var(--radius);color:var(--text);font-size:.9375rem;font-family:var(--font);outline:none">
          <button class="btn btn-primary" type="submit" id="blog-subscribe-btn">
            <i class="material-icons" aria-hidden="true" style="font-size:1.1rem;vertical-align:middle;margin-right:0.2rem">send</i> Subscribe
          </button>
        </form>
      </div>
    </div>
  </section>
</main>

<script>
// Live search
var searchInput = document.getElementById('blog-search-input');
var noResults = document.getElementById('blog-no-results');
searchInput && searchInput.addEventListener('input', function() {
  var q = this.value.toLowerCase().trim();
  var cards = document.querySelectorAll('#blog-posts-grid .blog-card');
  var found = 0;
  cards.forEach(function(c) {
    var title = c.dataset.title || '';
    var show = !q || title.includes(q);
    c.style.display = show ? '' : 'none';
    if (show) found++;
  });
  noResults.style.display = found === 0 ? 'block' : 'none';
});

// Newsletter
document.getElementById('blog-newsletter-form').addEventListener('submit', function(e) {
  e.preventDefault();
  var i = this.querySelector('input');
  var b = document.getElementById('blog-subscribe-btn');
  if (i.value) {
    b.innerHTML = '<i class="material-icons" aria-hidden="true" style="font-size:1.1rem;vertical-align:middle;margin-right:0.2rem">check</i> Subscribed!';
    i.value = '';
    setTimeout(function() { b.innerHTML = '<i class="material-icons" aria-hidden="true" style="font-size:1.1rem;vertical-align:middle;margin-right:0.2rem">send</i> Subscribe'; }, 3000);
  }
});
</script>
<?php include '../includes/footer.php'; ?>
