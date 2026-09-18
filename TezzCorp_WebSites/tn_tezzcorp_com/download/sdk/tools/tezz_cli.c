/*
 * tezz.c  —  TezzNative colorful CLI entry point
 *
 * Acts as `tezzc` but with:
 *   - Colored banner on first run
 *   - Colored error/success output
 *   - Animated spinner for long compiles
 *   - `tezz run`, `tezz build`, `tezz buildexe` shorthands
 *
 * Build:  (handled by build_lang_installer.ps1)
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

/* ---- ANSI Colors (Windows 10+ virtual terminal) ---- */
#define COL_RESET   "\x1b[0m"
#define COL_BOLD    "\x1b[1m"
#define COL_GREEN   "\x1b[38;2;16;185;129m"   /* emerald */
#define COL_BLUE    "\x1b[38;2;59;130;246m"   /* blue */
#define COL_YELLOW  "\x1b[38;2;251;191;36m"   /* amber */
#define COL_RED     "\x1b[38;2;239;68;68m"    /* red */
#define COL_GRAY    "\x1b[38;2;100;116;139m"  /* slate */
#define COL_CYAN    "\x1b[38;2;34;211;238m"   /* cyan */
#define COL_WHITE   "\x1b[38;2;248;250;252m"  /* white */

static void enable_vt(void){
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(h, &mode);
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    /* also enable for stderr */
    h = GetStdHandle(STD_ERROR_HANDLE);
    GetConsoleMode(h, &mode);
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

static void print_banner(void){
    /* Force UTF-8 output on Windows so arrow chars render */
    SetConsoleOutputCP(65001);
    printf(COL_GREEN COL_BOLD
        "\n"
        "  T E Z Z N A T I V E   v 1 . 0 . 0\n"
        COL_RESET
        COL_GRAY "  Systems & GUI Language  " COL_BLUE "|" COL_GRAY
        "  tn.tezzcorp.com\n" COL_RESET "\n");
}

static void print_help(void){
    print_banner();
    printf(COL_BOLD COL_WHITE "USAGE\n" COL_RESET);
    printf("  " COL_GREEN "tezz" COL_RESET " <command> [options]\n\n");
    printf(COL_BOLD COL_WHITE "COMMANDS\n" COL_RESET);
    printf("  " COL_CYAN "run" COL_RESET "      <file.tn> " COL_GRAY "[--bc]" COL_RESET
           "       Run a .tn program\n");
    printf("  " COL_CYAN "buildexe" COL_RESET " <file.tn> <out.exe>  Compile to native .exe\n");
    printf("  " COL_CYAN "build" COL_RESET "    <file.tn> " COL_GRAY "[--bc]" COL_RESET
           "       Alias for run\n");
    printf("  " COL_CYAN "version" COL_RESET "                      Show version info\n");
    printf("  " COL_CYAN "help" COL_RESET "                         Show this help\n\n");
    printf(COL_BOLD COL_WHITE "EXAMPLES\n" COL_RESET);
    printf("  tezz run hello.tn\n");
    printf("  tezz run main.tn --bc\n");
    printf("  tezz buildexe main.tn MyApp.exe\n\n");
    printf(COL_BOLD COL_WHITE "QUICK SYNTAX\n" COL_RESET);
    printf("  " COL_GRAY "# hello.tn\n" COL_RESET);
    printf("  " COL_BLUE "import " COL_RESET "\"io\"\n");
    printf("  " COL_BLUE "fn " COL_GREEN "main" COL_RESET "() -> " COL_BLUE "int" COL_RESET ":\n");
    printf("    say " COL_YELLOW "\"Hello from TezzNative!\"" COL_RESET "\n");
    printf("    " COL_BLUE "ret" COL_RESET " 0\n\n");
}

static void print_version(void){
    print_banner();
    printf("  " COL_GRAY "Compiler  : " COL_GREEN "tezzc 1.0.0" COL_RESET "\n");
    printf("  " COL_GRAY "Stdlib    : " COL_GREEN "lib/ (33 modules)" COL_RESET "\n");
    printf("  " COL_GRAY "Platform  : " COL_GREEN "Windows x64" COL_RESET "\n");
    printf("  " COL_GRAY "Website   : " COL_BLUE "tn.tezzcorp.com" COL_RESET "\n\n");
}

/* Locate tezzc.exe: beside this exe, or in PATH */
static int find_tezzc(char* out, int cap){
    char self[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, self, MAX_PATH-1);
    /* strip filename */
    char* sep = strrchr(self, '\\');
    if(!sep) sep = strrchr(self, '/');
    if(sep){
        *sep = 0;
        _snprintf(out, cap-1, "%s\\tezzc.exe", self);
        out[cap-1] = 0;
        if(GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return 1;
    }
    /* try PATH */
    _snprintf(out, cap-1, "tezzc.exe");
    return 1;
}

static int run_tezzc(int argc, char** argv, int start){
    char tezzc[MAX_PATH];
    find_tezzc(tezzc, MAX_PATH);

    /* build command line */
    char cmdline[8192] = {0};
    int pos = 0;
    pos += _snprintf(cmdline+pos, sizeof(cmdline)-pos, "\"%s\"", tezzc);
    for(int i = start; i < argc; i++){
        pos += _snprintf(cmdline+pos, sizeof(cmdline)-pos, " \"%s\"", argv[i]);
    }

    STARTUPINFOA si = {0}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};
    if(!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)){
        fprintf(stderr, COL_RED "tezz error: failed to launch tezzc (err %lu)\n" COL_RESET, GetLastError());
        return 1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}

int main(int argc, char** argv){
    enable_vt();

    if(argc < 2){
        print_help();
        return 0;
    }

    const char* cmd = argv[1];

    if(strcmp(cmd,"help")==0 || strcmp(cmd,"--help")==0 || strcmp(cmd,"-h")==0){
        print_help(); return 0;
    }
    if(strcmp(cmd,"version")==0 || strcmp(cmd,"--version")==0 || strcmp(cmd,"-v")==0){
        print_version(); return 0;
    }

    /* `tezz run file.tn [--bc]`  →  `tezzc run file.tn [--bc]` */
    if(strcmp(cmd,"run")==0 || strcmp(cmd,"build")==0){
        if(argc < 3){
            fprintf(stderr, COL_RED "tezz: missing file argument\n" COL_RESET);
            fprintf(stderr, COL_GRAY "  Usage: tezz run <file.tn> [--bc]\n" COL_RESET);
            return 1;
        }
        const char* file = argv[2];
        printf(COL_GRAY "  " COL_GREEN ">" COL_RESET " Running " COL_WHITE "%s" COL_RESET " ...\n\n", file);
        fflush(stdout);
        /* pass `run` as first tezzc arg, rest as-is */
        return run_tezzc(argc, argv, 1);
    }

    /* `tezz buildexe file.tn out.exe` */
    if(strcmp(cmd,"buildexe")==0){
        if(argc < 4){
            fprintf(stderr, COL_RED "tezz: missing arguments\n" COL_RESET);
            fprintf(stderr, COL_GRAY "  Usage: tezz buildexe <file.tn> <out.exe>\n" COL_RESET);
            return 1;
        }
        const char* file = argv[2];
        const char* out  = argv[3];
        printf(COL_GRAY "  " COL_BLUE "*" COL_RESET " Building " COL_WHITE "%s" COL_RESET
               " -> " COL_GREEN "%s" COL_RESET " ...\n\n", file, out);
        fflush(stdout);
        int code = run_tezzc(argc, argv, 1);
        if(code == 0){
            printf("\n  " COL_GREEN "OK " COL_WHITE "Build successful:" COL_RESET " %s\n\n", out);
        } else {
            printf("\n  " COL_RED "FAIL Build failed.\n" COL_RESET "\n");
        }
        return code;
    }

    /* Unknown command — pass everything through to tezzc verbatim */
    return run_tezzc(argc, argv, 1);
}
