// lib/tzmnist_kernels.c -- TezzNative MNIST DataLoader
// Reads IDX binary format files from the official MNIST dataset:
//   train-images-idx3-ubyte  / t10k-images-idx3-ubyte
//   train-labels-idx1-ubyte  / t10k-labels-idx1-ubyte
//
// Build (Windows x64):
//   cl /O2 /LD /MD /nologo tzmnist_kernels.c /Fe:tzmnist.dll
// Build (Linux/macOS):
//   gcc -O2 -shared -fPIC tzmnist_kernels.c -o libtzmnist.so

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#  define TZ_MNIST_EXPORT __declspec(dllexport)
#else
#  define TZ_MNIST_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ── Internal helpers ─────────────────────────────────────────────────────── */
static float*   fp(int64_t p){ return (float*)(uintptr_t)(uint64_t)p; }
static int32_t* ip(int64_t p){ return (int32_t*)(uintptr_t)(uint64_t)p; }

// Swap big-endian int32 (MNIST files are big-endian)
static uint32_t swap32(uint32_t x){
  return ((x&0xFF)<<24)|((x&0xFF00)<<8)|((x&0xFF0000)>>8)|((x>>24)&0xFF);
}

/* ── Dataset handle (allocated on heap, returned as int64 pointer) ────────── */
typedef struct {
  float   *images;      // [n, 784] normalized float32
  int32_t *labels;      // [n] int32
  int64_t  n_samples;
  int64_t  n_features;  // 784 for MNIST
  int64_t  cursor;      // next batch start index
  int64_t  n_classes;
} MnistDataset;

static MnistDataset* ds(int64_t p){ return (MnistDataset*)(uintptr_t)(uint64_t)p; }

/* ── tz_mnist_load ────────────────────────────────────────────────────────── */
// Load MNIST dataset from directory (must contain both images and labels file).
//   dir_ptr:    path to directory containing MNIST IDX files
//   split_ptr:  "train" or "test"
// Returns a handle (int64 pointer to MnistDataset), or 0 on failure.
TZ_MNIST_EXPORT int64_t tz_mnist_load(int64_t dir_ptr, int64_t split_ptr){
  const char *dir   = (const char*)(uintptr_t)(uint64_t)dir_ptr;
  const char *split = (const char*)(uintptr_t)(uint64_t)split_ptr;

  char img_path[512], lbl_path[512];
  int is_train = (strncmp(split, "train", 5) == 0);

  if(is_train){
    snprintf(img_path, 512, "%s/train-images-idx3-ubyte", dir);
    snprintf(lbl_path, 512, "%s/train-labels-idx1-ubyte", dir);
  } else {
    snprintf(img_path, 512, "%s/t10k-images-idx3-ubyte", dir);
    snprintf(lbl_path, 512, "%s/t10k-labels-idx1-ubyte", dir);
  }

  // Open images file
  FILE *fi = fopen(img_path, "rb");
  if(!fi){
    fprintf(stderr,"[mnist] cannot open images: %s\n", img_path);
    return 0;
  }
  uint32_t magic_i=0, n_img=0, rows=0, cols=0;
  fread(&magic_i, 4, 1, fi); magic_i = swap32(magic_i);
  fread(&n_img,   4, 1, fi); n_img   = swap32(n_img);
  fread(&rows,    4, 1, fi); rows    = swap32(rows);
  fread(&cols,    4, 1, fi); cols    = swap32(cols);
  if(magic_i != 0x803){
    fprintf(stderr,"[mnist] bad image magic: 0x%04X\n", magic_i);
    fclose(fi); return 0;
  }

  // Open labels file
  FILE *fl = fopen(lbl_path, "rb");
  if(!fl){
    fprintf(stderr,"[mnist] cannot open labels: %s\n", lbl_path);
    fclose(fi); return 0;
  }
  uint32_t magic_l=0, n_lbl=0;
  fread(&magic_l, 4, 1, fl); magic_l = swap32(magic_l);
  fread(&n_lbl,   4, 1, fl); n_lbl   = swap32(n_lbl);
  if(magic_l != 0x801){
    fprintf(stderr,"[mnist] bad label magic: 0x%04X\n", magic_l);
    fclose(fi); fclose(fl); return 0;
  }
  if(n_img != n_lbl){
    fprintf(stderr,"[mnist] image/label count mismatch: %u vs %u\n", n_img, n_lbl);
    fclose(fi); fclose(fl); return 0;
  }

  int64_t n     = (int64_t)n_img;
  int64_t nfeat = (int64_t)(rows * cols);

  // Allocate dataset
  MnistDataset *dataset = (MnistDataset*)malloc(sizeof(MnistDataset));
  dataset->n_samples  = n;
  dataset->n_features = nfeat;
  dataset->cursor     = 0;
  dataset->n_classes  = 10;
  dataset->images     = (float*)malloc((size_t)(n * nfeat) * sizeof(float));
  dataset->labels     = (int32_t*)malloc((size_t)n * sizeof(int32_t));

  // Read images (uint8) → float32 normalized to [0,1]
  uint8_t *tmp = (uint8_t*)malloc((size_t)(n * nfeat));
  fread(tmp, 1, (size_t)(n * nfeat), fi);
  for(int64_t k = 0; k < n * nfeat; k++)
    dataset->images[k] = (float)tmp[k] / 255.0f;
  free(tmp);

  // Read labels (uint8) → int32
  uint8_t *ltmp = (uint8_t*)malloc((size_t)n);
  fread(ltmp, 1, (size_t)n, fl);
  for(int64_t k = 0; k < n; k++) dataset->labels[k] = (int32_t)ltmp[k];
  free(ltmp);

  fclose(fi);
  fclose(fl);

  fprintf(stdout,"[mnist] loaded %lld %s samples (%lldx%lld features)\n",
          (long long)n, split, (long long)rows, (long long)cols);
  return (int64_t)(uintptr_t)dataset;
}

/* ── tz_mnist_n_samples ───────────────────────────────────────────────────── */
TZ_MNIST_EXPORT int64_t tz_mnist_n_samples(int64_t handle){
  return ds(handle)->n_samples;
}

/* ── tz_mnist_n_features ──────────────────────────────────────────────────── */
TZ_MNIST_EXPORT int64_t tz_mnist_n_features(int64_t handle){
  return ds(handle)->n_features;
}

/* ── tz_mnist_reset ───────────────────────────────────────────────────────── */
// Reset dataset cursor to 0 (start of epoch)
TZ_MNIST_EXPORT int64_t tz_mnist_reset(int64_t handle){
  ds(handle)->cursor = 0;
  return 0;
}

/* ── tz_mnist_next_batch ──────────────────────────────────────────────────── */
// Copy next batch into pre-allocated x_buf[batch_size × n_features] and
// y_buf[batch_size] (int32). Wraps around if near end.
// Returns actual batch size (may be < requested if near end of data).
TZ_MNIST_EXPORT int64_t tz_mnist_next_batch(int64_t handle,
                                              int64_t x_buf,
                                              int64_t y_buf,
                                              int64_t batch_size){
  MnistDataset *d = ds(handle);
  float   *xout = fp(x_buf);
  int32_t *yout = ip(y_buf);

  int64_t n    = d->n_samples;
  int64_t feat = d->n_features;
  int64_t cur  = d->cursor;
  int64_t got  = 0;

  for(int64_t i = 0; i < batch_size; i++){
    int64_t src = cur % n;
    memcpy(xout + i*feat, d->images + src*feat, (size_t)feat * sizeof(float));
    yout[i] = d->labels[src];
    cur++;
    got++;
  }
  d->cursor = cur;
  return got;
}

/* ── tz_mnist_shuffle ─────────────────────────────────────────────────────── */
// Fisher-Yates shuffle the dataset in-place using seed.
TZ_MNIST_EXPORT int64_t tz_mnist_shuffle(int64_t handle, int64_t seed){
  MnistDataset *d = ds(handle);
  int64_t n    = d->n_samples;
  int64_t feat = d->n_features;
  uint64_t rng = (uint64_t)seed ^ 0xDEADBEEFCAFEBABEULL;

  float   *tmp_img = (float*)malloc((size_t)feat * sizeof(float));
  for(int64_t i = n-1; i > 0; i--){
    // xorshift64 rng
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    int64_t j = (int64_t)(rng % (uint64_t)(i+1));
    // swap images
    memcpy(tmp_img,          d->images + j*feat, (size_t)feat * sizeof(float));
    memcpy(d->images + j*feat, d->images + i*feat, (size_t)feat * sizeof(float));
    memcpy(d->images + i*feat, tmp_img,           (size_t)feat * sizeof(float));
    // swap labels
    int32_t tl = d->labels[j];
    d->labels[j] = d->labels[i];
    d->labels[i] = tl;
  }
  free(tmp_img);
  return 0;
}

/* ── tz_mnist_free ────────────────────────────────────────────────────────── */
TZ_MNIST_EXPORT int64_t tz_mnist_free(int64_t handle){
  MnistDataset *d = ds(handle);
  if(!d) return 0;
  free(d->images);
  free(d->labels);
  free(d);
  return 0;
}

/* ── tz_mnist_get_image ───────────────────────────────────────────────────── */
// Copy a single image (normalized float32) into dst_buf[n_features]
TZ_MNIST_EXPORT int64_t tz_mnist_get_image(int64_t handle, int64_t idx, int64_t dst_buf){
  MnistDataset *d = ds(handle);
  float *dst = fp(dst_buf);
  int64_t feat = d->n_features;
  if(idx < 0 || idx >= d->n_samples) return -1;
  memcpy(dst, d->images + idx*feat, (size_t)feat * sizeof(float));
  return 0;
}

/* ── tz_mnist_get_label ───────────────────────────────────────────────────── */
TZ_MNIST_EXPORT int64_t tz_mnist_get_label(int64_t handle, int64_t idx){
  MnistDataset *d = ds(handle);
  if(idx < 0 || idx >= d->n_samples) return -1;
  return (int64_t)d->labels[idx];
}

#ifdef __cplusplus
}
#endif
