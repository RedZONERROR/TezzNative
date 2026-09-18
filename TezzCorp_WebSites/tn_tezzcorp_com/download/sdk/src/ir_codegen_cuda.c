// src/ir_codegen_cuda.c
#include "ir.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

enum { TN_CODEGEN_NAME_MAX = 4096, TN_CODEGEN_CALL_ARG_MAX = 1024 };

static const char* sanitize_sym_name(const char* name, int* len){
  if(!len){
    return name ? name : "";
  }
  if(*len < 0){
    *len = 0;
  } else if(*len > TN_CODEGEN_NAME_MAX){
    *len = TN_CODEGEN_NAME_MAX;
  }
  if(!name){
    *len = 0;
    return "";
  }
  return name;
}

static int safe_call_arg(const IRIns* in, int idx){
  if(!in || idx < 0 || idx >= in->argc || !in->args){
    return 0;
  }
  return in->args[idx];
}

static void mangle_sym(const char* in, int n, char* out, int cap){
  if(!out || cap <= 0){
    return;
  }
  if(!in || n <= 0){
    out[0] = 0;
    return;
  }
  if(n > TN_CODEGEN_NAME_MAX){
    n = TN_CODEGEN_NAME_MAX;
  }
  int w = 0;
  for(int i=0;i<n && w<cap-1;i++){
    char c = in[i];
    if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'){
      out[w++] = c;
    } else if(c=='.'){
      if(w < cap-2){
        out[w++] = '_';
        out[w++] = '_';
      }
    } else {
      out[w++] = '_';
    }
  }
  out[w] = 0;
}

static const char* map_builtin_name(const char* name, int len, int* out_len){
  if(!out_len){
    return name ? name : "";
  }
  name = sanitize_sym_name(name, &len);
  if(len==6 && strncmp(name,"strcmp",6)==0){ *out_len=9; return "tn_strcmp"; }
  if(len==10 && strncmp(name,"bit_popcnt",10)==0){ *out_len=13; return "tn_bit_popcnt"; }
  if(len==7 && strncmp(name,"bit_clz",7)==0){ *out_len=10; return "tn_bit_clz"; }
  if(len==7 && strncmp(name,"bit_ctz",7)==0){ *out_len=10; return "tn_bit_ctz"; }
  if(len==9 && strncmp(name,"bit_bswap",9)==0){ *out_len=12; return "tn_bit_bswap"; }
  if(len==8 && strncmp(name,"bit_rotl",8)==0){ *out_len=11; return "tn_bit_rotl"; }
  if(len==8 && strncmp(name,"bit_rotr",8)==0){ *out_len=11; return "tn_bit_rotr"; }
  if(len==12 && strncmp(name,"cpu_has_sse2",12)==0){ *out_len=15; return "tn_cpu_has_sse2"; }
  if(len==12 && strncmp(name,"cpu_has_avx2",12)==0){ *out_len=15; return "tn_cpu_has_avx2"; }
  if(len==11 && strncmp(name,"cpu_has_fma",11)==0){ *out_len=14; return "tn_cpu_has_fma"; }
  if(len==12 && strncmp(name,"cpu_has_neon",12)==0){ *out_len=15; return "tn_cpu_has_neon"; }
  if(len==5 && strncmp(name,"fopen",5)==0){ *out_len=8; return "tn_fopen"; }
  if(len==6 && strncmp(name,"fclose",6)==0){ *out_len=9; return "tn_fclose"; }
  if(len==6 && strncmp(name,"fflush",6)==0){ *out_len=9; return "tn_fflush"; }
  if(len==5 && strncmp(name,"fread",5)==0){ *out_len=8; return "tn_fread"; }
  if(len==6 && strncmp(name,"fwrite",6)==0){ *out_len=9; return "tn_fwrite"; }
  if(len==5 && strncmp(name,"fseek",5)==0){ *out_len=8; return "tn_fseek"; }
  if(len==5 && strncmp(name,"ftell",5)==0){ *out_len=8; return "tn_ftell"; }
  if(len==9 && strncmp(name,"file_size",9)==0){ *out_len=12; return "tn_file_size"; }
  if(len==9 && strncmp(name,"read_file",9)==0){ *out_len=12; return "tn_read_file"; }
  if(len==10 && strncmp(name,"write_file",10)==0){ *out_len=13; return "tn_write_file"; }
  if(len==9 && strncmp(name,"read_line",9)==0){ *out_len=12; return "tn_read_line"; }
  if(len==10 && strncmp(name,"read_bytes",10)==0){ *out_len=13; return "tn_read_bytes"; }
  if(len==10 && strncmp(name,"write_line",10)==0){ *out_len=13; return "tn_write_line"; }
  if(len==6 && strncmp(name,"io_err",6)==0){ *out_len=9; return "tn_io_err"; }
  if(len==6 && strncmp(name,"io_eof",6)==0){ *out_len=9; return "tn_io_eof"; }
  if(len==5 && strncmp(name,"stdin",5)==0){ *out_len=8; return "tn_stdin"; }
  if(len==6 && strncmp(name,"stdout",6)==0){ *out_len=9; return "tn_stdout"; }
  if(len==6 && strncmp(name,"stderr",6)==0){ *out_len=9; return "tn_stderr"; }
  if(len==7 && strncmp(name,"get_cwd",7)==0){ *out_len=10; return "tn_get_cwd"; }
  if(len==5 && strncmp(name,"chdir",5)==0){ *out_len=8; return "tn_chdir"; }
  if(len==5 && strncmp(name,"mkdir",5)==0){ *out_len=8; return "tn_mkdir"; }
  if(len==5 && strncmp(name,"rmdir",5)==0){ *out_len=8; return "tn_rmdir"; }
  if(len==6 && strncmp(name,"remove",6)==0){ *out_len=9; return "tn_remove"; }
  if(len==6 && strncmp(name,"rename",6)==0){ *out_len=9; return "tn_rename"; }
  if(len==11 && strncmp(name,"path_exists",11)==0){ *out_len=14; return "tn_path_exists"; }
  if(len==11 && strncmp(name,"path_is_dir",11)==0){ *out_len=14; return "tn_path_is_dir"; }
  if(len==9 && strncmp(name,"path_join",9)==0){ *out_len=12; return "tn_path_join"; }
  if(len==8 && strncmp(name,"path_sep",8)==0){ *out_len=11; return "tn_path_sep"; }
  if(len==13 && strncmp(name,"path_basename",13)==0){ *out_len=16; return "tn_path_basename"; }
  if(len==12 && strncmp(name,"path_dirname",12)==0){ *out_len=15; return "tn_path_dirname"; }
  if(len==8 && strncmp(name,"list_dir",8)==0){ *out_len=11; return "tn_list_dir"; }
  if(len==18 && strncmp(name,"list_dir_recursive",18)==0){ *out_len=21; return "tn_list_dir_recursive"; }
  if(len==4 && strncmp(name,"glob",4)==0){ *out_len=7; return "tn_glob"; }
  if(len==14 && strncmp(name,"path_normalize",14)==0){ *out_len=17; return "tn_path_normalize"; }
  if(len==19 && strncmp(name,"path_normalize_opts",19)==0){ *out_len=22; return "tn_path_normalize_opts"; }
  if(len==8 && strncmp(name,"os_name",8)==0){ *out_len=11; return "tn_os_name"; }
  if(len==8 && strncmp(name,"time_now",8)==0){ *out_len=11; return "tn_time_now"; }
  if(len==11 && strncmp(name,"time_now_ms",11)==0){ *out_len=14; return "tn_time_now_ms"; }
  if(len==11 && strncmp(name,"time_now_ns",11)==0){ *out_len=14; return "tn_time_now_ns"; }
  if(len==8 && strncmp(name,"date_now",8)==0){ *out_len=11; return "tn_date_now"; }
  if(len==12 && strncmp(name,"date_now_utc",12)==0){ *out_len=15; return "tn_date_now_utc"; }
  if(len==8 && strncmp(name,"sleep_ms",8)==0){ *out_len=11; return "tn_sleep_ms"; }
  if(len==5 && strncmp(name,"async",5)==0){ *out_len=14; return "tn_async_spawn"; }
  if(len==5 && strncmp(name,"await",5)==0){ *out_len=13; return "tn_async_await"; }
  if(len==13 && strncmp(name,"async_workers",13)==0){ *out_len=16; return "tn_async_workers"; }
  if(len==13 && strncmp(name,"async_pending",13)==0){ *out_len=16; return "tn_async_pending"; }
  if(len==11 && strncmp(name,"net_af_inet",11)==0){ *out_len=14; return "tn_net_af_inet"; }
  if(len==12 && strncmp(name,"net_af_inet6",12)==0){ *out_len=15; return "tn_net_af_inet6"; }
  if(len==14 && strncmp(name,"net_sock_stream",14)==0){ *out_len=17; return "tn_net_sock_stream"; }
  if(len==13 && strncmp(name,"net_sock_dgram",13)==0){ *out_len=16; return "tn_net_sock_dgram"; }
  if(len==15 && strncmp(name,"net_ipproto_tcp",15)==0){ *out_len=18; return "tn_net_ipproto_tcp"; }
  if(len==15 && strncmp(name,"net_ipproto_udp",15)==0){ *out_len=18; return "tn_net_ipproto_udp"; }
  if(len==8 && strncmp(name,"net_init",8)==0){ *out_len=11; return "tn_net_init"; }
  if(len==11 && strncmp(name,"net_cleanup",11)==0){ *out_len=14; return "tn_net_cleanup"; }
  if(len==14 && strncmp(name,"net_last_error",14)==0){ *out_len=17; return "tn_net_last_error"; }
  if(len==10 && strncmp(name,"net_socket",10)==0){ *out_len=13; return "tn_net_socket"; }
  if(len==9 && strncmp(name,"net_close",9)==0){ *out_len=12; return "tn_net_close"; }
  if(len==11 && strncmp(name,"net_connect",11)==0){ *out_len=14; return "tn_net_connect"; }
  if(len==8 && strncmp(name,"net_bind",8)==0){ *out_len=11; return "tn_net_bind"; }
  if(len==9 && strncmp(name,"net_listen",9)==0){ *out_len=12; return "tn_net_listen"; }
  if(len==10 && strncmp(name,"net_accept",10)==0){ *out_len=13; return "tn_net_accept"; }
  if(len==8 && strncmp(name,"net_send",8)==0){ *out_len=11; return "tn_net_send"; }
  if(len==8 && strncmp(name,"net_recv",8)==0){ *out_len=11; return "tn_net_recv"; }
  if(len==16 && strncmp(name,"net_set_blocking",16)==0){ *out_len=19; return "tn_net_set_blocking"; }
  if(len==15 && strncmp(name,"net_set_timeout",15)==0){ *out_len=18; return "tn_net_set_timeout"; }
  if(len==11 && strncmp(name,"net_resolve",11)==0){ *out_len=14; return "tn_net_resolve"; }
  if(len==13 && strncmp(name,"http_download",13)==0){ *out_len=16; return "tn_http_download"; }
  if(len==11 && strncmp(name,"tls_connect",11)==0){ *out_len=14; return "tn_tls_connect"; }
  if(len==14 && strncmp(name,"tls_connect_ex",14)==0){ *out_len=17; return "tn_tls_connect_ex"; }
  if(len==8 && strncmp(name,"tls_send",8)==0){ *out_len=11; return "tn_tls_send"; }
  if(len==8 && strncmp(name,"tls_recv",8)==0){ *out_len=11; return "tn_tls_recv"; }
  if(len==8 && strncmp(name,"tls_free",8)==0){ *out_len=12; return "tn_tls_close"; }
  if(len==9 && strncmp(name,"tls_close",9)==0){ *out_len=12; return "tn_tls_close"; }
  if(len==16 && strncmp(name,"tls_policy_reset",16)==0){ *out_len=19; return "tn_tls_policy_reset"; }
  if(len==18 && strncmp(name,"tls_policy_set_min",18)==0){ *out_len=21; return "tn_tls_policy_set_min"; }
  if(len==25 && strncmp(name,"tls_policy_set_pin_sha256",25)==0){ *out_len=28; return "tn_tls_policy_set_pin_sha256"; }
  if(len==32 && strncmp(name,"tls_policy_set_handshake_timeout",32)==0){ *out_len=35; return "tn_tls_policy_set_handshake_timeout"; }
  if(len==18 && strncmp(name,"tls_policy_get_min",18)==0){ *out_len=21; return "tn_tls_policy_get_min"; }
  if(len==32 && strncmp(name,"tls_policy_get_handshake_timeout",32)==0){ *out_len=35; return "tn_tls_policy_get_handshake_timeout"; }
  if(len==22 && strncmp(name,"tls_policy_pin_enabled",22)==0){ *out_len=25; return "tn_tls_policy_pin_enabled"; }
  if(len==12 && strncmp(name,"simd.v4f_add",12)==0){ *out_len=15; return "tn_simd_v4f_add"; }
  if(len==12 && strncmp(name,"simd.v4f_sub",12)==0){ *out_len=15; return "tn_simd_v4f_sub"; }
  if(len==12 && strncmp(name,"simd.v4f_mul",12)==0){ *out_len=15; return "tn_simd_v4f_mul"; }
  if(len==12 && strncmp(name,"simd.v4f_dot",12)==0){ *out_len=15; return "tn_simd_v4f_dot"; }
  if(len==12 && strncmp(name,"simd.v4i_add",12)==0){ *out_len=15; return "tn_simd_v4i_add"; }
  if(len==12 && strncmp(name,"simd.v4i_mul",12)==0){ *out_len=15; return "tn_simd_v4i_mul"; }
  if(len==20 && strncmp(name,"simd.v4f_add_strided",20)==0){ *out_len=23; return "tn_simd_v4f_add_strided"; }
  if(len==20 && strncmp(name,"simd.v4f_sub_strided",20)==0){ *out_len=23; return "tn_simd_v4f_sub_strided"; }
  if(len==20 && strncmp(name,"simd.v4f_mul_strided",20)==0){ *out_len=23; return "tn_simd_v4f_mul_strided"; }
  if(len==20 && strncmp(name,"simd.v4i_add_strided",20)==0){ *out_len=23; return "tn_simd_v4i_add_strided"; }
  if(len==20 && strncmp(name,"simd.v4i_mul_strided",20)==0){ *out_len=23; return "tn_simd_v4i_mul_strided"; }
  if(len==13 && strncmp(name,"simd.v4f_load",13)==0){ *out_len=16; return "tn_simd_v4f_load"; }
  if(len==14 && strncmp(name,"simd.v4f_store",14)==0){ *out_len=17; return "tn_simd_v4f_store"; }
  if(len==13 && strncmp(name,"simd.v4i_load",13)==0){ *out_len=16; return "tn_simd_v4i_load"; }
  if(len==14 && strncmp(name,"simd.v4i_store",14)==0){ *out_len=17; return "tn_simd_v4i_store"; }
  if(len==15 && strncmp(name,"gpu_has_backend",15)==0){ *out_len=18; return "tn_gpu_has_backend"; }
  if(len==8 && strncmp(name,"gpu_init",8)==0){ *out_len=11; return "tn_gpu_init"; }
  if(len==12 && strncmp(name,"gpu_shutdown",12)==0){ *out_len=15; return "tn_gpu_shutdown"; }
  if(len==16 && strncmp(name,"gpu_device_count",16)==0){ *out_len=19; return "tn_gpu_device_count"; }
  if(len==14 && strncmp(name,"gpu_set_device",14)==0){ *out_len=17; return "tn_gpu_set_device"; }
  if(len==9 && strncmp(name,"gpu_alloc",9)==0){ *out_len=12; return "tn_gpu_alloc"; }
  if(len==8 && strncmp(name,"gpu_free",8)==0){ *out_len=11; return "tn_gpu_free"; }
  if(len==20 && strncmp(name,"gpu_memcpy_to_device",20)==0){ *out_len=23; return "tn_gpu_memcpy_to_device"; }
  if(len==18 && strncmp(name,"gpu_memcpy_to_host",18)==0){ *out_len=21; return "tn_gpu_memcpy_to_host"; }
  if(len==10 && strncmp(name,"gpu_memset",10)==0){ *out_len=13; return "tn_gpu_memset"; }
  if(len==13 && strncmp(name,"gpu_launch_1d",13)==0){ *out_len=16; return "tn_gpu_launch_1d"; }
  if(len==18 && strncmp(name,"gpu_launch_dxil_1d",18)==0){ *out_len=21; return "tn_gpu_launch_dxil_1d"; }
  if(len==16 && strncmp(name,"gpu_backend_name",16)==0){ *out_len=19; return "tn_gpu_backend_name"; }
  if(len==19 && strncmp(name,"gpu_supports_dxil",19)==0){ *out_len=22; return "tn_gpu_supports_dxil"; }
  if(len==15 && strncmp(name,"npu_has_backend",15)==0){ *out_len=18; return "tn_npu_has_backend"; }
  if(len==8 && strncmp(name,"npu_init",8)==0){ *out_len=11; return "tn_npu_init"; }
  if(len==12 && strncmp(name,"npu_shutdown",12)==0){ *out_len=15; return "tn_npu_shutdown"; }
  if(len==16 && strncmp(name,"npu_device_count",16)==0){ *out_len=19; return "tn_npu_device_count"; }
  if(len==14 && strncmp(name,"npu_set_device",14)==0){ *out_len=17; return "tn_npu_set_device"; }
  if(len==9 && strncmp(name,"npu_alloc",9)==0){ *out_len=12; return "tn_npu_alloc"; }
  if(len==8 && strncmp(name,"npu_free",8)==0){ *out_len=11; return "tn_npu_free"; }
  if(len==20 && strncmp(name,"npu_memcpy_to_device",20)==0){ *out_len=23; return "tn_npu_memcpy_to_device"; }
  if(len==18 && strncmp(name,"npu_memcpy_to_host",18)==0){ *out_len=21; return "tn_npu_memcpy_to_host"; }
  if(len==10 && strncmp(name,"npu_memset",10)==0){ *out_len=13; return "tn_npu_memset"; }
  if(len==13 && strncmp(name,"npu_launch_1d",13)==0){ *out_len=16; return "tn_npu_launch_1d"; }
  if(len==18 && strncmp(name,"npu_launch_dxil_1d",18)==0){ *out_len=21; return "tn_npu_launch_dxil_1d"; }
  if(len==16 && strncmp(name,"npu_backend_name",16)==0){ *out_len=19; return "tn_npu_backend_name"; }
  if(len==19 && strncmp(name,"npu_supports_dxil",19)==0){ *out_len=22; return "tn_npu_supports_dxil"; }
  if(len==14 && strncmp(name,"npu_model_load",14)==0){ *out_len=17; return "tn_npu_model_load"; }
  if(len==14 && strncmp(name,"npu_model_free",14)==0){ *out_len=17; return "tn_npu_model_free"; }
  if(len==20 && strncmp(name,"npu_model_run_f64_1d",20)==0){ *out_len=23; return "tn_npu_model_run_f64_1d"; }
  if(len==10 && strncmp(name,"args_count",10)==0){ *out_len=13; return "tn_args_count"; }
  if(len==3 && strncmp(name,"arg",3)==0){ *out_len=6; return "tn_arg"; }
  if(len==7 && strncmp(name,"proc_run",7)==0){ *out_len=10; return "tn_proc_run"; }
  if(len==8 && strncmp(name,"proc_out",8)==0){ *out_len=11; return "tn_proc_out"; }

  *out_len = len;
  return name;
}

static void emit_c_string(FILE* f, const char* s){
  fputc('"', f);
  for(const char* p = s ? s : ""; *p; p++){
    unsigned char c = (unsigned char)(*p);
    if(c=='\\') fputs("\\\\", f);
    else if(c=='\"') fputs("\\\"", f);
    else if(c=='\n') fputs("\\n", f);
    else if(c=='\r') fputs("\\r", f);
    else if(c=='\t') fputs("\\t", f);
    else if(c < 32 || c >= 127){
      fprintf(f, "\\x%02x", (unsigned int)c);
    } else {
      fputc((char)c, f);
    }
  }
  fputc('"', f);
}

typedef struct {
  const char* name;
  int len;
  int argc;
} CallDecl;

static int call_decl_eq(const CallDecl* a, const CallDecl* b){
  if(!a || !b || a->len < 0 || b->len < 0 || !a->name || !b->name){
    return 0;
  }
  return a->len==b->len && a->argc==b->argc && strncmp(a->name, b->name, (size_t)a->len)==0;
}

static int fn_is_defined(IRModule* m, const char* name, int len){
  if(!m){
    return 0;
  }
  name = sanitize_sym_name(name, &len);
  for(int i=0;i<m->fn_n;i++){
    IRFunc* f = &m->fns[i];
    if(f->name && f->len==len && strncmp(f->name, name, (size_t)len)==0) return 1;
  }
  return 0;
}

static char* dup_cstr(const char* s){
  if(!s) s = "";
  size_t n = strlen(s);
  char* out = (char*)malloc(n+1);
  if(!out) die("out of memory");
  memcpy(out, s, n+1);
  return out;
}

static void emit_label(FILE* f, const char* fn, int id){
  fprintf(f, "L_%s_%d", fn, id);
}

void ir_compile_to_cuda(IRModule* m, const char* out_cu){
  if(!m || !out_cu) return;
  FILE* f = fopen(out_cu, "wb");
  if(!f) die("ir_codegen_cuda: cannot open output");

  fputs("// Auto-generated by tezzc (IR -> CUDA C)\n", f);
  fputs("#include <cuda_runtime.h>\n", f);
  fputs("#include <stdint.h>\n", f);
  fputs("#include <string.h>\n", f);
  fputs("#include <stdlib.h>\n", f);
  fputs("#include <stdio.h>\n", f);
  fputs("\n", f);
  fputs("#if defined(_MSC_VER)\n", f);
  fputs("#  define TN_ALIGN(N) __declspec(align(N))\n", f);
  fputs("#else\n", f);
  fputs("#  define TN_ALIGN(N) __attribute__((aligned(N)))\n", f);
  fputs("#endif\n", f);
  fputs("static inline double tn_bits_to_f(int64_t bits){ double x; memcpy(&x,&bits,8); return x; }\n", f);
  fputs("static inline int64_t tn_f_to_bits(double x){ int64_t b; memcpy(&b,&x,8); return b; }\n", f);
  fputs("\n", f);

  // string constants
  for(int i=0;i<m->str_n;i++){
    fprintf(f, "static const unsigned char tn_str_%d[] = ", i);
    emit_c_string(f, m->strs[i].bytes);
    fputs(";\n", f);
  }
  if(m->str_n > 0) fputc('\n', f);

  // globals
  for(int i=0;i<m->global_n;i++){
    IRGlobal* g = &m->globals[i];
    char sym[512];
    mangle_sym(g->name, g->len, sym, (int)sizeof(sym));
    const char* storage = g->is_static ? "static " : "";
    if(g->is_extern){
      if(g->size==1) fprintf(f, "extern int8_t %s;\n", sym);
      else if(g->size==2) fprintf(f, "extern int16_t %s;\n", sym);
      else if(g->size==4) fprintf(f, "extern int32_t %s;\n", sym);
      else if(g->size==8) fprintf(f, "extern int64_t %s;\n", sym);
      else fprintf(f, "extern unsigned char %s[%d];\n", sym, g->size);
      continue;
    }
    if(g->size==1){
      fprintf(f, "%sint8_t %s", storage, sym);
      if(g->has_init) fprintf(f, " = (int8_t)%lld", g->init_int);
      fputs(";\n", f);
    } else if(g->size==2){
      fprintf(f, "%sint16_t %s", storage, sym);
      if(g->has_init) fprintf(f, " = (int16_t)%lld", g->init_int);
      fputs(";\n", f);
    } else if(g->size==4){
      fprintf(f, "%sint32_t %s", storage, sym);
      if(g->has_init) fprintf(f, " = (int32_t)%lld", g->init_int);
      fputs(";\n", f);
    } else if(g->size==8){
      fprintf(f, "%sint64_t %s", storage, sym);
      if(g->has_init){
        if(g->init_fbits){
          double x = 0.0;
          memcpy(&x, &g->init_fbits, sizeof(double));
          fprintf(f, " = tn_f_to_bits(%.*g)", 17, x);
        } else {
          fprintf(f, " = (int64_t)%lld", g->init_int);
        }
      }
      fputs(";\n", f);
    } else {
      fprintf(f, "%sunsigned char %s[%d] = {0};\n", storage, sym, g->size);
    }
  }
  if(m->global_n > 0) fputc('\n', f);

  // forward decls for functions defined in IR
  for(int i=0;i<m->fn_n;i++){
    IRFunc* fn = &m->fns[i];
    char sym[512];
    mangle_sym(fn->name, fn->len, sym, (int)sizeof(sym));
    if(fn->is_extern){
      fprintf(f, "extern int64_t %s(", sym);
    } else if(fn->is_kernel){
      fprintf(f, "extern \"C\" __global__ void %s(", sym);
    } else {
      fprintf(f, "static int64_t %s(", sym);
    }
    for(int p=0;p<fn->param_count;p++){
      if(p) fputs(", ", f);
      fprintf(f, "int64_t a%d", p);
    }
    if(fn->param_count==0) fputs("void", f);
    fputs(");\n", f);
  }

  // extern decls for runtime/builtin calls not in module
  CallDecl* calls = NULL;
  int call_n = 0, call_cap = 0;
  for(int i=0;i<m->fn_n;i++){
    IRFunc* fn = &m->fns[i];
    for(int j=0;j<fn->n;j++){
      IRIns* in = &fn->ins[j];
      if(in->op != I_CALL) continue;
      int maplen = 0;
      const char* mapped = map_builtin_name(in->name, in->nlen, &maplen);
      int argc = in->argc;
      if(argc < 0){
        argc = 0;
      } else if(argc > TN_CODEGEN_CALL_ARG_MAX){
        argc = TN_CODEGEN_CALL_ARG_MAX;
      }
      char sym[512];
      mangle_sym(mapped, maplen, sym, (int)sizeof(sym));
      if(fn_is_defined(m, mapped, maplen)) continue;
      char* cp = dup_cstr(sym);
      CallDecl cd = {cp, (int)strlen(cp), argc};
      int exists = 0;
      for(int k=0;k<call_n;k++){
        if(call_decl_eq(&calls[k], &cd)){ exists = 1; break; }
      }
      if(!exists){
        if(call_n==call_cap){
          call_cap = call_cap? call_cap*2 : 32;
          calls = (CallDecl*)realloc(calls, sizeof(CallDecl)*(size_t)call_cap);
          if(!calls) die("out of memory");
        }
        calls[call_n++] = cd;
      } else {
        free(cp);
      }
    }
  }
  for(int i=0;i<call_n;i++){
    fprintf(f, "extern int64_t %.*s(", calls[i].len, calls[i].name);
    for(int a=0;a<calls[i].argc;a++){
      if(a) fputs(", ", f);
      fprintf(f, "int64_t a%d", a);
    }
    if(calls[i].argc==0) fputs("void", f);
    fputs(");\n", f);
  }
  if(call_n>0) fputc('\n', f);
  for(int i=0;i<call_n;i++){
    free((void*)calls[i].name);
  }
  free(calls);

  // function definitions
  for(int i=0;i<m->fn_n;i++){
    IRFunc* fn = &m->fns[i];
    if(fn->is_extern) continue;
    char fnsym[512];
    mangle_sym(fn->name, fn->len, fnsym, (int)sizeof(fnsym));
    if(fn->is_kernel){
      fprintf(f, "extern \"C\" __global__ void %s(", fnsym);
    } else {
      fprintf(f, "static int64_t %s(", fnsym);
    }
    for(int p=0;p<fn->param_count;p++){
      if(p) fputs(", ", f);
      fprintf(f, "int64_t a%d", p);
    }
    if(fn->param_count==0) fputs("void", f);
    fputs("){\n", f);

    int regs = (fn->max_reg >= 0) ? (fn->max_reg + 1) : 0;
    if(regs < 1) regs = 1;
    fprintf(f, "  int64_t r[%d];\n", regs);
    if(fn->is_kernel){
      fprintf(f, "  for(int i=0;i<%d;i++) r[i]=0;\n", regs);
    } else {
      fprintf(f, "  memset(r, 0, sizeof(r));\n");
    }

    // allocas
    for(int j=0;j<fn->n;j++){
      IRIns* in = &fn->ins[j];
      if(in->op==I_ALLOCA){
        int al = in->align ? in->align : 1;
        int sz = in->size > 0 ? in->size : 1;
        if(al > 1){
          fprintf(f, "  TN_ALIGN(%d) unsigned char _alloca_%d[%d];\n", al, in->a, sz);
        } else {
          fprintf(f, "  unsigned char _alloca_%d[%d];\n", in->a, sz);
        }
      }
    }

    // param mapping: r0,r2,r4...
    for(int p=0;p<fn->param_count;p++){
      int vreg = p * 2;
      if(vreg < regs){
        fprintf(f, "  r[%d] = a%d;\n", vreg, p);
      }
    }

    for(int j=0;j<fn->n;j++){
      IRIns* in = &fn->ins[j];
      int is_kernel = fn->is_kernel;
      switch(in->op){
        case I_LABEL:
          fputs("  ", f);
          emit_label(f, fnsym, in->a);
          fputs(":\n", f);
          break;
        case I_ICONST:
          fprintf(f, "  r[%d] = %lld;\n", in->a, in->imm);
          break;
        case I_FCONST:
          fprintf(f, "  r[%d] = %lld;\n", in->a, in->immf);
          break;
        case I_ADDRSYM: {
          char sym[512];
          mangle_sym(in->name, in->nlen, sym, (int)sizeof(sym));
          fprintf(f, "  r[%d] = (int64_t)(intptr_t)&%s;\n", in->a, sym);
          break;
        }
        case I_SCONST:
          fprintf(f, "  r[%d] = (int64_t)(intptr_t)tn_str_%d;\n", in->a, in->sid);
          break;
        case I_MOV:
          fprintf(f, "  r[%d] = r[%d];\n", in->a, in->b);
          break;
        case I_BIN: {
          const char* op = "+";
          switch(in->binop){
            case B_ADD: op="+"; break;
            case B_SUB: op="-"; break;
            case B_MUL: op="*"; break;
            case B_DIV: op="/"; break;
            case B_MOD: op="%"; break;
            case B_AND: op="&"; break;
            case B_OR:  op="|"; break;
            case B_XOR: op="^"; break;
            case B_SHL: op="<<"; break;
            case B_SHR: op=">>"; break;
          }
          fprintf(f, "  r[%d] = (int64_t)(r[%d] %s r[%d]);\n", in->a, in->b, op, in->c);
          break;
        }
        case I_CMP: {
          const char* op = "==";
          switch(in->cmpop){
            case C_EQ: op="=="; break;
            case C_NEQ: op="!="; break;
            case C_LT: op="<"; break;
            case C_LTE: op="<="; break;
            case C_GT: op=">"; break;
            case C_GTE: op=">="; break;
          }
          fprintf(f, "  r[%d] = (r[%d] %s r[%d]) ? 1 : 0;\n", in->a, in->b, op, in->c);
          break;
        }
        case I_FBIN: {
          const char* op = "+";
          switch(in->fop){
            case F_ADD: op="+"; break;
            case F_SUB: op="-"; break;
            case F_MUL: op="*"; break;
            case F_DIV: op="/"; break;
          }
          fprintf(f, "  r[%d] = tn_f_to_bits(tn_bits_to_f(r[%d]) %s tn_bits_to_f(r[%d]));\n", in->a, in->b, op, in->c);
          break;
        }
        case I_FCMP: {
          const char* op = "==";
          switch(in->cmpop){
            case C_EQ: op="=="; break;
            case C_NEQ: op="!="; break;
            case C_LT: op="<"; break;
            case C_LTE: op="<="; break;
            case C_GT: op=">"; break;
            case C_GTE: op=">="; break;
          }
          fprintf(f, "  r[%d] = (tn_bits_to_f(r[%d]) %s tn_bits_to_f(r[%d])) ? 1 : 0;\n", in->a, in->b, op, in->c);
          break;
        }
        case I_I2F:
          fprintf(f, "  r[%d] = tn_f_to_bits((double)r[%d]);\n", in->a, in->b);
          break;
        case I_F2I:
          fprintf(f, "  r[%d] = (int64_t)tn_bits_to_f(r[%d]);\n", in->a, in->b);
          break;
        case I_CALL: {
          int call_name_len = in->nlen;
          const char* call_name = sanitize_sym_name(in->name, &call_name_len);
          if(call_name_len==7 && strncmp(call_name, "gpu_tid", 7)==0){
            fprintf(f, "  r[%d] = (int64_t)(blockIdx.x * blockDim.x + threadIdx.x);\n", in->a);
            break;
          }
          int maplen = 0;
          const char* mapped = map_builtin_name(call_name, call_name_len, &maplen);
          int argc = in->argc;
          if(argc < 0){
            argc = 0;
          } else if(argc > TN_CODEGEN_CALL_ARG_MAX){
            argc = TN_CODEGEN_CALL_ARG_MAX;
          }
          char sym[512];
          mangle_sym(mapped, maplen, sym, (int)sizeof(sym));
          fprintf(f, "  r[%d] = %s(", in->a, sym);
          for(int a=0;a<argc;a++){
            if(a) fputs(", ", f);
            fprintf(f, "r[%d]", safe_call_arg(in, a));
          }
          if(argc==0) fputs("", f);
          fputs(");\n", f);
          break;
        }
        case I_CALLPTR: {
          int argc = in->argc;
          if(argc < 0){
            argc = 0;
          } else if(argc > TN_CODEGEN_CALL_ARG_MAX){
            argc = TN_CODEGEN_CALL_ARG_MAX;
          }
          fprintf(f, "  r[%d] = ((int64_t(*)(%s)) (intptr_t)r[%d])(", in->a,
                  (argc==0) ? "void" : "int64_t", in->b);
          if(argc > 1){
            for(int a=1;a<argc;a++) fputs(", int64_t", f);
          }
          fputs("))(", f);
          for(int a=0;a<argc;a++){
            if(a) fputs(", ", f);
            fprintf(f, "r[%d]", safe_call_arg(in, a));
          }
          fputs(");\n", f);
          break;
        }
        case I_ALLOCA:
          fprintf(f, "  r[%d] = (int64_t)(intptr_t)_alloca_%d;\n", in->a, in->a);
          break;
        case I_LOAD: {
          fprintf(f, "  {\n");
          if(in->is_unsigned || in->size >= 8){
            fprintf(f, "    uint64_t tmp = 0;\n");
            fprintf(f, "    memcpy(&tmp, (void*)(intptr_t)r[%d], %d);\n", in->b, in->size);
            fprintf(f, "    r[%d] = (int64_t)tmp;\n", in->a);
          } else if(in->size == 1){
            fprintf(f, "    int8_t tmp = 0;\n");
            fprintf(f, "    memcpy(&tmp, (void*)(intptr_t)r[%d], 1);\n", in->b);
            fprintf(f, "    r[%d] = (int64_t)tmp;\n", in->a);
          } else if(in->size == 2){
            fprintf(f, "    int16_t tmp = 0;\n");
            fprintf(f, "    memcpy(&tmp, (void*)(intptr_t)r[%d], 2);\n", in->b);
            fprintf(f, "    r[%d] = (int64_t)tmp;\n", in->a);
          } else if(in->size == 4){
            fprintf(f, "    int32_t tmp = 0;\n");
            fprintf(f, "    memcpy(&tmp, (void*)(intptr_t)r[%d], 4);\n", in->b);
            fprintf(f, "    r[%d] = (int64_t)tmp;\n", in->a);
          }
          fprintf(f, "  }\n");
          break;
        }
        case I_STORE: {
          fprintf(f, "  {\n");
          fprintf(f, "    uint64_t tmp = (uint64_t)r[%d];\n", in->b);
          fprintf(f, "    memcpy((void*)(intptr_t)r[%d], &tmp, %d);\n", in->a, in->size);
          fprintf(f, "  }\n");
          break;
        }
        case I_GEP:
          if(in->imm!=0) fprintf(f, "  r[%d] = r[%d] + %lld;\n", in->a, in->b, in->imm);
          else fprintf(f, "  r[%d] = r[%d];\n", in->a, in->b);
          break;
        case I_JMP:
          fputs("  goto ", f);
          emit_label(f, fnsym, in->a);
          fputs(";\n", f);
          break;
        case I_JZ:
          fprintf(f, "  if(r[%d]==0) goto ", in->a);
          emit_label(f, fnsym, in->b);
          fputs(";\n", f);
          break;
        case I_RET:
          if(is_kernel){
            fputs("  return;\n", f);
          } else {
            fprintf(f, "  return r[%d];\n", in->a);
          }
          break;
        default:
          break;
      }
    }

    fputs("  return 0;\n", f);
    fputs("}\n\n", f);
  }

  fclose(f);
}
