<?php
declare(strict_types=1);
require_once __DIR__ . '/_common.php';
tn_doc_boot('/docs/actor-runtime', 'actor-runtime');
tn_head('Actor Runtime', 'TezzNative local actor runtime, mailbox, supervision, node metadata, and OTP-like app framework boundary.', 'docs', '/docs/actor-runtime');
tn_page_shell_start('Actor runtime', 'Local actors first. Distributed claims only after gates.', 'TezzNative now has a native-smoke-gated actor foundation for service-runtime experiments.');
?>
  <section class="tn-section">
    <div class="tn-container tn-doc-layout">
      <?php tn_doc_nav(); ?>
      <div class="tn-card-grid">
        <article class="tn-card"><?= tn_badge('Beta', 'green') ?><h3>Local actor primitives</h3><p>`actor.spawn`, `actor.send`, FIFO `actor.receive`, and selective `actor.receive_match` cover the first mailbox contract.</p></article>
        <article class="tn-card"><?= tn_badge('Gated', 'blue') ?><h3>Supervisor basics</h3><p>Parent links, restart policies, restart counters, and app-level supervision are covered by the native actor fixture.</p></article>
        <article class="tn-card"><?= tn_badge('Fail closed', 'amber') ?><h3>Node messaging</h3><p>Local node sends route through mailboxes. Remote node sends return `actor.remote_unsupported()` until authenticated transport exists.</p></article>
        <article class="tn-card"><?= tn_badge('Metadata', 'blue') ?><h3>Hot version tags</h3><p>`actor.hot_reload` updates module version tags. It is not live code replacement yet.</p></article>
        <article class="tn-card"><?= tn_badge('Planned', 'amber') ?><h3>Production runtime</h3><p>Scheduler-backed actors, distribution, child specs, hot module replacement, tracing, metrics, and service-scale benchmarks are still required.</p></article>
        <article class="tn-card"><?= tn_badge('Command', 'blue') ?><h3>Verification</h3><p>Run `tests/conformance/run-native-smoke.ps1` or build `tests/conformance/native/actor_runtime.tn` directly.</p></article>
      </div>
    </div>
  </section>
<?php tn_page_shell_end(); tn_footer(); ?>
