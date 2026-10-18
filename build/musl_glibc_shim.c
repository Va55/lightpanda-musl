/*
 * musl_glibc_shim.c — the "ZL-V8" drop-in layer.
 *
 * Lightpanda's V8 (zig-v8-fork) and its Rust FFI get compiled against glibc
 * headers by Chromium's/cargo's glibc-tuned build (the -Dtarget=x86_64-linux-musl
 * only reaches the final Zig link). At link against musl, the small finite set
 * of glibc-only symbols below is missing. This shim defines them over musl so a
 * fully pure-musl Lightpanda can link and run on an Alpine box with no glibc.
 *
 * Rationale per symbol:
 *  - *64 large-file funcs: musl's off_t/struct stat are already 64-bit, so each
 *    aliases its plain form.
 *  - __isoc23_* : glibc C23 variants; musl strtoX/sscanf already behave per C23.
 *  - __*_chk    : glibc fortify; here routed to the plain func (drops hardening).
 *  - backtrace* : musl has none; implemented via _Unwind_Backtrace (libunwind,
 *    present in the C++ runtime chain) + dladdr for names.
 *  - __sysconf/__mbrlen: glibc internal aliases -> plain libc.
 *  - __libc_stack_end: glibc symbol V8 reads for the main thread stack start;
 *    set from /proc/self/maps [stack] by a constructor.
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <wchar.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <unwind.h>

/* ---- 64-bit large-file aliases (musl off_t/stat are 64-bit on every arch) ---- */
void *mmap64(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    return mmap(addr, length, prot, flags, fd, offset);
}
int open64(const char *path, int oflag, ...) {
    mode_t mode = 0;
    va_list ap;
    va_start(ap, oflag);
    if (oflag & O_CREAT) mode = va_arg(ap, mode_t);
    va_end(ap);
    return open(path, oflag, mode);
}
int __open64_2(const char *path, int oflag, ...) {
    mode_t mode = 0;
    va_list ap;
    va_start(ap, oflag);
    if (oflag & O_CREAT) mode = va_arg(ap, mode_t);
    va_end(ap);
    return open(path, oflag, mode);
}
FILE *fopen64(const char *path, const char *mode) { return fopen(path, mode); }
int fstat64(int fd, struct stat *st)      { return fstat(fd, st); }
int stat64(const char *path, struct stat *st) { return stat(path, st); }
int lstat64(const char *path, struct stat *st){ return lstat(path, st); }
off_t lseek64(int fd, off_t off, int whence)  { return lseek(fd, off, whence); }
int ftruncate64(int fd, off_t len)        { return ftruncate(fd, len); }
int fseeko64(FILE *f, off_t off, int wh)  { return fseeko(f, off, wh); }
off_t ftello64(FILE *f)                   { return ftello(f); }
struct dirent *readdir64(DIR *d)          { return readdir(d); }
FILE *tmpfile64(void)                     { return tmpfile(); }
int __fxstat64(int ver, int fd, struct stat *st){ (void)ver; return fstat(fd, st); }
int __fxstat(int ver, int fd, struct stat *st)  { (void)ver; return fstat(fd, st); }
int __xstat64(int ver, const char *p, struct stat *st){ (void)ver; return stat(p, st); }
int __lxstat64(int ver, const char *p, struct stat *st){ (void)ver; return lstat(p, st); }

/* ---- glibc C23 variants: musl strtoX/sscanf are already C23-behaved ---- */
long long       __isoc23_strtoll(const char *n, char **e, int b){ return strtoll(n, e, b); }
unsigned long long __isoc23_strtoull(const char *n, char **e, int b){ return strtoull(n, e, b); }
long            __isoc23_strtol(const char *n, char **e, int b) { return strtol(n, e, b); }
unsigned long   __isoc23_strtoul(const char *n, char **e, int b){ return strtoul(n, e, b); }
int             __isoc23_sscanf(const char *s, const char *fmt, ...) {
    int r;
    va_list ap;
    va_start(ap, fmt);
    r = vsscanf(s, fmt, ap);
    va_end(ap);
    return r;
}

/* ---- glibc fortify _chk variants (route plain; drops hardening) ---- */
int __vsnprintf_chk(char *s, size_t maxlen, int flag, size_t slen,
                    const char *fmt, va_list ap) {
    (void)flag; (void)slen;
    return vsnprintf(s, maxlen, fmt, ap);
}
int __snprintf_chk(char *s, size_t maxlen, int flag, size_t slen,
                   const char *fmt, ...) {
    int r;
    va_list ap;
    va_start(ap, fmt);
    (void)flag; (void)slen;
    r = vsnprintf(s, maxlen, fmt, ap);
    va_end(ap);
    return r;
}
int __vprintf_chk(int flag, const char *fmt, va_list ap) {
    (void)flag;
    return vprintf(fmt, ap);
}
int __printf_chk(int flag, const char *fmt, ...) {
    int r;
    va_list ap;
    va_start(ap, fmt);
    (void)flag;
    r = vprintf(fmt, ap);
    va_end(ap);
    return r;
}
int __vfprintf_chk(FILE *f, int flag, const char *fmt, va_list ap) {
    (void)flag;
    return vfprintf(f, fmt, ap);
}
int __fprintf_chk(FILE *f, int flag, const char *fmt, ...) {
    int r;
    va_list ap;
    va_start(ap, fmt);
    (void)flag;
    r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

/* ---- glibc internal libc aliases ---- */
long __sysconf(int name) { return sysconf(name); }
size_t __mbrlen(const char *s, size_t n, mbstate_t *st) { return mbrlen(s, n, st); }

/* ---- __libc_stack_end: V8 reads it for the main-thread stack start ---- */
void *__libc_stack_end = 0;
__attribute__((constructor)) static void __setup_stack_end(void) {
    FILE *f = fopen("/proc/self/maps", "r");
    char line[256];
    unsigned long start = 0, end = 0;
    if (!f) { __libc_stack_end = (void*)0x800000000000ULL; return; }
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%lx-%lx %*s %*s %*s %*s [stack]", &start, &end) == 2)
            { __libc_stack_end = (void*)end; break; }
    }
    fclose(f);
}

/* ---- backtrace family: musl lacks it; use the C++-runtime unwinder ---- */
typedef struct { void **buf; int size; int n; } BtCtx;
static _Unwind_Reason_Code bt_cb(struct _Unwind_Context *ctx, void *arg) {
    BtCtx *c = (BtCtx*)arg;
    if (c->n < c->size) c->buf[c->n] = (void*)_Unwind_GetIP(ctx);
    c->n++;
    return _URC_NO_REASON;
}
int backtrace(void **buffer, int size) {
    BtCtx c = { buffer, size, 0 };
    _Unwind_Backtrace(bt_cb, &c);
    if (c.n > size) c.n = size;
    return c.n;
}
static void bt_addr(char *out, size_t cap, void *addr) {
    Dl_info info;
    if (dladdr(addr, &info) && info.dli_sname)
        snprintf(out, cap, "%s(+0x%zx)[%p]", info.dli_sname,
                 (size_t)((char*)addr - (char*)info.dli_saddr), addr);
    else
        snprintf(out, cap, "[%p]", addr);
}
char **backtrace_symbols(void *const *buffer, int size) {
    char **res = malloc((size_t)size * sizeof(char*));
    if (!res) return NULL;
    for (int i = 0; i < size; i++) {
        char line[256];
        bt_addr(line, sizeof line, buffer[i]);
        res[i] = strdup(line);
    }
    return res;
}
void backtrace_symbols_fd(void *const *buffer, int size, int fd) {
    char line[256];
    for (int i = 0; i < size; i++) {
        bt_addr(line, sizeof line, buffer[i]);
        dprintf(fd, "%s\n", line);
    }
}
