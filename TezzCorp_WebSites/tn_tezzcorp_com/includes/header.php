<?php
// includes/header.php - TezzNative Web Portal Header
if (!defined('TN_APP')) define('TN_APP', true);
$page_title = isset($page_title) ? $page_title . ' | TezzNative Programming Language' : 'TezzNative - The Fast, Pythonic Systems & AI Programming Language';
$page_desc = isset($page_desc) ? $page_desc : 'TezzNative combines the clean clarity of Python with bare-metal C speed, native tensors, async/await coroutines, scoped memory cleanup, and zero-overhead standalone binaries.';
$active_nav = isset($active_nav) ? $active_nav : 'home';
?>
<!DOCTYPE html>
<html lang="en" class="dark">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title><?= htmlspecialchars($page_title) ?></title>
  <meta name="description" content="<?= htmlspecialchars($page_desc) ?>">
  <meta name="keywords" content="TezzNative, programming language, AI, tensors, async await, machine learning, compiler, fast, pythonic, C speed, GGUF">
  
  <!-- Open Graph -->
  <meta property="og:title" content="<?= htmlspecialchars($page_title) ?>">
  <meta property="og:description" content="<?= htmlspecialchars($page_desc) ?>">
  <meta property="og:type" content="website">
  <meta property="og:url" content="https://tn.tezzcorp.com/">
  
  <!-- Favicon -->
  <link rel="icon" type="image/x-icon" href="/favicon.ico">
  
  <!-- Google Fonts: Inter & JetBrains Mono -->
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700;800&family=JetBrains+Mono:ital,wght@0,400;0,500;0,600;0,700;1,400&display=swap" rel="stylesheet">
  
  <!-- Stylesheets -->
  <link rel="stylesheet" href="/assets/app.css?v=2.2.4">
  
</head>
<body>
<div class="site-wrapper">
<?php
require_once __DIR__ . '/nav.php';
if (!isset($suppress_nav) || !$suppress_nav) {
  tn_nav($active_nav);
}
?>
