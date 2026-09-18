#include <stdint.h>

#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__) || defined(TN_WINABI) || defined(TEZZ_UEFI)
#define TN_MSABI __attribute__((ms_abi))
#else
#define TN_MSABI
#endif

enum {
  TN_HAL_MEMORY_ABI_MAGIC = 1414350157u,
  TN_HAL_ABI_CALLCONV_SYSV = 1u,
  TN_HAL_ABI_CALLCONV_MSABI = 2u
};

typedef struct {
  uint32_t magic;
  uint16_t word_bytes;
  uint16_t callconv_id;
  uint32_t reserved;
} tn_hal_memory_abi_contract_t;

_Static_assert(sizeof(tn_hal_memory_abi_contract_t) == 12, "tn_hal_memory_abi_contract_t size mismatch");

extern long long tn_hal_memory_abi_magic(void) TN_MSABI;
extern long long tn_hal_memory_abi_word_bytes(void) TN_MSABI;
extern long long tn_hal_memory_abi_callconv_id(void) TN_MSABI;

static const tn_hal_memory_abi_contract_t g_tn_hal_memory_abi = {
  TN_HAL_MEMORY_ABI_MAGIC,
  4,
#if defined(TN_WINABI) || defined(TEZZ_UEFI)
  TN_HAL_ABI_CALLCONV_MSABI,
#else
  TN_HAL_ABI_CALLCONV_SYSV,
#endif
  0
};

TN_MSABI long long tn_hal_memory_abi_validate(void){
  if((uint32_t)tn_hal_memory_abi_magic() != g_tn_hal_memory_abi.magic) return -1;
  if((uint16_t)tn_hal_memory_abi_word_bytes() != g_tn_hal_memory_abi.word_bytes) return -1;
  if((uint16_t)tn_hal_memory_abi_callconv_id() != g_tn_hal_memory_abi.callconv_id) return -1;
  if(sizeof(void*) != g_tn_hal_memory_abi.word_bytes) return -1;
  return 0;
}
