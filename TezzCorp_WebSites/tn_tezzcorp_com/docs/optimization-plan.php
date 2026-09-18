<?php
declare(strict_types=1);
require_once __DIR__ . '/_common.php';
tn_doc_boot('/docs/optimization-plan', 'optimization');
tn_head('Optimization Plan', 'TezzNative optimization plan for conformance, backend reliability, standard library hardening, DX, packages, Python bridge, C ABI, benchmarks, and release engineering.', 'docs', '/docs/optimization-plan');
tn_nav('docs');
tn_page_shell_start('Optimization plan', 'Make the language reliable before making it loud.', 'The full roadmap is also tracked in GitHub under docs/OPTIMIZATION_PLAN.md.');
$items = [
    ['Milestone 0', 'Public trust baseline', 'Completed for the current public surface with verified claims, visible stability labels, and bounded replacement messaging.'],
    ['Milestone 1', 'Conformance and correctness harness', 'Completed for the current stable-core surface with parser, typecheck, diagnostics, and stdlib gates.'],
    ['Milestone 2', 'Native backend reliability', 'Completed for the current Windows/Linux x64 surface with native smoke, byte-reproducibility gates, and fail-closed unsupported targets.'],
    ['Milestone 3', 'Standard library hardening', 'Completed for the current stable-candidate stdlib slice with ownership docs, failure rules, and math/string/vector/arena edge gates.'],
    ['Milestone 4', 'Developer experience', 'Completed for the current first-user workflow: actionable diagnostics, fmt idempotence, lint rule IDs, examples, LSP source, snippets, run, and native build gates.'],
    ['Milestone 5', 'Ecosystem and package trust', 'Completed for the current first-party SDK package set with tezz command coverage, SemVer pins, lock/registry parity, package checksums, generated package docs, and CI gates.'],
    ['Milestone 6+', 'Python bridge, C ABI, benchmarks, releases', 'C ABI, release security, and the starter benchmark proof are complete for the current public surface.'],
    ['Milestone 11', 'Actor runtime and OTP-like apps', 'Started with local actors, mailbox matching, supervisor restarts, hot version tags, node metadata, and OTP-style app helpers. Distributed scheduling and hot replacement remain gated.'],
];
?>
  <section class="tn-section">
    <div class="tn-container tn-doc-layout">
      <?php tn_doc_nav(); ?>
      <div class="tn-card-grid">
        <?php foreach ($items as [$m, $title, $body]): ?>
        <article class="tn-card"><?= tn_badge($m, 'blue') ?><h3><?= htmlspecialchars($title) ?></h3><p><?= htmlspecialchars($body) ?></p></article>
        <?php endforeach; ?>
      </div>
    </div>
  </section>
<?php tn_page_shell_end(); tn_footer(); ?>
