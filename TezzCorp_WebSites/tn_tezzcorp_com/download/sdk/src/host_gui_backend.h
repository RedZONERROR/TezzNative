#ifndef TEZZ_HOST_GUI_BACKEND_H
#define TEZZ_HOST_GUI_BACKEND_H

#ifdef __cplusplus
extern "C" {
#endif

int tn_host_gui_backend_compiled(void);
int tn_host_gui_backend_ready(void);

unsigned char* tn_host_fb_addr(void);
long long tn_host_fb_width(void);
long long tn_host_fb_height(void);
long long tn_host_fb_pitch(void);
long long tn_host_fb_bpp(void);
long long tn_host_screen_width(void);
long long tn_host_screen_height(void);
long long tn_host_boot_text(void);
long long tn_host_fb_init_ex(const char* title, long long W, long long H);
long long tn_host_fb_present(void);
long long tn_host_fb_text(long long x, long long y, const unsigned char* s, unsigned int color);
long long tn_host_fb_set_scale(long long scale);
long long tn_host_fb_get_scale(void);
long long tn_host_fb_text_mode(long long mode);
long long tn_host_fb_font_aa_mode(long long mode);
long long tn_host_fb_font_ttf_mode(long long mode);
long long tn_host_fb_draw_logo(long long x, long long y, long long w, long long h);
long long tn_host_fb_draw_wallpaper(long long x, long long y, long long w, long long h);
long long tn_host_fb_draw_cursor(long long x, long long y, long long scale);
long long tn_host_fb_draw_app_icon(long long app, long long x, long long y, long long w, long long h);
long long tn_host_fb_fill(unsigned int color);
long long tn_host_fb_fill_rect(long long x, long long y, long long w, long long h, unsigned int color);
long long tn_host_fb_blit(long long sx, long long sy, long long w, long long h, long long dx, long long dy);
long long tn_host_fb_composite_rect(long long x, long long y, long long w, long long h, unsigned int color, long long alpha);
long long tn_host_fb_get_pixel(long long x, long long y);
long long tn_host_fb_put_pixel(long long x, long long y, unsigned int color);
long long tn_host_fb_cursor_soft_reset(void);
long long tn_host_fb_cursor_soft_move(long long x, long long y, long long scale);
long long tn_host_fb_cursor_kind(long long kind);
long long tn_host_fb_set_color(unsigned int color);
long long tn_host_fb_set_cursor(long long x, long long y);
long long tn_host_fb_cursor_mode(long long mode);
long long tn_host_fb_cursor(long long on, unsigned int color);
long long tn_host_fb_host_windowing(void);

long long tn_host_kbd_has_event(void);
long long tn_host_kbd_read_scancode(void);
long long tn_host_kbd_read_scancode_raw(void);
long long tn_host_kbd_read_char(void);
long long tn_host_kbd_ime_active(void);
long long tn_host_kbd_ime_cursor(void);
long long tn_host_kbd_ime_length(void);
long long tn_host_kbd_set_debug(long long on);
long long tn_host_kbd_last_scancode(void);

long long tn_host_mouse_has_packet(void);
long long tn_host_mouse_read_packet(void);
long long tn_host_mouse_dx(void);
long long tn_host_mouse_dy(void);
long long tn_host_mouse_buttons(void);
long long tn_host_mouse_pos_x(void);
long long tn_host_mouse_pos_y(void);

/* Extended font + cursor + rounded rect */
long long tn_host_fb_text_ex(long long x, long long y, const unsigned char* s,
                              unsigned int color, long long scale, long long bold);
long long tn_host_fb_soft_cursor(long long x, long long y, unsigned int color);
long long tn_host_fb_fill_rounded(long long x, long long y, long long w, long long h,
                                   unsigned int color, long long radius);

#ifdef __cplusplus
}
#endif

#endif
