#include "host_gui_backend.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

// A minimalistic Win32 GUI backend implementation without SDL.
// It uses a memory-backed DIB section for the framebuffer.

#define TN_HOST_KEY_QUEUE_CAP 256

static struct {
  int tried_init;
  int ready;
  int width;
  int height;
  int pitch;
  int bpp;
  uint32_t* pixels;
#ifdef _WIN32
  HWND hwnd;
  HDC mem_dc;
  HBITMAP dib;
#endif
  int key_q[TN_HOST_KEY_QUEUE_CAP];
  int key_head;
  int key_tail;
  int key_count;
  int key_last;
  
  int mouse_x;
  int mouse_y;
  int mouse_buttons_now;
  int mouse_pending_dx;
  int mouse_pending_dy;
  int mouse_packet_pending;
  int mouse_last_dx;
  int mouse_last_dy;
  int mouse_last_buttons;
  /* resize tracking */
  volatile int resize_pending;
  volatile int resize_w;
  volatile int resize_h;
  /* vsync / timing */
  int refresh_rate;          /* Hz, queried once from display */
#ifdef _WIN32
  LARGE_INTEGER qpc_freq;
  LARGE_INTEGER qpc_last;
#endif
  int vsync_initialized;
} g_gui = {0};

int tn_host_gui_backend_compiled(void){ return 1; }
int tn_host_gui_backend_ready(void){ return g_gui.ready; }
unsigned char* tn_host_fb_addr(void){ return (unsigned char*)g_gui.pixels; }
long long tn_host_fb_width(void){ return g_gui.width; }
long long tn_host_fb_height(void){ return g_gui.height; }
long long tn_host_fb_pitch(void){ return g_gui.pitch; }
long long tn_host_fb_bpp(void){ return g_gui.bpp; }

/* Screen dimensions (primary monitor) */
long long tn_host_screen_width(void){
#ifdef _WIN32
  return (long long)GetSystemMetrics(SM_CXSCREEN);
#else
  return 1920;
#endif
}
long long tn_host_screen_height(void){
#ifdef _WIN32
  return (long long)GetSystemMetrics(SM_CYSCREEN);
#else
  return 1080;
#endif
}

/* Absolute mouse position (updated every WM_MOUSEMOVE) */
long long tn_host_mouse_pos_x(void){ return g_gui.mouse_x; }
long long tn_host_mouse_pos_y(void){ return g_gui.mouse_y; }

static void tn_host_key_push(int key){
  if(key <= 0) return;
  if(g_gui.key_count >= TN_HOST_KEY_QUEUE_CAP){
    g_gui.key_head = (g_gui.key_head + 1) % TN_HOST_KEY_QUEUE_CAP;
    g_gui.key_count--;
  }
  g_gui.key_q[g_gui.key_tail] = key;
  g_gui.key_tail = (g_gui.key_tail + 1) % TN_HOST_KEY_QUEUE_CAP;
  g_gui.key_count++;
}

static int tn_host_key_pop(void){
  if(g_gui.key_count <= 0) return 0;
  int key = g_gui.key_q[g_gui.key_head];
  g_gui.key_head = (g_gui.key_head + 1) % TN_HOST_KEY_QUEUE_CAP;
  g_gui.key_count--;
  g_gui.key_last = key;
  return key;
}

#ifdef _WIN32
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp){
  switch(msg){
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      if(g_gui.mem_dc && g_gui.dib){
        BitBlt(hdc, 0, 0, g_gui.width, g_gui.height, g_gui.mem_dc, 0, 0, SRCCOPY);
      }
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_SIZE: {
      int nw = (int)LOWORD(lp);
      int nh = (int)HIWORD(lp);
      if(nw > 0 && nh > 0){
        g_gui.resize_pending = 1;
        g_gui.resize_w = nw;
        g_gui.resize_h = nh;
      }
      return 0;
    }
    case WM_CLOSE:
    case WM_DESTROY:
      PostQuitMessage(0);
      tn_host_key_push(27);
      return 0;
    case WM_KEYDOWN: {
      int key = 0;
      if(wp == VK_BACK) key = 8;
      else if(wp == VK_RETURN) key = 13;
      else if(wp == VK_ESCAPE) key = 27;
      else if(wp == VK_TAB) key = 9;
      else if(wp == VK_DELETE) key = 127;
      if(key) tn_host_key_push(key);
      return 0;
    }
    case WM_CHAR: {
      int key = (int)wp;
      if(key >= 32 && key <= 126) tn_host_key_push(key);
      return 0;
    }
    case WM_MOUSEMOVE: {
      int x = (int)(short)LOWORD(lp);
      int y = (int)(short)HIWORD(lp);
      g_gui.mouse_pending_dx += (x - g_gui.mouse_x);
      g_gui.mouse_pending_dy += (y - g_gui.mouse_y);
      g_gui.mouse_x = x;
      g_gui.mouse_y = y;
      g_gui.mouse_packet_pending = 1;
      return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP: {
      int bit = 0;
      int down = 0;
      if(msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) bit = 1;
      if(msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP) bit = 2;
      if(msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP) bit = 4;
      if(msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN) down = 1;
      
      if(down) g_gui.mouse_buttons_now |= bit;
      else g_gui.mouse_buttons_now &= ~bit;
      g_gui.mouse_packet_pending = 1;
      
      if(down) SetCapture(hwnd);
      else ReleaseCapture();
      return 0;
    }
    case WM_SETCURSOR:
      if(LOWORD(lp) == HTCLIENT){
        SetCursor(LoadCursorA(NULL, IDC_ARROW));
        return TRUE;
      }
      break;
  }
  return DefWindowProcA(hwnd, msg, wp, lp);
}
#endif

/* tn_host_fb_init_ex — create window with custom title and size.
 * Call BEFORE fb_present(). If already initialized, this is a no-op.
 * title: window title string, W/H: client area size in pixels.
 */
long long tn_host_fb_init_ex(const char* title, long long W, long long H){
#ifdef _WIN32
  if(g_gui.tried_init) return 0; /* already done */
  g_gui.tried_init = 1;
  g_gui.width  = (W > 0) ? (int)W : 1024;
  g_gui.height = (H > 0) ? (int)H : 768;
  g_gui.bpp    = 32;
  g_gui.pitch  = g_gui.width * 4;

  WNDCLASSA wc = {0};
  wc.lpfnWndProc   = WndProc;
  wc.hInstance     = GetModuleHandleA(NULL);
  wc.lpszClassName = "TezzGuiWnd";
  wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  wc.hCursor       = LoadCursorA(NULL, IDC_ARROW); /* native OS cursor */
  RegisterClassA(&wc);

  DWORD style = WS_OVERLAPPEDWINDOW;
  RECT  rect  = {0, 0, g_gui.width, g_gui.height};
  AdjustWindowRect(&rect, style, FALSE);

  const char* t = (title && title[0]) ? title : "TezzNative";
  g_gui.hwnd = CreateWindowA("TezzGuiWnd", t, style,
    CW_USEDEFAULT, CW_USEDEFAULT,
    rect.right - rect.left, rect.bottom - rect.top,
    NULL, NULL, wc.hInstance, NULL);

  if(g_gui.hwnd){
    HDC hdc = GetDC(g_gui.hwnd);
    g_gui.mem_dc = CreateCompatibleDC(hdc);

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = g_gui.width;
    bmi.bmiHeader.biHeight      = -g_gui.height; /* negative = top-down */
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    g_gui.dib = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS,
                                  (void**)&g_gui.pixels, NULL, 0);
    SelectObject(g_gui.mem_dc, g_gui.dib);
    ReleaseDC(g_gui.hwnd, hdc);

    ShowWindow(g_gui.hwnd, SW_SHOW);
    UpdateWindow(g_gui.hwnd);
    g_gui.ready = 1;
  }
#else
  (void)title;
  (void)W;
  (void)H;
#endif
  return g_gui.ready ? 0 : -1;
}

long long tn_host_fb_present(void){
#ifdef _WIN32
  if(!g_gui.tried_init){
    /* Default window if fb_init_ex not called */
    tn_host_fb_init_ex("TezzNative", 1024, 768);
  }

  if(!g_gui.ready) return -1;

  /* Handle pending resize */
  if(g_gui.resize_pending){
    g_gui.resize_pending = 0;
    int nw = g_gui.resize_w;
    int nh = g_gui.resize_h;
    if(nw != g_gui.width || nh != g_gui.height){
      g_gui.width = nw;
      g_gui.height = nh;
      g_gui.pitch = nw * 4;
      
      HDC hdc = GetDC(g_gui.hwnd);
      if(g_gui.dib){
        DeleteObject(g_gui.dib);
      }
      
      BITMAPINFO bmi = {0};
      bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
      bmi.bmiHeader.biWidth       = g_gui.width;
      bmi.bmiHeader.biHeight      = -g_gui.height;
      bmi.bmiHeader.biPlanes      = 1;
      bmi.bmiHeader.biBitCount    = 32;
      bmi.bmiHeader.biCompression = BI_RGB;
      
      g_gui.dib = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS,
                                    (void**)&g_gui.pixels, NULL, 0);
      SelectObject(g_gui.mem_dc, g_gui.dib);
      ReleaseDC(g_gui.hwnd, hdc);
    }
  }

  /* Blit framebuffer directly to window DC — no WM_PAINT round-trip */
  HDC hdc = GetDC(g_gui.hwnd);
  if(hdc && g_gui.mem_dc){
    BitBlt(hdc, 0, 0, g_gui.width, g_gui.height, g_gui.mem_dc, 0, 0, SRCCOPY);
    ReleaseDC(g_gui.hwnd, hdc);
  }

  /* Pump pending messages without blocking */
  MSG msg;
  while(PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)){
    if(msg.message == WM_QUIT){
      tn_host_key_push(27); /* ESC = quit signal */
    }
    TranslateMessage(&msg);
    DispatchMessageA(&msg);
  }

  /* VSync logic targeting monitor refresh rate */
  if(!g_gui.vsync_initialized){
    g_gui.vsync_initialized = 1;
    QueryPerformanceFrequency(&g_gui.qpc_freq);
    QueryPerformanceCounter(&g_gui.qpc_last);
    
    DEVMODEA dm = {0};
    dm.dmSize = sizeof(dm);
    if(EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm)){
      g_gui.refresh_rate = dm.dmDisplayFrequency;
    }
    if(g_gui.refresh_rate <= 0) g_gui.refresh_rate = 60;
  }

  LARGE_INTEGER target_ticks, current;
  target_ticks.QuadPart = g_gui.qpc_freq.QuadPart / g_gui.refresh_rate;
  
  while(1){
    QueryPerformanceCounter(&current);
    if(current.QuadPart - g_gui.qpc_last.QuadPart >= target_ticks.QuadPart){
      break;
    }
    /* Sleep a tiny bit if we have more than 2ms to wait */
    long long wait_ms = ((target_ticks.QuadPart - (current.QuadPart - g_gui.qpc_last.QuadPart)) * 1000) / g_gui.qpc_freq.QuadPart;
    if(wait_ms > 2){
      Sleep((DWORD)(wait_ms - 1));
    } else {
      YieldProcessor();
    }
  }
  g_gui.qpc_last = current;

#endif
  return 0;
}

long long tn_host_kbd_has_event(void){ return (g_gui.key_count > 0) ? 1 : 0; }
long long tn_host_kbd_read_scancode(void){ return tn_host_key_pop(); }
long long tn_host_kbd_read_scancode_raw(void){ return tn_host_key_pop(); }
long long tn_host_kbd_read_char(void){ return tn_host_key_pop(); }
long long tn_host_kbd_ime_active(void){ return 0; }
long long tn_host_kbd_ime_cursor(void){ return 0; }
long long tn_host_kbd_ime_length(void){ return 0; }
long long tn_host_kbd_set_debug(long long on){ (void)on; return 0; }
long long tn_host_kbd_last_scancode(void){ return g_gui.key_last; }

long long tn_host_mouse_has_packet(void){ return g_gui.mouse_packet_pending ? 1 : 0; }
long long tn_host_mouse_read_packet(void){
  if(!g_gui.mouse_packet_pending){
    g_gui.mouse_last_dx = 0; g_gui.mouse_last_dy = 0;
    g_gui.mouse_last_buttons = g_gui.mouse_buttons_now;
    return 0;
  }
  g_gui.mouse_last_dx = g_gui.mouse_pending_dx;
  g_gui.mouse_last_dy = g_gui.mouse_pending_dy;
  g_gui.mouse_last_buttons = g_gui.mouse_buttons_now;
  g_gui.mouse_pending_dx = 0; g_gui.mouse_pending_dy = 0;
  g_gui.mouse_packet_pending = 0;
  return 1;
}
long long tn_host_mouse_dx(void){ return g_gui.mouse_last_dx; }
long long tn_host_mouse_dy(void){ return g_gui.mouse_last_dy; }
long long tn_host_mouse_buttons(void){ return g_gui.mouse_buttons_now; }

static unsigned int tn_host_argb_from_rgb(unsigned int color){
  unsigned int r = (color >> 16) & 255u;
  unsigned int g = (color >> 8) & 255u;
  unsigned int b = color & 255u;
  return 0xFF000000u | (r << 16) | (g << 8) | b;
}

long long tn_host_fb_put_pixel(long long x, long long y, unsigned int color){
  if(!g_gui.ready) return -1;
  if(x >= 0 && x < g_gui.width && y >= 0 && y < g_gui.height){
    g_gui.pixels[(int)y * g_gui.width + (int)x] = tn_host_argb_from_rgb(color);
  }
  return 0;
}

long long tn_host_fb_fill_rect(long long x, long long y, long long w, long long h, unsigned int color){
  if(!g_gui.ready) return -1;
  unsigned int argb = tn_host_argb_from_rgb(color);
  for(int yy = 0; yy < h; yy++){
    int py = (int)y + yy;
    if(py < 0 || py >= g_gui.height) continue;
    for(int xx = 0; xx < w; xx++){
      int px = (int)x + xx;
      if(px >= 0 && px < g_gui.width){
        g_gui.pixels[py * g_gui.width + px] = argb;
      }
    }
  }
  return 0;
}

/* --- Compact 8x8 bitmap font (printable ASCII 32-126) --- */
/* Each char = 8 bytes, 1 bit per pixel, MSB = leftmost pixel */
static const unsigned char g_font8x8[95][8] = {
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 32 space */
  {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, /* 33 ! */
  {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, /* 34 " */
  {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, /* 35 # */
  {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, /* 36 $ */
  {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, /* 37 % */
  {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, /* 38 & */
  {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, /* 39 ' */
  {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, /* 40 ( */
  {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, /* 41 ) */
  {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, /* 42 * */
  {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00}, /* 43 + */
  {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}, /* 44 , */
  {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, /* 45 - */
  {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, /* 46 . */
  {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, /* 47 / */
  {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, /* 48 0 */
  {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}, /* 49 1 */
  {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, /* 50 2 */
  {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}, /* 51 3 */
  {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, /* 52 4 */
  {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}, /* 53 5 */
  {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, /* 54 6 */
  {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}, /* 55 7 */
  {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, /* 56 8 */
  {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}, /* 57 9 */
  {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, /* 58 : */
  {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06}, /* 59 ; */
  {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, /* 60 < */
  {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00}, /* 61 = */
  {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, /* 62 > */
  {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}, /* 63 ? */
  {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, /* 64 @ */
  {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, /* 65 A */
  {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, /* 66 B */
  {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, /* 67 C */
  {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, /* 68 D */
  {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, /* 69 E */
  {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, /* 70 F */
  {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, /* 71 G */
  {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, /* 72 H */
  {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* 73 I */
  {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, /* 74 J */
  {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, /* 75 K */
  {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}, /* 76 L */
  {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}, /* 77 M */
  {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, /* 78 N */
  {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, /* 79 O */
  {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}, /* 80 P */
  {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, /* 81 Q */
  {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, /* 82 R */
  {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, /* 83 S */
  {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* 84 T */
  {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}, /* 85 U */
  {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, /* 86 V */
  {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, /* 87 W */
  {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, /* 88 X */
  {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, /* 89 Y */
  {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, /* 90 Z */
  {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, /* 91 [ */
  {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, /* 92 \ */
  {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}, /* 93 ] */
  {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, /* 94 ^ */
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, /* 95 _ */
  {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, /* 96 ` */
  {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}, /* 97 a */
  {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, /* 98 b */
  {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, /* 99 c */
  {0x38,0x30,0x30,0x3e,0x33,0x33,0x6E,0x00}, /* 100 d */
  {0x00,0x00,0x1E,0x33,0x3f,0x03,0x1E,0x00}, /* 101 e */
  {0x1C,0x36,0x06,0x0f,0x06,0x06,0x0F,0x00}, /* 102 f */
  {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, /* 103 g */
  {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, /* 104 h */
  {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}, /* 105 i */
  {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, /* 106 j */
  {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}, /* 107 k */
  {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* 108 l */
  {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}, /* 109 m */
  {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, /* 110 n */
  {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, /* 111 o */
  {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, /* 112 p */
  {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78}, /* 113 q */
  {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, /* 114 r */
  {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00}, /* 115 s */
  {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, /* 116 t */
  {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}, /* 117 u */
  {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, /* 118 v */
  {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, /* 119 w */
  {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, /* 120 x */
  {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}, /* 121 y */
  {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, /* 122 z */
  {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}, /* 123 { */
  {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, /* 124 | */
  {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}, /* 125 } */
  {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}, /* 126 ~ */
};

long long tn_host_fb_text(long long x, long long y, const unsigned char* s, unsigned int color){
  if(!g_gui.ready || !s) return -1;
  unsigned int argb = tn_host_argb_from_rgb(color);
  int i = 0;
  while(s[i] != 0){
    unsigned int ch = (unsigned int)s[i];
    int ox = (int)x + i * 8;
    int oy = (int)y;
    if(ch >= 32 && ch <= 126){
      const unsigned char* glyph = g_font8x8[ch - 32];
      for(int row = 0; row < 8; row++){
        unsigned char bits = glyph[row];
        for(int col = 0; col < 8; col++){
          if((bits >> col) & 1){  /* LSB = leftmost pixel */
            int px = ox + col;
            int py = oy + row;
            if(px >= 0 && px < g_gui.width && py >= 0 && py < g_gui.height)
              g_gui.pixels[py * g_gui.width + px] = argb;
          }
        }
      }
    }
    i++;
  }
  return 0;
}

/* ---- fb_text_ex: scaled + bold text rendering --------------------------------
 * scale: 1=8px  2=16px  3=24px  (clamped 1..6)
 * bold:  0=normal  1=bold (each glyph pixel drawn 2 wide)
 * Returns 0 on success, -1 if not ready.
 */
long long tn_host_fb_text_ex(long long x, long long y, const unsigned char* s,
                              unsigned int color, long long scale, long long bold){
  if(!g_gui.ready || !s) return -1;
  if(scale < 1) scale = 1;
  if(scale > 6) scale = 6;
  unsigned int argb = tn_host_argb_from_rgb(color);
  int sc = (int)scale;
  int pw = bold ? sc + 1 : sc;   /* pixel block width (bold=+1) */
  int cw = bold ? 9*sc : 8*sc;   /* advance per character        */
  int i  = 0;
  int cx = (int)x;
  while(s[i] != 0){
    unsigned int ch = (unsigned int)s[i];
    if(ch >= 32 && ch <= 126){
      const unsigned char* g = g_font8x8[ch - 32];
      for(int row = 0; row < 8; row++){
        unsigned char bits = g[row];
        for(int col = 0; col < 8; col++){
          if((bits >> col) & 1){  /* LSB = leftmost pixel */
            int bx = cx + col*sc;
            int by = (int)y + row*sc;
            for(int dy = 0; dy < sc; dy++)
              for(int dx = 0; dx < pw; dx++){
                int fx = bx+dx, fy = by+dy;
                if(fx>=0 && fx<g_gui.width && fy>=0 && fy<g_gui.height)
                  g_gui.pixels[fy*g_gui.width+fx] = argb;
              }
          }
        }
      }
    }
    cx += cw;
    i++;
  }
  return 0;
}

/* ---- Software arrow cursor (11 wide x 19 tall) ----------------------------
 * Call once per frame AFTER presenting the scene to overlay cursor on screen.
 * color: fill color (outline is always black).
 */
long long tn_host_fb_soft_cursor(long long x, long long y, unsigned int color){
  if(!g_gui.ready) return -1;
  /* arrow shape: col index where outline/fill pixels sit per row */
  /* each entry: {outline_right_col, fill_end_col} for the triangle */
  static const int arrow_w[20] = {
    1,2,3,4,5,6,7,8,9,10,11, /* top triangle rows 0-10 */
    7,7,5,5,3,3,1,1,0        /* stem rows 11-19 (horizontal bar then stem) */
  };
  int cx=(int)x, cy=(int)y;
  uint32_t B=0xFF000000u;
  uint32_t W=tn_host_argb_from_rgb(color ? color : 0xFFFFFFu);
  for(int r=0; r<20; r++){
    int rw = arrow_w[r];
    if(rw <= 0) continue;
    /* left edge (always black) */
    int px0=cx, py=cy+r;
    if(py<g_gui.height && px0>=0 && px0<g_gui.width)
      g_gui.pixels[py*g_gui.width+px0]=B;
    /* right/diagonal edge */
    int pxr = cx + rw - 1;
    if(r<11){ /* triangle diagonal */
      if(py<g_gui.height && pxr>=0 && pxr<g_gui.width)
        g_gui.pixels[py*g_gui.width+pxr]=B;
    }
    /* bottom edge at row 10 */
    if(r==10){
      for(int c=0;c<rw;c++){
        int px=cx+c;
        if(px>=0&&px<g_gui.width&&py>=0&&py<g_gui.height)
          g_gui.pixels[py*g_gui.width+px]=B;
      }
    }
    /* fill interior */
    for(int c=1; c<rw-1; c++){
      int px=cx+c;
      if(px>=0&&px<g_gui.width&&py>=0&&py<g_gui.height)
        g_gui.pixels[py*g_gui.width+px]=W;
    }
  }
  return 0;
}

/* ---- fb_fill_rounded: rect with 2-pixel corner cut ------------------------ */
long long tn_host_fb_fill_rounded(long long x, long long y, long long w, long long h,
                                   unsigned int color, long long radius){
  if(!g_gui.ready) return -1;
  (void)radius; /* reserved — use 2-pixel corner cut for now */
  if(w<=0||h<=0) return 0;
  unsigned int argb = tn_host_argb_from_rgb(color);
  int X=(int)x,Y=(int)y,W=(int)w,H=(int)h;
  for(int row=0;row<H;row++){
    int py=Y+row;
    if(py<0||py>=g_gui.height) continue;
    int xs=X, xe=X+W;
    /* cut corners */
    if(row<2||row>=H-2){ xs+=2; xe-=2; }
    for(int px=xs;px<xe;px++){
      if(px>=0&&px<g_gui.width)
        g_gui.pixels[py*g_gui.width+px]=argb;
    }
  }
  return 0;
}

// Unused / stubs for remaining API
long long tn_host_boot_text(void){ return 0; }
long long tn_host_fb_set_scale(long long scale){ (void)scale; return 1; }
long long tn_host_fb_get_scale(void){ return 1; }
long long tn_host_fb_text_mode(long long mode){ (void)mode; return 0; }
long long tn_host_fb_font_aa_mode(long long mode){ (void)mode; return 1; }
long long tn_host_fb_font_ttf_mode(long long mode){ (void)mode; return 1; }
long long tn_host_fb_draw_logo(long long x, long long y, long long w, long long h){ return tn_host_fb_fill_rect(x, y, w, h, 0x0060A8FFu); }
long long tn_host_fb_draw_wallpaper(long long x, long long y, long long w, long long h){ (void)x; (void)y; (void)w; (void)h; return 0; }
long long tn_host_fb_draw_cursor(long long x, long long y, long long scale){ (void)x; (void)y; (void)scale; return 0; }
long long tn_host_fb_draw_app_icon(long long app, long long x, long long y, long long w, long long h){ (void)app; (void)x; (void)y; (void)w; (void)h; return 0; }
long long tn_host_fb_fill(unsigned int color){ return tn_host_fb_fill_rect(0, 0, g_gui.width, g_gui.height, color); }
long long tn_host_fb_blit(long long sx, long long sy, long long w, long long h, long long dx, long long dy){ (void)sx; (void)sy; (void)w; (void)h; (void)dx; (void)dy; return 0; }
long long tn_host_fb_composite_rect(long long x, long long y, long long w, long long h, unsigned int color, long long alpha){ (void)x; (void)y; (void)w; (void)h; (void)color; (void)alpha; return 0; }
long long tn_host_fb_get_pixel(long long x, long long y){ (void)x; (void)y; return -1; }
long long tn_host_fb_cursor_soft_reset(void){ return 0; }
long long tn_host_fb_cursor_soft_move(long long x, long long y, long long scale){ (void)x; (void)y; (void)scale; return 0; }
long long tn_host_fb_cursor_kind(long long kind){ (void)kind; return 0; }
long long tn_host_fb_set_color(unsigned int color){ (void)color; return 0; }
long long tn_host_fb_set_cursor(long long x, long long y){ (void)x; (void)y; return 0; }
long long tn_host_fb_cursor_mode(long long mode){ (void)mode; return 0; }
long long tn_host_fb_cursor(long long on, unsigned int color){ (void)on; (void)color; return 0; }
long long tn_host_fb_host_windowing(void){ return 1; }
