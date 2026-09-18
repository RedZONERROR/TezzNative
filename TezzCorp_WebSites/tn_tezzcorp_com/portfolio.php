<?php
require_once __DIR__ . '/config/config.php';
require_once __DIR__ . '/includes/website_content.php';

$caseStudies = [];

try {
    $pdo = db();
    $orgId = tezzWebsiteActiveOrgId($pdo);
    $caseStudies = tezzWebsiteFetchCaseStudies($pdo, $orgId);
} catch (Throwable $e) {
    // Keep site available even when DB is temporarily unreachable.
    error_log('[website] portfolio fallback mode: ' . $e->getMessage());
}

function h($value): string {
    return htmlspecialchars((string)$value, ENT_QUOTES, 'UTF-8');
}

$filters = ['all' => 'All Projects'];
foreach ($caseStudies as $case) {
    $raw = trim((string)($case['category'] ?: $case['industry']));
    if ($raw === '') {
        continue;
    }
    $key = strtolower(preg_replace('/[^a-z0-9]+/', '-', $raw));
    $key = trim($key, '-');
    if ($key === '') {
        continue;
    }
    if (!isset($filters[$key])) {
        $filters[$key] = $raw;
    }
}
?>
<!DOCTYPE html>
<html lang="en-IN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Case Studies - TezzCorp</title>
    <meta name="description" content="Explore TezzCorp case studies across web engineering, CRM operations, API products, and enterprise delivery.">
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Manrope:wght@400;500;600;700;800&family=Sora:wght@500;600;700;800&display=swap" rel="stylesheet">
    <link rel="stylesheet" href="css/style.css?v=<?php echo time(); ?>">
    <link rel="stylesheet" href="css/portfolio.css?v=<?php echo time(); ?>">
    <link rel="apple-touch-icon" sizes="180x180" href="favicon/apple-touch-icon.png">
    <link rel="icon" type="image/png" sizes="32x32" href="favicon/favicon-32x32.png">
    <link rel="icon" type="image/png" sizes="16x16" href="favicon/favicon-16x16.png">
    <link rel="manifest" href="favicon/site.webmanifest">
</head>
<body class="light-mode page-portfolio">

<?php include 'header.php'; ?>

<main>
    <section class="page-hero">
        <div class="container">
            <div class="hero-wrap reveal">
                <div class="breadcrumbs"><a href="index">Home</a> / <span>Case Studies</span></div>
                <h1>Backend-Powered <span class="dynamic-gradient-text">Case Studies</span></h1>
                <p>Case studies on this page are fetched from CRM-managed backend content with publish controls.</p>
            </div>
        </div>
    </section>

    <section class="portfolio-filter">
        <div class="container">
            <div class="filter-buttons">
                <?php $isFirst = true; foreach ($filters as $filterKey => $filterLabel): ?>
                    <button class="filter-btn <?php echo $isFirst ? 'active' : ''; ?>" data-filter="<?php echo h($filterKey); ?>"><?php echo h($filterLabel); ?></button>
                <?php $isFirst = false; endforeach; ?>
            </div>
        </div>
    </section>

    <section class="portfolio-grid">
        <div class="container">
            <div class="portfolio-container">
                <?php foreach ($caseStudies as $case):
                    $categoryLabel = trim((string)($case['category'] ?: $case['industry'] ?: 'General'));
                    $categoryKey = strtolower(preg_replace('/[^a-z0-9]+/', '-', $categoryLabel));
                    $categoryKey = trim($categoryKey, '-');
                    if ($categoryKey === '') {
                        $categoryKey = 'general';
                    }

                    $payload = [
                        'title' => $case['title'] ?? '',
                        'client' => $case['client_name'] ?? '',
                        'category' => $categoryLabel,
                        'industry' => $case['industry'] ?? '',
                        'description' => $case['summary'] ?? '',
                        'challenge' => $case['challenge_text'] ?? '',
                        'solution' => $case['solution_text'] ?? '',
                        'impact_label' => $case['impact_label'] ?? '',
                        'impact_value' => $case['impact_value'] ?? '',
                        'technologies' => $case['technologies'] ?? [],
                        'image' => $case['image_url'] ?: 'img/web.png',
                        'cta_label' => $case['cta_label'] ?? 'Contact Team',
                        'cta_url' => $case['cta_url'] ?? 'contact',
                    ];
                ?>
                    <article class="portfolio-item" data-category="<?php echo h($categoryKey); ?>" data-case="<?php echo h(json_encode($payload, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES)); ?>">
                        <div class="portfolio-image">
                            <img src="<?php echo h($payload['image']); ?>" alt="<?php echo h($payload['title']); ?>">
                            <div class="portfolio-overlay">
                                <div class="overlay-content">
                                    <span class="project-category"><?php echo h($categoryLabel); ?></span>
                                    <h3><?php echo h($payload['title']); ?></h3>
                                    <p><?php echo h($payload['description']); ?></p>
                                    <button class="btn-view-project">View Case Study</button>
                                </div>
                            </div>
                        </div>
                    </article>
                <?php endforeach; ?>
            </div>
        </div>
    </section>

    <div class="project-modal" id="project-modal">
        <div class="modal-content">
            <span class="close-modal">&times;</span>
            <div class="modal-body">
                <div class="project-images">
                    <div class="main-image">
                        <img src="img/web.png" alt="Case Study" id="modal-main-image">
                    </div>
                </div>
                <div class="project-details">
                    <h2 id="modal-project-title">Case Study</h2>
                    <div class="project-meta">
                        <div class="meta-item">
                            <span class="meta-label">Client:</span>
                            <span class="meta-value" id="modal-client">-</span>
                        </div>
                        <div class="meta-item">
                            <span class="meta-label">Category:</span>
                            <span class="meta-value" id="modal-category">-</span>
                        </div>
                        <div class="meta-item">
                            <span class="meta-label" id="modal-impact-label">Impact:</span>
                            <span class="meta-value" id="modal-impact-value">-</span>
                        </div>
                    </div>
                    <div class="project-description">
                        <h3>Summary</h3>
                        <p id="modal-description">-</p>
                    </div>
                    <div class="project-description">
                        <h3>Challenge</h3>
                        <p id="modal-challenge">-</p>
                    </div>
                    <div class="project-description">
                        <h3>Solution</h3>
                        <p id="modal-solution">-</p>
                    </div>
                    <div class="project-technologies">
                        <h3>Technologies Used</h3>
                        <div class="tech-tags" id="modal-technologies"></div>
                    </div>
                    <a href="contact" class="btn btn-primary btn-animated" id="modal-live-link"><span>Contact Team</span><i class="material-icons" aria-hidden="true" style="font-size:1.1rem;vertical-align:middle;margin-left:0.2rem">arrow_forward</i></a>
                </div>
            </div>
        </div>
    </div>

    <section class="cta-section">
        <div class="container">
            <div class="cta-content">
                <h2>Ready to Build Your <span class="dynamic-gradient-text">Next Case Study</span>?</h2>
                <p>Contact us for a production-grade roadmap and execution model.</p>
                <a href="contact" class="btn btn-primary btn-animated"><span>Get in Touch</span><i class="material-icons" aria-hidden="true" style="font-size:1.1rem;vertical-align:middle;margin-left:0.2rem">send</i></a>
            </div>
        </div>
    </section>
</main>

<?php include 'footer.php'; ?>

<script src="js/main.js?v=<?php echo time(); ?>"></script>
<script src="js/portfolio.js?v=<?php echo time(); ?>"></script>
</body>
</html>
