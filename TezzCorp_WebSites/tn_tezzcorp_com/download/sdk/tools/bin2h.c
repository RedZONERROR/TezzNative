#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* Write one file as a C byte array. Returns file size. */
static long write_array(FILE* out, const char* varname, const char* path)
{
    FILE* in = fopen(path, "rb");
    if (!in) { fprintf(stderr, "Cannot open: %s\n", path); exit(1); }
    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    fseek(in, 0, SEEK_SET);
    unsigned char* buf = (unsigned char*)malloc(size);
    if (!buf) { fprintf(stderr, "OOM\n"); exit(1); }
    fread(buf, 1, size, in);
    fclose(in);

    fprintf(out, "/* %s  (%ld bytes) */\n", path, size);
    fprintf(out, "static const unsigned char %s[] = {\n", varname);
    for (long i = 0; i < size; i++) {
        fprintf(out, "0x%02x", buf[i]);
        if (i + 1 < size) fputc(',', out);
        if ((i + 1) % 16 == 0) fputc('\n', out);
    }
    fprintf(out, "\n};\n\n");
    free(buf);
    return size;
}

typedef struct { char varname[64]; char relpath[MAX_PATH]; long size; } Entry;

int main(void)
{
    Entry entries[512];
    int   n = 0;

    FILE* out = fopen("tools/payload.h", "w");
    if (!out) { fprintf(stderr, "Cannot write tools/payload.h\n"); return 1; }

    fprintf(out, "#pragma once\n");
    fprintf(out,
        "typedef struct {\n"
        "    const char*          path;\n"
        "    const unsigned char* data;\n"
        "    unsigned long        size;\n"
        "} payload_file_t;\n\n");

    /* --- tezzc.exe ------------------------------------------------- */
    strcpy(entries[n].varname, "p_tezzc");
    strcpy(entries[n].relpath, "tezzc.exe");
    entries[n].size = write_array(out, "p_tezzc", "bin/Release/tezzc.exe");
    n++;

    /* --- tezz.exe (colorful CLI wrapper) --------------------------- */
    if (GetFileAttributesA("bin/Release/tezz.exe") != INVALID_FILE_ATTRIBUTES) {
        strcpy(entries[n].varname, "p_tezz");
        strcpy(entries[n].relpath, "tezz.exe");
        entries[n].size = write_array(out, "p_tezz", "bin/Release/tezz.exe");
        n++;
    }

    /* --- tezz.mod -------------------------------------------------- */
    if (GetFileAttributesA("tezz.mod") != INVALID_FILE_ATTRIBUTES) {
        strcpy(entries[n].varname, "p_mod");
        strcpy(entries[n].relpath, "tezz.mod");
        entries[n].size = write_array(out, "p_mod", "tezz.mod");
        n++;
    }

    /* --- tezz.lock ------------------------------------------------- */
    if (GetFileAttributesA("tezz.lock") != INVALID_FILE_ATTRIBUTES) {
        strcpy(entries[n].varname, "p_lock");
        strcpy(entries[n].relpath, "tezz.lock");
        entries[n].size = write_array(out, "p_lock", "tezz.lock");
        n++;
    }

    /* --- lib/*.tn -------------------------------------------------- */
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("lib\\*.*", &fd);
    int idx = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            char src[MAX_PATH]; sprintf(src, "lib/%s", fd.cFileName);
            char var[64];      sprintf(var, "p_lib%d", idx);
            char rel[MAX_PATH]; sprintf(rel, "lib\\%s", fd.cFileName);
            entries[n].size = write_array(out, var, src);
            strcpy(entries[n].varname, var);
            strcpy(entries[n].relpath, rel);
            n++; idx++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    /* --- payload table (sizes as literals, not variables) ----------- */
    fprintf(out, "static const payload_file_t g_payload[] = {\n");
    for (int i = 0; i < n; i++) {
        /* Use back-slash escaped path and size as a plain integer literal */
        char esc[MAX_PATH*2]; int ei=0;
        for (int ci=0; entries[i].relpath[ci]; ci++){
            if (entries[i].relpath[ci]=='\\') esc[ei++]='\\';
            esc[ei++] = entries[i].relpath[ci];
        }
        esc[ei]=0;
        fprintf(out, "    { \"%s\", %s, %luUL },\n",
                esc, entries[i].varname, (unsigned long)entries[i].size);
    }
    fprintf(out, "    { 0, 0, 0 }\n};\n");
    fclose(out);
    printf("payload.h generated: %d files embedded.\n", n);
    return 0;
}
