<?php
$title = 'Terms of Service';
$effectiveDate = date('F d, Y');
?>
<!DOCTYPE html>
<html lang="en-IN">
<head>
    <meta charset="UTF-8">
    <title><?= htmlspecialchars($title) ?> - TezzCorp</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta name="description" content="Review TezzCorp terms for service usage, account responsibilities, and legal conditions.">

    <link rel="apple-touch-icon" sizes="180x180" href="favicon/apple-touch-icon.png">
    <link rel="icon" type="image/png" sizes="32x32" href="favicon/favicon-32x32.png">
    <link rel="icon" type="image/png" sizes="16x16" href="favicon/favicon-16x16.png">
    <link rel="manifest" href="favicon/site.webmanifest">

    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Manrope:wght@400;500;600;700;800&family=Sora:wght@500;600;700;800&display=swap" rel="stylesheet">
    <link rel="stylesheet" href="css/style.css?v=<?php echo time(); ?>">
</head>
<body class="light-mode page-legal">

<?php include 'header.php'; ?>

<main>
    <section class="page-hero">
        <div class="container">
            <div class="hero-wrap reveal">
                <div class="breadcrumbs"><a href="index">Home</a> / <span>Terms of Service</span></div>
                <h1>Terms of <span class="dynamic-gradient-text">Service</span></h1>
                <p>Effective date: <?= htmlspecialchars($effectiveDate) ?>. These terms govern your use of TezzCorp platforms and services.</p>
            </div>
        </div>
    </section>

    <section class="section-shell reveal">
        <div class="container">
            <article class="card" style="padding:24px; display:grid; gap:18px;">
                <section>
                    <h2>1. Acceptance of Terms</h2>
                    <p>By accessing or using TezzCorp services, you agree to these Terms of Service and our Privacy Policy.</p>
                </section>

                <section>
                    <h2>2. Service Scope</h2>
                    <p>TezzCorp provides software, website, CRM, and API related solutions. Features and availability may vary by plan, project scope, or contractual agreement.</p>
                </section>

                <section>
                    <h2>3. Account Responsibilities</h2>
                    <ul style="list-style:disc; padding-left:20px; display:grid; gap:8px; color:var(--text-muted);">
                        <li>You are responsible for maintaining account credential security.</li>
                        <li>You must provide accurate and lawful information.</li>
                        <li>You must not misuse, disrupt, or attempt unauthorized access to TezzCorp systems.</li>
                    </ul>
                </section>

                <section>
                    <h2>4. Data and Integrations</h2>
                    <p>Where third-party services are connected (for example OAuth providers or cloud services), usage remains subject to those providers’ terms and your granted permissions.</p>
                </section>

                <section>
                    <h2>5. Intellectual Property</h2>
                    <p>Unless otherwise agreed in writing, TezzCorp retains rights to proprietary frameworks, tooling, and platform components used to deliver services.</p>
                </section>

                <section>
                    <h2>6. Limitation of Liability</h2>
                    <p>To the maximum extent permitted by law, TezzCorp is not liable for indirect, incidental, or consequential damages arising from service use.</p>
                </section>

                <section>
                    <h2>7. Termination</h2>
                    <p>We may suspend or terminate access for breaches of these terms, security risks, or legal requirements.</p>
                </section>

                <section>
                    <h2>8. Updates to Terms</h2>
                    <p>We may revise these terms periodically. Continued use after updates indicates acceptance of the revised terms.</p>
                </section>

                <section>
                    <h2>9. Contact</h2>
                    <p>For legal or compliance questions, contact <a href="mailto:support@tezzcorp.com">support@tezzcorp.com</a>.</p>
                </section>
            </article>
        </div>
    </section>
</main>

<?php include 'footer.php'; ?>

<script src="js/main.js?v=<?php echo time(); ?>"></script>
</body>
</html>
