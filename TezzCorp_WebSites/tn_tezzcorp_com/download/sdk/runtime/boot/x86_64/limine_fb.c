// runtime/boot/x86_64/limine_fb.c
// Minimal Limine framebuffer text output (8x8 font).

#include "../limine/limine.h"
#include "font8x8_basic.h"
#include "tezz_logo32.h"
#if defined(__has_include)
#if __has_include("tezz_cursor_bank.h")
#include "tezz_cursor_bank.h"
#define TEZZ_CURSOR_BANK_AVAILABLE 1
#else
#include "tezz_cursor32.h"
#define TEZZ_CURSOR_BANK_AVAILABLE 0
#endif
#else
#include "tezz_cursor32.h"
#define TEZZ_CURSOR_BANK_AVAILABLE 0
#endif
#include "tezz_app_icons.h"
#include "tezz_wallpaper.h"

#if TEZZ_CURSOR_BANK_AVAILABLE == 0
enum {
  TEZZ_CURSOR_KIND_ARROW = 0,
  TEZZ_CURSOR_KIND_COUNT = 1
};
typedef struct tezz_cursor_shape {
  int w;
  int h;
  int hot_x;
  int hot_y;
  const uint32_t* pixels;
} tezz_cursor_shape;
static const tezz_cursor_shape tezz_cursor_bank[TEZZ_CURSOR_KIND_COUNT] = {
  { TEZZ_CURSOR_W, TEZZ_CURSOR_H, TEZZ_CURSOR_HOT_X, TEZZ_CURSOR_HOT_Y, tezz_cursor_argb }
};
#endif

typedef unsigned long size_t;

static void* fb_stb_memcpy(void* dst, const void* src, size_t n){
  unsigned char* d = (unsigned char*)dst;
  const unsigned char* s = (const unsigned char*)src;
  for(size_t i = 0; i < n; i++) d[i] = s[i];
  return dst;
}

static void* fb_stb_memset(void* dst, int c, size_t n){
  unsigned char* d = (unsigned char*)dst;
  unsigned char v = (unsigned char)c;
  for(size_t i = 0; i < n; i++) d[i] = v;
  return dst;
}

static size_t fb_stb_strlen(const char* s){
  if(!s) return 0;
  size_t n = 0;
  while(s[n] != 0) n++;
  return n;
}

#define FB_STB_ARENA_BYTES (128u * 1024u)
static unsigned char fb_stb_arena[FB_STB_ARENA_BYTES];
static size_t fb_stb_arena_off = 0;

static void fb_stb_arena_reset(void){
  fb_stb_arena_off = 0;
}

static void* fb_stb_malloc(size_t n, void* user){
  (void)user;
  if(n == 0) return 0;
  size_t want = (n + 7u) & ~7u;
  if(want > FB_STB_ARENA_BYTES) return 0;
  if(fb_stb_arena_off > (FB_STB_ARENA_BYTES - want)) return 0;
  void* out = (void*)(fb_stb_arena + fb_stb_arena_off);
  fb_stb_arena_off += want;
  return out;
}

static void fb_stb_free(void* p, void* user){
  (void)p;
  (void)user;
}

static float fb_stb_fabs(float x){
  return (x < 0.0f) ? -x : x;
}

static float fb_stb_floor(float x){
  int i = (int)x;
  if((float)i > x) i -= 1;
  return (float)i;
}

static float fb_stb_ceil(float x){
  int i = (int)x;
  if((float)i < x) i += 1;
  return (float)i;
}

static float fb_stb_sqrt(float x){
  if(x <= 0.0f) return 0.0f;
  float g = (x > 1.0f) ? x : 1.0f;
  for(int i = 0; i < 10; i++){
    g = 0.5f * (g + (x / g));
  }
  return g;
}

static float fb_stb_fmod(float x, float y){
  if(y == 0.0f) return 0.0f;
  int q = (int)(x / y);
  return x - ((float)q * y);
}

static float fb_stb_wrap_pi(float x){
  const float pi = 3.14159265358979323846f;
  const float two_pi = 6.28318530717958647692f;
  while(x > pi) x -= two_pi;
  while(x < -pi) x += two_pi;
  return x;
}

static float fb_stb_cos(float x){
  x = fb_stb_wrap_pi(x);
  float x2 = x * x;
  float t2 = x2 * x2;
  float t3 = t2 * x2;
  return 1.0f - (x2 * 0.5f) + (t2 * (1.0f / 24.0f)) - (t3 * (1.0f / 720.0f));
}

static float fb_stb_acos(float x){
  const float pi = 3.14159265358979323846f;
  if(x <= -1.0f) return pi;
  if(x >= 1.0f) return 0.0f;
  float negate = (x < 0.0f) ? 1.0f : 0.0f;
  if(x < 0.0f) x = -x;
  float ret = -0.0187293f;
  ret = ret * x + 0.0742610f;
  ret = ret * x - 0.2121144f;
  ret = ret * x + 1.5707288f;
  ret = ret * fb_stb_sqrt(1.0f - x);
  ret = ret - (2.0f * negate * ret);
  return (negate * pi) + ret;
}

static int fb_stb_near(float a, float b){
  return fb_stb_fabs(a - b) < 0.0001f;
}

static float fb_stb_cbrt(float x){
  if(x == 0.0f) return 0.0f;
  float a = (x < 0.0f) ? -x : x;
  float g = (a > 1.0f) ? a : 1.0f;
  for(int i = 0; i < 12; i++){
    g = (2.0f * g + (a / (g * g))) / 3.0f;
  }
  return (x < 0.0f) ? -g : g;
}

static float fb_stb_pow(float x, float y){
  if(y == 0.0f) return 1.0f;
  if(y == 1.0f) return x;
  if(fb_stb_near(y, 0.5f)){
    if(x <= 0.0f) return 0.0f;
    return fb_stb_sqrt(x);
  }
  if(fb_stb_near(y, 1.0f / 3.0f)){
    return fb_stb_cbrt(x);
  }
  int yi = (int)y;
  if((float)yi == y){
    float out = 1.0f;
    int exp = yi;
    int neg = 0;
    if(exp < 0){
      neg = 1;
      exp = -exp;
    }
    while(exp > 0){
      out *= x;
      exp--;
    }
    if(neg){
      if(out == 0.0f) return 0.0f;
      return 1.0f / out;
    }
    return out;
  }
  return 0.0f;
}

#ifndef NULL
#define NULL ((void*)0)
#endif

#define STBTT_STATIC
#define STBTT_assert(x) ((void)(x))
#define STBTT_malloc(x, u) fb_stb_malloc((size_t)(x), (u))
#define STBTT_free(x, u) fb_stb_free((x), (u))
#define STBTT_memcpy fb_stb_memcpy
#define STBTT_memset fb_stb_memset
#define STBTT_strlen(x) fb_stb_strlen((const char*)(x))
#define STBTT_ifloor(x) ((int)fb_stb_floor((float)(x)))
#define STBTT_iceil(x) ((int)fb_stb_ceil((float)(x)))
#define STBTT_sqrt(x) fb_stb_sqrt((float)(x))
#define STBTT_pow(x, y) fb_stb_pow((float)(x), (float)(y))
#define STBTT_fmod(x, y) fb_stb_fmod((float)(x), (float)(y))
#define STBTT_cos(x) fb_stb_cos((float)(x))
#define STBTT_acos(x) fb_stb_acos((float)(x))
#define STBTT_fabs(x) fb_stb_fabs((float)(x))
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

__attribute__((section(".limine_requests"), used))
volatile struct limine_framebuffer_request limine_fb_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
    .response = 0
};

static size_t fb_cursor_x = 0;
static size_t fb_cursor_y = 0;
static size_t fb_scale = 1;
static uint32_t fb_fg = 0x00E0E0E0;
static uint32_t fb_cursor_color = 0x00FFFFFF;
static uint8_t fb_cursor_on = 0;
static uint8_t fb_cursor_invert = 0;
static uint8_t fb_text_transparent = 0;
static uint8_t fb_font_aa = 0;
static uint8_t fb_font_ttf = 0;

extern const unsigned char _binary_DejaVuSansMono_ttf_start[] __attribute__((weak));
extern const unsigned char _binary_DejaVuSansMono_ttf_end[] __attribute__((weak));
extern const unsigned char _binary_runtime_assets_DejaVuSansMono_ttf_start[] __attribute__((weak));
extern const unsigned char _binary_runtime_assets_DejaVuSansMono_ttf_end[] __attribute__((weak));

static stbtt_fontinfo fb_ttf_font;
static uint8_t fb_ttf_init_done = 0;
static uint8_t fb_ttf_ready = 0;
static int fb_ttf_ascent = 0;
static int fb_ttf_descent = 0;
static int fb_ttf_line_gap = 0;

#define FB_SOFT_CURSOR_MAX_PIX 20000u
static uint8_t fb_soft_cursor_valid = 0;
static size_t fb_soft_cursor_x = 0;
static size_t fb_soft_cursor_y = 0;
static size_t fb_soft_cursor_w = 0;
static size_t fb_soft_cursor_h = 0;
static uint32_t fb_soft_cursor_saved[FB_SOFT_CURSOR_MAX_PIX];
static int fb_cursor_kind = TEZZ_CURSOR_KIND_ARROW;
#define FB_WALLPAPER_MAP_MAX 8192u
static int fb_wallpaper_xmap[FB_WALLPAPER_MAP_MAX];

static const tezz_cursor_shape* fb_cursor_shape_for_kind(int kind){
  int idx = kind;
  if(idx < 0 || idx >= TEZZ_CURSOR_KIND_COUNT){
    idx = TEZZ_CURSOR_KIND_ARROW;
  }
  const tezz_cursor_shape* cur = &tezz_cursor_bank[idx];
  if(!cur->pixels || cur->w <= 0 || cur->h <= 0){
    cur = &tezz_cursor_bank[TEZZ_CURSOR_KIND_ARROW];
  }
  return cur;
}

static const tezz_cursor_shape* fb_cursor_shape_current(void){
  return fb_cursor_shape_for_kind(fb_cursor_kind);
}

static uint8_t fb_chan_size(uint8_t sz){
  if(sz == 0) return 8;
  if(sz > 16) return 16;
  return sz;
}

static uint8_t fb_chan_shift(uint8_t sh){
  if(sh > 24) return 24;
  return sh;
}

static uint32_t fb_chan_mask(uint8_t sz){
  if(sz >= 31) return 0x7FFFFFFFu;
  return (1u << sz) - 1u;
}

static uint32_t fb_scale_8_to_n(uint8_t v, uint8_t nbits){
  if(nbits == 8) return (uint32_t)v;
  if(nbits > 8) return ((uint32_t)v) << (nbits - 8);
  uint32_t mask = fb_chan_mask(nbits);
  return (uint32_t)((((uint32_t)v * mask) + 127u) / 255u);
}

static uint8_t fb_scale_n_to_8(uint32_t v, uint8_t nbits){
  if(nbits == 8) return (uint8_t)(v & 0xFFu);
  if(nbits > 8) return (uint8_t)((v >> (nbits - 8)) & 0xFFu);
  uint32_t mask = fb_chan_mask(nbits);
  if(mask == 0) return 0;
  return (uint8_t)((v * 255u) / mask);
}

static int fb_masks_invalid(struct limine_framebuffer* fb){
  uint8_t rs = fb->red_mask_size;
  uint8_t gs = fb->green_mask_size;
  uint8_t bs = fb->blue_mask_size;
  uint8_t rsh = fb->red_mask_shift;
  uint8_t gsh = fb->green_mask_shift;
  uint8_t bsh = fb->blue_mask_shift;
  if(rs == 0 && gs == 0 && bs == 0) return 1;
  if(rsh == gsh || rsh == bsh || gsh == bsh) return 1;
  if(fb->bpp >= 24){
    if(rsh > 24 || gsh > 24 || bsh > 24) return 1;
  }
  return 0;
}

static void fb_resolve_masks(struct limine_framebuffer* fb,
                             uint8_t* rs, uint8_t* gs, uint8_t* bs,
                             uint8_t* rsh, uint8_t* gsh, uint8_t* bsh){
  if(fb_masks_invalid(fb)){
    if(fb->bpp <= 16){
      *rs = 5; *gs = 6; *bs = 5;
      *rsh = 11; *gsh = 5; *bsh = 0;
    } else {
      *rs = 8; *gs = 8; *bs = 8;
      *rsh = 16; *gsh = 8; *bsh = 0;
    }
    return;
  }
  *rs = fb_chan_size(fb->red_mask_size);
  *gs = fb_chan_size(fb->green_mask_size);
  *bs = fb_chan_size(fb->blue_mask_size);
  *rsh = fb_chan_shift(fb->red_mask_shift);
  *gsh = fb_chan_shift(fb->green_mask_shift);
  *bsh = fb_chan_shift(fb->blue_mask_shift);
}

static uint32_t fb_read_raw(struct limine_framebuffer* fb, size_t x, size_t y){
  if(x >= fb->width || y >= fb->height) return 0;
  uint8_t* base = (uint8_t*)fb->address;
  size_t bpp = fb->bpp / 8;
  if(bpp < 3) bpp = 4;
  uint8_t* p = base + y * fb->pitch + x * bpp;
  uint32_t raw = 0;
  raw |= (uint32_t)p[0];
  raw |= ((uint32_t)p[1]) << 8;
  raw |= ((uint32_t)p[2]) << 16;
  if(bpp >= 4){
    raw |= ((uint32_t)p[3]) << 24;
  }
  return raw;
}

static void fb_write_raw(struct limine_framebuffer* fb, size_t x, size_t y, uint32_t raw){
  if(x >= fb->width || y >= fb->height) return;
  uint8_t* base = (uint8_t*)fb->address;
  size_t bpp = fb->bpp / 8;
  if(bpp < 3) bpp = 4;
  uint8_t* p = base + y * fb->pitch + x * bpp;
  p[0] = (uint8_t)(raw & 0xFFu);
  p[1] = (uint8_t)((raw >> 8) & 0xFFu);
  p[2] = (uint8_t)((raw >> 16) & 0xFFu);
  if(bpp >= 4){
    p[3] = (uint8_t)((raw >> 24) & 0xFFu);
  }
}

static uint32_t fb_pack_rgb(struct limine_framebuffer* fb, uint32_t color){
  uint8_t rs = 0;
  uint8_t gs = 0;
  uint8_t bs = 0;
  uint8_t rsh = 0;
  uint8_t gsh = 0;
  uint8_t bsh = 0;
  fb_resolve_masks(fb, &rs, &gs, &bs, &rsh, &gsh, &bsh);
  uint32_t r = fb_scale_8_to_n((uint8_t)((color >> 16) & 0xFFu), rs) & fb_chan_mask(rs);
  uint32_t g = fb_scale_8_to_n((uint8_t)((color >> 8) & 0xFFu), gs) & fb_chan_mask(gs);
  uint32_t b = fb_scale_8_to_n((uint8_t)(color & 0xFFu), bs) & fb_chan_mask(bs);
  uint32_t raw = 0;
  raw |= r << rsh;
  raw |= g << gsh;
  raw |= b << bsh;
  return raw;
}

static uint32_t fb_unpack_rgb(struct limine_framebuffer* fb, uint32_t raw){
  uint8_t rs = 0;
  uint8_t gs = 0;
  uint8_t bs = 0;
  uint8_t rsh = 0;
  uint8_t gsh = 0;
  uint8_t bsh = 0;
  fb_resolve_masks(fb, &rs, &gs, &bs, &rsh, &gsh, &bsh);
  uint32_t rn = (raw >> rsh) & fb_chan_mask(rs);
  uint32_t gn = (raw >> gsh) & fb_chan_mask(gs);
  uint32_t bn = (raw >> bsh) & fb_chan_mask(bs);
  uint8_t r = fb_scale_n_to_8(rn, rs);
  uint8_t g = fb_scale_n_to_8(gn, gs);
  uint8_t b = fb_scale_n_to_8(bn, bs);
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void fb_putpixel(struct limine_framebuffer* fb, size_t x, size_t y, uint32_t color){
  uint32_t raw = fb_pack_rgb(fb, color);
  fb_write_raw(fb, x, y, raw);
}

static void fb_blendpixel(struct limine_framebuffer* fb, size_t x, size_t y, uint32_t color, uint8_t alpha){
  if(alpha == 0){
    return;
  }
  if(alpha >= 255){
    fb_putpixel(fb, x, y, color);
    return;
  }
  if(x >= fb->width || y >= fb->height) return;
  uint32_t dst = fb_unpack_rgb(fb, fb_read_raw(fb, x, y));
  uint32_t inv = 255u - (uint32_t)alpha;
  uint32_t src_b = (uint32_t)(color & 0xFFu);
  uint32_t src_g = (uint32_t)((color >> 8) & 0xFFu);
  uint32_t src_r = (uint32_t)((color >> 16) & 0xFFu);
  uint32_t dst_b = (uint32_t)(dst & 0xFFu);
  uint32_t dst_g = (uint32_t)((dst >> 8) & 0xFFu);
  uint32_t dst_r = (uint32_t)((dst >> 16) & 0xFFu);
  uint32_t out_b = (src_b * (uint32_t)alpha + dst_b * inv) / 255u;
  uint32_t out_g = (src_g * (uint32_t)alpha + dst_g * inv) / 255u;
  uint32_t out_r = (src_r * (uint32_t)alpha + dst_r * inv) / 255u;
  fb_putpixel(fb, x, y, (out_r << 16) | (out_g << 8) | out_b);
}

static void fb_soft_cursor_restore(struct limine_framebuffer* fb){
  if(!fb_soft_cursor_valid) return;
  size_t i = 0;
  for(size_t yy = 0; yy < fb_soft_cursor_h; yy++){
    size_t dy = fb_soft_cursor_y + yy;
    if(dy >= fb->height){
      i += fb_soft_cursor_w;
      continue;
    }
    for(size_t xx = 0; xx < fb_soft_cursor_w; xx++){
      size_t dx = fb_soft_cursor_x + xx;
      if(dx < fb->width && i < FB_SOFT_CURSOR_MAX_PIX){
        fb_write_raw(fb, dx, dy, fb_soft_cursor_saved[i]);
      }
      i++;
    }
  }
  fb_soft_cursor_valid = 0;
}

long long limine_fb_fill_rect(long long x, long long y, long long w, long long h, unsigned int color){
  if(!limine_fb_request.response) return -1;
  if(limine_fb_request.response->framebuffer_count == 0) return -1;
  struct limine_framebuffer* fb = limine_fb_request.response->framebuffers[0];
  if(!fb || fb->bpp < 24) return -1;
  if(w <= 0 || h <= 0) return 0;
  long long x0 = x;
  long long y0 = y;
  long long x1 = x + w;
  long long y1 = y + h;
  if(x0 < 0) x0 = 0;
  if(y0 < 0) y0 = 0;
  if(x1 > (long long)fb->width) x1 = (long long)fb->width;
  if(y1 > (long long)fb->height) y1 = (long long)fb->height;
  if(x1 <= x0 || y1 <= y0) return 0;
  uint32_t raw = fb_pack_rgb(fb, color);
  size_t bpp = fb->bpp / 8;
  if(bpp < 3) bpp = 4;
  uint8_t* base = (uint8_t*)fb->address;
  for(long long yy = y0; yy < y1; yy++){
    uint8_t* row = base + (size_t)yy * fb->pitch + (size_t)x0 * bpp;
    for(long long xx = x0; xx < x1; xx++){
      row[0] = (uint8_t)(raw & 0xFFu);
      row[1] = (uint8_t)((raw >> 8) & 0xFFu);
      row[2] = (uint8_t)((raw >> 16) & 0xFFu);
      if(bpp >= 4){
        row[3] = (uint8_t)((raw >> 24) & 0xFFu);
      }
      row += bpp;
    }
  }
  return 0;
}

static void fb_row_move(uint8_t* dst, const uint8_t* src, size_t n){
  if(!dst || !src || n == 0 || dst == src) return;
  if(dst < src){
    for(size_t i = 0; i < n; i++){
      dst[i] = src[i];
    }
    return;
  }
  for(size_t i = n; i > 0; i--){
    dst[i - 1] = src[i - 1];
  }
}

long long limine_fb_blit(long long sx, long long sy, long long w, long long h, long long dx, long long dy){
  if(!limine_fb_request.response) return -1;
  if(limine_fb_request.response->framebuffer_count == 0) return -1;
  struct limine_framebuffer* fb = limine_fb_request.response->framebuffers[0];
  if(!fb || fb->bpp < 24) return -1;
  if(w <= 0 || h <= 0) return 0;
  long long sx0 = sx;
  long long sy0 = sy;
  long long dx0 = dx;
  long long dy0 = dy;
  long long rw = w;
  long long rh = h;
  if(sx0 < 0){ dx0 -= sx0; rw += sx0; sx0 = 0; }
  if(sy0 < 0){ dy0 -= sy0; rh += sy0; sy0 = 0; }
  if(dx0 < 0){ sx0 -= dx0; rw += dx0; dx0 = 0; }
  if(dy0 < 0){ sy0 -= dy0; rh += dy0; dy0 = 0; }
  if(sx0 + rw > (long long)fb->width) rw = (long long)fb->width - sx0;
  if(dx0 + rw > (long long)fb->width) rw = (long long)fb->width - dx0;
  if(sy0 + rh > (long long)fb->height) rh = (long long)fb->height - sy0;
  if(dy0 + rh > (long long)fb->height) rh = (long long)fb->height - dy0;
  if(rw <= 0 || rh <= 0) return 0;
  size_t bpp = fb->bpp / 8;
  if(bpp < 3) bpp = 4;
  size_t bytes = (size_t)rw * bpp;
  uint8_t* base = (uint8_t*)fb->address;
  if(dy0 > sy0){
    for(long long row = rh - 1; row >= 0; row--){
      uint8_t* src = base + (size_t)(sy0 + row) * fb->pitch + (size_t)sx0 * bpp;
      uint8_t* dst = base + (size_t)(dy0 + row) * fb->pitch + (size_t)dx0 * bpp;
      fb_row_move(dst, src, bytes);
    }
  } else {
    for(long long row = 0; row < rh; row++){
      uint8_t* src = base + (size_t)(sy0 + row) * fb->pitch + (size_t)sx0 * bpp;
      uint8_t* dst = base + (size_t)(dy0 + row) * fb->pitch + (size_t)dx0 * bpp;
      fb_row_move(dst, src, bytes);
    }
  }
  return 0;
}

long long limine_fb_composite_rect(long long x, long long y, long long w, long long h, unsigned int color, long long alpha){
  if(!limine_fb_request.response) return -1;
  if(limine_fb_request.response->framebuffer_count == 0) return -1;
  struct limine_framebuffer* fb = limine_fb_request.response->framebuffers[0];
  if(!fb || fb->bpp < 24) return -1;
  if(w <= 0 || h <= 0) return 0;
  if(alpha <= 0) return 0;
  if(alpha >= 255) return limine_fb_fill_rect(x, y, w, h, color);
  long long x0 = x;
  long long y0 = y;
  long long x1 = x + w;
  long long y1 = y + h;
  if(x0 < 0) x0 = 0;
  if(y0 < 0) y0 = 0;
  if(x1 > (long long)fb->width) x1 = (long long)fb->width;
  if(y1 > (long long)fb->height) y1 = (long long)fb->height;
  if(x1 <= x0 || y1 <= y0) return 0;
  for(long long yy = y0; yy < y1; yy++){
    for(long long xx = x0; xx < x1; xx++){
      fb_blendpixel(fb, (size_t)xx, (size_t)yy, color, (uint8_t)alpha);
    }
  }
  return 0;
}

long long limine_fb_fill(unsigned int color){
  if(!limine_fb_request.response) return -1;
  if(limine_fb_request.response->framebuffer_count == 0) return -1;
  struct limine_framebuffer* fb = limine_fb_request.response->framebuffers[0];
  if(!fb || fb->bpp < 24) return -1;
  return limine_fb_fill_rect(0, 0, (long long)fb->width, (long long)fb->height, color);
}

long long limine_fb_get_pixel(long long x, long long y){
  if(!limine_fb_request.response) return -1;
  if(limine_fb_request.response->framebuffer_count == 0) return -1;
  struct limine_framebuffer* fb = limine_fb_request.response->framebuffers[0];
  if(!fb || fb->bpp < 24) return -1;
  if(x < 0 || y < 0) return -1;
  if((uint64_t)x >= fb->width || (uint64_t)y >= fb->height) return -1;
  return (long long)fb_unpack_rgb(fb, fb_read_raw(fb, (size_t)x, (size_t)y));
}

long long limine_fb_put_pixel(long long x, long long y, unsigned int color){
  if(!limine_fb_request.response) return -1;
  if(limine_fb_request.response->framebuffer_count == 0) return -1;
  struct limine_framebuffer* fb = limine_fb_request.response->framebuffers[0];
  if(!fb || fb->bpp < 24) return -1;
  if(x < 0 || y < 0) return -1;
  if((uint64_t)x >= fb->width || (uint64_t)y >= fb->height) return -1;
  fb_putpixel(fb, (size_t)x, (size_t)y, color);
  return 0;
}

static uint32_t fb_bg = 0x000000;

static int fb_ttf_try_blob(const unsigned char* start, const unsigned char* end){
  uintptr_t a = (uintptr_t)start;
  uintptr_t b = (uintptr_t)end;
  if(a == 0 || b == 0 || b <= a) return 0;
  int off = stbtt_GetFontOffsetForIndex(start, 0);
  if(off < 0) return 0;
  if(!stbtt_InitFont(&fb_ttf_font, start, off)) return 0;
  stbtt_GetFontVMetrics(&fb_ttf_font, &fb_ttf_ascent, &fb_ttf_descent, &fb_ttf_line_gap);
  fb_ttf_ready = 1;
  return 1;
}

static void fb_ttf_init_once(void){
  if(fb_ttf_init_done) return;
  fb_ttf_init_done = 1;
  fb_ttf_ready = 0;
  if(fb_ttf_try_blob(_binary_DejaVuSansMono_ttf_start, _binary_DejaVuSansMono_ttf_end)) return;
  fb_ttf_try_blob(_binary_runtime_assets_DejaVuSansMono_ttf_start, _binary_runtime_assets_DejaVuSansMono_ttf_end);
}

static int glyph_px_on(const unsigned char* glyph, int row, int col){
  if(!glyph) return 0;
  if(row < 0 || row >= 8 || col < 0 || col >= 8) return 0;
  return (glyph[row] & (1u << col)) ? 1 : 0;
}

static uint8_t glyph_edge_alpha(const unsigned char* glyph, int row, int col){
  if(glyph_px_on(glyph, row, col)) return 255;
  int orth = 0;
  int diag = 0;
  if(glyph_px_on(glyph, row - 1, col)) orth++;
  if(glyph_px_on(glyph, row + 1, col)) orth++;
  if(glyph_px_on(glyph, row, col - 1)) orth++;
  if(glyph_px_on(glyph, row, col + 1)) orth++;
  if(glyph_px_on(glyph, row - 1, col - 1)) diag++;
  if(glyph_px_on(glyph, row - 1, col + 1)) diag++;
  if(glyph_px_on(glyph, row + 1, col - 1)) diag++;
  if(glyph_px_on(glyph, row + 1, col + 1)) diag++;
  if(orth > 0){
    return 120;
  }
  if(diag > 0){
    return 80;
  }
  return 0;
}

static void fb_fill_cell(struct limine_framebuffer* fb, size_t x, size_t y, uint32_t color, size_t scale){
  size_t cw = 8 * scale;
  size_t ch = 8 * scale;
  for(size_t row=0; row<ch; row++){
    for(size_t col=0; col<cw; col++){
      fb_putpixel(fb, x + col, y + row, color);
    }
  }
}

static void fb_invert_cell(struct limine_framebuffer* fb, size_t x, size_t y, size_t scale){
  size_t cw = 8 * scale;
  size_t ch = 8 * scale;
  if(!fb || fb->bpp < 24) return;
  for(size_t row=0; row<ch; row++){
    for(size_t col=0; col<cw; col++){
      uint32_t c = fb_unpack_rgb(fb, fb_read_raw(fb, x + col, y + row));
      c = ((0xFFu - ((c >> 16) & 0xFFu)) << 16) |
          ((0xFFu - ((c >> 8) & 0xFFu)) << 8) |
          (0xFFu - (c & 0xFFu));
      fb_putpixel(fb, x + col, y + row, c);
    }
  }
}

static int fb_draw_char_ttf(struct limine_framebuffer* fb, size_t x, size_t y, unsigned char ch, uint32_t fg, size_t scale){
  fb_ttf_init_once();
  if(!fb_ttf_ready) return 0;
  if(ch >= 128) ch = '?';
  if(ch == ' '){
    if(!fb_text_transparent){
      fb_fill_cell(fb, x, y, fb_bg, scale);
    }
    return 1;
  }
  if(!fb_text_transparent){
    fb_fill_cell(fb, x, y, fb_bg, scale);
  }
  size_t cell_w = 8u * scale;
  size_t cell_h = 8u * scale;
  if(cell_w == 0 || cell_h == 0) return 1;
  float glyph_scale = stbtt_ScaleForPixelHeight(&fb_ttf_font, (float)cell_h);
  if(glyph_scale <= 0.0f) return 0;
  int glyph = stbtt_FindGlyphIndex(&fb_ttf_font, (int)ch);
  if(glyph <= 0){
    glyph = stbtt_FindGlyphIndex(&fb_ttf_font, (int)'?');
  }
  int gx0 = 0;
  int gy0 = 0;
  int gx1 = 0;
  int gy1 = 0;
  stbtt_GetGlyphBitmapBox(&fb_ttf_font, glyph, glyph_scale, glyph_scale, &gx0, &gy0, &gx1, &gy1);
  int gw = gx1 - gx0;
  int gh = gy1 - gy0;
  if(gw <= 0 || gh <= 0) return 1;
  if((size_t)(gw * gh) > 4096u) return 0;
  unsigned char bmp[4096];
  fb_stb_memset(bmp, 0, sizeof(bmp));
  fb_stb_arena_reset();
  stbtt_MakeGlyphBitmap(&fb_ttf_font, bmp, gw, gh, gw, glyph_scale, glyph_scale, glyph);
  int baseline = (int)((float)fb_ttf_ascent * glyph_scale);
  int cell_x0 = (int)x;
  int cell_y0 = (int)y;
  int cell_x1 = cell_x0 + (int)cell_w;
  int cell_y1 = cell_y0 + (int)cell_h;
  int origin_x = cell_x0;
  int origin_y = cell_y0 + baseline;
  for(int row = 0; row < gh; row++){
    for(int col = 0; col < gw; col++){
      uint8_t alpha = bmp[row * gw + col];
      if(alpha == 0) continue;
      if(!fb_font_aa){
        if(alpha < 128) continue;
        alpha = 255;
      }
      int dx = origin_x + gx0 + col;
      int dy = origin_y + gy0 + row;
      if(dx < cell_x0 || dx >= cell_x1 || dy < cell_y0 || dy >= cell_y1){
        continue;
      }
      fb_blendpixel(fb, (size_t)dx, (size_t)dy, fg, alpha);
    }
  }
  return 1;
}

static void fb_draw_char_at_scaled(struct limine_framebuffer* fb, size_t x, size_t y, unsigned char ch, uint32_t fg, size_t scale){
  if(fb_font_ttf){
    if(fb_draw_char_ttf(fb, x, y, ch, fg, scale)){
      return;
    }
  }
  if(ch >= 128) ch = '?';
  if(ch == ' '){
    if(!fb_text_transparent){
      fb_fill_cell(fb, x, y, fb_bg, scale);
    }
    return;
  }
  if(!fb_text_transparent){
    fb_fill_cell(fb, x, y, fb_bg, scale);
  }
  const unsigned char* glyph = tezz_font8x8_basic[ch];
  for(size_t row=0; row<8; row++){
    for(size_t col=0; col<8; col++){
      uint8_t alpha = glyph_edge_alpha(glyph, (int)row, (int)col);
      if(alpha == 0){
        continue;
      }
      if(!fb_font_aa && alpha < 255){
        continue;
      }
      size_t px = x + col * scale;
      size_t py = y + row * scale;
      if(alpha < 255 && scale > 1){
        alpha = (uint8_t)((alpha * 3u) / 4u);
      }
      for(size_t sy=0; sy<scale; sy++){
        for(size_t sx=0; sx<scale; sx++){
          if(alpha >= 255){
            fb_putpixel(fb, px + sx, py + sy, fg);
          } else {
            fb_blendpixel(fb, px + sx, py + sy, fg, alpha);
          }
        }
      }
    }
  }
}

static void fb_scroll(struct limine_framebuffer* fb, size_t step){
  if(step == 0 || step >= fb->height) return;
  size_t bpp = fb->bpp / 8;
  uint8_t* base = (uint8_t*)fb->address;
  size_t row_bytes = fb->pitch;
  size_t move_rows = fb->height - step;
  for(size_t y=0; y<move_rows; y++){
    uint8_t* dst = base + y * row_bytes;
    uint8_t* src = base + (y + step) * row_bytes;
    for(size_t i=0;i<row_bytes;i++) dst[i] = src[i];
  }
  // clear last step rows
  for(size_t y=move_rows; y<fb->height; y++){
    uint8_t* dst = base + y * row_bytes;
    for(size_t x=0; x<fb->width; x++){
      uint8_t* p = dst + x * bpp;
      p[0] = 0;
      if(bpp > 1) p[1] = 0;
      if(bpp > 2) p[2] = 0;
      if(bpp > 3) p[3] = 255;
    }
  }
}

long long limine_fb_text_at(const unsigned char* s, long long len, long long x, long long y, uint32_t fg){
  if(!s || len <= 0) return 0;
  if(!limine_fb_request.response) return -1;
  if(limine_fb_request.response->framebuffer_count == 0) return -1;
  struct limine_framebuffer* fb = limine_fb_request.response->framebuffers[0];
  if(!fb || fb->bpp < 24) return -1;
  size_t scale = fb_scale ? fb_scale : 1;
  size_t cw = 8 * scale;
  size_t ch = 8 * scale;
  size_t cx = (size_t)x;
  size_t cy = (size_t)y;
  for(long long i=0;i<len;i++){
    unsigned char c = s[i];
    if(c == '\n'){
      cx = (size_t)x;
      cy += ch;
      if(cy + ch > fb->height){
        fb_scroll(fb, ch);
        cy = fb->height - ch;
      }
      continue;
    }
    if(c == '\r'){
      // Treat carriage return as absolute column 0 for robust line redraws.
      cx = 0;
      continue;
    }
    if(c == '\b'){
      if(cx >= cw){
        cx -= cw;
        fb_draw_char_at_scaled(fb, cx, cy, ' ', fg, scale);
      }
      continue;
    }
    if(cx + cw > fb->width){
      cx = (size_t)x;
      cy += ch;
      if(cy + ch > fb->height){
        fb_scroll(fb, ch);
        cy = fb->height - ch;
      }
    }
    fb_draw_char_at_scaled(fb, cx, cy, c, fg, scale);
    cx += cw;
  }
  fb_cursor_x = cx;
  fb_cursor_y = cy;
  return len;
}

long long limine_fb_text_at_raw(const unsigned char* s, long long len, long long x, long long y, uint32_t fg,
                                void* addr, unsigned long long width, unsigned long long height,
                                unsigned long long pitch, unsigned long long bpp){
  if(!s || len <= 0) return 0;
  if(!addr || width == 0 || height == 0 || pitch == 0) return -1;
  if(bpp < 24) return -1;
  struct limine_framebuffer fb;
  fb.address = addr;
  fb.width = width;
  fb.height = height;
  fb.pitch = pitch;
  fb.bpp = (uint16_t)bpp;
  size_t scale = fb_scale ? fb_scale : 1;
  size_t cw = 8 * scale;
  size_t ch = 8 * scale;
  size_t cx = (size_t)x;
  size_t cy = (size_t)y;
  for(long long i=0;i<len;i++){
    unsigned char c = s[i];
    if(c == '\n'){
      cx = (size_t)x;
      cy += ch;
      if(cy + ch > fb.height){
        fb_scroll(&fb, ch);
        cy = fb.height - ch;
      }
      continue;
    }
    if(c == '\r'){
      // Treat carriage return as absolute column 0 for robust line redraws.
      cx = 0;
      continue;
    }
    if(c == '\b'){
      if(cx >= cw){
        cx -= cw;
        fb_draw_char_at_scaled(&fb, cx, cy, ' ', fg, scale);
      }
      continue;
    }
    if(cx + cw > fb.width){
      cx = (size_t)x;
      cy += ch;
      if(cy + ch > fb.height){
        fb_scroll(&fb, ch);
        cy = fb.height - ch;
      }
    }
    fb_draw_char_at_scaled(&fb, cx, cy, c, fg, scale);
    cx += cw;
  }
  fb_cursor_x = cx;
  fb_cursor_y = cy;
  return len;
}

long long limine_fb_write(const unsigned char* s, long long len){
  if(!s || len <= 0) return 0;
  if(!limine_fb_request.response) return -1;
  if(limine_fb_request.response->framebuffer_count == 0) return -1;
  struct limine_framebuffer* fb = limine_fb_request.response->framebuffers[0];
  if(!fb || fb->bpp < 24) return -1;
  return limine_fb_text_at(s, len, (long long)fb_cursor_x, (long long)fb_cursor_y, fb_fg);
}

long long limine_fb_write_raw(const unsigned char* s, long long len,
                              void* addr, unsigned long long width, unsigned long long height,
                              unsigned long long pitch, unsigned long long bpp){
  return limine_fb_text_at_raw(s, len, (long long)fb_cursor_x, (long long)fb_cursor_y, fb_fg,
                               addr, width, height, pitch, bpp);
}

void limine_fb_marker(void){
  static const unsigned char msg[] = "TEZZ FB OK\n";
  limine_fb_write(msg, (long long)(sizeof(msg) - 1));
}

long long limine_fb_set_scale(long long scale){
  if(scale < 1) scale = 1;
  if(scale > 6) scale = 6;
  fb_scale = (size_t)scale;
  return (long long)fb_scale;
}

long long limine_fb_get_scale(void){
  return (long long)fb_scale;
}

long long limine_fb_text_mode(long long mode){
  long long prev = (fb_text_transparent != 0) ? 1 : 0;
  if(mode >= 0){
    fb_text_transparent = (mode != 0) ? 1 : 0;
  }
  return prev;
}

long long limine_fb_font_aa_mode(long long mode){
  long long prev = (fb_font_aa != 0) ? 1 : 0;
  if(mode >= 0){
    fb_font_aa = (mode != 0) ? 1 : 0;
  }
  return prev;
}

long long limine_fb_font_ttf_mode(long long mode){
  long long prev = (fb_font_ttf != 0) ? 1 : 0;
  if(mode >= 0){
    fb_font_ttf = (mode != 0) ? 1 : 0;
    if(fb_font_ttf){
      fb_ttf_init_once();
    }
  }
  return prev;
}

long long limine_fb_draw_logo(long long x, long long y, long long w, long long h){
  if(!limine_fb_request.response) return -1;
  if(limine_fb_request.response->framebuffer_count == 0) return -1;
  struct limine_framebuffer* fb = limine_fb_request.response->framebuffers[0];
  if(!fb || fb->bpp < 24) return -1;
  if(w <= 0) w = TEZZ_LOGO_W;
  if(h <= 0) h = TEZZ_LOGO_H;
  if(w > 512) w = 512;
  if(h > 512) h = 512;
  for(long long yy = 0; yy < h; yy++){
    long long dy = y + yy;
    if(dy < 0 || (uint64_t)dy >= fb->height) continue;
    long long sy = 0;
    if(h > 1) sy = (yy * (TEZZ_LOGO_H - 1)) / (h - 1);
    if(sy < 0) sy = 0;
    if(sy >= TEZZ_LOGO_H) sy = TEZZ_LOGO_H - 1;
    for(long long xx = 0; xx < w; xx++){
      long long dx = x + xx;
      if(dx < 0 || (uint64_t)dx >= fb->width) continue;
      long long sx = 0;
      if(w > 1) sx = (xx * (TEZZ_LOGO_W - 1)) / (w - 1);
      if(sx < 0) sx = 0;
      if(sx >= TEZZ_LOGO_W) sx = TEZZ_LOGO_W - 1;
      uint32_t color = tezz_logo_rgb[(sy * TEZZ_LOGO_W) + sx];
      fb_putpixel(fb, (size_t)dx, (size_t)dy, color);
    }
  }
  return 0;
}

long long limine_fb_draw_wallpaper(long long x, long long y, long long w, long long h){
  if(!limine_fb_request.response) return -1;
  if(limine_fb_request.response->framebuffer_count == 0) return -1;
  struct limine_framebuffer* fb = limine_fb_request.response->framebuffers[0];
  if(!fb || fb->bpp < 24) return -1;
  if(w <= 0) w = (long long)fb->width;
  if(h <= 0) h = (long long)fb->height;
  long long x0 = x;
  long long y0 = y;
  long long x1 = x + w;
  long long y1 = y + h;
  if(x0 < 0) x0 = 0;
  if(y0 < 0) y0 = 0;
  if(x1 > (long long)fb->width) x1 = (long long)fb->width;
  if(y1 > (long long)fb->height) y1 = (long long)fb->height;
  if(x1 <= x0 || y1 <= y0) return 0;

  size_t bpp = fb->bpp / 8;
  if(bpp < 3) bpp = 4;
  uint8_t* base = (uint8_t*)fb->address;
  long long draw_w = x1 - x0;
  int use_xmap = (draw_w > 0 && (uint64_t)draw_w <= (uint64_t)FB_WALLPAPER_MAP_MAX) ? 1 : 0;
  if(use_xmap){
    for(long long i = 0; i < draw_w; i++){
      long long xx = (x0 + i) - x;
      long long sx = 0;
      if(w > 1) sx = (xx * (TEZZ_WALLPAPER_W - 1)) / (w - 1);
      if(sx < 0) sx = 0;
      if(sx >= TEZZ_WALLPAPER_W) sx = TEZZ_WALLPAPER_W - 1;
      fb_wallpaper_xmap[i] = (int)sx;
    }
  }

  int direct_rgb = 0;
  if(fb->red_mask_size == 8 && fb->green_mask_size == 8 && fb->blue_mask_size == 8 &&
     fb->red_mask_shift == 16 && fb->green_mask_shift == 8 && fb->blue_mask_shift == 0){
    direct_rgb = 1;
  }

  for(long long dy = y0; dy < y1; dy++){
    long long yy = dy - y;
    long long sy = 0;
    if(h > 1) sy = (yy * (TEZZ_WALLPAPER_H - 1)) / (h - 1);
    if(sy < 0) sy = 0;
    if(sy >= TEZZ_WALLPAPER_H) sy = TEZZ_WALLPAPER_H - 1;
    const uint32_t* src_row = &tezz_wallpaper_rgb[sy * TEZZ_WALLPAPER_W];
    uint8_t* row = base + (size_t)dy * fb->pitch + (size_t)x0 * bpp;
    if(bpp >= 4){
      for(long long i = 0; i < draw_w; i++){
        long long sx = 0;
        if(use_xmap){
          sx = (long long)fb_wallpaper_xmap[i];
        } else {
          long long xx = (x0 + i) - x;
          if(w > 1) sx = (xx * (TEZZ_WALLPAPER_W - 1)) / (w - 1);
          if(sx < 0) sx = 0;
          if(sx >= TEZZ_WALLPAPER_W) sx = TEZZ_WALLPAPER_W - 1;
        }
        uint32_t color = src_row[sx];
        uint32_t raw = direct_rgb ? color : fb_pack_rgb(fb, color);
        size_t off = (size_t)i * bpp;
        row[off + 0] = (uint8_t)(raw & 0xFFu);
        row[off + 1] = (uint8_t)((raw >> 8) & 0xFFu);
        row[off + 2] = (uint8_t)((raw >> 16) & 0xFFu);
        row[off + 3] = (uint8_t)((raw >> 24) & 0xFFu);
      }
    } else {
      for(long long i = 0; i < draw_w; i++){
        long long sx = 0;
        if(use_xmap){
          sx = (long long)fb_wallpaper_xmap[i];
        } else {
          long long xx = (x0 + i) - x;
          if(w > 1) sx = (xx * (TEZZ_WALLPAPER_W - 1)) / (w - 1);
          if(sx < 0) sx = 0;
          if(sx >= TEZZ_WALLPAPER_W) sx = TEZZ_WALLPAPER_W - 1;
        }
        uint32_t color = src_row[sx];
        uint32_t raw = direct_rgb ? color : fb_pack_rgb(fb, color);
        size_t off = (size_t)i * bpp;
        row[off + 0] = (uint8_t)(raw & 0xFFu);
        row[off + 1] = (uint8_t)((raw >> 8) & 0xFFu);
        row[off + 2] = (uint8_t)((raw >> 16) & 0xFFu);
      }
    }
  }
  return 0;
}

long long limine_fb_draw_cursor(long long x, long long y, long long scale){
  if(!limine_fb_request.response) return -1;
  if(limine_fb_request.response->framebuffer_count == 0) return -1;
  struct limine_framebuffer* fb = limine_fb_request.response->framebuffers[0];
  if(!fb || fb->bpp < 24) return -1;
  const tezz_cursor_shape* cur = fb_cursor_shape_current();
  if(!cur || !cur->pixels) return -1;
  if(scale < 1) scale = 1;
  if(scale > 4) scale = 4;
  long long top_x = x - ((long long)cur->hot_x * scale);
  long long top_y = y - ((long long)cur->hot_y * scale);
  for(long long yy = 0; yy < (long long)cur->h; yy++){
    for(long long xx = 0; xx < (long long)cur->w; xx++){
      uint32_t argb = cur->pixels[(yy * (long long)cur->w) + xx];
      uint8_t a = (uint8_t)((argb >> 24) & 0xFFu);
      if(a == 0) continue;
      uint32_t rgb = argb & 0x00FFFFFFu;
      long long dy0 = top_y + (yy * scale);
      long long dx0 = top_x + (xx * scale);
      for(long long sy = 0; sy < scale; sy++){
        long long dy = dy0 + sy;
        if(dy < 0 || (uint64_t)dy >= fb->height) continue;
        for(long long sx = 0; sx < scale; sx++){
          long long dx = dx0 + sx;
          if(dx < 0 || (uint64_t)dx >= fb->width) continue;
          fb_blendpixel(fb, (size_t)dx, (size_t)dy, rgb, a);
        }
      }
    }
  }
  return 0;
}

long long limine_fb_draw_app_icon(long long app, long long x, long long y, long long w, long long h){
  if(!limine_fb_request.response) return -1;
  if(limine_fb_request.response->framebuffer_count == 0) return -1;
  struct limine_framebuffer* fb = limine_fb_request.response->framebuffers[0];
  if(!fb || fb->bpp < 24) return -1;
  if(app <= 0 || app >= TEZZ_APP_ICON_COUNT) return -1;
  const uint32_t* src = tezz_app_icons[app];
  if(!src) return -1;
  if(w <= 0) w = TEZZ_APP_ICON_W;
  if(h <= 0) h = TEZZ_APP_ICON_H;
  if(w < 1 || h < 1) return -1;

  long long x0 = x;
  long long y0 = y;
  long long x1 = x + w;
  long long y1 = y + h;
  if(x0 < 0) x0 = 0;
  if(y0 < 0) y0 = 0;
  if(x1 > (long long)fb->width) x1 = (long long)fb->width;
  if(y1 > (long long)fb->height) y1 = (long long)fb->height;
  if(x1 <= x0 || y1 <= y0) return 0;

  for(long long dy = y0; dy < y1; dy++){
    long long yy = dy - y;
    long long sy = 0;
    if(h > 1) sy = (yy * (TEZZ_APP_ICON_H - 1)) / (h - 1);
    if(sy < 0) sy = 0;
    if(sy >= TEZZ_APP_ICON_H) sy = TEZZ_APP_ICON_H - 1;
    const uint32_t* src_row = &src[sy * TEZZ_APP_ICON_W];
    for(long long dx = x0; dx < x1; dx++){
      long long xx = dx - x;
      long long sx = 0;
      if(w > 1) sx = (xx * (TEZZ_APP_ICON_W - 1)) / (w - 1);
      if(sx < 0) sx = 0;
      if(sx >= TEZZ_APP_ICON_W) sx = TEZZ_APP_ICON_W - 1;
      fb_putpixel(fb, (size_t)dx, (size_t)dy, src_row[sx]);
    }
  }
  return 0;
}

long long limine_fb_cursor_overlay_reset(void){
  if(!limine_fb_request.response) return -1;
  if(limine_fb_request.response->framebuffer_count == 0) return -1;
  struct limine_framebuffer* fb = limine_fb_request.response->framebuffers[0];
  if(!fb || fb->bpp < 24) return -1;
  fb_soft_cursor_restore(fb);
  return 0;
}

long long limine_fb_cursor_set_kind(long long kind){
  int want = (int)kind;
  if(want < 0 || want >= TEZZ_CURSOR_KIND_COUNT){
    want = TEZZ_CURSOR_KIND_ARROW;
  }
  if(want == fb_cursor_kind) return (long long)fb_cursor_kind;
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    struct limine_framebuffer* fb = limine_fb_request.response->framebuffers[0];
    if(fb && fb->bpp >= 24){
      fb_soft_cursor_restore(fb);
    } else {
      fb_soft_cursor_valid = 0;
    }
  } else {
    fb_soft_cursor_valid = 0;
  }
  fb_cursor_kind = want;
  return (long long)fb_cursor_kind;
}

long long limine_fb_cursor_overlay_move(long long x, long long y, long long scale){
  if(!limine_fb_request.response) return -1;
  if(limine_fb_request.response->framebuffer_count == 0) return -1;
  struct limine_framebuffer* fb = limine_fb_request.response->framebuffers[0];
  if(!fb || fb->bpp < 24) return -1;
  const tezz_cursor_shape* cur = fb_cursor_shape_current();
  if(!cur || !cur->pixels) return -1;
  if(scale < 1) scale = 1;
  if(scale > 4) scale = 4;

  fb_soft_cursor_restore(fb);

  long long top_x = x - ((long long)cur->hot_x * scale) - 2;
  long long top_y = y - ((long long)cur->hot_y * scale) - 2;
  long long ww = ((long long)cur->w * scale) + 4;
  long long hh = ((long long)cur->h * scale) + 4;
  long long x0 = top_x;
  long long y0 = top_y;
  long long x1 = top_x + ww;
  long long y1 = top_y + hh;
  if(x0 < 0) x0 = 0;
  if(y0 < 0) y0 = 0;
  if(x1 > (long long)fb->width) x1 = (long long)fb->width;
  if(y1 > (long long)fb->height) y1 = (long long)fb->height;
  if(x1 > x0 && y1 > y0){
    size_t sw = (size_t)(x1 - x0);
    size_t sh = (size_t)(y1 - y0);
    size_t need = sw * sh;
    if(need <= FB_SOFT_CURSOR_MAX_PIX){
      size_t i = 0;
      for(size_t yy = 0; yy < sh; yy++){
        size_t dy = (size_t)y0 + yy;
        for(size_t xx = 0; xx < sw; xx++){
          size_t dx = (size_t)x0 + xx;
          fb_soft_cursor_saved[i++] = fb_read_raw(fb, dx, dy);
        }
      }
      fb_soft_cursor_x = (size_t)x0;
      fb_soft_cursor_y = (size_t)y0;
      fb_soft_cursor_w = sw;
      fb_soft_cursor_h = sh;
      fb_soft_cursor_valid = 1;
    } else {
      fb_soft_cursor_valid = 0;
    }
  } else {
    fb_soft_cursor_valid = 0;
  }
  return limine_fb_draw_cursor(x, y, scale);
}

long long limine_fb_set_color(unsigned int fg){
  fb_fg = fg;
  return (long long)fb_fg;
}

long long limine_fb_set_cursor(long long x, long long y){
  if(x < 0) x = 0;
  if(y < 0) y = 0;
  fb_cursor_x = (size_t)x;
  fb_cursor_y = (size_t)y;
  return 0;
}

long long limine_fb_get_cursor_x(void){
  return (long long)fb_cursor_x;
}

long long limine_fb_get_cursor_y(void){
  return (long long)fb_cursor_y;
}

long long limine_fb_cursor_mode(long long mode){
  fb_cursor_invert = (mode != 0) ? 1 : 0;
  fb_cursor_on = 0;
  return (long long)fb_cursor_invert;
}

long long limine_fb_cursor(long long on, unsigned int color){
  if(!limine_fb_request.response) return -1;
  if(limine_fb_request.response->framebuffer_count == 0) return -1;
  struct limine_framebuffer* fb = limine_fb_request.response->framebuffers[0];
  if(!fb || fb->bpp < 24) return -1;
  uint8_t prev_on = fb_cursor_on;
  fb_cursor_on = (on != 0) ? 1 : 0;
  fb_cursor_color = color;
  size_t scale = fb_scale ? fb_scale : 1;
  size_t cw = 8 * scale;
  size_t ch = 8 * scale;
  size_t cx = fb_cursor_x;
  size_t cy = fb_cursor_y;
  if(cx + cw > fb->width){
    if(cx >= cw) cx -= cw;
    else cx = 0;
  }
  if(cy + ch > fb->height){
    if(cy >= ch) cy -= ch;
    else cy = 0;
  }
  if(fb_cursor_invert){
    if(prev_on != fb_cursor_on){
      fb_invert_cell(fb, cx, cy, scale);
    }
  } else {
    fb_fill_cell(fb, cx, cy, fb_cursor_on ? fb_cursor_color : fb_bg, scale);
  }
  return 0;
}
