// runtime/boot/x86/hal.c

#include <stdint.h>

#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__) || defined(TN_WINABI) || defined(TEZZ_UEFI)
#define TN_MSABI __attribute__((ms_abi))
#else
#define TN_MSABI
#endif

TN_MSABI long long arch_load_cr3(void* pml4){
#if defined(__i386__)
  __asm__ volatile("mov %0, %%cr3" : : "r"(pml4) : "memory");
#else
  (void)pml4;
#endif
  return 0;
}

TN_MSABI long long arch_enable_paging(void){
#if defined(__i386__)
  uint32_t cr0 = 0;
  __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
  cr0 |= 0x80000000u;
  __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
#endif
  return 0;
}

TN_MSABI long long arch_invlpg(void* p){
#if defined(__i386__) || defined(__x86_64__)
  __asm__ volatile("invlpg (%0)" : : "r"(p) : "memory");
#else
  (void)p;
#endif
  return 0;
}

TN_MSABI long long arch_rdtsc(void){
#if defined(__i386__) || defined(__x86_64__)
  uint32_t lo = 0, hi = 0;
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return ((long long)hi << 32) | (long long)lo;
#else
  return 0;
#endif
}

TN_MSABI long long os__apic_available(void){ return 0; }
TN_MSABI long long os__apic_init(void){ return -1; }
TN_MSABI long long os__apic_eoi(void){ return 0; }
TN_MSABI long long os__apic_timer_init(long long hz){ (void)hz; return -1; }

TN_MSABI long long os__acpi_hpet_base(void){ return 0; }
