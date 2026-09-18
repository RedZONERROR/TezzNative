<?php
declare(strict_types=1);
$page_title = 'Events at TezzCorp — Conferences, Keynotes, and Tech Sessions';
$page_desc  = 'Stay up to date with TezzCorp conferences, developer keynotes, and system seminars. Join our technical webinars and physical summits across India.';
$active_nav = 'events';
include 'includes/header.php';
?>

<main id="main-content">
  <!-- Page Hero -->
  <section style="padding: 4.5rem 0 3.5rem; border-bottom: 1px solid var(--border)">
    <div class="container">
      <div style="max-width: 720px">
        <div class="eyebrow" aria-label="Page category">Events</div>
        <h1 id="events-page-heading">News &amp; Developer <span class="gradient-text">Seminars</span></h1>
        <p style="font-size: 1.125rem; margin-top: 1rem">
          Join our technical conferences, interactive developer sessions, and bare-metal systems seminars. Connect with our engineering core and discover how to leverage TezzNative and TezzLLM in production.
        </p>
      </div>
    </div>
  </section>

  <!-- Featured Event -->
  <section class="section" aria-labelledby="featured-event-heading">
    <div class="container">
      <div class="section-head">
        <div class="eyebrow">Keynote Spotlight</div>
        <h2 id="featured-event-heading">Upcoming Technical Keynote</h2>
      </div>

      <div style="background: linear-gradient(135deg, rgba(99, 102, 241, 0.06) 0%, rgba(6, 182, 212, 0.06) 100%); border: 1px solid var(--border); border-radius: var(--radius-xl); padding: 3rem; margin-top: 2rem; position: relative; overflow: hidden" class="card">
        <div style="position: absolute; top: -100px; right: -100px; width: 300px; height: 300px; background: radial-gradient(circle, var(--monet-orb-a) 0%, transparent 70%); pointer-events: none" aria-hidden="true"></div>

        <div style="display: flex; justify-content: space-between; align-items: flex-start; flex-wrap: wrap; gap: 2rem; position: relative; z-index: 2">
          <div style="max-width: 580px">
            <span style="background: var(--gradient); color: #fff; padding: 6px 12px; border-radius: var(--radius-pill); font-size: 0.75rem; font-weight: 700; text-transform: uppercase; letter-spacing: 0.08em; display: inline-block; margin-bottom: 1.25rem">
              Developer Webinar
            </span>
            <h3 style="font-size: 2rem; font-weight: 800; margin-bottom: 1rem">TezzLLM: Practical Federated Training &amp; Run-Time Optimization</h3>
            <p style="font-size: 1.0625rem; margin-bottom: 1.5rem; color: var(--muted)">
              Learn how to configure your on-device language models for distributed weight averaging. We will demonstrate how to set up `tezzllm_parallel.exe`, orchestrate multi-core Win32 executions, and merge weights using federated averaging algorithms — all in pure TezzNative.
            </p>

            <div style="display: flex; gap: 1.5rem; flex-wrap: wrap; margin-bottom: 2rem">
              <div style="display: flex; align-items: center; gap: 0.5rem; font-size: 0.9375rem; color: var(--dim)">
                <i class="material-icons" aria-hidden="true" style="color: var(--primary-color);font-size:1.1rem;vertical-align:middle">event</i>
                <strong>June 12, 2026</strong>
              </div>
              <div style="display: flex; align-items: center; gap: 0.5rem; font-size: 0.9375rem; color: var(--dim)">
                <i class="material-icons" aria-hidden="true" style="color: var(--primary-color);font-size:1.1rem;vertical-align:middle">schedule</i>
                <strong>03:00 PM - 04:30 PM IST</strong>
              </div>
              <div style="display: flex; align-items: center; gap: 0.5rem; font-size: 0.9375rem; color: var(--dim)">
                <i class="material-icons" aria-hidden="true" style="color: var(--primary-color);font-size:1.1rem;vertical-align:middle">language</i>
                <strong>Online (YouTube Live)</strong>
              </div>
            </div>

            <div style="display: flex; gap: 1rem; flex-wrap: wrap">
              <a href="#register" class="btn btn-primary" onclick="alert('Thank you for registering! We have sent a calendar invite to your registered developer email.')">
                <i class="material-icons" aria-hidden="true" style="margin-right:0.3rem;font-size:1.1rem;vertical-align:middle">person_add</i> Register for Event
              </a>
              <a href="https://ai.tezzcorp.com" target="_blank" rel="noopener" class="btn btn-secondary">
                Discover TezzLLM Stack
              </a>
            </div>
          </div>

          <div style="background: var(--bg-elevated); border: 1px solid var(--border); border-radius: var(--radius-lg); padding: 1.5rem; width: 100%; max-width: 320px; box-shadow: 0 10px 25px rgba(0,0,0,0.05)">
            <h4 style="margin-bottom: 1rem; font-size: 1.0625rem"><i class="material-icons" aria-hidden="true" style="font-size:1.2rem;vertical-align:middle;margin-right:0.3rem">mic</i> Featured Speakers</h4>
            <div style="display: flex; align-items: center; gap: 1rem; margin-bottom: 1rem">
              <div style="width: 44px; height: 44px; border-radius: 10px; background: var(--gradient); color: #fff; display: flex; align-items: center; justify-content: center; font-weight: 800; font-family: monospace">RP</div>
              <div>
                <strong style="display: block; font-size: 0.9375rem">Rohit Pathak</strong>
                <span style="font-size: 0.75rem; color: var(--dim)">Creator &amp; Director, TezzCorp</span>
              </div>
            </div>
            <div style="font-size: 0.8125rem; color: var(--muted); border-top: 1px solid var(--border); padding-top: 1rem; line-height: 1.5">
              Rohit will guide attendees through a live coding session writing custom token streams and loading weights dynamically in a C++-free native process.
            </div>
          </div>
        </div>
      </div>
    </div>
  </section>

  <!-- Events Calendar -->
  <section class="section" style="background: var(--bg2); border-top: 1px solid var(--border); border-bottom: 1px solid var(--border)" aria-labelledby="all-events-heading">
    <div class="container">
      <div class="section-head">
        <div class="eyebrow">Schedule</div>
        <h2 id="all-events-heading">All Events &amp; Summits</h2>
        <p>Browse our list of completed and upcoming tech exhibitions and software launches.</p>
      </div>

      <div style="display: grid; gap: 1.5rem; margin-top: 3rem">
        <?php
        $events = [
            [
                'title' => 'TezzLLM: Practical Federated Training & Run-Time Optimization',
                'date' => 'June 12, 2026',
                'time' => '03:00 PM IST',
                'type' => 'Webinar',
                'loc' => 'Online (YouTube Live)',
                'status' => 'Upcoming',
                'desc' => 'Technical developer training on orchestrating weight averaging, launching parallel engine nodes, and loading model buffers dynamically.',
                'btn_text' => 'Register Now',
                'btn_action' => 'alert(\'Calendar invite sent!\');'
            ],
            [
                'title' => 'Bihar Technology & Innovation Summit 2026',
                'date' => 'July 05, 2026',
                'time' => '10:00 AM IST',
                'type' => 'Exhibition & Keynote',
                'loc' => 'Gyan Bhawan, Patna, Bihar',
                'status' => 'Upcoming',
                'desc' => 'TezzCorp will exhibit at Bihar\'s leading tech summit. Rohit Pathak will present a keynote: "Engineering compiled systems languages & lightweight neural networks from scratch in India."',
                'btn_text' => 'Book Visitor Pass',
                'btn_action' => 'window.open(\'https://patnatechsummit.gov.in\', \'_blank\');'
            ],
            [
                'title' => 'TezzNative v1.0.0 Product Launch Showcase',
                'date' => 'May 15, 2026',
                'time' => '11:00 AM IST',
                'type' => 'Keynote Presentation',
                'loc' => 'Online Broadcast',
                'status' => 'Completed',
                'desc' => 'The official public launch of the TezzNative compiler, documenting build speeds (<50ms), Win32 bindings, and the TezzServe web backend framework.',
                'btn_text' => 'Watch Keynote Recording',
                'btn_action' => 'alert(\'Recording will be loaded shortly. Check back soon!\');'
            ]
        ];

        foreach ($events as $idx => $ev):
            $is_completed = $ev['status'] === 'Completed';
        ?>
        <div style="background: var(--bg-card); border: 1px solid var(--border); border-radius: var(--radius-xl); padding: 2rem; display: flex; flex-direction: column; gap: 1.5rem; transition: var(--transition);" class="event-card">
          <div style="display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 1rem">
            <div style="display: flex; align-items: center; gap: 1rem">
              <div style="width: 48px; height: 48px; border-radius: 12px; background: <?= $is_completed ? 'rgba(84,101,125,0.1)' : 'rgba(16,185,129,0.1)' ?>; color: <?= $is_completed ? 'var(--text-muted)' : 'var(--green)' ?>; display: flex; align-items: center; justify-content: center; font-size: 1.25rem">
                <i class="material-icons" aria-hidden="true"><?= $is_completed ? 'check_circle' : 'event' ?></i>
              </div>
              <div>
                <h3 style="font-size: 1.25rem; font-weight: 700; margin: 0"><?= htmlspecialchars($ev['title']) ?></h3>
                <div style="display: flex; gap: 0.75rem; font-size: 0.8125rem; color: var(--dim); margin-top: 0.25rem">
                  <span><i class="material-icons" aria-hidden="true" style="font-size:0.9rem;vertical-align:middle;margin-right:0.2rem">sell</i> <?= htmlspecialchars($ev['type']) ?></span>
                  <span>·</span>
                  <span><i class="material-icons" aria-hidden="true" style="font-size:0.9rem;vertical-align:middle;margin-right:0.2rem">place</i> <?= htmlspecialchars($ev['loc']) ?></span>
                  <span>·</span>
                  <span style="font-weight: 700; color: <?= $is_completed ? 'var(--text-muted)' : 'var(--green)' ?>"><?= htmlspecialchars($ev['status']) ?></span>
                </div>
              </div>
            </div>
            <button onclick="<?= $ev['btn_action'] ?>" class="btn <?= $is_completed ? 'btn-secondary' : 'btn-primary' ?> btn-sm">
              <?= htmlspecialchars($ev['btn_text']) ?>
            </button>
          </div>

          <p style="margin: 0; color: var(--muted)"><?= htmlspecialchars($ev['desc']) ?></p>

          <div style="display: flex; gap: 2rem; border-top: 1px solid var(--border); padding-top: 1.25rem; font-size: 0.875rem; color: var(--dim)">
            <div><i class="material-icons" aria-hidden="true" style="font-size:1rem;vertical-align:middle;margin-right: 0.375rem">calendar_today</i> Date: <strong><?= htmlspecialchars($ev['date']) ?></strong></div>
            <div><i class="material-icons" aria-hidden="true" style="font-size:1rem;vertical-align:middle;margin-right: 0.375rem">schedule</i> Time: <strong><?= htmlspecialchars($ev['time']) ?></strong></div>
          </div>
        </div>
        <?php endforeach; ?>
      </div>
    </div>
  </section>

  <!-- Newsletter Section -->
  <section class="section">
    <div class="container" style="max-width: 760px">
      <div class="cta-box" style="padding: 3rem">
        <div class="eyebrow" style="color: var(--blue)">Stay Updated</div>
        <h2>Subscribe to TezzCorp technical announcements</h2>
        <p style="margin-bottom: 2rem">Get notified immediately about compiler updates, new AI models, and upcoming developer sessions. No spam, only technical insights.</p>
        <form onsubmit="alert('Subscription successful! Welcome to our developer mailing list.'); return false;" style="display: flex; gap: 0.75rem; max-width: 500px; margin: 0 auto; flex-wrap: wrap">
          <input type="email" required placeholder="your@email.com" style="flex: 1; min-width: 240px; padding: 0.75rem 1.25rem; border-radius: var(--radius-pill); border: 1px solid var(--border); background: var(--bg-color); color: var(--text); font-family: inherit">
          <button type="submit" class="btn btn-primary">Subscribe</button>
        </form>
      </div>
    </div>
  </section>
</main>

<?php include 'includes/footer.php'; ?>
