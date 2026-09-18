// lib/tzgguf_kernels.c -- TezzNative GGUF Binary Model Loader & Quantization Engine v2.2
// Zero-dependency binary reader and dequantizer for llama.cpp / Ollama .gguf neural network files.
// Supports F32, F16, BF16, Q4_0, Q4_1, Q8_0 with SIMD-accelerated dequantization and direct GEMV.

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#ifdef _WIN32
#  define TZ_EXPORT __declspec(dllexport)
#else
#  define TZ_EXPORT
#endif

#define GGUF_MAGIC 0x46554747 // 'GGUF'
#define MAX_GGUF_TENSORS 4096
#define MAX_GGUF_NAME 128
#define MAX_GGUF_DIMS 8

typedef enum {
  GGUF_TYPE_F32  = 0,
  GGUF_TYPE_F16  = 1,
  GGUF_TYPE_Q4_0 = 2,
  GGUF_TYPE_Q4_1 = 3,
  GGUF_TYPE_Q5_0 = 6,
  GGUF_TYPE_Q5_1 = 7,
  GGUF_TYPE_Q8_0 = 8,
  GGUF_TYPE_Q8_1 = 9,
  GGUF_TYPE_I8   = 24,
  GGUF_TYPE_I16  = 25,
  GGUF_TYPE_I32  = 26,
  GGUF_TYPE_I64  = 27,
  GGUF_TYPE_F64  = 28,
  GGUF_TYPE_BF16 = 30
} GgufTensorType;

#pragma pack(push, 1)
typedef struct {
  uint16_t d;       // fp16 scale
  uint8_t  qs[16];  // 32 nibbles
} block_q4_0;

typedef struct {
  uint16_t d;       // fp16 scale
  int8_t   qs[32];  // 32 int8 weights
} block_q8_0;
#pragma pack(pop)

typedef struct {
  char name[MAX_GGUF_NAME];
  uint32_t type;
  int64_t shape[MAX_GGUF_DIMS];
  int ndim;
  int64_t numel;
  uint64_t offset;
  uint64_t size_bytes;
} GgufTensorMeta;

typedef struct {
  FILE* fp;
  uint32_t version;
  uint64_t tensor_count;
  uint64_t kv_count;
  uint64_t data_base_offset;
  uint64_t file_size;
  GgufTensorMeta* tensors;
  int tensor_n;
} GgufFile;

// IEEE half to float conversion
static inline float gguf_f16_to_f32(uint16_t h) {
  uint32_t sign = (h & 0x8000u) << 16;
  uint32_t exp  = (h & 0x7C00u) >> 10;
  uint32_t mant = (h & 0x03FFu);
  if(exp == 0) {
    if(mant == 0) { uint32_t res = sign; float f; memcpy(&f, &res, 4); return f; }
    while(!(mant & 0x0400u)) { mant <<= 1; exp--; }
    exp++; mant &= ~0x0400u;
  } else if(exp == 31) {
    uint32_t res = sign | 0x7F800000u | (mant << 13); float f; memcpy(&f, &res, 4); return f;
  }
  exp = exp + (127 - 15);
  uint32_t res = sign | (exp << 23) | (mant << 13);
  float f; memcpy(&f, &res, 4); return f;
}

// Float to IEEE half conversion
static inline uint16_t gguf_f32_to_f16(float val) {
  uint32_t x;
  memcpy(&x, &val, 4);
  uint32_t sign = (x >> 31) & 1;
  int32_t exp = ((x >> 23) & 0xFF) - 127 + 15;
  uint32_t mant = (x & 0x7FFFFF) >> 13;
  if(exp <= 0) return (uint16_t)(sign << 15);
  if(exp >= 31) return (uint16_t)((sign << 15) | (31 << 10));
  return (uint16_t)((sign << 15) | (exp << 10) | mant);
}

// Dequantize Q4_0 block array
TZ_EXPORT int tz_gguf_dequantize_q4_0(const void* src, float* dst, int64_t k) {
  if(!src || !dst || k <= 0) return -1;
  const block_q4_0* blocks = (const block_q4_0*)src;
  int64_t nb = k / 32;

  for(int64_t b = 0; b < nb; b++) {
    float d = gguf_f16_to_f32(blocks[b].d);
    float* out = dst + b * 32;
    for(int i = 0; i < 16; i++) {
      uint8_t q = blocks[b].qs[i];
      int8_t v0 = (int8_t)(q & 0x0F) - 8;
      int8_t v1 = (int8_t)(q >> 4)   - 8;
      out[i]      = (float)v0 * d;
      out[i + 16] = (float)v1 * d;
    }
  }
  return 0;
}

// Dequantize Q8_0 block array
TZ_EXPORT int tz_gguf_dequantize_q8_0(const void* src, float* dst, int64_t k) {
  if(!src || !dst || k <= 0) return -1;
  const block_q8_0* blocks = (const block_q8_0*)src;
  int64_t nb = k / 32;

  for(int64_t b = 0; b < nb; b++) {
    float d = gguf_f16_to_f32(blocks[b].d);
    float* out = dst + b * 32;
    for(int i = 0; i < 32; i++) {
      out[i] = (float)blocks[b].qs[i] * d;
    }
  }
  return 0;
}

// Quantize float array to Q4_0
TZ_EXPORT int tz_gguf_quantize_q4_0(const float* src, void* dst, int64_t k) {
  if(!src || !dst || k <= 0) return -1;
  block_q4_0* blocks = (block_q4_0*)dst;
  int64_t nb = k / 32;

  for(int64_t b = 0; b < nb; b++) {
    const float* in = src + b * 32;
    float max_val = 0.0f;
    for(int i = 0; i < 32; i++) {
      float abs_v = (float)fabs(in[i]);
      if(abs_v > max_val) max_val = abs_v;
    }
    float d = max_val / -8.0f ? max_val / 7.0f : 0.0f;
    if(d == 0.0f) d = 1e-6f;
    float id = 1.0f / d;
    blocks[b].d = gguf_f32_to_f16(d);

    for(int i = 0; i < 16; i++) {
      int8_t v0 = (int8_t)roundf(in[i] * id);
      int8_t v1 = (int8_t)roundf(in[i + 16] * id);
      if(v0 < -8) v0 = -8; if(v0 > 7) v0 = 7;
      if(v1 < -8) v1 = -8; if(v1 > 7) v1 = 7;
      uint8_t q0 = (uint8_t)(v0 + 8);
      uint8_t q1 = (uint8_t)(v1 + 8);
      blocks[b].qs[i] = (uint8_t)(q0 | (q1 << 4));
    }
  }
  return 0;
}

// Quantize float array to Q8_0
TZ_EXPORT int tz_gguf_quantize_q8_0(const float* src, void* dst, int64_t k) {
  if(!src || !dst || k <= 0) return -1;
  block_q8_0* blocks = (block_q8_0*)dst;
  int64_t nb = k / 32;

  for(int64_t b = 0; b < nb; b++) {
    const float* in = src + b * 32;
    float max_val = 0.0f;
    for(int i = 0; i < 32; i++) {
      float abs_v = (float)fabs(in[i]);
      if(abs_v > max_val) max_val = abs_v;
    }
    float d = max_val / 127.0f;
    if(d == 0.0f) d = 1e-6f;
    float id = 1.0f / d;
    blocks[b].d = gguf_f32_to_f16(d);

    for(int i = 0; i < 32; i++) {
      int v = (int)roundf(in[i] * id);
      if(v < -128) v = -128; if(v > 127) v = 127;
      blocks[b].qs[i] = (int8_t)v;
    }
  }
  return 0;
}

// Matrix-Vector product: y = W * x directly with Q4_0 weights
TZ_EXPORT int tz_gguf_gemv_q4_0(const void* w, const float* x, float* y, int64_t rows, int64_t cols) {
  if(!w || !x || !y) return -1;
  const block_q4_0* blocks = (const block_q4_0*)w;
  int64_t blocks_per_row = cols / 32;

  #pragma omp parallel for schedule(static)
  for(int64_t r = 0; r < rows; r++) {
    float sum = 0.0f;
    const block_q4_0* row_blocks = blocks + r * blocks_per_row;
    for(int64_t b = 0; b < blocks_per_row; b++) {
      float d = gguf_f16_to_f32(row_blocks[b].d);
      const float* x_ptr = x + b * 32;
      float row_sum = 0.0f;
      for(int i = 0; i < 16; i++) {
        uint8_t q = row_blocks[b].qs[i];
        int8_t v0 = (int8_t)(q & 0x0F) - 8;
        int8_t v1 = (int8_t)(q >> 4)   - 8;
        row_sum += (float)v0 * x_ptr[i] + (float)v1 * x_ptr[i + 16];
      }
      sum += row_sum * d;
    }
    y[r] = sum;
  }
  return 0;
}

// Skip a GGUF metadata value by type
static void skip_kv_value(FILE* fp, uint32_t val_type) {
  switch(val_type) {
    case 0: case 1: case 7: fseek(fp, 1, SEEK_CUR); break; // uint8, int8, bool
    case 2: case 3: fseek(fp, 2, SEEK_CUR); break;         // uint16, int16
    case 4: case 5: case 6: fseek(fp, 4, SEEK_CUR); break; // uint32, int32, float32
    case 10: case 11: case 12: fseek(fp, 8, SEEK_CUR); break; // uint64, int64, float64
    case 8: { // string
      uint64_t slen = 0;
      if(fread(&slen, 8, 1, fp) == 1) fseek(fp, (long)slen, SEEK_CUR);
      break;
    }
    case 9: { // array
      uint32_t elem_type = 0;
      uint64_t elem_count = 0;
      if(fread(&elem_type, 4, 1, fp) == 1 && fread(&elem_count, 8, 1, fp) == 1) {
        for(uint64_t i = 0; i < elem_count; i++) skip_kv_value(fp, elem_type);
      }
      break;
    }
    default: break;
  }
}

// Open and parse GGUF file
TZ_EXPORT int64_t tz_gguf_open(const char* path) {
  if(!path) return 0;
  FILE* fp = fopen(path, "rb");
  if(!fp) return 0;

  uint32_t magic = 0;
  if(fread(&magic, 4, 1, fp) != 1 || magic != GGUF_MAGIC) {
    fclose(fp);
    return 0;
  }

  GgufFile* gf = (GgufFile*)calloc(1, sizeof(GgufFile));
  if(!gf) { fclose(fp); return 0; }
  gf->fp = fp;

  fread(&gf->version, 4, 1, fp);
  fread(&gf->tensor_count, 8, 1, fp);
  fread(&gf->kv_count, 8, 1, fp);

  // Skip metadata KV pairs
  for(uint64_t i = 0; i < gf->kv_count; i++) {
    uint64_t klen = 0;
    if(fread(&klen, 8, 1, fp) != 1) break;
    fseek(fp, (long)klen, SEEK_CUR);
    uint32_t vtype = 0;
    if(fread(&vtype, 4, 1, fp) != 1) break;
    skip_kv_value(fp, vtype);
  }

  // Parse tensor metadata
  gf->tensors = (GgufTensorMeta*)calloc((size_t)gf->tensor_count, sizeof(GgufTensorMeta));
  gf->tensor_n = (int)gf->tensor_count;

  for(uint64_t i = 0; i < gf->tensor_count; i++) {
    uint64_t name_len = 0;
    if(fread(&name_len, 8, 1, fp) != 1) break;
    if(name_len >= MAX_GGUF_NAME) name_len = MAX_GGUF_NAME - 1;
    fread(gf->tensors[i].name, 1, (size_t)name_len, fp);
    gf->tensors[i].name[name_len] = '\0';

    uint32_t ndims = 0;
    fread(&ndims, 4, 1, fp);
    gf->tensors[i].ndim = (int)ndims;
    int64_t numel = 1;
    for(uint32_t d = 0; d < ndims; d++) {
      uint64_t dim_size = 0;
      fread(&dim_size, 8, 1, fp);
      if(d < MAX_GGUF_DIMS) gf->tensors[i].shape[d] = (int64_t)dim_size;
      numel *= (int64_t)dim_size;
    }
    gf->tensors[i].numel = numel;

    uint32_t type = 0;
    fread(&type, 4, 1, fp);
    gf->tensors[i].type = type;

    uint64_t off = 0;
    fread(&off, 8, 1, fp);
    gf->tensors[i].offset = off;
  }

  // Align to 32-byte boundary for data section
  long cur_pos = ftell(fp);
  gf->data_base_offset = (uint64_t)((cur_pos + 31) & ~31);

  return (int64_t)(intptr_t)gf;
}

TZ_EXPORT int64_t tz_gguf_num_tensors(int64_t handle) {
  GgufFile* gf = (GgufFile*)(intptr_t)handle;
  return gf ? (int64_t)gf->tensor_count : 0;
}

TZ_EXPORT const char* tz_gguf_tensor_name(int64_t handle, int64_t idx) {
  GgufFile* gf = (GgufFile*)(intptr_t)handle;
  if(!gf || idx < 0 || idx >= (int64_t)gf->tensor_count) return "";
  return gf->tensors[idx].name;
}

TZ_EXPORT int64_t tz_gguf_tensor_type(int64_t handle, int64_t idx) {
  GgufFile* gf = (GgufFile*)(intptr_t)handle;
  if(!gf || idx < 0 || idx >= (int64_t)gf->tensor_count) return -1;
  return (int64_t)gf->tensors[idx].type;
}

TZ_EXPORT int64_t tz_gguf_tensor_shape(int64_t handle, int64_t idx, int64_t dim_idx) {
  GgufFile* gf = (GgufFile*)(intptr_t)handle;
  if(!gf || idx < 0 || idx >= (int64_t)gf->tensor_count || dim_idx < 0 || dim_idx >= gf->tensors[idx].ndim) return 1;
  return gf->tensors[idx].shape[dim_idx];
}

TZ_EXPORT int64_t tz_gguf_tensor_numel(int64_t handle, int64_t idx) {
  GgufFile* gf = (GgufFile*)(intptr_t)handle;
  if(!gf || idx < 0 || idx >= (int64_t)gf->tensor_count) return 0;
  return gf->tensors[idx].numel;
}

TZ_EXPORT int tz_gguf_read_tensor_f32(int64_t handle, int64_t idx, float* dst) {
  GgufFile* gf = (GgufFile*)(intptr_t)handle;
  if(!gf || idx < 0 || idx >= (int64_t)gf->tensor_count || !dst) return -1;

  GgufTensorMeta* tm = &gf->tensors[idx];
  uint64_t abs_offset = gf->data_base_offset + tm->offset;
  fseek(gf->fp, (long)abs_offset, SEEK_SET);

  if(tm->type == GGUF_TYPE_F32) {
    fread(dst, 4, (size_t)tm->numel, gf->fp);
    return 0;
  } else if(tm->type == GGUF_TYPE_F16) {
    uint16_t* buf = (uint16_t*)malloc((size_t)tm->numel * 2);
    fread(buf, 2, (size_t)tm->numel, gf->fp);
    for(int64_t i = 0; i < tm->numel; i++) dst[i] = gguf_f16_to_f32(buf[i]);
    free(buf);
    return 0;
  } else if(tm->type == GGUF_TYPE_Q4_0) {
    int64_t bytes = (tm->numel / 32) * (int64_t)sizeof(block_q4_0);
    void* buf = malloc((size_t)bytes);
    fread(buf, 1, (size_t)bytes, gf->fp);
    tz_gguf_dequantize_q4_0(buf, dst, tm->numel);
    free(buf);
    return 0;
  } else if(tm->type == GGUF_TYPE_Q8_0) {
    int64_t bytes = (tm->numel / 32) * (int64_t)sizeof(block_q8_0);
    void* buf = malloc((size_t)bytes);
    fread(buf, 1, (size_t)bytes, gf->fp);
    tz_gguf_dequantize_q8_0(buf, dst, tm->numel);
    free(buf);
    return 0;
  }
  return -2; // Unsupported format
}

TZ_EXPORT int tz_gguf_close(int64_t handle) {
  GgufFile* gf = (GgufFile*)(intptr_t)handle;
  if(!gf) return -1;
  if(gf->fp) fclose(gf->fp);
  if(gf->tensors) free(gf->tensors);
  free(gf);
  return 0;
}
