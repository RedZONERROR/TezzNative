<?php
declare(strict_types=1);
/**
 * community.php — TezzNative Community Hub (Production)
 * Database-backed community registry, leaderboard, channels, and guidelines.
 */

require_once __DIR__ . '/includes/nav.php';

$boot   = tn_boot('/community/');
$pdo    = $boot['pdo'];
$notice = '';
$noticeOk = false;
$memberCount = 0;

// Handle registration form POST
if (strtoupper((string)($_SERVER['REQUEST_METHOD'] ?? 'GET')) === 'POST' && $pdo !== null) {
    try {
        $result   = tezz_register_user($pdo, $_POST);
        $notice   = (string)$result['message'];
        $noticeOk = (bool)$result['ok'];
    } catch (Throwable $e) {
        $notice = 'Community signup is temporarily unavailable. Please try again shortly.';
    }
}

// Fetch contributors + member count
$contributors = [];
try {
    if ($pdo !== null) {
        $contributors  = tezz_fetch_top_contributors($pdo, 9);
        $memberCount   = tezz_community_count($pdo);
    }
} catch (Throwable $_e) {}

$page_title  = 'TezzNative Community Hub';
$page_desc   = 'Join the TezzNative community. Register as an early developer, connect on GitHub Discussions, report bugs, and shape the language roadmap with TezzCorp Pvt Ltd.';
$active_nav  = 'community';
require_once __DIR__ . '/includes/header.php';
?>

<style>
/* ── Community Page Styles ─────────────────────────────────────────────────── */
.community-hero {
  text-align: center;
  padding: 72px 24px 56px;
  position: relative;
  overflow: hidden;
}
.community-hero::before {
  content: '';
  position: absolute;
  top: -80px; left: 50%; transform: translateX(-50%);
  width: 700px; height: 400px;
  background: radial-gradient(circle, rgba(255,153,51,.18) 0%, rgba(255,103,31,.08) 50%, transparent 70%);
  filter: blur(50px); pointer-events: none; z-index: 0;
}
.community-hero > * { position: relative; z-index: 1; }
.comm-pill {
  display: inline-flex; align-items: center; gap: 8px;
  padding: 6px 16px; border-radius: var(--radius-full);
  background: rgba(255,153,51,.12); border: 1px solid rgba(255,153,51,.35);
  font-size: .8rem; font-weight: 700; color: #ff9933;
  letter-spacing: .5px; text-transform: uppercase; margin-bottom: 20px;
}
.comm-hero-title {
  font-size: clamp(2rem, 5vw, 3.2rem);
  font-weight: 900; letter-spacing: -1px;
  line-height: 1.15; margin-bottom: 18px;
}
.comm-hero-lead {
  color: var(--text-secondary); font-size: 1.1rem;
  max-width: 640px; margin: 0 auto 36px; line-height: 1.7;
}
.comm-stats-row {
  display: flex; align-items: center; justify-content: center;
  flex-wrap: wrap; gap: 32px; margin-bottom: 0;
}
.comm-stat { display: flex; flex-direction: column; align-items: center; }
.comm-stat-val {
  font-size: 2rem; font-weight: 900; color: var(--text-primary);
  font-variant-numeric: tabular-nums;
}
.comm-stat-lbl { font-size: .8rem; color: var(--text-tertiary); font-weight: 500; }

/* ── Channel Cards ─────────────────────────────────────────────────────────── */
.comm-channels-section {
  padding: 56px 24px;
  background: var(--bg-surface);
  border-top: 1px solid var(--border-subtle);
  border-bottom: 1px solid var(--border-subtle);
}
.comm-channels-grid {
  max-width: 1200px; margin: 0 auto;
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 20px;
}
.ch-card {
  background: var(--bg-surface-raised);
  border: 1px solid var(--border-subtle);
  border-radius: var(--radius-lg);
  padding: 28px 24px; display: flex; flex-direction: column;
  transition: transform .25s ease, border-color .25s ease, box-shadow .25s ease;
}
.ch-card:hover {
  transform: translateY(-4px);
  border-color: var(--border-accent);
  box-shadow: 0 12px 28px rgba(0,0,0,.35);
}
.ch-icon { font-size: 2rem; margin-bottom: 14px; line-height: 1; }
.ch-title { font-size: 1.05rem; font-weight: 700; color: var(--text-primary); margin-bottom: 8px; }
.ch-desc { color: var(--text-secondary); font-size: .9rem; line-height: 1.6; flex: 1; }
.ch-link {
  display: inline-flex; align-items: center; gap: 6px;
  margin-top: 18px; padding: 9px 18px;
  background: var(--bg-surface-elevated);
  border: 1px solid var(--border-medium);
  border-radius: var(--radius-sm);
  color: var(--text-accent); font-weight: 600; font-size: .88rem;
  transition: all .2s ease; text-decoration: none;
}
.ch-link:hover {
  background: rgba(255,153,51,.1);
  border-color: var(--border-accent);
  color: #ff9933; text-decoration: none;
}

/* ── Community Main Grid ───────────────────────────────────────────────────── */
.comm-main-grid {
  max-width: 1200px; margin: 0 auto;
  padding: 56px 24px;
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 28px;
  align-items: start;
}
@media (max-width: 900px) {
  .comm-main-grid { grid-template-columns: 1fr; }
}

/* ── Registration Card ─────────────────────────────────────────────────────── */
.comm-card {
  background: var(--bg-surface);
  border: 1px solid var(--border-subtle);
  border-radius: var(--radius-lg);
  padding: 32px;
}
.comm-card-title {
  font-size: 1.3rem; font-weight: 800; color: var(--text-primary); margin-bottom: 8px;
}
.comm-card-lead {
  color: var(--text-secondary); font-size: .9rem; line-height: 1.6; margin-bottom: 24px;
}
.comm-form { display: flex; flex-direction: column; gap: 14px; }
.form-row { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
@media (max-width: 540px) { .form-row { grid-template-columns: 1fr; } }
.form-field { display: flex; flex-direction: column; gap: 6px; }
.form-label { font-size: .82rem; font-weight: 600; color: var(--text-secondary); }
.form-input, .form-textarea {
  padding: 10px 14px;
  border-radius: var(--radius-sm);
  background: var(--bg-surface-raised);
  border: 1px solid var(--border-subtle);
  color: var(--text-primary);
  font-family: var(--font-sans);
  font-size: .92rem;
  transition: border-color .2s ease, box-shadow .2s ease;
  outline: none; width: 100%;
}
.form-input:focus, .form-textarea:focus {
  border-color: rgba(255,153,51,.5);
  box-shadow: 0 0 0 3px rgba(255,153,51,.12);
}
.form-textarea { resize: vertical; min-height: 80px; line-height: 1.5; }
.notice {
  padding: 12px 16px; border-radius: var(--radius-sm);
  font-size: .9rem; font-weight: 500; margin-bottom: 4px;
}
.notice-ok { background: rgba(16,185,129,.12); color: #10b981; border: 1px solid rgba(16,185,129,.3); }
.notice-err { background: rgba(239,68,68,.1); color: #f87171; border: 1px solid rgba(239,68,68,.25); }
.form-hint { font-size: .78rem; color: var(--text-tertiary); }

/* ── Leaderboard Card ──────────────────────────────────────────────────────── */
.member-list { list-style: none; padding: 0; display: flex; flex-direction: column; gap: 10px; }
.member-item {
  display: flex; align-items: center; gap: 14px;
  padding: 12px 14px;
  background: var(--bg-surface-raised);
  border: 1px solid var(--border-subtle);
  border-radius: var(--radius-md);
  transition: border-color .2s ease;
}
.member-item:hover { border-color: var(--border-medium); }
.member-avatar {
  width: 38px; height: 38px; border-radius: 50%;
  background: var(--brand-gradient);
  display: flex; align-items: center; justify-content: center;
  font-size: 1rem; font-weight: 800; color: white;
  flex-shrink: 0; text-transform: uppercase;
}
.member-info { flex: 1; min-width: 0; }
.member-name { font-weight: 700; color: var(--text-primary); font-size: .95rem; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.member-joined { font-size: .78rem; color: var(--text-tertiary); }
.member-role {
  font-size: .72rem; font-weight: 700; padding: 3px 8px;
  border-radius: var(--radius-full); flex-shrink: 0;
}
.role-developer { background: rgba(56,189,248,.15); color: #38bdf8; }
.role-contributor { background: rgba(255,153,51,.15); color: #ff9933; }
.role-tester { background: rgba(167,139,250,.15); color: #a78bfa; }
.role-founder { background: rgba(16,185,129,.15); color: #10b981; }
.empty-state {
  padding: 32px; text-align: center;
  background: var(--bg-surface-raised); border-radius: var(--radius-md);
  border: 2px dashed var(--border-subtle); color: var(--text-tertiary);
}
.empty-state .es-icon { font-size: 2.5rem; margin-bottom: 12px; }
.empty-state p { font-size: .92rem; }

/* ── Guidelines ────────────────────────────────────────────────────────────── */
.guidelines-section {
  background: var(--bg-surface);
  border-top: 1px solid var(--border-subtle);
  padding: 56px 24px;
}
.guidelines-inner { max-width: 860px; margin: 0 auto; }
.gl-header { text-align: center; margin-bottom: 40px; }
.gl-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(260px, 1fr)); gap: 20px; }
.gl-item {
  display: flex; gap: 16px;
  padding: 20px; border-radius: var(--radius-md);
  background: var(--bg-surface-raised); border: 1px solid var(--border-subtle);
}
.gl-num {
  font-size: 1.8rem; font-weight: 900; color: #ff9933;
  font-family: var(--font-mono); flex-shrink: 0; width: 36px;
  line-height: 1;
}
.gl-text h3 { font-size: .95rem; font-weight: 700; color: var(--text-primary); margin-bottom: 4px; }
.gl-text p { font-size: .85rem; color: var(--text-secondary); margin: 0; line-height: 1.6; }
</style>

<!-- Community Hero -->
<section class="community-hero">
  <div class="comm-pill">
    <span style="width:8px;height:8px;border-radius:50%;background:#10b981;box-shadow:0 0 8px #10b981;animation:pulse 2s infinite;"></span>
    Community Hub
  </div>
  <h1 class="comm-hero-title">
    Build Together with<br>
    <span style="background:var(--brand-gradient);-webkit-background-clip:text;-webkit-text-fill-color:transparent;">TezzNative Developers</span>
  </h1>
  <p class="comm-hero-lead">
    Join the official TezzNative developer registry. Connect with engineers building native AI, systems software, and real-time applications using TezzNative at TezzCorp Pvt Ltd.
  </p>
  <div class="comm-stats-row">
    <div class="comm-stat">
      <span class="comm-stat-val" id="statMembers"><?= number_format($memberCount ?: 0) ?></span>
      <span class="comm-stat-lbl">Registered Builders</span>
    </div>
    <div class="comm-stat">
      <span class="comm-stat-val">40+</span>
      <span class="comm-stat-lbl">Native Packages</span>
    </div>
    <div class="comm-stat">
      <span class="comm-stat-val">v2.2.1</span>
      <span class="comm-stat-lbl">Current Release</span>
    </div>
    <div class="comm-stat">
      <span class="comm-stat-val">Open</span>
      <span class="comm-stat-lbl">GitHub Issues</span>
    </div>
  </div>
</section>

<!-- Community Channels -->
<section class="comm-channels-section">
  <div style="max-width:1200px;margin:0 auto 36px;text-align:center;">
    <span class="section-pill">CONNECT & CONTRIBUTE</span>
    <h2 class="section-title" style="margin-top:12px;">Community Channels</h2>
    <p class="section-desc">Multiple ways to engage — pick what works best for you.</p>
  </div>
  <div class="comm-channels-grid">
    <div class="ch-card">
      <div class="ch-icon">💬</div>
      <h3 class="ch-title">GitHub Discussions</h3>
      <p class="ch-desc">Ask questions, share projects, post ideas, and get answers from the core TezzCorp team and community members.</p>
      <a href="https://github.com/TezzCorp/TezzNative/discussions" class="ch-link" target="_blank" rel="noopener">Open Discussions →</a>
    </div>
    <div class="ch-card">
      <div class="ch-icon">🐛</div>
      <h3 class="ch-title">GitHub Issues</h3>
      <p class="ch-desc">Found a bug? Want a new stdlib module or language feature? Open an issue and the core team will triage it promptly.</p>
      <a href="https://github.com/TezzCorp/TezzNative/issues" class="ch-link" target="_blank" rel="noopener">Report a Bug →</a>
    </div>
    <div class="ch-card">
      <div class="ch-icon">📦</div>
      <h3 class="ch-title">GitHub Repository</h3>
      <p class="ch-desc">Browse the TezzNative compiler source, standard library, examples, and contribute pull requests to improve the language.</p>
      <a href="https://github.com/TezzCorp/TezzNative" class="ch-link" target="_blank" rel="noopener">View on GitHub →</a>
    </div>
    <div class="ch-card">
      <div class="ch-icon">📖</div>
      <h3 class="ch-title">Documentation</h3>
      <p class="ch-desc">The official TezzNative language reference with verified working examples for every feature, type, stdlib module, and GUI API.</p>
      <a href="/docs/" class="ch-link">Read the Docs →</a>
    </div>
    <div class="ch-card">
      <div class="ch-icon">📝</div>
      <h3 class="ch-title">Changelog & Releases</h3>
      <p class="ch-desc">Stay up to date with every version release. View what changed, new stdlib additions, and breaking changes per release.</p>
      <a href="/versions/" class="ch-link">View Releases →</a>
    </div>
    <div class="ch-card">
      <div class="ch-icon">📧</div>
      <h3 class="ch-title">Direct Support</h3>
      <p class="ch-desc">For enterprise inquiries, licensing questions, or priority technical support — contact the TezzCorp engineering team.</p>
      <a href="/support.php" class="ch-link">Get Support →</a>
    </div>
  </div>
</section>

<!-- Registration + Leaderboard -->
<section style="background: var(--bg-base);">
  <div class="comm-main-grid">

    <!-- Registration Card -->
    <article class="comm-card">
      <h2 class="comm-card-title">🚀 Join the Official Registry</h2>
      <p class="comm-card-lead">Create your TezzNative community profile. Track your contributions, appear on the leaderboard, and get notified on new releases.</p>

      <?php if ($notice !== ''): ?>
      <div class="notice <?= $noticeOk ? 'notice-ok' : 'notice-err' ?>">
        <?= htmlspecialchars($notice) ?>
      </div>
      <?php endif; ?>

      <?php if (!$noticeOk): ?>
      <form class="comm-form" id="communityForm" method="post" novalidate>
        <div class="form-row">
          <div class="form-field">
            <label class="form-label" for="display_name">Full Name</label>
            <input class="form-input" type="text" id="display_name" name="display_name"
              placeholder="Rohit Pathak" maxlength="100" required autocomplete="name"
              value="<?= htmlspecialchars((string)($_POST['display_name'] ?? '')) ?>">
          </div>
          <div class="form-field">
            <label class="form-label" for="email">Email Address</label>
            <input class="form-input" type="email" id="email" name="email"
              placeholder="you@example.com" maxlength="190" required autocomplete="email"
              value="<?= htmlspecialchars((string)($_POST['email'] ?? '')) ?>">
          </div>
        </div>
        <div class="form-field">
          <label class="form-label" for="password">Password <span class="form-hint">(min. 8 characters)</span></label>
          <input class="form-input" type="password" id="password" name="password"
            placeholder="Create a secure password" minlength="8" required autocomplete="new-password">
        </div>
        <div class="form-field">
          <label class="form-label" for="github_url">GitHub Profile URL <span class="form-hint">(optional)</span></label>
          <input class="form-input" type="url" id="github_url" name="github_url"
            placeholder="https://github.com/yourusername"
            value="<?= htmlspecialchars((string)($_POST['github_url'] ?? '')) ?>">
        </div>
        <div class="form-field">
          <label class="form-label" for="bio">What are you building with TezzNative? <span class="form-hint">(optional)</span></label>
          <textarea class="form-textarea" id="bio" name="bio" maxlength="1200" rows="3"
            placeholder="An AI inference engine, a native GUI app, a high-throughput HTTP server..."><?= htmlspecialchars((string)($_POST['bio'] ?? '')) ?></textarea>
        </div>
        <button class="btn btn-primary" type="submit" style="width:100%;margin-top:4px;" id="regSubmitBtn">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M16 21v-2a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4v2"/><circle cx="9" cy="7" r="4"/><line x1="19" y1="8" x2="19" y2="14"/><line x1="22" y1="11" x2="16" y2="11"/></svg>
          Create Community Profile
        </button>
        <p class="form-hint" style="text-align:center;">
          By registering you agree to our <a href="/terms.php" style="color:var(--text-accent);">Terms of Service</a>. Your email is never shared.
        </p>
      </form>
      <?php else: ?>
      <div style="text-align:center;padding:32px 0;">
        <div style="font-size:3rem;margin-bottom:16px;">🎉</div>
        <p style="color:var(--text-primary);font-weight:700;font-size:1.1rem;margin-bottom:8px;">You're officially registered!</p>
        <p style="color:var(--text-secondary);font-size:.9rem;">Check your email and explore the community channels above.</p>
      </div>
      <?php endif; ?>
    </article>

    <!-- Active Builders Leaderboard -->
    <article class="comm-card">
      <h2 class="comm-card-title">🏆 Active Builders</h2>
      <p class="comm-card-lead">Engineers developing native tools, AI pipelines, and systems software with TezzNative.</p>

      <?php if (empty($contributors)): ?>
      <div class="empty-state">
        <div class="es-icon">👾</div>
        <p><strong>Be the first builder!</strong><br>Register on the left to claim your spot on the leaderboard.</p>
      </div>
      <?php else: ?>
      <ul class="member-list">
        <?php foreach ($contributors as $i => $m): ?>
        <li class="member-item">
          <div class="member-avatar"><?= htmlspecialchars(mb_substr((string)$m['display_name'], 0, 1)) ?></div>
          <div class="member-info">
            <div class="member-name"><?= htmlspecialchars((string)$m['display_name']) ?></div>
            <div class="member-joined">Joined <?= htmlspecialchars((string)$m['joined']) ?></div>
          </div>
          <span class="member-role role-<?= htmlspecialchars((string)($m['role'] ?? 'developer')) ?>">
            <?= htmlspecialchars(ucfirst((string)($m['role'] ?? 'developer'))) ?>
          </span>
        </li>
        <?php endforeach; ?>
      </ul>
      <?php if ($memberCount > 9): ?>
      <p style="text-align:center;margin-top:16px;font-size:.84rem;color:var(--text-tertiary);">
        + <?= number_format($memberCount - 9) ?> more builders registered
      </p>
      <?php endif; ?>
      <?php endif; ?>
    </article>
  </div>
</section>

<!-- Community Guidelines -->
<section class="guidelines-section">
  <div class="guidelines-inner">
    <div class="gl-header">
      <span class="section-pill">COMMUNITY STANDARDS</span>
      <h2 class="section-title" style="margin-top:12px;">Community Guidelines</h2>
      <p class="section-desc">We keep things professional, respectful, and constructive.</p>
    </div>
    <div class="gl-grid">
      <div class="gl-item">
        <div class="gl-num">1</div>
        <div class="gl-text">
          <h3>Be Kind & Respectful</h3>
          <p>Treat everyone with respect. No harassment, hate speech, or personal attacks of any kind.</p>
        </div>
      </div>
      <div class="gl-item">
        <div class="gl-num">2</div>
        <div class="gl-text">
          <h3>Search Before Asking</h3>
          <p>Check the docs and existing GitHub Discussions before opening a new question to keep things organized.</p>
        </div>
      </div>
      <div class="gl-item">
        <div class="gl-num">3</div>
        <div class="gl-text">
          <h3>Post Reproducible Examples</h3>
          <p>When reporting bugs, include minimal <code>.tn</code> code that reproduces the issue and the exact error output.</p>
        </div>
      </div>
      <div class="gl-item">
        <div class="gl-num">4</div>
        <div class="gl-text">
          <h3>English First</h3>
          <p>Use English in public channels so every developer worldwide can understand and participate.</p>
        </div>
      </div>
      <div class="gl-item">
        <div class="gl-num">5</div>
        <div class="gl-text">
          <h3>No Self-Promotion Spam</h3>
          <p>Sharing your TezzNative projects is welcome; unsolicited advertising or off-topic promotion is not.</p>
        </div>
      </div>
      <div class="gl-item">
        <div class="gl-num">6</div>
        <div class="gl-text">
          <h3>Contribute Back</h3>
          <p>If you find a solution to a problem, document it. Help the next developer who hits the same wall.</p>
        </div>
      </div>
    </div>
  </div>
</section>

<!-- CTA Banner -->
<section style="text-align:center;padding:60px 24px;background:var(--bg-base);border-top:1px solid var(--border-subtle);">
  <div style="max-width:680px;margin:0 auto;">
    <h2 style="font-size:clamp(1.6rem,3vw,2.2rem);font-weight:900;margin-bottom:14px;">Ready to Build Something Native?</h2>
    <p style="color:var(--text-secondary);margin-bottom:28px;">Download TezzNative v2.2.1 and start compiling zero-overhead standalone AI and systems applications today.</p>
    <div style="display:flex;justify-content:center;gap:14px;flex-wrap:wrap;">
      <a href="/download/TezzNativeInstaller.exe" class="btn btn-primary btn-lg">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>
        Download Installer (.exe)
      </a>
      <a href="/docs/" class="btn btn-secondary btn-lg">Read the Docs</a>
    </div>
  </div>
</section>

<script>
// Community form client-side validation
(function() {
  var form = document.getElementById('communityForm');
  if (!form) return;

  form.addEventListener('submit', function(e) {
    var name  = form.querySelector('#display_name').value.trim();
    var email = form.querySelector('#email').value.trim();
    var pass  = form.querySelector('#password').value;
    var gh    = form.querySelector('#github_url') ? form.querySelector('#github_url').value.trim() : '';
    var errs  = [];

    if (name.length < 2)  errs.push('Name must be at least 2 characters.');
    if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email)) errs.push('Enter a valid email address.');
    if (pass.length < 8)  errs.push('Password must be at least 8 characters.');
    if (gh && !/^https?:\/\/.+/.test(gh)) errs.push('GitHub URL must start with https://');

    if (errs.length > 0) {
      e.preventDefault();
      var existing = form.querySelector('.js-validation-notice');
      if (existing) existing.remove();
      var notice = document.createElement('div');
      notice.className = 'notice notice-err js-validation-notice';
      notice.textContent = errs[0];
      form.insertBefore(notice, form.firstChild);
      notice.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
    } else {
      var btn = document.getElementById('regSubmitBtn');
      if (btn) {
        btn.disabled = true;
        btn.textContent = 'Registering…';
      }
    }
  });
})();
</script>

<?php require_once __DIR__ . '/includes/footer.php'; ?>
