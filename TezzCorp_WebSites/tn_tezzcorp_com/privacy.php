<?php
$title = 'Privacy Policy';
$effectiveDate = date('F d, Y');
?>
<!DOCTYPE html>
<html lang="en-IN">
<head>
    <meta charset="UTF-8">
    <title><?= htmlspecialchars($title) ?> - TezzCorp</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta name="description" content="Read the TezzCorp Privacy Policy covering data handling, usage, retention, and user rights.">

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
                <div class="breadcrumbs"><a href="index">Home</a> / <span>Privacy Policy</span></div>
                <h1>Privacy <span class="dynamic-gradient-text">Policy</span></h1>
                <p>Effective date: <?= htmlspecialchars($effectiveDate) ?>. This policy explains how TezzCorp collects, uses, and protects your information.</p>
            </div>
        </div>
    </section>

    <section class="section-shell reveal">
        <div class="container">
            <article class="card" style="padding:24px; display:grid; gap:18px;">
                <section>
                    <h2>1. Introduction</h2>
                    <p>This Privacy Policy describes how TezzCorp processes personal information when you use our website, CRM, and related software services.</p>
                </section>

                <section>
                    <h2>2. Information We Collect</h2>
                    <ul style="list-style:disc; padding-left:20px; display:grid; gap:8px; color:var(--text-muted);">
                        <li>Name and contact information for account and communication.</li>
                        <li>Authentication data, including credentials and access metadata.</li>
                        <li>Project and operational data needed to provide services.</li>
                        <li>Optional integrations such as Google OAuth and connected APIs, based on your authorization.</li>
                    </ul>
                </section>

                <section>
                    <h2>3. How We Use Information</h2>
                    <ul style="list-style:disc; padding-left:20px; display:grid; gap:8px; color:var(--text-muted);">
                        <li>Service delivery, account management, and support operations.</li>
                        <li>Security monitoring, fraud prevention, and incident response.</li>
                        <li>Product improvements and performance optimization.</li>
                        <li>Compliance with contractual and legal obligations.</li>
                    </ul>
                </section>

                <section>
                    <h2>4. Data Sharing</h2>
                    <p>TezzCorp does not sell personal data. Information may be shared with trusted processors and infrastructure providers required for service delivery, or when required by law.</p>
                </section>

                <section>
                    <h2>5. Security and Retention</h2>
                    <p>We apply technical and organizational safeguards including access controls, logging, and encryption where appropriate. Data is retained only as long as necessary for service, compliance, or security purposes.</p>
                </section>

                <section>
                    <h2>6. Your Rights</h2>
                    <p>You may request access, correction, or deletion of your data by contacting us at <a href="mailto:privacy@tezzcorp.com">privacy@tezzcorp.com</a>.</p>
                </section>

                <section>
                    <h2>7. Policy Updates</h2>
                    <p>We may update this policy from time to time. Continued use of TezzCorp services after updates means you accept the revised policy.</p>
                </section>
            </article>
        </div>
    </section>
</main>

<?php include 'footer.php'; ?>

<script src="js/main.js?v=<?php echo time(); ?>"></script>
</body>
</html>
