<?php
require_once __DIR__ . '/config/config.php';
require_once __DIR__ . '/includes/website_content.php';

$services = [];
$plans = [];

try {
    $pdo = db();
    $orgId = tezzWebsiteActiveOrgId($pdo);
    $services = tezzWebsiteFetchServices($pdo, $orgId);
    $plans = tezzWebsiteFetchPlans($pdo, $orgId);
} catch (Throwable $e) {
    // Keep site available even when DB is temporarily unreachable.
    error_log('[website] services fallback mode: ' . $e->getMessage());
}

function h($value): string {
    return htmlspecialchars((string)$value, ENT_QUOTES, 'UTF-8');
}
?>
<!DOCTYPE html>
<html lang="en-IN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>TezzCorp Services | Websites, CRM, API, and Software Delivery</title>
    <meta name="description" content="Explore TezzCorp's production services for websites, CRM operations, API products, and enterprise software execution.">

    <link rel="apple-touch-icon" sizes="180x180" href="favicon/apple-touch-icon.png">
    <link rel="icon" type="image/png" sizes="32x32" href="favicon/favicon-32x32.png">
    <link rel="icon" type="image/png" sizes="16x16" href="favicon/favicon-16x16.png">
    <link rel="manifest" href="favicon/site.webmanifest">

    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Manrope:wght@400;500;600;700;800&family=Sora:wght@500;600;700;800&display=swap" rel="stylesheet">
    <link rel="stylesheet" href="css/style.css?v=<?php echo time(); ?>">
</head>
<body class="light-mode page-services">

<?php include 'header.php'; ?>

<main>
    <section class="page-hero">
        <div class="container">
            <div class="hero-wrap reveal">
                <div class="breadcrumbs"><a href="index">Home</a> / <span>Services</span></div>
                <h1>Production Services <span class="dynamic-gradient-text">For Growth Teams</span></h1>
                <p>Service cards and plans are now controlled from CRM content module and rendered from live backend data.</p>
            </div>
        </div>
    </section>

    <section class="section-shell reveal">
        <div class="container">
            <div class="section-head center">
                <p class="eyebrow">Service Catalog</p>
                <h2>Backend-Powered Service Portfolio</h2>
            </div>
            <div class="service-catalog">
                <?php foreach ($services as $service): ?>
                    <article class="service-card card">
                        <div class="service-icon"><i class="material-icons" aria-hidden="true"><?php echo h($service['icon_class'] ?? 'layers'); ?></i></div>
                        <?php if (!empty($service['badge'])): ?><span class="service-badge"><?php echo h($service['badge']); ?></span><?php endif; ?>
                        <h3><?php echo h($service['name'] ?? 'Service'); ?></h3>
                        <p><?php echo h($service['short_description'] ?? ''); ?></p>

                        <?php if (!empty($service['highlights']) && is_array($service['highlights'])): ?>
                            <ul>
                                <?php foreach ($service['highlights'] as $item): ?>
                                    <li><i class="material-icons" aria-hidden="true" style="font-size:1rem;color:var(--green);vertical-align:middle;margin-right:0.3rem">check_circle</i> <?php echo h($item); ?></li>
                                <?php endforeach; ?>
                            </ul>
                        <?php endif; ?>

                        <a href="<?php echo h($service['cta_url'] ?? 'contact'); ?>" class="btn btn-secondary btn-animated btn-inline">
                            <span><?php echo h($service['cta_label'] ?? 'Discuss Service'); ?></span>
                            <i class="material-icons" aria-hidden="true" style="font-size:1.1rem;vertical-align:middle;margin-left:0.2rem">arrow_forward</i>
                        </a>
                    </article>
                <?php endforeach; ?>
            </div>
        </div>
    </section>

    <section class="section-shell tone-soft reveal">
        <div class="container">
            <div class="section-head center">
                <p class="eyebrow">Execution Framework</p>
                <h2>How We Ship Predictable Production Outcomes</h2>
            </div>
            <div class="delivery-model">
                <article class="card">
                    <span class="step">01</span>
                    <h3>Business Discovery</h3>
                    <p>Goals, constraints, and technical context are converted into a clear roadmap.</p>
                </article>
                <article class="card">
                    <span class="step">02</span>
                    <h3>Solution Design</h3>
                    <p>UX, architecture, data model, and delivery phases are aligned before build starts.</p>
                </article>
                <article class="card">
                    <span class="step">03</span>
                    <h3>Build & Validate</h3>
                    <p>Implementation with QA, security checks, and stakeholder review checkpoints.</p>
                </article>
                <article class="card">
                    <span class="step">04</span>
                    <h3>Launch & Optimize</h3>
                    <p>Controlled release, monitoring setup, and ongoing performance improvements.</p>
                </article>
            </div>
        </div>
    </section>

    <section class="section-shell reveal" id="plans">
        <div class="container">
            <div class="section-head center">
                <p class="eyebrow">Engagement Plans</p>
                <h2>Plans Managed From CRM</h2>
            </div>
            <div class="plans-grid">
                <?php foreach ($plans as $plan): ?>
                    <article class="plan-card card <?php echo !empty($plan['is_popular']) ? 'popular' : ''; ?>">
                        <?php if (!empty($plan['is_popular'])): ?><span class="plan-badge">Most Chosen</span><?php endif; ?>
                        <h3><?php echo h($plan['name'] ?? 'Plan'); ?></h3>
                        <p class="plan-price"><?php echo h($plan['price_label'] ?: 'Custom'); ?></p>
                        <p class="plan-cycle"><?php echo h($plan['billing_cycle'] ?: 'Custom'); ?></p>
                        <p><?php echo h($plan['best_for'] ?? ''); ?></p>
                        <?php if (!empty($plan['includes']) && is_array($plan['includes'])): ?>
                            <ul>
                                <?php foreach ($plan['includes'] as $item): ?>
                                    <li><i class="material-icons" aria-hidden="true" style="font-size:1rem;color:var(--green);vertical-align:middle;margin-right:0.3rem">check_circle</i> <?php echo h($item); ?></li>
                                <?php endforeach; ?>
                            </ul>
                        <?php endif; ?>
                        <?php if (!empty($plan['support_text'])): ?><p class="plan-support"><i class="material-icons" aria-hidden="true" style="font-size:1rem;vertical-align:middle;margin-right:0.3rem">headset</i> <?php echo h($plan['support_text']); ?></p><?php endif; ?>
                        <a href="<?php echo h($plan['cta_url'] ?? 'contact'); ?>" class="btn btn-primary btn-animated">
                            <span><?php echo h($plan['cta_label'] ?? 'Choose Plan'); ?></span>
                            <i class="material-icons" aria-hidden="true" style="font-size:1.1rem;vertical-align:middle;margin-left:0.2rem">arrow_forward</i>
                        </a>
                    </article>
                <?php endforeach; ?>
            </div>
        </div>
    </section>

    <section class="section-shell reveal">
        <div class="container">
            <div class="cta-panel">
                <div>
                    <p class="eyebrow">Need A Custom Scope?</p>
                    <h2>Get a production roadmap tailored to your business model.</h2>
                    <p>We map your requirements into milestones, ownership, and delivery confidence before build begins.</p>
                </div>
                <div class="actions">
                    <a href="contact" class="btn btn-primary btn-animated"><span>Request Proposal</span><i class="material-icons" aria-hidden="true" style="font-size:1.1rem;vertical-align:middle;margin-left:0.2rem">send</i></a>
                    <a href="about" class="btn btn-secondary btn-animated"><span>Meet the Team</span><i class="material-icons" aria-hidden="true" style="font-size:1.1rem;vertical-align:middle;margin-left:0.2rem">groups</i></a>
                </div>
            </div>
        </div>
    </section>
</main>

<?php include 'footer.php'; ?>

<script src="js/main.js?v=<?php echo time(); ?>"></script>
</body>
</html>
