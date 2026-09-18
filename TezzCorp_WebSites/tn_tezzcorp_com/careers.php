<?php
declare(strict_types=1);
$page_title = 'Careers at TezzCorp — Engineering the Next Generation of Systems and AI';
$page_desc  = 'Explore career opportunities at TezzCorp Pvt Ltd. Join our team in Bettiah, Bihar, and help build the future of systems programming and on-device AI.';
$active_nav = 'careers';
include 'includes/header.php';
?>

<main id="main-content">
  <!-- Page Hero -->
  <section style="padding: 4.5rem 0 3.5rem; border-bottom: 1px solid var(--border)">
    <div class="container">
      <div style="max-width: 720px">
        <div class="eyebrow" aria-label="Page category">Careers</div>
        <h1 id="careers-page-heading">Join the <span class="gradient-text">TezzCorp Team</span></h1>
        <p style="font-size: 1.125rem; margin-top: 1rem">
          We are building a world-class technology stack in India — compiled systems languages, lightweight on-device LLMs, and custom enterprise CRM engines. If you love deep tech and zero-dependency software, let's build together.
        </p>
      </div>
    </div>
  </section>

  <!-- Why Work Here -->
  <section class="section" aria-labelledby="why-work-heading">
    <div class="container">
      <div class="section-head text-center">
        <div class="eyebrow">Culture &amp; Philosophy</div>
        <h2 id="why-work-heading">Why build at TezzCorp?</h2>
        <p style="max-width: 540px; margin: 0.75rem auto 0">
          We believe in extreme ownership, high-performance systems, and building tools that developers and businesses love.
        </p>
      </div>

      <div class="services-grid" style="margin-top: 3rem">
        <div class="service-card" style="border: 1px solid var(--border); border-radius: var(--radius-xl); padding: 2rem">
          <div class="service-icon" style="background: rgba(59, 130, 246, 0.1); color: var(--blue); margin-bottom: 1.25rem">
            <i class="material-icons" aria-hidden="true">memory</i>
          </div>
          <h3>Deep Tech Stack</h3>
          <p>We compile our own language (TezzNative), write our own database (TezzDB), and train our own LLM models from scratch. No legacy bloat or wrapper APIs.</p>
        </div>

        <div class="service-card" style="border: 1px solid var(--border); border-radius: var(--radius-xl); padding: 2rem">
          <div class="service-icon" style="background: rgba(99, 102, 241, 0.1); color: var(--indigo); margin-bottom: 1.25rem">
            <i class="material-icons" aria-hidden="true">offline_bolt</i>
          </div>
          <h3>Extreme Autonomy</h3>
          <p>We work in small, highly-autonomous units. You own your code from compiler optimization down to physical deployment on Hostinger and production servers.</p>
        </div>

        <div class="service-card" style="border: 1px solid var(--border); border-radius: var(--radius-xl); padding: 2rem">
          <div class="service-icon" style="background: rgba(16, 185, 129, 0.1); color: var(--green); margin-bottom: 1.25rem">
            <i class="material-icons" aria-hidden="true">place</i>
          </div>
          <h3>From Bihar, For the World</h3>
          <p>Based in Jhiliya, Bettiah, Bihar, we are proud to contribute directly to the tech ecosystem in India, building state-of-the-art native infrastructure.</p>
        </div>
      </div>
    </div>
  </section>

  <!-- Open Positions -->
  <section class="section" style="background: var(--bg2); border-top: 1px solid var(--border); border-bottom: 1px solid var(--border)" aria-labelledby="jobs-heading">
    <div class="container">
      <div class="section-head">
        <div class="eyebrow">Open Opportunities</div>
        <h2 id="jobs-heading">Engineering positions</h2>
        <p>We are actively seeking talented developers to join our hybrid workspace in Bettiah, Bihar.</p>
      </div>

      <div style="display: grid; gap: 1.5rem; max-width: 900px; margin-top: 2rem">
        <?php
        $jobs = [
            [
                'title' => 'Lead Systems Compiler Engineer (TezzNative)',
                'dept' => 'Systems Engineering',
                'type' => 'Full-time (Hybrid)',
                'loc' => 'Jhiliya, Bettiah, Bihar',
                'desc' => 'Work directly on TezzNative single-pass compiler optimization, AST transformations, target code generation (x86/x64 and WebAssembly), and core systems library architecture.',
                'reqs' => ['Deep understanding of compilers, linkers, parsing, and assembly code.', 'Experience in custom language development or bare-metal systems.', 'Strong commitment to zero-dependency and high-performance engineering.'],
                'icon' => 'code',
                'color' => 'var(--blue)',
                'bg' => 'rgba(59,130,246,0.1)'
            ],
            [
                'title' => 'On-Device AI Engine Developer (TezzLLM)',
                'dept' => 'Artificial Intelligence',
                'type' => 'Full-time (Hybrid)',
                'loc' => 'Jhiliya, Bettiah, Bihar',
                'desc' => 'Optimize transformer runtimes, design super-compact model architectures (<10M parameters), and implement federated training logic in pure TezzNative.',
                'reqs' => ['Excellent understanding of deep learning math: self-attention, SwiGLU, RMSNorm, RoPE embeddings.', 'Experience building or optimizing neural network kernels from scratch.', 'Strong linear algebra and hardware execution profiling skills.'],
                'icon' => 'psychology',
                'color' => 'var(--indigo)',
                'bg' => 'rgba(99,102,241,0.1)'
            ],
            [
                'title' => 'Full-Stack Web Architect (TezzServe & CRM)',
                'dept' => 'Web Platforms',
                'type' => 'Full-time (Hybrid)',
                'loc' => 'Jhiliya, Bettiah, Bihar',
                'desc' => 'Architect and scale our web-facing products. Work on TezzServe backend performance, improve CRM automation workflows, and integrate custom ERP modules.',
                'reqs' => ['Proven track record building fast, secure PHP and web architectures.', 'Strong database knowledge (PostgreSQL, custom SQL engines, ACID compliance).', 'High eye for modern CSS, vanilla JS, and aesthetic UI layouts.'],
                'icon' => 'dns',
                'color' => 'var(--green)',
                'bg' => 'rgba(16,185,129,0.1)'
            ]
        ];

        foreach ($jobs as $idx => $job):
        ?>
        <div style="background: var(--bg-card); border: 1px solid var(--border); border-radius: var(--radius-xl); padding: 2rem; display: flex; flex-direction: column; gap: 1.5rem; transition: var(--transition);" class="job-card">
          <div style="display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 1rem">
            <div style="display: flex; align-items: center; gap: 1rem">
              <div style="width: 48px; height: 48px; border-radius: 12px; background: <?= $job['bg'] ?>; color: <?= $job['color'] ?>; display: flex; align-items: center; justify-content: center; font-size: 1.25rem">
                <i class="material-icons" aria-hidden="true"><?= $job['icon'] ?></i>
              </div>
              <div>
                <h3 style="font-size: 1.25rem; font-weight: 700; margin: 0"><?= htmlspecialchars($job['title']) ?></h3>
                <div style="display: flex; gap: 0.75rem; font-size: 0.8125rem; color: var(--dim); margin-top: 0.25rem">
                  <span><i class="material-icons" aria-hidden="true" style="font-size:0.9rem;vertical-align:middle;margin-right:0.2rem">layers</i> <?= htmlspecialchars($job['dept']) ?></span>
                  <span>·</span>
                  <span><i class="material-icons" aria-hidden="true" style="font-size:0.9rem;vertical-align:middle;margin-right:0.2rem">schedule</i> <?= htmlspecialchars($job['type']) ?></span>
                  <span>·</span>
                  <span><i class="material-icons" aria-hidden="true" style="font-size:0.9rem;vertical-align:middle;margin-right:0.2rem">place</i> <?= htmlspecialchars($job['loc']) ?></span>
                </div>
              </div>
            </div>
            <a href="#apply-form" class="btn btn-secondary btn-sm" onclick="selectJob('<?= htmlspecialchars($job['title']) ?>')">
              Apply Now
            </a>
          </div>

          <p style="margin: 0; color: var(--muted)"><?= htmlspecialchars($job['desc']) ?></p>

          <div>
            <div style="font-weight: 700; font-size: 0.875rem; margin-bottom: 0.5rem; color: var(--text)">Key Requirements:</div>
            <ul style="list-style: none; padding: 0; display: grid; gap: 0.375rem">
              <?php foreach ($job['reqs'] as $req): ?>
              <li style="font-size: 0.875rem; color: var(--dim); display: flex; align-items: flex-start; gap: 0.5rem">
                <i class="material-icons" style="color: var(--green); margin-top: 0.25rem; font-size: 0.95rem" aria-hidden="true">check</i>
                <span><?= htmlspecialchars($req) ?></span>
              </li>
              <?php endforeach; ?>
            </ul>
          </div>
        </div>
        <?php endforeach; ?>
      </div>
    </div>
  </section>

  <!-- Application Form -->
  <section class="section" id="apply-form" aria-labelledby="apply-heading">
    <div class="container" style="max-width: 680px">
      <div class="cta-box" style="background: var(--bg-card); border: 1px solid var(--border); border-radius: var(--radius-xl); padding: 2.5rem; text-align: left">
        <h2 id="apply-heading" style="text-align: center; margin-bottom: 0.5rem">Apply for a <span class="gradient-text">Position</span></h2>
        <p style="text-align: center; margin-bottom: 2rem">Submit your details below and our team will get back to you within 3 business days.</p>

        <form action="#" method="POST" onsubmit="alert('Application received! Rohit will get in touch with you shortly.'); return false;" style="display: grid; gap: 1.25rem">
          <div style="display: grid; gap: 0.5rem">
            <label for="app-job" style="font-weight: 600; font-size: 0.875rem; color: var(--text)">Target Position *</label>
            <select id="app-job" name="job" required style="width: 100%; padding: 0.75rem 1rem; border-radius: var(--radius-sm); border: 1px solid var(--border); background: var(--bg-color); color: var(--text); font-family: inherit">
              <option value="">-- Select a position --</option>
              <option value="Lead Systems Compiler Engineer (TezzNative)">Lead Systems Compiler Engineer (TezzNative)</option>
              <option value="On-Device AI Engine Developer (TezzLLM)">On-Device AI Engine Developer (TezzLLM)</option>
              <option value="Full-Stack Web Architect (TezzServe & CRM)">Full-Stack Web Architect (TezzServe & CRM)</option>
              <option value="Other / General Application">Other / General Application</option>
            </select>
          </div>

          <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 1rem">
            <div style="display: grid; gap: 0.5rem">
              <label for="app-name" style="font-weight: 600; font-size: 0.875rem; color: var(--text)">Full Name *</label>
              <input type="text" id="app-name" name="name" required placeholder="Rohit Pathak" style="width: 100%; padding: 0.75rem 1rem; border-radius: var(--radius-sm); border: 1px solid var(--border); background: var(--bg-color); color: var(--text); font-family: inherit">
            </div>
            <div style="display: grid; gap: 0.5rem">
              <label for="app-email" style="font-weight: 600; font-size: 0.875rem; color: var(--text)">Email Address *</label>
              <input type="email" id="app-email" name="email" required placeholder="you@example.com" style="width: 100%; padding: 0.75rem 1rem; border-radius: var(--radius-sm); border: 1px solid var(--border); background: var(--bg-color); color: var(--text); font-family: inherit">
            </div>
          </div>

          <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 1rem">
            <div style="display: grid; gap: 0.5rem">
              <label for="app-phone" style="font-weight: 600; font-size: 0.875rem; color: var(--text)">Phone Number *</label>
              <input type="tel" id="app-phone" name="phone" required placeholder="+91 96080 83342" style="width: 100%; padding: 0.75rem 1rem; border-radius: var(--radius-sm); border: 1px solid var(--border); background: var(--bg-color); color: var(--text); font-family: inherit">
            </div>
            <div style="display: grid; gap: 0.5rem">
              <label for="app-github" style="font-weight: 600; font-size: 0.875rem; color: var(--text)">GitHub / Portfolio Link *</label>
              <input type="url" id="app-github" name="github" required placeholder="https://github.com/username" style="width: 100%; padding: 0.75rem 1rem; border-radius: var(--radius-sm); border: 1px solid var(--border); background: var(--bg-color); color: var(--text); font-family: inherit">
            </div>
          </div>

          <div style="display: grid; gap: 0.5rem">
            <label for="app-cover" style="font-weight: 600; font-size: 0.875rem; color: var(--text)">Tell us about a zero-dependency project you built *</label>
            <textarea id="app-cover" name="cover" required rows="4" placeholder="Briefly describe what you built, what language was used, and why you avoided external libraries." style="width: 100%; padding: 0.75rem 1rem; border-radius: var(--radius-sm); border: 1px solid var(--border); background: var(--bg-color); color: var(--text); font-family: inherit; resize: vertical"></textarea>
          </div>

          <button type="submit" class="btn btn-primary btn-lg" style="width: 100%; margin-top: 1rem">
            <i class="material-icons" aria-hidden="true" style="margin-right:0.3rem;vertical-align:middle;font-size:1.1rem">send</i> Submit Application
          </button>
        </form>
      </div>
    </div>
  </section>
</main>

<script>
function selectJob(title) {
    const select = document.getElementById('app-job');
    if (select) {
        select.value = title;
    }
}
</script>

<?php include 'includes/footer.php'; ?>
