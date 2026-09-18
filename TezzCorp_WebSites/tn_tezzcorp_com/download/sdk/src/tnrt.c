// src/tnrt.c  (TezzNative runtime for Win64 / MinGW)
// Exports: say, say_str, say_f, len, tn_strcmp, input_i64, input_line,
//          tn_fopen/tn_fclose/tn_fflush/tn_fread/tn_fwrite/tn_fseek/tn_ftell,
//          tn_file_size/tn_read_file/tn_write_file,
//          tn_read_line/tn_read_bytes/tn_write_line, tn_io_err/tn_io_eof,
//          tn_stdin/tn_stdout/tn_stderr, tn_get_cwd/tn_chdir/tn_mkdir/tn_rmdir,
//          tn_remove/tn_rename/tn_path_exists/tn_path_is_dir/tn_path_join,
//          tn_path_sep/tn_path_basename/tn_path_dirname/tn_list_dir/tn_os_name,
//          tn_list_dir_recursive/tn_glob/tn_path_normalize/tn_path_normalize_opts,
//          tn_time_now/tn_time_now_ms/tn_time_now_ns,
//          tn_date_now/tn_date_now_utc,
//          tn_bit_popcnt/tn_bit_clz/tn_bit_ctz/tn_bit_bswap/tn_bit_rotl/tn_bit_rotr,
//          tn_cpu_has_sse2/tn_cpu_has_avx2/tn_cpu_has_fma/tn_cpu_has_neon,
//          tn_simd_v4f_add/tn_simd_v4f_sub/tn_simd_v4f_mul/tn_simd_v4f_dot,
//          tn_simd_v4i_add/tn_simd_v4f_load/tn_simd_v4f_store,
//          tn_simd_v4i_load/tn_simd_v4i_store,
//          tn_gpu_* (stub GPU hooks)
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>
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

#if (defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64))
  #if defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER)
    #include <immintrin.h>
  #endif
#endif

#ifdef TN_GUARD_ALLOC
#include "tn_guard_alloc.h"
#define malloc  tn_guard_malloc
#define free    tn_guard_free
#define realloc tn_guard_realloc
#endif

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wundef"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_FAILURE_STRINGS
#define STBI_NO_PIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "host_gui_backend.h"

#if !defined(_WIN32) && !defined(TN_SOCK_T_DEFINED)
typedef int tn_sock_t;
#define TN_INVALID_SOCKET (-1)
#define TN_SOCK_T_DEFINED 1
#endif
static int tn_net_init_impl(void);
#ifndef _WIN32
static int tn_net_set_timeout_impl(tn_sock_t s, long long ms);
#endif

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

static int tn_ai_validate_matmul_shape(long long m, long long n, long long k){
  long long mn = 0;
  long long mk = 0;
  long long kn = 0;
  size_t bytes = 0;
  if(m <= 0 || n <= 0 || k <= 0) return 0;
  if(tn_mul_overflow_i64_pos(m, n, &mn)) return 0;
  if(tn_mul_overflow_i64_pos(m, k, &mk)) return 0;
  if(tn_mul_overflow_i64_pos(k, n, &kn)) return 0;
  if(tn_mul_overflow_size(mn, (long long)sizeof(float), &bytes)) return 0;
  if(tn_mul_overflow_size(mk, (long long)sizeof(float), &bytes)) return 0;
  if(tn_mul_overflow_size(kn, (long long)sizeof(float), &bytes)) return 0;
  return 1;
}

static int tn_ai_validate_matmul_shape_elem(long long m, long long n, long long k, long long elem_size){
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

static int tn_ai_validate_matmul_i8_f64_shape(long long m, long long n, long long k){
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

static int tn_ai_validate_rmsnorm_shape(long long rows, long long cols){
  long long total = 0;
  size_t bytes = 0;
  if(rows <= 0 || cols <= 0) return 0;
  if(tn_mul_overflow_i64_pos(rows, cols, &total)) return 0;
  if(tn_mul_overflow_size(total, (long long)sizeof(float), &bytes)) return 0;
  if(tn_mul_overflow_size(cols, (long long)sizeof(float), &bytes)) return 0;
  return 1;
}

static int tn_ai_validate_rows_cols_shape_elem(long long rows, long long cols, long long elem_size){
  long long total = 0;
  size_t bytes = 0;
  if(rows <= 0 || cols <= 0 || elem_size <= 0) return 0;
  if(tn_mul_overflow_i64_pos(rows, cols, &total)) return 0;
  if(tn_mul_overflow_size(total, elem_size, &bytes)) return 0;
  if(tn_mul_overflow_size(cols, elem_size, &bytes)) return 0;
  return 1;
}

static float tn_ai_norm_eps(float eps){
  if(eps != eps) return 1.0e-5f;
  if(eps <= 0.0f) return 1.0e-5f;
  return eps;
}

static double tn_ai_norm_eps_f64(double eps){
  if(eps != eps) return 1.0e-5;
  if(eps <= 0.0) return 1.0e-5;
  return eps;
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
  if((ecx_leaf1 & (1u << 27)) == 0u) return 0;
  if((ecx_leaf1 & (1u << 28)) == 0u) return 0;
  unsigned long long xcr0 = tn_cpu_xgetbv(0);
  return ((xcr0 & 0x6ull) == 0x6ull) ? 1 : 0;
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
  if((c & (1u << 12)) == 0u) return 0;
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

typedef struct tn_tls_policy_cfg {
  int min_version;               // 0=default, 12=TLS1.2+, 13=TLS1.3-only
  long long handshake_timeout_ms; // 0=runtime default
  int pin_enabled;               // 1 when pin_sha256 is set
  unsigned char pin_sha256[32];  // SHA-256 cert fingerprint bytes
} tn_tls_policy_cfg;

static tn_tls_policy_cfg g_tn_tls_policy = {0, 0, 0, {0}};

static int tn_tls_hex_nibble(int c){
  if(c >= '0' && c <= '9') return c - '0';
  if(c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if(c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

static int tn_tls_parse_sha256_hex(const char* hex, unsigned char out[32]){
  if(!hex || !out) return -1;
  for(int i=0;i<64;i++){
    if(hex[i] == 0) return -1;
  }
  if(hex[64] != 0) return -1;
  for(int i=0;i<32;i++){
    int hi = tn_tls_hex_nibble((unsigned char)hex[i * 2]);
    int lo = tn_tls_hex_nibble((unsigned char)hex[i * 2 + 1]);
    if(hi < 0 || lo < 0) return -1;
    out[i] = (unsigned char)((hi << 4) | lo);
  }
  return 0;
}

static int tn_tls_policy_reset_impl(void){
  g_tn_tls_policy.min_version = 0;
  g_tn_tls_policy.handshake_timeout_ms = 0;
  g_tn_tls_policy.pin_enabled = 0;
  memset(g_tn_tls_policy.pin_sha256, 0, sizeof(g_tn_tls_policy.pin_sha256));
  return 0;
}

static int tn_tls_policy_set_min_impl(long long min_version){
  if(min_version != 0 && min_version != 12 && min_version != 13) return -1;
  g_tn_tls_policy.min_version = (int)min_version;
  return 0;
}

static int tn_tls_policy_set_handshake_timeout_impl(long long timeout_ms){
  if(timeout_ms < 0) return -1;
  g_tn_tls_policy.handshake_timeout_ms = timeout_ms;
  return 0;
}

static int tn_tls_policy_set_pin_sha256_impl(const char* hex){
  if(!hex || !hex[0]){
    g_tn_tls_policy.pin_enabled = 0;
    memset(g_tn_tls_policy.pin_sha256, 0, sizeof(g_tn_tls_policy.pin_sha256));
    return 0;
  }
  unsigned char parsed[32];
  if(tn_tls_parse_sha256_hex(hex, parsed) != 0) return -1;
  memcpy(g_tn_tls_policy.pin_sha256, parsed, 32);
  g_tn_tls_policy.pin_enabled = 1;
  return 0;
}

static long long tn_tls_policy_get_min_impl(void){
  return (long long)g_tn_tls_policy.min_version;
}

static long long tn_tls_policy_get_handshake_timeout_impl(void){
  return g_tn_tls_policy.handshake_timeout_ms;
}

static long long tn_tls_policy_pin_enabled_impl(void){
  return (long long)g_tn_tls_policy.pin_enabled;
}

static int tn_tls_policy_build_request(long long min_version,
                                       long long handshake_timeout_ms,
                                       const char* pin_hex,
                                       tn_tls_policy_cfg* out){
  if(!out) return -1;
  if(min_version != 0 && min_version != 12 && min_version != 13) return -1;
  if(handshake_timeout_ms < 0) return -1;
  memset(out, 0, sizeof(*out));
  out->min_version = (int)min_version;
  out->handshake_timeout_ms = handshake_timeout_ms;
  if(pin_hex && pin_hex[0]){
    if(tn_tls_parse_sha256_hex(pin_hex, out->pin_sha256) != 0) return -1;
    out->pin_enabled = 1;
  }
  return 0;
}

#if defined(_WIN32) && defined(TN_GPU_DIRECTML)
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <directml.h>
#endif

#if defined(TN_NPU_ONNXRUNTIME)
#include <onnxruntime_c_api.h>
#if defined(USE_OPENVINO)
#include <onnxruntime_provider_factory.h>
#endif
#endif

#if defined(TN_TLS_OPENSSL) && !defined(_WIN32)
typedef struct tn_tls_ossl {
  int sock;
  SSL_CTX* ctx;
  SSL* ssl;
} tn_tls_ossl;

static void tn_tls_free_ossl(tn_tls_ossl* t);

static int tn_tcp_connect_posix(const char* host, long long port){
  if(tn_net_init_impl() != 0) return -1;
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

static int tn_tls_verify_pin_ossl(tn_tls_ossl* t, const tn_tls_policy_cfg* policy){
  const tn_tls_policy_cfg* pol = policy ? policy : &g_tn_tls_policy;
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

static tn_tls_ossl* tn_tls_connect_ossl_policy(const char* host, long long port, const tn_tls_policy_cfg* policy){
  const tn_tls_policy_cfg* pol = policy ? policy : &g_tn_tls_policy;
  SSL_library_init();
  SSL_load_error_strings();
  OpenSSL_add_all_algorithms();

  SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
  if(!ctx) return NULL;

  if(pol->min_version == 13){
#ifdef TLS1_3_VERSION
    if(SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION) != 1){
      SSL_CTX_free(ctx);
      return NULL;
    }
#else
    SSL_CTX_free(ctx);
    return NULL;
#endif
  } else if(pol->min_version == 12){
#ifdef TLS1_2_VERSION
    if(SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION) != 1){
      SSL_CTX_free(ctx);
      return NULL;
    }
#else
    SSL_CTX_free(ctx);
    return NULL;
#endif
  }

  int sock = tn_tcp_connect_posix(host, port);
  if(sock < 0){
    SSL_CTX_free(ctx);
    return NULL;
  }

  if(pol->handshake_timeout_ms > 0){
    if(tn_net_set_timeout_impl(sock, pol->handshake_timeout_ms) != 0){
      close(sock);
      SSL_CTX_free(ctx);
      return NULL;
    }
  }

  SSL* ssl = SSL_new(ctx);
  if(!ssl){
    close(sock);
    SSL_CTX_free(ctx);
    return NULL;
  }
  SSL_set_fd(ssl, sock);
  if(host && host[0]) SSL_set_tlsext_host_name(ssl, host);

  if(SSL_connect(ssl) != 1){
    SSL_free(ssl);
    close(sock);
    SSL_CTX_free(ctx);
    return NULL;
  }

  tn_tls_ossl* t = (tn_tls_ossl*)malloc(sizeof(tn_tls_ossl));
  if(!t){
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(sock);
    SSL_CTX_free(ctx);
    return NULL;
  }
  t->sock = sock;
  t->ctx = ctx;
  t->ssl = ssl;
  if(tn_tls_verify_pin_ossl(t, pol) != 0){
    tn_tls_free_ossl(t);
    return NULL;
  }
  return t;
}

static tn_tls_ossl* tn_tls_connect_ossl(const char* host, long long port){
  return tn_tls_connect_ossl_policy(host, port, &g_tn_tls_policy);
}

static long long tn_tls_send_ossl(tn_tls_ossl* t, const unsigned char* buf, long long len){
  if(!t || !buf || len <= 0) return 0;
  int n = SSL_write(t->ssl, buf, (int)len);
  return (long long)n;
}

static long long tn_tls_recv_ossl(tn_tls_ossl* t, unsigned char* buf, long long len){
  if(!t || !buf || len <= 0) return 0;
  int n = SSL_read(t->ssl, buf, (int)len);
  return (long long)n;
}

static void tn_tls_free_ossl(tn_tls_ossl* t){
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
#include <ctype.h>
#if defined(TN_TLS_OPENSSL) && !defined(_WIN32)
  #include <openssl/ssl.h>
  #include <openssl/err.h>
#endif

#ifdef _WIN32
  #define TN_EXPORT __declspec(dllexport)
  #include <direct.h>
  #include <io.h>
#else
  #define TN_EXPORT
  #include <dirent.h>
  #include <unistd.h>
  #include <sys/socket.h>
  #include <netdb.h>
  #include <arpa/inet.h>
  #include <fcntl.h>
  #include <pthread.h>
  #include <signal.h>
#endif

#ifndef TN_SEND_FLAGS
  #ifdef MSG_NOSIGNAL
    #define TN_SEND_FLAGS MSG_NOSIGNAL
  #else
    #define TN_SEND_FLAGS 0
  #endif
#endif

static long long tn_ftell_impl(FILE* f){
  return (long long)ftell(f);
}

static int tn_fseek_impl(FILE* f, long long off, int whence){
  return fseek(f, (long)off, whence);
}

static unsigned char* tn_dup_cstr(const char* s){
  if(!s) s = "";
  size_t n = strlen(s);
  unsigned char* p = (unsigned char*)malloc(n + 1);
  if(!p) return NULL;
  memcpy(p, s, n + 1);
  return p;
}

#ifdef _WIN32
static wchar_t* tn_utf8_to_wide(const char* s){
  if(!s) s = "";
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  if(n <= 0) return NULL;
  wchar_t* w = (wchar_t*)malloc(sizeof(wchar_t) * (size_t)n);
  if(!w) return NULL;
  MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
  return w;
}
#endif

static long long tn_http_download_impl(const char* url, const char* out_path){
#ifdef _WIN32
  if(!url || !out_path) return -1;
  wchar_t* wurl = tn_utf8_to_wide(url);
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
  wchar_t* host = (wchar_t*)malloc(sizeof(wchar_t) * (size_t)(host_len + 1));
  if(!host){ free(wurl); return -2; }
  wcsncpy(host, uc.lpszHostName, host_len);
  host[host_len] = 0;

  DWORD path_len = uc.dwUrlPathLength + uc.dwExtraInfoLength;
  wchar_t* path = NULL;
  if(path_len == 0){
    path = tn_utf8_to_wide("/");
  } else {
    path = (wchar_t*)malloc(sizeof(wchar_t) * (size_t)(path_len + 1));
    if(!path){ free(wurl); free(host); return -2; }
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
    char* buf = (char*)malloc(avail);
    if(!buf) break;
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
#else
  (void)url; (void)out_path;
  return -2;
#endif
}

TN_EXPORT long long tn_http_download(unsigned char* url, unsigned char* out_path){
  return tn_http_download_impl((const char*)url, (const char*)out_path);
}

// ANSI color helpers (enabled via TN_COLOR or TTY auto-detect)
#define TN_CLR_RESET "\x1b[0m"
#define TN_CLR_INT   "\x1b[38;5;76m"
#define TN_CLR_FLOAT "\x1b[38;5;81m"
#define TN_CLR_STR   "\x1b[38;5;213m"
#define TN_CLR_NULL  "\x1b[38;5;203m"

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

static int tn_stdout_flush_enabled(void){
  static int inited = 0;
  static int enabled = 0;
  if(inited) return enabled;
  inited = 1;
  const char* env = getenv("TN_STDOUT_FLUSH");
  if(env && env[0]){
    char c = env[0];
    if(c=='1' || c=='y' || c=='Y' || c=='t' || c=='T'){
      enabled = 1;
    }
  }
  return enabled;
}

static void tn_stdout_line_end(void){
  fputc('\n', stdout);
  if(tn_stdout_flush_enabled()) fflush(stdout);
}

static void tn_write_i64(long long v){
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

static void tn_write_f64(double v){
  char buf[96];
  int n = snprintf(buf, sizeof(buf), "%g", v);
  if(n <= 0) return;
  if(n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
  fwrite(buf, 1, (size_t)n, stdout);
}

static unsigned char* tn_path_normalize_opts_impl(const char* inpath, long long flags);
static unsigned char* tn_path_join_impl(const char* a, const char* b);

typedef struct {
  char* name;
  int is_dir;
} TnDirEntry;

static int tn_dir_entry_cmp(const void* a, const void* b){
  const TnDirEntry* ea = (const TnDirEntry*)a;
  const TnDirEntry* eb = (const TnDirEntry*)b;
  const char* an = (ea && ea->name) ? ea->name : "";
  const char* bn = (eb && eb->name) ? eb->name : "";
  return strcmp(an, bn);
}

static void tn_dir_entries_free(TnDirEntry* entries, size_t count){
  if(!entries) return;
  for(size_t i = 0; i < count; i++){
    free(entries[i].name);
  }
  free(entries);
}

static int tn_dir_entry_push(TnDirEntry** entries, size_t* count, size_t* cap,
                             const char* name, int is_dir){
  if(!entries || !count || !cap || !name) return -1;
  if(*count >= *cap){
    size_t next_cap = (*cap == 0) ? 16 : (*cap * 2);
    TnDirEntry* next = (TnDirEntry*)realloc(*entries, next_cap * sizeof(TnDirEntry));
    if(!next) return -1;
    *entries = next;
    *cap = next_cap;
  }
  unsigned char* copy = tn_dup_cstr(name);
  if(!copy) return -1;
  (*entries)[*count].name = (char*)copy;
  (*entries)[*count].is_dir = is_dir ? 1 : 0;
  *count += 1;
  return 0;
}

static int tn_collect_dir_entries(const char* path, TnDirEntry** out_entries, size_t* out_count){
  if(!out_entries || !out_count) return -1;
  *out_entries = NULL;
  *out_count = 0;
  size_t cap = 0;
  TnDirEntry* entries = NULL;

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
    if(tn_dir_entry_push(&entries, out_count, &cap, n,
                         (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != 0){
      FindClose(h);
      tn_dir_entries_free(entries, *out_count);
      *out_count = 0;
      return -1;
    }
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
    unsigned char* full = tn_path_join_impl(p, n);
    if(full){
      struct stat st;
      if(stat((const char*)full, &st)==0 && S_ISDIR(st.st_mode)){
        is_dir = 1;
      }
      free(full);
    }
    if(tn_dir_entry_push(&entries, out_count, &cap, n, is_dir) != 0){
      closedir(d);
      tn_dir_entries_free(entries, *out_count);
      *out_count = 0;
      return -1;
    }
  }
  closedir(d);
#endif

  if(*out_count > 1){
    qsort(entries, *out_count, sizeof(TnDirEntry), tn_dir_entry_cmp);
  }
  *out_entries = entries;
  return 0;
}

static void tn_append_bytes(char** buf, size_t* len, size_t* cap, const char* s, size_t n){
  if(*cap == 0){
    *cap = 128;
    *buf = (char*)malloc(*cap);
    if(!*buf) return;
    (*buf)[0] = 0;
  }
  while(*len + n + 1 > *cap){
    *cap *= 2;
    char* nb = (char*)realloc(*buf, *cap);
    if(!nb) return;
    *buf = nb;
  }
  memcpy(*buf + *len, s, n);
  *len += n;
  (*buf)[*len] = 0;
}

static unsigned char* tn_list_dir_impl(const char* path){
  char* out = NULL;
  size_t len = 0, cap = 0;
  TnDirEntry* entries = NULL;
  size_t count = 0;

  if(tn_collect_dir_entries(path, &entries, &count) != 0){
    return tn_dup_cstr("");
  }

  for(size_t i = 0; i < count; i++){
    const char* n = entries[i].name ? entries[i].name : "";
    if(n[0] == 0) continue;
    tn_append_bytes(&out, &len, &cap, n, strlen(n));
    tn_append_bytes(&out, &len, &cap, "\n", 1);
  }
  tn_dir_entries_free(entries, count);

  if(!out) return tn_dup_cstr("");
  if(len > 0 && out[len-1] == '\n') out[len-1] = 0;
  return (unsigned char*)out;
}

static unsigned char* tn_path_join_impl(const char* a, const char* b){
  if(!a || !a[0]) return tn_dup_cstr(b ? b : "");
  if(!b || !b[0]) return tn_dup_cstr(a);
#ifdef _WIN32
  if((strlen(b) >= 2 && b[1]==':') || b[0]=='\\' || b[0]=='/'){
    return tn_dup_cstr(b);
  }
  char sep = '\\';
#else
  if(b[0]=='/') return tn_dup_cstr(b);
  char sep = '/';
#endif
  size_t al = strlen(a);
  size_t bl = strlen(b);
  int need = (a[al-1] == sep) ? 0 : 1;
  char* out = (char*)malloc(al + bl + (size_t)need + 1);
  if(!out) return NULL;
  memcpy(out, a, al);
  size_t pos = al;
  if(need) out[pos++] = sep;
  memcpy(out + pos, b, bl);
  out[pos + bl] = 0;
  return (unsigned char*)out;
}

static unsigned char* tn_path_basename_impl(const char* p){
  if(!p || !p[0]) return tn_dup_cstr("");
  const char* end = p + strlen(p);
  const char* s = end;
  while(s > p){
    char c = s[-1];
    if(c=='/' || c=='\\') break;
    s--;
  }
  return tn_dup_cstr(s);
}

static unsigned char* tn_path_dirname_impl(const char* p){
  if(!p || !p[0]) return tn_dup_cstr(".");
  const char* end = p + strlen(p);
  const char* s = end;
  while(s > p){
    char c = s[-1];
    if(c=='/' || c=='\\') break;
    s--;
  }
  if(s == p) return tn_dup_cstr(".");
  size_t n = (size_t)(s - p - 1);
  unsigned char* out = (unsigned char*)malloc(n + 1);
  if(!out) return NULL;
  memcpy(out, p, n);
  out[n] = 0;
  return out;
}

static void tn_list_dir_recursive_walk(const char* base, char** out, size_t* len, size_t* cap){
  TnDirEntry* entries = NULL;
  size_t count = 0;
  if(tn_collect_dir_entries(base, &entries, &count) != 0){
    return;
  }

  for(size_t i = 0; i < count; i++){
    const char* n = entries[i].name ? entries[i].name : "";
    if(n[0] == 0) continue;
    unsigned char* full = tn_path_join_impl(base, n);
    if(full){
      tn_append_bytes(out, len, cap, (const char*)full, strlen((const char*)full));
      tn_append_bytes(out, len, cap, "\n", 1);
      if(entries[i].is_dir){
        tn_list_dir_recursive_walk((const char*)full, out, len, cap);
      }
      free(full);
    }
  }
  tn_dir_entries_free(entries, count);
}

static unsigned char* tn_list_dir_recursive_impl(const char* path){
  char* out = NULL;
  size_t len = 0, cap = 0;
  tn_list_dir_recursive_walk(path ? path : ".", &out, &len, &cap);
  if(!out) return tn_dup_cstr("");
  if(len > 0 && out[len-1] == '\n') out[len-1] = 0;
  return (unsigned char*)out;
}

static int tn_match_glob(const char* pat, const char* s){
  if(!pat || !s) return 0;
  while(*pat){
    if(*pat=='*'){
      while(pat[1]=='*') pat++;
      pat++;
      if(!*pat) return 1;
      while(*s){
        if(tn_match_glob(pat, s)) return 1;
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

static unsigned char* tn_glob_impl(const char* pattern){
  if(!pattern) return tn_dup_cstr("");
  const char* last = pattern;
  for(const char* p=pattern; *p; p++){
    if(*p=='/' || *p=='\\') last=p;
  }
  char* dir = NULL;
  const char* pat = pattern;
  if(last != pattern){
    size_t n = (size_t)(last - pattern);
    dir = (char*)malloc(n + 1);
    if(!dir) return tn_dup_cstr("");
    memcpy(dir, pattern, n);
    dir[n] = 0;
    pat = last + 1;
  } else {
    dir = (char*)tn_dup_cstr(".");
  }

  unsigned char* listing = tn_list_dir_impl(dir);
  if(!listing){
    free(dir);
    return tn_dup_cstr("");
  }

  char* out = NULL;
  size_t len = 0, cap = 0;
  char* cur = (char*)listing;
  while(*cur){
    char* line = cur;
    while(*cur && *cur!='\n') cur++;
    size_t ln = (size_t)(cur - line);
    if(*cur=='\n') *cur++ = 0;
    if(ln > 0 && tn_match_glob(pat, line)){
      unsigned char* full = tn_path_join_impl(dir, line);
      tn_append_bytes(&out, &len, &cap, (const char*)full, strlen((const char*)full));
      tn_append_bytes(&out, &len, &cap, "\n", 1);
      free(full);
    }
  }
  free(listing);
  free(dir);

  if(!out) return tn_dup_cstr("");
  if(len > 0 && out[len-1] == '\n') out[len-1] = 0;
  return (unsigned char*)out;
}

static unsigned char* tn_path_normalize_impl(const char* path){
  return tn_path_normalize_opts_impl(path, 0);
}

static unsigned char* tn_path_resolve_impl(const char* path){
  if(!path) return NULL;
#ifdef _WIN32
  char buf[MAX_PATH];
  if(_fullpath(buf, path, MAX_PATH)) return tn_dup_cstr(buf);
  return NULL;
#else
  char* rp = realpath(path, NULL);
  if(!rp) return NULL;
  unsigned char* out = tn_dup_cstr(rp);
  free(rp);
  return out;
#endif
}

static char tn_pick_sep_from_path(const char* path, char def){
  if(!path) return def;
  for(const char* p=path; *p; p++){
    if(*p=='/' || *p=='\\') return *p;
  }
  return def;
}

static unsigned char* tn_path_normalize_opts_impl(const char* inpath, long long flags){
  if(!inpath) return tn_dup_cstr("");

  unsigned char* resolved = NULL;
  if(flags & 4){
    resolved = tn_path_resolve_impl(inpath);
  }
  const char* path = resolved ? (const char*)resolved : inpath;

  const char* p = path;
#ifdef _WIN32
  char drive[3] = {0,0,0};
  int has_drive = 0;
  if(isalpha((unsigned char)p[0]) && p[1]==':'){
    drive[0]=p[0]; drive[1]=':'; drive[2]=0;
    has_drive = 1;
    p += 2;
  }
#else
  int has_drive = 0;
#endif
  int absolute = (*p=='/' || *p=='\\');
  while(*p=='/' || *p=='\\') p++;

#ifdef _WIN32
  char def_sep = '\\';
#else
  char def_sep = '/';
#endif
  char sep = (flags & 1) ? tn_pick_sep_from_path(path, def_sep) : def_sep;

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
        segs[n++] = (char*)tn_dup_cstr("..");
      }
      continue;
    }
    if(n==cap){ cap = cap? cap*2:8; segs=(char**)realloc(segs,sizeof(char*)*(size_t)cap); }
    char* part = (char*)malloc((size_t)slen + 1);
    if(!part) break;
    memcpy(part, s, (size_t)slen);
    part[slen]=0;
    segs[n++] = part;
  }

  if(n==0 && !absolute){
#ifdef _WIN32
    if(has_drive){
      char* out = (char*)malloc(3);
      if(!out){ free(resolved); return tn_dup_cstr(""); }
      out[0]=drive[0]; out[1]=':'; out[2]=0;
      if(flags & 2){
        for(int i=0;i<2;i++) out[i]=(char)tolower((unsigned char)out[i]);
      }
      free(resolved);
      return (unsigned char*)out;
    }
#endif
    free(resolved);
    return tn_dup_cstr(".");
  }

  size_t outlen = 0;
#ifdef _WIN32
  if(has_drive) outlen += 2;
#endif
  if(absolute) outlen += 1;
  for(int i=0;i<n;i++) outlen += strlen(segs[i]) + (i>0 ? 1 : 0);

  char* out = (char*)malloc(outlen + 1);
  if(!out){ free(resolved); return tn_dup_cstr(""); }
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
    for(size_t i=0;i<pos;i++) out[i]=(char)tolower((unsigned char)out[i]);
  }
  free(resolved);
  return (unsigned char*)out;
}

static long long tn_time_now_ns_impl(void){
#ifdef _WIN32
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
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

static void tn_sleep_ms_impl(long long ms){
  if(ms <= 0) return;
#ifdef _WIN32
  /* Pump Win32 messages during sleep so the window stays responsive */
  DWORD deadline = GetTickCount() + (DWORD)ms;
  for(;;){
    MSG msg;
    while(PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)){
      TranslateMessage(&msg);
      DispatchMessageA(&msg);
    }
    DWORD now = GetTickCount();
    if(now >= deadline) break;
    DWORD remain = deadline - now;
    if(remain > 8) remain = 8;  /* check messages ~120fps */
    Sleep(remain);
  }
#else
  usleep((useconds_t)(ms * 1000));
#endif
}

typedef long long (*tn_async_fn0)(void);

typedef struct tn_async_task {
  tn_async_fn0 fn;
  long long result;
  int done;
#ifdef _WIN32
  SRWLOCK lock;
  CONDITION_VARIABLE cv;
#else
  pthread_mutex_t lock;
  pthread_cond_t cv;
#endif
} tn_async_task;

typedef struct {
  tn_async_task** buf;
  int cap;
  int head;
  int tail;
  int count;
#ifdef _WIN32
  CRITICAL_SECTION lock;
#else
  pthread_mutex_t lock;
#endif
} tn_deque;

typedef struct {
  int id;
#ifdef _WIN32
  HANDLE thread;
#else
  pthread_t thread;
#endif
  tn_deque dq;
} tn_worker;

static tn_worker* tn_sched_workers = NULL;
static int tn_sched_n = 0;
static volatile int tn_sched_inited = 0;
static volatile int tn_sched_shutdown = 0;
static long long tn_sched_pending = 0;
static long long tn_sched_rr = 0;

#ifdef _WIN32
static CRITICAL_SECTION tn_sched_lock;
static LONG tn_sched_lock_inited = 0;
static CONDITION_VARIABLE tn_sched_cv;
static void tn_sched_lock_init(void){
  if(InterlockedCompareExchange(&tn_sched_lock_inited, 1, 0) == 0){
    InitializeCriticalSection(&tn_sched_lock);
    InitializeConditionVariable(&tn_sched_cv);
  }
}
#define TN_SCHED_LOCK() do { tn_sched_lock_init(); EnterCriticalSection(&tn_sched_lock); } while(0)
#define TN_SCHED_UNLOCK() LeaveCriticalSection(&tn_sched_lock)
#else
static pthread_mutex_t tn_sched_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t tn_sched_cv = PTHREAD_COND_INITIALIZER;
#define TN_SCHED_LOCK() pthread_mutex_lock(&tn_sched_lock)
#define TN_SCHED_UNLOCK() pthread_mutex_unlock(&tn_sched_lock)
#endif

#if defined(_MSC_VER)
__declspec(thread) static int tn_worker_id = -1;
#else
static __thread int tn_worker_id = -1;
#endif

static int tn_cpu_count(void){
#ifdef _WIN32
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  int n = (int)si.dwNumberOfProcessors;
  return n > 0 ? n : 1;
#else
  long n = sysconf(_SC_NPROCESSORS_ONLN);
  return (int)(n > 0 ? n : 1);
#endif
}

static void tn_deque_init(tn_deque* dq){
  if(!dq) return;
  dq->cap = 64;
  dq->buf = (tn_async_task**)malloc(sizeof(tn_async_task*) * (size_t)dq->cap);
  dq->head = 0;
  dq->tail = 0;
  dq->count = 0;
#ifdef _WIN32
  InitializeCriticalSection(&dq->lock);
#else
  pthread_mutex_init(&dq->lock, NULL);
#endif
}

static void tn_deque_free(tn_deque* dq){
  if(!dq) return;
  if(dq->buf) free(dq->buf);
#ifdef _WIN32
  DeleteCriticalSection(&dq->lock);
#else
  pthread_mutex_destroy(&dq->lock);
#endif
  memset(dq, 0, sizeof(*dq));
}

static void tn_deque_grow(tn_deque* dq){
  if(!dq) return;
  int newcap = dq->cap ? dq->cap * 2 : 64;
  tn_async_task** nb = (tn_async_task**)malloc(sizeof(tn_async_task*) * (size_t)newcap);
  if(!nb) return;
  for(int i=0;i<dq->count;i++){
    int idx = (dq->tail + i) % dq->cap;
    nb[i] = dq->buf[idx];
  }
  free(dq->buf);
  dq->buf = nb;
  dq->cap = newcap;
  dq->tail = 0;
  dq->head = dq->count;
}

static void tn_deque_push_head(tn_deque* dq, tn_async_task* t){
  if(!dq || !t) return;
#ifdef _WIN32
  EnterCriticalSection(&dq->lock);
#else
  pthread_mutex_lock(&dq->lock);
#endif
  if(dq->count == dq->cap){
    tn_deque_grow(dq);
  }
  if(dq->count == dq->cap || dq->cap == 0 || !dq->buf){
#ifdef _WIN32
    LeaveCriticalSection(&dq->lock);
#else
    pthread_mutex_unlock(&dq->lock);
#endif
    return;
  }
  dq->buf[dq->head] = t;
  dq->head = (dq->head + 1) % dq->cap;
  dq->count++;
#ifdef _WIN32
  LeaveCriticalSection(&dq->lock);
#else
  pthread_mutex_unlock(&dq->lock);
#endif
}

static tn_async_task* tn_deque_pop_head(tn_deque* dq){
  tn_async_task* t = NULL;
  if(!dq) return NULL;
#ifdef _WIN32
  EnterCriticalSection(&dq->lock);
#else
  pthread_mutex_lock(&dq->lock);
#endif
  if(dq->count > 0){
    dq->head = (dq->head - 1 + dq->cap) % dq->cap;
    t = dq->buf[dq->head];
    dq->count--;
  }
#ifdef _WIN32
  LeaveCriticalSection(&dq->lock);
#else
  pthread_mutex_unlock(&dq->lock);
#endif
  return t;
}

static tn_async_task* tn_deque_steal_tail(tn_deque* dq){
  tn_async_task* t = NULL;
  if(!dq) return NULL;
#ifdef _WIN32
  EnterCriticalSection(&dq->lock);
#else
  pthread_mutex_lock(&dq->lock);
#endif
  if(dq->count > 0){
    t = dq->buf[dq->tail];
    dq->tail = (dq->tail + 1) % dq->cap;
    dq->count--;
  }
#ifdef _WIN32
  LeaveCriticalSection(&dq->lock);
#else
  pthread_mutex_unlock(&dq->lock);
#endif
  return t;
}

static void tn_task_complete(tn_async_task* t, long long result){
  if(!t) return;
#ifdef _WIN32
  AcquireSRWLockExclusive(&t->lock);
  t->result = result;
  t->done = 1;
  WakeAllConditionVariable(&t->cv);
  ReleaseSRWLockExclusive(&t->lock);
#else
  pthread_mutex_lock(&t->lock);
  t->result = result;
  t->done = 1;
  pthread_cond_broadcast(&t->cv);
  pthread_mutex_unlock(&t->lock);
#endif
}

#ifdef _WIN32
static DWORD WINAPI tn_worker_thread(LPVOID p)
#else
static void* tn_worker_thread(void* p)
#endif
{
  tn_worker* w = (tn_worker*)p;
  if(w) tn_worker_id = w->id;
  for(;;){
    tn_async_task* task = NULL;
    if(w) task = tn_deque_pop_head(&w->dq);
    if(!task){
      for(int i=0;i<tn_sched_n;i++){
        if(!w || i == w->id) continue;
        task = tn_deque_steal_tail(&tn_sched_workers[i].dq);
        if(task) break;
      }
    }
    if(task){
      TN_SCHED_LOCK();
      if(tn_sched_pending > 0) tn_sched_pending--;
      TN_SCHED_UNLOCK();
      long long r = 0;
      if(task->fn) r = task->fn();
      tn_task_complete(task, r);
      continue;
    }
    TN_SCHED_LOCK();
    while(!tn_sched_shutdown && tn_sched_pending == 0){
#ifdef _WIN32
      SleepConditionVariableCS(&tn_sched_cv, &tn_sched_lock, INFINITE);
#else
      pthread_cond_wait(&tn_sched_cv, &tn_sched_lock);
#endif
    }
    int stop = tn_sched_shutdown;
    TN_SCHED_UNLOCK();
    if(stop) break;
  }
#ifdef _WIN32
  return 0;
#else
  return NULL;
#endif
}

static int tn_sched_init(void){
  if(tn_sched_inited) return 0;
  TN_SCHED_LOCK();
  if(tn_sched_inited){
    TN_SCHED_UNLOCK();
    return 0;
  }
  int n = tn_cpu_count();
  const char* env = getenv("TN_ASYNC_THREADS");
  if(env && env[0]){
    int v = atoi(env);
    if(v > 0) n = v;
  }
  if(n < 2) n = 2;
  tn_sched_workers = (tn_worker*)calloc((size_t)n, sizeof(tn_worker));
  if(!tn_sched_workers){
    TN_SCHED_UNLOCK();
    return -1;
  }
  tn_sched_n = n;
  tn_sched_pending = 0;
  tn_sched_shutdown = 0;
  tn_sched_inited = 1;
  TN_SCHED_UNLOCK();

  int created = 0;
  for(int i=0;i<n;i++){
    tn_sched_workers[i].id = i;
    tn_deque_init(&tn_sched_workers[i].dq);
#ifdef _WIN32
    tn_sched_workers[i].thread = CreateThread(NULL, 0, tn_worker_thread, &tn_sched_workers[i], 0, NULL);
    if(!tn_sched_workers[i].thread) break;
    CloseHandle(tn_sched_workers[i].thread);
    tn_sched_workers[i].thread = NULL;
#else
    if(pthread_create(&tn_sched_workers[i].thread, NULL, tn_worker_thread, &tn_sched_workers[i]) != 0){
      break;
    }
    pthread_detach(tn_sched_workers[i].thread);
#endif
    created++;
  }
  if(created == 0){
    for(int i=0;i<n;i++) tn_deque_free(&tn_sched_workers[i].dq);
    free(tn_sched_workers);
    tn_sched_workers = NULL;
    TN_SCHED_LOCK();
    tn_sched_n = 0;
    tn_sched_inited = 0;
    TN_SCHED_UNLOCK();
    return -1;
  }
  if(created < n){
    TN_SCHED_LOCK();
    tn_sched_n = created;
    TN_SCHED_UNLOCK();
  }
  return 0;
}

typedef long long tn_sock_ll;
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

static int tn_net_inited = 0;
#ifndef _WIN32
static int tn_net_sigpipe_ignored = 0;
#endif

#ifdef _WIN32
static CRITICAL_SECTION tn_net_lock;
static LONG tn_net_lock_inited = 0;
static void tn_net_lock_init(void){
  if(InterlockedCompareExchange(&tn_net_lock_inited, 1, 0) == 0){
    InitializeCriticalSection(&tn_net_lock);
  }
}
#define TN_NET_LOCK() do { tn_net_lock_init(); EnterCriticalSection(&tn_net_lock); } while(0)
#define TN_NET_UNLOCK() LeaveCriticalSection(&tn_net_lock)

static CRITICAL_SECTION tn_tm_lock;
static LONG tn_tm_lock_inited = 0;
static void tn_tm_lock_init(void){
  if(InterlockedCompareExchange(&tn_tm_lock_inited, 1, 0) == 0){
    InitializeCriticalSection(&tn_tm_lock);
  }
}
#define TN_TM_LOCK() do { tn_tm_lock_init(); EnterCriticalSection(&tn_tm_lock); } while(0)
#define TN_TM_UNLOCK() LeaveCriticalSection(&tn_tm_lock)
#else
static pthread_mutex_t tn_net_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t tn_tm_lock = PTHREAD_MUTEX_INITIALIZER;
#define TN_NET_LOCK() pthread_mutex_lock(&tn_net_lock)
#define TN_NET_UNLOCK() pthread_mutex_unlock(&tn_net_lock)
#define TN_TM_LOCK() pthread_mutex_lock(&tn_tm_lock)
#define TN_TM_UNLOCK() pthread_mutex_unlock(&tn_tm_lock)
#endif

static unsigned char* tn_date_now_impl(int utc){
  time_t t = time(NULL);
  struct tm tmv;
#ifdef _WIN32
  TN_TM_LOCK();
  struct tm* ptm = utc ? gmtime(&t) : localtime(&t);
  if(!ptm){
    TN_TM_UNLOCK();
    return tn_dup_cstr("");
  }
  tmv = *ptm;
  TN_TM_UNLOCK();
#else
  struct tm* ptm = utc ? gmtime_r(&t, &tmv) : localtime_r(&t, &tmv);
  if(!ptm) return tn_dup_cstr("");
#endif
  char buf[64];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
           tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
           tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  return tn_dup_cstr(buf);
}

static int tn_net_init_impl(void){
#ifdef _WIN32
  TN_NET_LOCK();
  if(tn_net_inited){
    TN_NET_UNLOCK();
    return 0;
  }
  WSADATA wsa;
  int r = WSAStartup(MAKEWORD(2,2), &wsa);
  if(r == 0) tn_net_inited = 1;
  TN_NET_UNLOCK();
  return r == 0 ? 0 : -1;
#else
  if(!tn_net_sigpipe_ignored){
    signal(SIGPIPE, SIG_IGN);
    tn_net_sigpipe_ignored = 1;
  }
  (void)tn_net_inited;
  return 0;
#endif
}

static int tn_net_cleanup_impl(void){
#ifdef _WIN32
  TN_NET_LOCK();
  if(tn_net_inited){
    WSACleanup();
    tn_net_inited = 0;
  }
  TN_NET_UNLOCK();
  return 0;
#else
  return 0;
#endif
}

static long long tn_net_last_error_impl(void){
#ifdef _WIN32
  return (long long)WSAGetLastError();
#else
  return (long long)errno;
#endif
}

static tn_sock_t tn_sock_from_ll(long long v){
  return (tn_sock_t)(intptr_t)v;
}

static long long tn_sock_to_ll(tn_sock_t s){
  return (long long)(intptr_t)s;
}

static int tn_net_set_blocking_impl(tn_sock_t s, int on){
#ifdef _WIN32
  u_long mode = on ? 0UL : 1UL;
  return ioctlsocket(s, FIONBIO, &mode) == 0 ? 0 : -1;
#else
  int flags = fcntl(s, F_GETFL, 0);
  if(flags < 0) return -1;
  if(on) flags &= ~O_NONBLOCK;
  else flags |= O_NONBLOCK;
  return fcntl(s, F_SETFL, flags) == 0 ? 0 : -1;
#endif
}

static int tn_net_set_timeout_impl(tn_sock_t s, long long ms){
  if(ms < 0) ms = 0;
#ifdef _WIN32
  DWORD tv = (DWORD)ms;
  if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)) != 0) return -1;
  if(setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv)) != 0) return -1;
  return 0;
#else
  struct timeval tv;
  tv.tv_sec = (time_t)(ms / 1000);
  tv.tv_usec = (suseconds_t)((ms % 1000) * 1000);
  if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) return -1;
  if(setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0) return -1;
  return 0;
#endif
}

static int tn_net_connect_impl(tn_sock_t s, const char* host, long long port){
  if(tn_net_init_impl() != 0) return -1;
  if(s == TN_INVALID_SOCKET) return -1;
  char portbuf[16];
  snprintf(portbuf, sizeof(portbuf), "%lld", port);
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = 0;
  hints.ai_protocol = 0;
  struct addrinfo* res = NULL;
  int rc = getaddrinfo((host && host[0]) ? host : "127.0.0.1", portbuf, &hints, &res);
  if(rc != 0 || !res) return -1;
  int ok = -1;
  for(struct addrinfo* ai = res; ai; ai = ai->ai_next){
    if(connect(s, ai->ai_addr, (int)ai->ai_addrlen) == 0){
      ok = 0;
      break;
    }
  }
  freeaddrinfo(res);
  return ok;
}

static int tn_net_bind_impl(tn_sock_t s, const char* host, long long port){
  if(tn_net_init_impl() != 0) return -1;
  if(s == TN_INVALID_SOCKET) return -1;
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
  if(rc != 0 || !res) return -1;
  int ok = -1;
  for(struct addrinfo* ai = res; ai; ai = ai->ai_next){
    if(bind(s, ai->ai_addr, (int)ai->ai_addrlen) == 0){
      ok = 0;
      break;
    }
  }
  freeaddrinfo(res);
  return ok;
}

static unsigned char* tn_net_resolve_impl(const char* host, long long port){
  if(tn_net_init_impl() != 0) return tn_dup_cstr("");
  char portbuf[16];
  snprintf(portbuf, sizeof(portbuf), "%lld", port);
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = 0;
  hints.ai_protocol = 0;
  struct addrinfo* res = NULL;
  int rc = getaddrinfo((host && host[0]) ? host : "127.0.0.1", portbuf, &hints, &res);
  if(rc != 0 || !res) return tn_dup_cstr("");
  char* out = NULL;
  size_t len = 0, cap = 0;
  for(struct addrinfo* ai = res; ai; ai = ai->ai_next){
    char hbuf[NI_MAXHOST];
    if(getnameinfo(ai->ai_addr, (socklen_t)ai->ai_addrlen, hbuf, sizeof(hbuf), NULL, 0, NI_NUMERICHOST) == 0){
      tn_append_bytes(&out, &len, &cap, hbuf, strlen(hbuf));
      tn_append_bytes(&out, &len, &cap, "\n", 1);
    }
  }
  freeaddrinfo(res);
  if(!out) return tn_dup_cstr("");
  if(len > 0 && out[len-1] == '\n') out[len-1] = 0;
  return (unsigned char*)out;
}

#ifdef _WIN32
typedef struct tn_tls {
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
} tn_tls;

static int tn_tls_grow_inbuf(tn_tls* t){
  unsigned char* new_buf;
  int new_cap;
  if(!t) return -1;
  if(t->in_cap <= 0) return -1;
  if(t->in_cap > INT_MAX / 2) return -1;
  new_cap = t->in_cap * 2;
  new_buf = (unsigned char*)realloc(t->inbuf, (size_t)new_cap);
  if(!new_buf) return -1;
  t->inbuf = new_buf;
  t->in_cap = new_cap;
  return 0;
}

static int tn_tls_verify_pin_win(tn_tls* t, const tn_tls_policy_cfg* policy){
  const tn_tls_policy_cfg* pol = policy ? policy : &g_tn_tls_policy;
  if(!t) return -1;
  if(pol->pin_enabled == 0) return 0;
  PCCERT_CONTEXT cert = NULL;
  SECURITY_STATUS st = QueryContextAttributes(&t->ctx, SECPKG_ATTR_REMOTE_CERT_CONTEXT, (PVOID)&cert);
  if(st != SEC_E_OK || cert == NULL) return -1;
  BYTE hash[64];
  DWORD hash_len = (DWORD)sizeof(hash);
  BOOL ok = CertGetCertificateContextProperty(cert, CERT_SHA256_HASH_PROP_ID, hash, &hash_len);
  CertFreeCertificateContext(cert);
  if(!ok || hash_len != 32) return -1;
  return (memcmp(hash, pol->pin_sha256, 32) == 0) ? 0 : -1;
}

static SOCKET tn_tcp_connect(const char* host, long long port){
  if(tn_net_init_impl() != 0) return INVALID_SOCKET;
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

static int tn_tls_handshake(tn_tls* t, const char* host, const tn_tls_policy_cfg* policy){
  const tn_tls_policy_cfg* pol = policy ? policy : &g_tn_tls_policy;
  SCHANNEL_CRED sc;
  memset(&sc, 0, sizeof(sc));
  sc.dwVersion = SCHANNEL_CRED_VERSION;
  if(pol->min_version == 13){
#ifdef SP_PROT_TLS1_3_CLIENT
    sc.grbitEnabledProtocols = SP_PROT_TLS1_3_CLIENT;
#else
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
  if(st != SEC_E_OK) return -1;

  DWORD flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
                ISC_REQ_CONFIDENTIALITY | ISC_REQ_STREAM |
                ISC_REQ_ALLOCATE_MEMORY;
  DWORD out_flags = 0;

  t->in_cap = 64 * 1024;
  t->inbuf = (unsigned char*)malloc((size_t)t->in_cap);
  if(!t->inbuf) return -1;
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
        if(tn_tls_grow_inbuf(t) != 0) return -1;
      }
      int n = recv(t->sock, (char*)t->inbuf + t->in_len, t->in_cap - t->in_len, 0);
      if(n <= 0) return -1;
      t->in_len += n;
      continue;
    }

    return -1;
  }

  if(QueryContextAttributes(&t->ctx, SECPKG_ATTR_STREAM_SIZES, &t->sizes) != SEC_E_OK){
    return -1;
  }
  t->sizes_ok = 1;
  return 0;
}

static void tn_tls_free(tn_tls* t){
  if(!t) return;
  if(t->have_ctx) DeleteSecurityContext(&t->ctx);
  FreeCredentialHandle(&t->cred);
  if(t->sock != INVALID_SOCKET) closesocket(t->sock);
  if(t->inbuf) free(t->inbuf);
  if(t->plain) free(t->plain);
  free(t);
}

static tn_tls* tn_tls_connect_impl_policy(const char* host, long long port, const tn_tls_policy_cfg* policy){
  const tn_tls_policy_cfg* pol = policy ? policy : &g_tn_tls_policy;
  tn_tls* t = (tn_tls*)calloc(1, sizeof(tn_tls));
  if(!t) return NULL;
  t->sock = tn_tcp_connect(host, port);
  if(t->sock == INVALID_SOCKET){
    tn_tls_free(t);
    return NULL;
  }
  if(pol->handshake_timeout_ms > 0){
    if(tn_net_set_timeout_impl(t->sock, pol->handshake_timeout_ms) != 0){
      tn_tls_free(t);
      return NULL;
    }
  }
  if(tn_tls_handshake(t, host, pol) != 0){
    tn_tls_free(t);
    return NULL;
  }
  if(tn_tls_verify_pin_win(t, pol) != 0){
    tn_tls_free(t);
    return NULL;
  }
  return t;
}

static tn_tls* tn_tls_connect_impl(const char* host, long long port){
  return tn_tls_connect_impl_policy(host, port, &g_tn_tls_policy);
}

static long long tn_tls_send_impl(tn_tls* t, const unsigned char* buf, long long len){
  if(!t || !buf || len <= 0) return 0;
  if(!t->sizes_ok) return -1;
  int header = (int)t->sizes.cbHeader;
  int trailer = (int)t->sizes.cbTrailer;
  int total = header + (int)len + trailer;
  unsigned char* out = (unsigned char*)malloc((size_t)total);
  if(!out) return -1;

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
    free(out);
    return -1;
  }

  int to_send = (int)(bufs[0].cbBuffer + bufs[1].cbBuffer + bufs[2].cbBuffer);
  int sent = send(t->sock, (const char*)out, to_send, TN_SEND_FLAGS);
  free(out);
  if(sent <= 0) return -1;
  return len;
}

static long long tn_tls_recv_impl(tn_tls* t, unsigned char* out, long long len){
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
    return take;
  }

  for(;;){
    if(t->in_len < 1){
      if(t->in_len >= t->in_cap){
        if(tn_tls_grow_inbuf(t) != 0) return -1;
      }
      int n = recv(t->sock, (char*)t->inbuf + t->in_len, t->in_cap - t->in_len, 0);
      if(n <= 0) return 0;
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
        if(tn_tls_grow_inbuf(t) != 0) return -1;
      }
      int n = recv(t->sock, (char*)t->inbuf + t->in_len, t->in_cap - t->in_len, 0);
      if(n <= 0) return 0;
      t->in_len += n;
      continue;
    }
    if(st == SEC_I_CONTEXT_EXPIRED){
      return 0;
    }
    if(st != SEC_E_OK){
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
      t->plain = (unsigned char*)malloc((size_t)data->cbBuffer);
      if(!t->plain) return -1;
      memcpy(t->plain, data->pvBuffer, data->cbBuffer);
      t->plain_len = (int)data->cbBuffer;
      t->plain_pos = 0;
      break;
    }
  }

  return tn_tls_recv_impl(t, out, len);
}
#endif

// ------------------------------
// Printing
// ------------------------------
TN_EXPORT void say(long long x){
  // Fast i64 print path (newline + optional flush via TN_STDOUT_FLUSH=1)
  tn_color_begin(TN_CLR_INT);
  tn_write_i64(x);
  tn_color_end();
  tn_stdout_line_end();
}

TN_EXPORT void say_f(long long bits){
  double x = 0.0;
  memcpy(&x, &bits, sizeof(double));
  tn_color_begin(TN_CLR_FLOAT);
  tn_write_f64(x);
  tn_color_end();
  tn_stdout_line_end();
}

TN_EXPORT void say_str(unsigned char* s){
  if(!s) {
    tn_color_begin(TN_CLR_NULL);
    fputs("(null)", stdout);
    tn_color_end();
    tn_stdout_line_end();
    return;
  }
  tn_color_begin(TN_CLR_STR);
  fputs((char*)s, stdout);
  tn_color_end();
  tn_stdout_line_end();
}

TN_EXPORT long long say_multi(long long* vals, long long* tags, long long argc){
  if(!vals || !tags || argc <= 0){
    tn_stdout_line_end();
    return 0;
  }
  for(long long i=0;i<argc;i++){
    if(i) fputc(' ', stdout);
    long long tag = tags[i];
    long long v = vals[i];
    if(tag == 2){
      unsigned char* s = (unsigned char*)(uintptr_t)v;
      if(!s){
        tn_color_begin(TN_CLR_NULL);
        fputs("(null)", stdout);
        tn_color_end();
      } else {
        tn_color_begin(TN_CLR_STR);
        fputs((char*)s, stdout);
        tn_color_end();
      }
    } else if(tag == 1){
      double x = 0.0;
      memcpy(&x, &v, sizeof(double));
      tn_color_begin(TN_CLR_FLOAT);
      tn_write_f64(x);
      tn_color_end();
    } else {
      tn_color_begin(TN_CLR_INT);
      tn_write_i64(v);
      tn_color_end();
    }
  }
  tn_stdout_line_end();
  return 0;
}

// ------------------------------
// Strings
// ------------------------------
TN_EXPORT long long len(unsigned char* s){
  if(!s) return 0;
  return (long long)strlen((char*)s);
}

// IMPORTANT: do NOT export "strcmp" (conflicts with <string.h>)
TN_EXPORT long long tn_strcmp(unsigned char* a, unsigned char* b){
  if(!a && !b) return 0;
  if(!a) return -1;
  if(!b) return 1;
  return (long long)strcmp((char*)a, (char*)b);
}

// ------------------------------
// Input
// ------------------------------
// Reads a signed 64-bit integer from stdin.
// Returns 0 on EOF / invalid input (you can change policy later).
TN_EXPORT long long input_i64(void){
  // Use fgets + strtoll for robust parsing.
  char buf[256];
  if(!fgets(buf, (int)sizeof(buf), stdin)){
    return 0;
  }

  // Trim leading spaces
  char* p = buf;
  while(*p==' ' || *p=='\t' || *p=='\r' || *p=='\n') p++;

  errno = 0;
  char* end = NULL;
  long long v = strtoll(p, &end, 10);

  if(errno != 0){
    return 0;
  }
  // If no digits were consumed
  if(end == p){
    return 0;
  }
  return v;
}

// Reads a full line from stdin (without trailing \n/\r\n),
// returns heap-allocated NUL-terminated bytes. Caller must free().
TN_EXPORT unsigned char* input_line(void){
  size_t cap = 128;
  size_t n = 0;
  char* buf = (char*)malloc(cap);
  if(!buf) return NULL;

  for(;;){
    int ch = fgetc(stdin);
    if(ch == EOF){
      break;
    }
    if(ch == '\n'){
      break;
    }
    if(ch == '\r'){
      // Handle Windows CRLF
      int nx = fgetc(stdin);
      if(nx != '\n' && nx != EOF) ungetc(nx, stdin);
      break;
    }

    if(n + 1 >= cap){
      size_t ncap = cap * 2;
      char* nb = (char*)realloc(buf, ncap);
      if(!nb){
        free(buf);
        return NULL;
      }
      buf = nb;
      cap = ncap;
    }

    buf[n++] = (char)ch;
  }

  // If EOF immediately and nothing read, return empty string (or NULL if you prefer)
  if(n + 1 >= cap){
    char* nb = (char*)realloc(buf, cap + 1);
    if(!nb){
      free(buf);
      return NULL;
    }
    buf = nb;
    cap = cap + 1;
  }

  buf[n] = 0;
  return (unsigned char*)buf;
}

// ------------------------------
// File I/O (C stdio wrappers)
// ------------------------------
TN_EXPORT unsigned char* tn_fopen(unsigned char* path, unsigned char* mode){
  const char* p = (const char*)path;
  const char* m = (const char*)mode;
  FILE* f = fopen(p ? p : "", m ? m : "");
  return (unsigned char*)f;
}

TN_EXPORT long long tn_fclose(unsigned char* f){
  FILE* fp = (FILE*)f;
  if(!fp) return -1;
  return (long long)fclose(fp);
}

TN_EXPORT long long tn_fflush(unsigned char* f){
  FILE* fp = (FILE*)f;
  return (long long)fflush(fp);
}

TN_EXPORT long long tn_fread(unsigned char* buf, long long size, long long count, unsigned char* f){
  FILE* fp = (FILE*)f;
  if(!fp || !buf || size <= 0 || count <= 0) return 0;
  size_t total = 0;
  if(tn_mul_overflow_size(size, count, &total)) return 0;
  size_t n = fread(buf, 1, total, fp);
  return (long long)n;
}

TN_EXPORT long long tn_fwrite(unsigned char* buf, long long size, long long count, unsigned char* f){
  FILE* fp = (FILE*)f;
  if(!fp || !buf || size <= 0 || count <= 0) return 0;
  size_t total = 0;
  if(tn_mul_overflow_size(size, count, &total)) return 0;
  size_t n = fwrite(buf, 1, total, fp);
  return (long long)n;
}

TN_EXPORT long long tn_fseek(unsigned char* f, long long off, long long whence){
  FILE* fp = (FILE*)f;
  if(!fp) return -1;
  return (long long)tn_fseek_impl(fp, off, (int)whence);
}

TN_EXPORT long long tn_ftell(unsigned char* f){
  FILE* fp = (FILE*)f;
  if(!fp) return -1;
  return (long long)tn_ftell_impl(fp);
}

TN_EXPORT long long tn_file_size(unsigned char* path){
  const char* p = (const char*)path;
  FILE* f = fopen(p ? p : "", "rb");
  if(!f) return -1;
  long long sz = -1;
  if(tn_fseek_impl(f, 0, 2) == 0){
    sz = tn_ftell_impl(f);
  }
  fclose(f);
  return sz;
}

TN_EXPORT unsigned char* tn_read_file(unsigned char* path){
  const char* p = (const char*)path;
  FILE* f = fopen(p ? p : "", "rb");
  if(!f) return NULL;
  if(tn_fseek_impl(f, 0, 2) != 0){
    fclose(f);
    return NULL;
  }
  long long sz = tn_ftell_impl(f);
  if(sz < 0 || (unsigned long long)sz > (unsigned long long)(SIZE_MAX - 1) || tn_fseek_impl(f, 0, 0) != 0){
    fclose(f);
    return NULL;
  }
  char* buf = (char*)malloc((size_t)sz + 1);
  if(!buf){
    fclose(f);
    return NULL;
  }
  size_t got = fread(buf, 1, (size_t)sz, f);
  buf[got] = 0;
  fclose(f);
  return (unsigned char*)buf;
}

TN_EXPORT long long tn_write_file(unsigned char* path, unsigned char* data, long long len){
  const char* p = (const char*)path;
  FILE* f = fopen(p ? p : "", "wb");
  if(!f) return -1;
  if(len < 0){
    fclose(f);
    return -1;
  }
  size_t n = 0;
  if(data && len > 0){
    if((unsigned long long)len > (unsigned long long)SIZE_MAX){
      fclose(f);
      return -1;
    }
    n = fwrite(data, 1, (size_t)len, f);
  }
  fclose(f);
  return (long long)n;
}

#ifdef _WIN32
#include <windows.h>
TN_EXPORT void* tn_mmap_file(unsigned char* path, long long* out_size){
  const char* p = (const char*)path;
  HANDLE hFile = CreateFileA(p ? p : "", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if(hFile == INVALID_HANDLE_VALUE) return NULL;
  LARGE_INTEGER sz;
  if(!GetFileSizeEx(hFile, &sz)){ CloseHandle(hFile); return NULL; }
  if(out_size) *out_size = sz.QuadPart;
  HANDLE hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
  CloseHandle(hFile);
  if(!hMap) return NULL;
  void* view = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
  CloseHandle(hMap);
  return view;
}

TN_EXPORT void tn_munmap_file(void* view, long long size){
  (void)size;
  if(view) UnmapViewOfFile(view);
}
#else
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
TN_EXPORT void* tn_mmap_file(unsigned char* path, long long* out_size){
  const char* p = (const char*)path;
  int fd = open(p ? p : "", O_RDONLY);
  if(fd < 0) return NULL;
  struct stat st;
  if(fstat(fd, &st) < 0){ close(fd); return NULL; }
  if(out_size) *out_size = (long long)st.st_size;
  void* view = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if(view == MAP_FAILED) return NULL;
  return view;
}
TN_EXPORT void tn_munmap_file(void* view, long long size){
  if(view) munmap(view, (size_t)size);
}
#endif

TN_EXPORT unsigned char* tn_tzimage_decode_file(unsigned char* path, long long req_channels,
                                                long long* out_w, long long* out_h, long long* out_channels){
  if(out_w) *out_w = 0;
  if(out_h) *out_h = 0;
  if(out_channels) *out_channels = 0;

  const char* p = (const char*)path;
  if(!p || p[0] == 0){
    return NULL;
  }

  int rw = 0;
  int rh = 0;
  int rc = 0;
  int desired = 0;
  if(req_channels >= 1 && req_channels <= 4){
    desired = (int)req_channels;
  }

  unsigned char* dec = stbi_load(p, &rw, &rh, &rc, desired);
  if(!dec){
    return NULL;
  }
  if(rw <= 0 || rh <= 0){
    stbi_image_free(dec);
    return NULL;
  }

  if(out_w) *out_w = (long long)rw;
  if(out_h) *out_h = (long long)rh;
  if(out_channels) *out_channels = (long long)((desired > 0) ? desired : rc);
  return dec;
}

TN_EXPORT long long tn_tzimage_free(unsigned char* p){
  if(p){
    stbi_image_free((void*)p);
  }
  return 0;
}

TN_EXPORT unsigned char* tn_read_line(unsigned char* f){
  FILE* fp = (FILE*)f;
  if(!fp) return NULL;
  size_t cap = 128;
  size_t n = 0;
  char* buf = (char*)malloc(cap);
  if(!buf) return NULL;
  for(;;){
    int ch = fgetc(fp);
    if(ch == EOF || ch == '\n') break;
    if(ch == '\r'){
      int nx = fgetc(fp);
      if(nx != '\n' && nx != EOF) ungetc(nx, fp);
      break;
    }
    if(n + 1 >= cap){
      if(cap > (SIZE_MAX / 2)) { free(buf); return NULL; }
      size_t ncap = cap * 2;
      char* nb = (char*)realloc(buf, ncap);
      if(!nb){ free(buf); return NULL; }
      buf = nb; cap = ncap;
    }
    buf[n++] = (char)ch;
  }
  if(n + 1 >= cap){
    if(cap == SIZE_MAX){ free(buf); return NULL; }
    char* nb = (char*)realloc(buf, cap + 1);
    if(!nb){ free(buf); return NULL; }
    buf = nb;
  }
  buf[n] = 0;
  return (unsigned char*)buf;
}

TN_EXPORT long long tn_read_bytes(unsigned char* f, unsigned char* buf, long long n){
  FILE* fp = (FILE*)f;
  if(!fp || !buf || n <= 0) return 0;
  if((unsigned long long)n > (unsigned long long)SIZE_MAX) return 0;
  size_t got = fread(buf, 1, (size_t)n, fp);
  return (long long)got;
}

TN_EXPORT long long tn_write_line(unsigned char* f, unsigned char* s){
  FILE* fp = (FILE*)f;
  if(!fp) return -1;
  const char* p = (const char*)s;
  if(!p) p = "";
  if(fputs(p, fp) < 0) return -1;
  if(fputc('\n', fp) < 0) return -1;
  return 0;
}

TN_EXPORT long long tn_io_err(void){
  return (long long)errno;
}

TN_EXPORT long long tn_io_eof(unsigned char* f){
  FILE* fp = (FILE*)f;
  if(!fp) return 1;
  return (long long)feof(fp);
}

// ------------------------------
// Standard streams
// ------------------------------
TN_EXPORT unsigned char* tn_stdin(void){ return (unsigned char*)stdin; }
TN_EXPORT unsigned char* tn_stdout(void){ return (unsigned char*)stdout; }
TN_EXPORT unsigned char* tn_stderr(void){ return (unsigned char*)stderr; }

// ------------------------------
// OS / Path / FS helpers
// ------------------------------
TN_EXPORT unsigned char* tn_get_cwd(void){
  char buf[4096];
#ifdef _WIN32
  if(!_getcwd(buf, (int)sizeof(buf))) buf[0]=0;
#else
  if(!getcwd(buf, sizeof(buf))) buf[0]=0;
#endif
  return tn_dup_cstr(buf);
}

TN_EXPORT long long tn_chdir(unsigned char* path){
  const char* p = (const char*)path;
#ifdef _WIN32
  return (long long)_chdir(p ? p : "");
#else
  return (long long)chdir(p ? p : "");
#endif
}

TN_EXPORT long long tn_mkdir(unsigned char* path){
  const char* p = (const char*)path;
#ifdef _WIN32
  return (long long)_mkdir(p ? p : "");
#else
  return (long long)mkdir(p ? p : "", 0777);
#endif
}

TN_EXPORT long long tn_rmdir(unsigned char* path){
  const char* p = (const char*)path;
#ifdef _WIN32
  return (long long)_rmdir(p ? p : "");
#else
  return (long long)rmdir(p ? p : "");
#endif
}

TN_EXPORT long long tn_remove(unsigned char* path){
  const char* p = (const char*)path;
  return (long long)remove(p ? p : "");
}

TN_EXPORT long long tn_rename(unsigned char* oldp, unsigned char* newp){
  const char* a = (const char*)oldp;
  const char* b = (const char*)newp;
  return (long long)rename(a ? a : "", b ? b : "");
}

TN_EXPORT long long tn_path_exists(unsigned char* path){
  const char* p = (const char*)path;
#ifdef _WIN32
  int r = _access(p ? p : "", 0);
#else
  int r = access(p ? p : "", F_OK);
#endif
  return (long long)(r==0);
}

TN_EXPORT long long tn_path_is_dir(unsigned char* path){
  const char* p = (const char*)path;
  int ok = 0;
#ifdef _WIN32
  DWORD attr = GetFileAttributesA(p ? p : "");
  if(attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) ok = 1;
#else
  struct stat st;
  if(p && stat(p, &st) == 0 && S_ISDIR(st.st_mode)) ok = 1;
#endif
  return (long long)ok;
}

TN_EXPORT unsigned char* tn_path_join(unsigned char* a, unsigned char* b){
  return tn_path_join_impl((const char*)a, (const char*)b);
}

TN_EXPORT long long tn_path_sep(void){
#ifdef _WIN32
  return (long long)'\\';
#else
  return (long long)'/';
#endif
}

TN_EXPORT unsigned char* tn_path_basename(unsigned char* p){
  return tn_path_basename_impl((const char*)p);
}

TN_EXPORT unsigned char* tn_path_dirname(unsigned char* p){
  return tn_path_dirname_impl((const char*)p);
}

TN_EXPORT unsigned char* tn_list_dir(unsigned char* path){
  return tn_list_dir_impl((const char*)path);
}

TN_EXPORT unsigned char* tn_list_dir_recursive(unsigned char* path){
  return tn_list_dir_recursive_impl((const char*)path);
}

TN_EXPORT unsigned char* tn_glob(unsigned char* pattern){
  return tn_glob_impl((const char*)pattern);
}

TN_EXPORT unsigned char* tn_path_normalize(unsigned char* path){
  return tn_path_normalize_impl((const char*)path);
}

TN_EXPORT unsigned char* tn_path_normalize_opts(unsigned char* path, long long flags){
  return tn_path_normalize_opts_impl((const char*)path, flags);
}

TN_EXPORT long long tn_time_now(void){
  return tn_time_now_ns_impl() / 1000000000LL;
}

TN_EXPORT long long tn_time_now_ms(void){
  return tn_time_now_ns_impl() / 1000000LL;
}

TN_EXPORT long long tn_time_now_ns(void){
  return tn_time_now_ns_impl();
}

TN_EXPORT unsigned char* tn_date_now(void){
  return tn_date_now_impl(0);
}

TN_EXPORT unsigned char* tn_date_now_utc(void){
  return tn_date_now_impl(1);
}

TN_EXPORT long long tn_sleep_ms(long long ms){
  tn_sleep_ms_impl(ms);
  return 0;
}

TN_EXPORT unsigned char* tn_fb_addr(void){
  return tn_host_fb_addr();
}

TN_EXPORT long long tn_fb_width(void){
  return tn_host_fb_width();
}

TN_EXPORT long long tn_fb_height(void){
  return tn_host_fb_height();
}

TN_EXPORT long long tn_fb_pitch(void){
  return tn_host_fb_pitch();
}

TN_EXPORT long long tn_fb_bpp(void){
  return tn_host_fb_bpp();
}

TN_EXPORT long long tn_os_system(unsigned char* cmd){
  if(!cmd) return -1;
  return system((const char*)cmd);
}

#ifdef _WIN32
  #include <io.h>
  #include <fcntl.h>
#else
  #include <unistd.h>
#endif

TN_EXPORT long long tn_io_pipe_create(long long* read_fd, long long* write_fd) {
  int fds[2];
#ifdef _WIN32
  int r = _pipe(fds, 4096, _O_BINARY);
#else
  int r = pipe(fds);
#endif
  if (r == 0) {
    if (read_fd) *read_fd = fds[0];
    if (write_fd) *write_fd = fds[1];
    return 0;
  }
  return -1;
}

TN_EXPORT long long tn_io_fd_read(long long fd, unsigned char* buf, long long n) {
#ifdef _WIN32
  return _read((int)fd, buf, (unsigned int)n);
#else
  return read((int)fd, buf, (size_t)n);
#endif
}

TN_EXPORT long long tn_io_fd_write(long long fd, unsigned char* buf, long long n) {
#ifdef _WIN32
  return _write((int)fd, buf, (unsigned int)n);
#else
  return write((int)fd, buf, (size_t)n);
#endif
}

TN_EXPORT long long tn_io_fd_close(long long fd) {
#ifdef _WIN32
  return _close((int)fd);
#else
  return close((int)fd);
#endif
}

TN_EXPORT long long tn_screen_width(void){
  return tn_host_screen_width();
}

TN_EXPORT long long tn_screen_height(void){
  return tn_host_screen_height();
}

TN_EXPORT long long tn_boot_text(void){
  return tn_host_boot_text();
}

TN_EXPORT long long tn_fb_init_ex(unsigned char* title, long long W, long long H){
  return tn_host_fb_init_ex((const char*)title, W, H);
}

TN_EXPORT long long tn_fb_present(void){
  return tn_host_fb_present();
}

TN_EXPORT long long tn_fb_text(long long x, long long y, unsigned char* s, unsigned int color){
  return tn_host_fb_text(x, y, s, color);
}

TN_EXPORT long long tn_fb_text_ex(long long x, long long y, unsigned char* s, unsigned int color, long long scale, long long bold){
  return tn_host_fb_text_ex(x, y, s, color, scale, bold);
}

TN_EXPORT long long tn_fb_soft_cursor(long long x, long long y, unsigned int color){
  return tn_host_fb_soft_cursor(x, y, color);
}

TN_EXPORT long long tn_fb_fill_rounded(long long x, long long y, long long w, long long h, unsigned int color, long long radius){
  return tn_host_fb_fill_rounded(x, y, w, h, color, radius);
}

TN_EXPORT long long tn_fb_set_scale(long long scale){
  return tn_host_fb_set_scale(scale);
}

TN_EXPORT long long tn_fb_get_scale(void){
  return tn_host_fb_get_scale();
}

TN_EXPORT long long tn_fb_text_mode(long long mode){
  return tn_host_fb_text_mode(mode);
}

TN_EXPORT long long tn_fb_font_aa_mode(long long mode){
  return tn_host_fb_font_aa_mode(mode);
}

TN_EXPORT long long tn_fb_font_ttf_mode(long long mode){
  return tn_host_fb_font_ttf_mode(mode);
}

TN_EXPORT long long tn_fb_draw_logo(long long x, long long y, long long w, long long h){
  return tn_host_fb_draw_logo(x, y, w, h);
}

TN_EXPORT long long tn_fb_draw_wallpaper(long long x, long long y, long long w, long long h){
  return tn_host_fb_draw_wallpaper(x, y, w, h);
}

TN_EXPORT long long tn_fb_draw_cursor(long long x, long long y, long long scale){
  return tn_host_fb_draw_cursor(x, y, scale);
}

TN_EXPORT long long tn_fb_draw_app_icon(long long app, long long x, long long y, long long w, long long h){
  return tn_host_fb_draw_app_icon(app, x, y, w, h);
}

TN_EXPORT long long tn_fb_fill(unsigned int color){
  return tn_host_fb_fill(color);
}

TN_EXPORT long long tn_fb_fill_rect(long long x, long long y, long long w, long long h, unsigned int color){
  return tn_host_fb_fill_rect(x, y, w, h, color);
}

TN_EXPORT long long tn_fb_blit(long long sx, long long sy, long long w, long long h, long long dx, long long dy){
  return tn_host_fb_blit(sx, sy, w, h, dx, dy);
}

TN_EXPORT long long tn_fb_composite_rect(long long x, long long y, long long w, long long h, unsigned int color, long long alpha){
  return tn_host_fb_composite_rect(x, y, w, h, color, alpha);
}

TN_EXPORT long long tn_fb_get_pixel(long long x, long long y){
  return tn_host_fb_get_pixel(x, y);
}

TN_EXPORT long long tn_fb_put_pixel(long long x, long long y, unsigned int color){
  return tn_host_fb_put_pixel(x, y, color);
}

TN_EXPORT long long tn_fb_cursor_soft_reset(void){
  return tn_host_fb_cursor_soft_reset();
}

TN_EXPORT long long tn_fb_cursor_soft_move(long long x, long long y, long long scale){
  return tn_host_fb_cursor_soft_move(x, y, scale);
}

TN_EXPORT long long tn_fb_cursor_kind(long long kind){
  return tn_host_fb_cursor_kind(kind);
}

TN_EXPORT long long tn_fb_set_color(unsigned int color){
  return tn_host_fb_set_color(color);
}

TN_EXPORT long long tn_fb_set_cursor(long long x, long long y){
  return tn_host_fb_set_cursor(x, y);
}

TN_EXPORT long long tn_fb_cursor_mode(long long mode){
  return tn_host_fb_cursor_mode(mode);
}

TN_EXPORT long long tn_fb_cursor(long long on, unsigned int color){
  return tn_host_fb_cursor(on, color);
}

TN_EXPORT long long tn_fb_host_windowing(void){
  return tn_host_fb_host_windowing();
}

TN_EXPORT long long tn_kbd_has_event(void){
  return tn_host_kbd_has_event();
}

TN_EXPORT long long tn_kbd_read_scancode(void){
  return tn_host_kbd_read_scancode();
}

TN_EXPORT long long tn_kbd_read_scancode_raw(void){
  return tn_host_kbd_read_scancode_raw();
}

TN_EXPORT long long tn_kbd_read_char(void){
  return tn_host_kbd_read_char();
}

TN_EXPORT long long tn_kbd_ime_active(void){
  return tn_host_kbd_ime_active();
}

TN_EXPORT long long tn_kbd_ime_cursor(void){
  return tn_host_kbd_ime_cursor();
}

TN_EXPORT long long tn_kbd_ime_length(void){
  return tn_host_kbd_ime_length();
}

TN_EXPORT long long tn_kbd_set_debug(long long on){
  return tn_host_kbd_set_debug(on);
}

TN_EXPORT long long tn_kbd_last_scancode(void){
  return tn_host_kbd_last_scancode();
}

TN_EXPORT long long tn_mouse_has_packet(void){
  return tn_host_mouse_has_packet();
}

TN_EXPORT long long tn_mouse_read_packet(void){
  return tn_host_mouse_read_packet();
}

TN_EXPORT long long tn_mouse_dx(void){
  return tn_host_mouse_dx();
}

TN_EXPORT long long tn_mouse_dy(void){
  return tn_host_mouse_dy();
}

TN_EXPORT long long tn_mouse_pos_x(void){
  return tn_host_mouse_pos_x();
}

TN_EXPORT long long tn_mouse_pos_y(void){
  return tn_host_mouse_pos_y();
}

TN_EXPORT long long tn_mouse_buttons(void){
  return tn_host_mouse_buttons();
}

TN_EXPORT long long tn_bit_popcnt(long long x){
  return tn_popcnt64((unsigned long long)x);
}

TN_EXPORT long long tn_bit_clz(long long x){
  return tn_clz64((unsigned long long)x);
}

TN_EXPORT long long tn_bit_ctz(long long x){
  return tn_ctz64((unsigned long long)x);
}

TN_EXPORT long long tn_bit_bswap(long long x){
  return (long long)tn_bswap64((unsigned long long)x);
}

TN_EXPORT long long tn_bit_rotl(long long x, long long shift){
  return (long long)tn_rotl64((unsigned long long)x, (unsigned long long)shift);
}

TN_EXPORT long long tn_bit_rotr(long long x, long long shift){
  return (long long)tn_rotr64((unsigned long long)x, (unsigned long long)shift);
}

TN_EXPORT long long tn_cpu_has_sse2(void){
  return (long long)tn_cpu_has_sse2_impl();
}

TN_EXPORT long long tn_cpu_has_avx2(void){
  return (long long)tn_cpu_has_avx2_impl();
}

TN_EXPORT long long tn_cpu_has_fma(void){
  return (long long)tn_cpu_has_fma_impl();
}

TN_EXPORT long long tn_cpu_has_neon(void){
  return (long long)tn_cpu_has_neon_impl();
}

TN_EXPORT unsigned char* tn_async_spawn(unsigned char* fnptr){
  if(!fnptr) return NULL;
  if(tn_sched_init() != 0) return NULL;
  tn_async_task* t = (tn_async_task*)malloc(sizeof(tn_async_task));
  if(!t) return NULL;
  memset(t, 0, sizeof(*t));
  t->fn = (tn_async_fn0)fnptr;
#ifdef _WIN32
  InitializeSRWLock(&t->lock);
  InitializeConditionVariable(&t->cv);
#else
  pthread_mutex_init(&t->lock, NULL);
  pthread_cond_init(&t->cv, NULL);
#endif
  int wid = tn_worker_id;
  if(wid < 0 || wid >= tn_sched_n){
    long long rr = tn_sched_rr++;
    if(rr < 0) rr = 0;
    wid = (int)(rr % tn_sched_n);
  }
  TN_SCHED_LOCK();
  tn_sched_pending++;
  TN_SCHED_UNLOCK();
  tn_deque_push_head(&tn_sched_workers[wid].dq, t);
  TN_SCHED_LOCK();
#ifdef _WIN32
  WakeConditionVariable(&tn_sched_cv);
#else
  pthread_cond_signal(&tn_sched_cv);
#endif
  TN_SCHED_UNLOCK();
  return (unsigned char*)t;
}

TN_EXPORT long long tn_async_await(unsigned char* handle){
  tn_async_task* t = (tn_async_task*)handle;
  if(!t) return 0;
#ifdef _WIN32
  AcquireSRWLockExclusive(&t->lock);
  while(!t->done){
    SleepConditionVariableSRW(&t->cv, &t->lock, INFINITE, 0);
  }
  long long r = t->result;
  ReleaseSRWLockExclusive(&t->lock);
#else
  pthread_mutex_lock(&t->lock);
  while(!t->done){
    pthread_cond_wait(&t->cv, &t->lock);
  }
  long long r = t->result;
  pthread_mutex_unlock(&t->lock);
  pthread_mutex_destroy(&t->lock);
  pthread_cond_destroy(&t->cv);
#endif
  free(t);
  return r;
}

TN_EXPORT long long tn_async_workers(void){
  if(tn_sched_init() != 0) return 0;
  return (long long)tn_sched_n;
}

TN_EXPORT long long tn_async_pending(void){
  long long pending = 0;
  TN_SCHED_LOCK();
  pending = tn_sched_pending;
  TN_SCHED_UNLOCK();
  return pending;
}

#if defined(__GNUC__) || defined(__clang__)
#define TN_SIMD_V4 1
typedef double tn_v4f __attribute__((vector_size(32)));
typedef long long tn_v4i __attribute__((vector_size(32)));
#else
#define TN_SIMD_V4 0
#endif

TN_EXPORT long long tn_simd_v4f_add(double* out, const double* a, const double* b){
#if TN_SIMD_V4
  tn_v4f va, vb;
  memcpy(&va, a, sizeof(va));
  memcpy(&vb, b, sizeof(vb));
  tn_v4f vr = va + vb;
  memcpy(out, &vr, sizeof(vr));
#else
  out[0] = a[0] + b[0];
  out[1] = a[1] + b[1];
  out[2] = a[2] + b[2];
  out[3] = a[3] + b[3];
#endif
  return 0;
}

TN_EXPORT long long tn_simd_v4f_sub(double* out, const double* a, const double* b){
#if TN_SIMD_V4
  tn_v4f va, vb;
  memcpy(&va, a, sizeof(va));
  memcpy(&vb, b, sizeof(vb));
  tn_v4f vr = va - vb;
  memcpy(out, &vr, sizeof(vr));
#else
  out[0] = a[0] - b[0];
  out[1] = a[1] - b[1];
  out[2] = a[2] - b[2];
  out[3] = a[3] - b[3];
#endif
  return 0;
}

TN_EXPORT long long tn_simd_v4f_mul(double* out, const double* a, const double* b){
#if TN_SIMD_V4
  tn_v4f va, vb;
  memcpy(&va, a, sizeof(va));
  memcpy(&vb, b, sizeof(vb));
  tn_v4f vr = va * vb;
  memcpy(out, &vr, sizeof(vr));
#else
  out[0] = a[0] * b[0];
  out[1] = a[1] * b[1];
  out[2] = a[2] * b[2];
  out[3] = a[3] * b[3];
#endif
  return 0;
}

TN_EXPORT long long tn_simd_v4f_dot(const double* a, const double* b){
  double r = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];
  long long bits = 0;
  memcpy(&bits, &r, sizeof(bits));
  return bits;
}

TN_EXPORT long long tn_simd_v4i_add(long long* out, const long long* a, const long long* b){
#if TN_SIMD_V4
  tn_v4i va, vb;
  memcpy(&va, a, sizeof(va));
  memcpy(&vb, b, sizeof(vb));
  tn_v4i vr = va + vb;
  memcpy(out, &vr, sizeof(vr));
#else
  out[0] = a[0] + b[0];
  out[1] = a[1] + b[1];
  out[2] = a[2] + b[2];
  out[3] = a[3] + b[3];
#endif
  return 0;
}

TN_EXPORT long long tn_simd_v4i_mul(long long* out, const long long* a, const long long* b){
#if TN_SIMD_V4
  tn_v4i va, vb;
  memcpy(&va, a, sizeof(va));
  memcpy(&vb, b, sizeof(vb));
  tn_v4i vr = va * vb;
  memcpy(out, &vr, sizeof(vr));
#else
  out[0] = a[0] * b[0];
  out[1] = a[1] * b[1];
  out[2] = a[2] * b[2];
  out[3] = a[3] * b[3];
#endif
  return 0;
}

TN_EXPORT long long tn_simd_v4f_add_strided(double* out, const double* a, const double* b, long long stride){
  size_t s = (size_t)stride;
  out[0] = a[0] + b[0];
  out[s] = a[s] + b[s];
  out[s*2] = a[s*2] + b[s*2];
  out[s*3] = a[s*3] + b[s*3];
  return 0;
}

TN_EXPORT long long tn_simd_v4f_sub_strided(double* out, const double* a, const double* b, long long stride){
  size_t s = (size_t)stride;
  out[0] = a[0] - b[0];
  out[s] = a[s] - b[s];
  out[s*2] = a[s*2] - b[s*2];
  out[s*3] = a[s*3] - b[s*3];
  return 0;
}

TN_EXPORT long long tn_simd_v4f_mul_strided(double* out, const double* a, const double* b, long long stride){
  size_t s = (size_t)stride;
  out[0] = a[0] * b[0];
  out[s] = a[s] * b[s];
  out[s*2] = a[s*2] * b[s*2];
  out[s*3] = a[s*3] * b[s*3];
  return 0;
}

TN_EXPORT long long tn_simd_v4i_add_strided(long long* out, const long long* a, const long long* b, long long stride){
  size_t s = (size_t)stride;
  out[0] = a[0] + b[0];
  out[s] = a[s] + b[s];
  out[s*2] = a[s*2] + b[s*2];
  out[s*3] = a[s*3] + b[s*3];
  return 0;
}

TN_EXPORT long long tn_simd_v4i_mul_strided(long long* out, const long long* a, const long long* b, long long stride){
  size_t s = (size_t)stride;
  out[0] = a[0] * b[0];
  out[s] = a[s] * b[s];
  out[s*2] = a[s*2] * b[s*2];
  out[s*3] = a[s*3] * b[s*3];
  return 0;
}

TN_EXPORT long long tn_simd_v4f_load(double* out, const double* p){
  memcpy(out, p, sizeof(double) * 4);
  return 0;
}

TN_EXPORT long long tn_simd_v4f_store(double* p, const double* v){
  memcpy(p, v, sizeof(double) * 4);
  return 0;
}

TN_EXPORT long long tn_simd_v4i_load(long long* out, const long long* p){
  memcpy(out, p, sizeof(long long) * 4);
  return 0;
}

TN_EXPORT long long tn_simd_v4i_store(long long* p, const long long* v){
  memcpy(p, v, sizeof(long long) * 4);
  return 0;
}

static float tn_ai_inv_sqrt_f32(float x){
  if(x <= 0.0f) return 0.0f;
  float y = (x > 1.0f) ? (1.0f / x) : 1.0f;
  for(int i = 0; i < 6; ++i){
    y = y * (1.5f - (0.5f * x * y * y));
  }
  return y;
}

static long long tn_ai_vec_add_f32_scalar(float* out, const float* a, const float* b, long long n){
  if(!out || !a || !b || n <= 0) return -1;
  for(long long i = 0; i < n; ++i) out[i] = a[i] + b[i];
  return 0;
}

static long long tn_ai_vec_mul_f32_scalar(float* out, const float* a, const float* b, long long n){
  if(!out || !a || !b || n <= 0) return -1;
  for(long long i = 0; i < n; ++i) out[i] = a[i] * b[i];
  return 0;
}

static long long tn_ai_vec_axpy_f32_scalar(float* out, const float* a, const float* b, float alpha, long long n){
  if(!out || !a || !b || n <= 0) return -1;
  for(long long i = 0; i < n; ++i) out[i] = a[i] + alpha * b[i];
  return 0;
}

static long long tn_ai_dot_f32_scalar(const float* a, const float* b, long long n, float* out){
  if(!a || !b || !out || n <= 0) return -1;
  float acc = 0.0f;
  for(long long i = 0; i < n; ++i) acc += a[i] * b[i];
  *out = acc;
  return 0;
}

static long long tn_ai_matmul_f32_scalar(float* out, const float* a, const float* b,
                                         long long m, long long n, long long k){
  if(!out || !a || !b || !tn_ai_validate_matmul_shape(m, n, k)) return -1;
  for(long long row = 0; row < m; ++row){
    for(long long col = 0; col < n; ++col){
      float acc = 0.0f;
      for(long long p = 0; p < k; ++p){
        acc += a[row * k + p] * b[p * n + col];
      }
      out[row * n + col] = acc;
    }
  }
  return 0;
}

static long long tn_ai_rmsnorm_rows_f32_scalar(float* out, const float* x, const float* weight,
                                                long long rows, long long cols, float eps){
  if(!out || !x || !tn_ai_validate_rmsnorm_shape(rows, cols)) return -1;
  eps = tn_ai_norm_eps(eps);
  for(long long r = 0; r < rows; ++r){
    const float* src = x + (r * cols);
    float* dst = out + (r * cols);
    float sum_sq = 0.0f;
    for(long long c = 0; c < cols; ++c){
      float v = src[c];
      sum_sq += v * v;
    }
    float mean_sq = (sum_sq / (float)cols) + eps;
    float inv = tn_ai_inv_sqrt_f32(mean_sq);
    if(weight){
      for(long long c = 0; c < cols; ++c){
        dst[c] = src[c] * inv * weight[c];
      }
    } else {
      for(long long c = 0; c < cols; ++c){
        dst[c] = src[c] * inv;
      }
    }
  }
  return 0;
}

#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
__attribute__((target("avx2,fma")))
static long long tn_ai_matmul_f32_avx2(float* out, const float* a, const float* b,
                                       long long m, long long n, long long k){
  if(!out || !a || !b || !tn_ai_validate_matmul_shape(m, n, k)) return -1;
  for(long long row = 0; row < m; ++row){
    const float* arow = a + (row * k);
    long long col = 0;
    for(; col + 8 <= n; col += 8){
      __m256 acc = _mm256_setzero_ps();
      for(long long p = 0; p < k; ++p){
        __m256 bv = _mm256_loadu_ps(b + (p * n) + col);
        __m256 av = _mm256_set1_ps(arow[p]);
        acc = _mm256_fmadd_ps(av, bv, acc);
      }
      _mm256_storeu_ps(out + (row * n) + col, acc);
    }
    for(; col < n; ++col){
      float acc = 0.0f;
      for(long long p = 0; p < k; ++p){
        acc += arow[p] * b[p * n + col];
      }
      out[row * n + col] = acc;
    }
  }
  return 0;
}

__attribute__((target("avx2,fma")))
static long long tn_ai_rmsnorm_rows_f32_avx2(float* out, const float* x, const float* weight,
                                             long long rows, long long cols, float eps){
  if(!out || !x || !tn_ai_validate_rmsnorm_shape(rows, cols)) return -1;
  eps = tn_ai_norm_eps(eps);

  for(long long r = 0; r < rows; ++r){
    const float* src = x + (r * cols);
    float* dst = out + (r * cols);
    __m256 accv = _mm256_setzero_ps();
    long long c = 0;
    for(; c + 8 <= cols; c += 8){
      __m256 v = _mm256_loadu_ps(src + c);
      accv = _mm256_fmadd_ps(v, v, accv);
    }
    float partial[8];
    _mm256_storeu_ps(partial, accv);
    float sum_sq = partial[0] + partial[1] + partial[2] + partial[3] +
                   partial[4] + partial[5] + partial[6] + partial[7];
    for(; c < cols; ++c){
      float v = src[c];
      sum_sq += v * v;
    }

    float mean_sq = (sum_sq / (float)cols) + eps;
    float inv = tn_ai_inv_sqrt_f32(mean_sq);
    __m256 invv = _mm256_set1_ps(inv);
    c = 0;
    if(weight){
      for(; c + 8 <= cols; c += 8){
        __m256 xv = _mm256_loadu_ps(src + c);
        __m256 wv = _mm256_loadu_ps(weight + c);
        __m256 yv = _mm256_mul_ps(_mm256_mul_ps(xv, invv), wv);
        _mm256_storeu_ps(dst + c, yv);
      }
      for(; c < cols; ++c){
        dst[c] = src[c] * inv * weight[c];
      }
    } else {
      for(; c + 8 <= cols; c += 8){
        __m256 xv = _mm256_loadu_ps(src + c);
        __m256 yv = _mm256_mul_ps(xv, invv);
        _mm256_storeu_ps(dst + c, yv);
      }
      for(; c < cols; ++c){
        dst[c] = src[c] * inv;
      }
    }
  }
  return 0;
}
#endif

TN_EXPORT long long tezz_ai_vec_add_f32(float* out, const float* a, const float* b, long long n){
  return tn_ai_vec_add_f32_scalar(out, a, b, n);
}

TN_EXPORT long long tezz_ai_vec_mul_f32(float* out, const float* a, const float* b, long long n){
  return tn_ai_vec_mul_f32_scalar(out, a, b, n);
}

TN_EXPORT long long tezz_ai_vec_axpy_f32(float* out, const float* a, const float* b,
                                         float alpha, long long n){
  return tn_ai_vec_axpy_f32_scalar(out, a, b, alpha, n);
}

TN_EXPORT long long tezz_ai_dot_f32(const float* a, const float* b, long long n, float* out){
  return tn_ai_dot_f32_scalar(a, b, n, out);
}

TN_EXPORT long long tezz_ai_matmul_f32(float* out, const float* a, const float* b,
                                       long long m, long long n, long long k){
#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
  if(tn_cpu_has_avx2_impl() && tn_cpu_has_fma_impl()){
    return tn_ai_matmul_f32_avx2(out, a, b, m, n, k);
  }
#endif
  return tn_ai_matmul_f32_scalar(out, a, b, m, n, k);
}

TN_EXPORT long long tezz_ai_rmsnorm_rows_f32(float* out, const float* x, const float* weight,
                                             long long rows, long long cols, float eps){
#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
  if(tn_cpu_has_avx2_impl() && tn_cpu_has_fma_impl()){
    return tn_ai_rmsnorm_rows_f32_avx2(out, x, weight, rows, cols, eps);
  }
#endif
  return tn_ai_rmsnorm_rows_f32_scalar(out, x, weight, rows, cols, eps);
}

TN_EXPORT long long tezz_ai_matmul_f64(double* out, const double* a, const double* b,
                                       long long m, long long n, long long k){
  if(!out || !a || !b || !tn_ai_validate_matmul_shape_elem(m, n, k, (long long)sizeof(double))) return -1;
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

TN_EXPORT long long tezz_ai_matmul_i8_f64(double* out, const double* a, const signed char* b,
                                          const double* scales, long long m, long long n, long long k){
  if(!out || !a || !b || !tn_ai_validate_matmul_i8_f64_shape(m, n, k)) return -1;
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

TN_EXPORT long long tezz_ai_rmsnorm_rows_f64(double* out, const double* x, const double* weight,
                                             long long rows, long long cols, double eps){
  if(!out || !x || !tn_ai_validate_rows_cols_shape_elem(rows, cols, (long long)sizeof(double))) return -1;
  eps = tn_ai_norm_eps_f64(eps);
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

TN_EXPORT long long tezz_ai_softmax_rows_f64(double* out, const double* x,
                                             long long rows, long long cols){
  if(!out || !x || !tn_ai_validate_rows_cols_shape_elem(rows, cols, (long long)sizeof(double))) return -1;
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

TN_EXPORT long long tezz_ai_rope_f64(double* q, double* k, long long pos,
                                     long long dim, double base){
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

#if defined(_WIN32) && defined(TN_GPU_DIRECTML)

#define TN_SAFE_RELEASE(x) do{ if((x)!=NULL){ (x)->Release(); (x)=NULL; } }while(0)

typedef struct tn_dml_ctx {
  IDXGIFactory1* factory;
  IDXGIAdapter1* adapter;
  ID3D12Device* device;
  ID3D12CommandQueue* queue;
  ID3D12CommandAllocator* allocator;
  ID3D12GraphicsCommandList* list;
  ID3D12Fence* fence;
  HANDLE fence_event;
  UINT64 fence_value;
  IDMLDevice* dml;
} tn_dml_ctx;

static tn_dml_ctx g_dml;
static int g_dml_inited = 0;

typedef struct tn_res_state {
  ID3D12Resource* res;
  D3D12_RESOURCE_STATES state;
} tn_res_state;

static tn_res_state* g_res_states = NULL;
static int g_res_state_n = 0;
static int g_res_state_cap = 0;

static D3D12_RESOURCE_STATES tn_res_get(ID3D12Resource* r){
  for(int i=0;i<g_res_state_n;i++){
    if(g_res_states[i].res == r) return g_res_states[i].state;
  }
  return D3D12_RESOURCE_STATE_COMMON;
}

static void tn_res_set(ID3D12Resource* r, D3D12_RESOURCE_STATES st){
  for(int i=0;i<g_res_state_n;i++){
    if(g_res_states[i].res == r){
      g_res_states[i].state = st;
      return;
    }
  }
  if(g_res_state_n == g_res_state_cap){
    g_res_state_cap = g_res_state_cap ? g_res_state_cap * 2 : 64;
    g_res_states = (tn_res_state*)realloc(g_res_states, sizeof(tn_res_state) * (size_t)g_res_state_cap);
    if(!g_res_states) return;
  }
  g_res_states[g_res_state_n].res = r;
  g_res_states[g_res_state_n].state = st;
  g_res_state_n++;
}

static void tn_res_del(ID3D12Resource* r){
  for(int i=0;i<g_res_state_n;i++){
    if(g_res_states[i].res == r){
      g_res_states[i] = g_res_states[g_res_state_n - 1];
      g_res_state_n--;
      return;
    }
  }
}

static void tn_dml_release(void){
  TN_SAFE_RELEASE(g_dml.dml);
  TN_SAFE_RELEASE(g_dml.list);
  TN_SAFE_RELEASE(g_dml.allocator);
  TN_SAFE_RELEASE(g_dml.queue);
  TN_SAFE_RELEASE(g_dml.fence);
  TN_SAFE_RELEASE(g_dml.device);
  TN_SAFE_RELEASE(g_dml.adapter);
  TN_SAFE_RELEASE(g_dml.factory);
  if(g_dml.fence_event){
    CloseHandle(g_dml.fence_event);
    g_dml.fence_event = NULL;
  }
  g_dml.fence_value = 0;
  free(g_res_states);
  g_res_states = NULL;
  g_res_state_n = 0;
  g_res_state_cap = 0;
}

static int tn_dml_wait(void){
  g_dml.fence_value++;
  if(FAILED(g_dml.queue->Signal(g_dml.fence, g_dml.fence_value))) return -1;
  if(g_dml.fence->GetCompletedValue() < g_dml.fence_value){
    if(FAILED(g_dml.fence->SetEventOnCompletion(g_dml.fence_value, g_dml.fence_event))) return -1;
    WaitForSingleObject(g_dml.fence_event, INFINITE);
  }
  return 0;
}

static int tn_dml_begin(void){
  if(FAILED(g_dml.allocator->Reset())) return -1;
  if(FAILED(g_dml.list->Reset(g_dml.allocator, NULL))) return -1;
  return 0;
}

static int tn_dml_end(void){
  if(FAILED(g_dml.list->Close())) return -1;
  ID3D12CommandList* lists[1];
  lists[0] = (ID3D12CommandList*)g_dml.list;
  g_dml.queue->ExecuteCommandLists(1, lists);
  return tn_dml_wait();
}

static void tn_dml_barrier(ID3D12Resource* res, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after){
  if(!res || before == after) return;
  D3D12_RESOURCE_BARRIER bar;
  memset(&bar, 0, sizeof(bar));
  bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  bar.Transition.pResource = res;
  bar.Transition.StateBefore = before;
  bar.Transition.StateAfter = after;
  bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  g_dml.list->ResourceBarrier(1, &bar);
}

static int tn_dml_init(void){
  if(g_dml_inited) return 0;
  memset(&g_dml, 0, sizeof(g_dml));

  HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&g_dml.factory));
  if(FAILED(hr)) return -1;

  IDXGIAdapter1* adapter = NULL;
  for(UINT i=0; g_dml.factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++){
    DXGI_ADAPTER_DESC1 desc;
    adapter->GetDesc1(&desc);
    if(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE){
      adapter->Release();
      continue;
    }
    if(SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_dml.device)))){
      g_dml.adapter = adapter;
      break;
    }
    adapter->Release();
  }
  if(!g_dml.device){
    tn_dml_release();
    return -1;
  }

  D3D12_COMMAND_QUEUE_DESC qd;
  memset(&qd, 0, sizeof(qd));
  qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if(FAILED(g_dml.device->CreateCommandQueue(&qd, IID_PPV_ARGS(&g_dml.queue)))){ tn_dml_release(); return -1; }
  if(FAILED(g_dml.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_dml.allocator)))){ tn_dml_release(); return -1; }
  if(FAILED(g_dml.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_dml.allocator, NULL, IID_PPV_ARGS(&g_dml.list)))){ tn_dml_release(); return -1; }
  g_dml.list->Close();

  if(FAILED(g_dml.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_dml.fence)))){ tn_dml_release(); return -1; }
  g_dml.fence_event = CreateEvent(NULL, FALSE, FALSE, NULL);
  if(!g_dml.fence_event){ tn_dml_release(); return -1; }

  if(FAILED(DMLCreateDevice(g_dml.device, DML_CREATE_DEVICE_FLAG_NONE, IID_PPV_ARGS(&g_dml.dml)))){ tn_dml_release(); return -1; }

  g_dml_inited = 1;
  return 0;
}

TN_EXPORT long long tn_gpu_has_backend(void){
  if(tn_dml_init() != 0) return 0;
  return 1;
}

TN_EXPORT long long tn_gpu_init(void){
  return tn_dml_init() == 0 ? 0 : -1;
}

TN_EXPORT long long tn_gpu_shutdown(void){
  tn_dml_release();
  g_dml_inited = 0;
  return 0;
}

TN_EXPORT long long tn_gpu_device_count(void){
  if(!g_dml.device) return 0;
  return 1;
}

TN_EXPORT long long tn_gpu_set_device(long long id){
  (void)id;
  return g_dml.device ? 0 : -1;
}

TN_EXPORT unsigned char* tn_gpu_backend_name(void){
  return tn_dup_cstr("directml");
}

TN_EXPORT long long tn_gpu_supports_dxil(void){
  return 1;
}

TN_EXPORT unsigned char* tn_gpu_alloc(long long bytes){
  if(!g_dml.device || bytes <= 0) return NULL;
  D3D12_HEAP_PROPERTIES hp;
  memset(&hp, 0, sizeof(hp));
  hp.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC rd;
  memset(&rd, 0, sizeof(rd));
  rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  rd.Width = (UINT64)bytes;
  rd.Height = 1;
  rd.DepthOrArraySize = 1;
  rd.MipLevels = 1;
  rd.SampleDesc.Count = 1;
  rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ID3D12Resource* res = NULL;
  if(FAILED(g_dml.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, NULL, IID_PPV_ARGS(&res)))){
    return NULL;
  }
  tn_res_set(res, D3D12_RESOURCE_STATE_COMMON);
  return (unsigned char*)res;
}

TN_EXPORT long long tn_gpu_free(unsigned char* p){
  ID3D12Resource* res = (ID3D12Resource*)p;
  if(res){
    tn_res_del(res);
    res->Release();
  }
  return 0;
}

TN_EXPORT long long tn_gpu_memcpy_to_device(unsigned char* dst, unsigned char* src, long long bytes){
  if(!g_dml.device || !dst || !src || bytes <= 0) return -1;
  ID3D12Resource* dres = (ID3D12Resource*)dst;

  D3D12_HEAP_PROPERTIES hp;
  memset(&hp, 0, sizeof(hp));
  hp.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC rd;
  memset(&rd, 0, sizeof(rd));
  rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  rd.Width = (UINT64)bytes;
  rd.Height = 1;
  rd.DepthOrArraySize = 1;
  rd.MipLevels = 1;
  rd.SampleDesc.Count = 1;
  rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  ID3D12Resource* up = NULL;
  if(FAILED(g_dml.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(&up)))){
    return -1;
  }
  void* map = NULL;
  if(FAILED(up->Map(0, NULL, &map))){
    up->Release();
    return -1;
  }
  memcpy(map, src, (size_t)bytes);
  up->Unmap(0, NULL);

  if(tn_dml_begin() != 0){ up->Release(); return -1; }
  D3D12_RESOURCE_STATES st = tn_res_get(dres);
  tn_dml_barrier(dres, st, D3D12_RESOURCE_STATE_COPY_DEST);
  g_dml.list->CopyBufferRegion(dres, 0, up, 0, (UINT64)bytes);
  tn_dml_barrier(dres, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  int ok = tn_dml_end();
  if(ok == 0) tn_res_set(dres, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  up->Release();
  return ok == 0 ? 0 : -1;
}

TN_EXPORT long long tn_gpu_memcpy_to_host(unsigned char* dst, unsigned char* src, long long bytes){
  if(!g_dml.device || !dst || !src || bytes <= 0) return -1;
  ID3D12Resource* sres = (ID3D12Resource*)src;

  D3D12_HEAP_PROPERTIES hp;
  memset(&hp, 0, sizeof(hp));
  hp.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC rd;
  memset(&rd, 0, sizeof(rd));
  rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  rd.Width = (UINT64)bytes;
  rd.Height = 1;
  rd.DepthOrArraySize = 1;
  rd.MipLevels = 1;
  rd.SampleDesc.Count = 1;
  rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  ID3D12Resource* rb = NULL;
  if(FAILED(g_dml.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, NULL, IID_PPV_ARGS(&rb)))){
    return -1;
  }

  if(tn_dml_begin() != 0){ rb->Release(); return -1; }
  D3D12_RESOURCE_STATES st = tn_res_get(sres);
  tn_dml_barrier(sres, st, D3D12_RESOURCE_STATE_COPY_SOURCE);
  g_dml.list->CopyBufferRegion(rb, 0, sres, 0, (UINT64)bytes);
  tn_dml_barrier(sres, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if(tn_dml_end() != 0){ rb->Release(); return -1; }
  tn_res_set(sres, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  void* map = NULL;
  if(FAILED(rb->Map(0, NULL, &map))){
    rb->Release();
    return -1;
  }
  memcpy(dst, map, (size_t)bytes);
  rb->Unmap(0, NULL);
  rb->Release();
  return 0;
}

TN_EXPORT long long tn_gpu_memset(unsigned char* dst, long long value, long long bytes){
  if(!dst || bytes <= 0) return -1;
  unsigned char* tmp = (unsigned char*)malloc((size_t)bytes);
  if(!tmp) return -1;
  memset(tmp, (int)value, (size_t)bytes);
  long long r = tn_gpu_memcpy_to_device(dst, tmp, bytes);
  free(tmp);
  return r;
}

static unsigned char* tn_read_file_bytes(const char* path, size_t* out_len){
  if(out_len) *out_len = 0;
  FILE* f = fopen(path, "rb");
  if(!f) return NULL;
  fseek(f, 0, SEEK_END);
  long long n = ftell(f);
  if(n <= 0){ fclose(f); return NULL; }
  fseek(f, 0, SEEK_SET);
  unsigned char* buf = (unsigned char*)malloc((size_t)n);
  if(!buf){ fclose(f); return NULL; }
  size_t got = fread(buf, 1, (size_t)n, f);
  fclose(f);
  if(got != (size_t)n){ free(buf); return NULL; }
  if(out_len) *out_len = (size_t)n;
  return buf;
}

TN_EXPORT long long tn_gpu_launch_dxil_1d(unsigned char* path, long long grid, long long block, unsigned char* args){
  (void)block; // thread group size is fixed by shader [numthreads]
  if(!path || !args) return -1;
  if(tn_dml_init() != 0) return -1;

  size_t blob_len = 0;
  unsigned char* blob = tn_read_file_bytes((const char*)path, &blob_len);
  if(!blob || blob_len == 0) return -1;

  D3D12_DESCRIPTOR_HEAP_DESC hd;
  memset(&hd, 0, sizeof(hd));
  hd.NumDescriptors = 1;
  hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ID3D12DescriptorHeap* heap = NULL;
  if(FAILED(g_dml.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)))){
    free(blob);
    return -1;
  }

  D3D12_ROOT_PARAMETER rp;
  memset(&rp, 0, sizeof(rp));
  D3D12_DESCRIPTOR_RANGE range;
  memset(&range, 0, sizeof(range));
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  range.NumDescriptors = 1;
  range.BaseShaderRegister = 0;
  range.RegisterSpace = 0;
  range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
  rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  rp.DescriptorTable.NumDescriptorRanges = 1;
  rp.DescriptorTable.pDescriptorRanges = &range;
  rp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC rsd;
  memset(&rsd, 0, sizeof(rsd));
  rsd.NumParameters = 1;
  rsd.pParameters = &rp;
  rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  ID3DBlob* sig = NULL;
  ID3DBlob* err = NULL;
  HRESULT hr = D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
  if(FAILED(hr)){
    if(err) err->Release();
    heap->Release();
    free(blob);
    return -1;
  }

  ID3D12RootSignature* rs = NULL;
  hr = g_dml.device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&rs));
  sig->Release();
  if(FAILED(hr)){
    if(err) err->Release();
    heap->Release();
    free(blob);
    return -1;
  }
  if(err) err->Release();

  D3D12_COMPUTE_PIPELINE_STATE_DESC pd;
  memset(&pd, 0, sizeof(pd));
  pd.pRootSignature = rs;
  pd.CS.pShaderBytecode = blob;
  pd.CS.BytecodeLength = blob_len;

  ID3D12PipelineState* pso = NULL;
  hr = g_dml.device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso));
  if(FAILED(hr)){
    rs->Release();
    heap->Release();
    free(blob);
    return -1;
  }

  ID3D12Resource* ares = (ID3D12Resource*)args;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav;
  memset(&uav, 0, sizeof(uav));
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.FirstElement = 0;
  uav.Buffer.NumElements = 0;
  uav.Buffer.StructureByteStride = 0;
  uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;

  D3D12_CPU_DESCRIPTOR_HANDLE hcpu = heap->GetCPUDescriptorHandleForHeapStart();
  g_dml.device->CreateUnorderedAccessView(ares, NULL, &uav, hcpu);

  if(tn_dml_begin() != 0){
    pso->Release();
    rs->Release();
    heap->Release();
    free(blob);
    return -1;
  }

  D3D12_RESOURCE_STATES st = tn_res_get(ares);
  tn_dml_barrier(ares, st, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  tn_res_set(ares, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  g_dml.list->SetPipelineState(pso);
  g_dml.list->SetComputeRootSignature(rs);
  ID3D12DescriptorHeap* heaps[1] = { heap };
  g_dml.list->SetDescriptorHeaps(1, heaps);
  g_dml.list->SetComputeRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart());
  g_dml.list->Dispatch((UINT)grid, 1, 1);

  int ok = tn_dml_end();

  pso->Release();
  rs->Release();
  heap->Release();
  free(blob);
  return ok == 0 ? 0 : -1;
}

TN_EXPORT long long tn_gpu_launch_1d(long long (*kernel)(unsigned char*), long long grid, long long block, unsigned char* args, long long args_size){
  (void)kernel; (void)grid; (void)block; (void)args; (void)args_size;
  return -1;
}

#else

TN_EXPORT long long tn_gpu_has_backend(void){ return 0; }
TN_EXPORT long long tn_gpu_init(void){ return -1; }
TN_EXPORT long long tn_gpu_shutdown(void){ return 0; }
TN_EXPORT long long tn_gpu_device_count(void){ return 0; }
TN_EXPORT long long tn_gpu_set_device(long long id){ (void)id; return -1; }
TN_EXPORT unsigned char* tn_gpu_alloc(long long bytes){ (void)bytes; return NULL; }
TN_EXPORT long long tn_gpu_free(unsigned char* p){ (void)p; return 0; }
TN_EXPORT unsigned char* tn_gpu_backend_name(void){ return tn_dup_cstr("none"); }
TN_EXPORT long long tn_gpu_supports_dxil(void){ return 0; }
TN_EXPORT long long tn_gpu_memcpy_to_device(unsigned char* dst, unsigned char* src, long long bytes){
  (void)dst; (void)src; (void)bytes;
  return -1;
}
TN_EXPORT long long tn_gpu_memcpy_to_host(unsigned char* dst, unsigned char* src, long long bytes){
  (void)dst; (void)src; (void)bytes;
  return -1;
}
TN_EXPORT long long tn_gpu_memset(unsigned char* dst, long long value, long long bytes){
  (void)dst; (void)value; (void)bytes;
  return -1;
}
TN_EXPORT long long tn_gpu_launch_1d(long long (*kernel)(unsigned char*), long long grid, long long block, unsigned char* args, long long args_size){
  (void)kernel; (void)grid; (void)block; (void)args; (void)args_size;
  return -1;
}

TN_EXPORT long long tn_gpu_launch_dxil_1d(unsigned char* path, long long grid, long long block, unsigned char* args){
  (void)path; (void)grid; (void)block; (void)args;
  return -1;
}

#endif

#if defined(TN_NPU_ONNXRUNTIME)

typedef struct tn_npu_model {
  OrtSession* sess;
} tn_npu_model;

static const OrtApi* g_ort_api = NULL;
static OrtEnv* g_ort_env = NULL;

static int tn_ort_init(void){
  if(g_ort_env) return 0;
  g_ort_api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
  if(!g_ort_api) return -1;
  OrtStatus* st = g_ort_api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "tezznpu", &g_ort_env);
  if(st){ g_ort_api->ReleaseStatus(st); g_ort_env = NULL; return -1; }
  return 0;
}

static void tn_ort_shutdown(void){
  if(g_ort_api && g_ort_env){
    g_ort_api->ReleaseEnv(g_ort_env);
    g_ort_env = NULL;
  }
}

static int tn_npu_use_openvino(void){
  const char* ep = getenv("TN_NPU_EP");
  if(!ep) return 0;
  return (strcmp(ep, "openvino")==0 || strcmp(ep, "OPENVINO")==0);
}

TN_EXPORT long long tn_npu_has_backend(void){
  return tn_ort_init() == 0 ? 1 : 0;
}
TN_EXPORT long long tn_npu_init(void){
  return tn_ort_init() == 0 ? 0 : -1;
}
TN_EXPORT long long tn_npu_shutdown(void){
  tn_ort_shutdown();
  return 0;
}
TN_EXPORT long long tn_npu_device_count(void){
  return tn_ort_init() == 0 ? 1 : 0;
}
TN_EXPORT long long tn_npu_set_device(long long id){
  (void)id;
  return tn_ort_init() == 0 ? 0 : -1;
}
TN_EXPORT unsigned char* tn_npu_alloc(long long bytes){
  if(bytes <= 0) return NULL;
  return (unsigned char*)malloc((size_t)bytes);
}
TN_EXPORT long long tn_npu_free(unsigned char* p){
  if(p) free(p);
  return 0;
}
TN_EXPORT long long tn_npu_memcpy_to_device(unsigned char* dst, unsigned char* src, long long bytes){
  if(!dst || !src || bytes <= 0) return -1;
  memcpy(dst, src, (size_t)bytes);
  return 0;
}
TN_EXPORT long long tn_npu_memcpy_to_host(unsigned char* dst, unsigned char* src, long long bytes){
  if(!dst || !src || bytes <= 0) return -1;
  memcpy(dst, src, (size_t)bytes);
  return 0;
}
TN_EXPORT long long tn_npu_memset(unsigned char* dst, long long value, long long bytes){
  if(!dst || bytes <= 0) return -1;
  memset(dst, (int)value, (size_t)bytes);
  return 0;
}
TN_EXPORT long long tn_npu_launch_1d(long long (*kernel)(unsigned char*), long long grid, long long block, unsigned char* args, long long args_size){
  (void)kernel; (void)grid; (void)block; (void)args; (void)args_size;
  return -2;
}
TN_EXPORT long long tn_npu_launch_dxil_1d(unsigned char* path, long long grid, long long block, unsigned char* args){
  (void)path; (void)grid; (void)block; (void)args;
  return -1;
}
TN_EXPORT unsigned char* tn_npu_backend_name(void){
  if(tn_npu_use_openvino()) return tn_dup_cstr("onnxruntime-openvino");
  return tn_dup_cstr("onnxruntime");
}
TN_EXPORT long long tn_npu_supports_dxil(void){ return 0; }

TN_EXPORT long long tn_npu_model_load(unsigned char* path){
  if(!path || tn_ort_init() != 0) return -1;
  OrtSessionOptions* so = NULL;
  OrtStatus* st = g_ort_api->CreateSessionOptions(&so);
  if(st){ g_ort_api->ReleaseStatus(st); return -1; }
  g_ort_api->SetSessionGraphOptimizationLevel(so, ORT_ENABLE_EXTENDED);
#ifdef USE_OPENVINO
  if(tn_npu_use_openvino()){
    OrtOpenVINOProviderOptions opt;
    memset(&opt, 0, sizeof(opt));
    opt.device_type = "CPU_FP32";
    OrtStatus* st2 = OrtSessionOptionsAppendExecutionProvider_OpenVINO(so, &opt);
    if(st2){ g_ort_api->ReleaseStatus(st2); }
  }
#endif
  OrtSession* sess = NULL;
  st = g_ort_api->CreateSession(g_ort_env, (const char*)path, so, &sess);
  g_ort_api->ReleaseSessionOptions(so);
  if(st){ g_ort_api->ReleaseStatus(st); return -1; }
  tn_npu_model* m = (tn_npu_model*)malloc(sizeof(tn_npu_model));
  if(!m){ g_ort_api->ReleaseSession(sess); return -1; }
  m->sess = sess;
  return (long long)(intptr_t)m;
}

TN_EXPORT long long tn_npu_model_free(long long h){
  tn_npu_model* m = (tn_npu_model*)(intptr_t)h;
  if(!m) return -1;
  if(g_ort_api && m->sess) g_ort_api->ReleaseSession(m->sess);
  free(m);
  return 0;
}

TN_EXPORT long long tn_npu_model_run_f64_1d(long long h, double* input, long long input_len, double* output, long long output_len){
  tn_npu_model* m = (tn_npu_model*)(intptr_t)h;
  if(!m || !m->sess || !input || !output || input_len <= 0 || output_len <= 0) return -1;
  if(tn_ort_init() != 0) return -1;
  OrtAllocator* alloc = NULL;
  OrtStatus* st = g_ort_api->GetAllocatorWithDefaultOptions(&alloc);
  if(st){
    g_ort_api->ReleaseStatus(st);
    return -1;
  }

  char* in_name = NULL;
  char* out_name = NULL;
  st = g_ort_api->SessionGetInputName(m->sess, 0, alloc, &in_name);
  if(st){
    g_ort_api->ReleaseStatus(st);
    return -1;
  }
  st = g_ort_api->SessionGetOutputName(m->sess, 0, alloc, &out_name);
  if(st){
    g_ort_api->ReleaseStatus(st);
    alloc->Free(alloc, in_name);
    return -1;
  }

  size_t in_bytes = 0;
  size_t out_bytes = 0;
  if(tn_mul_overflow_size(input_len, (long long)sizeof(double), &in_bytes)) return -1;
  if(tn_mul_overflow_size(output_len, (long long)sizeof(double), &out_bytes)) return -1;

  int64_t in_shape[1]; in_shape[0] = (int64_t)input_len;
  int64_t out_shape[1]; out_shape[0] = (int64_t)output_len;

  OrtMemoryInfo* mem = NULL;
  st = g_ort_api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mem);
  if(st){ g_ort_api->ReleaseStatus(st); alloc->Free(alloc, in_name); alloc->Free(alloc, out_name); return -1; }

  OrtValue* in_val = NULL;
  OrtValue* out_val = NULL;
  st = g_ort_api->CreateTensorWithDataAsOrtValue(mem, input, in_bytes, in_shape, 1, ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE, &in_val);
  if(st){
    g_ort_api->ReleaseStatus(st);
    g_ort_api->ReleaseMemoryInfo(mem);
    alloc->Free(alloc, in_name);
    alloc->Free(alloc, out_name);
    return -1;
  }
  st = g_ort_api->CreateTensorWithDataAsOrtValue(mem, output, out_bytes, out_shape, 1, ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE, &out_val);
  if(st){
    g_ort_api->ReleaseStatus(st);
    g_ort_api->ReleaseValue(in_val);
    g_ort_api->ReleaseMemoryInfo(mem);
    alloc->Free(alloc, in_name);
    alloc->Free(alloc, out_name);
    return -1;
  }

  const char* in_names[1] = { in_name };
  const char* out_names[1] = { out_name };
  OrtValue* out_vals[1] = { out_val };
  const OrtValue* in_vals[1] = { in_val };

  st = g_ort_api->Run(m->sess, NULL, in_names, in_vals, 1, out_names, 1, out_vals);
  if(st){ g_ort_api->ReleaseStatus(st); }

  g_ort_api->ReleaseValue(in_val);
  g_ort_api->ReleaseValue(out_val);
  g_ort_api->ReleaseMemoryInfo(mem);
  alloc->Free(alloc, in_name);
  alloc->Free(alloc, out_name);
  return st ? -1 : 0;
}

#elif defined(_WIN32) && defined(TN_GPU_DIRECTML)

TN_EXPORT long long tn_npu_has_backend(void){ return tn_gpu_has_backend(); }
TN_EXPORT long long tn_npu_init(void){ return tn_gpu_init(); }
TN_EXPORT long long tn_npu_shutdown(void){ return tn_gpu_shutdown(); }
TN_EXPORT long long tn_npu_device_count(void){ return tn_gpu_device_count(); }
TN_EXPORT long long tn_npu_set_device(long long id){ return tn_gpu_set_device(id); }
TN_EXPORT unsigned char* tn_npu_alloc(long long bytes){ return tn_gpu_alloc(bytes); }
TN_EXPORT long long tn_npu_free(unsigned char* p){ return tn_gpu_free(p); }
TN_EXPORT long long tn_npu_memcpy_to_device(unsigned char* dst, unsigned char* src, long long bytes){
  return tn_gpu_memcpy_to_device(dst, src, bytes);
}
TN_EXPORT long long tn_npu_memcpy_to_host(unsigned char* dst, unsigned char* src, long long bytes){
  return tn_gpu_memcpy_to_host(dst, src, bytes);
}
TN_EXPORT long long tn_npu_memset(unsigned char* dst, long long value, long long bytes){
  return tn_gpu_memset(dst, value, bytes);
}
TN_EXPORT long long tn_npu_launch_1d(long long (*kernel)(unsigned char*), long long grid, long long block, unsigned char* args, long long args_size){
  (void)kernel; (void)grid; (void)block; (void)args; (void)args_size;
  return -2;
}
TN_EXPORT long long tn_npu_launch_dxil_1d(unsigned char* path, long long grid, long long block, unsigned char* args){
  return tn_gpu_launch_dxil_1d(path, grid, block, args);
}
TN_EXPORT unsigned char* tn_npu_backend_name(void){
  return tn_dup_cstr("directml");
}
TN_EXPORT long long tn_npu_supports_dxil(void){
  return 1;
}
TN_EXPORT long long tn_npu_model_load(unsigned char* path){
  (void)path;
  return -1;
}
TN_EXPORT long long tn_npu_model_free(long long h){
  (void)h;
  return -1;
}
TN_EXPORT long long tn_npu_model_run_f64_1d(long long h, double* input, long long input_len, double* output, long long output_len){
  (void)h; (void)input; (void)input_len; (void)output; (void)output_len;
  return -1;
}

#else

TN_EXPORT long long tn_npu_has_backend(void){ return 0; }
TN_EXPORT long long tn_npu_init(void){ return -1; }
TN_EXPORT long long tn_npu_shutdown(void){ return 0; }
TN_EXPORT long long tn_npu_device_count(void){ return 0; }
TN_EXPORT long long tn_npu_set_device(long long id){ (void)id; return -1; }
TN_EXPORT unsigned char* tn_npu_alloc(long long bytes){ (void)bytes; return NULL; }
TN_EXPORT long long tn_npu_free(unsigned char* p){ (void)p; return 0; }
TN_EXPORT long long tn_npu_memcpy_to_device(unsigned char* dst, unsigned char* src, long long bytes){
  (void)dst; (void)src; (void)bytes;
  return -1;
}
TN_EXPORT long long tn_npu_memcpy_to_host(unsigned char* dst, unsigned char* src, long long bytes){
  (void)dst; (void)src; (void)bytes;
  return -1;
}
TN_EXPORT long long tn_npu_memset(unsigned char* dst, long long value, long long bytes){
  (void)dst; (void)value; (void)bytes;
  return -1;
}
TN_EXPORT long long tn_npu_launch_1d(long long (*kernel)(unsigned char*), long long grid, long long block, unsigned char* args, long long args_size){
  (void)kernel; (void)grid; (void)block; (void)args; (void)args_size;
  return -1;
}
TN_EXPORT long long tn_npu_launch_dxil_1d(unsigned char* path, long long grid, long long block, unsigned char* args){
  (void)path; (void)grid; (void)block; (void)args;
  return -1;
}
TN_EXPORT unsigned char* tn_npu_backend_name(void){ return tn_dup_cstr("none"); }
TN_EXPORT long long tn_npu_supports_dxil(void){ return 0; }
TN_EXPORT long long tn_npu_model_load(unsigned char* path){
  (void)path;
  return -1;
}
TN_EXPORT long long tn_npu_model_free(long long h){
  (void)h;
  return -1;
}
TN_EXPORT long long tn_npu_model_run_f64_1d(long long h, double* input, long long input_len, double* output, long long output_len){
  (void)h; (void)input; (void)input_len; (void)output; (void)output_len;
  return -1;
}
#endif

TN_EXPORT long long tn_net_af_inet(void){ return (long long)AF_INET; }
TN_EXPORT long long tn_net_af_inet6(void){ return (long long)AF_INET6; }
TN_EXPORT long long tn_net_sock_stream(void){ return (long long)SOCK_STREAM; }
TN_EXPORT long long tn_net_sock_dgram(void){ return (long long)SOCK_DGRAM; }
TN_EXPORT long long tn_net_ipproto_tcp(void){ return (long long)IPPROTO_TCP; }
TN_EXPORT long long tn_net_ipproto_udp(void){ return (long long)IPPROTO_UDP; }

TN_EXPORT long long tn_net_init(void){
  return (long long)tn_net_init_impl();
}

TN_EXPORT long long tn_net_cleanup(void){
  return (long long)tn_net_cleanup_impl();
}

TN_EXPORT long long tn_net_socket(long long fam, long long type, long long proto){
  if(tn_net_init_impl() != 0) return -1;
  tn_sock_t s = (tn_sock_t)socket((int)fam, (int)type, (int)proto);
  return tn_sock_to_ll(s);
}

TN_EXPORT long long tn_net_close(long long sock){
  tn_sock_t s = tn_sock_from_ll(sock);
#ifdef _WIN32
  return (long long)((s==TN_INVALID_SOCKET) ? -1 : closesocket(s));
#else
  return (long long)((s==TN_INVALID_SOCKET) ? -1 : close(s));
#endif
}

TN_EXPORT long long tn_net_connect(long long sock, unsigned char* host, long long port){
  return (long long)tn_net_connect_impl(tn_sock_from_ll(sock), (const char*)host, port);
}

TN_EXPORT long long tn_net_bind(long long sock, unsigned char* host, long long port){
  return (long long)tn_net_bind_impl(tn_sock_from_ll(sock), (const char*)host, port);
}

TN_EXPORT long long tn_net_listen(long long sock, long long backlog){
  tn_sock_t s = tn_sock_from_ll(sock);
  return (long long)listen(s, (int)backlog);
}

TN_EXPORT long long tn_net_accept(long long sock){
  tn_sock_t s = tn_sock_from_ll(sock);
  tn_sock_t c = (tn_sock_t)accept(s, NULL, NULL);
  return (c==TN_INVALID_SOCKET) ? -1 : tn_sock_to_ll(c);
}

TN_EXPORT long long tn_net_send(long long sock, unsigned char* buf, long long len){
  tn_sock_t s = tn_sock_from_ll(sock);
  int r = (buf && len>0) ? (int)send(s, (const char*)buf, (int)len, TN_SEND_FLAGS) : 0;
  if(r < 0) r = -1;
  return (long long)r;
}

TN_EXPORT long long tn_net_recv(long long sock, unsigned char* buf, long long len){
  tn_sock_t s = tn_sock_from_ll(sock);
  int r = (buf && len>0) ? (int)recv(s, (char*)buf, (int)len, 0) : 0;
  if(r < 0) r = -1;
  return (long long)r;
}

TN_EXPORT long long tn_net_set_blocking(long long sock, long long on){
  return (long long)tn_net_set_blocking_impl(tn_sock_from_ll(sock), (int)on);
}

TN_EXPORT long long tn_net_set_timeout(long long sock, long long ms){
  return (long long)tn_net_set_timeout_impl(tn_sock_from_ll(sock), ms);
}

TN_EXPORT long long tn_net_last_error(void){
  return tn_net_last_error_impl();
}

// ------------------------------
// Process helpers
// ------------------------------
TN_EXPORT long long proc_run(unsigned char* cmd){
  const char* c = (const char*)cmd;
  if(!c) c = "";
  return (long long)system(c);
}

TN_EXPORT unsigned char* proc_out(unsigned char* cmd){
  const char* c = (const char*)cmd;
  if(!c) c = "";
#ifdef _WIN32
  FILE* p = _popen(c, "rb");
#else
  FILE* p = popen(c, "rb");
#endif
  if(!p) return tn_dup_cstr("");
  size_t cap = 256;
  size_t len = 0;
  char* buf = (char*)malloc(cap);
  if(!buf){
#ifdef _WIN32
    _pclose(p);
#else
    pclose(p);
#endif
    return tn_dup_cstr("");
  }
  for(;;){
    if(len + 128 >= cap){
      cap *= 2;
      char* nb = (char*)realloc(buf, cap);
      if(!nb){ break; }
      buf = nb;
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
    char* nb = (char*)realloc(buf, len + 1);
    if(nb) buf = nb;
  }
  buf[len] = 0;
  return (unsigned char*)buf;
}

// ------------------------------
// Args (Windows-only for now)
// ------------------------------
TN_EXPORT long long args_count(void){
#ifdef _WIN32
  return (long long)__argc;
#else
  return 0;
#endif
}

TN_EXPORT unsigned char* arg(long long idx){
#ifdef _WIN32
  if(idx < 0 || idx >= __argc) return NULL;
  return (unsigned char*)__argv[idx];
#else
  (void)idx;
  return NULL;
#endif
}

TN_EXPORT unsigned char* tn_net_resolve(unsigned char* host, long long port){
  return tn_net_resolve_impl((const char*)host, port);
}

TN_EXPORT long long tn_tls_connect(unsigned char* host, long long port){
#ifdef _WIN32
  tn_tls* t = tn_tls_connect_impl((const char*)host, port);
  return t ? (long long)(intptr_t)t : -1;
#else
  #if defined(TN_TLS_OPENSSL)
    tn_tls_ossl* t = tn_tls_connect_ossl((const char*)host, port);
    return t ? (long long)(intptr_t)t : -1;
  #else
    (void)host; (void)port;
    return -2;
  #endif
#endif
}

TN_EXPORT long long tn_tls_connect_ex(unsigned char* host,
                                      long long port,
                                      long long min_version,
                                      long long handshake_timeout_ms,
                                      unsigned char* pin_hex){
  tn_tls_policy_cfg req_policy;
  if(tn_tls_policy_build_request(min_version, handshake_timeout_ms, (const char*)pin_hex, &req_policy) != 0){
    return -1;
  }
#ifdef _WIN32
  tn_tls* t = tn_tls_connect_impl_policy((const char*)host, port, &req_policy);
  return t ? (long long)(intptr_t)t : -1;
#else
  #if defined(TN_TLS_OPENSSL)
    tn_tls_ossl* t = tn_tls_connect_ossl_policy((const char*)host, port, &req_policy);
    return t ? (long long)(intptr_t)t : -1;
  #else
    (void)host; (void)port;
    return -2;
  #endif
#endif
}

TN_EXPORT long long tn_tls_send(long long handle, unsigned char* buf, long long len){
#ifdef _WIN32
  tn_tls* t = (tn_tls*)(intptr_t)handle;
  return tn_tls_send_impl(t, buf, len);
#else
  #if defined(TN_TLS_OPENSSL)
    tn_tls_ossl* t = (tn_tls_ossl*)(intptr_t)handle;
    return tn_tls_send_ossl(t, buf, len);
  #else
    (void)handle; (void)buf; (void)len;
    return -2;
  #endif
#endif
}

TN_EXPORT long long tn_tls_recv(long long handle, unsigned char* buf, long long len){
#ifdef _WIN32
  tn_tls* t = (tn_tls*)(intptr_t)handle;
  return tn_tls_recv_impl(t, buf, len);
#else
  #if defined(TN_TLS_OPENSSL)
    tn_tls_ossl* t = (tn_tls_ossl*)(intptr_t)handle;
    return tn_tls_recv_ossl(t, buf, len);
  #else
    (void)handle; (void)buf; (void)len;
    return -2;
  #endif
#endif
}

TN_EXPORT long long tn_tls_close(long long handle){
#ifdef _WIN32
  tn_tls* t = (tn_tls*)(intptr_t)handle;
  tn_tls_free(t);
  return 0;
#else
  #if defined(TN_TLS_OPENSSL)
    tn_tls_ossl* t = (tn_tls_ossl*)(intptr_t)handle;
    tn_tls_free_ossl(t);
    return 0;
  #else
    (void)handle;
    return -2;
  #endif
#endif
}

TN_EXPORT long long tn_tls_policy_reset(void){
  return (long long)tn_tls_policy_reset_impl();
}

TN_EXPORT long long tn_tls_policy_set_min(long long min_version){
  return (long long)tn_tls_policy_set_min_impl(min_version);
}

TN_EXPORT long long tn_tls_policy_set_pin_sha256(unsigned char* pin_hex){
  return (long long)tn_tls_policy_set_pin_sha256_impl((const char*)pin_hex);
}

TN_EXPORT long long tn_tls_policy_set_handshake_timeout(long long timeout_ms){
  return (long long)tn_tls_policy_set_handshake_timeout_impl(timeout_ms);
}

TN_EXPORT long long tn_tls_policy_get_min(void){
  return tn_tls_policy_get_min_impl();
}

TN_EXPORT long long tn_tls_policy_get_handshake_timeout(void){
  return tn_tls_policy_get_handshake_timeout_impl();
}

TN_EXPORT long long tn_tls_policy_pin_enabled(void){
  return tn_tls_policy_pin_enabled_impl();
}

TN_EXPORT unsigned char* tn_os_name(void){
#ifdef _WIN32
  return tn_dup_cstr("windows");
#else
  #ifdef __APPLE__
    return tn_dup_cstr("macos");
  #else
    return tn_dup_cstr("linux");
  #endif
#endif
}

TN_EXPORT unsigned char* tn_getenv_str(const unsigned char* name){
  const char* val = getenv((const char*)name);
  if(!val) return NULL;
  return tn_dup_cstr(val);
}

TN_EXPORT long long tn_color_is_tty_stdout(void){
#ifdef _WIN32
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if(h == INVALID_HANDLE_VALUE) return 0;
  DWORD mode = 0;
  if(!GetConsoleMode(h, &mode)) return 0;
  return 1;
#else
  return isatty(fileno(stdout)) ? 1 : 0;
#endif
}

#ifdef _WIN32
typedef BOOL (WINAPI *PlaySoundA_t)(LPCSTR, HMODULE, DWORD);
typedef MCIERROR (WINAPI *mciSendStringA_t)(LPCSTR, LPSTR, UINT, HANDLE);
#endif

TN_EXPORT long long tn_tts_play_pcm(long long* pcm, long long len, long long rate) {
#ifdef _WIN32
  HMODULE winmm = LoadLibraryA("winmm.dll");
  if (!winmm) return -1;
  PlaySoundA_t play_sound = (PlaySoundA_t)(void*)GetProcAddress(winmm, "PlaySoundA");
  if (!play_sound) {
    FreeLibrary(winmm);
    return -1;
  }

  char temp_path[MAX_PATH];
  GetTempPathA(MAX_PATH, temp_path);
  strcat(temp_path, "tezz_tts_temp.wav");

  FILE* f = fopen(temp_path, "wb");
  if (!f) {
    FreeLibrary(winmm);
    return -1;
  }

  long long data_bytes = len * 2;
  long long total_size = data_bytes + 36;
  unsigned char hdr[44] = {0};
  hdr[0] = 'R'; hdr[1] = 'I'; hdr[2] = 'F'; hdr[3] = 'F';
  hdr[4] = (unsigned char)(total_size & 0xFF);
  hdr[5] = (unsigned char)((total_size >> 8) & 0xFF);
  hdr[6] = (unsigned char)((total_size >> 16) & 0xFF);
  hdr[7] = (unsigned char)((total_size >> 24) & 0xFF);
  hdr[8] = 'W'; hdr[9] = 'A'; hdr[10] = 'V'; hdr[11] = 'E';
  hdr[12] = 'f'; hdr[13] = 'm'; hdr[14] = 't'; hdr[15] = ' ';
  hdr[16] = 16;
  hdr[20] = 1;
  hdr[22] = 1;
  hdr[24] = (unsigned char)(rate & 0xFF);
  hdr[25] = (unsigned char)((rate >> 8) & 0xFF);
  hdr[26] = (unsigned char)((rate >> 16) & 0xFF);
  hdr[27] = (unsigned char)((rate >> 24) & 0xFF);
  long long byte_rate = rate * 2;
  hdr[28] = (unsigned char)(byte_rate & 0xFF);
  hdr[29] = (unsigned char)((byte_rate >> 8) & 0xFF);
  hdr[30] = (unsigned char)((byte_rate >> 16) & 0xFF);
  hdr[31] = (unsigned char)((byte_rate >> 24) & 0xFF);
  hdr[32] = 2;
  hdr[34] = 16;
  hdr[36] = 'd'; hdr[37] = 'a'; hdr[38] = 't'; hdr[39] = 'a';
  hdr[40] = (unsigned char)(data_bytes & 0xFF);
  hdr[41] = (unsigned char)((data_bytes >> 8) & 0xFF);
  hdr[42] = (unsigned char)((data_bytes >> 16) & 0xFF);
  hdr[43] = (unsigned char)((data_bytes >> 24) & 0xFF);

  fwrite(hdr, 1, 44, f);
  for (long long i = 0; i < len; i++) {
    long long v = pcm[i];
    if (v > 32767) v = 32767;
    if (v < -32767) v = -32767;
    short s = (short)v;
    fwrite(&s, 2, 1, f);
  }
  fclose(f);

  play_sound(temp_path, NULL, 0x0002 | 0x0000); // SND_FILENAME | SND_SYNC

  DeleteFileA(temp_path);
  FreeLibrary(winmm);
  return 0;
#else
  printf("[TTS speak] Playback of %lld samples at %lld Hz is simulated on this OS.\n", len, rate);
  return 0;
#endif
}

TN_EXPORT long long tn_stt_capture_mic(long long* pcm, long long n_samples, long long rate) {
#ifdef _WIN32
  HMODULE winmm = LoadLibraryA("winmm.dll");
  if (!winmm) return -1;
  mciSendStringA_t mci_send_string = (mciSendStringA_t)(void*)GetProcAddress(winmm, "mciSendStringA");
  if (!mci_send_string) {
    FreeLibrary(winmm);
    return -1;
  }

  mci_send_string("open new type waveaudio alias recsound", NULL, 0, NULL);
  char set_cmd[128];
  sprintf(set_cmd, "set recsound bitspersample 16 samplespersec %lld alignment 2 bytespersec %lld", rate, rate * 2);
  mci_send_string(set_cmd, NULL, 0, NULL);

  mci_send_string("record recsound", NULL, 0, NULL);
  long long duration_ms = (n_samples * 1000) / rate;
  Sleep((DWORD)duration_ms);

  mci_send_string("stop recsound", NULL, 0, NULL);
  char temp_path[MAX_PATH];
  GetTempPathA(MAX_PATH, temp_path);
  strcat(temp_path, "tezz_stt_temp.wav");
  DeleteFileA(temp_path);

  char save_cmd[MAX_PATH + 32];
  sprintf(save_cmd, "save recsound \"%s\"", temp_path);
  mci_send_string(save_cmd, NULL, 0, NULL);
  mci_send_string("close recsound", NULL, 0, NULL);

  FILE* f = fopen(temp_path, "rb");
  if (!f) {
    FreeLibrary(winmm);
    return -1;
  }

  fseek(f, 44, SEEK_SET);
  long long count = 0;
  short s = 0;
  while (count < n_samples && fread(&s, 2, 1, f) == 1) {
    pcm[count] = (long long)s;
    count++;
  }
  fclose(f);
  DeleteFileA(temp_path);
  FreeLibrary(winmm);
  return count;
#else
  printf("[STT mic] Simulated microphone recording on this OS.\n");
  for (long long i = 0; i < n_samples; i++) {
    pcm[i] = 0;
  }
  return n_samples;
#endif
}

