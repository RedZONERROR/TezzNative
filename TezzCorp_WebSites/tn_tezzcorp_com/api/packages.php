<?php
// api/packages.php - REST API for TezzNative Package Registry

$packages = [
  [
    'name' => 'tztensor',
    'version' => '2.2.0',
    'category' => 'ai',
    'category_name' => 'AI & Machine Learning',
    'desc' => 'High-performance n-dimensional tensor library with AVX2/NEON vectorization, infix @ matrix multiplication, and 4D slicing sugar.',
    'install' => 'tezz mod add tztensor'
  ],
  [
    'name' => 'tzautodiff',
    'version' => '2.2.0',
    'category' => 'ai',
    'category_name' => 'AI & Machine Learning',
    'desc' => 'Dynamic computation graph and automatic reverse-mode differentiation tape for deep neural network training.',
    'install' => 'tezz mod add tzautodiff'
  ],
  [
    'name' => 'tzgguf',
    'version' => '2.2.0',
    'category' => 'ai',
    'category_name' => 'AI & Machine Learning',
    'desc' => 'Zero-dependency native binary GGUF v3 model ingestion engine supporting Q4_0, Q8_0, and f16 quantized tensor loading.',
    'install' => 'tezz mod add tzgguf'
  ],
  [
    'name' => 'tzsafetensors',
    'version' => '2.2.0',
    'category' => 'ai',
    'category_name' => 'AI & Machine Learning',
    'desc' => 'HuggingFace Safetensors file format loader with zero-copy memory mapping for transformer weight parameters.',
    'install' => 'tezz mod add tzsafetensors'
  ],
  [
    'name' => 'tztokenizer',
    'version' => '2.2.0',
    'category' => 'ai',
    'category_name' => 'AI & Machine Learning',
    'desc' => 'High-throughput Byte-Pair Encoding (BPE) and WordPiece tokenizer runtime for Large Language Models.',
    'install' => 'tezz mod add tztokenizer'
  ],
  [
    'name' => 'tzoptim',
    'version' => '2.2.0',
    'category' => 'ai',
    'category_name' => 'AI & Machine Learning',
    'desc' => 'Gradient-based optimization algorithms including Adam, AdamW, RMSProp, and momentum SGD with weight decay.',
    'install' => 'tezz mod add tzoptim'
  ],
  [
    'name' => 'tzattention',
    'version' => '2.2.0',
    'category' => 'ai',
    'category_name' => 'AI & Machine Learning',
    'desc' => 'FlashAttention-style scaled dot-product attention, Rotary Position Embeddings (RoPE), and KV-cache management.',
    'install' => 'tezz mod add tzattention'
  ],
  [
    'name' => 'task',
    'version' => '2.2.0',
    'category' => 'async',
    'category_name' => 'Concurrency & I/O',
    'desc' => 'Native asynchronous task spawning, hardware worker threads, and await event loop integration for non-blocking coroutines.',
    'install' => 'tezz mod add task'
  ],
  [
    'name' => 'net',
    'version' => '2.2.0',
    'category' => 'web',
    'category_name' => 'Networking & Web',
    'desc' => 'High-throughput async TCP/UDP socket abstractions, HTTP/1.1 and HTTP/2 micro-servers, and client request utilities.',
    'install' => 'tezz mod add net'
  ],
  [
    'name' => 'io',
    'version' => '2.2.0',
    'category' => 'system',
    'category_name' => 'Systems & Low-level',
    'desc' => 'Low-level file system primitives, buffered readers/writers, memory-mapped files, and scoped stream descriptors.',
    'install' => 'tezz mod add io'
  ],
  [
    'name' => 'tezzui',
    'version' => '2.2.0',
    'category' => 'gui',
    'category_name' => 'GUI & Graphics',
    'desc' => 'Immediate-mode hardware-accelerated desktop GUI toolkit with responsive layouts, typography, and canvas rendering.',
    'install' => 'tezz mod add tezzui'
  ],
  [
    'name' => 'crypto',
    'version' => '2.2.0',
    'category' => 'system',
    'category_name' => 'Systems & Low-level',
    'desc' => 'Cryptographic primitives including SHA-256, SHA-512, AES-256-GCM, HMAC, and cryptographically secure random entropy.',
    'install' => 'tezz mod add crypto'
  ],
  [
    'name' => 'json',
    'version' => '2.2.0',
    'category' => 'data',
    'category_name' => 'Data & Storage',
    'desc' => 'SIMD-accelerated JSON parser and serializer with direct struct reflection and zero allocations on numeric paths.',
    'install' => 'tezz mod add json'
  ],
  [
    'name' => 'sql',
    'version' => '2.2.0',
    'category' => 'data',
    'category_name' => 'Data & Storage',
    'desc' => 'High-speed embedded SQLite and network MySQL driver interfaces with parameterized queries and connection pooling.',
    'install' => 'tezz mod add sql'
  ],
  [
    'name' => 'channel',
    'version' => '2.2.0',
    'category' => 'async',
    'category_name' => 'Concurrency & I/O',
    'desc' => 'Thread-safe lockless multi-producer multi-consumer typed message queues with select statement semantics.',
    'install' => 'tezz mod add channel'
  ],
  [
    'name' => 'time',
    'version' => '2.2.0',
    'category' => 'system',
    'category_name' => 'Systems & Low-level',
    'desc' => 'Monotonic nanosecond clocks, duration math, timezone formatting, and high-resolution interval timers.',
    'install' => 'tezz mod add time'
  ]
];

if (basename($_SERVER['SCRIPT_FILENAME'] ?? '') === 'packages.php' || (isset($_SERVER['HTTP_ACCEPT']) && strpos($_SERVER['HTTP_ACCEPT'], 'application/json') !== false)) {
  header('Content-Type: application/json');
  header('Access-Control-Allow-Origin: *');
  $query = isset($_GET['q']) ? strtolower(trim($_GET['q'])) : '';
  $category = isset($_GET['category']) ? strtolower(trim($_GET['category'])) : '';

  $results = array_filter($packages, function ($pkg) use ($query, $category) {
    $matchesQuery = empty($query) || (strpos(strtolower($pkg['name']), $query) !== false) || (strpos(strtolower($pkg['desc']), $query) !== false);
    $matchesCat = empty($category) || ($category === 'all') || ($pkg['category'] === $category);
    return $matchesQuery && $matchesCat;
  });

  echo json_encode([
    'status' => 'success',
    'total' => count($packages),
    'count' => count($results),
    'packages' => array_values($results)
  ], JSON_PRETTY_PRINT);
  exit;
}
