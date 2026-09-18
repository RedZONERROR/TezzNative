#pragma once
#include <stddef.h>
#include <stdint.h>

// On Windows-hosted Tezz kernel builds, Tezz-generated code uses Win64 ABI
// register passing (RCX/RDX/R8/R9). Keep runtime exports on ms_abi so kernel
// calls into tnrt_fs/bridge are ABI-compatible in BIOS/GRUB/UEFI paths.
#if (defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__) || defined(TN_WINABI) || defined(TEZZ_UEFI))
#define TN_MSABI __attribute__((ms_abi))
#else
#define TN_MSABI
#endif

// Minimal freestanding runtime surface for kernel/OS targets.
// These symbols satisfy the TezzNative native backend in freestanding mode.

TN_MSABI void  say(long long x);
TN_MSABI void  say_f(long long bits);
TN_MSABI void  say_str(unsigned char* s);

TN_MSABI long long len(unsigned char* s);
TN_MSABI long long tn_strcmp(unsigned char* a, unsigned char* b);

TN_MSABI void* memcpy(void* dst, const void* src, size_t n);
TN_MSABI void* memset(void* dst, int c, size_t n);
TN_MSABI int   memcmp(const void* a, const void* b, size_t n);

TN_MSABI void* malloc(size_t n);
TN_MSABI void  free(void* p);

TN_MSABI long long sys_outb(long long port, long long val);
TN_MSABI long long sys_inb(long long port);
TN_MSABI long long sys_write(long long fd, unsigned char* buf, long long len);
TN_MSABI long long sys_exit(long long code);
TN_MSABI long long sys_time_ns(void);
TN_MSABI long long sys_yield(void);
TN_MSABI void* os__fb_addr(void);
TN_MSABI long long os__fb_width(void);
TN_MSABI long long os__fb_height(void);
TN_MSABI long long os__fb_pitch(void);
TN_MSABI long long os__fb_bpp(void);
TN_MSABI long long os__boot_text(void);
TN_MSABI long long os__fb_text(long long x, long long y, unsigned char* s, long long color);
TN_MSABI long long os__fb_set_scale(long long scale);
TN_MSABI long long os__fb_get_scale(void);
TN_MSABI long long os__fb_text_mode(long long mode);
TN_MSABI long long os__fb_font_aa_mode(long long mode);
TN_MSABI long long os__fb_font_ttf_mode(long long mode);
TN_MSABI long long os__fb_draw_logo(long long x, long long y, long long w, long long h);
TN_MSABI long long os__fb_draw_wallpaper(long long x, long long y, long long w, long long h);
TN_MSABI long long os__fb_draw_cursor(long long x, long long y, long long scale);
TN_MSABI long long os__fb_draw_app_icon(long long app, long long x, long long y, long long w, long long h);
TN_MSABI long long os__fb_fill(long long color);
TN_MSABI long long os__fb_fill_rect(long long x, long long y, long long w, long long h, long long color);
TN_MSABI long long os__fb_blit(long long sx, long long sy, long long w, long long h, long long dx, long long dy);
TN_MSABI long long os__fb_composite_rect(long long x, long long y, long long w, long long h, long long color, long long alpha);
TN_MSABI long long os__fb_get_pixel(long long x, long long y);
TN_MSABI long long os__fb_put_pixel(long long x, long long y, long long color);
TN_MSABI long long os__fb_cursor_soft_reset(void);
TN_MSABI long long os__fb_cursor_soft_move(long long x, long long y, long long scale);
TN_MSABI long long os__fb_cursor_kind(long long kind);
TN_MSABI long long os__fb_set_color(long long color);
TN_MSABI long long os__fb_set_cursor(long long x, long long y);
TN_MSABI long long os__memmap_count(void);
TN_MSABI long long os__memmap_base(long long idx);
TN_MSABI long long os__memmap_len(long long idx);
TN_MSABI long long os__memmap_type(long long idx);
TN_MSABI long long os__disk_count(void);
TN_MSABI long long os__disk_media_type(long long idx);
TN_MSABI long long os__disk_table_kind(long long idx);
TN_MSABI long long os__disk_mbr_id(long long idx);
TN_MSABI long long os__disk_part_count(long long idx);
TN_MSABI long long os__disk_part_index(long long disk_idx, long long part_idx);
TN_MSABI long long os__disk_gpt_disk_hi(long long idx);
TN_MSABI long long os__disk_gpt_disk_lo(long long idx);
TN_MSABI long long os__disk_gpt_part_hi(long long idx);
TN_MSABI long long os__disk_gpt_part_lo(long long idx);
TN_MSABI long long os__pci_display_probe(void);
TN_MSABI long long os__pci_display_vendor(void);
TN_MSABI long long os__pci_display_device(void);
TN_MSABI long long os__pci_display_class(void);
TN_MSABI long long os__pci_display_subclass(void);
TN_MSABI long long os__pci_display_prog_if(void);
TN_MSABI long long os__pci_display_bus(void);
TN_MSABI long long os__pci_display_slot(void);
TN_MSABI long long os__pci_display_func(void);
TN_MSABI long long os__pci_display_command(void);
TN_MSABI long long os__pci_display_bar_addr(long long idx);
TN_MSABI long long os__pci_display_bar_size(long long idx);
TN_MSABI long long os__pci_display_bar_is_io(long long idx);
TN_MSABI long long os__pci_display_bar_prefetch(long long idx);
TN_MSABI long long os__pci_display_bar_mem_type(long long idx);
TN_MSABI long long os__pci_display_bar_safe(long long idx);
TN_MSABI long long os__pci_display_mmio_safe(void);
TN_MSABI long long os__pci_usb_probe(void);
TN_MSABI long long os__pci_usb_count(void);
TN_MSABI long long os__pci_usb_kind_count(long long kind);
TN_MSABI long long os__pci_usb_bus(long long idx);
TN_MSABI long long os__pci_usb_slot(long long idx);
TN_MSABI long long os__pci_usb_func(long long idx);
TN_MSABI long long os__pci_usb_prog_if(long long idx);
TN_MSABI long long os__pci_usb_vendor(long long idx);
TN_MSABI long long os__pci_usb_device(long long idx);
TN_MSABI long long os__pci_net_probe(void);
TN_MSABI long long os__pci_net_count(void);
TN_MSABI long long os__pci_net_bus(long long idx);
TN_MSABI long long os__pci_net_slot(long long idx);
TN_MSABI long long os__pci_net_func(long long idx);
TN_MSABI long long os__pci_net_subclass(long long idx);
TN_MSABI long long os__pci_net_prog_if(long long idx);
TN_MSABI long long os__pci_net_vendor(long long idx);
TN_MSABI long long os__pci_net_device(long long idx);
TN_MSABI long long os__pci_audio_probe(void);
TN_MSABI long long os__pci_audio_count(void);
TN_MSABI long long os__pci_audio_bus(long long idx);
TN_MSABI long long os__pci_audio_slot(long long idx);
TN_MSABI long long os__pci_audio_func(long long idx);
TN_MSABI long long os__pci_audio_subclass(long long idx);
TN_MSABI long long os__pci_audio_prog_if(long long idx);
TN_MSABI long long os__pci_audio_vendor(long long idx);
TN_MSABI long long os__pci_audio_device(long long idx);
TN_MSABI long long os__gpu_hw_cursor_available(void);
TN_MSABI long long os__gpu_hw_cursor_enable(long long on);
TN_MSABI long long os__gpu_hw_cursor_move(long long x, long long y);
TN_MSABI long long os__gpu_driver_kind(void);
TN_MSABI long long os__arch_id(void);
TN_MSABI long long os__arch_bits(void);
TN_MSABI long long os__blk_count(void);
TN_MSABI long long os__blk_sector_size(long long dev_idx);
TN_MSABI long long os__blk_sector_count(long long dev_idx);
TN_MSABI long long os__blk_read_sector(long long dev_idx, long long lba, unsigned char* out512);
TN_MSABI long long os__blk_write_sector(long long dev_idx, long long lba, unsigned char* in512);
TN_MSABI long long os__irq_enable(void);
TN_MSABI long long os__irq_disable(void);
TN_MSABI long long os__halt(void);
TN_MSABI long long os__timer_init(long long hz);
TN_MSABI long long os__timer_ticks_irq(void);
TN_MSABI long long os__timer_sleep_ms(long long ms);
TN_MSABI long long os__kbd_has_event(void);
TN_MSABI long long os__kbd_read_scancode(void);
TN_MSABI long long os__kbd_read_scancode_raw(void);
TN_MSABI long long os__kbd_read_char(void);
TN_MSABI long long os__kbd_set_debug(long long on);
TN_MSABI long long os__kbd_last_scancode(void);
TN_MSABI long long os__mouse_has_packet(void);
TN_MSABI long long os__mouse_read_packet(void);
TN_MSABI long long os__mouse_dx(void);
TN_MSABI long long os__mouse_dy(void);
TN_MSABI long long os__mouse_buttons(void);
TN_MSABI long long os__ps2_force_mouse_path(long long profile);
TN_MSABI long long os__apic_available(void);
TN_MSABI long long os__apic_init(void);
TN_MSABI long long os__apic_eoi(void);
TN_MSABI long long os__apic_timer_init(long long hz);
TN_MSABI long long os__hpet_init(void);
TN_MSABI long long os__hpet_set_base(long long base);
TN_MSABI long long os__hpet_time_ns(void);
TN_MSABI long long os__acpi_hpet_base(void);
TN_MSABI long long os__power_battery_present(void);
TN_MSABI long long os__power_battery_percent(void);
TN_MSABI long long os__rtc_probe(void);
TN_MSABI long long os__rtc_year(void);
TN_MSABI long long os__rtc_month(void);
TN_MSABI long long os__rtc_day(void);
TN_MSABI long long os__rtc_hour(void);
TN_MSABI long long os__rtc_minute(void);
TN_MSABI long long os__rtc_second(void);
void tn_fb_boot_marker(void);

// Module-prefixed aliases (for TezzNative sys.* calls)
TN_MSABI long long sys__sys_outb(long long port, long long val);
TN_MSABI long long sys__sys_inb(long long port);
TN_MSABI long long sys__sys_write(long long fd, unsigned char* buf, long long len);
TN_MSABI long long sys__sys_exit(long long code);
TN_MSABI long long sys__sys_time_ns(void);
TN_MSABI long long sys__sys_yield(void);

TN_MSABI unsigned char* tn_async_spawn(unsigned char* fnptr);
TN_MSABI long long tn_async_await(unsigned char* handle);

TN_MSABI long long tn_simd_v4f_add(double* out, const double* a, const double* b);
TN_MSABI long long tn_simd_v4f_sub(double* out, const double* a, const double* b);
TN_MSABI long long tn_simd_v4f_mul(double* out, const double* a, const double* b);
TN_MSABI long long tn_simd_v4f_dot(const double* a, const double* b);
TN_MSABI long long tn_simd_v4i_add(long long* out, const long long* a, const long long* b);
TN_MSABI long long tn_simd_v4f_load(double* out, const double* p);
TN_MSABI long long tn_simd_v4f_store(double* p, const double* v);
TN_MSABI long long tn_simd_v4i_load(long long* out, const long long* p);
TN_MSABI long long tn_simd_v4i_store(long long* p, const long long* v);
