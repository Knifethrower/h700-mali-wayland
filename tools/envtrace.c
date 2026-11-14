/* envtrace: black-box log of the settings a library asks libc for. Each distinct one is logged once:
 * - environment variables (getenv, secure_getenv);
 * - files it opens or probes (open, open64, openat, fopen, fopen64, access, stat-like calls are not hooked).
 * Only calls made from a library whose name contains ENVTRACE_FILTER (default "mali") are logged;
 * ENVTRACE_ALL=1 logs every caller. Lines go to stderr as "[envtrace] <kind> <name> <- <caller lib>".
 * getenv is answered from environ directly, so nothing here can recurse into libc's getenv.
 *
 * Build: aarch64-linux-gnu-gcc -O2 -shared -fPIC -o envtrace.so envtrace.c -ldl
 * Use:   LD_PRELOAD=envtrace.so <program>
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

extern char **environ;
static __thread int busy;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static char seen[512][160];
static int nseen;

static const char *caller_lib(void *ra)
{
   Dl_info i;
   if (!ra || !dladdr(ra, &i) || !i.dli_fname)
      return "?";
   const char *s = strrchr(i.dli_fname, '/');
   return s ? s + 1 : i.dli_fname;
}

static void note(const char *kind, const char *what, void *ra)
{
   if (busy || !what)
      return;
   busy = 1;
   const char *lib = caller_lib(ra);
   const char *all = NULL, *filt = NULL;
   for (char **e = environ; e && *e; e++) {
      if (!strncmp(*e, "ENVTRACE_ALL=", 13))
         all = *e + 13;
      else if (!strncmp(*e, "ENVTRACE_FILTER=", 16))
         filt = *e + 16;
   }
   if ((all && *all == '1') || strstr(lib, filt ? filt : "mali")) {
      char key[160];
      snprintf(key, sizeof key, "%s %s %s", kind, what, lib);
      pthread_mutex_lock(&mu);
      int dup = 0;
      for (int i = 0; i < nseen && !dup; i++)
         dup = !strcmp(seen[i], key);
      if (!dup && nseen < 512)
         strcpy(seen[nseen++], key);
      pthread_mutex_unlock(&mu);
      if (!dup) {
         char buf[700];
         int n = snprintf(buf, sizeof buf, "[envtrace] %s %s <- %s\n", kind, what, lib);
         syscall(SYS_write, 2, buf, (size_t)n);
      }
   }
   busy = 0;
}

static char *lookup(const char *name)
{
   size_t l = strlen(name);
   for (char **e = environ; e && *e; e++)
      if (!strncmp(*e, name, l) && (*e)[l] == '=')
         return *e + l + 1;
   return NULL;
}

char *getenv(const char *name)
{
   note("getenv", name, __builtin_return_address(0));
   return lookup(name);
}

char *secure_getenv(const char *name)
{
   note("secure_getenv", name, __builtin_return_address(0));
   return lookup(name);
}

static int sys_openat(int d, const char *p, int f, mode_t m) { return (int)syscall(SYS_openat, d, p, f, m); }

int open(const char *p, int f, ...)
{
   mode_t m = 0;
   if (f & (O_CREAT | O_TMPFILE)) {
      va_list ap;
      va_start(ap, f);
      m = va_arg(ap, mode_t);
      va_end(ap);
   }
   note("open", p, __builtin_return_address(0));
   return sys_openat(AT_FDCWD, p, f, m);
}
int open64(const char *p, int f, ...) __attribute__((alias("open")));

int openat(int d, const char *p, int f, ...)
{
   mode_t m = 0;
   if (f & (O_CREAT | O_TMPFILE)) {
      va_list ap;
      va_start(ap, f);
      m = va_arg(ap, mode_t);
      va_end(ap);
   }
   note("openat", p, __builtin_return_address(0));
   return sys_openat(d, p, f, m);
}
int openat64(int d, const char *p, int f, ...) __attribute__((alias("openat")));

FILE *fopen(const char *p, const char *mode)
{
   static FILE *(*real)(const char *, const char *);
   if (!real)
      real = (FILE * (*)(const char *, const char *)) dlsym(RTLD_NEXT, "fopen");
   note("fopen", p, __builtin_return_address(0));
   return real(p, mode);
}
FILE *fopen64(const char *p, const char *mode) __attribute__((alias("fopen")));

int access(const char *p, int mode)
{
   note("access", p, __builtin_return_address(0));
   return (int)syscall(SYS_faccessat, AT_FDCWD, p, mode, 0);
}
