// lib/tzsafetensors_kernels.c -- TezzNative HuggingFace Safetensors Binary Loader v2.2
// Zero-dependency reader for HuggingFace .safetensors neural network weights.
// Supports F32, F16 (IEEE-754 half), BF16 (bfloat16), and INT8 weights with automatic F32 upcasting.

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#  define TZ_EXPORT __declspec(dllexport)
#else
#  define TZ_EXPORT
#endif

#define MAX_TENSORS 512
#define MAX_NAME_LEN 128
#define MAX_DIMS 8

typedef enum {
  ST_DTYPE_F32 = 0,
  ST_DTYPE_F16 = 1,
  ST_DTYPE_BF16 = 2,
  ST_DTYPE_I8 = 3,
  ST_DTYPE_I32 = 4,
  ST_DTYPE_UNKNOWN = 99
} StDtype;

typedef struct {
  char name[MAX_NAME_LEN];
  StDtype dtype;
  int64_t shape[MAX_DIMS];
  int ndim;
  int64_t numel;
  uint64_t data_start;
  uint64_t data_end;
} StTensorMeta;

typedef struct {
  FILE* fp;
  uint64_t header_len;
  uint64_t file_size;
  uint64_t data_base_offset;
  StTensorMeta tensors[MAX_TENSORS];
  int tensor_count;
} SafeTensorsFile;

// IEEE half to float conversion
static float f16_to_f32_val(uint16_t h) {
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
  mant = mant << 13;
  uint32_t res = sign | (exp << 23) | mant;
  float f; memcpy(&f, &res, 4); return f;
}

// Bfloat16 to float conversion
static float bf16_to_f32_val(uint16_t bf) {
  uint32_t u = (uint32_t)bf << 16;
  float f; memcpy(&f, &u, 4); return f;
}

// Simple zero-allocation JSON parser for Safetensors headers
static void parse_safetensors_json(SafeTensorsFile* st, const char* json, size_t json_len) {
  const char* p = json;
  const char* end = json + json_len;

  while(p < end && st->tensor_count < MAX_TENSORS) {
    // Find next key
    p = strchr(p, '"');
    if(!p || p >= end) break;
    p++;
    const char* key_start = p;
    const char* key_end = strchr(p, '"');
    if(!key_end || key_end >= end) break;
    size_t klen = (size_t)(key_end - key_start);
    p = key_end + 1;

    // Skip __metadata__
    if(klen == 12 && strncmp(key_start, "__metadata__", 12) == 0) {
      // Skip metadata block
      p = strchr(p, '}');
      if(p) p++;
      continue;
    }

    if(klen >= MAX_NAME_LEN) klen = MAX_NAME_LEN - 1;
    StTensorMeta* meta = &st->tensors[st->tensor_count];
    memcpy(meta->name, key_start, klen);
    meta->name[klen] = '\0';
    meta->dtype = ST_DTYPE_F32;
    meta->ndim = 0;
    meta->numel = 1;
    meta->data_start = 0;
    meta->data_end = 0;

    // Find opening '{' of tensor descriptor
    p = strchr(p, '{');
    if(!p || p >= end) break;
    const char* block_end = strchr(p, '}');
    if(!block_end || block_end >= end) break;

    // Parse dtype
    const char* dt = strstr(p, "\"dtype\"");
    if(dt && dt < block_end) {
      dt = strchr(dt + 7, '"');
      if(dt && dt < block_end) {
        dt++;
        if(strncmp(dt, "F32", 3) == 0) meta->dtype = ST_DTYPE_F32;
        else if(strncmp(dt, "F16", 3) == 0) meta->dtype = ST_DTYPE_F16;
        else if(strncmp(dt, "BF16", 4) == 0) meta->dtype = ST_DTYPE_BF16;
        else if(strncmp(dt, "I8", 2) == 0) meta->dtype = ST_DTYPE_I8;
        else if(strncmp(dt, "I32", 3) == 0) meta->dtype = ST_DTYPE_I32;
      }
    }

    // Parse shape: [d0, d1, ...]
    const char* sh = strstr(p, "\"shape\"");
    if(sh && sh < block_end) {
      sh = strchr(sh, '[');
      if(sh && sh < block_end) {
        sh++;
        while(sh < block_end && *sh != ']' && meta->ndim < MAX_DIMS) {
          while(*sh == ' ' || *sh == ',') sh++;
          if(*sh == ']') break;
          char* nxt = NULL;
          long long dim = strtoll(sh, &nxt, 10);
          if(nxt == sh) break;
          meta->shape[meta->ndim++] = dim;
          meta->numel *= dim;
          sh = nxt;
        }
      }
    }

    // Parse data_offsets: [start, end]
    const char* off = strstr(p, "\"data_offsets\"");
    if(off && off < block_end) {
      off = strchr(off, '[');
      if(off && off < block_end) {
        off++;
        char* nxt = NULL;
        meta->data_start = (uint64_t)strtoull(off, &nxt, 10);
        if(nxt && *nxt == ',') {
          off = nxt + 1;
          meta->data_end = (uint64_t)strtoull(off, &nxt, 10);
        }
      }
    }

    st->tensor_count++;
    p = block_end + 1;
  }
}

// ── Exported API ──────────────────────────────────────────────────────────────

TZ_EXPORT int64_t tz_safetensors_open(int64_t path_cstr) {
  const char* path = (const char*)(uintptr_t)path_cstr;
  if(!path) return 0;

  FILE* f = fopen(path, "rb");
  if(!f) return 0;

  // Read 8-byte header size
  uint64_t header_len = 0;
  if(fread(&header_len, 1, 8, f) != 8 || header_len > 100 * 1024 * 1024) {
    fclose(f);
    return 0;
  }

  char* json_buf = (char*)malloc((size_t)header_len + 1);
  if(!json_buf) { fclose(f); return 0; }

  if(fread(json_buf, 1, (size_t)header_len, f) != header_len) {
    free(json_buf);
    fclose(f);
    return 0;
  }
  json_buf[header_len] = '\0';

  SafeTensorsFile* st = (SafeTensorsFile*)malloc(sizeof(SafeTensorsFile));
  if(!st) { free(json_buf); fclose(f); return 0; }
  memset(st, 0, sizeof(*st));

  st->fp = f;
  st->header_len = header_len;
  st->data_base_offset = 8 + header_len;

  fseek(f, 0, SEEK_END);
  st->file_size = (uint64_t)ftell(f);

  parse_safetensors_json(st, json_buf, (size_t)header_len);
  free(json_buf);

  return (int64_t)(uintptr_t)st;
}

TZ_EXPORT int64_t tz_safetensors_num_tensors(int64_t handle) {
  SafeTensorsFile* st = (SafeTensorsFile*)(uintptr_t)handle;
  return st ? (int64_t)st->tensor_count : 0;
}

TZ_EXPORT int64_t tz_safetensors_tensor_name(int64_t handle, int64_t idx, int64_t out_buf) {
  SafeTensorsFile* st = (SafeTensorsFile*)(uintptr_t)handle;
  char* dst = (char*)(uintptr_t)out_buf;
  if(!st || !dst || idx < 0 || idx >= st->tensor_count) return -1;
  strcpy(dst, st->tensors[idx].name);
  return (int64_t)strlen(dst);
}

TZ_EXPORT int64_t tz_safetensors_tensor_shape(int64_t handle, int64_t name_cstr, int64_t out_shape_buf) {
  SafeTensorsFile* st = (SafeTensorsFile*)(uintptr_t)handle;
  const char* name = (const char*)(uintptr_t)name_cstr;
  int64_t* shape_out = (int64_t*)(uintptr_t)out_shape_buf;
  if(!st || !name || !shape_out) return -1;

  for(int i = 0; i < st->tensor_count; i++) {
    if(strcmp(st->tensors[i].name, name) == 0) {
      for(int d = 0; d < st->tensors[i].ndim; d++) {
        shape_out[d] = st->tensors[i].shape[d];
      }
      return (int64_t)st->tensors[i].ndim;
    }
  }
  return -1;
}

TZ_EXPORT int64_t tz_safetensors_tensor_numel(int64_t handle, int64_t name_cstr) {
  SafeTensorsFile* st = (SafeTensorsFile*)(uintptr_t)handle;
  const char* name = (const char*)(uintptr_t)name_cstr;
  if(!st || !name) return 0;

  for(int i = 0; i < st->tensor_count; i++) {
    if(strcmp(st->tensors[i].name, name) == 0) {
      return st->tensors[i].numel;
    }
  }
  return 0;
}

TZ_EXPORT int64_t tz_safetensors_read_tensor_f32(int64_t handle, int64_t name_cstr, int64_t dst_ptr) {
  SafeTensorsFile* st = (SafeTensorsFile*)(uintptr_t)handle;
  const char* name = (const char*)(uintptr_t)name_cstr;
  float* dst = (float*)(uintptr_t)dst_ptr;
  if(!st || !name || !dst) return -1;

  for(int i = 0; i < st->tensor_count; i++) {
    if(strcmp(st->tensors[i].name, name) == 0) {
      StTensorMeta* meta = &st->tensors[i];
      uint64_t raw_offset = st->data_base_offset + meta->data_start;
      size_t byte_len = (size_t)(meta->data_end - meta->data_start);
      if(byte_len == 0) return 0;

      fseek(st->fp, (long)raw_offset, SEEK_SET);

      if(meta->dtype == ST_DTYPE_F32) {
        if(fread(dst, 1, byte_len, st->fp) != byte_len) return -1;
      } else if(meta->dtype == ST_DTYPE_F16) {
        uint16_t* hbuf = (uint16_t*)malloc(byte_len);
        if(!hbuf) return -1;
        if(fread(hbuf, 1, byte_len, st->fp) != byte_len) { free(hbuf); return -1; }
        size_t count = byte_len / 2;
        for(size_t j = 0; j < count; j++) dst[j] = f16_to_f32_val(hbuf[j]);
        free(hbuf);
      } else if(meta->dtype == ST_DTYPE_BF16) {
        uint16_t* bbuf = (uint16_t*)malloc(byte_len);
        if(!bbuf) return -1;
        if(fread(bbuf, 1, byte_len, st->fp) != byte_len) { free(bbuf); return -1; }
        size_t count = byte_len / 2;
        for(size_t j = 0; j < count; j++) dst[j] = bf16_to_f32_val(bbuf[j]);
        free(bbuf);
      } else if(meta->dtype == ST_DTYPE_I8) {
        int8_t* ibuf = (int8_t*)malloc(byte_len);
        if(!ibuf) return -1;
        if(fread(ibuf, 1, byte_len, st->fp) != byte_len) { free(ibuf); return -1; }
        for(size_t j = 0; j < byte_len; j++) dst[j] = (float)ibuf[j];
        free(ibuf);
      } else {
        return -1;
      }
      return meta->numel;
    }
  }
  return -1;
}

TZ_EXPORT int64_t tz_safetensors_close(int64_t handle) {
  SafeTensorsFile* st = (SafeTensorsFile*)(uintptr_t)handle;
  if(st) {
    if(st->fp) fclose(st->fp);
    free(st);
  }
  return 0;
}
