/* kbfbtrace.so: LD_PRELOAD logger for what a Mali kbase blob does with
 * /dev/mali0 and /dev/fb0 (and any dma-buf it touches). Black-box only: it
 * watches libc calls at the kernel interface, decoded with the GPL kbase
 * r20p0 uapi (UK 11.17) and the Linux fbdev uapi. Nothing in the blob is read.
 *
 * KBFB_LOG=path   log file (default stderr)
 * KBFB_ATOMS=0    don't list each atom of a JOB_SUBMIT
 *
 * Build: aarch64-linux-gnu-gcc -O2 -shared -fPIC -o kbfbtrace.so kbfbtrace.c -ldl -lpthread
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <linux/fb.h>

#define MAXFD 4096
enum { K_NONE = 0, K_MALI, K_FB, K_DMABUF, K_OTHERDEV };
static unsigned char kind[MAXFD];
static int logfd = 2, list_atoms = 1;
static pthread_mutex_t lk = PTHREAD_MUTEX_INITIALIZER;

static int (*r_open)(const char *, int, ...);
static int (*r_open64)(const char *, int, ...);
static int (*r_openat)(int, const char *, int, ...);
static int (*r_close)(int);
static int (*r_ioctl)(int, unsigned long, ...);
static void *(*r_mmap)(void *, size_t, int, int, int, off_t);
static int (*r_munmap)(void *, size_t);
static ssize_t (*r_read)(int, void *, size_t);
static int (*r_dup)(int);
static int (*r_dup2)(int, int);
static int (*r_dup3)(int, int, int);

static double now_ms(void)
{
   struct timespec t;
   clock_gettime(CLOCK_MONOTONIC, &t);
   return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static void L(const char *fmt, ...)
{
   char b[1024];
   int n = snprintf(b, sizeof b, "%.3f t%ld ", now_ms(), (long)syscall(SYS_gettid));
   va_list ap;
   va_start(ap, fmt);
   n += vsnprintf(b + n, sizeof b - n, fmt, ap);
   va_end(ap);
   if (n > (int)sizeof b - 2)
      n = sizeof b - 2;
   b[n++] = '\n';
   pthread_mutex_lock(&lk);
   ssize_t w = write(logfd, b, n);
   (void)w;
   pthread_mutex_unlock(&lk);
}

__attribute__((constructor)) static void init(void)
{
   r_open = dlsym(RTLD_NEXT, "open");
   r_open64 = dlsym(RTLD_NEXT, "open64");
   r_openat = dlsym(RTLD_NEXT, "openat");
   r_close = dlsym(RTLD_NEXT, "close");
   r_ioctl = dlsym(RTLD_NEXT, "ioctl");
   r_mmap = dlsym(RTLD_NEXT, "mmap");
   r_munmap = dlsym(RTLD_NEXT, "munmap");
   r_read = dlsym(RTLD_NEXT, "read");
   r_dup = dlsym(RTLD_NEXT, "dup");
   r_dup2 = dlsym(RTLD_NEXT, "dup2");
   r_dup3 = dlsym(RTLD_NEXT, "dup3");
   const char *p = getenv("KBFB_LOG");
   if (p && *p) {
      int fd = r_open(p, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
      if (fd >= 0)
         logfd = fd;
   }
   p = getenv("KBFB_ATOMS");
   if (p && *p == '0')
      list_atoms = 0;
   L("kbfbtrace up, pid %d", getpid());
}

static int classify_path(const char *path)
{
   if (!path)
      return K_NONE;
   if (!strncmp(path, "/dev/mali", 9))
      return K_MALI;
   if (!strncmp(path, "/dev/fb", 7) || !strncmp(path, "/dev/graphics/fb", 16))
      return K_FB;
   if (!strncmp(path, "/dev/", 5))
      return K_OTHERDEV;
   return K_NONE;
}

static int classify_fd(int fd)
{
   if (fd < 0 || fd >= MAXFD)
      return K_NONE;
   if (kind[fd])
      return kind[fd];
   char lp[64], tgt[256];
   snprintf(lp, sizeof lp, "/proc/self/fd/%d", fd);
   ssize_t n = readlink(lp, tgt, sizeof tgt - 1);
   if (n <= 0)
      return K_NONE;
   tgt[n] = 0;
   if (strstr(tgt, "dmabuf"))
      return kind[fd] = K_DMABUF;
   return classify_path(tgt);
}

static void note_open(const char *path, int fd)
{
   int k = classify_path(path);
   if (k && fd >= 0 && fd < MAXFD) {
      kind[fd] = k;
      L("open %s -> fd %d", path, fd);
   }
}

int open(const char *path, int flags, ...)
{
   mode_t m = 0;
   if (flags & O_CREAT) {
      va_list ap;
      va_start(ap, flags);
      m = va_arg(ap, mode_t);
      va_end(ap);
   }
   int fd = r_open(path, flags, m);
   note_open(path, fd);
   return fd;
}

int open64(const char *path, int flags, ...)
{
   mode_t m = 0;
   if (flags & O_CREAT) {
      va_list ap;
      va_start(ap, flags);
      m = va_arg(ap, mode_t);
      va_end(ap);
   }
   int fd = (r_open64 ? r_open64 : r_open)(path, flags, m);
   note_open(path, fd);
   return fd;
}

int openat(int dirfd, const char *path, int flags, ...)
{
   mode_t m = 0;
   if (flags & O_CREAT) {
      va_list ap;
      va_start(ap, flags);
      m = va_arg(ap, mode_t);
      va_end(ap);
   }
   int fd = r_openat(dirfd, path, flags, m);
   note_open(path, fd);
   return fd;
}
int openat64(int dirfd, const char *path, int flags, ...) __attribute__((alias("openat")));

int close(int fd)
{
   if (fd >= 0 && fd < MAXFD && kind[fd]) {
      if (kind[fd] != K_OTHERDEV)
         L("close fd %d (kind %d)", fd, kind[fd]);
      kind[fd] = 0;
   }
   return r_close(fd);
}

static void dup_note(int from, int to)
{
   if (to >= 0 && to < MAXFD && from >= 0 && from < MAXFD && kind[from]) {
      kind[to] = kind[from];
      L("dup fd %d -> %d", from, to);
   }
}
int dup(int fd) { int r = r_dup(fd); dup_note(fd, r); return r; }
int dup2(int a, int b) { int r = r_dup2(a, b); dup_note(a, r); return r; }
int dup3(int a, int b, int f) { int r = r_dup3(a, b, f); dup_note(a, r); return r; }

/* ---- kbase r20p0 (UK 11.17) uapi pieces, from the GPL headers ---- */
static const char *kb_name(unsigned nr)
{
   switch (nr) {
   case 0: return "VERSION_CHECK";
   case 1: return "SET_FLAGS";
   case 2: return "JOB_SUBMIT";
   case 3: return "GET_GPUPROPS";
   case 4: return "POST_TERM";
   case 5: return "MEM_ALLOC";
   case 6: return "MEM_QUERY";
   case 7: return "MEM_FREE";
   case 8: return "HWCNT_READER_SETUP";
   case 12: return "DISJOINT_QUERY";
   case 13: return "GET_DDK_VERSION";
   case 14: return "MEM_JIT_INIT";
   case 15: return "MEM_SYNC";
   case 16: return "MEM_FIND_CPU_OFFSET";
   case 17: return "GET_CONTEXT_ID";
   case 18: return "TLSTREAM_ACQUIRE";
   case 19: return "TLSTREAM_FLUSH";
   case 20: return "MEM_COMMIT";
   case 21: return "MEM_ALIAS";
   case 22: return "MEM_IMPORT";
   case 23: return "MEM_FLAGS_CHANGE";
   case 24: return "STREAM_CREATE";
   case 25: return "FENCE_VALIDATE";
   case 27: return "MEM_PROFILE_ADD";
   case 28: return "SOFT_EVENT_UPDATE";
   case 29: return "STICKY_RESOURCE_MAP";
   case 30: return "STICKY_RESOURCE_UNMAP";
   case 31: return "MEM_FIND_GPU_START_AND_OFFSET";
   case 33: return "HWCNT_SET";
   case 34: return "HWCNT_CLEAR";
   case 38: return "MEM_EXEC_INIT";
   case 50: return "GET_CPU_GPU_TIMEINFO";
   default: return "?";
   }
}

struct kb_submit { uint64_t addr; uint32_t nr_atoms, stride; };
union kb_alloc { struct { uint64_t va_pages, commit_pages, extent, flags; } in; struct { uint64_t flags, gpu_va; } out; };
union kb_import { struct { uint64_t flags, phandle; uint32_t type, padding; } in; struct { uint64_t flags, gpu_va, va_pages; } out; };
struct kb_user_buf { uint64_t ptr, length; };
struct kb_sticky { uint64_t count, address; };

static void fdlink(int fd, char *out, size_t n)
{
   char lp[64];
   snprintf(lp, sizeof lp, "/proc/self/fd/%d", fd);
   ssize_t r = readlink(lp, out, n - 1);
   out[r > 0 ? r : 0] = 0;
}

static void log_submit(const struct kb_submit *s)
{
   L("mali JOB_SUBMIT nr %u stride %u", s->nr_atoms, s->stride);
   if (!list_atoms)
      return;
   /* stride 64 (newer kbase): a u64 seq_nr comes first, the rest follows the 48/56-byte layout */
   const uint8_t *a = (const uint8_t *)(uintptr_t)s->addr + (s->stride >= 64 ? 8 : 0);
   for (uint32_t i = 0; i < s->nr_atoms; i++, a += s->stride) {
      uint64_t jc, extres;
      uint16_t nr_extres;
      uint32_t core_req = 0;
      memcpy(&jc, a, 8);
      memcpy(&extres, a + 24, 8);
      memcpy(&nr_extres, a + 32, 2);
      if (s->stride >= 48)
         memcpy(&core_req, a + 44, 4);
      char ex[256] = "";
      int o = 0;
      const uint64_t *e = (const uint64_t *)(uintptr_t)extres;
      for (unsigned k = 0; e && k < nr_extres && k < 6 && !(core_req & 0x200); k++)
         o += snprintf(ex + o, sizeof ex - o, " %#llx", (unsigned long long)e[k]);
      L("  atom %u core_req %#x jc %#llx dep %u/%u %u/%u prio %u slot %u extres %u%s", a[40], core_req,
        (unsigned long long)jc, a[36], a[37], a[38], a[39], a[41], a[43], nr_extres, ex);
   }
}

static void log_fb_ioctl(int fd, unsigned long req, void *arg, int ret, double ms)
{
   switch (req) {
   case FBIOGET_VSCREENINFO:
   case FBIOPUT_VSCREENINFO: {
      struct fb_var_screeninfo *v = arg;
      L("fb%d %s -> %d: %ux%u virt %ux%u off %u,%u bpp %u r%u/%u g%u/%u b%u/%u a%u/%u act %#x",
        fd, req == FBIOGET_VSCREENINFO ? "GET_VSCREENINFO" : "PUT_VSCREENINFO", ret, v->xres, v->yres,
        v->xres_virtual, v->yres_virtual, v->xoffset, v->yoffset, v->bits_per_pixel, v->red.offset,
        v->red.length, v->green.offset, v->green.length, v->blue.offset, v->blue.length,
        v->transp.offset, v->transp.length, v->activate);
      break;
   }
   case FBIOGET_FSCREENINFO: {
      struct fb_fix_screeninfo *f = arg;
      L("fb%d GET_FSCREENINFO -> %d: id '%.16s' smem %#lx len %u line %u type %u visual %u ypan %u",
        fd, ret, f->id, f->smem_start, f->smem_len, f->line_length, f->type, f->visual, f->ypanstep);
      break;
   }
   case FBIOPAN_DISPLAY: {
      struct fb_var_screeninfo *v = arg;
      L("fb%d PAN_DISPLAY yoff %u -> %d (%.3f ms)", fd, v->yoffset, ret, ms);
      break;
   }
   case FBIO_WAITFORVSYNC:
      L("fb%d WAITFORVSYNC -> %d (%.3f ms)", fd, ret, ms);
      break;
   default: {
      /* Unknown, e.g. a vendor dma-buf export: print the number and, if it
       * returned a small int, which fd it maps to. */
      unsigned dir = _IOC_DIR(req), type = _IOC_TYPE(req), nr = _IOC_NR(req), sz = _IOC_SIZE(req);
      char extra[160] = "";
      if (ret >= 0 && arg && sz >= 4 && (dir & _IOC_READ)) {
         int32_t w[4] = {0};
         memcpy(w, arg, sz < 16 ? sz : 16);
         char l0[96] = "";
         if (w[0] > 2 && w[0] < MAXFD) {
            fdlink(w[0], l0, sizeof l0);
         }
         snprintf(extra, sizeof extra, " out %d %d %d %d%s%s", w[0], w[1], w[2], w[3], *l0 ? " fd0->" : "", l0);
      }
      L("fb%d ioctl %#lx (dir %u type %#x '%c' nr %#x size %u) -> %d (%.3f ms)%s", fd, req, dir, type,
        type >= 32 && type < 127 ? type : '?', nr, sz, ret, ms, extra);
   }
   }
}

int ioctl(int fd, unsigned long req, ...)
{
   va_list ap;
   va_start(ap, req);
   void *arg = va_arg(ap, void *);
   va_end(ap);

   int k = classify_fd(fd);
   if (!k)
      return r_ioctl(fd, req, arg);

   /* Inputs that the call overwrites. */
   union kb_import imp_in;
   union kb_alloc alloc_in;
   unsigned nr = _IOC_NR(req);
   if (k == K_MALI && nr == 22 && arg)
      memcpy(&imp_in, arg, sizeof imp_in);
   if (k == K_MALI && nr == 5 && arg)
      memcpy(&alloc_in, arg, sizeof alloc_in);
   if (k == K_MALI && nr == 2 && arg)
      log_submit(arg);

   double t0 = now_ms();
   int ret = r_ioctl(fd, req, arg);
   int e = errno;
   double ms = now_ms() - t0;

   if (k == K_MALI) {
      if (nr == 22) {
         char what[200] = "";
         if (imp_in.in.type == 2) { /* UMM: phandle points at an int fd */
            int dfd = *(int *)(uintptr_t)imp_in.in.phandle;
            char l[128];
            fdlink(dfd, l, sizeof l);
            snprintf(what, sizeof what, "UMM fd %d (%s)", dfd, l);
         } else if (imp_in.in.type == 3) {
            struct kb_user_buf *u = (void *)(uintptr_t)imp_in.in.phandle;
            snprintf(what, sizeof what, "USER_BUFFER ptr %#llx len %llu", (unsigned long long)u->ptr,
                     (unsigned long long)u->length);
         } else {
            snprintf(what, sizeof what, "type %u", imp_in.in.type);
         }
         union kb_import *o = arg;
         L("mali MEM_IMPORT %s flags %#llx -> %d gpu_va %#llx pages %llu", what,
           (unsigned long long)imp_in.in.flags, ret, (unsigned long long)o->out.gpu_va,
           (unsigned long long)o->out.va_pages);
      } else if (nr == 5) {
         union kb_alloc *o = arg;
         L("mali MEM_ALLOC va %llu commit %llu extent %llu flags %#llx -> %d gpu_va %#llx",
           (unsigned long long)alloc_in.in.va_pages, (unsigned long long)alloc_in.in.commit_pages,
           (unsigned long long)alloc_in.in.extent, (unsigned long long)alloc_in.in.flags, ret,
           (unsigned long long)o->out.gpu_va);
      } else if (nr == 29 || nr == 30) {
         struct kb_sticky *s = arg;
         const uint64_t *ad = (const uint64_t *)(uintptr_t)s->address;
         L("mali %s count %llu first %#llx -> %d", kb_name(nr), (unsigned long long)s->count,
           (unsigned long long)(ad && s->count ? ad[0] : 0), ret);
      } else if (nr != 2) {
         L("mali %s (nr %u size %u) -> %d%s (%.3f ms)", kb_name(nr), nr, _IOC_SIZE(req), ret,
           ret < 0 ? strerror(e) : "", ms);
      } else if (ret < 0) {
         L("mali JOB_SUBMIT failed: %s", strerror(e));
      }
   } else if (k == K_FB) {
      log_fb_ioctl(fd, req, arg, ret, ms);
   } else {
      char l[128];
      fdlink(fd, l, sizeof l);
      unsigned type = _IOC_TYPE(req);
      L("ioctl fd %d (%s) %#lx type '%c' nr %#x size %u -> %d (%.3f ms)", fd, l, req,
        type >= 32 && type < 127 ? type : '?', _IOC_NR(req), _IOC_SIZE(req), ret, ms);
   }
   errno = e;
   return ret;
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
   void *r = r_mmap(addr, len, prot, flags, fd, off);
   int k = fd >= 0 ? classify_fd(fd) : 0;
   if (k && k != K_OTHERDEV)
      L("mmap fd %d (kind %d) len %zu prot %d flags %#x off %#llx -> %p", fd, k, len, prot, flags,
        (unsigned long long)off, r);
   return r;
}
void *mmap64(void *, size_t, int, int, int, off_t) __attribute__((alias("mmap")));

ssize_t read(int fd, void *buf, size_t n)
{
   ssize_t r = r_read(fd, buf, n);
   if (fd >= 0 && fd < MAXFD && kind[fd] == K_MALI) {
      /* base_jd_event_v2: u32 code, u8 atom, pad, u64 udata[2] = 24 bytes */
      const uint8_t *p = buf;
      for (ssize_t o = 0; r > 0 && o + 24 <= r; o += 24) {
         uint32_t code;
         memcpy(&code, p + o, 4);
         L("mali event code %#x atom %u", code, p[o + 4]);
      }
      if (r <= 0)
         L("mali read -> %zd", r);
   }
   return r;
}
