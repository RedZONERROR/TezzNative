// runtime/boot/riscv64/hal.c

#include <stdint.h>

#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__) || defined(TN_WINABI) || defined(TEZZ_UEFI)
#define TN_MSABI __attribute__((ms_abi))
#else
#define TN_MSABI
#endif

TN_MSABI long long arch_load_cr3(void* pml4){
  (void)pml4;
  return 0;
}

TN_MSABI long long arch_enable_paging(void){
  return 0;
}

TN_MSABI long long arch_invlpg(void* p){
  (void)p;
  return 0;
}

TN_MSABI long long arch_rdtsc(void){
#if defined(__riscv)
  unsigned long long v = 0;
  __asm__ volatile("rdcycle %0" : "=r"(v));
  return (long long)v;
#else
  return 0;
#endif
}
