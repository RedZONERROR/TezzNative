#include "lexer.h"
#include "../include/util.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

static int is_ident_start(char c){ return isalpha((unsigned char)c) || c=='_'; }
static int is_ident(char c){ return isalnum((unsigned char)c) || c=='_'; }

/* O(1) keyword dispatch: switch on length then first two chars */
static TokenKind keyword_lookup(const char* s, int len){
  switch(len){
    case 2:
      if(s[0]=='f'&&s[1]=='n') return TK_FN;
      if(s[0]=='i'&&s[1]=='f') return TK_IF;
      if(s[0]=='a'&&s[1]=='s') return TK_AS;
      break;
    case 3:
      if(s[0]=='l'&&s[1]=='e'&&s[2]=='t') return TK_LET;
      if(s[0]=='r'&&s[1]=='e'&&s[2]=='t') return TK_RET;
      if(s[0]=='f'&&s[1]=='o'&&s[2]=='r') return TK_FOR;
      if(s[0]=='s'&&s[1]=='a'&&s[2]=='y') return TK_SAY;
      break;
    case 4:
      if(s[0]=='e'&&s[1]=='l'&&s[2]=='s'&&s[3]=='e') return TK_ELSE;
      if(s[0]=='e'&&s[1]=='n'&&s[2]=='u'&&s[3]=='m') return TK_ENUM;
      if(s[0]=='f'&&s[1]=='r'&&s[2]=='e'&&s[3]=='e') return TK_FREE;
      if(s[0]=='c'&&s[1]=='a'&&s[2]=='s'&&s[3]=='e') return TK_CASE;
      break;
    case 5:
      if(s[0]=='w'&&s[1]=='h'&&s[2]=='i'&&s[3]=='l'&&s[4]=='e') return TK_WHILE;
      if(s[0]=='b'&&s[1]=='r'&&s[2]=='e'&&s[3]=='a'&&s[4]=='k') return TK_BREAK;
      if(s[0]=='c'&&s[1]=='o'&&s[2]=='n'&&s[3]=='s'&&s[4]=='t') return TK_CONST;
      break;
    case 6:
      if(s[0]=='s'&&s[1]=='t'&&s[2]=='r'&&s[3]=='u'&&s[4]=='c'&&s[5]=='t') return TK_STRUCT;
      if(s[0]=='i'&&s[1]=='m'&&s[2]=='p'&&s[3]=='o'&&s[4]=='r'&&s[5]=='t') return TK_IMPORT;
      if(s[0]=='s'&&s[1]=='w'&&s[2]=='i'&&s[3]=='t'&&s[4]=='c'&&s[5]=='h') return TK_SWITCH;
      if(s[0]=='m'&&s[1]=='a'&&s[2]=='l'&&s[3]=='l'&&s[4]=='o'&&s[5]=='c') return TK_MALLOC;
      if(s[0]=='e'&&s[1]=='x'&&s[2]=='t'&&s[3]=='e'&&s[4]=='r'&&s[5]=='n') return TK_EXTERN;
      if(s[0]=='s'&&s[1]=='t'&&s[2]=='a'&&s[3]=='t'&&s[4]=='i'&&s[5]=='c') return TK_STATIC;
      if(s[0]=='u'&&s[1]=='n'&&s[2]=='s'&&s[3]=='a'&&s[4]=='f'&&s[5]=='e') return TK_UNSAFE;
      if(s[0]=='k'&&s[1]=='e'&&s[2]=='r'&&s[3]=='n'&&s[4]=='e'&&s[5]=='l') return TK_KERNEL;
      if(s[0]=='s'&&s[1]=='i'&&s[2]=='z'&&s[3]=='e'&&s[4]=='o'&&s[5]=='f') return TK_SIZEOF;
      break;
    case 7:
      if(s[0]=='s'&&s[1]=='a'&&s[2]=='y'&&s[3]=='_'&&s[4]=='s'&&s[5]=='t'&&s[6]=='r') return TK_SAYSTR;
      if(s[0]=='t'&&s[1]=='y'&&s[2]=='p'&&s[3]=='e'&&s[4]=='d'&&s[5]=='e'&&s[6]=='f') return TK_TYPEDEF;
      if(s[0]=='d'&&s[1]=='e'&&s[2]=='f'&&s[3]=='a'&&s[4]=='u'&&s[5]=='l'&&s[6]=='t') return TK_DEFAULT;
      if(s[0]=='a'&&s[1]=='l'&&s[2]=='i'&&s[3]=='g'&&s[4]=='n'&&s[5]=='o'&&s[6]=='f') return TK_ALIGNOF;
      break;
    case 8:
      if(s[0]=='c'&&s[1]=='o'&&s[2]=='n'&&s[3]=='t'&&s[4]=='i'&&s[5]=='n'&&s[6]=='u'&&s[7]=='e') return TK_CONTINUE;
      break;
    case 11:
      if(s[0]=='f'&&s[1]=='a'&&s[2]=='l'&&s[3]=='l'&&s[4]=='t') return TK_FALLTHROUGH;
      break;
    case 9:
      if(s[0]=='i'&&s[1]=='n'&&s[2]=='p'&&s[3]=='u'&&s[4]=='t'&&s[5]=='_'&&s[6]=='i'&&s[7]=='6'&&s[8]=='4') return TK_INPUT_I64;
      break;
    case 10:
      if(s[0]=='i'&&s[1]=='n'&&s[2]=='p'&&s[3]=='u'&&s[4]=='t'&&s[5]=='_'&&s[6]=='l'&&s[7]=='i'&&s[8]=='n'&&s[9]=='e') return TK_INPUT_LINE;
      break;
    default:
      break;
  }
  return TK_IDENT;
}

static void bump(Lexer* L){
  char c = L->src[L->pos];
  if (!c) return;
  L->pos++;
  if (c == '\n'){ L->line++; L->col = 1; }
  else L->col++;
}

static void skip_ws_no_nl(Lexer* L) {
  for (;;) {
    char c = L->src[L->pos];
    if (c==' '||c=='\t'||c=='\r'){ bump(L); continue; }

    if (c=='/' && L->src[L->pos+1]=='/') {
      bump(L); bump(L);
      while (L->src[L->pos] && L->src[L->pos] != '\n') bump(L);
      continue;
    }
    if (c=='/' && L->src[L->pos+1]=='*') {
      bump(L); bump(L);
      while (L->src[L->pos] && !(L->src[L->pos]=='*' && L->src[L->pos+1]=='/')) bump(L);
      if (!L->src[L->pos]) die("unterminated block comment");
      bump(L); bump(L);
      continue;
    }
    break;
  }
}

static char* unescape_string(const char* s, int len) {
  char* out = (char*)malloc((size_t)len + 1);
  if (!out) die("out of memory");
  int j=0;
  for (int i=0;i<len;i++){
    char c=s[i];
    if (c=='\\' && i+1<len){
      char n=s[i+1];
      if (n=='n'){ out[j++]='\n'; i++; continue; }
      if (n=='r'){ out[j++]='\r'; i++; continue; }
      if (n=='t'){ out[j++]='\t'; i++; continue; }
      if (n=='0'){ out[j++]=0; i++; continue; }
      if (n=='\\'){ out[j++]='\\'; i++; continue; }
      if (n=='"'){ out[j++]='"'; i++; continue; }
      if (n=='\''){ out[j++]='\''; i++; continue; }
      out[j++]=n; i++; continue;
    }
    out[j++]=c;
  }
  out[j]=0;
  return out;
}

/*
static int tok_text_eq(Token* t, const char* lit) {
  int n = (int)strlen(lit);
  return t->len==n && strncmp(t->start, lit, (size_t)n)==0;
}
*/

void lex_init(Lexer* L, const char* src, const char* path) {
  memset(L, 0, sizeof(*L));
  L->src = src;
  L->path = path ? path : "<mem>";
  L->pos = 0;
  L->line = 1;
  L->col = 1;
  L->at_line_start = 1;
  L->indent_top = 1;
  L->indent_stack[0] = 0;
  L->pending_dedents = 0;
  L->cur.kind = TK_EOF;
  L->cur.str = NULL;
  lex_next(L);
}

void lex_next(Lexer* L) {
  if (L->cur.kind == TK_STRING && L->cur.str) {
    free(L->cur.str);
    L->cur.str = NULL;
  }

  if (L->pending_dedents > 0) {
    L->pending_dedents--;
    L->cur.kind = TK_DEDENT;
    L->cur.start = L->src + L->pos;
    L->cur.len = 0;
    L->cur.line = L->line;
    L->cur.col = L->col;
    return;
  }

  if (L->at_line_start) {
    // compute indentation (ignore blank/comment-only lines)
    for (;;) {
      size_t p = L->pos;
      int col = L->col;
      int indent = 0;

      while (L->src[p]==' ' || L->src[p]=='\t') {
        if (L->src[p]==' ') { indent++; }
        else { indent += 4 - (indent % 4); }
        p++; col++;
      }

      char c0 = L->src[p];
      // blank line: let NEWLINE handle it
      if (c0=='\n' || c0=='\r') {
        L->pos = p;
        L->col = col;
        break;
      }
      // comment-only line: treat as blank
      if (c0=='/' && L->src[p+1]=='/') {
        L->pos = p;
        L->col = col;
        break;
      }
      if (c0=='/' && L->src[p+1]=='*') {
        L->pos = p;
        L->col = col;
        break;
      }

      // non-blank line: apply indentation
      L->pos = p;
      L->col = col;
      L->at_line_start = 0;

      int cur = L->indent_stack[L->indent_top-1];
      if (indent > cur) {
        if (L->indent_top >= (int)(sizeof(L->indent_stack)/sizeof(L->indent_stack[0]))) {
          die("indentation too deep");
        }
        L->indent_stack[L->indent_top++] = indent;
        L->cur.kind = TK_INDENT;
        L->cur.start = L->src + L->pos;
        L->cur.len = 0;
        L->cur.line = L->line;
        L->cur.col = L->col;
        return;
      }
      if (indent < cur) {
        int pops = 0;
        while (L->indent_top > 1 && indent < L->indent_stack[L->indent_top-1]) {
          L->indent_top--;
          pops++;
        }
        if (indent != L->indent_stack[L->indent_top-1]) {
          die("inconsistent indentation");
        }
        if (pops > 0) {
          L->pending_dedents = pops - 1;
          L->cur.kind = TK_DEDENT;
          L->cur.start = L->src + L->pos;
          L->cur.len = 0;
          L->cur.line = L->line;
          L->cur.col = L->col;
          return;
        }
      }
      break;
    }
  }

  skip_ws_no_nl(L);

  const char* s = L->src + L->pos;
  char c = *s;

  L->cur.start = s;
  L->cur.len = 0;
  L->cur.line = L->line;
  L->cur.col = L->col;

  if (!c) {
    if (L->indent_top > 1) {
      L->indent_top--;
      L->cur.kind = TK_DEDENT;
      return;
    }
    L->cur.kind = TK_EOF;
    return;
  }

  if (c=='\n') {
    L->cur.kind = TK_NEWLINE;
    L->cur.len = 1;
    bump(L);
    L->at_line_start = 1;
    return;
  }

  // punctuation
  if (c=='('){ L->cur.kind=TK_LPAREN; L->cur.len=1; bump(L); return; }
  if (c==')'){ L->cur.kind=TK_RPAREN; L->cur.len=1; bump(L); return; }
  if (c=='{'){ L->cur.kind=TK_LBRACE; L->cur.len=1; bump(L); return; }
  if (c=='}'){ L->cur.kind=TK_RBRACE; L->cur.len=1; bump(L); return; }
  if (c=='['){ L->cur.kind=TK_LBRACKET; L->cur.len=1; bump(L); return; }
  if (c==']'){ L->cur.kind=TK_RBRACKET; L->cur.len=1; bump(L); return; }
  if (c==';'){ L->cur.kind=TK_SEMI; L->cur.len=1; bump(L); return; }
  if (c==','){ L->cur.kind=TK_COMMA; L->cur.len=1; bump(L); return; }
  if (c=='?'){ L->cur.kind=TK_QUESTION; L->cur.len=1; bump(L); return; }
  if (c==':'){ L->cur.kind=TK_COLON; L->cur.len=1; bump(L); return; }
  if (c=='.'){ L->cur.kind=TK_DOT; L->cur.len=1; bump(L); return; }
  if (c=='@'){ L->cur.kind=TK_AT; L->cur.len=1; bump(L); return; }

  // 3-char operators
  if (c=='<' && L->src[L->pos+1]=='<' && L->src[L->pos+2]=='='){ L->cur.kind=TK_SHLEQ; L->cur.len=3; bump(L); bump(L); bump(L); return; }
  if (c=='>' && L->src[L->pos+1]=='>' && L->src[L->pos+2]=='='){ L->cur.kind=TK_SHREQ; L->cur.len=3; bump(L); bump(L); bump(L); return; }

  // 2-char operators
  if (c=='+' && L->src[L->pos+1]=='+'){ L->cur.kind=TK_PLUSPLUS; L->cur.len=2; bump(L); bump(L); return; }
  if (c=='-' && L->src[L->pos+1]=='-'){ L->cur.kind=TK_MINUSMINUS; L->cur.len=2; bump(L); bump(L); return; }
  if (c=='&' && L->src[L->pos+1]=='&'){ L->cur.kind=TK_LAND; L->cur.len=2; bump(L); bump(L); return; }
  if (c=='|' && L->src[L->pos+1]=='|'){ L->cur.kind=TK_LOR;  L->cur.len=2; bump(L); bump(L); return; }
  if (c=='<' && L->src[L->pos+1]=='<'){ L->cur.kind=TK_SHL; L->cur.len=2; bump(L); bump(L); return; }
  if (c=='>' && L->src[L->pos+1]=='>'){ L->cur.kind=TK_SHR; L->cur.len=2; bump(L); bump(L); return; }
  if (c=='=' && L->src[L->pos+1]=='='){ L->cur.kind=TK_EQ;  L->cur.len=2; bump(L); bump(L); return; }
  if (c=='!' && L->src[L->pos+1]=='='){ L->cur.kind=TK_NEQ; L->cur.len=2; bump(L); bump(L); return; }
  if (c=='<' && L->src[L->pos+1]=='='){ L->cur.kind=TK_LTE; L->cur.len=2; bump(L); bump(L); return; }
  if (c=='>' && L->src[L->pos+1]=='='){ L->cur.kind=TK_GTE; L->cur.len=2; bump(L); bump(L); return; }

  if (c=='+' && L->src[L->pos+1]=='='){ L->cur.kind=TK_PLUSEQ; L->cur.len=2; bump(L); bump(L); return; }
  if (c=='-' && L->src[L->pos+1]=='='){ L->cur.kind=TK_MINUSEQ; L->cur.len=2; bump(L); bump(L); return; }
  if (c=='-' && L->src[L->pos+1]=='>'){ L->cur.kind=TK_ARROW; L->cur.len=2; bump(L); bump(L); return; }
  if (c=='*' && L->src[L->pos+1]=='='){ L->cur.kind=TK_STAREQ; L->cur.len=2; bump(L); bump(L); return; }
  if (c=='/' && L->src[L->pos+1]=='='){ L->cur.kind=TK_SLASHEQ; L->cur.len=2; bump(L); bump(L); return; }
  if (c=='%' && L->src[L->pos+1]=='='){ L->cur.kind=TK_PERCENTEQ; L->cur.len=2; bump(L); bump(L); return; }

  if (c=='&' && L->src[L->pos+1]=='='){ L->cur.kind=TK_ANDEQ; L->cur.len=2; bump(L); bump(L); return; }
  if (c=='|' && L->src[L->pos+1]=='='){ L->cur.kind=TK_OREQ;  L->cur.len=2; bump(L); bump(L); return; }
  if (c=='^' && L->src[L->pos+1]=='='){ L->cur.kind=TK_XOREQ; L->cur.len=2; bump(L); bump(L); return; }

  // 1-char operators
  if (c=='='){ L->cur.kind=TK_ASSIGN;  L->cur.len=1; bump(L); return; }
  if (c=='+'){ L->cur.kind=TK_PLUS;    L->cur.len=1; bump(L); return; }
  if (c=='-'){ L->cur.kind=TK_MINUS;   L->cur.len=1; bump(L); return; }
  if (c=='*'){ L->cur.kind=TK_STAR;    L->cur.len=1; bump(L); return; }
  if (c=='/'){ L->cur.kind=TK_SLASH;   L->cur.len=1; bump(L); return; }
  if (c=='%'){ L->cur.kind=TK_PERCENT; L->cur.len=1; bump(L); return; }

  if (c=='!'){ L->cur.kind=TK_LNOT;    L->cur.len=1; bump(L); return; }
  if (c=='~'){ L->cur.kind=TK_BNOT;    L->cur.len=1; bump(L); return; }

  if (c=='&'){ L->cur.kind=TK_BAND;    L->cur.len=1; bump(L); return; }
  if (c=='|'){ L->cur.kind=TK_BOR;     L->cur.len=1; bump(L); return; }
  if (c=='^'){ L->cur.kind=TK_BXOR;    L->cur.len=1; bump(L); return; }

  if (c=='<'){ L->cur.kind=TK_LT;      L->cur.len=1; bump(L); return; }
  if (c=='>'){ L->cur.kind=TK_GT;      L->cur.len=1; bump(L); return; }

  // char literal: 'A' -> integer
  if (c == '\'' ) {
    bump(L); /* consume opening ' */
    char ch = L->src[L->pos];
    long long val = 0;
    if (ch == '\\' && L->src[L->pos+1]) {
      char esc = L->src[L->pos+1];
      if(esc=='n')  val='\n';
      else if(esc=='r') val='\r';
      else if(esc=='t') val='\t';
      else if(esc=='0') val=0;
      else if(esc=='\\') val='\\';
      else if(esc=='\'') val='\'';
      else val=esc;
      bump(L); bump(L);
    } else {
      val=(unsigned char)ch;
      bump(L);
    }
    if (L->src[L->pos] != '\'') die("unterminated char literal");
    bump(L); /* consume closing ' */
    L->cur.kind=TK_NUMBER;
    L->cur.num=val;
    return;
  }

  // number / float (hex 0x, binary 0b, underscore separators)
  if (isdigit((unsigned char)c)) {
    size_t p=L->pos;
    long long v=0;
    int is_float=0;

    /* hex literal: 0x... */
    if(L->src[p]=='0' && (L->src[p+1]=='x'||L->src[p+1]=='X')){
      p+=2;
      while(isxdigit((unsigned char)L->src[p])||L->src[p]=='_'){
        if(L->src[p]!='_'){
          unsigned char hc=(unsigned char)L->src[p];
          int d = isdigit(hc)? hc-'0' : (tolower(hc)-'a'+10);
          v = v*16 + d;
        }
        p++;
      }
      L->cur.start=L->src+L->pos;
      L->cur.len=(int)(p-L->pos);
      L->cur.kind=TK_NUMBER;
      L->cur.num=v;
      while(L->pos<p) bump(L);
      return;
    }
    /* binary literal: 0b... */
    if(L->src[p]=='0' && (L->src[p+1]=='b'||L->src[p+1]=='B')){
      p+=2;
      while(L->src[p]=='0'||L->src[p]=='1'||L->src[p]=='_'){
        if(L->src[p]!='_') v = v*2 + (L->src[p]-'0');
        p++;
      }
      L->cur.start=L->src+L->pos;
      L->cur.len=(int)(p-L->pos);
      L->cur.kind=TK_NUMBER;
      L->cur.num=v;
      while(L->pos<p) bump(L);
      return;
    }
    /* decimal (with optional _ separators) */
    while (isdigit((unsigned char)L->src[p])||L->src[p]=='_') {
      if(L->src[p]!='_') v = v*10 + (L->src[p]-'0');
      p++;
    }
    if (L->src[p]=='.' && isdigit((unsigned char)L->src[p+1])) {
      is_float = 1;
      p++;
      while (isdigit((unsigned char)L->src[p])||L->src[p]=='_') p++;
    }
    if (L->src[p]=='e'||L->src[p]=='E') {
      size_t ep = p + 1;
      if (L->src[ep]=='+'||L->src[ep]=='-') ep++;
      if (isdigit((unsigned char)L->src[ep])) {
        is_float = 1;
        p = ep;
        while (isdigit((unsigned char)L->src[p])) p++;
      }
    }
    L->cur.start=L->src+L->pos;
    L->cur.len=(int)(p-L->pos);
    if(is_float){
      char tmp[128];
      int n = L->cur.len < (int)(sizeof(tmp)-1) ? L->cur.len : (int)(sizeof(tmp)-1);
      memcpy(tmp, L->cur.start, (size_t)n);
      tmp[n]=0;
      L->cur.kind=TK_FLOAT;
      L->cur.f=strtod(tmp, NULL);
    } else {
      L->cur.kind=TK_NUMBER;
      L->cur.num=v;
    }
    while (L->pos < p) bump(L);
    return;
  }

  // string
  if (c=='"') {
    size_t p=L->pos+1;
    while (L->src[p] && L->src[p]!='"') {
      if (L->src[p]=='\\' && L->src[p+1]) p+=2;
      else p++;
    }
    if (L->src[p] != '"') die("unterminated string literal");
    int raw_len = (int)(p - (L->pos+1));
    const char* raw = L->src + L->pos + 1;

    L->cur.kind=TK_STRING;
    L->cur.start=L->src+L->pos;
    L->cur.len=(int)(p-L->pos+1);
    L->cur.str=unescape_string(raw, raw_len);

    // move pos past closing "
    bump(L); // opening "
    while (L->src[L->pos] && L->pos < p) bump(L);
    bump(L); // closing "
    return;
  }

  // ident / keywords (O(1) dispatch via keyword_lookup)
  if (is_ident_start(c)) {
    size_t p=L->pos+1;
    while (is_ident(L->src[p])) p++;
    L->cur.kind=TK_IDENT;
    L->cur.start=L->src+L->pos;
    L->cur.len=(int)(p-L->pos);
    /* O(1) keyword lookup replaces 30+ sequential comparisons */
    L->cur.kind = keyword_lookup(L->cur.start, L->cur.len);
    // advance
    while (L->pos < p) bump(L);
    return;
  }
  {
  char msg[128];
  snprintf(msg, sizeof(msg), "unexpected character '%c' (%d)", c, (int)(unsigned char)c);
  die(msg);
  }
}
