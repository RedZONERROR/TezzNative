// src/ir_codegen_gas.c
#include "ir.h"
#include "ir_codegen_gas.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int align_up_i(int x, int a){ return (x + (a-1)) & ~(a-1); }
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
  // Convert "math.fib" -> "math__fib", and anything weird -> '_'
  int w=0;
  for(int i=0;i<n && w<cap-1;i++){
    char c=in[i];
    if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'){
      out[w++]=c;
    } else if(c=='.'){
      if(w<cap-2){ out[w++]='_'; out[w++]='_'; }
      else out[w++]='_';
    } else {
      out[w++]='_';
    }
  }
  out[w]=0;
}

static void func_label(char* out, int cap, const char* fname, int flen, int lid){
  // .L<fname>_<lid>
  char tmp[256];
  mangle_sym(fname, flen, tmp, (int)sizeof(tmp));
  snprintf(out, (size_t)cap, ".L%s_%d", tmp, lid);
}

// ---- stack slot for virtual reg ----
// We store every virtual reg as 8 bytes at [rbp - off].
// r0 at -8, r1 at -16, etc.
static int reg_off(int r){ return 8*(r+1); } // bytes below rbp

static void store_reg_rax(FILE* f, int r){
  fprintf(f, "  movq %%rax, -%d(%%rbp)\n", reg_off(r));
}
static void load_reg_rax(FILE* f, int r){
  fprintf(f, "  movq -%d(%%rbp), %%rax\n", reg_off(r));
}
static void load_reg_rcx(FILE* f, int r){
  fprintf(f, "  movq -%d(%%rbp), %%rcx\n", reg_off(r));
}
static void load_reg_rdx(FILE* f, int r){
  fprintf(f, "  movq -%d(%%rbp), %%rdx\n", reg_off(r));
}
static void load_reg_r8(FILE* f, int r){
  fprintf(f, "  movq -%d(%%rbp), %%r8\n", reg_off(r));
}
static void load_reg_r9(FILE* f, int r){
  fprintf(f, "  movq -%d(%%rbp), %%r9\n", reg_off(r));
}

static void load_reg_xmm0(FILE* f, int r){
  fprintf(f, "  movsd -%d(%%rbp), %%xmm0\n", reg_off(r));
}
static void load_reg_xmm1(FILE* f, int r){
  fprintf(f, "  movsd -%d(%%rbp), %%xmm1\n", reg_off(r));
}
static void store_reg_xmm0(FILE* f, int r){
  fprintf(f, "  movsd %%xmm0, -%d(%%rbp)\n", reg_off(r));
}

static void store_reg_al(FILE* f, int r){
  // store %al (0/1) as full i64
  fprintf(f, "  movzbq %%al, %%rax\n");
  store_reg_rax(f, r);
}

// For LOAD/STORE/GEP we use %rax as address scratch.
static void load_addr_rax(FILE* f, int addr_reg){
  load_reg_rax(f, addr_reg);
}

static const char* setcc_for(IRCmp c){
  switch(c){
    case C_EQ:  return "sete";
    case C_NEQ: return "setne";
    case C_LT:  return "setl";
    case C_LTE: return "setle";
    case C_GT:  return "setg";
    case C_GTE: return "setge";
    default:    return "sete";
  }
}

static void emit_bin(FILE* f, IRBin op){
  // expects: a in %rax, b in %rcx
  switch(op){
    case B_ADD: fprintf(f, "  addq %%rcx, %%rax\n"); break;
    case B_SUB: fprintf(f, "  subq %%rcx, %%rax\n"); break;
    case B_MUL: fprintf(f, "  imulq %%rcx, %%rax\n"); break;
    case B_AND: fprintf(f, "  andq %%rcx, %%rax\n"); break;
    case B_OR:  fprintf(f, "  orq  %%rcx, %%rax\n"); break;
    case B_XOR: fprintf(f, "  xorq %%rcx, %%rax\n"); break;
    case B_SHL:
      // shift count must be in CL
      fprintf(f, "  movq %%rcx, %%r10\n");
      fprintf(f, "  movb %%r10b, %%cl\n");
      fprintf(f, "  shlq %%cl, %%rax\n");
      break;
    case B_SHR:
      fprintf(f, "  movq %%rcx, %%r10\n");
      fprintf(f, "  movb %%r10b, %%cl\n");
      fprintf(f, "  sarq %%cl, %%rax\n");
      break;
    case B_DIV:
    case B_MOD:
      // idiv uses rdx:rax / rcx
      // a in rax, b in rcx
      fprintf(f, "  cqto\n");               // sign-extend rax -> rdx:rax
      fprintf(f, "  idivq %%rcx\n");        // quotient rax, remainder rdx
      if(op==B_MOD) fprintf(f, "  movq %%rdx, %%rax\n");
      break;
    default:
      fprintf(f, "  addq %%rcx, %%rax\n");
      break;
  }
}

typedef struct {
  int* dst_reg;     // alloca result reg
  int* off_bytes;   // pointer = rbp - off
  int n, cap;
} AllocaMap;

static void amap_add(AllocaMap* A, int r, int off){
  if(A->n==A->cap){
    A->cap = A->cap? A->cap*2 : 16;
    A->dst_reg  = (int*)realloc(A->dst_reg,  sizeof(int)* (size_t)A->cap);
    A->off_bytes= (int*)realloc(A->off_bytes,sizeof(int)* (size_t)A->cap);
    if(!A->dst_reg || !A->off_bytes) die("out of memory");
  }
  A->dst_reg[A->n]=r;
  A->off_bytes[A->n]=off;
  A->n++;
}

static int amap_find(AllocaMap* A, int r){
  for(int i=0;i<A->n;i++){
    if(A->dst_reg[i]==r) return A->off_bytes[i];
  }
  return -1;
}

static int emit_call_win64_prep(FILE* f, IRIns* in){
  if(!f || !in){
    return 0;
  }
  // Win64 ABI:
  // rcx, rdx, r8, r9 then stack args at [rsp+32...]
  // shadow space 32 bytes is REQUIRED at call time.
  int argc = in->argc;
  const int* call_args = in->args;
  if(argc < 0){
    argc = 0;
  } else if(argc > TN_CODEGEN_CALL_ARG_MAX){
    argc = TN_CODEGEN_CALL_ARG_MAX;
  }
  if(argc > 0 && !call_args){
    argc = 0;
  }

  int extra = (argc > 4) ? (argc - 4) * 8 : 0;

  // Reserve shadow + extra, then align to 16
  int total = 32 + extra;
  int total_al = align_up_i(total, 16);

  if(total_al){
    fprintf(f, "  subq $%d, %%rsp\n", total_al);
  }

  // stack args (begin at rsp+32)
  for(int i=4;i<argc;i++){
    fprintf(f, "  movq -%d(%%rbp), %%rax\n", reg_off(call_args[i]));
    fprintf(f, "  movq %%rax, %d(%%rsp)\n", 32 + 8*(i-4));
  }

  // register args
  if(argc>0) load_reg_rcx(f, call_args[0]);
  if(argc>1) load_reg_rdx(f, call_args[1]);
  if(argc>2) load_reg_r8 (f, call_args[2]);
  if(argc>3) load_reg_r9 (f, call_args[3]);
  return total_al;
}

static void emit_call_win64(FILE* f, IRIns* in){
  if(!f || !in){
    return;
  }
  int total_al = emit_call_win64_prep(f, in);

  // call symbol
  char sym[512];
  const char* callname = in->name;
  int calllen = in->nlen;
  callname = sanitize_sym_name(callname, &calllen);

  // map language builtins to runtime symbols
  if(calllen==6 && strncmp(callname, "strcmp", 6)==0){
    callname = "tn_strcmp";
    calllen = 9;
  }
  if(calllen==10 && strncmp(callname, "bit_popcnt", 10)==0){
    callname = "tn_bit_popcnt";
    calllen = 13;
  }
  if(calllen==7 && strncmp(callname, "bit_clz", 7)==0){
    callname = "tn_bit_clz";
    calllen = 10;
  }
  if(calllen==7 && strncmp(callname, "bit_ctz", 7)==0){
    callname = "tn_bit_ctz";
    calllen = 10;
  }
  if(calllen==9 && strncmp(callname, "bit_bswap", 9)==0){
    callname = "tn_bit_bswap";
    calllen = 12;
  }
  if(calllen==8 && strncmp(callname, "bit_rotl", 8)==0){
    callname = "tn_bit_rotl";
    calllen = 11;
  }
  if(calllen==8 && strncmp(callname, "bit_rotr", 8)==0){
    callname = "tn_bit_rotr";
    calllen = 11;
  }
  if(calllen==12 && strncmp(callname, "cpu_has_sse2", 12)==0){
    callname = "tn_cpu_has_sse2";
    calllen = 15;
  }
  if(calllen==12 && strncmp(callname, "cpu_has_avx2", 12)==0){
    callname = "tn_cpu_has_avx2";
    calllen = 15;
  }
  if(calllen==11 && strncmp(callname, "cpu_has_fma", 11)==0){
    callname = "tn_cpu_has_fma";
    calllen = 14;
  }
  if(calllen==12 && strncmp(callname, "cpu_has_neon", 12)==0){
    callname = "tn_cpu_has_neon";
    calllen = 15;
  }
  if(calllen==5 && strncmp(callname, "fopen", 5)==0){
    callname = "tn_fopen";
    calllen = 8;
  }
  if(calllen==6 && strncmp(callname, "fclose", 6)==0){
    callname = "tn_fclose";
    calllen = 9;
  }
  if(calllen==6 && strncmp(callname, "fflush", 6)==0){
    callname = "tn_fflush";
    calllen = 9;
  }
  if(calllen==5 && strncmp(callname, "fread", 5)==0){
    callname = "tn_fread";
    calllen = 8;
  }
  if(calllen==6 && strncmp(callname, "fwrite", 6)==0){
    callname = "tn_fwrite";
    calllen = 9;
  }
  if(calllen==5 && strncmp(callname, "fseek", 5)==0){
    callname = "tn_fseek";
    calllen = 8;
  }
  if(calllen==5 && strncmp(callname, "ftell", 5)==0){
    callname = "tn_ftell";
    calllen = 8;
  }
  if(calllen==9 && strncmp(callname, "file_size", 9)==0){
    callname = "tn_file_size";
    calllen = 12;
  }
  if(calllen==9 && strncmp(callname, "read_file", 9)==0){
    callname = "tn_read_file";
    calllen = 12;
  }
  if(calllen==10 && strncmp(callname, "write_file", 10)==0){
    callname = "tn_write_file";
    calllen = 13;
  }
  if(calllen==9 && strncmp(callname, "read_line", 9)==0){
    callname = "tn_read_line";
    calllen = 12;
  }
  if(calllen==10 && strncmp(callname, "read_bytes", 10)==0){
    callname = "tn_read_bytes";
    calllen = 13;
  }
  if(calllen==10 && strncmp(callname, "write_line", 10)==0){
    callname = "tn_write_line";
    calllen = 13;
  }
  if(calllen==6 && strncmp(callname, "io_err", 6)==0){
    callname = "tn_io_err";
    calllen = 9;
  }
  if(calllen==6 && strncmp(callname, "io_eof", 6)==0){
    callname = "tn_io_eof";
    calllen = 9;
  }
  if(calllen==5 && strncmp(callname, "stdin", 5)==0){
    callname = "tn_stdin";
    calllen = 8;
  }
  if(calllen==6 && strncmp(callname, "stdout", 6)==0){
    callname = "tn_stdout";
    calllen = 9;
  }
  if(calllen==6 && strncmp(callname, "stderr", 6)==0){
    callname = "tn_stderr";
    calllen = 9;
  }
  if(calllen==7 && strncmp(callname, "get_cwd", 7)==0){
    callname = "tn_get_cwd";
    calllen = 10;
  }
  if(calllen==5 && strncmp(callname, "chdir", 5)==0){
    callname = "tn_chdir";
    calllen = 8;
  }
  if(calllen==5 && strncmp(callname, "mkdir", 5)==0){
    callname = "tn_mkdir";
    calllen = 8;
  }
  if(calllen==5 && strncmp(callname, "rmdir", 5)==0){
    callname = "tn_rmdir";
    calllen = 8;
  }
  if(calllen==6 && strncmp(callname, "remove", 6)==0){
    callname = "tn_remove";
    calllen = 9;
  }
  if(calllen==6 && strncmp(callname, "rename", 6)==0){
    callname = "tn_rename";
    calllen = 9;
  }
  if(calllen==11 && strncmp(callname, "path_exists", 11)==0){
    callname = "tn_path_exists";
    calllen = 14;
  }
  if(calllen==11 && strncmp(callname, "path_is_dir", 11)==0){
    callname = "tn_path_is_dir";
    calllen = 14;
  }
  if(calllen==9 && strncmp(callname, "path_join", 9)==0){
    callname = "tn_path_join";
    calllen = 12;
  }
  if(calllen==8 && strncmp(callname, "path_sep", 8)==0){
    callname = "tn_path_sep";
    calllen = 11;
  }
  if(calllen==13 && strncmp(callname, "path_basename", 13)==0){
    callname = "tn_path_basename";
    calllen = 16;
  }
  if(calllen==12 && strncmp(callname, "path_dirname", 12)==0){
    callname = "tn_path_dirname";
    calllen = 15;
  }
  if(calllen==8 && strncmp(callname, "list_dir", 8)==0){
    callname = "tn_list_dir";
    calllen = 11;
  }
  if(calllen==18 && strncmp(callname, "list_dir_recursive", 18)==0){
    callname = "tn_list_dir_recursive";
    calllen = 21;
  }
  if(calllen==4 && strncmp(callname, "glob", 4)==0){
    callname = "tn_glob";
    calllen = 7;
  }
  if(calllen==14 && strncmp(callname, "path_normalize", 14)==0){
    callname = "tn_path_normalize";
    calllen = 17;
  }
  if(calllen==19 && strncmp(callname, "path_normalize_opts", 19)==0){
    callname = "tn_path_normalize_opts";
    calllen = 22;
  }
  if(calllen==8 && strncmp(callname, "time_now", 8)==0){
    callname = "tn_time_now";
    calllen = 11;
  }
  if(calllen==11 && strncmp(callname, "time_now_ms", 11)==0){
    callname = "tn_time_now_ms";
    calllen = 14;
  }
  if(calllen==11 && strncmp(callname, "time_now_ns", 11)==0){
    callname = "tn_time_now_ns";
    calllen = 14;
  }
  if(calllen==8 && strncmp(callname, "date_now", 8)==0){
    callname = "tn_date_now";
    calllen = 11;
  }
  if(calllen==12 && strncmp(callname, "date_now_utc", 12)==0){
    callname = "tn_date_now_utc";
    calllen = 15;
  }
  if(calllen==8 && strncmp(callname, "sleep_ms", 8)==0){
    callname = "tn_sleep_ms";
    calllen = 11;
  }
  if(calllen==5 && strncmp(callname, "async", 5)==0){
    callname = "tn_async_spawn";
    calllen = 14;
  }
  if(calllen==5 && strncmp(callname, "await", 5)==0){
    callname = "tn_async_await";
    calllen = 14;
  }
  if(calllen==13 && strncmp(callname, "async_workers", 13)==0){
    callname = "tn_async_workers";
    calllen = 16;
  }
  if(calllen==13 && strncmp(callname, "async_pending", 13)==0){
    callname = "tn_async_pending";
    calllen = 16;
  }
  if(calllen==11 && strncmp(callname, "net_af_inet", 11)==0){
    callname = "tn_net_af_inet";
    calllen = 14;
  }
  if(calllen==12 && strncmp(callname, "net_af_inet6", 12)==0){
    callname = "tn_net_af_inet6";
    calllen = 15;
  }
  if(calllen==15 && strncmp(callname, "net_sock_stream", 15)==0){
    callname = "tn_net_sock_stream";
    calllen = 18;
  }
  if(calllen==14 && strncmp(callname, "net_sock_dgram", 14)==0){
    callname = "tn_net_sock_dgram";
    calllen = 17;
  }
  if(calllen==15 && strncmp(callname, "net_ipproto_tcp", 15)==0){
    callname = "tn_net_ipproto_tcp";
    calllen = 18;
  }
  if(calllen==15 && strncmp(callname, "net_ipproto_udp", 15)==0){
    callname = "tn_net_ipproto_udp";
    calllen = 18;
  }
  if(calllen==8 && strncmp(callname, "net_init", 8)==0){
    callname = "tn_net_init";
    calllen = 11;
  }
  if(calllen==11 && strncmp(callname, "net_cleanup", 11)==0){
    callname = "tn_net_cleanup";
    calllen = 14;
  }
  if(calllen==10 && strncmp(callname, "net_socket", 10)==0){
    callname = "tn_net_socket";
    calllen = 13;
  }
  if(calllen==9 && strncmp(callname, "net_close", 9)==0){
    callname = "tn_net_close";
    calllen = 12;
  }
  if(calllen==11 && strncmp(callname, "net_connect", 11)==0){
    callname = "tn_net_connect";
    calllen = 14;
  }
  if(calllen==8 && strncmp(callname, "net_bind", 8)==0){
    callname = "tn_net_bind";
    calllen = 11;
  }
  if(calllen==10 && strncmp(callname, "net_listen", 10)==0){
    callname = "tn_net_listen";
    calllen = 13;
  }
  if(calllen==10 && strncmp(callname, "net_accept", 10)==0){
    callname = "tn_net_accept";
    calllen = 13;
  }
  if(calllen==8 && strncmp(callname, "net_send", 8)==0){
    callname = "tn_net_send";
    calllen = 11;
  }
  if(calllen==8 && strncmp(callname, "net_recv", 8)==0){
    callname = "tn_net_recv";
    calllen = 11;
  }
  if(calllen==16 && strncmp(callname, "net_set_blocking", 16)==0){
    callname = "tn_net_set_blocking";
    calllen = 19;
  }
  if(calllen==15 && strncmp(callname, "net_set_timeout", 15)==0){
    callname = "tn_net_set_timeout";
    calllen = 18;
  }
  if(calllen==14 && strncmp(callname, "net_last_error", 14)==0){
    callname = "tn_net_last_error";
    calllen = 17;
  }
  if(calllen==8 && strncmp(callname, "proc_run", 8)==0){
    callname = "proc_run";
    calllen = 8;
  }
  if(calllen==8 && strncmp(callname, "proc_out", 8)==0){
    callname = "proc_out";
    calllen = 8;
  }
  if(calllen==13 && strncmp(callname, "http_download", 13)==0){
    callname = "tn_http_download";
    calllen = 16;
  }
  if(calllen==11 && strncmp(callname, "net_resolve", 11)==0){
    callname = "tn_net_resolve";
    calllen = 14;
  }
  if(calllen==11 && strncmp(callname, "tls_connect", 11)==0){
    callname = "tn_tls_connect";
    calllen = 14;
  }
  if(calllen==14 && strncmp(callname, "tls_connect_ex", 14)==0){
    callname = "tn_tls_connect_ex";
    calllen = 17;
  }
  if(calllen==8 && strncmp(callname, "tls_send", 8)==0){
    callname = "tn_tls_send";
    calllen = 11;
  }
  if(calllen==8 && strncmp(callname, "tls_recv", 8)==0){
    callname = "tn_tls_recv";
    calllen = 11;
  }
  if(calllen==9 && strncmp(callname, "tls_close", 9)==0){
    callname = "tn_tls_close";
    calllen = 12;
  }
  if(calllen==16 && strncmp(callname, "tls_policy_reset", 16)==0){
    callname = "tn_tls_policy_reset";
    calllen = 19;
  }
  if(calllen==18 && strncmp(callname, "tls_policy_set_min", 18)==0){
    callname = "tn_tls_policy_set_min";
    calllen = 21;
  }
  if(calllen==25 && strncmp(callname, "tls_policy_set_pin_sha256", 25)==0){
    callname = "tn_tls_policy_set_pin_sha256";
    calllen = 28;
  }
  if(calllen==32 && strncmp(callname, "tls_policy_set_handshake_timeout", 32)==0){
    callname = "tn_tls_policy_set_handshake_timeout";
    calllen = 35;
  }
  if(calllen==18 && strncmp(callname, "tls_policy_get_min", 18)==0){
    callname = "tn_tls_policy_get_min";
    calllen = 21;
  }
  if(calllen==32 && strncmp(callname, "tls_policy_get_handshake_timeout", 32)==0){
    callname = "tn_tls_policy_get_handshake_timeout";
    calllen = 35;
  }
  if(calllen==22 && strncmp(callname, "tls_policy_pin_enabled", 22)==0){
    callname = "tn_tls_policy_pin_enabled";
    calllen = 25;
  }
  if(calllen==12 && strncmp(callname, "simd.v4f_add", 12)==0){
    callname = "tn_simd_v4f_add";
    calllen = 15;
  }
  if(calllen==12 && strncmp(callname, "simd.v4f_sub", 12)==0){
    callname = "tn_simd_v4f_sub";
    calllen = 15;
  }
  if(calllen==12 && strncmp(callname, "simd.v4f_mul", 12)==0){
    callname = "tn_simd_v4f_mul";
    calllen = 15;
  }
  if(calllen==12 && strncmp(callname, "simd.v4f_dot", 12)==0){
    callname = "tn_simd_v4f_dot";
    calllen = 15;
  }
  if(calllen==12 && strncmp(callname, "simd.v4i_add", 12)==0){
    callname = "tn_simd_v4i_add";
    calllen = 15;
  }
  if(calllen==12 && strncmp(callname, "simd.v4i_mul", 12)==0){
    callname = "tn_simd_v4i_mul";
    calllen = 15;
  }
  if(calllen==20 && strncmp(callname, "simd.v4f_add_strided", 20)==0){
    callname = "tn_simd_v4f_add_strided";
    calllen = 23;
  }
  if(calllen==20 && strncmp(callname, "simd.v4f_sub_strided", 20)==0){
    callname = "tn_simd_v4f_sub_strided";
    calllen = 23;
  }
  if(calllen==20 && strncmp(callname, "simd.v4f_mul_strided", 20)==0){
    callname = "tn_simd_v4f_mul_strided";
    calllen = 23;
  }
  if(calllen==20 && strncmp(callname, "simd.v4i_add_strided", 20)==0){
    callname = "tn_simd_v4i_add_strided";
    calllen = 23;
  }
  if(calllen==20 && strncmp(callname, "simd.v4i_mul_strided", 20)==0){
    callname = "tn_simd_v4i_mul_strided";
    calllen = 23;
  }
  if(calllen==13 && strncmp(callname, "simd.v4f_load", 13)==0){
    callname = "tn_simd_v4f_load";
    calllen = 16;
  }
  if(calllen==14 && strncmp(callname, "simd.v4f_store", 14)==0){
    callname = "tn_simd_v4f_store";
    calllen = 17;
  }
  if(calllen==13 && strncmp(callname, "simd.v4i_load", 13)==0){
    callname = "tn_simd_v4i_load";
    calllen = 16;
  }
  if(calllen==14 && strncmp(callname, "simd.v4i_store", 14)==0){
    callname = "tn_simd_v4i_store";
    calllen = 17;
  }
  if(calllen==15 && strncmp(callname, "gpu_has_backend", 15)==0){
    callname = "tn_gpu_has_backend";
    calllen = 18;
  }
  if(calllen==8 && strncmp(callname, "gpu_init", 8)==0){
    callname = "tn_gpu_init";
    calllen = 11;
  }
  if(calllen==12 && strncmp(callname, "gpu_shutdown", 12)==0){
    callname = "tn_gpu_shutdown";
    calllen = 15;
  }
  if(calllen==16 && strncmp(callname, "gpu_device_count", 16)==0){
    callname = "tn_gpu_device_count";
    calllen = 19;
  }
  if(calllen==14 && strncmp(callname, "gpu_set_device", 14)==0){
    callname = "tn_gpu_set_device";
    calllen = 17;
  }
  if(calllen==9 && strncmp(callname, "gpu_alloc", 9)==0){
    callname = "tn_gpu_alloc";
    calllen = 12;
  }
  if(calllen==8 && strncmp(callname, "gpu_free", 8)==0){
    callname = "tn_gpu_free";
    calllen = 11;
  }
  if(calllen==20 && strncmp(callname, "gpu_memcpy_to_device", 20)==0){
    callname = "tn_gpu_memcpy_to_device";
    calllen = 23;
  }
  if(calllen==18 && strncmp(callname, "gpu_memcpy_to_host", 18)==0){
    callname = "tn_gpu_memcpy_to_host";
    calllen = 21;
  }
  if(calllen==10 && strncmp(callname, "gpu_memset", 10)==0){
    callname = "tn_gpu_memset";
    calllen = 13;
  }
  if(calllen==13 && strncmp(callname, "gpu_launch_1d", 13)==0){
    callname = "tn_gpu_launch_1d";
    calllen = 16;
  }
  if(calllen==18 && strncmp(callname, "gpu_launch_dxil_1d", 18)==0){
    callname = "tn_gpu_launch_dxil_1d";
    calllen = 21;
  }
  if(calllen==16 && strncmp(callname, "gpu_backend_name", 16)==0){
    callname = "tn_gpu_backend_name";
    calllen = 19;
  }
  if(calllen==19 && strncmp(callname, "gpu_supports_dxil", 19)==0){
    callname = "tn_gpu_supports_dxil";
    calllen = 22;
  }
  if(calllen==15 && strncmp(callname, "npu_has_backend", 15)==0){
    callname = "tn_npu_has_backend";
    calllen = 18;
  }
  if(calllen==8 && strncmp(callname, "npu_init", 8)==0){
    callname = "tn_npu_init";
    calllen = 11;
  }
  if(calllen==12 && strncmp(callname, "npu_shutdown", 12)==0){
    callname = "tn_npu_shutdown";
    calllen = 15;
  }
  if(calllen==16 && strncmp(callname, "npu_device_count", 16)==0){
    callname = "tn_npu_device_count";
    calllen = 19;
  }
  if(calllen==14 && strncmp(callname, "npu_set_device", 14)==0){
    callname = "tn_npu_set_device";
    calllen = 17;
  }
  if(calllen==9 && strncmp(callname, "npu_alloc", 9)==0){
    callname = "tn_npu_alloc";
    calllen = 12;
  }
  if(calllen==8 && strncmp(callname, "npu_free", 8)==0){
    callname = "tn_npu_free";
    calllen = 11;
  }
  if(calllen==20 && strncmp(callname, "npu_memcpy_to_device", 20)==0){
    callname = "tn_npu_memcpy_to_device";
    calllen = 23;
  }
  if(calllen==18 && strncmp(callname, "npu_memcpy_to_host", 18)==0){
    callname = "tn_npu_memcpy_to_host";
    calllen = 21;
  }
  if(calllen==10 && strncmp(callname, "npu_memset", 10)==0){
    callname = "tn_npu_memset";
    calllen = 13;
  }
  if(calllen==13 && strncmp(callname, "npu_launch_1d", 13)==0){
    callname = "tn_npu_launch_1d";
    calllen = 16;
  }
  if(calllen==18 && strncmp(callname, "npu_launch_dxil_1d", 18)==0){
    callname = "tn_npu_launch_dxil_1d";
    calllen = 21;
  }
  if(calllen==16 && strncmp(callname, "npu_backend_name", 16)==0){
    callname = "tn_npu_backend_name";
    calllen = 19;
  }
  if(calllen==19 && strncmp(callname, "npu_supports_dxil", 19)==0){
    callname = "tn_npu_supports_dxil";
    calllen = 22;
  }
  if(calllen==14 && strncmp(callname, "npu_model_load", 14)==0){
    callname = "tn_npu_model_load";
    calllen = 17;
  }
  if(calllen==14 && strncmp(callname, "npu_model_free", 14)==0){
    callname = "tn_npu_model_free";
    calllen = 17;
  }
  if(calllen==20 && strncmp(callname, "npu_model_run_f64_1d", 20)==0){
    callname = "tn_npu_model_run_f64_1d";
    calllen = 23;
  }
  if(calllen==7 && strncmp(callname, "os_name", 7)==0){
    callname = "tn_os_name";
    calllen = 10;
  }

  mangle_sym(callname, calllen, sym, (int)sizeof(sym));
  fprintf(f, "  call %s\n", sym);

  if(total_al){
    fprintf(f, "  addq $%d, %%rsp\n", total_al);
  }

  // result in rax -> dst reg
  store_reg_rax(f, in->a);
}

// ---- rodata strings (IR v4) ----
static void emit_rodata_strings(FILE* f, IRModule* m){
  if(!m || m->str_n <= 0) return;

  fprintf(f, ".section .rodata\n");
  for(int i=0;i<m->str_n;i++){
    fprintf(f, ".Lstr%d:\n", i);
    fprintf(f, "  .byte ");

    // bytes includes trailing 0, len excludes it
    int total = m->strs[i].len + 1;
    for(int k=0;k<total;k++){
      unsigned char c = (unsigned char)m->strs[i].bytes[k];
      if(k) fprintf(f, ",");
      fprintf(f, "%u", (unsigned)c);
    }
    fprintf(f, "\n");
  }

  fprintf(f, ".text\n");
}

static void emit_globals(FILE* f, IRModule* m){
  if(!m || m->global_n<=0) return;
  for(int i=0;i<m->global_n;i++){
    IRGlobal* g = &m->globals[i];
    char sym[512];
    mangle_sym(g->name, g->len, sym, (int)sizeof(sym));
    if(g->is_extern){
      fprintf(f, ".extern %s\n", sym);
      continue;
    }
    if(g->has_init){
      fprintf(f, ".data\n");
      if(!g->is_static) fprintf(f, ".globl %s\n", sym);
      fprintf(f, "%s:\n", sym);
      if(g->size==8){
        if(g->init_fbits){
          fprintf(f, "  .quad %lld\n", g->init_fbits);
        } else {
          fprintf(f, "  .quad %lld\n", g->init_int);
        }
      } else if(g->size==1){
        fprintf(f, "  .byte %lld\n", g->init_int & 0xFF);
      } else {
        fprintf(f, "  .zero %d\n", g->size);
      }
      fprintf(f, ".text\n");
    } else {
      fprintf(f, ".bss\n");
      if(!g->is_static) fprintf(f, ".globl %s\n", sym);
      fprintf(f, "%s:\n", sym);
      fprintf(f, "  .zero %d\n", g->size);
      fprintf(f, ".text\n");
    }
  }
}

void ir_compile_to_gas(IRModule* m, const char* out_s){
  FILE* f = fopen(out_s, "wb");
  if(!f) die("cannot open output .s");

  emit_rodata_strings(f, m);
  emit_globals(f, m);

  // ---- text ----
  fprintf(f, ".text\n");

  // Declare externs for builtin runtime calls (C functions)
  // (Optional, but nice)
  fprintf(f, ".extern say\n");
  fprintf(f, ".extern say_str\n");
  fprintf(f, ".extern say_f\n");
  fprintf(f, ".extern say_multi\n");
  fprintf(f, ".extern len\n");
  fprintf(f, ".extern tn_strcmp\n");
  fprintf(f, ".extern memcpy\n");
  fprintf(f, ".extern malloc\n");
  fprintf(f, ".extern free\n");
  fprintf(f, ".extern input_i64\n");
  fprintf(f, ".extern input_line\n");
  fprintf(f, ".extern tn_fopen\n");
  fprintf(f, ".extern tn_fclose\n");
  fprintf(f, ".extern tn_fflush\n");
  fprintf(f, ".extern tn_fread\n");
  fprintf(f, ".extern tn_fwrite\n");
  fprintf(f, ".extern tn_fseek\n");
  fprintf(f, ".extern tn_ftell\n");
  fprintf(f, ".extern tn_file_size\n");
  fprintf(f, ".extern tn_read_file\n");
  fprintf(f, ".extern tn_write_file\n");
  fprintf(f, ".extern tn_read_line\n");
  fprintf(f, ".extern tn_read_bytes\n");
  fprintf(f, ".extern tn_write_line\n");
  fprintf(f, ".extern tn_io_err\n");
  fprintf(f, ".extern tn_io_eof\n");
  fprintf(f, ".extern tn_stdin\n");
  fprintf(f, ".extern tn_stdout\n");
  fprintf(f, ".extern tn_stderr\n");
  fprintf(f, ".extern tn_get_cwd\n");
  fprintf(f, ".extern tn_chdir\n");
  fprintf(f, ".extern tn_mkdir\n");
  fprintf(f, ".extern tn_rmdir\n");
  fprintf(f, ".extern tn_remove\n");
  fprintf(f, ".extern tn_rename\n");
  fprintf(f, ".extern tn_path_exists\n");
  fprintf(f, ".extern tn_path_is_dir\n");
  fprintf(f, ".extern tn_path_join\n");
  fprintf(f, ".extern tn_path_sep\n");
  fprintf(f, ".extern tn_path_basename\n");
  fprintf(f, ".extern tn_path_dirname\n");
  fprintf(f, ".extern tn_list_dir\n");
  fprintf(f, ".extern tn_list_dir_recursive\n");
  fprintf(f, ".extern tn_glob\n");
  fprintf(f, ".extern tn_path_normalize\n");
  fprintf(f, ".extern tn_path_normalize_opts\n");
  fprintf(f, ".extern tn_time_now\n");
  fprintf(f, ".extern tn_time_now_ms\n");
  fprintf(f, ".extern tn_time_now_ns\n");
  fprintf(f, ".extern tn_date_now\n");
  fprintf(f, ".extern tn_date_now_utc\n");
  fprintf(f, ".extern tn_sleep_ms\n");
  fprintf(f, ".extern tn_bit_popcnt\n");
  fprintf(f, ".extern tn_bit_clz\n");
  fprintf(f, ".extern tn_bit_ctz\n");
  fprintf(f, ".extern tn_bit_bswap\n");
  fprintf(f, ".extern tn_bit_rotl\n");
  fprintf(f, ".extern tn_bit_rotr\n");
  fprintf(f, ".extern tn_cpu_has_sse2\n");
  fprintf(f, ".extern tn_cpu_has_avx2\n");
  fprintf(f, ".extern tn_cpu_has_fma\n");
  fprintf(f, ".extern tn_cpu_has_neon\n");
  fprintf(f, ".extern tn_async_spawn\n");
  fprintf(f, ".extern tn_async_await\n");
  fprintf(f, ".extern tn_async_workers\n");
  fprintf(f, ".extern tn_async_pending\n");
  fprintf(f, ".extern tn_net_af_inet\n");
  fprintf(f, ".extern tn_net_af_inet6\n");
  fprintf(f, ".extern tn_net_sock_stream\n");
  fprintf(f, ".extern tn_net_sock_dgram\n");
  fprintf(f, ".extern tn_net_ipproto_tcp\n");
  fprintf(f, ".extern tn_net_ipproto_udp\n");
  fprintf(f, ".extern tn_net_init\n");
  fprintf(f, ".extern tn_net_cleanup\n");
  fprintf(f, ".extern tn_net_socket\n");
  fprintf(f, ".extern tn_net_close\n");
  fprintf(f, ".extern tn_net_connect\n");
  fprintf(f, ".extern tn_net_bind\n");
  fprintf(f, ".extern tn_net_listen\n");
  fprintf(f, ".extern tn_net_accept\n");
  fprintf(f, ".extern tn_net_send\n");
  fprintf(f, ".extern tn_net_recv\n");
  fprintf(f, ".extern tn_net_set_blocking\n");
  fprintf(f, ".extern tn_net_set_timeout\n");
  fprintf(f, ".extern tn_net_last_error\n");
  fprintf(f, ".extern tn_net_resolve\n");
  fprintf(f, ".extern tn_tls_connect\n");
  fprintf(f, ".extern tn_tls_connect_ex\n");
  fprintf(f, ".extern tn_tls_send\n");
  fprintf(f, ".extern tn_tls_recv\n");
  fprintf(f, ".extern tn_tls_close\n");
  fprintf(f, ".extern tn_tls_policy_reset\n");
  fprintf(f, ".extern tn_tls_policy_set_min\n");
  fprintf(f, ".extern tn_tls_policy_set_pin_sha256\n");
  fprintf(f, ".extern tn_tls_policy_set_handshake_timeout\n");
  fprintf(f, ".extern tn_tls_policy_get_min\n");
  fprintf(f, ".extern tn_tls_policy_get_handshake_timeout\n");
  fprintf(f, ".extern tn_tls_policy_pin_enabled\n");
  fprintf(f, ".extern tn_tzimage_decode_file\n");
  fprintf(f, ".extern tn_tzimage_free\n");
  fprintf(f, ".extern tn_simd_v4f_add\n");
  fprintf(f, ".extern tn_simd_v4f_sub\n");
  fprintf(f, ".extern tn_simd_v4f_mul\n");
  fprintf(f, ".extern tn_simd_v4f_dot\n");
  fprintf(f, ".extern tn_simd_v4i_add\n");
  fprintf(f, ".extern tn_simd_v4f_load\n");
  fprintf(f, ".extern tn_simd_v4f_store\n");
  fprintf(f, ".extern tn_simd_v4i_load\n");
  fprintf(f, ".extern tn_simd_v4i_store\n");
  fprintf(f, ".extern tn_gpu_has_backend\n");
  fprintf(f, ".extern tn_gpu_init\n");
  fprintf(f, ".extern tn_gpu_shutdown\n");
  fprintf(f, ".extern tn_gpu_device_count\n");
  fprintf(f, ".extern tn_gpu_set_device\n");
  fprintf(f, ".extern tn_gpu_alloc\n");
  fprintf(f, ".extern tn_gpu_free\n");
  fprintf(f, ".extern tn_gpu_memcpy_to_device\n");
  fprintf(f, ".extern tn_gpu_memcpy_to_host\n");
  fprintf(f, ".extern tn_gpu_memset\n");
  fprintf(f, ".extern tn_gpu_launch_1d\n");
  fprintf(f, ".extern tn_gpu_launch_dxil_1d\n");
  fprintf(f, ".extern tn_gpu_backend_name\n");
  fprintf(f, ".extern tn_gpu_supports_dxil\n");
  fprintf(f, ".extern tn_npu_has_backend\n");
  fprintf(f, ".extern tn_npu_init\n");
  fprintf(f, ".extern tn_npu_shutdown\n");
  fprintf(f, ".extern tn_npu_device_count\n");
  fprintf(f, ".extern tn_npu_set_device\n");
  fprintf(f, ".extern tn_npu_alloc\n");
  fprintf(f, ".extern tn_npu_free\n");
  fprintf(f, ".extern tn_npu_memcpy_to_device\n");
  fprintf(f, ".extern tn_npu_memcpy_to_host\n");
  fprintf(f, ".extern tn_npu_memset\n");
  fprintf(f, ".extern tn_npu_launch_1d\n");
  fprintf(f, ".extern tn_npu_launch_dxil_1d\n");
  fprintf(f, ".extern tn_npu_backend_name\n");
  fprintf(f, ".extern tn_npu_supports_dxil\n");
  fprintf(f, ".extern tn_npu_model_load\n");
  fprintf(f, ".extern tn_npu_model_free\n");
  fprintf(f, ".extern tn_npu_model_run_f64_1d\n");
  fprintf(f, ".extern args_count\n");
  fprintf(f, ".extern arg\n");
  fprintf(f, ".extern proc_run\n");
  fprintf(f, ".extern proc_out\n");
  fprintf(f, ".extern tn_http_download\n");
  fprintf(f, ".extern tn_os_name\n");

  for(int fi=0; fi<m->fn_n; fi++){
    IRFunc* fn = &m->fns[fi];

    // symbol name
    char sym[512];
    mangle_sym(fn->name, fn->len, sym, (int)sizeof(sym));

    // Export main
    if(fn->name && fn->len==4 && strncmp(fn->name,"main",4)==0){
      fprintf(f, ".globl main\n");
      fprintf(f, "main:\n");
    } else {
      fprintf(f, ".globl %s\n", sym);
      fprintf(f, "%s:\n", sym);
    }

    // ---- pre-scan allocas to build stack layout ----
    int regs = (fn->max_reg>=0 ? (fn->max_reg+1) : 0);
    int regslots = regs * 8;

    AllocaMap amap; memset(&amap,0,sizeof(amap));

    int used = regslots; // bytes below rbp already used by reg slots
    for(int i=0;i<fn->n;i++){
      IRIns* in = &fn->ins[i];
      if(in->op==I_ALLOCA){
        int al = in->align ? in->align : 8;
        used = align_up_i(used, al);
        used += in->size;
        // pointer should be rbp - used
        amap_add(&amap, in->a, used);
      }
    }

    // Reserve Win64 shadow space (32) in the frame once
    int frame = align_up_i(used, 16);

    // ---- prologue ----
    fprintf(f, "  pushq %%rbp\n");
    fprintf(f, "  movq %%rsp, %%rbp\n");
    if(frame>0){
      fprintf(f, "  subq $%d, %%rsp\n", frame);
    }

    // ---- params: move Win64 ABI args into virtual regs r0,r2,r4,... ----
    {
      int pcount = fn->param_count;
      int r0 = 0;
      int r1 = 2;
      int r2 = 4;
      int r3 = 6;
      if(pcount > 0 && regs > r0) fprintf(f, "  movq %%rcx, -%d(%%rbp)\n", reg_off(r0));
      if(pcount > 1 && regs > r1) fprintf(f, "  movq %%rdx, -%d(%%rbp)\n", reg_off(r1));
      if(pcount > 2 && regs > r2) fprintf(f, "  movq %%r8, -%d(%%rbp)\n", reg_off(r2));
      if(pcount > 3 && regs > r3) fprintf(f, "  movq %%r9, -%d(%%rbp)\n", reg_off(r3));
      for(int i=4;i<pcount;i++){
        int vreg = i * 2;
        if(vreg >= regs) break;
        int off = 48 + 8*(i-4);
        fprintf(f, "  movq %d(%%rbp), %%rax\n", off);
        store_reg_rax(f, vreg);
      }
    }

    // ---- body ----
    for(int i=0;i<fn->n;i++){
      IRIns* in = &fn->ins[i];

      switch(in->op){
        case I_LABEL: {
          char lab[512];
          func_label(lab, (int)sizeof(lab), fn->name, fn->len, in->a);
          fprintf(f, "%s:\n", lab);
          break;
        }

        case I_ICONST:
          fprintf(f, "  movq $%lld, %%rax\n", in->imm);
          store_reg_rax(f, in->a);
          break;

        case I_FCONST:
          fprintf(f, "  movabsq $%lld, %%rax\n", in->immf);
          store_reg_rax(f, in->a);
          break;

        case I_ADDRSYM: {
          char sym_local[512];
          mangle_sym(in->name, in->nlen, sym_local, (int)sizeof(sym_local));
          fprintf(f, "  leaq %s(%%rip), %%rax\n", sym_local);
          store_reg_rax(f, in->a);
          break;
        }

        case I_SCONST:
          // rA = &.LS<sid>
          fprintf(f, "  leaq .Lstr%d(%%rip), %%rax\n", in->sid);
          store_reg_rax(f, in->a);
          break;

        case I_MOV:
          load_reg_rax(f, in->b);
          store_reg_rax(f, in->a);
          break;

        case I_BIN:
          // dst = b (op) c
          load_reg_rax(f, in->b);
          load_reg_rcx(f, in->c);
          emit_bin(f, in->binop);
          store_reg_rax(f, in->a);
          break;

        case I_CMP:
          // dst = (b cmp c) ? 1:0
          load_reg_rax(f, in->b);
          load_reg_rcx(f, in->c);
          fprintf(f, "  cmpq %%rcx, %%rax\n");   // compare a vs b
          fprintf(f, "  %s %%al\n", setcc_for(in->cmpop));
          store_reg_al(f, in->a);
          break;

        case I_FBIN:
          load_reg_xmm0(f, in->b);
          load_reg_xmm1(f, in->c);
          switch(in->fop){
            case F_ADD: fprintf(f, "  addsd %%xmm1, %%xmm0\n"); break;
            case F_SUB: fprintf(f, "  subsd %%xmm1, %%xmm0\n"); break;
            case F_MUL: fprintf(f, "  mulsd %%xmm1, %%xmm0\n"); break;
            case F_DIV: fprintf(f, "  divsd %%xmm1, %%xmm0\n"); break;
            default: break;
          }
          store_reg_xmm0(f, in->a);
          break;

        case I_FCMP:
          load_reg_xmm0(f, in->b);
          load_reg_xmm1(f, in->c);
          fprintf(f, "  ucomisd %%xmm1, %%xmm0\n");
          fprintf(f, "  %s %%al\n", setcc_for(in->cmpop));
          store_reg_al(f, in->a);
          break;

        case I_I2F:
          load_reg_rax(f, in->b);
          fprintf(f, "  cvtsi2sdq %%rax, %%xmm0\n");
          store_reg_xmm0(f, in->a);
          break;

        case I_F2I:
          load_reg_xmm0(f, in->b);
          fprintf(f, "  cvttsd2siq %%xmm0, %%rax\n");
          store_reg_rax(f, in->a);
          break;

        case I_JMP: {
          char lab[512];
          func_label(lab, (int)sizeof(lab), fn->name, fn->len, in->a);
          fprintf(f, "  jmp %s\n", lab);
          break;
        }

        case I_JZ: {
          // if rA == 0 jump Lb
          load_reg_rax(f, in->a);
          fprintf(f, "  testq %%rax, %%rax\n");
          char lab[512];
          func_label(lab, (int)sizeof(lab), fn->name, fn->len, in->b);
          fprintf(f, "  je %s\n", lab);
          break;
        }

        case I_CALL:
          emit_call_win64(f, in);
          break;

        case I_CALLPTR: {
          // same as direct call, but target in reg in->b
          int total_al = emit_call_win64_prep(f, in);
          load_reg_rax(f, in->b);
          fprintf(f, "  call *%%rax\n");
          if(total_al){
            fprintf(f, "  addq $%d, %%rsp\n", total_al);
          }
          store_reg_rax(f, in->a);
          break;
        }

        case I_RET:
          load_reg_rax(f, in->a);
          // epilogue
          fprintf(f, "  movq %%rbp, %%rsp\n");
          fprintf(f, "  popq %%rbp\n");
          fprintf(f, "  ret\n");
          break;

        case I_ALLOCA: {
          int off = amap_find(&amap, in->a);
          if(off<0) die("alloca map missing");
          fprintf(f, "  leaq -%d(%%rbp), %%rax\n", off);
          store_reg_rax(f, in->a);
          break;
        }

        case I_GEP: {
          // dst = base + imm
          load_reg_rax(f, in->b);
          if(in->imm != 0){
            fprintf(f, "  addq $%lld, %%rax\n", in->imm);
          }
          store_reg_rax(f, in->a);
          break;
        }

        case I_LOAD: {
          // load dst = [addr] size
          load_addr_rax(f, in->b);
          if(in->size==1){
          fprintf(f, in->is_unsigned ? "  movzbl (%%rax), %%eax\n" : "  movsbq (%%rax), %%rax\n");
          store_reg_rax(f, in->a);
          } else if(in->size==2){
          fprintf(f, in->is_unsigned ? "  movzwl (%%rax), %%eax\n" : "  movswq (%%rax), %%rax\n");
          store_reg_rax(f, in->a);
          } else if(in->size==4){
          fprintf(f, in->is_unsigned ? "  movl (%%rax), %%eax\n" : "  movslq (%%rax), %%rax\n");
          store_reg_rax(f, in->a);
          } else if(in->size==8){
          fprintf(f, "  movq (%%rax), %%rax\n");
          store_reg_rax(f, in->a);
          } else {
          die("LOAD: unsupported size");
          }
          break;
        }

        case I_STORE: {
          // store [addr] = val size
          load_addr_rax(f, in->a);      // addr in rax
          // load val into rcx
          fprintf(f, "  movq -%d(%%rbp), %%rcx\n", reg_off(in->b));

          if(in->size==1){
            fprintf(f, "  movb %%cl, (%%rax)\n");
          } else if(in->size==2){
            fprintf(f, "  movw %%cx, (%%rax)\n");
          } else if(in->size==4){
            fprintf(f, "  movl %%ecx, (%%rax)\n");
          } else if(in->size==8){
            fprintf(f, "  movq %%rcx, (%%rax)\n");
          } else {
            fprintf(stderr, "STORE: unsupported size %d\n", in->size);
            die("STORE: unsupported size");
          }
          break;
        }

        default:
          // If you add new IR ops later, fail loudly.
          die("ir_codegen_gas: unsupported IR op");
      }
    }

    // If no explicit RET emitted, return 0
    // (should not happen if IR builder always ends with RET)
    fprintf(f, "  xor %%eax, %%eax\n");
    fprintf(f, "  movq %%rbp, %%rsp\n");
    fprintf(f, "  popq %%rbp\n");
    fprintf(f, "  ret\n");

    free(amap.dst_reg);
    free(amap.off_bytes);
  }

  fclose(f);
}

void ir_compile_to_gas_target(IRModule* m, const char* out_s, const char* target){
  if(!target || !target[0]){
    ir_compile_to_gas(m, out_s);
    return;
  }
  if(strcmp(target, "x86_64")==0 || strcmp(target, "amd64")==0 || strcmp(target, "x64")==0 || strcmp(target, "native")==0){
    ir_compile_to_gas(m, out_s);
    return;
  }
  if(strcmp(target, "c")==0 || strcmp(target, "portable")==0 ||
     strcmp(target, "x86")==0 || strcmp(target, "i386")==0 || strcmp(target, "ia32")==0 ||
     strcmp(target, "arm64")==0 || strcmp(target, "aarch64")==0 ||
     strcmp(target, "riscv64")==0 || strcmp(target, "rv64")==0){
    ir_compile_to_c(m, out_s);
    return;
  }
  if(strcmp(target, "cuda")==0){
    ir_compile_to_cuda(m, out_s);
    return;
  }
  if(strcmp(target, "metal")==0 || strcmp(target, "vulkan")==0 ||
     strcmp(target, "directml")==0){
    dief("ir_codegen_gas: target '%s' is declared experimental but has no lowering backend", target);
  }
  die("ir_codegen_gas: target not supported (use x86_64, x86, arm64, riscv64, c, or a GPU backend)");
}
