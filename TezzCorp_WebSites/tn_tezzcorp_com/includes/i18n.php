<?php
declare(strict_types=1);

// includes/i18n.php - TezzNative Internationalization & Localization Engine
// Supports: en (English), hi (Hindi), es (Spanish), zh (Chinese), ja (Japanese), de (German), fr (French), ru (Russian)

if (!defined('TN_I18N_LOADED')) {
    define('TN_I18N_LOADED', true);

    global $TN_LANGUAGES, $TN_CURRENT_LANG, $TN_TRANSLATIONS;

    $TN_LANGUAGES = [
        'en' => ['name' => 'English', 'native' => 'English', 'flag' => 'US', 'locale' => 'en_US'],
        'hi' => ['name' => 'Hindi', 'native' => 'हिन्दी', 'flag' => 'IN', 'locale' => 'hi_IN'],
        'es' => ['name' => 'Spanish', 'native' => 'Español', 'flag' => 'ES', 'locale' => 'es_ES'],
        'zh' => ['name' => 'Chinese', 'native' => '简体中文', 'flag' => 'CN', 'locale' => 'zh_CN'],
        'ja' => ['name' => 'Japanese', 'native' => '日本語', 'flag' => 'JP', 'locale' => 'ja_JP'],
        'de' => ['name' => 'German', 'native' => 'Deutsch', 'flag' => 'DE', 'locale' => 'de_DE'],
        'fr' => ['name' => 'French', 'native' => 'Français', 'flag' => 'FR', 'locale' => 'fr_FR'],
        'ru' => ['name' => 'Russian', 'native' => 'Русский', 'flag' => 'RU', 'locale' => 'ru_RU'],
    ];

    // Detect language from query, cookie, or header
    $selected_lang = 'en';
    if (!empty($_GET['lang']) && is_string($_GET['lang'])) {
        $candidate = strtolower(trim($_GET['lang']));
        if (array_key_exists($candidate, $TN_LANGUAGES)) {
            $selected_lang = $candidate;
            // Set cookie for 30 days
            if (!headers_sent()) {
                setcookie('tn_lang', $selected_lang, time() + (86400 * 30), '/', '', isset($_SERVER['HTTPS']), false);
            }
        }
    } elseif (!empty($_COOKIE['tn_lang']) && is_string($_COOKIE['tn_lang'])) {
        $candidate = strtolower(trim($_COOKIE['tn_lang']));
        if (array_key_exists($candidate, $TN_LANGUAGES)) {
            $selected_lang = $candidate;
        }
    } elseif (!empty($_SERVER['HTTP_ACCEPT_LANGUAGE'])) {
        $accept = strtolower($_SERVER['HTTP_ACCEPT_LANGUAGE']);
        foreach ($TN_LANGUAGES as $code => $meta) {
            if (strpos($accept, $code) === 0 || strpos($accept, ',' . $code) !== false) {
                $selected_lang = $code;
                break;
            }
        }
    }

    $TN_CURRENT_LANG = $selected_lang;

    // Translation Dictionaries
    $TN_TRANSLATIONS = [
        // English (Default)
        'en' => [
            'banner_badge' => 'NEW IN v2.2',
            'banner_text' => 'Native async/await Coroutines, Scoped defer, 4D Tensors & GGUF Ingestion Are Live!',
            'banner_link' => 'Get TezzNative v2.2',
            'nav_features' => 'Features',
            'nav_benchmarks' => 'Benchmarks',
            'nav_playground' => 'Playground',
            'nav_packages' => 'Packages (40+)',
            'nav_lsp' => 'LSP & IDEs',
            'nav_docs' => 'Docs',
            'nav_more' => 'More',
            'nav_downloads' => 'Downloads',
            'nav_install_cli' => 'Install Tezz',
            'hero_badge' => 'TEZZNATIVE v2.2.1 PRODUCTION RELEASE LIVE • TEZZCORP PVT LTD',
            'hero_title_1' => 'Ultra-Fast Systems.',
            'hero_title_2' => 'Native Deep Learning.',
            'hero_title_3' => 'Engineered for Hardware.',
            'hero_subtitle' => 'A premier systems and AI programming language created by <strong>Rohit Pathak</strong> at <strong>TezzCorp Pvt Ltd</strong>. Compiling directly to zero-overhead standalone native executables with first-class tensor arithmetic, multi-threaded coroutines, and scoped memory safety without garbage collection pauses.',
            'hero_btn_install' => 'Install TezzNative',
            'hero_btn_lsp' => 'LSP & IDE Setup',
            'hero_btn_explore' => 'Explore 40+ Packages',
            'hero_stat_speed' => '0.3ms',
            'hero_stat_speed_desc' => 'Native Startup Overhead',
            'hero_stat_mem' => '0 KB',
            'hero_stat_mem_desc' => 'Zero GC Runtime Pauses',
            'hero_stat_packages' => '40+',
            'hero_stat_packages_desc' => 'Engineered Standard Libraries',
            'hero_stat_license' => 'MIT',
            'hero_stat_license_desc' => 'Commercial-Ready Freedom',
            'dl_title' => 'Install TezzNative v2.2',
            'dl_subtitle' => 'Install the complete toolchain via our fast CLI one-liners or download standalone compiler binaries, Language Server Protocol daemon, and SDK archives.',
            'dl_cmd_ps' => '# Windows PowerShell (Automated Setup - Adds to PATH & Environment):',
            'dl_cmd_bash' => '# Linux / macOS (Automated Bash Setup):',
            'dl_cmd_cmd' => '# Windows Command Prompt (cmd.exe):',
            'dl_win_title' => 'Windows SDK Package',
            'dl_win_desc' => 'Complete standalone bundle with tezzc.exe compiler, tezz project manager, all 40+ standard library modules, AI kernels, and launch scripts.',
            'dl_linux_title' => 'Linux SDK Package',
            'dl_linux_desc' => 'Official Linux toolchain bundle with native ELF compiler, POSIX standard libraries, async networking runtime, and package management tools.',
            'footer_bio' => 'Ultra-fast native AI, tensor arithmetic, and systems programming language founded on 18 Oct 2022 by Rohit Pathak at TezzCorp Pvt Ltd. Featuring zero runtime GC pauses, standalone PE/ELF binaries, first-class async/await concurrency, and native GGUF LLM inference.',
            'footer_copyright' => 'TezzCorp Pvt Ltd. — TezzNative, founded 18 Oct 2022 by Rohit Pathak. All rights reserved.',
            'footer_operational' => 'TezzNative v2.2.1 Production Operational',
            'lang_selector' => 'Language',
        ],

        // Hindi (हिन्दी)
        'hi' => [
            'banner_badge' => 'v2.2 में नया',
            'banner_text' => 'नेटिव async/await कोराउटिन्स, स्कोप्ड defer, 4D टेंसर और GGUF इन्जेक्शन अब लाइव हैं!',
            'banner_link' => 'TezzNative v2.2 प्राप्त करें',
            'nav_features' => 'विशेषताएं',
            'nav_benchmarks' => 'बेंचमार्क',
            'nav_playground' => 'प्लेग्राउंड',
            'nav_packages' => 'पैकेजेस (40+)',
            'nav_lsp' => 'LSP और IDEs',
            'nav_docs' => 'दस्तावेज़',
            'nav_more' => 'और भी',
            'nav_downloads' => 'डाउनलोड',
            'nav_install_cli' => 'Tezz स्थापित करें',
            'hero_badge' => 'TEZZNATIVE v2.2.1 प्रोडक्शन रिलीज लाइव • तेजकॉर्प प्राइवेट लिमिटेड',
            'hero_title_1' => 'अल्ट्रा-फास्ट सिस्टम्स।',
            'hero_title_2' => 'नेटिव डीप लर्निंग।',
            'hero_title_3' => 'हार्डवेयर के लिए निर्मित।',
            'hero_subtitle' => '<strong>रोहित पाठक</strong> द्वारा <strong>तेजकॉर्प प्राइवेट लिमिटेड</strong> में विकसित एक प्रमुख सिस्टम्स और एआई प्रोग्रामिंग भाषा। बिना किसी गारबेज कलेक्शन रुकावट के जीरो-ओवरहेड नेटिव एक्जीक्यूटेबल्स, टेंसर अंकगणित और मल्टी-थ्रेडेड कॉनकरेंसी में संकलित होती है।',
            'hero_btn_install' => 'TezzNative इंस्टॉल करें',
            'hero_btn_lsp' => 'LSP और IDE सेटअप',
            'hero_btn_explore' => '40+ पैकेजेस देखें',
            'hero_stat_speed' => '0.3ms',
            'hero_stat_speed_desc' => 'नेटिव स्टार्टअप ओवरहेड',
            'hero_stat_mem' => '0 KB',
            'hero_stat_mem_desc' => 'शून्य GC रुकावटें',
            'hero_stat_packages' => '40+',
            'hero_stat_packages_desc' => 'इंजीनियर्ड मानक लाइब्रेरीज़',
            'hero_stat_license' => 'MIT',
            'hero_stat_license_desc' => 'व्यावसायिक उपयोग के लिए स्वतंत्र',
            'dl_title' => 'TezzNative v2.2 इंस्टॉल करें',
            'dl_subtitle' => 'हमारे तेज़ CLI वन-लाइनर्स के माध्यम से पूरा टूलचेन इंस्टॉल करें या स्टैंडअलोन कंपाइलर बाइनरी और SDK डाउनलोड करें।',
            'dl_cmd_ps' => '# Windows PowerShell (स्वचालित सेटअप - PATH में जोड़ता है):',
            'dl_cmd_bash' => '# Linux / macOS (स्वचालित बैश सेटअप):',
            'dl_cmd_cmd' => '# Windows Command Prompt (cmd.exe):',
            'dl_win_title' => 'Windows SDK पैकेज',
            'dl_win_desc' => 'tezzc.exe कंपाइलर, tezz प्रोजेक्ट मैनेजर, 40+ मानक लाइब्रेरी मॉड्यूल और एआई कर्नेल के साथ पूरा स्टैंडअलोन बंडल।',
            'dl_linux_title' => 'Linux SDK पैकेज',
            'dl_linux_desc' => 'नेटिव ELF कंपाइलर, POSIX मानक लाइब्रेरीज़ और पैकेज मैनेजमेंट टूल्स के साथ आधिकारिक लिनक्स टूलचेन।',
            'footer_bio' => 'रोहित पाठक द्वारा तेजकॉर्प प्राइवेट लिमिटेड में 18 अक्टूबर 2022 को स्थापित अल्ट्रा-फास्ट नेटिव एआई और सिस्टम्स प्रोग्रामिंग भाषा।',
            'footer_copyright' => 'तेजकॉर्प प्राइवेट लिमिटेड — TezzNative, रोहित पाठक द्वारा 18 अक्टूबर 2022 को स्थापित। सर्वाधिकार सुरक्षित।',
            'footer_operational' => 'TezzNative v2.2.1 प्रोडक्शन पूरी तरह सक्रिय',
            'lang_selector' => 'भाषा',
        ],

        // Spanish (Español)
        'es' => [
            'banner_badge' => 'NUEVO EN v2.2',
            'banner_text' => '¡Corrutinas async/await nativas, defer con ámbito, tensores 4D e ingestión GGUF ya disponibles!',
            'banner_link' => 'Obtener TezzNative v2.2',
            'nav_features' => 'Características',
            'nav_benchmarks' => 'Rendimiento',
            'nav_playground' => 'Entorno de pruebas',
            'nav_packages' => 'Paquetes (40+)',
            'nav_lsp' => 'LSP e IDEs',
            'nav_docs' => 'Documentación',
            'nav_more' => 'Más',
            'nav_downloads' => 'Descargas',
            'nav_install_cli' => 'Instalar Tezz',
            'hero_badge' => 'TEZZNATIVE v2.2.1 VERSIÓN DE PRODUCCIÓN EN VIVO • TEZZCORP PVT LTD',
            'hero_title_1' => 'Sistemas Ultrarrápidos.',
            'hero_title_2' => 'Aprendizaje Profundo Nativo.',
            'hero_title_3' => 'Diseñado para el Hardware.',
            'hero_subtitle' => 'Un lenguaje líder de programación de sistemas e IA creado por <strong>Rohit Pathak</strong> en <strong>TezzCorp Pvt Ltd</strong>. Compila directamente a binarios nativos independientes sin sobrecarga, con aritmética de tensores y concurrencia sin pausas de GC.',
            'hero_btn_install' => 'Instalar TezzNative',
            'hero_btn_lsp' => 'Configurar LSP e IDE',
            'hero_btn_explore' => 'Explorar 40+ Paquetes',
            'hero_stat_speed' => '0.3ms',
            'hero_stat_speed_desc' => 'Sobrecarga de Inicio Nativo',
            'hero_stat_mem' => '0 KB',
            'hero_stat_mem_desc' => 'Cero Pausas de GC',
            'hero_stat_packages' => '40+',
            'hero_stat_packages_desc' => 'Bibliotecas Estándar Optimizadas',
            'hero_stat_license' => 'MIT',
            'hero_stat_license_desc' => 'Libre para Uso Comercial',
            'dl_title' => 'Instalar TezzNative v2.2',
            'dl_subtitle' => 'Instale la cadena de herramientas completa mediante comandos rápidos de CLI o descargue binarios independientes del compilador y archivos SDK.',
            'dl_cmd_ps' => '# Windows PowerShell (Instalación automática - Añade al PATH):',
            'dl_cmd_bash' => '# Linux / macOS (Instalación automática Bash):',
            'dl_cmd_cmd' => '# Símbolo del sistema de Windows (cmd.exe):',
            'dl_win_title' => 'Paquete SDK para Windows',
            'dl_win_desc' => 'Paquete independiente completo con compilador tezzc.exe, gestor de proyectos tezz y los 40+ módulos estándar.',
            'dl_linux_title' => 'Paquete SDK para Linux',
            'dl_linux_desc' => 'Cadena de herramientas oficial de Linux con compilador ELF nativo, bibliotecas estándar POSIX y herramientas de paquetes.',
            'footer_bio' => 'Lenguaje ultrarrápido para IA nativa y sistemas fundado el 18 de octubre de 2022 por Rohit Pathak en TezzCorp Pvt Ltd.',
            'footer_copyright' => 'TezzCorp Pvt Ltd. — TezzNative, fundado el 18 de oct. de 2022 por Rohit Pathak. Todos los derechos reservados.',
            'footer_operational' => 'TezzNative v2.2.1 Producción Operativa',
            'lang_selector' => 'Idioma',
        ],

        // Chinese (简体中文)
        'zh' => [
            'banner_badge' => 'v2.2 新特性',
            'banner_text' => '原生 async/await 协程、作用域 defer、4D 张量与 GGUF 模型加载现已发布！',
            'banner_link' => '获取 TezzNative v2.2',
            'nav_features' => '语言特性',
            'nav_benchmarks' => '基准测试',
            'nav_playground' => '在线试验场',
            'nav_packages' => '标准包 (40+)',
            'nav_lsp' => 'LSP 与 IDE 支持',
            'nav_docs' => '技术文档',
            'nav_more' => '更多',
            'nav_downloads' => '下载中心',
            'nav_install_cli' => '安装 Tezz',
            'hero_badge' => 'TEZZNATIVE v2.2.1 正式生产版本现已上线 • TEZZCORP PVT LTD',
            'hero_title_1' => '极致快速系统级开发。',
            'hero_title_2' => '原生深度学习。',
            'hero_title_3' => '专为底层硬件设计。',
            'hero_subtitle' => '由 <strong>Rohit Pathak</strong> 在 <strong>TezzCorp Pvt Ltd</strong> 打造的顶级系统与 AI 编程语言。直接编译为零开销独立原生二进制文件，具备一等张量算术、多线程协程，无 GC 垃圾回收停顿。',
            'hero_btn_install' => '立即安装 TezzNative',
            'hero_btn_lsp' => 'LSP 与 IDE 配置',
            'hero_btn_explore' => '浏览 40+ 标准包',
            'hero_stat_speed' => '0.3ms',
            'hero_stat_speed_desc' => '原生启动开销',
            'hero_stat_mem' => '0 KB',
            'hero_stat_mem_desc' => '零 GC 垃圾回收停顿',
            'hero_stat_packages' => '40+',
            'hero_stat_packages_desc' => '全套工程级标准库',
            'hero_stat_license' => 'MIT',
            'hero_stat_license_desc' => '商业级自由开源协议',
            'dl_title' => '安装 TezzNative v2.2',
            'dl_subtitle' => '通过极速 CLI 单行指令安装完整工具链，或直接下载独立编译器二进制文件与 SDK 压缩包。',
            'dl_cmd_ps' => '# Windows PowerShell (自动配置 PATH 与环境变量):',
            'dl_cmd_bash' => '# Linux / macOS (自动 Bash 安装):',
            'dl_cmd_cmd' => '# Windows 命令提示符 (cmd.exe):',
            'dl_win_title' => 'Windows SDK 完整包',
            'dl_win_desc' => '包含 tezzc.exe 编译器、tezz 项目管理器、全部 40+ 标准库与 AI 内核的独立包。',
            'dl_linux_title' => 'Linux SDK 完整包',
            'dl_linux_desc' => '官方 Linux 工具链，包含原生 ELF 编译器、POSIX 标准库及包管理工具。',
            'footer_bio' => '由 Rohit Pathak 于 2022 年 10 月 18 日在 TezzCorp Pvt Ltd 创立的高性能原生 AI 与系统编程语言。',
            'footer_copyright' => 'TezzCorp Pvt Ltd. — TezzNative, 2022年10月18日由 Rohit Pathak 创立。保留所有权利。',
            'footer_operational' => 'TezzNative v2.2.1 生产环境运行稳定',
            'lang_selector' => '语言',
        ],

        // Japanese (日本語)
        'ja' => [
            'banner_badge' => 'v2.2 新機能',
            'banner_text' => 'ネイティブ async/await コルーチン、スコープ付き defer、4D テンソル、GGUF サポートが利用可能！',
            'banner_link' => 'TezzNative v2.2 を入手',
            'nav_features' => '機能',
            'nav_benchmarks' => 'ベンチマーク',
            'nav_playground' => 'プレイグラウンド',
            'nav_packages' => '標準パッケージ (40+)',
            'nav_lsp' => 'LSP & IDE',
            'nav_docs' => 'ドキュメント',
            'nav_more' => 'その他',
            'nav_downloads' => 'ダウンロード',
            'nav_install_cli' => 'Tezz をインストール',
            'hero_badge' => 'TEZZNATIVE v2.2.1 プロダクション版リリース • TEZZCORP PVT LTD',
            'hero_title_1' => '超高速システム開発。',
            'hero_title_2' => 'ネイティブ深層学習。',
            'hero_title_3' => 'ハードウェア最適化。',
            'hero_subtitle' => '<strong>Rohit Pathak</strong> が <strong>TezzCorp Pvt Ltd</strong> で開発した最高峰のシステム & AI プログラミング言語。GC 停止のないゼロオーバーヘッドのネイティブバイナリへ直接コンパイルします。',
            'hero_btn_install' => 'TezzNative をインストール',
            'hero_btn_lsp' => 'LSP & IDE の設定',
            'hero_btn_explore' => '40以上のパッケージを見る',
            'hero_stat_speed' => '0.3ms',
            'hero_stat_speed_desc' => 'ネイティブ起動オーバーヘッド',
            'hero_stat_mem' => '0 KB',
            'hero_stat_mem_desc' => 'GC による一時停止ゼロ',
            'hero_stat_packages' => '40+',
            'hero_stat_packages_desc' => '設計済み標準ライブラリ',
            'hero_stat_license' => 'MIT',
            'hero_stat_license_desc' => '商用利用可能なライセンス',
            'dl_title' => 'TezzNative v2.2 のインストール',
            'dl_subtitle' => '高速 CLI コマンドにより完全なツールチェーンをインストール、またはスタンドアロンバイナリと SDK をダウンロード。',
            'dl_cmd_ps' => '# Windows PowerShell (自動セットアップ - PATH 設定対応):',
            'dl_cmd_bash' => '# Linux / macOS (自動 Bash セットアップ):',
            'dl_cmd_cmd' => '# Windows コマンドプロンプト (cmd.exe):',
            'dl_win_title' => 'Windows SDK パッケージ',
            'dl_win_desc' => 'tezzc.exe コンパイラ、tezz プロジェクトマネージャー、全40以上の標準モジュールを含む完全バンドル。',
            'dl_linux_title' => 'Linux SDK パッケージ',
            'dl_linux_desc' => 'ELF ネイティブコンパイラ、POSIX 標準ライブラリ、パッケージ管理ツールを備えた公式ツールチェーン。',
            'footer_bio' => '2022年10月18日に Rohit Pathak によって設立された、超高速ネイティブ AI およびシステム言語。',
            'footer_copyright' => 'TezzCorp Pvt Ltd. — TezzNative, 2022年10月18日 Rohit Pathak 創設。All rights reserved.',
            'footer_operational' => 'TezzNative v2.2.1 正常稼働中',
            'lang_selector' => '言語',
        ],

        // German (Deutsch)
        'de' => [
            'banner_badge' => 'NEU IN v2.2',
            'banner_text' => 'Native async/await Coroutinen, bereichsbasiertes defer, 4D-Tensoren & GGUF-Unterstützung sind live!',
            'banner_link' => 'TezzNative v2.2 herunterladen',
            'nav_features' => 'Funktionen',
            'nav_benchmarks' => 'Benchmarks',
            'nav_playground' => 'Playground',
            'nav_packages' => 'Pakete (40+)',
            'nav_lsp' => 'LSP & IDEs',
            'nav_docs' => 'Dokumentation',
            'nav_more' => 'Mehr',
            'nav_downloads' => 'Downloads',
            'nav_install_cli' => 'Tezz installieren',
            'hero_badge' => 'TEZZNATIVE v2.2.1 PRODUKTIONS-RELEASE LIVE • TEZZCORP PVT LTD',
            'hero_title_1' => 'Ultraschnelle Systeme.',
            'hero_title_2' => 'Natives Deep Learning.',
            'hero_title_3' => 'Entwickelt für Hardware.',
            'hero_subtitle' => 'Eine erstklassige System- und KI-Programmiersprache, entwickelt von <strong>Rohit Pathak</strong> bei <strong>TezzCorp Pvt Ltd</strong>. Kompiliert direkt zu eigenständigen nativen Binärdateien ohne GC-Pausen.',
            'hero_btn_install' => 'TezzNative installieren',
            'hero_btn_lsp' => 'LSP & IDE Einrichtung',
            'hero_btn_explore' => '40+ Pakete entdecken',
            'hero_stat_speed' => '0.3ms',
            'hero_stat_speed_desc' => 'Nativer Start-Overhead',
            'hero_stat_mem' => '0 KB',
            'hero_stat_mem_desc' => 'Null GC-Laufzeitpausen',
            'hero_stat_packages' => '40+',
            'hero_stat_packages_desc' => 'Standardbibliotheken',
            'hero_stat_license' => 'MIT',
            'hero_stat_license_desc' => 'Kommerziell einsetzbar',
            'dl_title' => 'TezzNative v2.2 installieren',
            'dl_subtitle' => 'Installieren Sie die Toolchain über unsere schnellen CLI-Befehle oder laden Sie eigenständige Compiler-Binärdateien herunter.',
            'dl_cmd_ps' => '# Windows PowerShell (Automatisierte Installation - Fügt zu PATH hinzu):',
            'dl_cmd_bash' => '# Linux / macOS (Automatisierte Bash-Installation):',
            'dl_cmd_cmd' => '# Windows Eingabeaufforderung (cmd.exe):',
            'dl_win_title' => 'Windows SDK Paket',
            'dl_win_desc' => 'Komplettes Bundle mit tezzc.exe Compiler, tezz Projektmanager und allen 40+ Standardbibliotheken.',
            'dl_linux_title' => 'Linux SDK Paket',
            'dl_linux_desc' => 'Offizielles Linux-Bundle mit nativem ELF-Compiler und POSIX-Standardbibliotheken.',
            'footer_bio' => 'Ultraschnelle native KI- und Systemprogrammiersprache, gegründet am 18. Okt. 2022 von Rohit Pathak bei TezzCorp Pvt Ltd.',
            'footer_copyright' => 'TezzCorp Pvt Ltd. — TezzNative, gegründet am 18. Okt. 2022 von Rohit Pathak. Alle Rechte vorbehalten.',
            'footer_operational' => 'TezzNative v2.2.1 Produktion betriebsbereit',
            'lang_selector' => 'Sprache',
        ],

        // French (Français)
        'fr' => [
            'banner_badge' => 'NOUVEAU DANS v2.2',
            'banner_text' => 'Coroutines async/await natives, defer délimité, tenseurs 4D et ingestion GGUF disponibles !',
            'banner_link' => 'Obtenir TezzNative v2.2',
            'nav_features' => 'Fonctionnalités',
            'nav_benchmarks' => 'Performances',
            'nav_playground' => 'Bac à sable',
            'nav_packages' => 'Paquets (40+)',
            'nav_lsp' => 'LSP et IDEs',
            'nav_docs' => 'Documentation',
            'nav_more' => 'Plus',
            'nav_downloads' => 'Téléchargements',
            'nav_install_cli' => 'Installer Tezz',
            'hero_badge' => 'TEZZNATIVE v2.2.1 VERSION PRODUCTION EN LIGNE • TEZZCORP PVT LTD',
            'hero_title_1' => 'Systèmes Ultra-Rapides.',
            'hero_title_2' => 'Apprentissage Profond Natif.',
            'hero_title_3' => 'Conçu pour le Matériel.',
            'hero_subtitle' => 'Un langage de programmation système et IA de pointe créé par <strong>Rohit Pathak</strong> chez <strong>TezzCorp Pvt Ltd</strong>. Compilation directe en binaires natifs autonomes sans pauses de ramasse-miettes.',
            'hero_btn_install' => 'Installer TezzNative',
            'hero_btn_lsp' => 'Configuration LSP et IDE',
            'hero_btn_explore' => 'Explorer 40+ Paquets',
            'hero_stat_speed' => '0.3ms',
            'hero_stat_speed_desc' => 'Démarrage Natif Immédiat',
            'hero_stat_mem' => '0 KB',
            'hero_stat_mem_desc' => 'Zéro Pause de Garbage Collector',
            'hero_stat_packages' => '40+',
            'hero_stat_packages_desc' => 'Bibliothèques Standard Complètes',
            'hero_stat_license' => 'MIT',
            'hero_stat_license_desc' => 'Prêt pour l\'Entreprise',
            'dl_title' => 'Installer TezzNative v2.2',
            'dl_subtitle' => 'Installez la chaîne d\'outils complète via nos commandes CLI ou téléchargez les binaires du compilateur et archives SDK.',
            'dl_cmd_ps' => '# Windows PowerShell (Configuration automatique - Ajoute au PATH) :',
            'dl_cmd_bash' => '# Linux / macOS (Installation Bash automatique) :',
            'dl_cmd_cmd' => '# Invite de commandes Windows (cmd.exe) :',
            'dl_win_title' => 'Paquet SDK Windows',
            'dl_win_desc' => 'Ensemble complet avec compilateur tezzc.exe, gestionnaire tezz et plus de 40 modules standard.',
            'dl_linux_title' => 'Paquet SDK Linux',
            'dl_linux_desc' => 'Outils officiels Linux avec compilateur ELF natif, bibliothèques POSIX et gestionnaire de paquets.',
            'footer_bio' => 'Langage ultra-rapide pour IA native et systèmes fondé le 18 oct. 2022 par Rohit Pathak chez TezzCorp Pvt Ltd.',
            'footer_copyright' => 'TezzCorp Pvt Ltd. — TezzNative, fondé le 18 oct. 2022 par Rohit Pathak. Tous droits réservés.',
            'footer_operational' => 'TezzNative v2.2.1 Opérationnel en Production',
            'lang_selector' => 'Langue',
        ],

        // Russian (Русский)
        'ru' => [
            'banner_badge' => 'НОВОЕ В v2.2',
            'banner_text' => 'Нативные корутины async/await, блочный defer, 4D тензоры и загрузка GGUF уже доступны!',
            'banner_link' => 'Скачать TezzNative v2.2',
            'nav_features' => 'Возможности',
            'nav_benchmarks' => 'Тесты скорости',
            'nav_playground' => 'Песочница',
            'nav_packages' => 'Пакеты (40+)',
            'nav_lsp' => 'LSP и IDE',
            'nav_docs' => 'Документация',
            'nav_more' => 'Ещё',
            'nav_downloads' => 'Загрузки',
            'nav_install_cli' => 'Установить Tezz',
            'hero_badge' => 'TEZZNATIVE v2.2.1 ПРОДАКШН РЕЛИЗ • TEZZCORP PVT LTD',
            'hero_title_1' => 'Сверхбыстрые Системы.',
            'hero_title_2' => 'Нативное Машинное Обучение.',
            'hero_title_3' => 'Создан для Железа.',
            'hero_subtitle' => 'Передовой язык системного программирования и ИИ, созданный <strong>Rohit Pathak</strong> в <strong>TezzCorp Pvt Ltd</strong>. Компилируется напрямую в автономные нативные бинарные файлы без пауз сборщика мусора.',
            'hero_btn_install' => 'Установить TezzNative',
            'hero_btn_lsp' => 'Настройка LSP и IDE',
            'hero_btn_explore' => 'Обзор 40+ пакетов',
            'hero_stat_speed' => '0.3ms',
            'hero_stat_speed_desc' => 'Время запуска нативного бинарника',
            'hero_stat_mem' => '0 KB',
            'hero_stat_mem_desc' => 'Ноль пауз сборщика мусора (GC)',
            'hero_stat_packages' => '40+',
            'hero_stat_packages_desc' => 'Стандартные библиотеки',
            'hero_stat_license' => 'MIT',
            'hero_stat_license_desc' => 'Полная свобода коммерческого использования',
            'dl_title' => 'Установка TezzNative v2.2',
            'dl_subtitle' => 'Установите весь инструментарий с помощью быстрых CLI команд или скачайте готовые бинарные сборки и SDK.',
            'dl_cmd_ps' => '# Windows PowerShell (Автоматическая установка и добавление в PATH):',
            'dl_cmd_bash' => '# Linux / macOS (Автоматическая установка через Bash):',
            'dl_cmd_cmd' => '# Командная строка Windows (cmd.exe):',
            'dl_win_title' => 'Пакет Windows SDK',
            'dl_win_desc' => 'Полный комплект с компилятором tezzc.exe, менеджером пакетов tezz и всеми 40+ модулями.',
            'dl_linux_title' => 'Пакет Linux SDK',
            'dl_linux_desc' => 'Официальный инструментарий для Linux с нативным ELF компилятором и библиотеками POSIX.',
            'footer_bio' => 'Сверхбыстрый нативный язык для ИИ и системного программирования, основанный 18 октября 2022 года Rohit Pathak в TezzCorp Pvt Ltd.',
            'footer_copyright' => 'TezzCorp Pvt Ltd. — TezzNative, основан 18 октября 2022 г. Rohit Pathak. Все права защищены.',
            'footer_operational' => 'TezzNative v2.2.1 Полностью работоспособен',
            'lang_selector' => 'Язык',
        ],
    ];
}

function tn_get_lang(): string {
    global $TN_CURRENT_LANG;
    return $TN_CURRENT_LANG ?? 'en';
}

function tn_get_locale(): string {
    global $TN_LANGUAGES, $TN_CURRENT_LANG;
    $lang = $TN_CURRENT_LANG ?? 'en';
    return $TN_LANGUAGES[$lang]['locale'] ?? 'en_US';
}

function __(string $key, ?string $default = null): string {
    global $TN_TRANSLATIONS, $TN_CURRENT_LANG;
    $lang = $TN_CURRENT_LANG ?? 'en';
    if (isset($TN_TRANSLATIONS[$lang][$key])) {
        return $TN_TRANSLATIONS[$lang][$key];
    }
    if (isset($TN_TRANSLATIONS['en'][$key])) {
        return $TN_TRANSLATIONS['en'][$key];
    }
    return $default ?? $key;
}

function tn_lang_url(string $targetLang): string {
    $uri = $_SERVER['REQUEST_URI'] ?? '/';
    $parts = parse_url($uri);
    $path = $parts['path'] ?? '/';
    $query = [];
    if (!empty($parts['query'])) {
        parse_str($parts['query'], $query);
    }
    $query['lang'] = $targetLang;
    return $path . '?' . http_build_query($query);
}

function tn_lang_switcher_html(string $instanceId = 'desktop'): string {
    global $TN_LANGUAGES, $TN_CURRENT_LANG;
    $curr = $TN_CURRENT_LANG ?? 'en';
    $currMeta = $TN_LANGUAGES[$curr] ?? $TN_LANGUAGES['en'];
    $btnId = 'langSelectorBtn_' . $instanceId;
    $ddId = 'langDropdown_' . $instanceId;

    $html = '<div class="lang-selector-wrapper" data-instance="' . htmlspecialchars($instanceId) . '">';
    $html .= '<button type="button" class="lang-selector-btn" id="' . $btnId . '" aria-label="Select Language" aria-expanded="false" aria-controls="' . $ddId . '">';
    $html .= '<span class="lang-flag">' . htmlspecialchars($currMeta['flag']) . '</span>';
    $html .= '<span class="lang-name">' . htmlspecialchars($currMeta['native']) . '</span>';
    $html .= '<svg class="lang-chevron" width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M6 9l6 6 6-6"/></svg>';
    $html .= '</button>';

    $html .= '<div class="lang-dropdown" id="' . $ddId . '" role="region" aria-label="Language Selection">';

    foreach ($TN_LANGUAGES as $code => $meta) {
        $isActive = ($code === $curr);
        $activeClass = $isActive ? ' active' : '';
        $html .= '<a href="' . htmlspecialchars(tn_lang_url($code)) . '" class="lang-option' . $activeClass . '">';
        $html .= '<span class="lang-option-flag">' . htmlspecialchars($meta['flag']) . '</span>';
        $html .= '<span class="lang-option-text">' . htmlspecialchars($meta['native']) . ' <small>(' . htmlspecialchars($meta['name']) . ')</small></span>';
        if ($isActive) {
            $html .= '<svg class="lang-check" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="#ff9933" stroke-width="2.5"><polyline points="20 6 9 17 4 12"/></svg>';
        }
        $html .= '</a>';
    }

    $html .= '</div>';
    $html .= '</div>';

    return $html;
}

function tn_hreflang_tags(string $canonicalPath = '/'): string {
    global $TN_LANGUAGES;
    $base = 'https://tezznative.org' . rtrim($canonicalPath, '/');
    if ($base === 'https://tezznative.org') {
        $base = 'https://tezznative.org/';
    }

    $tags = '';
    foreach ($TN_LANGUAGES as $code => $meta) {
        $url = ($code === 'en') ? $base : (strpos($base, '?') !== false ? $base . '&amp;lang=' . $code : $base . '?lang=' . $code);
        $tags .= '  <link rel="alternate" hreflang="' . $code . '" href="' . $url . '">' . "\n";
    }
    // x-default points to English
    $tags .= '  <link rel="alternate" hreflang="x-default" href="' . $base . '">' . "\n";
    return $tags;
}
