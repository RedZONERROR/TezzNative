<?php
declare(strict_types=1);

function tezz_sdk_lib_dir(): string {
    $base = dirname(__DIR__);
    $deployed = $base . '/download/sdk/lib';
    if (is_dir($deployed)) {
        return $deployed;
    }
    $sourceTree = $base . '/public/download/sdk/lib';
    if (is_dir($sourceTree)) {
        return $sourceTree;
    }
    return $deployed;
}

function tezz_collect_sdk_modules(string $libDir): array {
    if (!is_dir($libDir)) {
        return [];
    }

    $modules = [];
    $files = glob($libDir . '/*.tn') ?: [];
    sort($files);

    foreach ($files as $file) {
        $module = pathinfo($file, PATHINFO_FILENAME);
        $functions = [];
        $comments = [];
        $moduleDescription = '';

        $lines = @file($file, FILE_IGNORE_NEW_LINES);
        if (!is_array($lines)) {
            $lines = [];
        }

        foreach ($lines as $line) {
            $trim = trim($line);
            if ($trim === '' && $moduleDescription === '') {
                continue;
            }
            if (str_starts_with($trim, '//')) {
                $text = trim(substr($trim, 2));
                if ($text !== '') {
                    $moduleDescription = $moduleDescription === '' ? $text : ($moduleDescription . ' ' . $text);
                }
                continue;
            }
            break;
        }

        foreach ($lines as $line) {
            $trim = trim($line);
            if ($trim === '') {
                $comments = [];
                continue;
            }

            if (str_starts_with($trim, '//')) {
                $comments[] = trim(substr($trim, 2));
                if (count($comments) > 3) {
                    array_shift($comments);
                }
                continue;
            }

            if (preg_match('/^\s*(?:extern\s+)?fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)\s*(?:->\s*([^:{]+))?/', $line, $m) === 1) {
                $name = trim($m[1]);
                $args = trim($m[2]);
                $ret = isset($m[3]) ? trim($m[3]) : '';
                $signature = 'fn ' . $name . '(' . $args . ')';
                if ($ret !== '') {
                    $signature .= ' -> ' . $ret;
                }
                $functions[] = [
                    'name' => $name,
                    'signature' => $signature,
                    'description' => $comments ? implode(' ', $comments) : 'Description pending.',
                ];
                $comments = [];
                continue;
            }

            $comments = [];
        }

        $modules[] = [
            'module' => $module,
            'file' => basename($file),
            'description' => $moduleDescription !== '' ? $moduleDescription : 'Core SDK module.',
            'function_count' => count($functions),
            'functions' => $functions,
        ];
    }

    usort($modules, static fn(array $a, array $b): int => strcmp($a['module'], $b['module']));
    return $modules;
}

function tezz_find_tool_source(): string {
    $base = dirname(__DIR__);
    $candidates = [
        $base . '/download/sdk/tools/tezz.tn',
        $base . '/public/download/sdk/tools/tezz.tn',
    ];
    foreach ($candidates as $candidate) {
        if (is_file($candidate)) {
            return $candidate;
        }
    }
    return '';
}

function tezz_v1_feature_audit(array $modules): array {
    $toolPath = tezz_find_tool_source();
    $toolSource = $toolPath !== '' ? (string)@file_get_contents($toolPath) : '';
    $has = static fn(string $needle): bool => $toolSource !== '' && str_contains($toolSource, $needle);

    $moduleNames = array_flip(array_map(static fn(array $m): string => (string)$m['module'], $modules));
    $hasModule = static fn(string $name): bool => isset($moduleNames[$name]);

    return [
        [
            'name' => 'CLI run/build workflow',
            'status' => $has('run <file.tn>') && $has('build <file.tn>') ? 'ready' : 'missing',
            'detail' => 'Single-command entry points for run/build.',
        ],
        [
            'name' => 'Environment diagnostics',
            'status' => $has('doctor') ? 'ready' : 'missing',
            'detail' => 'Checks PATH, compiler, SDK core, and project files.',
        ],
        [
            'name' => 'Smoke testing path',
            'status' => $has('test [--smoke]') ? 'ready' : 'missing',
            'detail' => 'Fast CI/dev sanity tests.',
        ],
        [
            'name' => 'Cross-platform build targets',
            'status' => $has('--platform linux|windows|tezzos|macos|android') ? 'ready' : 'missing',
            'detail' => 'Linux, Windows, macOS, Android, and TezzOS outputs.',
        ],
        [
            'name' => 'Module lock and verification',
            'status' => $has('cmd_lock') && $has('cmd_verify') ? 'ready' : 'missing',
            'detail' => 'Dependency lock and integrity verification commands.',
        ],
        [
            'name' => 'Core std/io/net/math libraries',
            'status' => ($hasModule('std') && $hasModule('io') && $hasModule('net') && $hasModule('math')) ? 'ready' : 'missing',
            'detail' => 'Essential SDK modules packaged in v1.0.0.',
        ],
    ];
}
