// lib/tzcheckpoint_kernels.c -- TezzNative Model Checkpoint Save/Load
// Binary format:
//   Header: "TZKP" (4B) | version(4B) | n_tensors(4B) | reserved(4B)
//   Per tensor: name_len(4B) | name(name_len B) | n_elems(8B) | data(n_elems*4 B)
//
// Build (Windows x64):
//   cl /O2 /LD /MD /nologo tzcheckpoint_kernels.c /Fe:tzcheckpoint.dll
// Build (Linux/macOS):
//   gcc -O2 -shared -fPIC tzcheckpoint_kernels.c -o libtzcheckpoint.so

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#  define TZ_CK_EXPORT __declspec(dllexport)
#else
#  define TZ_CK_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define TZKP_MAGIC   0x504B5A54u  // "TZKP" little-endian
#define TZKP_VERSION 1u

/* ── Internal helpers ─────────────────────────────────────────────────────── */
static float* fp(int64_t p){ return (float*)(uintptr_t)(uint64_t)p; }

/* ── tz_checkpoint_save ───────────────────────────────────────────────────── */
// Save N tensors to a binary checkpoint file.
//   path_ptr:  int64 pointer to null-terminated path string
//   ptrs:      int64 pointer to array of N int64 buffer pointers
//   sizes:     int64 pointer to array of N int64 element counts
//   names:     int64 pointer to array of N int64 name string pointers
//   n_tensors: number of tensors
// Returns 0 on success, -1 on error.
TZ_CK_EXPORT int64_t tz_checkpoint_save(int64_t path_ptr,
                                         int64_t ptrs,
                                         int64_t sizes,
                                         int64_t names,
                                         int64_t n_tensors){
  const char  *path     = (const char*)(uintptr_t)(uint64_t)path_ptr;
  int64_t     *buf_ptrs = (int64_t*)(uintptr_t)(uint64_t)ptrs;
  int64_t     *buf_sizes= (int64_t*)(uintptr_t)(uint64_t)sizes;
  int64_t     *name_ptrs= (int64_t*)(uintptr_t)(uint64_t)names;

  FILE *f = fopen(path, "wb");
  if(!f){ fprintf(stderr,"[checkpoint] cannot open '%s' for write\n", path); return -1; }

  // Write header
  uint32_t magic   = TZKP_MAGIC;
  uint32_t version = TZKP_VERSION;
  uint32_t ntens   = (uint32_t)n_tensors;
  uint32_t resv    = 0;
  fwrite(&magic,   4, 1, f);
  fwrite(&version, 4, 1, f);
  fwrite(&ntens,   4, 1, f);
  fwrite(&resv,    4, 1, f);

  // Write each tensor
  int64_t total_floats = 0;
  for(int64_t i = 0; i < n_tensors; i++){
    const char *name     = (const char*)(uintptr_t)(uint64_t)name_ptrs[i];
    int64_t     n_elems  = buf_sizes[i];
    float      *data     = fp(buf_ptrs[i]);
    uint32_t    nlen     = name ? (uint32_t)strlen(name) : 0;

    fwrite(&nlen,    4, 1, f);
    if(nlen > 0) fwrite(name, 1, nlen, f);
    fwrite(&n_elems, 8, 1, f);
    fwrite(data,     4, (size_t)n_elems, f);
    total_floats += n_elems;
  }
  fclose(f);
  fprintf(stdout,"[checkpoint] saved %lld tensors (%lld params) → %s\n",
          (long long)n_tensors, (long long)total_floats, path);
  return 0;
}

/* ── tz_checkpoint_load ───────────────────────────────────────────────────── */
// Load checkpoint. Fills ptrs[i] with the loaded tensor data.
// ptrs[i] must already point to allocated buffers of the right size,
// OR be 0 — in which case we malloc a new buffer and fill ptrs[i].
// Returns n_tensors loaded, or -1 on error.
TZ_CK_EXPORT int64_t tz_checkpoint_load(int64_t path_ptr,
                                         int64_t ptrs,
                                         int64_t sizes,
                                         int64_t n_tensors_expected){
  const char *path     = (const char*)(uintptr_t)(uint64_t)path_ptr;
  int64_t    *buf_ptrs = (int64_t*)(uintptr_t)(uint64_t)ptrs;
  int64_t    *buf_sizes= (int64_t*)(uintptr_t)(uint64_t)sizes;

  FILE *f = fopen(path, "rb");
  if(!f){ fprintf(stderr,"[checkpoint] cannot open '%s' for read\n", path); return -1; }

  uint32_t magic=0, version=0, ntens=0, resv=0;
  fread(&magic,   4, 1, f);
  fread(&version, 4, 1, f);
  fread(&ntens,   4, 1, f);
  fread(&resv,    4, 1, f);

  if(magic != TZKP_MAGIC){
    fprintf(stderr,"[checkpoint] bad magic in '%s' (got 0x%08X)\n", path, magic);
    fclose(f); return -1;
  }
  if(version != TZKP_VERSION){
    fprintf(stderr,"[checkpoint] version mismatch (file=%u, expected=%u)\n",
            version, TZKP_VERSION);
    fclose(f); return -1;
  }

  int64_t to_load = (n_tensors_expected > 0 && (int64_t)ntens > n_tensors_expected)
                  ? n_tensors_expected : (int64_t)ntens;
  char namebuf[256];

  for(int64_t i = 0; i < (int64_t)ntens; i++){
    uint32_t nlen = 0;
    fread(&nlen, 4, 1, f);
    if(nlen > 0 && nlen < 256){ fread(namebuf, 1, nlen, f); namebuf[nlen]=0; }
    else if(nlen >= 256){ fseek(f, (long)nlen, SEEK_CUR); nlen=0; }

    int64_t n_elems = 0;
    fread(&n_elems, 8, 1, f);

    if(i < to_load){
      if(buf_ptrs[i] == 0){
        // Auto-allocate
        buf_ptrs[i] = (int64_t)(uintptr_t)malloc((size_t)n_elems * sizeof(float));
        if(buf_sizes) buf_sizes[i] = n_elems;
      }
      float *dst = fp(buf_ptrs[i]);
      int64_t avail = (buf_sizes && buf_sizes[i] > 0) ? buf_sizes[i] : n_elems;
      int64_t to_read = (n_elems < avail) ? n_elems : avail;
      fread(dst, 4, (size_t)to_read, f);
      if(to_read < n_elems) fseek(f, (long)((n_elems-to_read)*4), SEEK_CUR);
    } else {
      fseek(f, (long)(n_elems*4), SEEK_CUR);
    }
  }

  fclose(f);
  fprintf(stdout,"[checkpoint] loaded %lld tensors from %s\n",
          (long long)to_load, path);
  return to_load;
}

/* ── tz_checkpoint_n_tensors ──────────────────────────────────────────────── */
// Peek at a checkpoint file and return how many tensors it contains.
TZ_CK_EXPORT int64_t tz_checkpoint_n_tensors(int64_t path_ptr){
  const char *path = (const char*)(uintptr_t)(uint64_t)path_ptr;
  FILE *f = fopen(path, "rb");
  if(!f) return -1;
  uint32_t magic=0, version=0, ntens=0, resv=0;
  fread(&magic,   4, 1, f);
  fread(&version, 4, 1, f);
  fread(&ntens,   4, 1, f);
  fread(&resv,    4, 1, f);
  fclose(f);
  if(magic != TZKP_MAGIC) return -1;
  return (int64_t)ntens;
}

/* ── tz_checkpoint_info ───────────────────────────────────────────────────── */
// Print info about a checkpoint file (names + sizes).
TZ_CK_EXPORT int64_t tz_checkpoint_info(int64_t path_ptr){
  const char *path = (const char*)(uintptr_t)(uint64_t)path_ptr;
  FILE *f = fopen(path, "rb");
  if(!f){ fprintf(stderr,"[checkpoint] cannot open '%s'\n", path); return -1; }
  uint32_t magic=0, version=0, ntens=0, resv=0;
  fread(&magic,   4, 1, f);
  fread(&version, 4, 1, f);
  fread(&ntens,   4, 1, f);
  fread(&resv,    4, 1, f);
  if(magic != TZKP_MAGIC){ fclose(f); return -1; }
  fprintf(stdout,"[checkpoint] file: %s | version=%u | tensors=%u\n",
          path, version, ntens);
  char namebuf[256];
  int64_t total = 0;
  for(uint32_t i = 0; i < ntens; i++){
    uint32_t nlen = 0;
    fread(&nlen, 4, 1, f);
    if(nlen > 0 && nlen < 256){ fread(namebuf, 1, nlen, f); namebuf[nlen]=0; }
    else { snprintf(namebuf, 256, "<unnamed>"); if(nlen>0) fseek(f,(long)nlen,SEEK_CUR); }
    int64_t n_elems = 0;
    fread(&n_elems, 8, 1, f);
    fprintf(stdout,"  [%u] %-32s  %lld floats  (%.2f MB)\n",
            i, namebuf, (long long)n_elems, (double)n_elems*4.0/1048576.0);
    fseek(f, (long)(n_elems*4), SEEK_CUR);
    total += n_elems;
  }
  fprintf(stdout,"  Total params: %lld (%.2f MB)\n",
          (long long)total, (double)total*4.0/1048576.0);
  fclose(f);
  return (int64_t)ntens;
}

#ifdef __cplusplus
}
#endif
