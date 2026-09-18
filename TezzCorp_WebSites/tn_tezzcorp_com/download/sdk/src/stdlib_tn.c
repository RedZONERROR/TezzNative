// src/stdlib_tn.c
#include "vm.h"
#include "util.h"
#include "host_gui_backend.h"
extern long long tn_tts_play_pcm(long long* pcm, long long len, long long rate);
extern long long tn_stt_capture_mic(long long* pcm, long long n_samples, long long rate);
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <ctype.h>
#include <math.h>
#if defined(TN_TLS_OPENSSL) && !defined(_WIN32)
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif
#ifndef _WIN32
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#include <process.h>
#endif
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#endif
#if (defined(__i386__) || defined(__x86_64__)) && (defined(__GNUC__) || defined(__clang__))
  #if defined(__has_include)
    #if __has_include(<cpuid.h>)
      #include <cpuid.h>
      #define TN_HAVE_CPUID_H 1
    #else
      #define TN_HAVE_CPUID_H 0
    #endif
  #else
    #include <cpuid.h>
    #define TN_HAVE_CPUID_H 1
  #endif
#else
  #define TN_HAVE_CPUID_H 0
#endif

#ifdef TN_GUARD_ALLOC
#include "tn_guard_alloc.h"
#define malloc  tn_guard_malloc
#define free    tn_guard_free
#define realloc tn_guard_realloc
#endif

extern long long tn_os_system(unsigned char*);
extern void* tn_mmap_file(unsigned char* path, long long* out_size);
extern void tn_munmap_file(void* view, long long size);
extern long long tn_io_pipe_create(long long* read_fd, long long* write_fd);
extern long long tn_io_fd_read(long long fd, unsigned char* buf, long long n);
extern long long tn_io_fd_write(long long fd, unsigned char* buf, long long n);
extern long long tn_io_fd_close(long long fd);
#if !defined(_WIN32) && !defined(TN_SOCK_T_DEFINED)
typedef int tn_sock_t;
#define TN_INVALID_SOCKET (-1)
#define TN_SOCK_T_DEFINED 1
#endif
static void* xmalloc(size_t n);
static int net_init_impl(void);
static long long net_last_error_impl(void);
#ifndef _WIN32
static int net_set_timeout_impl(tn_sock_t s, long long ms);
#endif
static void tls_set_last_err(long long v);

#if defined(__has_include)
#  if __has_include("../include/stb_image.h")
#    define STBI_NO_HDR
#    define STBI_NO_LINEAR
#    define STBI_NO_PSD
#    define STBI_NO_PIC
#    define STBI_NO_PNM
#    define STBI_NO_GIF
#    define STBI_ONLY_PNG
#    define STBI_ONLY_JPEG
#    include "../include/stb_image.h"
#    define TN_HAVE_STBI 1
#  else
#    define TN_HAVE_STBI 0
#  endif
#else
#  define TN_HAVE_STBI 0
#endif

typedef struct tn_tls_policy_cfg_vm {
  int min_version;                // 0=default, 12=TLS1.2+, 13=TLS1.3-only
  long long handshake_timeout_ms; // 0=runtime default
  int pin_enabled;                // 1 when pin_sha256 is set
  unsigned char pin_sha256[32];   // SHA-256 cert fingerprint bytes
} tn_tls_policy_cfg_vm;

static tn_tls_policy_cfg_vm g_tn_tls_policy_vm = {0, 0, 0, {0}};

static int tn_tls_hex_nibble_vm(int c){
  if(c >= '0' && c <= '9') return c - '0';
  if(c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if(c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

static int tn_tls_parse_sha256_hex_vm(const char* hex, unsigned char out[32]){
  if(!hex || !out) return -1;
  for(int i=0;i<64;i++){
    if(hex[i] == 0) return -1;
  }
  if(hex[64] != 0) return -1;
  for(int i=0;i<32;i++){
    int hi = tn_tls_hex_nibble_vm((unsigned char)hex[i * 2]);
    int lo = tn_tls_hex_nibble_vm((unsigned char)hex[i * 2 + 1]);
    if(hi < 0 || lo < 0) return -1;
    out[i] = (unsigned char)((hi << 4) | lo);
  }
  return 0;
}

static int tls_policy_reset_impl(void){
  g_tn_tls_policy_vm.min_version = 0;
  g_tn_tls_policy_vm.handshake_timeout_ms = 0;
  g_tn_tls_policy_vm.pin_enabled = 0;
  memset(g_tn_tls_policy_vm.pin_sha256, 0, sizeof(g_tn_tls_policy_vm.pin_sha256));
  return 0;
}

static int tls_policy_set_min_impl(long long min_version){
  if(min_version != 0 && min_version != 12 && min_version != 13) return -1;
  g_tn_tls_policy_vm.min_version = (int)min_version;
  return 0;
}

static int tls_policy_set_handshake_timeout_impl(long long timeout_ms){
  if(timeout_ms < 0) return -1;
  g_tn_tls_policy_vm.handshake_timeout_ms = timeout_ms;
  return 0;
}

static int tls_policy_set_pin_sha256_impl(const char* pin_hex){
  if(!pin_hex || !pin_hex[0]){
    g_tn_tls_policy_vm.pin_enabled = 0;
    memset(g_tn_tls_policy_vm.pin_sha256, 0, sizeof(g_tn_tls_policy_vm.pin_sha256));
    return 0;
  }
  unsigned char parsed[32];
  if(tn_tls_parse_sha256_hex_vm(pin_hex, parsed) != 0) return -1;
  memcpy(g_tn_tls_policy_vm.pin_sha256, parsed, 32);
  g_tn_tls_policy_vm.pin_enabled = 1;
  return 0;
}

static long long tls_policy_get_min_impl(void){
  return (long long)g_tn_tls_policy_vm.min_version;
}

static long long tls_policy_get_handshake_timeout_impl(void){
  return g_tn_tls_policy_vm.handshake_timeout_ms;
}

static long long tls_policy_pin_enabled_impl(void){
  return (long long)g_tn_tls_policy_vm.pin_enabled;
}

/* image decode bridge for BC/IR VM builtins */
static unsigned char* tzimage_decode_file_vm(unsigned char* path,
                                             long long req_channels,
                                             long long* out_w,
                                             long long* out_h,
                                             long long* out_channels){
#if TN_HAVE_STBI
  if(out_w) *out_w = 0;
  if(out_h) *out_h = 0;
  if(out_channels) *out_channels = 0;
  if(!path || !path[0]) return NULL;
  int w = 0, h = 0, c = 0;
  int req = (int)req_channels;
  if(req < 0 || req > 4) req = 0;
  unsigned char* pix = stbi_load((const char*)path, &w, &h, &c, req);
  if(!pix) return NULL;
  if(out_w) *out_w = (long long)w;
  if(out_h) *out_h = (long long)h;
  if(out_channels) *out_channels = (long long)(req ? req : c);
  return pix;
#else
  (void)path; (void)req_channels; (void)out_w; (void)out_h; (void)out_channels;
  return NULL;
#endif
}

static long long tzimage_free_vm(unsigned char* p){
#if TN_HAVE_STBI
  if(p) stbi_image_free((void*)p);
#else
  (void)p;
#endif
  return 0;
}

static int tls_policy_build_request_vm(long long min_version,
                                       long long handshake_timeout_ms,
                                       const char* pin_hex,
                                       tn_tls_policy_cfg_vm* out){
  if(!out) return -1;
  if(min_version != 0 && min_version != 12 && min_version != 13) return -1;
  if(handshake_timeout_ms < 0) return -1;
  memset(out, 0, sizeof(*out));
  out->min_version = (int)min_version;
  out->handshake_timeout_ms = handshake_timeout_ms;
  if(pin_hex && pin_hex[0]){
    if(tn_tls_parse_sha256_hex_vm(pin_hex, out->pin_sha256) != 0) return -1;
    out->pin_enabled = 1;
  }
  return 0;
}

#if defined(TN_TLS_OPENSSL) && !defined(_WIN32)
typedef struct tn_tls_ossl_vm {
  int sock;
  SSL_CTX* ctx;
  SSL* ssl;
} tn_tls_ossl_vm;

static void tn_tls_free_ossl_vm(tn_tls_ossl_vm* t);

static int tn_tcp_connect_ossl_vm(const char* host, long long port){
  if(net_init_impl() != 0) return -1;
  char portbuf[16];
  snprintf(portbuf, sizeof(portbuf), "%lld", port);
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  struct addrinfo* res = NULL;
  int rc = getaddrinfo((host && host[0]) ? host : "127.0.0.1", portbuf, &hints, &res);
  if(rc != 0 || !res) return -1;
  int s = -1;
  for(struct addrinfo* ai = res; ai; ai = ai->ai_next){
    int sock = (int)socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if(sock < 0) continue;
    if(connect(sock, ai->ai_addr, (int)ai->ai_addrlen) == 0){
      s = sock;
      break;
    }
    close(sock);
  }
  freeaddrinfo(res);
  return s;
}

static int tn_tls_verify_pin_ossl_vm(tn_tls_ossl_vm* t, const tn_tls_policy_cfg_vm* policy){
  const tn_tls_policy_cfg_vm* pol = policy ? policy : &g_tn_tls_policy_vm;
  if(!t) return -1;
  if(pol->pin_enabled == 0) return 0;
  X509* cert = SSL_get_peer_certificate(t->ssl);
  if(!cert) return -1;
  unsigned char md[EVP_MAX_MD_SIZE];
  unsigned int mdlen = 0;
  int ok = X509_digest(cert, EVP_sha256(), md, &mdlen);
  X509_free(cert);
  if(ok != 1 || mdlen != 32) return -1;
  return (memcmp(md, pol->pin_sha256, 32) == 0) ? 0 : -1;
}

static tn_tls_ossl_vm* tn_tls_connect_ossl_with_policy_vm(const char* host,
                                                           long long port,
                                                           const tn_tls_policy_cfg_vm* policy){
  const tn_tls_policy_cfg_vm* pol = policy ? policy : &g_tn_tls_policy_vm;
  SSL_library_init();
  SSL_load_error_strings();
  OpenSSL_add_all_algorithms();

  SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
  if(!ctx){
    tls_set_last_err((long long)ERR_get_error());
    return NULL;
  }

  if(pol->min_version == 13){
#ifdef TLS1_3_VERSION
    if(SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION) != 1){
      SSL_CTX_free(ctx);
      tls_set_last_err((long long)ERR_get_error());
      return NULL;
    }
#else
    SSL_CTX_free(ctx);
    tls_set_last_err(-1);
    return NULL;
#endif
  } else if(pol->min_version == 12){
#ifdef TLS1_2_VERSION
    if(SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION) != 1){
      SSL_CTX_free(ctx);
      tls_set_last_err((long long)ERR_get_error());
      return NULL;
    }
#else
    SSL_CTX_free(ctx);
    tls_set_last_err(-1);
    return NULL;
#endif
  }

  int sock = tn_tcp_connect_ossl_vm(host, port);
  if(sock < 0){
    SSL_CTX_free(ctx);
    tls_set_last_err((long long)net_last_error_impl());
    return NULL;
  }

  if(pol->handshake_timeout_ms > 0){
    if(net_set_timeout_impl(sock, pol->handshake_timeout_ms) != 0){
      close(sock);
      SSL_CTX_free(ctx);
      tls_set_last_err((long long)net_last_error_impl());
      return NULL;
    }
  }

  SSL* ssl = SSL_new(ctx);
  if(!ssl){
    close(sock);
    SSL_CTX_free(ctx);
    tls_set_last_err((long long)ERR_get_error());
    return NULL;
  }
  SSL_set_fd(ssl, sock);
  if(host && host[0]) SSL_set_tlsext_host_name(ssl, host);
  if(SSL_connect(ssl) != 1){
    SSL_free(ssl);
    close(sock);
    SSL_CTX_free(ctx);
    tls_set_last_err((long long)ERR_get_error());
    return NULL;
  }

  tn_tls_ossl_vm* t = (tn_tls_ossl_vm*)xmalloc(sizeof(tn_tls_ossl_vm));
  t->sock = sock;
  t->ctx = ctx;
  t->ssl = ssl;
  if(tn_tls_verify_pin_ossl_vm(t, pol) != 0){
    tn_tls_free_ossl_vm(t);
    tls_set_last_err(-1);
    return NULL;
  }
  tls_set_last_err(0);
  return t;
}

static tn_tls_ossl_vm* tn_tls_connect_ossl_vm(const char* host, long long port){
  return tn_tls_connect_ossl_with_policy_vm(host, port, &g_tn_tls_policy_vm);
}

static long long tn_tls_send_ossl_vm(tn_tls_ossl_vm* t, const unsigned char* buf, long long len){
  if(!t || !buf || len <= 0) return 0;
  int n = SSL_write(t->ssl, buf, (int)len);
  if(n <= 0) tls_set_last_err((long long)ERR_get_error());
  else tls_set_last_err(0);
  return (long long)n;
}

static long long tn_tls_recv_ossl_vm(tn_tls_ossl_vm* t, unsigned char* buf, long long len){
  if(!t || !buf || len <= 0) return 0;
  int n = SSL_read(t->ssl, buf, (int)len);
  if(n <= 0) tls_set_last_err((long long)ERR_get_error());
  else tls_set_last_err(0);
  return (long long)n;
}

static void tn_tls_free_ossl_vm(tn_tls_ossl_vm* t){
  if(!t) return;
  if(t->ssl){
    SSL_shutdown(t->ssl);
    SSL_free(t->ssl);
  }
  if(t->sock >= 0) close(t->sock);
  if(t->ctx) SSL_CTX_free(t->ctx);
  free(t);
}
#endif
#ifdef _WIN32
  #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0601
  #endif
  #ifndef SECURITY_WIN32
    #define SECURITY_WIN32 1
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <winhttp.h>
  #include <security.h>
  #include <schannel.h>
  #include <wincrypt.h>
#else
  #include <sys/time.h>
#endif
#include <limits.h>
#include <time.h>
#if defined(TN_TLS_OPENSSL) && !defined(_WIN32)
  #include <openssl/ssl.h>
  #include <openssl/err.h>
#endif

#ifdef _WIN32
  #include <direct.h>
  #include <io.h>
#else
  #include <dirent.h>
  #include <unistd.h>
  #include <sys/socket.h>
  #include <netdb.h>
  #include <arpa/inet.h>
  #include <fcntl.h>
  #include <signal.h>
#endif

#ifndef TN_SEND_FLAGS
  #ifdef MSG_NOSIGNAL
    #define TN_SEND_FLAGS MSG_NOSIGNAL
  #else
    #define TN_SEND_FLAGS 0
  #endif
#endif

/* -------------------------------------------------
 * Helpers
 * ------------------------------------------------- */

static void* xmalloc(size_t n){
  void* p = malloc(n);
  if(!p) die("out of memory");
  return p;
}
static void* xrealloc(void* p, size_t n){
  void* q = realloc(p, n);
  if(!q) die("out of memory");
  return q;
}

static int tn_mul_overflow_size(long long a, long long b, size_t* out){
  if(a <= 0 || b <= 0){
    if(out) *out = 0;
    return 0;
  }
  unsigned long long ua = (unsigned long long)a;
  unsigned long long ub = (unsigned long long)b;
  if(ua > (unsigned long long)(SIZE_MAX / ub)) return 1;
  if(out) *out = (size_t)(ua * ub);
  return 0;
}

static int tn_mul_overflow_i64_pos(long long a, long long b, long long* out){
  if(a <= 0 || b <= 0){
    if(out) *out = 0;
    return 0;
  }
  if(a > (LLONG_MAX / b)) return 1;
  if(out) *out = a * b;
  return 0;
}

static int tn_ai_vm_validate_matmul_shape(long long m, long long n, long long k, long long elem_size){
  long long mn = 0;
  long long mk = 0;
  long long kn = 0;
  size_t bytes = 0;
  if(m <= 0 || n <= 0 || k <= 0 || elem_size <= 0) return 0;
  if(tn_mul_overflow_i64_pos(m, n, &mn)) return 0;
  if(tn_mul_overflow_i64_pos(m, k, &mk)) return 0;
  if(tn_mul_overflow_i64_pos(k, n, &kn)) return 0;
  if(tn_mul_overflow_size(mn, elem_size, &bytes)) return 0;
  if(tn_mul_overflow_size(mk, elem_size, &bytes)) return 0;
  if(tn_mul_overflow_size(kn, elem_size, &bytes)) return 0;
  return 1;
}

static int tn_ai_vm_validate_matmul_i8_f64_shape(long long m, long long n, long long k){
  long long mn = 0;
  long long mk = 0;
  long long kn = 0;
  size_t bytes = 0;
  if(m <= 0 || n <= 0 || k <= 0) return 0;
  if(tn_mul_overflow_i64_pos(m, n, &mn)) return 0;
  if(tn_mul_overflow_i64_pos(m, k, &mk)) return 0;
  if(tn_mul_overflow_i64_pos(k, n, &kn)) return 0;
  if(tn_mul_overflow_size(mn, (long long)sizeof(double), &bytes)) return 0;
  if(tn_mul_overflow_size(mk, (long long)sizeof(double), &bytes)) return 0;
  if(tn_mul_overflow_size(kn, (long long)sizeof(signed char), &bytes)) return 0;
  if(tn_mul_overflow_size(n, (long long)sizeof(double), &bytes)) return 0;
  return 1;
}

static int tn_ai_vm_validate_rows_cols_shape(long long rows, long long cols, long long elem_size){
  long long total = 0;
  size_t bytes = 0;
  if(rows <= 0 || cols <= 0 || elem_size <= 0) return 0;
  if(tn_mul_overflow_i64_pos(rows, cols, &total)) return 0;
  if(tn_mul_overflow_size(total, elem_size, &bytes)) return 0;
  if(tn_mul_overflow_size(cols, elem_size, &bytes)) return 0;
  return 1;
}

static double tn_ai_vm_norm_eps(double eps){
  if(eps != eps) return 1.0e-5;
  if(eps <= 0.0) return 1.0e-5;
  return eps;
}

static long long tn_ai_vm_matmul_f64(double* out, const double* a, const double* b,
                                     long long m, long long n, long long k){
  if(!out || !a || !b || !tn_ai_vm_validate_matmul_shape(m, n, k, (long long)sizeof(double))) return -1;
  for(long long row = 0; row < m; ++row){
    const double* arow = a + (row * k);
    double* outrow = out + (row * n);
    for(long long col = 0; col < n; ++col){
      double acc = 0.0;
      for(long long p = 0; p < k; ++p){
        acc += arow[p] * b[p * n + col];
      }
      outrow[col] = acc;
    }
  }
  return 0;
}

static long long tn_ai_vm_matmul_i8_f64(double* out, const double* a, const signed char* b,
                                        const double* scales, long long m, long long n, long long k){
  if(!out || !a || !b || !tn_ai_vm_validate_matmul_i8_f64_shape(m, n, k)) return -1;
  for(long long row = 0; row < m; ++row){
    const double* arow = a + (row * k);
    double* outrow = out + (row * n);
    for(long long col = 0; col < n; ++col){
      double scale = scales ? scales[col] : 1.0;
      if(scale != scale) scale = 0.0;
      double acc = 0.0;
      for(long long p = 0; p < k; ++p){
        acc += arow[p] * ((double)b[p * n + col] * scale);
      }
      outrow[col] = acc;
    }
  }
  return 0;
}

static long long tn_ai_vm_rmsnorm_rows_f64(double* out, const double* x, const double* weight,
                                           long long rows, long long cols, double eps){
  if(!out || !x || !tn_ai_vm_validate_rows_cols_shape(rows, cols, (long long)sizeof(double))) return -1;
  eps = tn_ai_vm_norm_eps(eps);
  for(long long r = 0; r < rows; ++r){
    const double* src = x + (r * cols);
    double* dst = out + (r * cols);
    double sum_sq = 0.0;
    for(long long c = 0; c < cols; ++c){
      double v = src[c];
      sum_sq += v * v;
    }
    double inv = 1.0 / sqrt((sum_sq / (double)cols) + eps);
    if(weight){
      for(long long c = 0; c < cols; ++c) dst[c] = src[c] * inv * weight[c];
    } else {
      for(long long c = 0; c < cols; ++c) dst[c] = src[c] * inv;
    }
  }
  return 0;
}

static long long tn_ai_vm_softmax_rows_f64(double* out, const double* x, long long rows, long long cols){
  if(!out || !x || !tn_ai_vm_validate_rows_cols_shape(rows, cols, (long long)sizeof(double))) return -1;
  for(long long r = 0; r < rows; ++r){
    const double* src = x + (r * cols);
    double* dst = out + (r * cols);
    double maxv = src[0];
    for(long long c = 1; c < cols; ++c){
      if(src[c] > maxv) maxv = src[c];
    }
    double sum = 0.0;
    for(long long c = 0; c < cols; ++c){
      double ev = exp(src[c] - maxv);
      dst[c] = ev;
      sum += ev;
    }
    if(sum <= 0.0 || sum != sum){
      double u = 1.0 / (double)cols;
      for(long long c = 0; c < cols; ++c) dst[c] = u;
    } else {
      double inv = 1.0 / sum;
      for(long long c = 0; c < cols; ++c) dst[c] *= inv;
    }
  }
  return 0;
}

static long long tn_ai_vm_rope_f64(double* q, double* k, long long pos, long long dim, double base){
  if(!q || !k || dim <= 0 || (dim & 1LL) != 0) return -1;
  if(pos == 0) return 0;
  if(base <= 1.0 || base != base) base = 10000.0;
  double log_base = log(base);
  for(long long i = 0; i < dim; i += 2){
    double freq = exp(-log_base * ((double)i / (double)dim));
    double angle = (double)pos * freq;
    double c = cos(angle);
    double s = sin(angle);
    double q0 = q[i];
    double q1 = q[i + 1];
    double k0 = k[i];
    double k1 = k[i + 1];
    q[i]     = q0 * c - q1 * s;
    q[i + 1] = q0 * s + q1 * c;
    k[i]     = k0 * c - k1 * s;
    k[i + 1] = k0 * s + k1 * c;
  }
  return 0;
}

static long long tn_popcnt64(unsigned long long v){
#if defined(__GNUC__) || defined(__clang__)
  return (long long)__builtin_popcountll(v);
#else
  long long c = 0;
  while(v){
    v &= (v - 1u);
    c++;
  }
  return c;
#endif
}

static long long tn_clz64(unsigned long long v){
  if(v == 0ull) return 64;
#if defined(__GNUC__) || defined(__clang__)
  return (long long)__builtin_clzll(v);
#else
  long long c = 0;
  unsigned long long m = 1ull << 63;
  while((v & m) == 0ull){
    c++;
    m >>= 1;
  }
  return c;
#endif
}

static long long tn_ctz64(unsigned long long v){
  if(v == 0ull) return 64;
#if defined(__GNUC__) || defined(__clang__)
  return (long long)__builtin_ctzll(v);
#else
  long long c = 0;
  while((v & 1ull) == 0ull){
    c++;
    v >>= 1;
  }
  return c;
#endif
}

static unsigned long long tn_bswap64(unsigned long long v){
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_bswap64(v);
#else
  return ((v & 0x00000000000000FFull) << 56) |
         ((v & 0x000000000000FF00ull) << 40) |
         ((v & 0x0000000000FF0000ull) << 24) |
         ((v & 0x00000000FF000000ull) << 8)  |
         ((v & 0x000000FF00000000ull) >> 8)  |
         ((v & 0x0000FF0000000000ull) >> 24) |
         ((v & 0x00FF000000000000ull) >> 40) |
         ((v & 0xFF00000000000000ull) >> 56);
#endif
}

static unsigned long long tn_rotl64(unsigned long long v, unsigned long long shift){
  unsigned int s = (unsigned int)(shift & 63ull);
  if(s == 0u) return v;
  return (v << s) | (v >> (64u - s));
}

static unsigned long long tn_rotr64(unsigned long long v, unsigned long long shift){
  unsigned int s = (unsigned int)(shift & 63ull);
  if(s == 0u) return v;
  return (v >> s) | (v << (64u - s));
}

static int tn_cpu_cpuid(unsigned int leaf, unsigned int subleaf, unsigned int* a, unsigned int* b, unsigned int* c, unsigned int* d){
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
  int regs[4] = {0,0,0,0};
  __cpuidex(regs, (int)leaf, (int)subleaf);
  if(a) *a = (unsigned int)regs[0];
  if(b) *b = (unsigned int)regs[1];
  if(c) *c = (unsigned int)regs[2];
  if(d) *d = (unsigned int)regs[3];
  return 1;
#elif TN_HAVE_CPUID_H
  unsigned int ra = 0, rb = 0, rc = 0, rd = 0;
  if(!__get_cpuid_count(leaf, subleaf, &ra, &rb, &rc, &rd)){
    if(a) *a = 0;
    if(b) *b = 0;
    if(c) *c = 0;
    if(d) *d = 0;
    return 0;
  }
  if(a) *a = ra;
  if(b) *b = rb;
  if(c) *c = rc;
  if(d) *d = rd;
  return 1;
#else
  if(a) *a = 0;
  if(b) *b = 0;
  if(c) *c = 0;
  if(d) *d = 0;
  (void)leaf;
  (void)subleaf;
  return 0;
#endif
}

static unsigned long long tn_cpu_xgetbv(unsigned int idx){
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
  return (unsigned long long)_xgetbv((unsigned int)idx);
#elif (defined(__i386__) || defined(__x86_64__)) && (defined(__GNUC__) || defined(__clang__))
  unsigned int eax = 0, edx = 0;
  __asm__ __volatile__(".byte 0x0f, 0x01, 0xd0"
                       : "=a"(eax), "=d"(edx)
                       : "c"(idx));
  return ((unsigned long long)edx << 32) | (unsigned long long)eax;
#else
  (void)idx;
  return 0ull;
#endif
}

static int tn_cpu_has_xsave_avx(unsigned int ecx_leaf1){
  if((ecx_leaf1 & (1u << 27)) == 0u) return 0; // OSXSAVE
  if((ecx_leaf1 & (1u << 28)) == 0u) return 0; // AVX
  unsigned long long xcr0 = tn_cpu_xgetbv(0);
  return ((xcr0 & 0x6ull) == 0x6ull) ? 1 : 0; // XMM + YMM state enabled
}

static int tn_cpu_has_sse2_impl(void){
#if defined(__x86_64__) || defined(_M_X64)
  return 1;
#elif defined(__i386__) || defined(_M_IX86)
  unsigned int a = 0, b = 0, c = 0, d = 0;
  if(!tn_cpu_cpuid(1u, 0u, &a, &b, &c, &d)) return 0;
  (void)a; (void)b; (void)c;
  return (d & (1u << 26)) ? 1 : 0;
#else
  return 0;
#endif
}

static int tn_cpu_has_avx2_impl(void){
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
  unsigned int a = 0, b = 0, c = 0, d = 0;
  if(!tn_cpu_cpuid(1u, 0u, &a, &b, &c, &d)) return 0;
  if(!tn_cpu_has_xsave_avx(c)) return 0;
  if(!tn_cpu_cpuid(7u, 0u, &a, &b, &c, &d)) return 0;
  (void)a; (void)c; (void)d;
  return (b & (1u << 5)) ? 1 : 0;
#else
  return 0;
#endif
}

static int tn_cpu_has_fma_impl(void){
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
  unsigned int a = 0, b = 0, c = 0, d = 0;
  if(!tn_cpu_cpuid(1u, 0u, &a, &b, &c, &d)) return 0;
  (void)a; (void)b; (void)d;
  if((c & (1u << 12)) == 0u) return 0; // FMA
  if(!tn_cpu_has_xsave_avx(c)) return 0;
  return 1;
#else
  return 0;
#endif
}

static int tn_cpu_has_neon_impl(void){
#if defined(__aarch64__) || defined(_M_ARM64)
  return 1;
#elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM)
  return 1;
#else
  return 0;
#endif
}

static char* dup_cstr(const char* s){
  if(!s) s = "";
  size_t n = strlen(s);
  char* p = (char*)xmalloc(n + 1);
  memcpy(p, s, n + 1);
  return p;
}

static int g_argc = 0;
static char** g_argv = NULL;
static unsigned char g_port8[65536];

#define TN_VM_EVENT_CAP 256
typedef struct tn_vm_event_slot {
  long long kind;
  long long key;
  long long x;
  long long y;
  long long buttons;
  long long mods;
  long long data;
} tn_vm_event_slot;

static tn_vm_event_slot g_tn_vm_event_q[TN_VM_EVENT_CAP];
static int g_tn_vm_event_head = 0;
static int g_tn_vm_event_tail = 0;
static int g_tn_vm_event_len = 0;
static int g_tn_vm_event_dropped = 0;

static long long tn_vm_event_queue_init(void){
  g_tn_vm_event_head = 0;
  g_tn_vm_event_tail = 0;
  g_tn_vm_event_len = 0;
  g_tn_vm_event_dropped = 0;
  return 0;
}

static long long tn_vm_event_queue_clear(void){
  g_tn_vm_event_head = 0;
  g_tn_vm_event_tail = 0;
  g_tn_vm_event_len = 0;
  return 0;
}

static long long tn_vm_event_push(long long kind,
                                  long long key,
                                  long long x,
                                  long long y,
                                  long long buttons,
                                  long long mods,
                                  long long data){
  tn_vm_event_slot* slot = NULL;
  if(g_tn_vm_event_len >= TN_VM_EVENT_CAP){
    g_tn_vm_event_dropped++;
    return -1;
  }
  slot = &g_tn_vm_event_q[g_tn_vm_event_tail];
  slot->kind = kind;
  slot->key = key;
  slot->x = x;
  slot->y = y;
  slot->buttons = buttons;
  slot->mods = mods;
  slot->data = data;
  g_tn_vm_event_tail = (g_tn_vm_event_tail + 1) % TN_VM_EVENT_CAP;
  g_tn_vm_event_len++;
  return 1;
}

static long long tn_vm_event_pop(long long* out_fields, int peek_only){
  tn_vm_event_slot* slot = NULL;
  if(g_tn_vm_event_len <= 0) return 0;
  slot = &g_tn_vm_event_q[g_tn_vm_event_head];
  if(out_fields){
    out_fields[0] = slot->kind;
    out_fields[1] = slot->key;
    out_fields[2] = slot->x;
    out_fields[3] = slot->y;
    out_fields[4] = slot->buttons;
    out_fields[5] = slot->mods;
    out_fields[6] = slot->data;
  }
  if(!peek_only){
    g_tn_vm_event_head = (g_tn_vm_event_head + 1) % TN_VM_EVENT_CAP;
    g_tn_vm_event_len--;
  }
  return 1;
}

void stdlib_set_args(int argc, char** argv){
  g_argc = argc;
  g_argv = argv;
}

/* Forward decl */
static void say_print_inline(Value v);

// ANSI color helpers (enabled via TN_COLOR or TTY auto-detect)
#define TN_CLR_RESET "\x1b[0m"
#define TN_CLR_INT   "\x1b[38;5;76m"
#define TN_CLR_FLOAT "\x1b[38;5;81m"
#define TN_CLR_STR   "\x1b[38;5;213m"
#define TN_CLR_NULL  "\x1b[38;5;203m"
#define TN_CLR_PTR   "\x1b[38;5;220m"

static int tn_color_enabled(void){
  static int inited = 0;
  static int enabled = 0;
  if(inited) return enabled;
  inited = 1;

  const char* env = getenv("TN_COLOR");
  if(env && env[0]){
    char c = env[0];
    if(c=='0' || c=='n' || c=='N' || c=='f' || c=='F' || c=='o' || c=='O'){
      enabled = 0;
      return enabled;
    }
    if(c=='1' || c=='y' || c=='Y' || c=='t' || c=='T'){
      enabled = 1;
      return enabled;
    }
  }
#ifdef _WIN32
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if(h == INVALID_HANDLE_VALUE) return 0;
  DWORD mode = 0;
  if(!GetConsoleMode(h, &mode)) return 0;
  mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
  SetConsoleMode(h, mode);
  enabled = 1;
#else
  enabled = isatty(fileno(stdout)) ? 1 : 0;
#endif
  return enabled;
}

static void tn_color_begin(const char* code){
  if(tn_color_enabled()) fputs(code, stdout);
}
static void tn_color_end(void){
  if(tn_color_enabled()) fputs(TN_CLR_RESET, stdout);
}

#ifdef _WIN32
static wchar_t* utf8_to_wide(const char* s){
  if(!s) s = "";
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  if(n <= 0) return NULL;
  wchar_t* w = (wchar_t*)xmalloc(sizeof(wchar_t) * (size_t)n);
  MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
  return w;
}

static int http_download_winhttp(const char* url, const char* out_path){
  if(!url || !out_path) return -1;
  wchar_t* wurl = utf8_to_wide(url);
  if(!wurl) return -1;

  URL_COMPONENTS uc;
  memset(&uc, 0, sizeof(uc));
  uc.dwStructSize = sizeof(uc);
  uc.dwSchemeLength = (DWORD)-1;
  uc.dwHostNameLength = (DWORD)-1;
  uc.dwUrlPathLength = (DWORD)-1;
  uc.dwExtraInfoLength = (DWORD)-1;

  if(!WinHttpCrackUrl(wurl, 0, 0, &uc)){
    free(wurl);
    return -2;
  }

  int https = (uc.nScheme == INTERNET_SCHEME_HTTPS);

  DWORD host_len = uc.dwHostNameLength;
  wchar_t* host = (wchar_t*)xmalloc(sizeof(wchar_t) * (size_t)(host_len + 1));
  wcsncpy(host, uc.lpszHostName, host_len);
  host[host_len] = 0;

  DWORD path_len = uc.dwUrlPathLength + uc.dwExtraInfoLength;
  wchar_t* path = NULL;
  if(path_len == 0){
    path = utf8_to_wide("/");
  } else {
    path = (wchar_t*)xmalloc(sizeof(wchar_t) * (size_t)(path_len + 1));
    if(uc.dwUrlPathLength) wcsncpy(path, uc.lpszUrlPath, uc.dwUrlPathLength);
    if(uc.dwExtraInfoLength) wcsncpy(path + uc.dwUrlPathLength, uc.lpszExtraInfo, uc.dwExtraInfoLength);
    path[path_len] = 0;
  }

  HINTERNET hSession = WinHttpOpen(L"TezzNative/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if(!hSession){
    free(wurl); free(host); free(path);
    return -3;
  }

  HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
  if(!hConnect){
    WinHttpCloseHandle(hSession);
    free(wurl); free(host); free(path);
    return -4;
  }

  DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if(!hRequest){
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    free(wurl); free(host); free(path);
    return -5;
  }

  BOOL bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                     WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
  if(bResults) bResults = WinHttpReceiveResponse(hRequest, NULL);
  if(!bResults){
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    free(wurl); free(host); free(path);
    return -6;
  }

  FILE* f = fopen(out_path, "wb");
  if(!f){
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    free(wurl); free(host); free(path);
    return -7;
  }

  for(;;){
    DWORD avail = 0;
    if(!WinHttpQueryDataAvailable(hRequest, &avail)) break;
    if(avail == 0) break;
    char* buf = (char*)xmalloc(avail);
    DWORD read = 0;
    if(!WinHttpReadData(hRequest, buf, avail, &read)){
      free(buf);
      break;
    }
    if(read > 0) fwrite(buf, 1, read, f);
    free(buf);
  }

  fclose(f);
  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);
  free(wurl); free(host); free(path);
  return 0;
}
#endif

/* Print a u8 array as string (stop at 0 or len) */
static void print_u8_array_inline(void* addr, long long n){
  if(!addr){ fputs("null", stdout); return; }
  unsigned char* p = (unsigned char*)addr;
  long long i=0;
  while(i<n && p[i]!=0) i++;
  fwrite(p, 1, (size_t)i, stdout);
}

/* Print i64 array inline */
static void print_i64_array_inline(void* addr, long long n){
  if(!addr){ fputs("null", stdout); return; }
  long long* p = (long long*)addr;
  fputc('[', stdout);
  for(long long i=0;i<n;i++){
    if(i) fputs(", ", stdout);
    Value v; v.k=V_I64; v.i=p[i]; v.p=NULL; v.ty=NULL;
    say_print_inline(v);
  }
  fputc(']', stdout);
}

/* Print f64 array inline */
static void print_f64_array_inline(void* addr, long long n){
  if(!addr){ fputs("null", stdout); return; }
  double* p = (double*)addr;
  fputc('[', stdout);
  for(long long i=0;i<n;i++){
    if(i) fputs(", ", stdout);
    Value v; v.k=V_F64; v.f=p[i]; v.i=0; v.p=NULL; v.ty=NULL;
    say_print_inline(v);
  }
  fputc(']', stdout);
}

/* For "pointer that is actually a C string" */
static void print_cstr_inline(const char* s){
  if(!s) s="";
  fputs(s, stdout);
}

static long long tn_ftell(FILE* f){
  return (long long)ftell(f);
}

static int tn_fseek(FILE* f, long long off, int whence){
  return fseek(f, (long)off, whence);
}

static char* path_join_impl(const char* a, const char* b);
static char* path_normalize_opts_impl(const char* path, long long flags);
static long long time_now_ns(void);
static char* date_now_impl(int utc);

#ifndef TN_SOCK_T_DEFINED
#ifdef _WIN32
typedef SOCKET tn_sock_t;
#define TN_INVALID_SOCKET INVALID_SOCKET
#else
typedef int tn_sock_t;
#define TN_INVALID_SOCKET (-1)
#endif
#define TN_SOCK_T_DEFINED 1
#endif

static int net_inited = 0;
static long long net_last_err = 0;
static long long tls_last_err = 0;
#ifndef _WIN32
static int net_sigpipe_ignored = 0;
#endif

static void net_set_last_err(long long v){ net_last_err = v; }
#if defined(_WIN32) || defined(TN_TLS_OPENSSL)
static void tls_set_last_err(long long v){ tls_last_err = v; }
#endif

static char* path_join_impl(const char* a, const char* b);

typedef struct {
  char* name;
  int is_dir;
} DirEntry;

static int dir_entry_cmp(const void* a, const void* b){
  const DirEntry* ea = (const DirEntry*)a;
  const DirEntry* eb = (const DirEntry*)b;
  const char* an = (ea && ea->name) ? ea->name : "";
  const char* bn = (eb && eb->name) ? eb->name : "";
  return strcmp(an, bn);
}

static void dir_entries_free(DirEntry* entries, size_t count){
  if(!entries) return;
  for(size_t i = 0; i < count; i++){
    free(entries[i].name);
  }
  free(entries);
}

static void dir_entry_push(DirEntry** entries, size_t* count, size_t* cap,
                           const char* name, int is_dir){
  if(!entries || !count || !cap || !name) return;
  if(*count >= *cap){
    size_t next_cap = (*cap == 0) ? 16 : (*cap * 2);
    *entries = (DirEntry*)xrealloc(*entries, next_cap * sizeof(DirEntry));
    *cap = next_cap;
  }
  (*entries)[*count].name = dup_cstr(name);
  (*entries)[*count].is_dir = is_dir ? 1 : 0;
  *count += 1;
}

static int collect_dir_entries(const char* path, DirEntry** out_entries, size_t* out_count){
  if(!out_entries || !out_count) return -1;
  *out_entries = NULL;
  *out_count = 0;
  size_t cap = 0;
  DirEntry* entries = NULL;

#ifdef _WIN32
  char pat[MAX_PATH];
  const char* p = (path && path[0]) ? path : ".";
  size_t pl = strlen(p);
  if(pl + 3 >= sizeof(pat)) return -1;
  snprintf(pat, sizeof(pat), "%s\\*", p);
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pat, &fd);
  if(h == INVALID_HANDLE_VALUE) return -1;
  do{
    const char* n = fd.cFileName;
    if(strcmp(n, ".")==0 || strcmp(n, "..")==0) continue;
    dir_entry_push(&entries, out_count, &cap, n,
                   (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
  } while(FindNextFileA(h, &fd));
  FindClose(h);
#else
  const char* p = (path && path[0]) ? path : ".";
  DIR* d = opendir(p);
  if(!d) return -1;
  struct dirent* de;
  while((de = readdir(d)) != NULL){
    const char* n = de->d_name;
    if(strcmp(n, ".")==0 || strcmp(n, "..")==0) continue;
    int is_dir = 0;
    char* full = path_join_impl(p, n);
    if(full){
      struct stat st;
      if(stat(full, &st)==0 && S_ISDIR(st.st_mode)){
        is_dir = 1;
      }
      free(full);
    }
    dir_entry_push(&entries, out_count, &cap, n, is_dir);
  }
  closedir(d);
#endif

  if(*out_count > 1){
    qsort(entries, *out_count, sizeof(DirEntry), dir_entry_cmp);
  }
  *out_entries = entries;
  return 0;
}

static void append_bytes(char** buf, size_t* len, size_t* cap, const char* s, size_t n){
  if(*cap == 0){
    *cap = 128;
    *buf = (char*)xmalloc(*cap);
  }
  while(*len + n + 1 > *cap){
    *cap *= 2;
    *buf = (char*)realloc(*buf, *cap);
    if(!*buf) die("out of memory");
  }
  memcpy(*buf + *len, s, n);
  *len += n;
  (*buf)[*len] = 0;
}

static char* list_dir_impl(const char* path){
  char* out = NULL;
  size_t len = 0, cap = 0;
  DirEntry* entries = NULL;
  size_t count = 0;

  if(collect_dir_entries(path, &entries, &count) != 0){
    return dup_cstr("");
  }

  for(size_t i = 0; i < count; i++){
    const char* n = entries[i].name ? entries[i].name : "";
    if(n[0] == 0) continue;
    append_bytes(&out, &len, &cap, n, strlen(n));
    append_bytes(&out, &len, &cap, "\n", 1);
  }
  dir_entries_free(entries, count);

  if(!out) return dup_cstr("");
  if(len > 0 && out[len-1] == '\n') out[len-1] = 0;
  return out;
}

static void list_dir_recursive_walk(const char* base, char** out, size_t* len, size_t* cap){
  DirEntry* entries = NULL;
  size_t count = 0;
  if(collect_dir_entries(base, &entries, &count) != 0){
    return;
  }

  for(size_t i = 0; i < count; i++){
    const char* n = entries[i].name ? entries[i].name : "";
    if(n[0] == 0) continue;
    char* full = path_join_impl(base, n);
    if(full){
      append_bytes(out, len, cap, full, strlen(full));
      append_bytes(out, len, cap, "\n", 1);
      if(entries[i].is_dir){
        list_dir_recursive_walk(full, out, len, cap);
      }
      free(full);
    }
  }
  dir_entries_free(entries, count);
}

static char* list_dir_recursive_impl(const char* path){
  char* out = NULL;
  size_t len = 0, cap = 0;
  list_dir_recursive_walk(path ? path : ".", &out, &len, &cap);
  if(!out) return dup_cstr("");
  if(len > 0 && out[len-1] == '\n') out[len-1] = 0;
  return out;
}

static int match_glob(const char* pat, const char* s){
  if(!pat || !s) return 0;
  while(*pat){
    if(*pat=='*'){
      while(pat[1]=='*') pat++;
      pat++;
      if(!*pat) return 1;
      while(*s){
        if(match_glob(pat, s)) return 1;
        s++;
      }
      return 0;
    }
    if(*pat=='?'){
      if(!*s) return 0;
      pat++; s++;
      continue;
    }
    if(*pat!=*s) return 0;
    pat++; s++;
  }
  return *s==0;
}

static char* glob_impl(const char* pattern){
  if(!pattern) return dup_cstr("");
  const char* last = pattern;
  for(const char* p=pattern; *p; p++){
    if(*p=='/' || *p=='\\') last=p;
  }
  char* dir = NULL;
  const char* pat = pattern;
  if(last != pattern){
    size_t n = (size_t)(last - pattern);
    dir = (char*)xmalloc(n + 1);
    memcpy(dir, pattern, n);
    dir[n] = 0;
    pat = last + 1;
  } else {
    dir = dup_cstr(".");
  }

  char* listing = list_dir_impl(dir);
  if(!listing){
    free(dir);
    return dup_cstr("");
  }

  char* out = NULL;
  size_t len = 0, cap = 0;
  char* cur = listing;
  while(*cur){
    char* line = cur;
    while(*cur && *cur!='\n') cur++;
    size_t ln = (size_t)(cur - line);
    if(*cur=='\n') *cur++ = 0;
    if(ln > 0 && match_glob(pat, line)){
      char* full = path_join_impl(dir, line);
      append_bytes(&out, &len, &cap, full, strlen(full));
      append_bytes(&out, &len, &cap, "\n", 1);
      free(full);
    }
  }
  free(listing);
  free(dir);

  if(!out) return dup_cstr("");
  if(len > 0 && out[len-1] == '\n') out[len-1] = 0;
  return out;
}

static char* path_normalize_impl(const char* path){
  return path_normalize_opts_impl(path, 0);
}

static char* path_resolve_impl(const char* path){
  if(!path) return NULL;
#ifdef _WIN32
  char buf[MAX_PATH];
  if(_fullpath(buf, path, MAX_PATH)) return dup_cstr(buf);
  return NULL;
#else
  char* rp = realpath(path, NULL);
  if(!rp) return NULL;
  char* out = dup_cstr(rp);
  free(rp);
  return out;
#endif
}

static char pick_sep_from_path(const char* path, char def){
  if(!path) return def;
  for(const char* p=path; *p; p++){
    if(*p=='/' || *p=='\\') return *p;
  }
  return def;
}

static char* path_normalize_opts_impl(const char* inpath, long long flags){
  if(!inpath) return dup_cstr("");

  char* resolved = NULL;
  if(flags & 4){
    resolved = path_resolve_impl(inpath);
  }
  const char* path = resolved ? resolved : inpath;

  const char* p = path;
#ifdef _WIN32
  char drive[3] = {0,0,0};
  int has_drive = 0;
  if(isalpha((unsigned char)p[0]) && p[1]==':'){
    drive[0]=p[0]; drive[1]=':'; drive[2]=0;
    has_drive = 1;
    p += 2;
  }
#endif
  int absolute = (*p=='/' || *p=='\\');
  while(*p=='/' || *p=='\\') p++;

#ifdef _WIN32
  char def_sep = '\\';
#else
  char def_sep = '/';
#endif
  char sep = (flags & 1) ? pick_sep_from_path(path, def_sep) : def_sep;

  char** segs = NULL;
  int n = 0, cap = 0;
  while(*p){
    const char* s = p;
    while(*p && *p!='/' && *p!='\\') p++;
    int slen = (int)(p - s);
    while(*p=='/' || *p=='\\') p++;
    if(slen==0) continue;
    if(slen==1 && s[0]=='.') continue;
    if(slen==2 && s[0]=='.' && s[1]=='.'){
      if(n>0 && strcmp(segs[n-1], "..")!=0){
        free(segs[--n]);
        continue;
      }
      if(!absolute){
        if(n==cap){ cap = cap? cap*2:8; segs=(char**)realloc(segs,sizeof(char*)*(size_t)cap); }
        segs[n++] = dup_cstr("..");
      }
      continue;
    }
    if(n==cap){ cap = cap? cap*2:8; segs=(char**)realloc(segs,sizeof(char*)*(size_t)cap); }
    char* part = (char*)xmalloc((size_t)slen + 1);
    memcpy(part, s, (size_t)slen);
    part[slen]=0;
    segs[n++] = part;
  }

  if(n==0 && !absolute){
#ifdef _WIN32
    if(has_drive){
      char* out = (char*)xmalloc(3);
      out[0]=drive[0]; out[1]=':'; out[2]=0;
      if(flags & 2){
        for(int i=0;i<2;i++) out[i]=(char)tolower((unsigned char)out[i]);
      }
      free(resolved);
      return out;
    }
#endif
    char* dot = dup_cstr(".");
    free(resolved);
    return dot;
  }

  size_t outlen = 0;
#ifdef _WIN32
  if(has_drive) outlen += 2;
#endif
  if(absolute) outlen += 1;
  for(int i=0;i<n;i++) outlen += strlen(segs[i]) + (i>0 ? 1 : 0);

  char* out = (char*)xmalloc(outlen + 1);
  size_t pos = 0;
#ifdef _WIN32
  if(has_drive){ out[pos++]=drive[0]; out[pos++]=':'; }
#endif
  if(absolute) out[pos++]=sep;
  for(int i=0;i<n;i++){
    if(i>0) out[pos++] = sep;
    size_t sl = strlen(segs[i]);
    memcpy(out + pos, segs[i], sl);
    pos += sl;
    free(segs[i]);
  }
  free(segs);
  out[pos]=0;
  if(flags & 2){
    for(size_t i=0;i<pos;i++){
      out[i]=(char)tolower((unsigned char)out[i]);
    }
  }
  free(resolved);
  return out;
}

static long long time_now_ns(void){
#ifdef _WIN32
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
  // FILETIME is 100ns since 1601-01-01
  const unsigned long long EPOCH_DIFF = 116444736000000000ULL;
  if(t < EPOCH_DIFF) return 0;
  return (long long)((t - EPOCH_DIFF) * 100ULL);
#else
  struct timeval tv;
  if(gettimeofday(&tv, NULL) != 0){
    return (long long)time(NULL) * 1000000000LL;
  }
  return (long long)tv.tv_sec * 1000000000LL + (long long)tv.tv_usec * 1000LL;
#endif
}

static char* date_now_impl(int utc){
  time_t t = time(NULL);
  struct tm tmv;
#ifdef _WIN32
  struct tm* ptm = utc ? gmtime(&t) : localtime(&t);
  if(!ptm) return dup_cstr("");
  tmv = *ptm;
#else
  struct tm* ptm = utc ? gmtime_r(&t, &tmv) : localtime_r(&t, &tmv);
  if(!ptm) return dup_cstr("");
#endif
  char buf[64];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
           tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
           tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  return dup_cstr(buf);
}

static int net_init_impl(void){
#ifdef _WIN32
  if(net_inited) return 0;
  WSADATA wsa;
  int r = WSAStartup(MAKEWORD(2,2), &wsa);
  if(r == 0){
    net_inited = 1;
    net_set_last_err(0);
    return 0;
  }
  net_set_last_err((long long)WSAGetLastError());
  return -1;
#else
  if(!net_sigpipe_ignored){
    signal(SIGPIPE, SIG_IGN);
    net_sigpipe_ignored = 1;
  }
  (void)net_inited;
  net_set_last_err(0);
  return 0;
#endif
}

static int net_cleanup_impl(void){
#ifdef _WIN32
  if(net_inited){
    WSACleanup();
    net_inited = 0;
  }
  net_set_last_err(0);
  return 0;
#else
  net_set_last_err(0);
  return 0;
#endif
}

static long long net_last_error_impl(void){
  if(net_last_err != 0) return net_last_err;
#ifdef _WIN32
  return (long long)WSAGetLastError();
#else
  return (long long)errno;
#endif
}

static long long tls_last_error_impl(void){
  if(tls_last_err != 0) return tls_last_err;
  return net_last_error_impl();
}

static tn_sock_t net_sock_from_ll(long long v){
  return (tn_sock_t)(intptr_t)v;
}

static long long net_sock_to_ll(tn_sock_t s){
  return (long long)(intptr_t)s;
}

static int net_set_blocking_impl(tn_sock_t s, int on){
#ifdef _WIN32
  u_long mode = on ? 0UL : 1UL;
  if(ioctlsocket(s, FIONBIO, &mode) == 0){
    net_set_last_err(0);
    return 0;
  }
  net_set_last_err((long long)WSAGetLastError());
  return -1;
#else
  int flags = fcntl(s, F_GETFL, 0);
  if(flags < 0){ net_set_last_err((long long)errno); return -1; }
  if(on) flags &= ~O_NONBLOCK;
  else flags |= O_NONBLOCK;
  if(fcntl(s, F_SETFL, flags) == 0){
    net_set_last_err(0);
    return 0;
  }
  net_set_last_err((long long)errno);
  return -1;
#endif
}

static int net_set_timeout_impl(tn_sock_t s, long long ms){
  if(ms < 0) ms = 0;
#ifdef _WIN32
  DWORD tv = (DWORD)ms;
  if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)) != 0){
    net_set_last_err((long long)WSAGetLastError());
    return -1;
  }
  if(setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv)) != 0){
    net_set_last_err((long long)WSAGetLastError());
    return -1;
  }
  net_set_last_err(0);
  return 0;
#else
  struct timeval tv;
  tv.tv_sec = (time_t)(ms / 1000);
  tv.tv_usec = (suseconds_t)((ms % 1000) * 1000);
  if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0){
    net_set_last_err((long long)errno);
    return -1;
  }
  if(setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0){
    net_set_last_err((long long)errno);
    return -1;
  }
  net_set_last_err(0);
  return 0;
#endif
}

static int net_connect_impl(tn_sock_t s, const char* host, long long port){
  if(net_init_impl() != 0) return -1;
  if(s == TN_INVALID_SOCKET){ net_set_last_err((long long)errno); return -1; }
  char portbuf[16];
  snprintf(portbuf, sizeof(portbuf), "%lld", port);
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = 0;
  hints.ai_protocol = 0;
  struct addrinfo* res = NULL;
  int rc = getaddrinfo((host && host[0]) ? host : "127.0.0.1", portbuf, &hints, &res);
  if(rc != 0 || !res){
    net_set_last_err((long long)rc);
    return -1;
  }
  int ok = -1;
  for(struct addrinfo* ai = res; ai; ai = ai->ai_next){
    if(connect(s, ai->ai_addr, (int)ai->ai_addrlen) == 0){
      ok = 0;
      break;
    }
  }
  freeaddrinfo(res);
  if(ok == 0) net_set_last_err(0);
  else net_set_last_err((long long)net_last_error_impl());
  return ok;
}

static int net_bind_impl(tn_sock_t s, const char* host, long long port){
  if(net_init_impl() != 0) return -1;
  if(s == TN_INVALID_SOCKET){ net_set_last_err((long long)errno); return -1; }
  char portbuf[16];
  snprintf(portbuf, sizeof(portbuf), "%lld", port);
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = 0;
  hints.ai_protocol = 0;
  if(!host || !host[0]) hints.ai_flags = AI_PASSIVE;
  struct addrinfo* res = NULL;
  int rc = getaddrinfo((host && host[0]) ? host : NULL, portbuf, &hints, &res);
  if(rc != 0 || !res){
    net_set_last_err((long long)rc);
    return -1;
  }
  int ok = -1;
  for(struct addrinfo* ai = res; ai; ai = ai->ai_next){
    if(bind(s, ai->ai_addr, (int)ai->ai_addrlen) == 0){
      ok = 0;
      break;
    }
  }
  freeaddrinfo(res);
  if(ok == 0) net_set_last_err(0);
  else net_set_last_err((long long)net_last_error_impl());
  return ok;
}

static char* net_resolve_impl(const char* host, long long port){
  if(net_init_impl() != 0){
    net_set_last_err((long long)net_last_error_impl());
    return dup_cstr("");
  }
  char portbuf[16];
  snprintf(portbuf, sizeof(portbuf), "%lld", port);
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = 0;
  hints.ai_protocol = 0;
  struct addrinfo* res = NULL;
  int rc = getaddrinfo((host && host[0]) ? host : "127.0.0.1", portbuf, &hints, &res);
  if(rc != 0 || !res){
    net_set_last_err((long long)rc);
    return dup_cstr("");
  }
  char* out = NULL;
  size_t len = 0, cap = 0;
  for(struct addrinfo* ai = res; ai; ai = ai->ai_next){
    char hbuf[NI_MAXHOST];
    if(getnameinfo(ai->ai_addr, (socklen_t)ai->ai_addrlen, hbuf, sizeof(hbuf), NULL, 0, NI_NUMERICHOST) == 0){
      append_bytes(&out, &len, &cap, hbuf, strlen(hbuf));
      append_bytes(&out, &len, &cap, "\n", 1);
    }
  }
  freeaddrinfo(res);
  net_set_last_err(0);
  if(!out) return dup_cstr("");
  if(len > 0 && out[len-1] == '\n') out[len-1] = 0;
  return out;
}

#ifdef _WIN32
typedef struct tn_tls_vm {
  SOCKET sock;
  CredHandle cred;
  CtxtHandle ctx;
  int have_ctx;
  SecPkgContext_StreamSizes sizes;
  int sizes_ok;
  unsigned char* inbuf;
  int in_len;
  int in_cap;
  unsigned char* plain;
  int plain_len;
  int plain_pos;
} tn_tls_vm;

static int tn_tls_verify_pin_win_vm(tn_tls_vm* t, const tn_tls_policy_cfg_vm* policy){
  const tn_tls_policy_cfg_vm* pol = policy ? policy : &g_tn_tls_policy_vm;
  if(!t) return -1;
  if(pol->pin_enabled == 0) return 0;
  PCCERT_CONTEXT cert = NULL;
  SECURITY_STATUS st = QueryContextAttributes(&t->ctx, SECPKG_ATTR_REMOTE_CERT_CONTEXT, (PVOID)&cert);
  if(st != SEC_E_OK || cert == NULL){
    tls_set_last_err((long long)st);
    return -1;
  }
  BYTE hash[64];
  DWORD hash_len = (DWORD)sizeof(hash);
  BOOL ok = CertGetCertificateContextProperty(cert, CERT_SHA256_HASH_PROP_ID, hash, &hash_len);
  CertFreeCertificateContext(cert);
  if(!ok || hash_len != 32){
    tls_set_last_err(-1);
    return -1;
  }
  if(memcmp(hash, pol->pin_sha256, 32) != 0){
    tls_set_last_err(-1);
    return -1;
  }
  return 0;
}

static int tn_tls_grow_inbuf_vm(tn_tls_vm* t){
  if(!t) return -1;
  if(t->in_cap <= 0) return -1;
  if(t->in_cap > INT_MAX / 2) return -1;
  t->in_cap *= 2;
  t->inbuf = (unsigned char*)xrealloc(t->inbuf, (size_t)t->in_cap);
  return 0;
}

static SOCKET tn_tcp_connect_vm(const char* host, long long port){
  if(net_init_impl() != 0) return INVALID_SOCKET;
  char portbuf[16];
  snprintf(portbuf, sizeof(portbuf), "%lld", port);
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  struct addrinfo* res = NULL;
  int rc = getaddrinfo((host && host[0]) ? host : "127.0.0.1", portbuf, &hints, &res);
  if(rc != 0 || !res) return INVALID_SOCKET;
  SOCKET s = INVALID_SOCKET;
  for(struct addrinfo* ai = res; ai; ai = ai->ai_next){
    SOCKET sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if(sock == INVALID_SOCKET) continue;
    if(connect(sock, ai->ai_addr, (int)ai->ai_addrlen) == 0){
      s = sock;
      break;
    }
    closesocket(sock);
  }
  freeaddrinfo(res);
  return s;
}

static int tn_tls_handshake_vm(tn_tls_vm* t, const char* host, const tn_tls_policy_cfg_vm* policy){
  const tn_tls_policy_cfg_vm* pol = policy ? policy : &g_tn_tls_policy_vm;
  SCHANNEL_CRED sc;
  memset(&sc, 0, sizeof(sc));
  sc.dwVersion = SCHANNEL_CRED_VERSION;
  if(pol->min_version == 13){
#ifdef SP_PROT_TLS1_3_CLIENT
    sc.grbitEnabledProtocols = SP_PROT_TLS1_3_CLIENT;
#else
    tls_set_last_err(-1);
    return -1;
#endif
  } else {
    sc.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
#ifdef SP_PROT_TLS1_3_CLIENT
    sc.grbitEnabledProtocols |= SP_PROT_TLS1_3_CLIENT;
#endif
  }
  sc.dwFlags = SCH_CRED_AUTO_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS;

  TimeStamp ts;
  SECURITY_STATUS st = AcquireCredentialsHandleA(NULL, (SEC_CHAR*)UNISP_NAME_A,
                                                 SECPKG_CRED_OUTBOUND, NULL,
                                                 &sc, NULL, NULL, &t->cred, &ts);
  if(st != SEC_E_OK){
    tls_set_last_err((long long)st);
    return -1;
  }

  DWORD flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
                ISC_REQ_CONFIDENTIALITY | ISC_REQ_STREAM |
                ISC_REQ_ALLOCATE_MEMORY;
  DWORD out_flags = 0;

  t->in_cap = 64 * 1024;
  t->inbuf = (unsigned char*)xmalloc((size_t)t->in_cap);
  t->in_len = 0;

  for(;;){
    SecBuffer inBufs[2];
    SecBufferDesc inDesc;
    inBufs[0].pvBuffer = t->inbuf;
    inBufs[0].cbBuffer = (ULONG)t->in_len;
    inBufs[0].BufferType = SECBUFFER_TOKEN;
    inBufs[1].pvBuffer = NULL;
    inBufs[1].cbBuffer = 0;
    inBufs[1].BufferType = SECBUFFER_EMPTY;
    inDesc.ulVersion = SECBUFFER_VERSION;
    inDesc.cBuffers = 2;
    inDesc.pBuffers = inBufs;

    SecBuffer outBuf;
    SecBufferDesc outDesc;
    outBuf.pvBuffer = NULL;
    outBuf.cbBuffer = 0;
    outBuf.BufferType = SECBUFFER_TOKEN;
    outDesc.ulVersion = SECBUFFER_VERSION;
    outDesc.cBuffers = 1;
    outDesc.pBuffers = &outBuf;

    st = InitializeSecurityContextA(&t->cred,
                                    t->have_ctx ? &t->ctx : NULL,
                                    (SEC_CHAR*)(host ? host : ""),
                                    flags, 0, 0,
                                    (t->have_ctx || t->in_len > 0) ? &inDesc : NULL,
                                    0, &t->ctx, &outDesc, &out_flags, &ts);
    t->have_ctx = 1;

    if(outBuf.pvBuffer && outBuf.cbBuffer > 0){
      send(t->sock, (const char*)outBuf.pvBuffer, (int)outBuf.cbBuffer, TN_SEND_FLAGS);
      FreeContextBuffer(outBuf.pvBuffer);
    }

    if(st == SEC_E_OK){
      if(inBufs[1].BufferType == SECBUFFER_EXTRA){
        int extra = (int)inBufs[1].cbBuffer;
        memmove(t->inbuf, t->inbuf + (t->in_len - extra), (size_t)extra);
        t->in_len = extra;
      } else {
        t->in_len = 0;
      }
      break;
    }

    if(st == SEC_I_CONTINUE_NEEDED || st == SEC_E_INCOMPLETE_MESSAGE){
      if(st != SEC_E_INCOMPLETE_MESSAGE){
        if(inBufs[1].BufferType == SECBUFFER_EXTRA){
          int extra = (int)inBufs[1].cbBuffer;
          memmove(t->inbuf, t->inbuf + (t->in_len - extra), (size_t)extra);
          t->in_len = extra;
        } else {
          t->in_len = 0;
        }
      }
      if(t->in_len >= t->in_cap){
        if(tn_tls_grow_inbuf_vm(t) != 0){
          tls_set_last_err((long long)net_last_error_impl());
          return -1;
        }
      }
      int n = recv(t->sock, (char*)t->inbuf + t->in_len, t->in_cap - t->in_len, 0);
      if(n <= 0){
        tls_set_last_err((long long)net_last_error_impl());
        return -1;
      }
      t->in_len += n;
      continue;
    }

    tls_set_last_err((long long)st);
    return -1;
  }

  if(QueryContextAttributes(&t->ctx, SECPKG_ATTR_STREAM_SIZES, &t->sizes) != SEC_E_OK){
    tls_set_last_err((long long)SEC_E_INTERNAL_ERROR);
    return -1;
  }
  t->sizes_ok = 1;
  tls_set_last_err(0);
  return 0;
}

static void tn_tls_free_vm(tn_tls_vm* t){
  if(!t) return;
  if(t->have_ctx) DeleteSecurityContext(&t->ctx);
  FreeCredentialHandle(&t->cred);
  if(t->sock != INVALID_SOCKET) closesocket(t->sock);
  if(t->inbuf) free(t->inbuf);
  if(t->plain) free(t->plain);
  free(t);
}

static tn_tls_vm* tn_tls_connect_with_policy_vm(const char* host, long long port, const tn_tls_policy_cfg_vm* policy){
  const tn_tls_policy_cfg_vm* pol = policy ? policy : &g_tn_tls_policy_vm;
  tn_tls_vm* t = (tn_tls_vm*)xmalloc(sizeof(tn_tls_vm));
  memset(t, 0, sizeof(*t));
  t->sock = tn_tcp_connect_vm(host, port);
  if(t->sock == INVALID_SOCKET){
    tls_set_last_err((long long)net_last_error_impl());
    tn_tls_free_vm(t);
    return NULL;
  }
  if(pol->handshake_timeout_ms > 0){
    if(net_set_timeout_impl(t->sock, pol->handshake_timeout_ms) != 0){
      tls_set_last_err((long long)net_last_error_impl());
      tn_tls_free_vm(t);
      return NULL;
    }
  }
  if(tn_tls_handshake_vm(t, host, pol) != 0){
    tn_tls_free_vm(t);
    return NULL;
  }
  if(tn_tls_verify_pin_win_vm(t, pol) != 0){
    tn_tls_free_vm(t);
    return NULL;
  }
  return t;
}

static tn_tls_vm* tn_tls_connect_vm(const char* host, long long port){
  return tn_tls_connect_with_policy_vm(host, port, &g_tn_tls_policy_vm);
}

static long long tn_tls_send_vm(tn_tls_vm* t, const unsigned char* buf, long long len){
  if(!t || !buf || len <= 0) return 0;
  if(!t->sizes_ok) return -1;
  int header = (int)t->sizes.cbHeader;
  int trailer = (int)t->sizes.cbTrailer;
  int total = header + (int)len + trailer;
  unsigned char* out = (unsigned char*)xmalloc((size_t)total);

  SecBuffer bufs[4];
  SecBufferDesc desc;
  bufs[0].BufferType = SECBUFFER_STREAM_HEADER;
  bufs[0].pvBuffer = out;
  bufs[0].cbBuffer = (ULONG)header;
  bufs[1].BufferType = SECBUFFER_DATA;
  bufs[1].pvBuffer = out + header;
  bufs[1].cbBuffer = (ULONG)len;
  memcpy(bufs[1].pvBuffer, buf, (size_t)len);
  bufs[2].BufferType = SECBUFFER_STREAM_TRAILER;
  bufs[2].pvBuffer = out + header + len;
  bufs[2].cbBuffer = (ULONG)trailer;
  bufs[3].BufferType = SECBUFFER_EMPTY;
  bufs[3].pvBuffer = NULL;
  bufs[3].cbBuffer = 0;
  desc.ulVersion = SECBUFFER_VERSION;
  desc.cBuffers = 4;
  desc.pBuffers = bufs;

  SECURITY_STATUS st = EncryptMessage(&t->ctx, 0, &desc, 0);
  if(st != SEC_E_OK){
    tls_set_last_err((long long)st);
    free(out);
    return -1;
  }

  int to_send = (int)(bufs[0].cbBuffer + bufs[1].cbBuffer + bufs[2].cbBuffer);
  int sent = send(t->sock, (const char*)out, to_send, TN_SEND_FLAGS);
  free(out);
  if(sent <= 0){
    tls_set_last_err((long long)net_last_error_impl());
    return -1;
  }
  tls_set_last_err(0);
  return len;
}

static long long tn_tls_recv_vm(tn_tls_vm* t, unsigned char* out, long long len){
  if(!t || !out || len <= 0) return 0;
  if(t->plain && t->plain_pos < t->plain_len){
    int avail = t->plain_len - t->plain_pos;
    int take = (int)len;
    if(take > avail) take = avail;
    memcpy(out, t->plain + t->plain_pos, (size_t)take);
    t->plain_pos += take;
    if(t->plain_pos >= t->plain_len){
      free(t->plain);
      t->plain = NULL;
      t->plain_len = 0;
      t->plain_pos = 0;
    }
    tls_set_last_err(0);
    return take;
  }

  for(;;){
    if(t->in_len < 1){
      if(t->in_len >= t->in_cap){
        if(tn_tls_grow_inbuf_vm(t) != 0){
          tls_set_last_err((long long)net_last_error_impl());
          return -1;
        }
      }
      int n = recv(t->sock, (char*)t->inbuf + t->in_len, t->in_cap - t->in_len, 0);
      if(n <= 0){
        tls_set_last_err((long long)net_last_error_impl());
        return 0;
      }
      t->in_len += n;
    }

    SecBuffer bufs[4];
    SecBufferDesc desc;
    bufs[0].BufferType = SECBUFFER_DATA;
    bufs[0].pvBuffer = t->inbuf;
    bufs[0].cbBuffer = (ULONG)t->in_len;
    bufs[1].BufferType = SECBUFFER_EMPTY;
    bufs[2].BufferType = SECBUFFER_EMPTY;
    bufs[3].BufferType = SECBUFFER_EMPTY;
    desc.ulVersion = SECBUFFER_VERSION;
    desc.cBuffers = 4;
    desc.pBuffers = bufs;

    SECURITY_STATUS st = DecryptMessage(&t->ctx, &desc, 0, NULL);
    if(st == SEC_E_INCOMPLETE_MESSAGE){
      if(t->in_len >= t->in_cap){
        if(tn_tls_grow_inbuf_vm(t) != 0){
          tls_set_last_err((long long)net_last_error_impl());
          return -1;
        }
      }
      int n = recv(t->sock, (char*)t->inbuf + t->in_len, t->in_cap - t->in_len, 0);
      if(n <= 0){
        tls_set_last_err((long long)net_last_error_impl());
        return 0;
      }
      t->in_len += n;
      continue;
    }
    if(st == SEC_I_CONTEXT_EXPIRED){
      tls_set_last_err(0);
      return 0;
    }
    if(st != SEC_E_OK){
      tls_set_last_err((long long)st);
      return -1;
    }

    SecBuffer* data = NULL;
    SecBuffer* extra = NULL;
    for(int i=0;i<4;i++){
      if(bufs[i].BufferType == SECBUFFER_DATA) data = &bufs[i];
      if(bufs[i].BufferType == SECBUFFER_EXTRA) extra = &bufs[i];
    }

    if(extra && extra->cbBuffer > 0){
      int ex = (int)extra->cbBuffer;
      memmove(t->inbuf, t->inbuf + (t->in_len - ex), (size_t)ex);
      t->in_len = ex;
    } else {
      t->in_len = 0;
    }

    if(data && data->cbBuffer > 0){
      t->plain = (unsigned char*)xmalloc((size_t)data->cbBuffer);
      memcpy(t->plain, data->pvBuffer, data->cbBuffer);
      t->plain_len = (int)data->cbBuffer;
      t->plain_pos = 0;
      break;
    }
  }

  tls_set_last_err(0);
  return tn_tls_recv_vm(t, out, len);
}
#endif

static char* path_join_impl(const char* a, const char* b){
  if(!a || !a[0]) return dup_cstr(b ? b : "");
  if(!b || !b[0]) return dup_cstr(a);
#ifdef _WIN32
  if((strlen(b) >= 2 && b[1]==':') || b[0]=='\\' || b[0]=='/'){
    return dup_cstr(b);
  }
  char sep = '\\';
#else
  if(b[0]=='/') return dup_cstr(b);
  char sep = '/';
#endif
  size_t al = strlen(a);
  size_t bl = strlen(b);
  int need = (a[al-1] == sep) ? 0 : 1;
  char* out = (char*)xmalloc(al + bl + (size_t)need + 1);
  memcpy(out, a, al);
  size_t pos = al;
  if(need) out[pos++] = sep;
  memcpy(out + pos, b, bl);
  out[pos + bl] = 0;
  return out;
}

static char* path_basename_impl(const char* p){
  if(!p || !p[0]) return dup_cstr("");
  const char* end = p + strlen(p);
  const char* s = end;
  while(s > p){
    char c = s[-1];
    if(c=='/' || c=='\\') break;
    s--;
  }
  return dup_cstr(s);
}

static char* path_dirname_impl(const char* p){
  if(!p || !p[0]) return dup_cstr(".");
  const char* end = p + strlen(p);
  const char* s = end;
  while(s > p){
    char c = s[-1];
    if(c=='/' || c=='\\') break;
    s--;
  }
  if(s == p) return dup_cstr(".");
  size_t n = (size_t)(s - p - 1);
  char* out = (char*)xmalloc(n + 1);
  memcpy(out, p, n);
  out[n] = 0;
  return out;
}

/* Struct printing inline */
static void print_struct_inline(Type* ty, void* addr){
  if(!ty || ty->k!=TY_STRUCT){
    fputs("<not-struct>", stdout);
    return;
  }
  if(!addr){
    fputs("null", stdout);
    return;
  }

  fputc('{', stdout);
  for(int i=0;i<ty->field_n;i++){
    Field* f = &ty->fields[i];
    if(i) fputs(", ", stdout);

    fprintf(stdout, "%.*s: ", f->name_len, f->name);

    /* Read field into a Value and print inline */
    Value fv;
    memset(&fv, 0, sizeof(fv));

    if(type_is_signed_int(f->ty)){
      fv.k = V_I64;
      if(type_size(f->ty)==1){ int8_t x=0; memcpy(&x, (char*)addr + f->off, 1); fv.i = (long long)x; }
      else if(type_size(f->ty)==2){ int16_t x=0; memcpy(&x, (char*)addr + f->off, 2); fv.i = (long long)x; }
      else if(type_size(f->ty)==4){ int32_t x=0; memcpy(&x, (char*)addr + f->off, 4); fv.i = (long long)x; }
      else { int64_t x=0; memcpy(&x, (char*)addr + f->off, 8); fv.i = (long long)x; }
      fv.ty = NULL;
    } else if(f->ty->k==TY_F64){
      fv.k = V_F64;
      memcpy(&fv.f, (char*)addr + f->off, sizeof(double));
      fv.ty = NULL;
    } else if(type_is_unsigned_int(f->ty)){
      fv.k = V_I64;
      if(type_size(f->ty)==1){ uint8_t x=0; memcpy(&x, (char*)addr + f->off, 1); fv.i = (long long)x; }
      else if(type_size(f->ty)==2){ uint16_t x=0; memcpy(&x, (char*)addr + f->off, 2); fv.i = (long long)x; }
      else if(type_size(f->ty)==4){ uint32_t x=0; memcpy(&x, (char*)addr + f->off, 4); fv.i = (long long)x; }
      else { uint64_t x=0; memcpy(&x, (char*)addr + f->off, 8); fv.i = (long long)x; }
      fv.ty = NULL;
    } else if(f->ty->k==TY_PTR){
      fv.k = V_PTR;
      fv.p = *(void**)((char*)addr + f->off);
      fv.ty = f->ty; /* keep pointer type */
    } else if(f->ty->k==TY_ARRAY || f->ty->k==TY_STRUCT){
      /* Inline composite points directly into struct storage */
      fv.k = V_PTR;
      fv.p = (void*)((char*)addr + f->off);
      fv.ty = f->ty;
    } else {
      fv.k = V_PTR;
      fv.p = (void*)((char*)addr + f->off);
      fv.ty = f->ty;
    }

    say_print_inline(fv);
  }
  fputc('}', stdout);
}

static void say_write_i64_inline(long long v){
  char tmp[32];
  char out[32];
  unsigned long long uv = (unsigned long long)v;
  int neg = 0;
  int n = 0;
  if(v < 0){
    neg = 1;
    uv = (unsigned long long)(~uv) + 1ull;
  }
  do{
    tmp[n++] = (char)('0' + (uv % 10ull));
    uv /= 10ull;
  } while(uv != 0ull && n < (int)sizeof(tmp));
  if(neg && n < (int)sizeof(tmp)) tmp[n++] = '-';
  int m = 0;
  for(int i=n-1;i>=0;i--) out[m++] = tmp[i];
  if(m > 0) fwrite(out, 1, (size_t)m, stdout);
}

/* Print a Value without newline */
static void say_print_inline(Value v){
  if(v.k == V_I64){
    tn_color_begin(TN_CLR_INT);
    say_write_i64_inline(v.i);
    tn_color_end();
    return;
  }
  if(v.k == V_F64){
    tn_color_begin(TN_CLR_FLOAT);
    fprintf(stdout, "%.6f", v.f);
    tn_color_end();
    return;
  }

  /* Pointer-like */
  if(!v.p){
    tn_color_begin(TN_CLR_NULL);
    fputs("null", stdout);
    tn_color_end();
    return;
  }

  /* If we have type info, use it */
  if(v.ty){
    if(v.ty->k==TY_ARRAY){
      if(v.ty->elem && v.ty->elem->k==TY_U8){
        tn_color_begin(TN_CLR_STR);
        print_u8_array_inline(v.p, v.ty->len);
        tn_color_end();
        return;
      }
      if(v.ty->elem && v.ty->elem->k==TY_I64){
        print_i64_array_inline(v.p, v.ty->len);
        return;
      }
      if(v.ty->elem && v.ty->elem->k==TY_F64){
        print_f64_array_inline(v.p, v.ty->len);
        return;
      }
      /* generic array */
      fprintf(stdout, "<array len=%lld>", v.ty->len);
      return;
    }

    if(v.ty->k==TY_STRUCT){
      print_struct_inline(v.ty, v.p);
      return;
    }

    if(v.ty->k==TY_PTR){
      /* Pointer to u8 -> print as C-string (common case) */
      if(v.ty->elem && v.ty->elem->k==TY_U8){
        tn_color_begin(TN_CLR_STR);
        print_cstr_inline((const char*)v.p);
        tn_color_end();
        return;
      }
      /* Else print address */
      tn_color_begin(TN_CLR_PTR);
      fprintf(stdout, "0x%llx", (unsigned long long)(uintptr_t)v.p);
      tn_color_end();
      return;
    }
  }

  /* No type info: default to C-string */
  tn_color_begin(TN_CLR_STR);
  print_cstr_inline((const char*)v.p);
  tn_color_end();
}

/* -------------------------------------------------
 * Builtins
 * ------------------------------------------------- */

int builtin_call(const char* name, int nlen, Value* args, int argc, Value* out){
  /* Normalize module-qualified calls (e.g. os.fb_width -> fb_width). */
  if(name && nlen > 0){
    for(int i = nlen - 1; i >= 0; i--){
      if(name[i] == '.'){
        if(i + 1 < nlen){
          name = name + i + 1;
          nlen = nlen - (i + 1);
        }
        break;
      }
    }
  }
  if(nlen==10 && strncmp(name,"args_count",10)==0){
    if(argc!=0) die("args_count expects 0 args");
    out->k=V_I64; out->i=(long long)g_argc; out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==3 && strncmp(name,"arg",3)==0){
    if(argc!=1) die("arg expects 1 arg");
    long long i = args[0].i;
    if(i < 0 || i >= g_argc || !g_argv){
      out->k=V_PTR; out->p=NULL; out->i=0; out->ty=NULL;
      return 1;
    }
    out->k=V_PTR; out->p=(void*)g_argv[i]; out->i=0; out->ty=NULL;
    return 1;
  }
  if(nlen==13 && strncmp(name,"async_workers",13)==0){
    if(argc!=0) die("async_workers expects 0 args");
    out->k=V_I64; out->i=1; out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==13 && strncmp(name,"async_pending",13)==0){
    if(argc!=0) die("async_pending expects 0 args");
    out->k=V_I64; out->i=0; out->p=NULL; out->ty=NULL;
    return 1;
  }
  /* sys_outb/sys_inb : port I/O stubs for VM */
  if((nlen==12 && strncmp(name,"sys.sys_outb",12)==0) ||
     (nlen==8 && strncmp(name,"sys_outb",8)==0)){
    if(argc!=2) die("sys_outb expects 2 args");
    int port = (int)args[0].i;
    int val = (int)args[1].i;
    if(port >= 0 && port < 65536) g_port8[port] = (unsigned char)(val & 0xFF);
    out->k=V_I64; out->i=0; out->p=NULL; out->ty=NULL;
    return 1;
  }
  if((nlen==11 && strncmp(name,"sys.sys_inb",11)==0) ||
     (nlen==7 && strncmp(name,"sys_inb",7)==0)){
    if(argc!=1) die("sys_inb expects 1 arg");
    int port = (int)args[0].i;
    int val = 0;
    if(port >= 0 && port < 65536) val = (int)g_port8[port];
    out->k=V_I64; out->i=val; out->p=NULL; out->ty=NULL;
    return 1;
  }
  if((nlen==12 && strncmp(name,"sys.sys_exit",12)==0) ||
     (nlen==8 && strncmp(name,"sys_exit",8)==0)){
    if(argc!=1) die("sys_exit expects 1 arg");
    out->k=V_I64; out->i=0; out->p=NULL; out->ty=NULL;
    return 1;
  }
  /* say(...) : Python-like print
     - say(); prints newline
     - say(a,b,c); prints space-separated values + newline
  */
  if(nlen==3 && strncmp(name,"say",3)==0){
    if(argc==0){
      fputc('\n', stdout);
      out->k=V_I64; out->i=0; out->p=NULL; out->ty=NULL;
      return 1;
    }

    for(int i=0;i<argc;i++){
      if(i) fputc(' ', stdout);
      say_print_inline(args[i]);
    }
    fputc('\n', stdout);

    out->k=V_I64; out->i=0; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* say_str(x) : explicit string print (kept for compatibility) */
  if(nlen==7 && strncmp(name,"say_str",7)==0){
    if(argc!=1) die("say_str expects 1 arg");
    const char* s=(const char*)args[0].p;
    if(!s) s="";
    tn_color_begin(TN_CLR_STR);
    printf("%s", s);
    tn_color_end();
    printf("\n");
    out->k=V_I64; out->i=0; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* len(*u8) -> i64 */
  if(nlen==3 && strncmp(name,"len",3)==0){
    if(argc!=1) die("len expects 1 arg");
    const char* s=(const char*)args[0].p;
    out->k=V_I64; out->i=(long long)strlen(s?s:""); out->p=NULL; out->ty=NULL;
    return 1;
  }

  if(nlen==10 && strncmp(name,"bit_popcnt",10)==0){
    if(argc!=1) die("bit_popcnt expects 1 arg");
    out->k=V_I64; out->i=tn_popcnt64((unsigned long long)args[0].i); out->p=NULL; out->ty=NULL;
    return 1;
  }

  if(nlen==7 && strncmp(name,"bit_clz",7)==0){
    if(argc!=1) die("bit_clz expects 1 arg");
    out->k=V_I64; out->i=tn_clz64((unsigned long long)args[0].i); out->p=NULL; out->ty=NULL;
    return 1;
  }

  if(nlen==7 && strncmp(name,"bit_ctz",7)==0){
    if(argc!=1) die("bit_ctz expects 1 arg");
    out->k=V_I64; out->i=tn_ctz64((unsigned long long)args[0].i); out->p=NULL; out->ty=NULL;
    return 1;
  }

  if(nlen==9 && strncmp(name,"bit_bswap",9)==0){
    if(argc!=1) die("bit_bswap expects 1 arg");
    out->k=V_I64; out->i=(long long)tn_bswap64((unsigned long long)args[0].i); out->p=NULL; out->ty=NULL;
    return 1;
  }

  if(nlen==8 && strncmp(name,"bit_rotl",8)==0){
    if(argc!=2) die("bit_rotl expects 2 args");
    out->k=V_I64;
    out->i=(long long)tn_rotl64((unsigned long long)args[0].i, (unsigned long long)args[1].i);
    out->p=NULL; out->ty=NULL;
    return 1;
  }

  if(nlen==8 && strncmp(name,"bit_rotr",8)==0){
    if(argc!=2) die("bit_rotr expects 2 args");
    out->k=V_I64;
    out->i=(long long)tn_rotr64((unsigned long long)args[0].i, (unsigned long long)args[1].i);
    out->p=NULL; out->ty=NULL;
    return 1;
  }

  if(nlen==12 && strncmp(name,"cpu_has_sse2",12)==0){
    if(argc!=0) die("cpu_has_sse2 expects 0 args");
    out->k=V_I64; out->i=(long long)tn_cpu_has_sse2_impl(); out->p=NULL; out->ty=NULL;
    return 1;
  }

  if(nlen==12 && strncmp(name,"cpu_has_avx2",12)==0){
    if(argc!=0) die("cpu_has_avx2 expects 0 args");
    out->k=V_I64; out->i=(long long)tn_cpu_has_avx2_impl(); out->p=NULL; out->ty=NULL;
    return 1;
  }

  if(nlen==11 && strncmp(name,"cpu_has_fma",11)==0){
    if(argc!=0) die("cpu_has_fma expects 0 args");
    out->k=V_I64; out->i=(long long)tn_cpu_has_fma_impl(); out->p=NULL; out->ty=NULL;
    return 1;
  }

  if(nlen==12 && strncmp(name,"cpu_has_neon",12)==0){
    if(argc!=0) die("cpu_has_neon expects 0 args");
    out->k=V_I64; out->i=(long long)tn_cpu_has_neon_impl(); out->p=NULL; out->ty=NULL;
    return 1;
  }

  if(nlen==18 && strncmp(name,"tezz_ai_matmul_f64",18)==0){
    if(argc!=6) die("tezz_ai_matmul_f64 expects 6 args");
    out->k=V_I64;
    out->i=tn_ai_vm_matmul_f64((double*)args[0].p, (const double*)args[1].p,
                               (const double*)args[2].p, args[3].i, args[4].i, args[5].i);
    out->p=NULL; out->ty=NULL;
    return 1;
  }

  if(nlen==21 && strncmp(name,"tezz_ai_matmul_i8_f64",21)==0){
    if(argc!=7) die("tezz_ai_matmul_i8_f64 expects 7 args");
    out->k=V_I64;
    out->i=tn_ai_vm_matmul_i8_f64((double*)args[0].p, (const double*)args[1].p,
                                  (const signed char*)args[2].p, (const double*)args[3].p,
                                  args[4].i, args[5].i, args[6].i);
    out->p=NULL; out->ty=NULL;
    return 1;
  }

  if(nlen==24 && strncmp(name,"tezz_ai_rmsnorm_rows_f64",24)==0){
    if(argc!=6) die("tezz_ai_rmsnorm_rows_f64 expects 6 args");
    double eps = (args[5].k==V_F64) ? args[5].f : (double)args[5].i;
    out->k=V_I64;
    out->i=tn_ai_vm_rmsnorm_rows_f64((double*)args[0].p, (const double*)args[1].p,
                                     (const double*)args[2].p, args[3].i, args[4].i, eps);
    out->p=NULL; out->ty=NULL;
    return 1;
  }

  if(nlen==24 && strncmp(name,"tezz_ai_softmax_rows_f64",24)==0){
    if(argc!=4) die("tezz_ai_softmax_rows_f64 expects 4 args");
    out->k=V_I64;
    out->i=tn_ai_vm_softmax_rows_f64((double*)args[0].p, (const double*)args[1].p,
                                     args[2].i, args[3].i);
    out->p=NULL; out->ty=NULL;
    return 1;
  }

  if(nlen==16 && strncmp(name,"tezz_ai_rope_f64",16)==0){
    if(argc!=5) die("tezz_ai_rope_f64 expects 5 args");
    double base = (args[4].k==V_F64) ? args[4].f : (double)args[4].i;
    out->k=V_I64;
    out->i=tn_ai_vm_rope_f64((double*)args[0].p, (double*)args[1].p,
                             args[2].i, args[3].i, base);
    out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* strcmp(a:*u8,b:*u8)->i64 */
  if(nlen==6 && strncmp(name,"strcmp",6)==0){
    if(argc!=2) die("strcmp expects 2 args");
    const char* a=(const char*)args[0].p;
    const char* b=(const char*)args[1].p;
    out->k=V_I64; out->i=(long long)strcmp(a?a:"", b?b:""); out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* memcpy(dst:*u8, src:*u8, n:i64) -> *u8 */
  if(nlen==6 && strncmp(name,"memcpy",6)==0){
    if(argc!=3) die("memcpy expects 3 args");
    void* dst=args[0].p;
    void* src=args[1].p;
    long long n=args[2].i;
    if(n<0) n=0;
    memcpy(dst, src, (size_t)n);
    out->k=V_PTR; out->p=dst; out->i=0; out->ty=NULL;
    return 1;
  }
  /* memmove(dst:*u8, src:*u8, n:i64) -> *u8 */
  if(nlen==7 && strncmp(name,"memmove",7)==0){
    if(argc!=3) die("memmove expects 3 args");
    void* dst=args[0].p;
    void* src=args[1].p;
    long long n=args[2].i;
    if(n<0) n=0;
    memmove(dst, src, (size_t)n);
    out->k=V_PTR; out->p=dst; out->i=0; out->ty=NULL;
    return 1;
  }
  /* memcmp(a:*u8, b:*u8, n:i64) -> i64 */
  if(nlen==6 && strncmp(name,"memcmp",6)==0){
    if(argc!=3) die("memcmp expects 3 args");
    void* a=args[0].p;
    void* b=args[1].p;
    long long n=args[2].i;
    if(n<0) n=0;
    int r = memcmp(a, b, (size_t)n);
    out->k=V_I64; out->i=(long long)r; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* malloc(n:i64)->*u8 */
  if(nlen==6 && strncmp(name,"malloc",6)==0){
    if(argc!=1) die("malloc expects 1 arg");
    long long n=args[0].i; if(n<0) n=0;
    out->k=V_PTR; out->p=xmalloc((size_t)n); out->i=0; out->ty=NULL;
    return 1;
  }

  /* free(p:*u8)->i64 */
  if(nlen==4 && strncmp(name,"free",4)==0){
    if(argc!=1) die("free expects 1 arg");
    free(args[0].p);
    out->k=V_I64; out->i=0; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* input_line()->*u8 (heap buffer) */
  if(nlen==10 && strncmp(name,"input_line",10)==0){
    if(argc!=0) die("input_line expects 0 args");
    char buf[4096];
    if(!fgets(buf,sizeof(buf),stdin)) buf[0]=0;
    size_t L=strlen(buf);
    if(L && buf[L-1]=='\n') buf[L-1]=0;
    char* s=(char*)xmalloc(strlen(buf)+1);
    strcpy(s,buf);
    out->k=V_PTR; out->p=s; out->i=0; out->ty=NULL;
    return 1;
  }

  /* input_i64()->i64 */
  if(nlen==9 && strncmp(name,"input_i64",9)==0){
    if(argc!=0) die("input_i64 expects 0 args");
    long long v=0;
    if(scanf("%lld",&v)!=1) v=0;
    out->k=V_I64; out->i=v; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* input([prompt:*u8[, mode:i64]])->f64 (mode currently accepted for compatibility) */
  if(nlen==5 && strncmp(name,"input",5)==0){
    if(argc>2) die("input expects 0, 1, or 2 args");
    const char* prompt = NULL;
    if(argc>=1) prompt = (const char*)args[0].p;
    for(;;){
      if(prompt && *prompt){
        fputs(prompt, stdout);
        fputs("\n", stdout);
        fflush(stdout);
      }
      char buf[4096];
      if(!fgets(buf, sizeof(buf), stdin)) buf[0]=0;
      // trim trailing newline/CR
      size_t L = strlen(buf);
      while(L && (buf[L-1]=='\n' || buf[L-1]=='\r')){ buf[L-1]=0; L--; }
      char* endp = NULL;
      double v = strtod(buf, &endp);
      if(endp && endp != buf){
        // allow trailing spaces
        while(*endp==' '||*endp=='\t') endp++;
        if(*endp==0){
          out->k=V_F64; out->f=v; out->i=0; out->p=NULL; out->ty=NULL;
          return 1;
        }
      }
      fputs("Please enter numbers.\n", stdout);
      fflush(stdout);
    }
  }

  /* say_f(x:f64) : float print (used by native lowering) */
  if(nlen==5 && strncmp(name,"say_f",5)==0){
    if(argc!=1) die("say_f expects 1 arg");
    double v = (args[0].k==V_F64) ? args[0].f : (double)args[0].i;
    tn_color_begin(TN_CLR_FLOAT);
    fprintf(stdout, "%.6f", v);
    tn_color_end();
    fprintf(stdout, "\n");
    out->k=V_I64; out->i=0; out->p=NULL; out->ty=NULL;
    return 1;
  }

  if(nlen==8 && strncmp(name,"proc_run",8)==0){
    if(argc!=1) die("proc_run expects 1 arg");
    const char* cmd = (const char*)args[0].p;
    if(!cmd) cmd = "";
    int rc = system(cmd);
    out->k=V_I64; out->i=(long long)rc; out->p=NULL; out->ty=NULL;
    return 1;
  }

  if(nlen==8 && strncmp(name,"proc_out",8)==0){
    if(argc!=1) die("proc_out expects 1 arg");
    const char* cmd = (const char*)args[0].p;
    if(!cmd) cmd = "";
#ifdef _WIN32
    FILE* p = _popen(cmd, "rb");
#else
    FILE* p = popen(cmd, "rb");
#endif
    if(!p){
      out->k=V_PTR; out->p=NULL; out->i=0; out->ty=NULL;
      return 1;
    }
    size_t cap = 256;
    size_t len = 0;
    char* buf = (char*)xmalloc(cap);
    for(;;){
      if(len + 128 >= cap){
        cap *= 2;
        buf = (char*)xrealloc(buf, cap);
      }
      size_t n = fread(buf + len, 1, 128, p);
      if(n==0) break;
      len += n;
    }
#ifdef _WIN32
    _pclose(p);
#else
    pclose(p);
#endif
    if(len + 1 > cap){
      buf = (char*)xrealloc(buf, len + 1);
    }
    buf[len] = 0;
    out->k=V_PTR; out->p=buf; out->i=0; out->ty=NULL;
    return 1;
  }

  if(nlen==13 && strncmp(name,"http_download",13)==0){
    if(argc!=2) die("http_download expects 2 args");
    const char* url = (const char*)args[0].p;
    const char* outp = (const char*)args[1].p;
#ifdef _WIN32
    int r = http_download_winhttp(url, outp);
    out->k=V_I64; out->i=(long long)r; out->p=NULL; out->ty=NULL;
    return 1;
#else
    (void)url; (void)outp;
    out->k=V_I64; out->i=-2; out->p=NULL; out->ty=NULL;
    return 1;
#endif
  }

  /* fopen(path:*u8, mode:*u8)->*u8 */
  if(nlen==5 && strncmp(name,"fopen",5)==0){
    if(argc!=2) die("fopen expects 2 args");
    const char* path = (const char*)args[0].p;
    const char* mode = (const char*)args[1].p;
    FILE* f = fopen(path?path:"", mode?mode:"");
    out->k=V_PTR; out->p=(void*)f; out->i=0; out->ty=NULL;
    return 1;
  }

  /* fclose(f:*u8)->i64 */
  if(nlen==6 && strncmp(name,"fclose",6)==0){
    if(argc!=1) die("fclose expects 1 arg");
    FILE* f = (FILE*)args[0].p;
    int r = f ? fclose(f) : -1;
    out->k=V_I64; out->i=(long long)r; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* fflush(f:*u8)->i64 */
  if(nlen==6 && strncmp(name,"fflush",6)==0){
    if(argc!=1) die("fflush expects 1 arg");
    FILE* f = (FILE*)args[0].p;
    int r = fflush(f);
    out->k=V_I64; out->i=(long long)r; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* fread(buf:*u8, size:i64, count:i64, f:*u8)->i64 (bytes read) */
  if(nlen==5 && strncmp(name,"fread",5)==0){
    if(argc!=4) die("fread expects 4 args");
    unsigned char* buf = (unsigned char*)args[0].p;
    long long size = args[1].i; if(size<0) size=0;
    long long count = args[2].i; if(count<0) count=0;
    FILE* f = (FILE*)args[3].p;
    size_t n = 0;
    if(f && buf && size>0 && count>0){
      size_t total = 0;
      if(!tn_mul_overflow_size(size, count, &total)){
        n = fread(buf, 1, total, f);
      }
    }
    out->k=V_I64; out->i=(long long)n; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* fwrite(buf:*u8, size:i64, count:i64, f:*u8)->i64 (bytes written) */
  if(nlen==6 && strncmp(name,"fwrite",6)==0){
    if(argc!=4) die("fwrite expects 4 args");
    unsigned char* buf = (unsigned char*)args[0].p;
    long long size = args[1].i; if(size<0) size=0;
    long long count = args[2].i; if(count<0) count=0;
    FILE* f = (FILE*)args[3].p;
    size_t n = 0;
    if(f && buf && size>0 && count>0){
      size_t total = 0;
      if(!tn_mul_overflow_size(size, count, &total)){
        n = fwrite(buf, 1, total, f);
      }
    }
    out->k=V_I64; out->i=(long long)n; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* fseek(f:*u8, offset:i64, whence:i64)->i64 */
  if(nlen==5 && strncmp(name,"fseek",5)==0){
    if(argc!=3) die("fseek expects 3 args");
    FILE* f = (FILE*)args[0].p;
    long long off = args[1].i;
    int whence = (int)args[2].i;
    int r = f ? tn_fseek(f, off, whence) : -1;
    out->k=V_I64; out->i=(long long)r; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* ftell(f:*u8)->i64 */
  if(nlen==5 && strncmp(name,"ftell",5)==0){
    if(argc!=1) die("ftell expects 1 arg");
    FILE* f = (FILE*)args[0].p;
    long long pos = f ? tn_ftell(f) : -1;
    out->k=V_I64; out->i=pos; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* file_size(path:*u8)->i64 */
  if(nlen==9 && strncmp(name,"file_size",9)==0){
    if(argc!=1) die("file_size expects 1 arg");
    const char* path = (const char*)args[0].p;
    FILE* f = fopen(path?path:"", "rb");
    long long sz = -1;
    if(f){
      if(tn_fseek(f, 0, 2)==0){
        sz = tn_ftell(f);
      }
      fclose(f);
    }
    out->k=V_I64; out->i=sz; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* read_file(path:*u8)->*u8 (heap buffer, NUL-terminated) */
  if(nlen==9 && strncmp(name,"read_file",9)==0){
    if(argc!=1) die("read_file expects 1 arg");
    const char* path = (const char*)args[0].p;
    FILE* f = fopen(path?path:"", "rb");
    if(!f){
      out->k=V_PTR; out->p=NULL; out->i=0; out->ty=NULL;
      return 1;
    }
    if(tn_fseek(f, 0, 2)!=0){
      fclose(f);
      out->k=V_PTR; out->p=NULL; out->i=0; out->ty=NULL;
      return 1;
    }
    long long sz = tn_ftell(f);
    if(sz < 0 || (unsigned long long)sz > (unsigned long long)(SIZE_MAX - 1) || tn_fseek(f, 0, 0)!=0){
      fclose(f);
      out->k=V_PTR; out->p=NULL; out->i=0; out->ty=NULL;
      return 1;
    }
    char* buf = (char*)xmalloc((size_t)sz + 1);
    size_t got = fread(buf, 1, (size_t)sz, f);
    buf[got] = 0;
    fclose(f);
    out->k=V_PTR; out->p=buf; out->i=0; out->ty=NULL;
    return 1;
  }

  /* write_file(path:*u8, data:*u8, len:i64)->i64 (bytes written or -1) */
  if(nlen==10 && strncmp(name,"write_file",10)==0){
    if(argc!=3) die("write_file expects 3 args");
    const char* path = (const char*)args[0].p;
    const unsigned char* data = (const unsigned char*)args[1].p;
    long long len = args[2].i;
    FILE* f = fopen(path?path:"", "wb");
    if(!f){
      out->k=V_I64; out->i=-1; out->p=NULL; out->ty=NULL;
      return 1;
    }
    size_t n = 0;
    if(len < 0){
      fclose(f);
      out->k=V_I64; out->i=-1; out->p=NULL; out->ty=NULL;
      return 1;
    }
    if(data && len>0){
      if((unsigned long long)len > (unsigned long long)SIZE_MAX){
        fclose(f);
        out->k=V_I64; out->i=-1; out->p=NULL; out->ty=NULL;
        return 1;
      }
      n = fwrite(data, 1, (size_t)len, f);
    }
    fclose(f);
    out->k=V_I64; out->i=(long long)n; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* read_line(f:*u8)->*u8 (heap buffer, NUL-terminated) */
  if(nlen==9 && strncmp(name,"read_line",9)==0){
    if(argc!=1) die("read_line expects 1 arg");
    FILE* f = (FILE*)args[0].p;
    if(!f){
      out->k=V_PTR; out->p=NULL; out->i=0; out->ty=NULL;
      return 1;
    }
    size_t cap = 128;
    size_t n = 0;
    char* buf = (char*)xmalloc(cap);
    for(;;){
      int ch = fgetc(f);
      if(ch == EOF || ch == '\n') break;
      if(ch == '\r'){
        int nx = fgetc(f);
        if(nx != '\n' && nx != EOF) ungetc(nx, f);
        break;
      }
      if(n + 1 >= cap){
        if(cap > (SIZE_MAX / 2)){
          free(buf);
          die("out of memory");
        }
        size_t ncap = cap * 2;
        char* nb = (char*)realloc(buf, ncap);
        if(!nb){ free(buf); die("out of memory"); }
        buf = nb; cap = ncap;
      }
      buf[n++] = (char)ch;
    }
    if(n + 1 >= cap){
      if(cap == SIZE_MAX){
        free(buf);
        die("out of memory");
      }
      char* nb = (char*)realloc(buf, cap + 1);
      if(!nb){ free(buf); die("out of memory"); }
      buf = nb;
    }
    buf[n] = 0;
    out->k=V_PTR; out->p=buf; out->i=0; out->ty=NULL;
    return 1;
  }

  /* read_bytes(f:*u8, buf:*u8, n:i64)->i64 (bytes read) */
  if(nlen==10 && strncmp(name,"read_bytes",10)==0){
    if(argc!=3) die("read_bytes expects 3 args");
    FILE* f = (FILE*)args[0].p;
    unsigned char* buf = (unsigned char*)args[1].p;
    long long n = args[2].i; if(n<0) n=0;
    size_t got = 0;
    if(f && buf && n>0){
      if((unsigned long long)n <= (unsigned long long)SIZE_MAX){
        got = fread(buf, 1, (size_t)n, f);
      }
    }
    out->k=V_I64; out->i=(long long)got; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* write_line(f:*u8, s:*u8)->i64 */
  if(nlen==10 && strncmp(name,"write_line",10)==0){
    if(argc!=2) die("write_line expects 2 args");
    FILE* f = (FILE*)args[0].p;
    const char* s = (const char*)args[1].p;
    int r = -1;
    if(f){
      if(!s) s = "";
      if(fputs(s, f) >= 0 && fputc('\n', f) >= 0) r = 0;
    }
    out->k=V_I64; out->i=(long long)r; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* io_err()->i64 */
  if(nlen==6 && strncmp(name,"io_err",6)==0){
    if(argc!=0) die("io_err expects 0 args");
    out->k=V_I64; out->i=(long long)errno; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* io_eof(f:*u8)->i64 */
  if(nlen==6 && strncmp(name,"io_eof",6)==0){
    if(argc!=1) die("io_eof expects 1 arg");
    FILE* f = (FILE*)args[0].p;
    int v = f ? feof(f) : 1;
    out->k=V_I64; out->i=(long long)v; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* stdin()->*u8 */
  if(nlen==5 && strncmp(name,"stdin",5)==0){
    if(argc!=0) die("stdin expects 0 args");
    out->k=V_PTR; out->p=(void*)stdin; out->i=0; out->ty=NULL;
    return 1;
  }

  /* stdout()->*u8 */
  if(nlen==6 && strncmp(name,"stdout",6)==0){
    if(argc!=0) die("stdout expects 0 args");
    out->k=V_PTR; out->p=(void*)stdout; out->i=0; out->ty=NULL;
    return 1;
  }

  /* stderr()->*u8 */
  if(nlen==6 && strncmp(name,"stderr",6)==0){
    if(argc!=0) die("stderr expects 0 args");
    out->k=V_PTR; out->p=(void*)stderr; out->i=0; out->ty=NULL;
    return 1;
  }

  /* get_cwd()->*u8 */
  if(nlen==7 && strncmp(name,"get_cwd",7)==0){
    if(argc!=0) die("get_cwd expects 0 args");
    char buf[4096];
#ifdef _WIN32
    if(!_getcwd(buf, (int)sizeof(buf))) buf[0]=0;
#else
    if(!getcwd(buf, sizeof(buf))) buf[0]=0;
#endif
    out->k=V_PTR; out->p=dup_cstr(buf); out->i=0; out->ty=NULL;
    return 1;
  }

  /* chdir(path:*u8)->i64 */
  if(nlen==5 && strncmp(name,"chdir",5)==0){
    if(argc!=1) die("chdir expects 1 arg");
    const char* p = (const char*)args[0].p;
#ifdef _WIN32
    int r = _chdir(p ? p : "");
#else
    int r = chdir(p ? p : "");
#endif
    out->k=V_I64; out->i=(long long)r; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* mkdir(path:*u8)->i64 */
  if(nlen==5 && strncmp(name,"mkdir",5)==0){
    if(argc!=1) die("mkdir expects 1 arg");
    const char* p = (const char*)args[0].p;
#ifdef _WIN32
    int r = _mkdir(p ? p : "");
#else
    int r = mkdir(p ? p : "", 0777);
#endif
    out->k=V_I64; out->i=(long long)r; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* rmdir(path:*u8)->i64 */
  if(nlen==5 && strncmp(name,"rmdir",5)==0){
    if(argc!=1) die("rmdir expects 1 arg");
    const char* p = (const char*)args[0].p;
#ifdef _WIN32
    int r = _rmdir(p ? p : "");
#else
    int r = rmdir(p ? p : "");
#endif
    out->k=V_I64; out->i=(long long)r; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* remove(path:*u8)->i64 */
  if(nlen==6 && strncmp(name,"remove",6)==0){
    if(argc!=1) die("remove expects 1 arg");
    const char* p = (const char*)args[0].p;
    int r = remove(p ? p : "");
    out->k=V_I64; out->i=(long long)r; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* rename(old:*u8, new:*u8)->i64 */
  if(nlen==6 && strncmp(name,"rename",6)==0){
    if(argc!=2) die("rename expects 2 args");
    const char* a = (const char*)args[0].p;
    const char* b = (const char*)args[1].p;
    int r = rename(a ? a : "", b ? b : "");
    out->k=V_I64; out->i=(long long)r; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* path_exists(path:*u8)->i64 */
  if(nlen==11 && strncmp(name,"path_exists",11)==0){
    if(argc!=1) die("path_exists expects 1 arg");
    const char* p = (const char*)args[0].p;
#ifdef _WIN32
    int r = _access(p ? p : "", 0);
#else
    int r = access(p ? p : "", F_OK);
#endif
    out->k=V_I64; out->i=(long long)(r==0); out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* path_is_dir(path:*u8)->i64 */
  if(nlen==11 && strncmp(name,"path_is_dir",11)==0){
    if(argc!=1) die("path_is_dir expects 1 arg");
    const char* p = (const char*)args[0].p;
    int ok = 0;
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(p ? p : "");
    if(attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) ok = 1;
#else
    struct stat st;
    if(p && stat(p, &st) == 0 && S_ISDIR(st.st_mode)) ok = 1;
#endif
    out->k=V_I64; out->i=(long long)ok; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* path_join(a:*u8, b:*u8)->*u8 */
  if(nlen==9 && strncmp(name,"path_join",9)==0){
    if(argc!=2) die("path_join expects 2 args");
    const char* a = (const char*)args[0].p;
    const char* b = (const char*)args[1].p;
    out->k=V_PTR; out->p=path_join_impl(a, b); out->i=0; out->ty=NULL;
    return 1;
  }

  /* path_sep()->i64 */
  if(nlen==8 && strncmp(name,"path_sep",8)==0){
    if(argc!=0) die("path_sep expects 0 args");
#ifdef _WIN32
    out->k=V_I64; out->i='\\'; out->p=NULL; out->ty=NULL;
#else
    out->k=V_I64; out->i='/'; out->p=NULL; out->ty=NULL;
#endif
    return 1;
  }

  /* path_basename(path:*u8)->*u8 */
  if(nlen==13 && strncmp(name,"path_basename",13)==0){
    if(argc!=1) die("path_basename expects 1 arg");
    const char* p = (const char*)args[0].p;
    out->k=V_PTR; out->p=path_basename_impl(p); out->i=0; out->ty=NULL;
    return 1;
  }

  /* path_dirname(path:*u8)->*u8 */
  if(nlen==12 && strncmp(name,"path_dirname",12)==0){
    if(argc!=1) die("path_dirname expects 1 arg");
    const char* p = (const char*)args[0].p;
    out->k=V_PTR; out->p=path_dirname_impl(p); out->i=0; out->ty=NULL;
    return 1;
  }

  /* list_dir(path:*u8)->*u8 */
  if(nlen==8 && strncmp(name,"list_dir",8)==0){
    if(argc!=1) die("list_dir expects 1 arg");
    const char* p = (const char*)args[0].p;
    out->k=V_PTR; out->p=list_dir_impl(p); out->i=0; out->ty=NULL;
    return 1;
  }

  /* os_name()->*u8 */
  if(nlen==7 && strncmp(name,"os_name",7)==0){
    if(argc!=0) die("os_name expects 0 args");
#ifdef _WIN32
    out->k=V_PTR; out->p=dup_cstr("windows"); out->i=0; out->ty=NULL;
#else
  #ifdef __APPLE__
    out->k=V_PTR; out->p=dup_cstr("macos"); out->i=0; out->ty=NULL;
  #else
    out->k=V_PTR; out->p=dup_cstr("linux"); out->i=0; out->ty=NULL;
  #endif
#endif
    return 1;
  }

  /* get_env(name:str)->str */
  if(nlen==7 && strncmp(name,"get_env",7)==0){
    if(argc!=1) die("get_env expects 1 arg");
    const char* key = (const char*)args[0].p;
    const char* val = getenv(key);
    out->k=V_PTR; out->p=val ? dup_cstr(val) : NULL; out->i=0; out->ty=NULL;
    return 1;
  }

  /* color_is_tty_stdout()->i64 */
  if(nlen==19 && strncmp(name,"color_is_tty_stdout",19)==0){
    if(argc!=0) die("color_is_tty_stdout expects 0 args");
    out->k=V_I64; out->i=tn_color_enabled(); out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* tts_play_pcm(pcm, len, rate)->i64 */
  if(nlen==12 && strncmp(name,"tts_play_pcm",12)==0){
    if(argc!=3) die("tts_play_pcm expects 3 args");
    long long* pcm = (long long*)args[0].p;
    long long len = args[1].i;
    long long rate = args[2].i;
    out->k=V_I64; out->i=tn_tts_play_pcm(pcm, len, rate); out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* stt_capture_mic(pcm, n_samples, rate)->i64 */
  if(nlen==15 && strncmp(name,"stt_capture_mic",15)==0){
    if(argc!=3) die("stt_capture_mic expects 3 args");
    long long* pcm = (long long*)args[0].p;
    long long n_samples = args[1].i;
    long long rate = args[2].i;
    out->k=V_I64; out->i=tn_stt_capture_mic(pcm, n_samples, rate); out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* Strip gui_win. module prefix — extern fn wrappers bridge to bare builtins */
  if(nlen > 8 && strncmp(name, "gui_win.", 8)==0){ name += 8; nlen -= 8; }
  if(nlen > 3 && strncmp(name, "os.", 3)==0){ name += 3; nlen -= 3; }

  /* framebuffer + host window backend */
  if(nlen==7 && strncmp(name,"fb_addr",7)==0){
    if(argc!=0) die("fb_addr expects 0 args");
    out->k=V_PTR; out->p=tn_host_fb_addr(); out->i=0; out->ty=NULL;
    return 1;
  }
  if(nlen==8 && strncmp(name,"fb_width",8)==0){
    if(argc!=0) die("fb_width expects 0 args");
    out->k=V_I64; out->i=tn_host_fb_width(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==9 && strncmp(name,"fb_height",9)==0){
    if(argc!=0) die("fb_height expects 0 args");
    out->k=V_I64; out->i=tn_host_fb_height(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==8 && strncmp(name,"fb_pitch",8)==0){
    if(argc!=0) die("fb_pitch expects 0 args");
    out->k=V_I64; out->i=tn_host_fb_pitch(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==6 && strncmp(name,"fb_bpp",6)==0){
    if(argc!=0) die("fb_bpp expects 0 args");
    out->k=V_I64; out->i=tn_host_fb_bpp(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==9 && strncmp(name,"os_system",9)==0){
    if(argc!=1) die("os_system expects 1 arg");
    out->k=V_I64; out->i=tn_os_system((unsigned char*)args[0].p); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==12 && strncmp(name,"io_mmap_open",12)==0){
    if(argc!=3) die("io_mmap_open expects 3 args");
    unsigned char* path = (unsigned char*)args[0].p;
    long long* out_size = (long long*)args[1].p;
    long long* out_handle = (long long*)args[2].p;
    void* ptr = tn_mmap_file(path, out_size);
    if(out_handle) *out_handle = (ptr != NULL) ? 1 : 0;
    out->k=V_PTR; out->p=ptr; out->i=0; out->ty=NULL;
    return 1;
  }
  if(nlen==13 && strncmp(name,"io_mmap_close",13)==0){
    if(argc!=3) die("io_mmap_close expects 3 args");
    void* ptr = args[0].p;
    long long size = args[1].i;
    long long handle = args[2].i;
    (void)handle;
    tn_munmap_file(ptr, size);
    out->k=V_I64; out->i=0; out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==14 && strncmp(name,"io_pipe_create",14)==0){
    if(argc!=2) die("io_pipe_create expects 2 args");
    long long* read_fd = (long long*)args[0].p;
    long long* write_fd = (long long*)args[1].p;
    out->k=V_I64; out->i=tn_io_pipe_create(read_fd, write_fd); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==10 && strncmp(name,"io_fd_read",10)==0){
    if(argc!=3) die("io_fd_read expects 3 args");
    long long fd = args[0].i;
    unsigned char* buf = (unsigned char*)args[1].p;
    long long n = args[2].i;
    out->k=V_I64; out->i=tn_io_fd_read(fd, buf, n); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==11 && strncmp(name,"io_fd_write",11)==0){
    if(argc!=3) die("io_fd_write expects 3 args");
    long long fd = args[0].i;
    unsigned char* buf = (unsigned char*)args[1].p;
    long long n = args[2].i;
    out->k=V_I64; out->i=tn_io_fd_write(fd, buf, n); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==11 && strncmp(name,"io_fd_close",11)==0){
    if(argc!=1) die("io_fd_close expects 1 arg");
    long long fd = args[0].i;
    out->k=V_I64; out->i=tn_io_fd_close(fd); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==12 && strncmp(name,"screen_width",12)==0){
    if(argc!=0) die("screen_width expects 0 args");
    out->k=V_I64; out->i=tn_host_screen_width(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==13 && strncmp(name,"screen_height",13)==0){
    if(argc!=0) die("screen_height expects 0 args");
    out->k=V_I64; out->i=tn_host_screen_height(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==9 && strncmp(name,"boot_text",9)==0){
    if(argc!=0) die("boot_text expects 0 args");
    out->k=V_I64; out->i=tn_host_boot_text(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==10 && strncmp(name,"fb_present",10)==0){
    if(argc!=0) die("fb_present expects 0 args");
    out->k=V_I64; out->i=tn_host_fb_present(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==7 && strncmp(name,"fb_text",7)==0){
    if(argc!=4) die("fb_text expects 4 args");
    out->k=V_I64;
    out->i=tn_host_fb_text(args[0].i, args[1].i, (const unsigned char*)args[2].p, (unsigned int)args[3].i);
    out->p=NULL;
    out->ty=NULL;
    return 1;
  }
  if(nlen==12 && strncmp(name,"fb_set_scale",12)==0){
    if(argc!=1) die("fb_set_scale expects 1 arg");
    out->k=V_I64; out->i=tn_host_fb_set_scale(args[0].i); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==12 && strncmp(name,"fb_get_scale",12)==0){
    if(argc!=0) die("fb_get_scale expects 0 args");
    out->k=V_I64; out->i=tn_host_fb_get_scale(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==12 && strncmp(name,"fb_text_mode",12)==0){
    if(argc!=1) die("fb_text_mode expects 1 arg");
    out->k=V_I64; out->i=tn_host_fb_text_mode(args[0].i); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==15 && strncmp(name,"fb_font_aa_mode",15)==0){
    if(argc!=1) die("fb_font_aa_mode expects 1 arg");
    out->k=V_I64; out->i=tn_host_fb_font_aa_mode(args[0].i); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==16 && strncmp(name,"fb_font_ttf_mode",16)==0){
    if(argc!=1) die("fb_font_ttf_mode expects 1 arg");
    out->k=V_I64; out->i=tn_host_fb_font_ttf_mode(args[0].i); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==12 && strncmp(name,"fb_draw_logo",12)==0){
    if(argc!=4) die("fb_draw_logo expects 4 args");
    out->k=V_I64; out->i=tn_host_fb_draw_logo(args[0].i, args[1].i, args[2].i, args[3].i); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==17 && strncmp(name,"fb_draw_wallpaper",17)==0){
    if(argc!=4) die("fb_draw_wallpaper expects 4 args");
    out->k=V_I64; out->i=tn_host_fb_draw_wallpaper(args[0].i, args[1].i, args[2].i, args[3].i); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==14 && strncmp(name,"fb_draw_cursor",14)==0){
    if(argc!=3) die("fb_draw_cursor expects 3 args");
    out->k=V_I64; out->i=tn_host_fb_draw_cursor(args[0].i, args[1].i, args[2].i); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==16 && strncmp(name,"fb_draw_app_icon",16)==0){
    if(argc!=5) die("fb_draw_app_icon expects 5 args");
    out->k=V_I64; out->i=tn_host_fb_draw_app_icon(args[0].i, args[1].i, args[2].i, args[3].i, args[4].i); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==7 && strncmp(name,"fb_fill",7)==0){
    if(argc!=1) die("fb_fill expects 1 arg");
    out->k=V_I64; out->i=tn_host_fb_fill((unsigned int)args[0].i); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==12 && strncmp(name,"fb_fill_rect",12)==0){
    if(argc!=5) die("fb_fill_rect expects 5 args");
    out->k=V_I64; out->i=tn_host_fb_fill_rect(args[0].i, args[1].i, args[2].i, args[3].i, (unsigned int)args[4].i); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==7 && strncmp(name,"fb_blit",7)==0){
    if(argc!=6) die("fb_blit expects 6 args");
    out->k=V_I64; out->i=tn_host_fb_blit(args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==17 && strncmp(name,"fb_composite_rect",17)==0){
    if(argc!=6) die("fb_composite_rect expects 6 args");
    out->k=V_I64; out->i=tn_host_fb_composite_rect(args[0].i, args[1].i, args[2].i, args[3].i, (unsigned int)args[4].i, args[5].i); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==12 && strncmp(name,"fb_get_pixel",12)==0){
    if(argc!=2) die("fb_get_pixel expects 2 args");
    out->k=V_I64; out->i=tn_host_fb_get_pixel(args[0].i, args[1].i); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==12 && strncmp(name,"fb_put_pixel",12)==0){
    if(argc!=3) die("fb_put_pixel expects 3 args");
    out->k=V_I64; out->i=tn_host_fb_put_pixel(args[0].i, args[1].i, (unsigned int)args[2].i); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==20 && strncmp(name,"fb_cursor_soft_reset",20)==0){
    if(argc!=0) die("fb_cursor_soft_reset expects 0 args");
    out->k=V_I64; out->i=tn_host_fb_cursor_soft_reset(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==19 && strncmp(name,"fb_cursor_soft_move",19)==0){
    if(argc!=3) die("fb_cursor_soft_move expects 3 args");
    out->k=V_I64; out->i=tn_host_fb_cursor_soft_move(args[0].i, args[1].i, args[2].i); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==14 && strncmp(name,"fb_cursor_kind",14)==0){
    if(argc!=1) die("fb_cursor_kind expects 1 arg");
    out->k=V_I64; out->i=tn_host_fb_cursor_kind(args[0].i); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==12 && strncmp(name,"fb_set_color",12)==0){
    if(argc!=1) die("fb_set_color expects 1 arg");
    out->k=V_I64; out->i=tn_host_fb_set_color((unsigned int)args[0].i); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==13 && strncmp(name,"fb_set_cursor",13)==0){
    if(argc!=2) die("fb_set_cursor expects 2 args");
    out->k=V_I64; out->i=tn_host_fb_set_cursor(args[0].i, args[1].i); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==14 && strncmp(name,"fb_cursor_mode",14)==0){
    if(argc!=1) die("fb_cursor_mode expects 1 arg");
    out->k=V_I64; out->i=tn_host_fb_cursor_mode(args[0].i); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==9 && strncmp(name,"fb_cursor",9)==0){
    if(argc!=2) die("fb_cursor expects 2 args");
    out->k=V_I64; out->i=tn_host_fb_cursor(args[0].i, (unsigned int)args[1].i); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==17 && strncmp(name,"fb_host_windowing",17)==0){
    if(argc!=0) die("fb_host_windowing expects 0 args");
    out->k=V_I64; out->i=tn_host_fb_host_windowing(); out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* keyboard + mouse polling */
  if(nlen==13 && strncmp(name,"kbd_has_event",13)==0){
    if(argc!=0) die("kbd_has_event expects 0 args");
    out->k=V_I64; out->i=tn_host_kbd_has_event(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==17 && strncmp(name,"kbd_read_scancode",17)==0){
    if(argc!=0) die("kbd_read_scancode expects 0 args");
    out->k=V_I64; out->i=tn_host_kbd_read_scancode(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==21 && strncmp(name,"kbd_read_scancode_raw",21)==0){
    if(argc!=0) die("kbd_read_scancode_raw expects 0 args");
    out->k=V_I64; out->i=tn_host_kbd_read_scancode_raw(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==13 && strncmp(name,"kbd_read_char",13)==0){
    if(argc!=0) die("kbd_read_char expects 0 args");
    out->k=V_I64; out->i=tn_host_kbd_read_char(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==14 && strncmp(name,"kbd_ime_active",14)==0){
    if(argc!=0) die("kbd_ime_active expects 0 args");
    out->k=V_I64; out->i=tn_host_kbd_ime_active(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==14 && strncmp(name,"kbd_ime_cursor",14)==0){
    if(argc!=0) die("kbd_ime_cursor expects 0 args");
    out->k=V_I64; out->i=tn_host_kbd_ime_cursor(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==14 && strncmp(name,"kbd_ime_length",14)==0){
    if(argc!=0) die("kbd_ime_length expects 0 args");
    out->k=V_I64; out->i=tn_host_kbd_ime_length(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==13 && strncmp(name,"kbd_set_debug",13)==0){
    if(argc!=1) die("kbd_set_debug expects 1 arg");
    out->k=V_I64; out->i=tn_host_kbd_set_debug(args[0].i); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==17 && strncmp(name,"kbd_last_scancode",17)==0){
    if(argc!=0) die("kbd_last_scancode expects 0 args");
    out->k=V_I64; out->i=tn_host_kbd_last_scancode(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==16 && strncmp(name,"mouse_has_packet",16)==0){
    if(argc!=0) die("mouse_has_packet expects 0 args");
    out->k=V_I64; out->i=tn_host_mouse_has_packet(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==17 && strncmp(name,"mouse_read_packet",17)==0){
    if(argc!=0) die("mouse_read_packet expects 0 args");
    out->k=V_I64; out->i=tn_host_mouse_read_packet(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==8 && strncmp(name,"mouse_dx",8)==0){
    if(argc!=0) die("mouse_dx expects 0 args");
    out->k=V_I64; out->i=tn_host_mouse_dx(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==8 && strncmp(name,"mouse_dy",8)==0){
    if(argc!=0) die("mouse_dy expects 0 args");
    out->k=V_I64; out->i=tn_host_mouse_dy(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==13 && strncmp(name,"mouse_buttons",13)==0){
    if(argc!=0) die("mouse_buttons expects 0 args");
    out->k=V_I64; out->i=tn_host_mouse_buttons(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==11 && strncmp(name,"mouse_pos_x",11)==0){
    if(argc!=0) die("mouse_pos_x expects 0 args");
    out->k=V_I64; out->i=tn_host_mouse_pos_x(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==11 && strncmp(name,"mouse_pos_y",11)==0){
    if(argc!=0) die("mouse_pos_y expects 0 args");
    out->k=V_I64; out->i=tn_host_mouse_pos_y(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==10 && strncmp(name,"fb_text_ex",10)==0){
    if(argc!=6) die("fb_text_ex expects 6 args");
    out->k=V_I64;
    out->i=tn_host_fb_text_ex(args[0].i, args[1].i,
                               (const unsigned char*)args[2].p,
                               (unsigned int)args[3].i,
                               args[4].i, args[5].i);
    out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==13 && strncmp(name,"fb_soft_cursor",14)==0){ /* note: length 14 */
    out->k=V_I64; out->p=NULL; out->ty=NULL;
    out->i=-1; return 1; /* skip wrong len */
  }
  if(nlen==14 && strncmp(name,"fb_soft_cursor",14)==0){
    if(argc!=3) die("fb_soft_cursor expects 3 args");
    out->k=V_I64;
    out->i=tn_host_fb_soft_cursor(args[0].i, args[1].i, (unsigned int)args[2].i);
    out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==15 && strncmp(name,"fb_fill_rounded",15)==0){
    if(argc!=6) die("fb_fill_rounded expects 6 args");
    out->k=V_I64;
    out->i=tn_host_fb_fill_rounded(args[0].i,args[1].i,args[2].i,args[3].i,
                                    (unsigned int)args[4].i, args[5].i);
    out->p=NULL; out->ty=NULL;
    return 1;
  }

  if(nlen==10 && strncmp(name,"fb_init_ex",10)==0){
    if(argc!=3) die("fb_init_ex expects 3 args: title, W, H");
    out->k=V_I64;
    out->i=tn_host_fb_init_ex((const char*)args[0].p, args[1].i, args[2].i);
    out->p=NULL; out->ty=NULL;
    return 1;
  }

  if(nlen==19 && strncmp(name,"tz_event_queue_init",19)==0){
    if(argc!=1) die("tz_event_queue_init expects 1 arg");
    out->k=V_I64; out->i=tn_vm_event_queue_init(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==20 && strncmp(name,"tz_event_queue_clear",20)==0){
    if(argc!=1) die("tz_event_queue_clear expects 1 arg");
    out->k=V_I64; out->i=tn_vm_event_queue_clear(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==18 && strncmp(name,"tz_event_queue_len",18)==0){
    if(argc!=1) die("tz_event_queue_len expects 1 arg");
    out->k=V_I64; out->i=(long long)g_tn_vm_event_len; out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==22 && strncmp(name,"tz_event_queue_dropped",22)==0){
    if(argc!=1) die("tz_event_queue_dropped expects 1 arg");
    out->k=V_I64; out->i=(long long)g_tn_vm_event_dropped; out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==13 && strncmp(name,"tz_event_push",13)==0){
    if(argc!=8) die("tz_event_push expects 8 args");
    out->k=V_I64;
    out->i=tn_vm_event_push(args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].i, args[7].i);
    out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==17 && strncmp(name,"tz_event_push_key",17)==0){
    if(argc!=2) die("tz_event_push_key expects 2 args");
    out->k=V_I64;
    out->i=tn_vm_event_push(1, args[1].i, 0, 0, 0, 0, 0);
    out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==21 && strncmp(name,"tz_event_push_pointer",21)==0){
    if(argc!=5) die("tz_event_push_pointer expects 5 args");
    out->k=V_I64;
    out->i=tn_vm_event_push(2, 0, args[1].i, args[2].i, args[3].i, args[4].i, 0);
    out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==20 && strncmp(name,"tz_event_push_button",20)==0){
    if(argc!=5) die("tz_event_push_button expects 5 args");
    out->k=V_I64;
    out->i=tn_vm_event_push(3, 0, args[1].i, args[2].i, args[3].i, args[4].i, 0);
    out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==12 && strncmp(name,"tz_event_pop",12)==0){
    if(argc!=2) die("tz_event_pop expects 2 args");
    out->k=V_I64; out->i=tn_vm_event_pop((long long*)args[1].p, 0); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==13 && strncmp(name,"tz_event_peek",13)==0){
    if(argc!=2) die("tz_event_peek expects 2 args");
    out->k=V_I64; out->i=tn_vm_event_pop((long long*)args[1].p, 1); out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* list_dir_recursive(path:*u8)->*u8 */
  if(nlen==18 && strncmp(name,"list_dir_recursive",18)==0){
    if(argc!=1) die("list_dir_recursive expects 1 arg");
    const char* p = (const char*)args[0].p;
    out->k=V_PTR; out->p=list_dir_recursive_impl(p); out->i=0; out->ty=NULL;
    return 1;
  }

  /* glob(pattern:*u8)->*u8 */
  if(nlen==4 && strncmp(name,"glob",4)==0){
    if(argc!=1) die("glob expects 1 arg");
    const char* p = (const char*)args[0].p;
    out->k=V_PTR; out->p=glob_impl(p); out->i=0; out->ty=NULL;
    return 1;
  }

  /* path_normalize(path:*u8)->*u8 */
  if(nlen==14 && strncmp(name,"path_normalize",14)==0){
    if(argc!=1) die("path_normalize expects 1 arg");
    const char* p = (const char*)args[0].p;
    out->k=V_PTR; out->p=path_normalize_impl(p); out->i=0; out->ty=NULL;
    return 1;
  }

  /* path_normalize_opts(path:*u8, flags:i64)->*u8 */
  if(nlen==19 && strncmp(name,"path_normalize_opts",19)==0){
    if(argc!=2) die("path_normalize_opts expects 2 args");
    const char* p = (const char*)args[0].p;
    long long flags = args[1].i;
    out->k=V_PTR; out->p=path_normalize_opts_impl(p, flags); out->i=0; out->ty=NULL;
    return 1;
  }

  /* time_now()->i64 (seconds since epoch) */
  if(nlen==8 && strncmp(name,"time_now",8)==0){
    if(argc!=0) die("time_now expects 0 args");
    long long ns = time_now_ns();
    out->k=V_I64; out->i=ns/1000000000LL; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* time_now_ms()->i64 */
  if(nlen==11 && strncmp(name,"time_now_ms",11)==0){
    if(argc!=0) die("time_now_ms expects 0 args");
    long long ns = time_now_ns();
    out->k=V_I64; out->i=ns/1000000LL; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* time_now_ns()->i64 */
  if(nlen==11 && strncmp(name,"time_now_ns",11)==0){
    if(argc!=0) die("time_now_ns expects 0 args");
    long long ns = time_now_ns();
    out->k=V_I64; out->i=ns; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* date_now()->*u8 (local time) */
  if(nlen==8 && strncmp(name,"date_now",8)==0){
    if(argc!=0) die("date_now expects 0 args");
    out->k=V_PTR; out->p=date_now_impl(0); out->i=0; out->ty=NULL;
    return 1;
  }

  /* date_now_utc()->*u8 (UTC time) */
  if(nlen==12 && strncmp(name,"date_now_utc",12)==0){
    if(argc!=0) die("date_now_utc expects 0 args");
    out->k=V_PTR; out->p=date_now_impl(1); out->i=0; out->ty=NULL;
    return 1;
  }

  /* sleep_ms(ms:i64)->i64 */
  if(nlen==8 && strncmp(name,"sleep_ms",8)==0){
    if(argc!=1) die("sleep_ms expects 1 arg");
    long long ms = args[0].i;
    if(ms < 0) ms = 0;
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)(ms * 1000));
#endif
    out->k=V_I64; out->i=0; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* net constants */
  if(nlen==11 && strncmp(name,"net_af_inet",11)==0){
    if(argc!=0) die("net_af_inet expects 0 args");
    out->k=V_I64; out->i=(long long)AF_INET; out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==12 && strncmp(name,"net_af_inet6",12)==0){
    if(argc!=0) die("net_af_inet6 expects 0 args");
    out->k=V_I64; out->i=(long long)AF_INET6; out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==15 && strncmp(name,"net_sock_stream",15)==0){
    if(argc!=0) die("net_sock_stream expects 0 args");
    out->k=V_I64; out->i=(long long)SOCK_STREAM; out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==14 && strncmp(name,"net_sock_dgram",14)==0){
    if(argc!=0) die("net_sock_dgram expects 0 args");
    out->k=V_I64; out->i=(long long)SOCK_DGRAM; out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==15 && strncmp(name,"net_ipproto_tcp",15)==0){
    if(argc!=0) die("net_ipproto_tcp expects 0 args");
    out->k=V_I64; out->i=(long long)IPPROTO_TCP; out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==15 && strncmp(name,"net_ipproto_udp",15)==0){
    if(argc!=0) die("net_ipproto_udp expects 0 args");
    out->k=V_I64; out->i=(long long)IPPROTO_UDP; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* net init/cleanup */
  if(nlen==8 && strncmp(name,"net_init",8)==0){
    if(argc!=0) die("net_init expects 0 args");
    out->k=V_I64; out->i=(long long)net_init_impl(); out->p=NULL; out->ty=NULL;
    return 1;
  }
  if(nlen==11 && strncmp(name,"net_cleanup",11)==0){
    if(argc!=0) die("net_cleanup expects 0 args");
    out->k=V_I64; out->i=(long long)net_cleanup_impl(); out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* net_socket(family, type, proto) -> i64 */
  if(nlen==10 && strncmp(name,"net_socket",10)==0){
    if(argc!=3) die("net_socket expects 3 args");
    if(net_init_impl() != 0){
      out->k=V_I64; out->i=-1; out->p=NULL; out->ty=NULL;
      return 1;
    }
    tn_sock_t s = (tn_sock_t)socket((int)args[0].i, (int)args[1].i, (int)args[2].i);
    out->k=V_I64; out->i=(long long)net_sock_to_ll(s); out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* net_close(sock) -> i64 */
  if(nlen==9 && strncmp(name,"net_close",9)==0){
    if(argc!=1) die("net_close expects 1 arg");
    tn_sock_t s = net_sock_from_ll(args[0].i);
#ifdef _WIN32
    int r = (s==TN_INVALID_SOCKET) ? -1 : closesocket(s);
#else
    int r = (s==TN_INVALID_SOCKET) ? -1 : close(s);
#endif
    out->k=V_I64; out->i=(long long)r; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* net_connect(sock, host, port) -> i64 */
  if(nlen==11 && strncmp(name,"net_connect",11)==0){
    if(argc!=3) die("net_connect expects 3 args");
    tn_sock_t s = net_sock_from_ll(args[0].i);
    const char* host = (const char*)args[1].p;
    long long port = args[2].i;
    out->k=V_I64; out->i=(long long)net_connect_impl(s, host, port); out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* net_bind(sock, host, port) -> i64 */
  if(nlen==8 && strncmp(name,"net_bind",8)==0){
    if(argc!=3) die("net_bind expects 3 args");
    tn_sock_t s = net_sock_from_ll(args[0].i);
    const char* host = (const char*)args[1].p;
    long long port = args[2].i;
    out->k=V_I64; out->i=(long long)net_bind_impl(s, host, port); out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* net_listen(sock, backlog) -> i64 */
  if(nlen==10 && strncmp(name,"net_listen",10)==0){
    if(argc!=2) die("net_listen expects 2 args");
    tn_sock_t s = net_sock_from_ll(args[0].i);
    int r = listen(s, (int)args[1].i);
    out->k=V_I64; out->i=(long long)r; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* net_accept(sock) -> i64 */
  if(nlen==10 && strncmp(name,"net_accept",10)==0){
    if(argc!=1) die("net_accept expects 1 arg");
    tn_sock_t s = net_sock_from_ll(args[0].i);
    tn_sock_t c = (tn_sock_t)accept(s, NULL, NULL);
    long long rv = (c==TN_INVALID_SOCKET) ? -1 : net_sock_to_ll(c);
    out->k=V_I64; out->i=rv; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* net_send(sock, buf, len) -> i64 */
  if(nlen==8 && strncmp(name,"net_send",8)==0){
    if(argc!=3) die("net_send expects 3 args");
    tn_sock_t s = net_sock_from_ll(args[0].i);
    const char* buf = (const char*)args[1].p;
    int len = (int)args[2].i;
    int r = (buf && len>0) ? (int)send(s, buf, len, TN_SEND_FLAGS) : 0;
    if(r < 0) r = -1;
    out->k=V_I64; out->i=(long long)r; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* net_recv(sock, buf, len) -> i64 */
  if(nlen==8 && strncmp(name,"net_recv",8)==0){
    if(argc!=3) die("net_recv expects 3 args");
    tn_sock_t s = net_sock_from_ll(args[0].i);
    char* buf = (char*)args[1].p;
    int len = (int)args[2].i;
    int r = (buf && len>0) ? (int)recv(s, buf, len, 0) : 0;
    if(r < 0) r = -1;
    out->k=V_I64; out->i=(long long)r; out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* net_set_blocking(sock, on) -> i64 */
  if(nlen==16 && strncmp(name,"net_set_blocking",16)==0){
    if(argc!=2) die("net_set_blocking expects 2 args");
    tn_sock_t s = net_sock_from_ll(args[0].i);
    out->k=V_I64; out->i=(long long)net_set_blocking_impl(s, (int)args[1].i); out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* net_set_timeout(sock, ms) -> i64 */
  if(nlen==15 && strncmp(name,"net_set_timeout",15)==0){
    if(argc!=2) die("net_set_timeout expects 2 args");
    tn_sock_t s = net_sock_from_ll(args[0].i);
    out->k=V_I64; out->i=(long long)net_set_timeout_impl(s, args[1].i); out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* net_last_error() -> i64 */
  if(nlen==14 && strncmp(name,"net_last_error",14)==0){
    if(argc!=0) die("net_last_error expects 0 args");
    out->k=V_I64; out->i=net_last_error_impl(); out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* net_resolve(host, port) -> *u8 */
  if(nlen==11 && strncmp(name,"net_resolve",11)==0){
    if(argc!=2) die("net_resolve expects 2 args");
    const char* host = (const char*)args[0].p;
    long long port = args[1].i;
    out->k=V_PTR; out->p=net_resolve_impl(host, port); out->i=0; out->ty=NULL;
    return 1;
  }

  /* tls_connect(host, port) -> i64 (handle) */
  if(nlen==11 && strncmp(name,"tls_connect",11)==0){
    if(argc!=2) die("tls_connect expects 2 args");
#ifdef _WIN32
    const char* host = (const char*)args[0].p;
    long long port = args[1].i;
    tn_tls_vm* t = tn_tls_connect_vm(host, port);
    out->k=V_I64; out->i=t ? (long long)(intptr_t)t : -1; out->p=NULL; out->ty=NULL;
#else
  #if defined(TN_TLS_OPENSSL)
    const char* host = (const char*)args[0].p;
    long long port = args[1].i;
    tn_tls_ossl_vm* t = tn_tls_connect_ossl_vm(host, port);
    out->k=V_I64; out->i=t ? (long long)(intptr_t)t : -1; out->p=NULL; out->ty=NULL;
  #else
    out->k=V_I64; out->i=-2; out->p=NULL; out->ty=NULL;
  #endif
#endif
    return 1;
  }

  /* tls_connect_ex(host, port, min_version, handshake_timeout_ms, pin_sha256_hex) -> i64 */
  if(nlen==14 && strncmp(name,"tls_connect_ex",14)==0){
    if(argc!=5) die("tls_connect_ex expects 5 args");
    const char* host = (const char*)args[0].p;
    long long port = args[1].i;
    long long min_version = args[2].i;
    long long timeout_ms = args[3].i;
    const char* pin_hex = (const char*)args[4].p;
#if !defined(_WIN32) && !defined(TN_TLS_OPENSSL)
    (void)host;
    (void)port;
#endif
    tn_tls_policy_cfg_vm req;
    if(tls_policy_build_request_vm(min_version, timeout_ms, pin_hex, &req) != 0){
      out->k=V_I64; out->i=-1; out->p=NULL; out->ty=NULL;
      return 1;
    }
#ifdef _WIN32
    tn_tls_vm* t = tn_tls_connect_with_policy_vm(host, port, &req);
    out->k=V_I64; out->i=t ? (long long)(intptr_t)t : -1; out->p=NULL; out->ty=NULL;
#else
  #if defined(TN_TLS_OPENSSL)
    tn_tls_ossl_vm* t = tn_tls_connect_ossl_with_policy_vm(host, port, &req);
    out->k=V_I64; out->i=t ? (long long)(intptr_t)t : -1; out->p=NULL; out->ty=NULL;
  #else
    out->k=V_I64; out->i=-2; out->p=NULL; out->ty=NULL;
  #endif
#endif
    return 1;
  }

  /* tls_send(handle, buf, len) -> i64 */
  if(nlen==8 && strncmp(name,"tls_send",8)==0){
    if(argc!=3) die("tls_send expects 3 args");
#ifdef _WIN32
    tn_tls_vm* t = (tn_tls_vm*)(intptr_t)args[0].i;
    const unsigned char* buf = (const unsigned char*)args[1].p;
    long long len = args[2].i;
    out->k=V_I64; out->i=tn_tls_send_vm(t, buf, len); out->p=NULL; out->ty=NULL;
#else
  #if defined(TN_TLS_OPENSSL)
    tn_tls_ossl_vm* t = (tn_tls_ossl_vm*)(intptr_t)args[0].i;
    const unsigned char* buf = (const unsigned char*)args[1].p;
    long long len = args[2].i;
    out->k=V_I64; out->i=tn_tls_send_ossl_vm(t, buf, len); out->p=NULL; out->ty=NULL;
  #else
    out->k=V_I64; out->i=-2; out->p=NULL; out->ty=NULL;
  #endif
#endif
    return 1;
  }

  /* tls_recv(handle, buf, len) -> i64 */
  if(nlen==8 && strncmp(name,"tls_recv",8)==0){
    if(argc!=3) die("tls_recv expects 3 args");
#ifdef _WIN32
    tn_tls_vm* t = (tn_tls_vm*)(intptr_t)args[0].i;
    unsigned char* buf = (unsigned char*)args[1].p;
    long long len = args[2].i;
    out->k=V_I64; out->i=tn_tls_recv_vm(t, buf, len); out->p=NULL; out->ty=NULL;
#else
  #if defined(TN_TLS_OPENSSL)
    tn_tls_ossl_vm* t = (tn_tls_ossl_vm*)(intptr_t)args[0].i;
    unsigned char* buf = (unsigned char*)args[1].p;
    long long len = args[2].i;
    out->k=V_I64; out->i=tn_tls_recv_ossl_vm(t, buf, len); out->p=NULL; out->ty=NULL;
  #else
    out->k=V_I64; out->i=-2; out->p=NULL; out->ty=NULL;
  #endif
#endif
    return 1;
  }

  /* tls_close(handle) -> i64 */
  if(nlen==9 && strncmp(name,"tls_close",9)==0){
    if(argc!=1) die("tls_close expects 1 arg");
#ifdef _WIN32
    tn_tls_vm* t = (tn_tls_vm*)(intptr_t)args[0].i;
    tn_tls_free_vm(t);
    out->k=V_I64; out->i=0; out->p=NULL; out->ty=NULL;
#else
  #if defined(TN_TLS_OPENSSL)
    tn_tls_ossl_vm* t = (tn_tls_ossl_vm*)(intptr_t)args[0].i;
    tn_tls_free_ossl_vm(t);
    out->k=V_I64; out->i=0; out->p=NULL; out->ty=NULL;
  #else
    out->k=V_I64; out->i=-2; out->p=NULL; out->ty=NULL;
  #endif
#endif
    return 1;
  }

  /* tls_last_error()->i64 */
  if(nlen==14 && strncmp(name,"tls_last_error",14)==0){
    if(argc!=0) die("tls_last_error expects 0 args");
    out->k=V_I64; out->i=tls_last_error_impl(); out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* tls_policy_reset()->i64 */
  if(nlen==16 && strncmp(name,"tls_policy_reset",16)==0){
    if(argc!=0) die("tls_policy_reset expects 0 args");
    out->k=V_I64; out->i=(long long)tls_policy_reset_impl(); out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* tls_policy_set_min(min)->i64 */
  if(nlen==18 && strncmp(name,"tls_policy_set_min",18)==0){
    if(argc!=1) die("tls_policy_set_min expects 1 arg");
    out->k=V_I64; out->i=(long long)tls_policy_set_min_impl(args[0].i); out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* tls_policy_set_pin_sha256(hex)->i64 */
  if(nlen==25 && strncmp(name,"tls_policy_set_pin_sha256",25)==0){
    if(argc!=1) die("tls_policy_set_pin_sha256 expects 1 arg");
    out->k=V_I64; out->i=(long long)tls_policy_set_pin_sha256_impl((const char*)args[0].p); out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* tls_policy_set_handshake_timeout(ms)->i64 */
  if(nlen==32 && strncmp(name,"tls_policy_set_handshake_timeout",32)==0){
    if(argc!=1) die("tls_policy_set_handshake_timeout expects 1 arg");
    out->k=V_I64; out->i=(long long)tls_policy_set_handshake_timeout_impl(args[0].i); out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* tls_policy_get_min()->i64 */
  if(nlen==18 && strncmp(name,"tls_policy_get_min",18)==0){
    if(argc!=0) die("tls_policy_get_min expects 0 args");
    out->k=V_I64; out->i=tls_policy_get_min_impl(); out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* tls_policy_get_handshake_timeout()->i64 */
  if(nlen==32 && strncmp(name,"tls_policy_get_handshake_timeout",32)==0){
    if(argc!=0) die("tls_policy_get_handshake_timeout expects 0 args");
    out->k=V_I64; out->i=tls_policy_get_handshake_timeout_impl(); out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* tls_policy_pin_enabled()->i64 */
  if(nlen==22 && strncmp(name,"tls_policy_pin_enabled",22)==0){
    if(argc!=0) die("tls_policy_pin_enabled expects 0 args");
    out->k=V_I64; out->i=tls_policy_pin_enabled_impl(); out->p=NULL; out->ty=NULL;
    return 1;
  }

  /* tn_tzimage_decode_file(path, req_channels, out_w, out_h, out_channels) -> *u8 */
  if(nlen==22 && strncmp(name,"tn_tzimage_decode_file",22)==0){
    if(argc!=5) die("tn_tzimage_decode_file expects 5 args");
    unsigned char* path = (unsigned char*)args[0].p;
    long long req_channels = args[1].i;
    long long* out_w = (long long*)args[2].p;
    long long* out_h = (long long*)args[3].p;
    long long* out_channels = (long long*)args[4].p;
    unsigned char* decoded = tzimage_decode_file_vm(path, req_channels, out_w, out_h, out_channels);
    out->k=V_PTR; out->p=decoded; out->i=0; out->ty=NULL;
    return 1;
  }

  /* tn_tzimage_free(ptr) -> i64 */
  if(nlen==15 && strncmp(name,"tn_tzimage_free",15)==0){
    if(argc!=1) die("tn_tzimage_free expects 1 arg");
    out->k=V_I64; out->i=tzimage_free_vm((unsigned char*)args[0].p); out->p=NULL; out->ty=NULL;
    return 1;
  }

  return 0;
}
