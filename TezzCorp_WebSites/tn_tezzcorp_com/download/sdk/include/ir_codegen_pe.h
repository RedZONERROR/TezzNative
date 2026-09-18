// include/ir_codegen_pe.h
#pragma once
#include "ir.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t runtime_flags;
  uint32_t import_size;
  uint32_t iat_exit;
  uint32_t iat_getstd;
  uint32_t iat_write;
  uint32_t iat_lstrlen;
  uint32_t iat_lstrcmp;
  uint32_t iat_gettime;
  uint32_t iat_valloc;
  uint32_t iat_vfree;
  uint32_t iat_createfile;
  uint32_t iat_closehandle;
  uint32_t iat_deletefile;
  uint32_t iat_movefile;
  uint32_t iat_getfilesize;
  uint32_t iat_readfile;
  uint32_t iat_sleep;
  uint32_t iat_getfileattr;
  uint32_t iat_createdir;
  uint32_t iat_removedir;
  uint32_t iat_getcurdir;
  uint32_t iat_createfilemap;
  uint32_t iat_mapview;
  uint32_t iat_unmapview;
  uint32_t iat_createthread;
  uint32_t iat_waitforsingleobject;
  uint32_t iat_wsprintf;
  uint32_t iat_sprintf;
  uint32_t iat_memcpy;
  uint32_t iat_memmove;
  uint32_t iat_memcmp;
  uint32_t iat_system;
  uint32_t iat_putchar;
  uint32_t iat_getchar;
  uint32_t iat_realloc;
  uint32_t iat_getenv;
  uint32_t iat_fopen;
  uint32_t iat_fclose;
  uint32_t iat_fread;
  uint32_t iat_fwrite;
  uint32_t iat_fseek;
  uint32_t iat_ftell;
  uint32_t iat_fflush;
  uint32_t iat_remove;
  uint32_t iat_rename;
  uint32_t iat_io_err;
  uint32_t iat_io_eof;
  uint32_t iat_read_line;
  uint32_t iat_read_bytes;
  uint32_t iat_write_line;
  uint32_t iat_stdin;
  uint32_t iat_stdout;
  uint32_t iat_stderr;
  uint32_t iat_list_dir;
  uint32_t iat_findfirstfile;
  uint32_t iat_findnextfile;
  uint32_t iat_findclose;
  uint32_t iat_popen;
  uint32_t iat_pclose;
  uint32_t iat_proc_run;
  uint32_t iat_proc_out;
  uint32_t iat_net_af_inet;
  uint32_t iat_net_af_inet6;
  uint32_t iat_net_sock_stream;
  uint32_t iat_net_sock_dgram;
  uint32_t iat_net_ipproto_tcp;
  uint32_t iat_net_ipproto_udp;
  uint32_t iat_net_init;
  uint32_t iat_net_cleanup;
  uint32_t iat_net_last_error;
  uint32_t iat_net_socket;
  uint32_t iat_net_close;
  uint32_t iat_net_connect;
  uint32_t iat_net_bind;
  uint32_t iat_net_listen;
  uint32_t iat_net_accept;
  uint32_t iat_net_send;
  uint32_t iat_net_recv;
  uint32_t iat_net_set_blocking;
  uint32_t iat_net_set_timeout;
  uint32_t iat_net_resolve;
  uint32_t iat_wsa_startup;
  uint32_t iat_wsa_cleanup;
  uint32_t iat_wsa_last_error;
  uint32_t iat_wsa_socket;
  uint32_t iat_wsa_closesocket;
  uint32_t iat_wsa_connect;
  uint32_t iat_wsa_bind;
  uint32_t iat_wsa_listen;
  uint32_t iat_wsa_accept;
  uint32_t iat_wsa_send;
  uint32_t iat_wsa_recv;
  uint32_t iat_wsa_ioctlsocket;
  uint32_t iat_wsa_setsockopt;
} PeImportInfo;

#define PE_IMPORT_RUNTIME_TN_ALLOC 0x1u

typedef struct {
  const char* name;
  int len;
  int is_extern;
  uint32_t rva;
} GlobalInfo;

// Minimal PE/COFF emitter (Windows). Returns 0 on success.
int ir_compile_to_pe_exe(IRModule* m, const char* out_exe, const char* target);
// Verify PE layout/imports. Returns 0 on success.
int ir_verify_pe_exe(const char* path);

// Shared x86_64 lowering (Windows ABI), returns text buffer + entry offset.
int ir_x64_codegen(IRModule* m, uint32_t text_rva,
                   const uint32_t* str_rva, int str_n,
                   uint32_t fmt_rva, uint32_t fmtf_rva, uint32_t nl_rva, uint32_t sp_rva,
                   const PeImportInfo* imp,
                   GlobalInfo* ginfo, int gcount,
                   unsigned char** out_text, uint32_t* out_text_size,
                   size_t* out_entry_off);
