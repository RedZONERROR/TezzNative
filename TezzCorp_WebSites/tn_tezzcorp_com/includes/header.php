<?php
// includes/header.php - TezzNative Web Portal Header & Complete SEO Suite
if (!defined('TN_APP')) define('TN_APP', true);

require_once __DIR__ . '/i18n.php';

$curr_path = parse_url($_SERVER['REQUEST_URI'] ?? '/', PHP_URL_PATH) ?: '/';
$canonical_path = isset($canonical_path) ? $canonical_path : $curr_path;
$canonical_url = 'https://tezznative.org' . ($canonical_path === '/' ? '/' : rtrim($canonical_path, '/'));

$page_title = isset($page_title) ? $page_title . ' | TezzNative Programming Language' : 'TezzNative - The Fast, Pythonic Systems & AI Programming Language';
$page_desc = isset($page_desc) ? $page_desc : 'TezzNative combines the clean clarity of Python with bare-metal C speed, native tensors, async/await coroutines, scoped memory cleanup, and zero-overhead standalone binaries.';
$active_nav = isset($active_nav) ? $active_nav : 'home';
$active_lang = tn_get_lang();
$active_locale = tn_get_locale();
?>
<!DOCTYPE html>
<html lang="<?= htmlspecialchars($active_lang) ?>" class="dark">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title><?= htmlspecialchars($page_title) ?></title>
  <meta name="description" content="<?= htmlspecialchars($page_desc) ?>">
  <meta name="keywords" content="TezzNative, programming language, systems programming, AI, tensors, async await, coroutines, machine learning, compiler, fast, pythonic, C speed, GGUF, zero GC, LLM">
  <meta name="author" content="Rohit Pathak, TezzCorp Pvt Ltd">
  <meta name="robots" content="index, follow, max-image-preview:large, max-snippet:-1, max-video-preview:-1">

  <!-- Canonical URL -->
  <link rel="canonical" href="<?= htmlspecialchars($canonical_url) ?>">

  <!-- Multilingual Hreflang Alternates -->
<?= tn_hreflang_tags($canonical_path) ?>

  <!-- Open Graph / Facebook -->
  <meta property="og:site_name" content="TezzNative Official Language Portal">
  <meta property="og:title" content="<?= htmlspecialchars($page_title) ?>">
  <meta property="og:description" content="<?= htmlspecialchars($page_desc) ?>">
  <meta property="og:type" content="website">
  <meta property="og:url" content="<?= htmlspecialchars($canonical_url) ?>">
  <meta property="og:locale" content="<?= htmlspecialchars($active_locale) ?>">
  <meta property="og:image" content="https://tezznative.org/assets/logo.png">
  <meta property="og:image:alt" content="TezzNative Official Logo">

  <!-- Twitter Card -->
  <meta name="twitter:card" content="summary_large_image">
  <meta name="twitter:site" content="@TezzCorp">
  <meta name="twitter:creator" content="@TezzCorp">
  <meta name="twitter:title" content="<?= htmlspecialchars($page_title) ?>">
  <meta name="twitter:description" content="<?= htmlspecialchars($page_desc) ?>">
  <meta name="twitter:image" content="https://tezznative.org/assets/logo.png">

  <!-- Favicons -->
  <link rel="icon" type="image/x-icon" href="/favicon.ico">
  <link rel="apple-touch-icon" href="/assets/logo.png">

  <!-- Google Fonts: Inter & JetBrains Mono -->
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700;800&family=JetBrains+Mono:ital,wght@0,400;0,500;0,600;0,700;1,400&display=swap" rel="stylesheet">

  <!-- Core App Stylesheets -->
  <link rel="stylesheet" href="/assets/app.css?v=2.2.6">

  <!-- JSON-LD Structured Data Schema -->
  <script type="application/ld+json">
  {
    "@context": "https://schema.org",
    "@type": "SoftwareApplication",
    "name": "TezzNative",
    "operatingSystem": "Windows 10, Windows 11, Linux x86_64, macOS",
    "applicationCategory": "DeveloperApplication",
    "programmingLanguage": "TezzNative",
    "description": "Ultra-fast systems and AI programming language combining Pythonic syntax with bare-metal C performance, native 4D tensors, coroutines, and zero-GC standalone binaries.",
    "url": "https://tezznative.org",
    "downloadUrl": "https://tezznative.org/download/",
    "softwareVersion": "2.2.1",
    "license": "https://opensource.org/licenses/MIT",
    "author": {
      "@type": "Person",
      "name": "Rohit Pathak",
      "jobTitle": "Creator & Lead Architect",
      "worksFor": {
        "@type": "Organization",
        "name": "TezzCorp Pvt Ltd.",
        "url": "https://tezzcorp.com"
      }
    },
    "publisher": {
      "@type": "Organization",
      "name": "TezzCorp Pvt Ltd.",
      "url": "https://tezzcorp.com",
      "logo": "https://tezznative.org/assets/logo.png"
    }
  }
  </script>
</head>
<body>
<div class="site-wrapper">
<?php
require_once __DIR__ . '/nav.php';
if (!isset($suppress_nav) || !$suppress_nav) {
  tn_nav($active_nav);
}
?>
