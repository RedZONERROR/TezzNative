/*
 * TezzCalc Launcher  —  tools/launcher.c
 *
 * Compiled by package.ps1 using MSVC (cl.exe).
 * Finds tezzc.exe next to itself and runs the bundled .tn app through it.
 * This is how ALL TezzNative GUI apps are distributed until the PE
 * codegen gains a tnrt.dll import-table backend.
 *
 * Build (done automatically by package.ps1):
 *   cl /O2 /W3 /nologo /subsystem:windows /entry:WinMainCRTStartup
 *      tools\launcher.c user32.lib /Fe:TezzCalc.exe
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* No CRT — use wsprintfA from user32 for formatting */
#define my_sprintf wsprintfA

/* Minimal intrinsics (no CRT) */
static __inline void* my_memcpy(void* d,const void* s,SIZE_T n){ char* dd=(char*)d; const char* ss=(const char*)s; while(n--)*(dd++)=*(ss++); return d; }
static __inline void* my_memset(void* d,int c,SIZE_T n){ char* dd=(char*)d; while(n--)*(dd++)=(char)c; return d; }
static __inline SIZE_T my_strlen(const char* s){ SIZE_T n=0; while(*s++)n++; return n; }
#define memcpy  my_memcpy
#define memset  my_memset
#define strlen  my_strlen

/* App config — edit to match your app */
#define TN_APP_FILE "calculator.tn"
#define APP_TITLE   "TezzCalc Pro"

static void error(const char* msg) {
    MessageBoxA(NULL, msg, APP_TITLE " - Error", MB_ICONERROR | MB_OK);
}

/* Strip the filename from a full path, leaving the directory (with trailing \) */
static void dir_of(const char* full, char* dir, int cap) {
    int n = (int)strlen(full);
    int i = n - 1;
    while (i > 0 && full[i] != '\\' && full[i] != '/') i--;
    if (i >= cap - 1) i = cap - 2;
    memcpy(dir, full, (size_t)i);
    dir[i] = 0;
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE hp, LPSTR cmd, int show) {
    (void)h; (void)hp; (void)cmd; (void)show;

    char self[MAX_PATH]  = {0};
    char dir[MAX_PATH]   = {0};
    char tezzc[MAX_PATH] = {0};
    char tnapp[MAX_PATH] = {0};
    char cmdline[MAX_PATH * 2 + 64] = {0};

    /* Where is this exe? */
    GetModuleFileNameA(NULL, self, MAX_PATH);
    dir_of(self, dir, MAX_PATH);

    /* tezzc.exe must live beside us */
    my_sprintf(tezzc, "%s\\tezzc.exe", dir);
    if (GetFileAttributesA(tezzc) == INVALID_FILE_ATTRIBUTES) {
        /* Try PATH */
        my_sprintf(tezzc, "%s", "tezzc.exe");
    }

    /* The .tn source lives beside us too */
    my_sprintf(tnapp, "%s\\" TN_APP_FILE, dir);
    if (GetFileAttributesA(tnapp) == INVALID_FILE_ATTRIBUTES) {
        error("Cannot find " TN_APP_FILE " next to " APP_TITLE ".\n"
              "Please reinstall the application.");
        return 1;
    }

    /* Build command line: tezzc.exe run "app.tn" --bc */
    my_sprintf(cmdline, "\"%s\" run \"%s\" --bc", tezzc, tnapp);

    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, dir, &si, &pi)) {
        char msg[512];
        my_sprintf(msg, "Failed to start tezzc.exe (error %lu).\nEnsure tezzc.exe is beside " APP_TITLE ".", GetLastError());
        error(msg);
        return 1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
}
