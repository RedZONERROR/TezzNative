/*
 * TezzNative Native Installer  —  tools/installer_win.c
 * Compile: cl /O2 /nologo /subsystem:windows installer_win.c user32.lib shell32.lib /Fe:TezzCalcSetup.exe
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <string.h>
#include <stdio.h>

#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")

/* ---- customise here ---- */
#define APP_NAME    "TezzCalc Pro"
#define APP_VER     "1.0.0"
#define APP_EXE     "TezzCalc.exe"
#define VENDOR      "TezzCorp"
#define REG_UNINST  "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\TezzCalcPro"
/* files to copy: must be beside setup.exe */
static const char* FILES[] = { "TezzCalc.exe", "calculator.tn", "tezzc.exe", NULL };
/* ---- end config ---- */

static char g_dest[MAX_PATH];
static HWND g_hwnd, g_edit_dest, g_progress, g_status, g_btn_install, g_btn_cancel;
static int  g_page = 0;   /* 0=welcome 1=dir 2=progress 3=done */
static BOOL g_installing = FALSE;

/* helpers */
static void set_status(const char* msg){ SetWindowTextA(g_status, msg); }

static BOOL copy_files(const char* src_dir){
    for(int i=0;FILES[i];i++){
        char src[MAX_PATH], dst[MAX_PATH];
        snprintf(src,MAX_PATH,"%s\\%s",src_dir,FILES[i]);
        snprintf(dst,MAX_PATH,"%s\\%s",g_dest,FILES[i]);
        char buf[128]; snprintf(buf,128,"Copying %s...",FILES[i]);
        set_status(buf);
        SendMessageA(g_progress,PBM_STEPIT,0,0);
        if(!CopyFileA(src,dst,FALSE)){
            snprintf(buf,128,"Failed to copy %s (err %lu)",FILES[i],GetLastError());
            MessageBoxA(g_hwnd,buf,"Install Error",MB_ICONERROR); return FALSE;
        }
    }
    return TRUE;
}

static void create_shortcuts(){
    IShellLinkA* lnk; CoCreateInstance(&CLSID_ShellLink,NULL,CLSCTX_ALL,&IID_IShellLinkA,(void**)&lnk);
    char exe[MAX_PATH]; snprintf(exe,MAX_PATH,"%s\\%s",g_dest,APP_EXE);
    lnk->lpVtbl->SetPath(lnk,exe);
    lnk->lpVtbl->SetWorkingDirectory(lnk,g_dest);
    lnk->lpVtbl->SetDescription(lnk,APP_NAME " " APP_VER);
    IPersistFile* pf; lnk->lpVtbl->QueryInterface(lnk,&IID_IPersistFile,(void**)&pf);
    wchar_t link_path[MAX_PATH];
    char start_menu[MAX_PATH];
    SHGetFolderPathA(NULL,CSIDL_PROGRAMS,NULL,0,start_menu);
    snprintf(start_menu+strlen(start_menu),MAX_PATH-strlen(start_menu),"\\%s.lnk",APP_NAME);
    MultiByteToWideChar(CP_ACP,0,start_menu,-1,link_path,MAX_PATH);
    pf->lpVtbl->Save(pf,link_path,TRUE);
    char desktop[MAX_PATH];
    SHGetFolderPathA(NULL,CSIDL_DESKTOP,NULL,0,desktop);
    snprintf(desktop+strlen(desktop),MAX_PATH-strlen(desktop),"\\%s.lnk",APP_NAME);
    MultiByteToWideChar(CP_ACP,0,desktop,-1,link_path,MAX_PATH);
    pf->lpVtbl->Save(pf,link_path,TRUE);
    pf->lpVtbl->Release(pf); lnk->lpVtbl->Release(lnk);
}

static void write_registry(){
    HKEY k;
    RegCreateKeyExA(HKEY_LOCAL_MACHINE,REG_UNINST,0,NULL,0,KEY_WRITE,NULL,&k,NULL);
    RegSetValueExA(k,"DisplayName",0,REG_SZ,(BYTE*)APP_NAME,(DWORD)strlen(APP_NAME)+1);
    RegSetValueExA(k,"DisplayVersion",0,REG_SZ,(BYTE*)APP_VER,(DWORD)strlen(APP_VER)+1);
    RegSetValueExA(k,"Publisher",0,REG_SZ,(BYTE*)VENDOR,(DWORD)strlen(VENDOR)+1);
    RegSetValueExA(k,"InstallLocation",0,REG_SZ,(BYTE*)g_dest,(DWORD)strlen(g_dest)+1);
    char uninst[MAX_PATH];
    snprintf(uninst,MAX_PATH,"\"%s\\Uninstall.exe\"",g_dest);
    RegSetValueExA(k,"UninstallString",0,REG_SZ,(BYTE*)uninst,(DWORD)strlen(uninst)+1);
    RegCloseKey(k);
}

/* Build uninstaller beside the installed files */
static void write_uninstaller(){
    char self[MAX_PATH]; GetModuleFileNameA(NULL,self,MAX_PATH);
    char dst[MAX_PATH]; snprintf(dst,MAX_PATH,"%s\\Uninstall.exe",g_dest);
    CopyFileA(self,dst,FALSE);
    /* mark it as uninstall mode via registry value */
    HKEY k; RegOpenKeyExA(HKEY_LOCAL_MACHINE,REG_UNINST,0,KEY_WRITE,&k);
    DWORD mode=1; RegSetValueExA(k,"_IsUninstaller",0,REG_DWORD,(BYTE*)&mode,4);
    RegCloseKey(k);
}

/* ---- install thread ---- */
static DWORD WINAPI do_install(void* _){
    char src_dir[MAX_PATH]; GetModuleFileNameA(NULL,src_dir,MAX_PATH);
    char* sep=strrchr(src_dir,'\\'); if(sep)*sep=0;
    CreateDirectoryA(g_dest,NULL);
    SendMessageA(g_progress,PBM_SETRANGE,0,MAKELPARAM(0,6));
    SendMessageA(g_progress,PBM_SETSTEP,1,0);
    CoInitialize(NULL);
    if(!copy_files(src_dir)){ CoUninitialize(); return 1; }
    set_status("Creating shortcuts..."); create_shortcuts();
    set_status("Writing registry..."); write_registry();
    set_status("Writing uninstaller..."); write_uninstaller();
    SendMessageA(g_progress,PBM_SETPOS,6,0);
    set_status("Installation complete!");
    g_page=3; PostMessageA(g_hwnd,WM_USER+1,0,0);
    CoUninitialize(); return 0;
}

/* ---- uninstall mode ---- */
static void do_uninstall(){
    if(MessageBoxA(NULL,"Remove " APP_NAME " from your computer?",
            "Uninstall " APP_NAME,MB_YESNO|MB_ICONQUESTION)!=IDYES) return;
    char inst[MAX_PATH]={0};
    HKEY k; if(RegOpenKeyExA(HKEY_LOCAL_MACHINE,REG_UNINST,0,KEY_READ,&k)==ERROR_SUCCESS){
        DWORD sz=MAX_PATH;
        RegQueryValueExA(k,"InstallLocation",NULL,NULL,(BYTE*)inst,&sz);
        RegCloseKey(k);
    }
    if(inst[0]){
        for(int i=0;FILES[i];i++){
            char f[MAX_PATH]; snprintf(f,MAX_PATH,"%s\\%s",inst,FILES[i]); DeleteFileA(f);
        }
        char f2[MAX_PATH];
        snprintf(f2,MAX_PATH,"%s\\Uninstall.exe",inst); /* self — delete on reboot */
        MoveFileExA(f2,NULL,MOVEFILE_DELAY_UNTIL_REBOOT);
        RemoveDirectoryA(inst);
        char lnk[MAX_PATH]; SHGetFolderPathA(NULL,CSIDL_PROGRAMS,NULL,0,lnk);
        snprintf(lnk+strlen(lnk),MAX_PATH-strlen(lnk),"\\%s.lnk",APP_NAME);
        DeleteFileA(lnk);
        SHGetFolderPathA(NULL,CSIDL_DESKTOP,NULL,0,lnk);
        snprintf(lnk+strlen(lnk),MAX_PATH-strlen(lnk),"\\%s.lnk",APP_NAME);
        DeleteFileA(lnk);
    }
    RegDeleteKeyA(HKEY_LOCAL_MACHINE,REG_UNINST);
    MessageBoxA(NULL,APP_NAME " was removed.","Uninstall",MB_OK|MB_ICONINFORMATION);
}

/* ---- UI drawing helpers ---- */
#define BG 0x050810
#define ACCENT 0x2D5BE3
static void draw_bg(HDC dc, RECT* r){ HBRUSH b=CreateSolidBrush(BG); FillRect(dc,r,b); DeleteObject(b); }

static LRESULT CALLBACK WndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp){
    switch(msg){
    case WM_CREATE:{
        g_edit_dest=CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,20,160,440,26,hw,(HMENU)1,NULL,NULL);
        char def[MAX_PATH]; SHGetFolderPathA(NULL,CSIDL_PROGRAM_FILES,NULL,0,def);
        snprintf(def+strlen(def),MAX_PATH-strlen(def),"\\%s\\%s",VENDOR,APP_NAME);
        SetWindowTextA(g_edit_dest,def); strcpy(g_dest,def);
        ShowWindow(g_edit_dest,SW_HIDE);
        g_progress=CreateWindowExA(0,PROGRESS_CLASSA,"",WS_CHILD|PBS_SMOOTH,20,200,440,20,hw,(HMENU)2,NULL,NULL);
        ShowWindow(g_progress,SW_HIDE);
        g_status=CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE|SS_CENTER,20,228,440,20,hw,(HMENU)3,NULL,NULL);
        ShowWindow(g_status,SW_HIDE);
        g_btn_install=CreateWindowExA(0,"BUTTON","Next",WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,370,330,80,28,hw,(HMENU)10,NULL,NULL);
        g_btn_cancel=CreateWindowExA(0,"BUTTON","Cancel",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,280,330,80,28,hw,(HMENU)11,NULL,NULL);
        break;
    }
    case WM_ERASEBKGND:{ RECT r; GetClientRect(hw,&r); draw_bg((HDC)wp,&r); return 1; }
    case WM_PAINT:{
        PAINTSTRUCT ps; HDC dc=BeginPaint(hw,&ps); RECT r; GetClientRect(hw,&r);
        draw_bg(dc,&r);
        /* accent bar */
        HBRUSH ab=CreateSolidBrush(ACCENT); RECT bar={0,0,r.right,4}; FillRect(dc,&bar,ab); DeleteObject(ab);
        SetBkMode(dc,TRANSPARENT);
        /* title */
        SetTextColor(dc,0xFFFFFF);
        HFONT big=CreateFontA(28,0,0,0,FW_BOLD,0,0,0,0,0,0,0,0,"Segoe UI");
        HFONT old=(HFONT)SelectObject(dc,big);
        RECT tr={20,16,460,60}; DrawTextA(dc,APP_NAME,-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        DeleteObject(SelectObject(dc,old));
        SetTextColor(dc,0x8899AA);
        HFONT sm=CreateFontA(14,0,0,0,FW_NORMAL,0,0,0,0,0,0,0,0,"Segoe UI");
        old=(HFONT)SelectObject(dc,sm);
        RECT sr={20,48,460,68}; DrawTextA(dc,APP_VER,-1,&sr,DT_LEFT|DT_TOP);
        /* separator */
        HPEN pen=CreatePen(PS_SOLID,1,0x1E3A6E); SelectObject(dc,pen);
        MoveToEx(dc,0,68,NULL); LineTo(dc,r.right,68);
        MoveToEx(dc,0,r.bottom-52,NULL); LineTo(dc,r.right,r.bottom-52);
        DeleteObject(pen);
        SetTextColor(dc,0xFFFFFF);
        if(g_page==0){
            RECT mr={20,80,460,150};
            DrawTextA(dc,"Welcome to the " APP_NAME " Setup Wizard.\n\nThis will install " APP_NAME " " APP_VER " on your computer.\nClick Install to continue.",-1,&mr,DT_LEFT|DT_WORDBREAK);
        } else if(g_page==1){
            RECT mr={20,80,460,150};
            DrawTextA(dc,"Choose installation folder:",-1,&mr,DT_LEFT|DT_TOP);
        } else if(g_page==2){
            RECT mr={20,80,460,150};
            DrawTextA(dc,"Installing, please wait...",-1,&mr,DT_LEFT|DT_TOP);
        } else if(g_page==3){
            SetTextColor(dc,0x10B981);
            HFONT donef=CreateFontA(22,0,0,0,FW_BOLD,0,0,0,0,0,0,0,0,"Segoe UI");
            old=(HFONT)SelectObject(dc,donef);
            RECT dr={20,80,460,120}; DrawTextA(dc,"Installation Complete!",-1,&dr,DT_LEFT|DT_TOP);
            DeleteObject(SelectObject(dc,old));
            SetTextColor(dc,0x8899AA);
            RECT dr2={20,116,460,220};
            DrawTextA(dc,APP_NAME " has been installed.\nA shortcut was added to your Desktop and Start Menu.",-1,&dr2,DT_LEFT|DT_WORDBREAK);
        }
        DeleteObject(SelectObject(dc,sm));
        EndPaint(hw,&ps); return 0;
    }
    case WM_CTLCOLORSTATIC:{ SetBkMode((HDC)wp,TRANSPARENT); SetTextColor((HDC)wp,0x8899AA); return (LRESULT)CreateSolidBrush(BG); }
    case WM_COMMAND:
        if(LOWORD(wp)==11){ /* Cancel */ PostQuitMessage(0); }
        if(LOWORD(wp)==10){ /* Next/Install/Finish */
            if(g_page==0){ g_page=1; ShowWindow(g_edit_dest,SW_SHOW); SetWindowTextA(g_btn_install,"Install"); InvalidateRect(hw,NULL,TRUE); }
            else if(g_page==1){
                GetWindowTextA(g_edit_dest,g_dest,MAX_PATH);
                if(!g_dest[0]){ MessageBoxA(hw,"Please enter an install folder.","Error",MB_ICONWARNING); break; }
                g_page=2; ShowWindow(g_edit_dest,SW_HIDE); ShowWindow(g_progress,SW_SHOW); ShowWindow(g_status,SW_SHOW);
                EnableWindow(g_btn_install,FALSE); SetWindowTextA(g_btn_cancel,"Cancel");
                InvalidateRect(hw,NULL,TRUE);
                CreateThread(NULL,0,do_install,NULL,0,NULL);
            }
            else if(g_page==3){ PostQuitMessage(0); /* optionally launch app */ }
        }
        break;
    case WM_USER+1: /* install done */ EnableWindow(g_btn_install,TRUE); SetWindowTextA(g_btn_install,"Finish"); EnableWindow(g_btn_cancel,FALSE); InvalidateRect(hw,NULL,TRUE); break;
    case WM_DESTROY: PostQuitMessage(0); break;
    }
    return DefWindowProcA(hw,msg,wp,lp);
}

int WINAPI WinMain(HINSTANCE hi,HINSTANCE hp,LPSTR cmd,int show){
    (void)hp;(void)cmd;(void)show;

    /* Uninstall mode? */
    HKEY k; DWORD mode=0,sz=4;
    if(RegOpenKeyExA(HKEY_LOCAL_MACHINE,REG_UNINST,0,KEY_READ,&k)==ERROR_SUCCESS){
        RegQueryValueExA(k,"_IsUninstaller",NULL,NULL,(BYTE*)&mode,&sz); RegCloseKey(k);
    }
    if(mode){ do_uninstall(); return 0; }

    InitCommonControls();
    CoInitialize(NULL);

    WNDCLASSEXA wc={sizeof(wc)};
    wc.lpszClassName="TzSetup"; wc.hInstance=hi; wc.lpfnWndProc=WndProc;
    wc.hCursor=LoadCursorA(NULL,IDC_ARROW); wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    wc.hIcon=LoadIconA(NULL,IDI_APPLICATION);
    RegisterClassExA(&wc);

    int W=500,H=380;
    int x=(GetSystemMetrics(SM_CXSCREEN)-W)/2, y=(GetSystemMetrics(SM_CYSCREEN)-H)/2;
    g_hwnd=CreateWindowExA(0,"TzSetup",APP_NAME " " APP_VER " Setup",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,x,y,W,H,NULL,NULL,hi,NULL);
    ShowWindow(g_hwnd,SW_SHOW); UpdateWindow(g_hwnd);

    MSG msg;
    while(GetMessageA(&msg,NULL,0,0)){ TranslateMessage(&msg); DispatchMessageA(&msg); }
    CoUninitialize(); return 0;
}
