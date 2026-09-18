<?php
declare(strict_types=1);
require_once __DIR__ . '/_common.php';
tn_doc_boot('/docs/examples', 'examples');
tn_head('TezzNative Examples', 'Searchable TezzNative examples for CLI, files, structs, HTTP, and services.', 'docs', '/docs/examples');
tn_nav('docs');
$examples = [
    ['Hello world', "import \"std\"\n\nfn main() -> int:\n  say \"hello from TezzNative\"\n  ret 0"],
    ['CLI-style flags', "fn flag_enabled(arg:str, name:str) -> int:\n  if strcmp(arg, name) == 0:\n    ret 1\n  ret 0\n\nfn main() -> int:\n  if flag_enabled(\"--verbose\", \"--verbose\") != 0:\n    say \"verbose mode\"\n  ret 0"],
    ['File IO', "import \"io\"\n\nfn main() -> int:\n  path:str = \"out.txt\"\n  data:str = \"saved\"\n  f:*File = io.open_w(path)\n  if f == 0:\n    ret 1\n  p:*char\n  unsafe:\n    p = data as *char\n  io.file_write(f, p, len(data))\n  io.file_close(f)\n  ret 0"],
    ['HTTP parse', "import \"net\"\n\nfn main() -> int:\n  resp:str = \"HTTP/1.1 200 OK\\nContent-Length: 2\\n\\nok\"\n  if net.http_status_code(resp) != 200:\n    ret 1\n  ret 0"],
    ['C extern', "@abi(\"c\")\nextern fn puts(s:str) -> int\n\nfn main() -> int:\n  puts(\"hello from C ABI\")\n  ret 0"],
];
tn_page_shell_start('Examples', 'Small programs that show the stable path.', 'Search examples by feature and copy the snippets directly into a `.tn` file.');
?>
  <section class="tn-section">
    <div class="tn-container tn-doc-layout">
      <?php tn_doc_nav(); ?>
      <div>
        <input class="tn-search" data-filter-input="[data-example-card]" placeholder="Search examples">
        <div class="tn-grid" style="margin-top:1rem">
          <?php foreach ($examples as [$title, $code]): ?>
          <article class="tn-panel" data-example-card>
            <h2><?= htmlspecialchars($title) ?></h2>
            <?php tn_code($title, $code); ?>
          </article>
          <?php endforeach; ?>
        </div>
      </div>
    </div>
  </section>
<?php tn_page_shell_end(); tn_footer(); ?>
