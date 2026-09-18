<?php
require_once __DIR__ . '/config/config.php';
require_once __DIR__ . '/includes/website_content.php';

$products = [];

try {
    $pdo = db();
    $orgId = tezzWebsiteActiveOrgId($pdo);
    $products = tezzWebsiteFetchProducts($pdo, $orgId);
} catch (Throwable $e) {
    // Keep site available even when DB is temporarily unreachable.
    error_log('[website] software fallback mode: ' . $e->getMessage());
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
    <title>TezzCorp Products | Software Platforms & Tools</title>
    <meta name="description" content="Explore TezzCorp product suite, including CRM platform modules, API growth tooling, and production software utilities.">
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Manrope:wght@400;500;600;700;800&family=Sora:wght@500;600;700;800&display=swap" rel="stylesheet">
    <link rel="stylesheet" href="css/style.css?v=<?php echo time(); ?>">
    <link rel="apple-touch-icon" sizes="180x180" href="favicon/apple-touch-icon.png">
    <link rel="icon" type="image/png" sizes="32x32" href="favicon/favicon-32x32.png">
    <link rel="icon" type="image/png" sizes="16x16" href="favicon/favicon-16x16.png">
    <link rel="manifest" href="favicon/site.webmanifest">
</head>
<body class="light-mode page-software">

<?php include 'header.php'; ?>

<main>
    <section class="page-hero">
        <div class="container">
            <div class="hero-wrap reveal">
                <div class="breadcrumbs"><a href="index">Home</a> / <span>Products</span></div>
                <h1>TezzCorp <span class="dynamic-gradient-text">Product Suite</span></h1>
                <p>Products below are managed in CRM and rendered from backend data with production-ready CTA flows.</p>
            </div>
        </div>
    </section>

    <section class="section-shell reveal">
        <div class="container">
            <div class="section-head center">
                <p class="eyebrow">Backend-Driven Product Catalog</p>
                <h2>Platforms, Utilities, and API Growth Modules</h2>
            </div>

            <div class="product-catalog-grid">
                <?php foreach ($products as $product): ?>
                    <article class="product-catalog-card card" id="<?php echo h($product['slug'] ?? 'product'); ?>">
                        <div class="product-catalog-head">
                            <span class="product-catalog-category"><?php echo h($product['category'] ?: 'Platform'); ?></span>
                            <?php if (!empty($product['platform'])): ?><span class="product-catalog-platform"><?php echo h($product['platform']); ?></span><?php endif; ?>
                        </div>

                        <div class="product-catalog-title">
                            <h3><?php echo h($product['name'] ?? 'Product'); ?></h3>
                            <?php if (!empty($product['tagline'])): ?><p><?php echo h($product['tagline']); ?></p><?php endif; ?>
                        </div>

                        <?php if (!empty($product['image_url'])): ?>
                            <div class="product-catalog-media">
                                <img src="<?php echo h($product['image_url']); ?>" alt="<?php echo h($product['name'] ?? 'Product'); ?>">
                            </div>
                        <?php endif; ?>

                        <p><?php echo h($product['description'] ?? ''); ?></p>

                        <?php if (!empty($product['features']) && is_array($product['features'])): ?>
                            <ul>
                                <?php foreach ($product['features'] as $feature): ?>
                                    <li><i class="material-icons" aria-hidden="true" style="font-size:1rem;color:var(--green);vertical-align:middle;margin-right:0.3rem">check_circle</i> <?php echo h($feature); ?></li>
                                <?php endforeach; ?>
                            </ul>
                        <?php endif; ?>

                        <div class="product-catalog-actions">
                            <a href="<?php echo h($product['cta_url'] ?? 'contact'); ?>" class="btn btn-primary btn-animated">
                                <span><?php echo h($product['cta_label'] ?? 'Explore Product'); ?></span>
                                <i class="material-icons" aria-hidden="true" style="font-size:1.1rem;vertical-align:middle;margin-left:0.2rem">arrow_forward</i>
                            </a>
                            <?php if (!empty($product['docs_url'])): ?>
                                <a href="<?php echo h($product['docs_url']); ?>" class="btn btn-secondary btn-animated">
                                    <span>Documentation</span>
                                    <i class="material-icons" aria-hidden="true" style="font-size:1.1rem;vertical-align:middle;margin-left:0.2rem">book</i>
                                </a>
                            <?php endif; ?>
                        </div>
                    </article>
                <?php endforeach; ?>
            </div>
        </div>
    </section>

    <section class="section-shell tone-soft reveal">
        <div class="container platform-grid">
            <article class="platform-card card">
                <h3>Product Operations in CRM</h3>
                <p>TezzCorp teams can manage product content, pricing narratives, and launch messaging directly from CRM without code edits.</p>
                <ul>
                    <li><i class="material-icons" aria-hidden="true" style="font-size:1rem;color:var(--green);vertical-align:middle;margin-right:0.3rem">check_circle</i> Centralized product content publishing</li>
                    <li><i class="material-icons" aria-hidden="true" style="font-size:1rem;color:var(--green);vertical-align:middle;margin-right:0.3rem">check_circle</i> Unified CTA routing and campaign control</li>
                    <li><i class="material-icons" aria-hidden="true" style="font-size:1rem;color:var(--green);vertical-align:middle;margin-right:0.3rem">check_circle</i> Consistent branding across all product pages</li>
                </ul>
            </article>
            <article class="platform-card card">
                <h3>Launch Faster With Clear Packaging</h3>
                <p>From first release to growth stage, product teams can evolve messaging and positioning based on customer feedback.</p>
                <ul>
                    <li><i class="material-icons" aria-hidden="true" style="font-size:1rem;color:var(--green);vertical-align:middle;margin-right:0.3rem">arrow_forward</i> Rapid content updates from CRM dashboard</li>
                    <li><i class="material-icons" aria-hidden="true" style="font-size:1rem;color:var(--green);vertical-align:middle;margin-right:0.3rem">arrow_forward</i> Clear separation of product lines</li>
                    <li><i class="material-icons" aria-hidden="true" style="font-size:1rem;color:var(--green);vertical-align:middle;margin-right:0.3rem">arrow_forward</i> Better conversion with targeted CTAs</li>
                </ul>
            </article>
        </div>
    </section>

    <section class="section-shell reveal">
        <div class="container">
            <div class="cta-panel">
                <div>
                    <p class="eyebrow">Need Product Engineering Support?</p>
                    <h2>Package your software offering for growth and monetization.</h2>
                    <p>We help define product strategy, backend architecture, release workflows, and go-to-market execution.</p>
                </div>
                <div class="actions">
                    <a href="contact" class="btn btn-primary btn-animated"><span>Talk To Product Team</span><i class="material-icons" aria-hidden="true" style="font-size:1.1rem;vertical-align:middle;margin-left:0.2rem">send</i></a>
                    <a href="api-docs" class="btn btn-secondary btn-animated"><span>View API Docs</span><i class="material-icons" aria-hidden="true" style="font-size:1.1rem;vertical-align:middle;margin-left:0.2rem">code</i></a>
                </div>
            </div>
        </div>
    </section>
</main>

<?php include 'footer.php'; ?>

<script src="js/main.js?v=<?php echo time(); ?>"></script>
</body>
</html>
