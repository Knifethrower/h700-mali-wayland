/* afbcfix: keep an AFBC-assuming Mali blob (Rockchip's g29p1 bifrost-g31) off AFBC on a Mali-G31
 * that was built without AFBC (Allwinner H700/H616).
 *
 * Everything here was found black-box on the RG34XXSP, 2026-11-13 (no blob code was
 * read; README.md has the summary). What the blob does on this GPU without
 * help:
 *  - Writing AFBC is what faults (DATA_INVALID_FAULT); what was written then reads as zeros, and a
 *    faulted job loses its color output too.
 *  - Textures: converted to AFBC by a GPU job at the first upload (2D, cube faces, 2D array layers,
 *    every mip level), and written as AFBC on every pass when they are FBO attachments (color and
 *    depth textures). 3D textures and compressed formats are left alone.
 *  - Renderbuffers (color, depth, stencil): written as AFBC on every pass.
 *  - Window surfaces: the depth/stencil buffer is written as AFBC when a frame is flushed to memory
 *    before the swap (glFinish, glReadPixels, a wait for an unsignaled fence, and a few milliseconds
 *    after a fence was made, by the blob itself, when the swap has not come by then). The frame that
 *    reaches the screen is then black.
 *  - A Wayland client takes an AFBC window buffer when the compositor offers one.
 *
 * What this library does about it:
 *  - Textures: before each call that gives a texture storage, ask EGL for an image of that texture with
 *    a kind that does not fit it (a cube-face image of a 2D texture, a 2D image of a cube or array).
 *    The blob refuses (EGL_BAD_PARAMETER) and makes no image, but from then on never compresses that
 *    texture, with or without storage, for uploads and as a render target. The GL call itself is passed
 *    on unchanged.
 *  - Renderbuffers: after the storage call, make an EGLImage from the renderbuffer and destroy it. That
 *    has the same lasting effect. (The refused-request form does not work for renderbuffers.)
 *  - EXT_multisampled_render_to_texture: its depth renderbuffers can't be kept off AFBC, so the two
 *    calls are turned into their single-sample forms and the extension is hidden from the extension
 *    list. Core ES 3 multisample renderbuffers are fine and left alone.
 *  - Window depth, two modes:
 *    discard (the start mode): the window's depth and stencil are discarded right before glFinish,
 *      glReadPixels on the window, making a fence, a wait for one that has not signaled, eglWaitGL,
 *      eglWaitClient, eglReleaseThread and an eglMakeCurrent that leaves the surface or the context.
 *      That is exact for the usual end-of-frame uses and costs nothing.
 *    keep: when the application goes on drawing with the depth or stencil test into the window after
 *      such a discard, the library switches to this mode for the rest of the process (one frame had a
 *      cleared depth buffer by then). From the next swap on, the window is drawn through a framebuffer
 *      object of ours (an uncompressed color texture and depth/stencil renderbuffer of the window's
 *      size) and copied into the real window right before each swap (a blit on ES 3, a textured quad on
 *      ES 2). Framebuffer 0 of the application is that object, so every flush, readback and fence wait
 *      keeps its true meaning and the depth buffer survives it. Costs one full-window copy per frame.
 *  - Wayland: Arm's modifiers are taken out of the list a compositor passes to its clients.
 *
 * Installed as libEGL.so.1, libGLESv2.so.2 and libGLESv1_CM.so.1 (symlinks to this one file) in front
 * of the blob's own wrappers, so it is found by direct linking, by dlsym on those libraries and by
 * eglGetProcAddress.
 *
 *   AFBCFIX=0            pass everything through
 *   AFBCFIX_MODS=0       (or a file /tmp/afbcfix_nomods) leave the modifier list alone
 *   AFBCFIX_WINDEPTH=0   leave the window's depth alone (no discard, no keep mode)
 *   AFBCFIX_KEEPDEPTH=1  keep mode from the first swap on; =0 never switch to it
 *   AFBCFIX_MSRTT=0      leave EXT_multisampled_render_to_texture alone
 *   AFBCFIX_DEBUG=1      print counters at exit; =2 also write the EGL-level calls to /dev/kmsg, where
 *                        they line up with the kernel's GPU fault lines (=3: every swap, not the first six)
 *
 * Tested on the RG34XXSP (results/rg34xxsp/ and README.md): the probe matrix, gaps and open
 * sets (gbm_probe, gles1_probe), sway + EmulationStation, glmark2, mpv, Torus Trooper and Slope (Unity,
 * GLES 3) in both depth modes.
 *
 * Not covered:
 *  - keep mode on a multisampled window config and on GLES 1 (both stay in discard mode);
 *  - keep mode starts one frame late: the frame in which the need shows up has a cleared depth buffer
 *    after its flush.
 * Not an AFBC matter, so not fixed here: RetroArch's glcore driver draws its menu wrong on this blob
 * with or without this library; its gl driver is right.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef void *EGLDisplay, *EGLContext, *EGLImage, *EGLSurface, *EGLConfig, *EGLSync;
typedef int32_t EGLint;
typedef unsigned int EGLBoolean, EGLenum;
typedef void (*fnptr)(void);

#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_TEXTURE_CUBE_MAP 0x8513
#define GL_TEXTURE_BINDING_CUBE_MAP 0x8514
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#define GL_TEXTURE_BINDING_2D_ARRAY 0x8C1D
#define GL_RENDERBUFFER 0x8D41
#define GL_RENDERBUFFER_BINDING 0x8CA7
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_EXT 0x1801
#define GL_STENCIL_EXT 0x1802
#define GL_DEPTH_BITS 0x0D56
#define GL_STENCIL_BITS 0x0D57
#define GL_DEPTH_TEST 0x0B71
#define GL_STENCIL_TEST 0x0B90
#define GL_DEPTH_BUFFER_BIT 0x0100
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_EXTENSIONS 0x1F03
#define GL_NUM_EXTENSIONS 0x821D
#define GL_ALREADY_SIGNALED 0x911A
#define GL_CONDITION_SATISFIED 0x911C
#define GL_RGB8 0x8051
#define GL_RGBA8 0x8058
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_STENCIL_ATTACHMENT 0x8D20
#define GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE 0x8CD0
#define GL_FRAMEBUFFER_DEFAULT 0x8218
#define GL_COLOR_EXT 0x1800
#define GL_BACK 0x0405
#define GL_READ_BUFFER 0x0C02
#define GL_DRAW_BUFFER0 0x8825
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_SCISSOR_TEST 0x0C11
#define GL_BLEND 0x0BE2
#define GL_CULL_FACE 0x0B44
#define GL_SAMPLE_ALPHA_TO_COVERAGE 0x809E
#define GL_SAMPLE_COVERAGE 0x80A0
#define GL_FLOAT 0x1406
#define GL_TRIANGLE_STRIP 0x0005
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_LINK_STATUS 0x8B82
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_TEXTURE0 0x84C0
#define GL_VIEWPORT 0x0BA2
#define GL_COLOR_WRITEMASK 0x0C23
#define GL_ARRAY_BUFFER 0x8892
#define GL_ARRAY_BUFFER_BINDING 0x8894
#define GL_STATIC_DRAW 0x88E4
#define GL_VERTEX_ATTRIB_ARRAY_ENABLED 0x8622
#define GL_VERTEX_ATTRIB_ARRAY_SIZE 0x8623
#define GL_VERTEX_ATTRIB_ARRAY_STRIDE 0x8624
#define GL_VERTEX_ATTRIB_ARRAY_TYPE 0x8625
#define GL_VERTEX_ATTRIB_ARRAY_NORMALIZED 0x886A
#define GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING 0x889F
#define GL_VERTEX_ATTRIB_ARRAY_POINTER 0x8645
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_DEPTH_SIZE 0x3025
#define EGL_STENCIL_SIZE 0x3026
#define EGL_SAMPLES 0x3031
#define EGL_GL_TEXTURE_2D_KHR 0x30B1
#define EGL_GL_TEXTURE_CUBE_MAP_POSITIVE_X_KHR 0x30B3
#define EGL_GL_RENDERBUFFER_KHR 0x30B9
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_DRAW 0x3059
#define EGL_READ 0x305A
#define EGL_CONFIG_ID 0x3028
#define EGL_ALPHA_SIZE 0x3021
#define EGL_NONE 0x3038
#define EGL_SYNC_FENCE_KHR 0x30F9
#define EGL_SYNC_NATIVE_FENCE_ANDROID 0x3144
#define EGL_CONDITION_SATISFIED_KHR 0x30F6

/* The draw calls: wrapped only to notice depth-tested drawing after a depth discard. */
#define DRAWS(X) \
   X(glDrawArrays, (unsigned a, int b, int c), (a, b, c)) \
   X(glDrawElements, (unsigned a, int b, unsigned c, const void *d), (a, b, c, d)) \
   X(glDrawArraysInstanced, (unsigned a, int b, int c, int d), (a, b, c, d)) \
   X(glDrawElementsInstanced, (unsigned a, int b, unsigned c, const void *d, int e), (a, b, c, d, e)) \
   X(glDrawRangeElements, (unsigned a, unsigned b, unsigned c, int d, unsigned e, const void *f), (a, b, c, d, e, f)) \
   X(glDrawElementsBaseVertex, (unsigned a, int b, unsigned c, const void *d, int e), (a, b, c, d, e)) \
   X(glDrawElementsBaseVertexEXT, (unsigned a, int b, unsigned c, const void *d, int e), (a, b, c, d, e)) \
   X(glDrawElementsBaseVertexOES, (unsigned a, int b, unsigned c, const void *d, int e), (a, b, c, d, e)) \
   X(glDrawRangeElementsBaseVertex, (unsigned a, unsigned b, unsigned c, int d, unsigned e, const void *f, int g), \
     (a, b, c, d, e, f, g)) \
   X(glDrawElementsInstancedBaseVertex, (unsigned a, int b, unsigned c, const void *d, int e, int f), (a, b, c, d, e, f)) \
   X(glDrawArraysIndirect, (unsigned a, const void *b), (a, b)) \
   X(glDrawElementsIndirect, (unsigned a, unsigned b, const void *c), (a, b, c))

/* What keep mode calls (the framebuffer object of the window and the copy into the real window). */
#define KEEPS(X) \
   X(void, glBlitFramebuffer, (int, int, int, int, int, int, int, int, unsigned, unsigned)) \
   X(void, glInvalidateSubFramebuffer, (unsigned, int, const unsigned *, int, int, int, int)) \
   X(void, glReadBuffer, (unsigned)) \
   X(void, glDrawBuffers, (int, const unsigned *)) \
   X(void, glGetFramebufferAttachmentParameteriv, (unsigned, unsigned, unsigned, int *)) \
   X(void, glDeleteFramebuffers, (int, const unsigned *)) \
   X(void, glDeleteTextures, (int, const unsigned *)) \
   X(void, glGenRenderbuffers, (int, unsigned *)) \
   X(void, glDeleteRenderbuffers, (int, const unsigned *)) \
   X(void, glBindRenderbuffer, (unsigned, unsigned)) \
   X(void, glFramebufferRenderbuffer, (unsigned, unsigned, unsigned, unsigned)) \
   X(void, glTexParameteri, (unsigned, unsigned, int)) \
   X(void, glEnable, (unsigned)) \
   X(void, glDisable, (unsigned)) \
   X(void, glViewport, (int, int, int, int)) \
   X(void, glColorMask, (unsigned char, unsigned char, unsigned char, unsigned char)) \
   X(void, glUseProgram, (unsigned)) \
   X(void, glActiveTexture, (unsigned)) \
   X(void, glGenBuffers, (int, unsigned *)) \
   X(void, glDeleteBuffers, (int, const unsigned *)) \
   X(void, glBindBuffer, (unsigned, unsigned)) \
   X(void, glBufferData, (unsigned, intptr_t, const void *, unsigned)) \
   X(void, glGetVertexAttribiv, (unsigned, unsigned, int *)) \
   X(void, glGetVertexAttribPointerv, (unsigned, unsigned, void **)) \
   X(void, glVertexAttribPointer, (unsigned, int, unsigned, unsigned char, int, const void *)) \
   X(void, glEnableVertexAttribArray, (unsigned)) \
   X(void, glDisableVertexAttribArray, (unsigned)) \
   X(unsigned, glCreateShader, (unsigned)) \
   X(void, glShaderSource, (unsigned, int, const char *const *, const int *)) \
   X(void, glCompileShader, (unsigned)) \
   X(void, glDeleteShader, (unsigned)) \
   X(unsigned, glCreateProgram, (void)) \
   X(void, glAttachShader, (unsigned, unsigned)) \
   X(void, glBindAttribLocation, (unsigned, unsigned, const char *)) \
   X(void, glLinkProgram, (unsigned)) \
   X(void, glGetProgramiv, (unsigned, unsigned, int *)) \
   X(void, glDeleteProgram, (unsigned))

static struct {
#define X(t, n, a) t (*n) a;
   KEEPS(X)
#undef X
   int ready; /* everything an ES 2 context needs is there (the first five are ES 3) */
} g;

static struct {
   void (*glTexImage2D)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
   void (*glTexStorage2D)(unsigned, int, unsigned, int, int);
   void (*glCopyTexImage2D)(unsigned, int, unsigned, int, int, int, int, int);
   void (*glTexImage3D)(unsigned, int, int, int, int, int, int, unsigned, unsigned, const void *);
   void (*glTexStorage3D)(unsigned, int, unsigned, int, int, int);
   void (*glRenderbufferStorage)(unsigned, unsigned, int, int);
   void (*glRenderbufferStorageOES)(unsigned, unsigned, int, int);
   void (*glRenderbufferStorageMultisample)(unsigned, int, unsigned, int, int);
   void (*glRenderbufferStorageMultisampleEXT)(unsigned, int, unsigned, int, int);
   void (*glFramebufferTexture2D)(unsigned, unsigned, unsigned, unsigned, int);
   void (*glFramebufferTexture2DMultisampleEXT)(unsigned, unsigned, unsigned, unsigned, int, int);
   void (*glBindFramebuffer)(unsigned, unsigned);
   void (*glBindFramebufferOES)(unsigned, unsigned);
   void (*glGenFramebuffers)(int, unsigned *);
   unsigned (*glCheckFramebufferStatus)(unsigned);
   void (*glGenTextures)(int, unsigned *);
   void (*glBindTexture)(unsigned, unsigned);
   void (*glDiscardFramebufferEXT)(unsigned, int, const unsigned *);
   void (*glInvalidateFramebuffer)(unsigned, int, const unsigned *);
   void (*glFinish)(void);
   void (*glFlush)(void);
   void (*glClear)(unsigned);
   unsigned char (*glIsEnabled)(unsigned);
   void (*glReadPixels)(int, int, int, int, unsigned, unsigned, void *);
   void (*glReadnPixels)(int, int, int, int, unsigned, unsigned, int, void *);
   void (*glGetIntegerv)(unsigned, int *);
   const unsigned char *(*glGetString)(unsigned);
   const unsigned char *(*glGetStringi)(unsigned, unsigned);
   void *(*glFenceSync)(unsigned, unsigned);
   unsigned (*glClientWaitSync)(void *, unsigned, uint64_t);
   void (*glWaitSync)(void *, unsigned, uint64_t);
   EGLDisplay (*eglGetCurrentDisplay)(void);
   EGLContext (*eglGetCurrentContext)(void);
   EGLSurface (*eglGetCurrentSurface)(EGLint);
   EGLBoolean (*eglQueryContext)(EGLDisplay, EGLContext, EGLint, EGLint *);
   EGLBoolean (*eglQuerySurface)(EGLDisplay, EGLSurface, EGLint, EGLint *);
   EGLBoolean (*eglChooseConfig)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);
   EGLBoolean (*eglGetConfigAttrib)(EGLDisplay, EGLConfig, EGLint, EGLint *);
   EGLImage (*eglCreateImageKHR)(EGLDisplay, EGLContext, EGLenum, void *, const EGLint *);
   EGLBoolean (*eglDestroyImageKHR)(EGLDisplay, EGLImage);
   EGLint (*eglGetError)(void);
   EGLBoolean (*eglWaitGL)(void);
   EGLBoolean (*eglWaitClient)(void);
   EGLBoolean (*eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
   EGLBoolean (*eglSwapBuffers)(EGLDisplay, EGLSurface);
   EGLBoolean (*eglReleaseThread)(void);
   EGLBoolean (*eglDestroyContext)(EGLDisplay, EGLContext);
   EGLSync (*eglCreateSyncKHR)(EGLDisplay, EGLenum, const EGLint *);
   EGLSync (*eglCreateSync)(EGLDisplay, EGLenum, const intptr_t *);
   EGLint (*eglClientWaitSyncKHR)(EGLDisplay, EGLSync, EGLint, uint64_t);
   EGLint (*eglClientWaitSync)(EGLDisplay, EGLSync, EGLint, uint64_t);
   EGLint (*eglWaitSyncKHR)(EGLDisplay, EGLSync, EGLint);
   EGLBoolean (*eglWaitSync)(EGLDisplay, EGLSync, EGLint);
   fnptr (*eglGetProcAddress)(const char *);
   EGLBoolean (*eglQueryDmaBufModifiersEXT)(EGLDisplay, EGLint, EGLint, uint64_t *, EGLBoolean *, EGLint *);
   EGLBoolean (*swap_damage[2])(EGLDisplay, EGLSurface, const EGLint *, EGLint); /* KHR, EXT: by eglGetProcAddress only */
#define X(n, p, a) void (*n) p;
   DRAWS(X)
#undef X
} r;
static int enabled = 1, debug, hide_mods = 1, fix_windepth = 1, fix_msrtt = 1;
static int keep_depth, keep_auto = 1; /* window depth mode: 0 discard before a flush, 1 draw through an FBO */
static unsigned long n_tex, n_tex_image, n_rb, n_rb_fail, n_mod, n_drop, n_ms, n_keep;
static pthread_once_t once = PTHREAD_ONCE_INIT;

/* The calling thread's window frame since its last swap. */
static __thread int depth_watch;      /* draw calls left to check for depth use after a discard */

/* Keep mode: the window of one context drawn through a framebuffer object of ours. */
struct redirect {
   EGLContext ctx; /* NULL: free slot */
   EGLSurface surf;
   unsigned fbo, tex, rb, prog, vbo;
   int w, h, es3, alpha, dead;
};
static struct redirect slots[4];
static pthread_mutex_t slot_lock = PTHREAD_MUTEX_INITIALIZER;
static __thread struct redirect *rd; /* the calling thread's context + draw surface are redirected */

static void *next(const char *name)
{
   void *p = dlsym(RTLD_NEXT, name);
   if (!p) {
      static void *mali;
      if (!mali)
         mali = dlopen("libmali.so.1", RTLD_NOW | RTLD_GLOBAL);
      if (mali)
         p = dlsym(mali, name);
   }
   return p;
}

static void report(void)
{
   fprintf(stderr,
           "[afbcfix] texture marks %lu (%lu made an image), renderbuffers %lu (%lu failed), AFBC modifiers hidden %lu, "
           "window depth discards %lu, multisample calls made single-sample %lu, depth mode %s (%lu windows drawn "
           "through a framebuffer object)\n",
           n_tex, n_tex_image, n_rb, n_rb_fail, n_mod, n_drop, n_ms, keep_depth ? "keep" : "discard", n_keep);
}

/* AFBCFIX_DEBUG=2: one line per EGL-level event in the kernel log, next to the GPU fault lines. */
static void trace(const char *fmt, ...)
{
   static int fd = -2;
   if (debug < 2)
      return;
   if (fd == -2)
      fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
   char b[240];
   int n = snprintf(b, sizeof b, "afbcfix[%d]: ", (int)getpid());
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(b + n, sizeof b - (size_t)n, fmt, ap);
   va_end(ap);
   if (fd < 0 || write(fd, b, strlen(b)) < 0)
      fprintf(stderr, "%s\n", b);
}

static int off(const char *var)
{
   const char *e = getenv(var);
   return e && *e == '0';
}

static void init_once(void)
{
   r.glTexImage2D = next("glTexImage2D");
   r.glTexStorage2D = next("glTexStorage2D");
   r.glCopyTexImage2D = next("glCopyTexImage2D");
   r.glTexImage3D = next("glTexImage3D");
   r.glTexStorage3D = next("glTexStorage3D");
   r.glRenderbufferStorage = next("glRenderbufferStorage");
   r.glRenderbufferStorageOES = next("glRenderbufferStorageOES");
   r.glRenderbufferStorageMultisample = next("glRenderbufferStorageMultisample");
   r.glRenderbufferStorageMultisampleEXT = next("glRenderbufferStorageMultisampleEXT");
   r.glFramebufferTexture2D = next("glFramebufferTexture2D");
   r.glFramebufferTexture2DMultisampleEXT = next("glFramebufferTexture2DMultisampleEXT");
   r.glBindFramebuffer = next("glBindFramebuffer");
   r.glBindFramebufferOES = next("glBindFramebufferOES");
   r.glGenFramebuffers = next("glGenFramebuffers");
   r.glCheckFramebufferStatus = next("glCheckFramebufferStatus");
   r.glGenTextures = next("glGenTextures");
   r.glBindTexture = next("glBindTexture");
   r.glDiscardFramebufferEXT = next("glDiscardFramebufferEXT");
   r.glInvalidateFramebuffer = next("glInvalidateFramebuffer");
   r.glFinish = next("glFinish");
   r.glFlush = next("glFlush");
   r.glIsEnabled = next("glIsEnabled");
   r.glReadPixels = next("glReadPixels");
   r.glReadnPixels = next("glReadnPixels");
   r.glGetIntegerv = next("glGetIntegerv");
   r.glGetString = next("glGetString");
   r.glGetStringi = next("glGetStringi");
   r.glFenceSync = next("glFenceSync");
   r.glClientWaitSync = next("glClientWaitSync");
   r.glWaitSync = next("glWaitSync");
   r.eglGetCurrentDisplay = next("eglGetCurrentDisplay");
   r.eglGetCurrentContext = next("eglGetCurrentContext");
   r.eglGetCurrentSurface = next("eglGetCurrentSurface");
   r.eglQueryContext = next("eglQueryContext");
   r.eglQuerySurface = next("eglQuerySurface");
   r.eglChooseConfig = next("eglChooseConfig");
   r.eglGetConfigAttrib = next("eglGetConfigAttrib");
   r.eglCreateImageKHR = next("eglCreateImageKHR");
   r.eglDestroyImageKHR = next("eglDestroyImageKHR");
   r.eglGetError = next("eglGetError");
   r.eglWaitGL = next("eglWaitGL");
   r.eglWaitClient = next("eglWaitClient");
   r.eglMakeCurrent = next("eglMakeCurrent");
   r.eglSwapBuffers = next("eglSwapBuffers");
   r.eglReleaseThread = next("eglReleaseThread");
   r.eglDestroyContext = next("eglDestroyContext");
   r.eglCreateSyncKHR = next("eglCreateSyncKHR");
   r.eglCreateSync = next("eglCreateSync");
   r.eglClientWaitSyncKHR = next("eglClientWaitSyncKHR");
   r.eglClientWaitSync = next("eglClientWaitSync");
   r.eglWaitSyncKHR = next("eglWaitSyncKHR");
   r.eglWaitSync = next("eglWaitSync");
   r.eglGetProcAddress = next("eglGetProcAddress");
   r.eglQueryDmaBufModifiersEXT = next("eglQueryDmaBufModifiersEXT");
   enabled = !off("AFBCFIX") && r.eglCreateImageKHR && r.eglDestroyImageKHR && r.glGetIntegerv && r.eglGetCurrentContext &&
             r.eglGetCurrentDisplay;
   hide_mods = !off("AFBCFIX_MODS") && access("/tmp/afbcfix_nomods", F_OK) != 0;
   fix_windepth = !off("AFBCFIX_WINDEPTH");
   fix_msrtt = !off("AFBCFIX_MSRTT");
   const char *e = getenv("AFBCFIX_KEEPDEPTH");
   keep_depth = e && *e == '1';
   keep_auto = !(e && *e == '0');
   e = getenv("AFBCFIX_DEBUG");
   debug = e ? atoi(e) : 0;
   if (debug) {
      fprintf(stderr, "[afbcfix] 3: %s\n", enabled ? "on" : "off");
      atexit(report);
   }
   g.ready = 1;
#define X(t, n, a) \
   g.n = next(#n); \
   if (!g.n && strcmp(#n, "glBlitFramebuffer") && strcmp(#n, "glInvalidateSubFramebuffer") && strcmp(#n, "glReadBuffer") && \
       strcmp(#n, "glDrawBuffers") && strcmp(#n, "glGetFramebufferAttachmentParameteriv")) \
      g.ready = 0;
   KEEPS(X)
#undef X
   if (!r.glGenTextures || !r.glBindTexture || !r.glGenFramebuffers || !r.glCheckFramebufferStatus || !r.glFramebufferTexture2D ||
       !r.glBindFramebuffer || !r.glIsEnabled || !r.eglQuerySurface)
      g.ready = 0;
   /* last: the draw and clear wrappers take their own pointer as the sign that all of this is done */
#define X(n, p, a) r.n = next(#n);
   DRAWS(X)
#undef X
   r.glClear = next("glClear");
}

static void init(void)
{
   pthread_once(&once, init_once);
}

static int ctx_version(void)
{
   EGLint ver = 2;
   if (r.eglQueryContext)
      r.eglQueryContext(r.eglGetCurrentDisplay(), r.eglGetCurrentContext(), EGL_CONTEXT_CLIENT_VERSION, &ver);
   return ver;
}

/* ---- textures ---- */

static unsigned binding_of(unsigned target)
{
   if (target == GL_TEXTURE_2D)
      return GL_TEXTURE_BINDING_2D;
   if (target == GL_TEXTURE_CUBE_MAP || (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target < GL_TEXTURE_CUBE_MAP_POSITIVE_X + 6))
      return GL_TEXTURE_BINDING_CUBE_MAP;
   if (target == GL_TEXTURE_2D_ARRAY)
      return GL_TEXTURE_BINDING_2D_ARRAY;
   return 0; /* 3D textures are never compressed; nothing else is handled */
}

/* Ask for an image of the bound texture with a kind that can't fit it. No image comes back, but the
 * blob keeps that texture uncompressed from then on. Safe to repeat. */
static void mark_texture(unsigned target)
{
   const unsigned binding = binding_of(target);
   EGLContext ctx;
   int name = 0;
   if (!enabled || !binding || !(ctx = r.eglGetCurrentContext()))
      return;
   r.glGetIntegerv(binding, &name);
   if (!name)
      return;
   EGLDisplay dpy = r.eglGetCurrentDisplay();
   const EGLenum kind = binding == GL_TEXTURE_BINDING_2D ? EGL_GL_TEXTURE_CUBE_MAP_POSITIVE_X_KHR : EGL_GL_TEXTURE_2D_KHR;
   EGLImage im = r.eglCreateImageKHR(dpy, ctx, kind, (void *)(uintptr_t)(unsigned)name, NULL);
   if (im) { /* not expected; don't leak it */
      r.eglDestroyImageKHR(dpy, im);
      n_tex_image++;
   } else if (r.eglGetError)
      r.eglGetError(); /* take the refusal out of the thread's EGL error state */
   n_tex++;
}

void glTexImage2D(unsigned target, int level, int ifmt, int w, int h, int border, unsigned fmt, unsigned type,
                  const void *data)
{
   init();
   mark_texture(target);
   r.glTexImage2D(target, level, ifmt, w, h, border, fmt, type, data);
}

void glTexStorage2D(unsigned target, int levels, unsigned ifmt, int w, int h)
{
   init();
   if (!r.glTexStorage2D)
      return;
   mark_texture(target);
   r.glTexStorage2D(target, levels, ifmt, w, h);
}

void glCopyTexImage2D(unsigned target, int level, unsigned ifmt, int x, int y, int w, int h, int border)
{
   init();
   mark_texture(target);
   r.glCopyTexImage2D(target, level, ifmt, x, y, w, h, border);
}

void glTexImage3D(unsigned target, int level, int ifmt, int w, int h, int d, int border, unsigned fmt, unsigned type,
                  const void *data)
{
   init();
   if (!r.glTexImage3D)
      return;
   mark_texture(target);
   r.glTexImage3D(target, level, ifmt, w, h, d, border, fmt, type, data);
}

void glTexStorage3D(unsigned target, int levels, unsigned ifmt, int w, int h, int d)
{
   init();
   if (!r.glTexStorage3D)
      return;
   mark_texture(target);
   r.glTexStorage3D(target, levels, ifmt, w, h, d);
}

/* ---- renderbuffers ---- */

/* The bound renderbuffer has storage: make an EGLImage from it and destroy it again. The blob then never
 * writes it as AFBC. An image that can't be made is not an error for the caller. */
static void unshare_renderbuffer(int w, int h)
{
   EGLContext ctx;
   int name = 0;
   if (!enabled || w < 16 || h < 16 || !(ctx = r.eglGetCurrentContext())) /* under one AFBC block: never compressed */
      return;
   r.glGetIntegerv(GL_RENDERBUFFER_BINDING, &name);
   if (!name)
      return;
   EGLDisplay dpy = r.eglGetCurrentDisplay();
   EGLImage im = r.eglCreateImageKHR(dpy, ctx, EGL_GL_RENDERBUFFER_KHR, (void *)(uintptr_t)(unsigned)name, NULL);
   if (!im) {
      if (r.eglGetError)
         r.eglGetError();
      n_rb_fail++;
      return;
   }
   r.eglDestroyImageKHR(dpy, im);
   n_rb++;
}

void glRenderbufferStorage(unsigned target, unsigned ifmt, int w, int h)
{
   init();
   r.glRenderbufferStorage(target, ifmt, w, h);
   if (target == GL_RENDERBUFFER)
      unshare_renderbuffer(w, h);
}

/* GLES 1 */
void glRenderbufferStorageOES(unsigned target, unsigned ifmt, int w, int h)
{
   init();
   if (!r.glRenderbufferStorageOES)
      return;
   r.glRenderbufferStorageOES(target, ifmt, w, h);
   if (target == GL_RENDERBUFFER)
      unshare_renderbuffer(w, h);
}

/* Core ES 3: real multisample storage is not compressed. With 0 samples it is plain storage. */
void glRenderbufferStorageMultisample(unsigned target, int samples, unsigned ifmt, int w, int h)
{
   init();
   if (!r.glRenderbufferStorageMultisample)
      return;
   r.glRenderbufferStorageMultisample(target, samples, ifmt, w, h);
   if (target == GL_RENDERBUFFER && samples == 0)
      unshare_renderbuffer(w, h);
}

/* EXT_multisampled_render_to_texture: the depth/stencil renderbuffer is resolved into single-sample
 * storage at the end of each pass, as AFBC, and neither image step changes that. So these framebuffers
 * are made single-sample: correct picture, no anti-aliasing. */
void glRenderbufferStorageMultisampleEXT(unsigned target, int samples, unsigned ifmt, int w, int h)
{
   init();
   if (enabled && fix_msrtt) {
      n_ms++;
      r.glRenderbufferStorage(target, ifmt, w, h);
      if (target == GL_RENDERBUFFER)
         unshare_renderbuffer(w, h);
      return;
   }
   if (r.glRenderbufferStorageMultisampleEXT)
      r.glRenderbufferStorageMultisampleEXT(target, samples, ifmt, w, h);
}

void glFramebufferTexture2DMultisampleEXT(unsigned target, unsigned attachment, unsigned textarget, unsigned texture,
                                          int level, int samples)
{
   init();
   if (enabled && fix_msrtt && r.glFramebufferTexture2D) {
      n_ms++;
      r.glFramebufferTexture2D(target, attachment, textarget, texture, level);
      return;
   }
   if (r.glFramebufferTexture2DMultisampleEXT)
      r.glFramebufferTexture2DMultisampleEXT(target, attachment, textarget, texture, level, samples);
}

/* ---- the extension list, without EXT_multisampled_render_to_texture ---- */

static int hidden_ext(const char *e, size_t len)
{
   static const char *const h[] = {"GL_EXT_multisampled_render_to_texture", "GL_EXT_multisampled_render_to_texture2"};
   for (unsigned i = 0; i < sizeof h / sizeof *h; i++)
      if (strlen(h[i]) == len && !memcmp(e, h[i], len))
         return 1;
   return 0;
}

const unsigned char *glGetString(unsigned name)
{
   static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
   static const unsigned char *src;
   static char *filtered;
   init();
   const unsigned char *s = r.glGetString(name);
   if (!enabled || !fix_msrtt || name != GL_EXTENSIONS || !s)
      return s;
   pthread_mutex_lock(&lock);
   if (s != src) { /* an older copy may still be in use by the caller: it is left alone */
      char *out = malloc(strlen((const char *)s) + 1), *o = out;
      if (!out) {
         pthread_mutex_unlock(&lock);
         return s;
      }
      for (const char *p = (const char *)s; *p;) {
         size_t len = strcspn(p, " ");
         if (len && !hidden_ext(p, len)) {
            memcpy(o, p, len);
            o += len;
            *o++ = ' ';
         }
         p += len;
         while (*p == ' ')
            p++;
      }
      if (o > out)
         o--;
      *o = 0;
      src = s;
      filtered = out;
   }
   const unsigned char *res = (const unsigned char *)filtered;
   pthread_mutex_unlock(&lock);
   return res;
}

/* ES 3 lists extensions by index: skip the hidden ones. */
const unsigned char *glGetStringi(unsigned name, unsigned index)
{
   init();
   if (!r.glGetStringi)
      return NULL;
   if (!enabled || !fix_msrtt || name != GL_EXTENSIONS)
      return r.glGetStringi(name, index);
   int n = 0;
   r.glGetIntegerv(GL_NUM_EXTENSIONS, &n);
   unsigned shown = 0;
   for (int j = 0; j < n; j++) {
      const unsigned char *e = r.glGetStringi(name, (unsigned)j);
      if (!e || hidden_ext((const char *)e, strlen((const char *)e)))
         continue;
      if (shown++ == index)
         return e;
   }
   return r.glGetStringi(name, (unsigned)n); /* out of range: let the blob raise its error */
}

void glGetIntegerv(unsigned pname, int *data)
{
   init();
   r.glGetIntegerv(pname, data);
   if (__builtin_expect(rd != NULL, 0) && data) {
      if ((pname == GL_FRAMEBUFFER_BINDING || pname == GL_READ_FRAMEBUFFER_BINDING) && (unsigned)*data == rd->fbo)
         *data = 0;
      else if ((pname == GL_READ_BUFFER || pname == GL_DRAW_BUFFER0) && *data == GL_COLOR_ATTACHMENT0) {
         int fb = 0;
         r.glGetIntegerv(pname == GL_READ_BUFFER ? GL_READ_FRAMEBUFFER_BINDING : GL_FRAMEBUFFER_BINDING, &fb);
         if ((unsigned)fb == rd->fbo)
            *data = GL_BACK;
      }
   }
   if (pname == GL_NUM_EXTENSIONS && enabled && fix_msrtt && data && r.glGetStringi) {
      const int n = *data;
      for (int j = 0; j < n; j++) {
         const unsigned char *e = r.glGetStringi(GL_EXTENSIONS, (unsigned)j);
         if (e && hidden_ext((const char *)e, strlen((const char *)e)))
            (*data)--;
      }
   }
}

/* ---- window depth ---- */

/* A context with a draw surface is current, the window depth handling is on and discard mode applies. */
static int have_window(void)
{
   return enabled && fix_windepth && !rd && r.eglGetCurrentContext() && r.eglGetCurrentSurface &&
          r.eglGetCurrentSurface(EGL_DRAW);
}

/* The current surface's frame ends (swap) or the thread leaves the surface. */
static void frame_end(void)
{
   depth_watch = 0;
}

/* A window frame that is flushed to memory before its swap gets its depth/stencil written as AFBC: a
 * fault, and the frame comes out black. Discarding depth and stencil first avoids the write.
 * reading: the caller reads pixels; only the window's own frame matters then. */
static void drop_window_depth(int reading)
{
   if (!have_window() || !r.glBindFramebuffer)
      return;
   const int ver = ctx_version(), es3 = ver >= 3 && r.glInvalidateFramebuffer;
   void (*const bind)(unsigned, unsigned) = ver == 1 && r.glBindFramebufferOES ? r.glBindFramebufferOES : r.glBindFramebuffer;
   int draw = 0, read = 0, depth = 0, stencil = 0;
   r.glGetIntegerv(GL_FRAMEBUFFER_BINDING, &draw);
   if (es3)
      r.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
   else
      read = draw;
   if (reading && read != 0)
      return;
   if (!es3 && !r.glDiscardFramebufferEXT)
      return;
   static const unsigned ds[2] = {GL_DEPTH_EXT, GL_STENCIL_EXT};
   const unsigned t = es3 ? GL_DRAW_FRAMEBUFFER : GL_FRAMEBUFFER;
   if (draw)
      bind(t, 0);
   r.glGetIntegerv(GL_DEPTH_BITS, &depth);
   r.glGetIntegerv(GL_STENCIL_BITS, &stencil);
   if (depth || stencil) { /* a window without them has nothing that could be written as AFBC */
      if (es3)
         r.glInvalidateFramebuffer(t, 2, ds);
      else
         r.glDiscardFramebufferEXT(GL_FRAMEBUFFER, 2, ds);
      n_drop++;
      if (keep_auto && !keep_depth && ver >= 2)
         depth_watch = 256;
   }
   if (draw)
      bind(t, (unsigned)draw);
}

/* A draw call after a discard: depth or stencil testing into the window means this application needs
 * the depth buffer across the flush. Switch to keep mode (it starts at the next swap). */
static void depth_use_check(void)
{
   int fb = 0;
   depth_watch--;
   if (!r.glIsEnabled || !r.eglGetCurrentContext())
      return;
   r.glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
   if (fb || (!r.glIsEnabled(GL_DEPTH_TEST) && !r.glIsEnabled(GL_STENCIL_TEST)))
      return;
   depth_watch = 0;
   keep_depth = 1;
   trace("depth used after a discard: keep mode");
   if (debug)
      fprintf(stderr, "[afbcfix] depth-tested drawing after a discard: keeping the window's depth from now on\n");
}

#define X(n, p, a) \
   void n p \
   { \
      if (__builtin_expect(!r.n, 0)) { \
         init(); \
         if (!r.n) \
            return; \
      } \
      if (__builtin_expect(depth_watch, 0)) \
         depth_use_check(); \
      r.n a; \
   }
DRAWS(X)
#undef X

/* Clearing the window's depth after a discard makes it valid again. */
void glClear(unsigned mask)
{
   if (__builtin_expect(!r.glClear, 0)) {
      init();
      if (!r.glClear)
         return;
   }
   if (__builtin_expect(depth_watch, 0) && (mask & GL_DEPTH_BUFFER_BIT)) {
      int fb = 0;
      r.glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
      if (!fb)
         depth_watch = 0;
   }
   r.glClear(mask);
}

void glFinish(void)
{
   init();
   drop_window_depth(0);
   r.glFinish();
}

void glReadPixels(int x, int y, int w, int h, unsigned fmt, unsigned type, void *pixels)
{
   init();
   drop_window_depth(1);
   r.glReadPixels(x, y, w, h, fmt, type, pixels);
}

void glReadnPixels(int x, int y, int w, int h, unsigned fmt, unsigned type, int size, void *pixels)
{
   init();
   if (!r.glReadnPixels)
      return;
   drop_window_depth(1);
   r.glReadnPixels(x, y, w, h, fmt, type, size, pixels);
}

void glReadnPixelsEXT(int, int, int, int, unsigned, unsigned, int, void *) __attribute__((alias("glReadnPixels")));
void glReadnPixelsKHR(int, int, int, int, unsigned, unsigned, int, void *) __attribute__((alias("glReadnPixels")));

EGLBoolean eglWaitGL(void)
{
   init();
   drop_window_depth(0);
   return r.eglWaitGL ? r.eglWaitGL() : 0;
}

EGLBoolean eglWaitClient(void)
{
   init();
   drop_window_depth(0);
   return r.eglWaitClient ? r.eglWaitClient() : 0;
}

/* ---- keep mode: the window drawn through a framebuffer object ---- */

/* An attribute of the surface's EGL config. */
static EGLint surface_cfg(EGLDisplay dpy, EGLSurface surf, EGLint attr, EGLint fallback)
{
   EGLint id = 0, v = fallback, n = 0;
   EGLConfig cfg = NULL;
   if (r.eglQuerySurface && r.eglChooseConfig && r.eglGetConfigAttrib && r.eglQuerySurface(dpy, surf, EGL_CONFIG_ID, &id)) {
      const EGLint want[] = {EGL_CONFIG_ID, id, EGL_NONE};
      if (r.eglChooseConfig(dpy, want, &cfg, 1, &n) && n == 1)
         r.eglGetConfigAttrib(dpy, cfg, attr, &v);
   }
   return v;
}

/* New color texture and depth/stencil renderbuffer of that size on d->fbo, both kept off AFBC. The
 * caller's bindings come back as they were. 1: the framebuffer is complete. */
static int rd_alloc(struct redirect *d, int w, int h)
{
   int tex0 = 0, rb0 = 0, draw = 0, read = 0;
   r.glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex0);
   r.glGetIntegerv(GL_RENDERBUFFER_BINDING, &rb0);
   r.glGetIntegerv(GL_FRAMEBUFFER_BINDING, &draw);
   if (d->es3)
      r.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
   if (d->tex)
      g.glDeleteTextures(1, &d->tex);
   if (d->rb)
      g.glDeleteRenderbuffers(1, &d->rb);
   r.glGenTextures(1, &d->tex);
   g.glGenRenderbuffers(1, &d->rb);
   r.glBindTexture(GL_TEXTURE_2D, d->tex);
   mark_texture(GL_TEXTURE_2D);
   if (d->es3 && r.glTexStorage2D) /* no pointer argument, so a bound pixel unpack buffer can't get in the way */
      r.glTexStorage2D(GL_TEXTURE_2D, 1, d->alpha ? GL_RGBA8 : GL_RGB8, w, h);
   else
      r.glTexImage2D(GL_TEXTURE_2D, 0, d->alpha ? GL_RGBA : GL_RGB, w, h, 0, d->alpha ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, NULL);
   g.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   g.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   g.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   g.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   r.glBindTexture(GL_TEXTURE_2D, (unsigned)tex0);
   g.glBindRenderbuffer(GL_RENDERBUFFER, d->rb);
   r.glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
   unshare_renderbuffer(w, h);
   g.glBindRenderbuffer(GL_RENDERBUFFER, (unsigned)rb0);
   r.glBindFramebuffer(GL_FRAMEBUFFER, d->fbo);
   r.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, d->tex, 0);
   g.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, d->rb);
   g.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, d->rb);
   const int ok = r.glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
   if (d->es3) {
      r.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (unsigned)draw);
      r.glBindFramebuffer(GL_READ_FRAMEBUFFER, (unsigned)read);
   } else
      r.glBindFramebuffer(GL_FRAMEBUFFER, (unsigned)draw);
   d->w = w;
   d->h = h;
   return ok;
}

/* The context of d is current: give the application its real window back and free our objects. */
static void rd_deactivate(struct redirect *d)
{
   int draw = 0, read = 0;
   r.glGetIntegerv(GL_FRAMEBUFFER_BINDING, &draw);
   if (d->es3) {
      r.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
      if ((unsigned)draw == d->fbo)
         r.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
      if ((unsigned)read == d->fbo)
         r.glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
   } else if ((unsigned)draw == d->fbo)
      r.glBindFramebuffer(GL_FRAMEBUFFER, 0);
   g.glDeleteFramebuffers(1, &d->fbo);
   g.glDeleteTextures(1, &d->tex);
   g.glDeleteRenderbuffers(1, &d->rb);
   if (d->prog)
      g.glDeleteProgram(d->prog);
   if (d->vbo)
      g.glDeleteBuffers(1, &d->vbo);
   trace("redirect off (surface %p)", d->surf);
   pthread_mutex_lock(&slot_lock);
   memset(d, 0, sizeof *d);
   pthread_mutex_unlock(&slot_lock);
}

/* ES 2 has no blit: a program and a vertex buffer for a full-window textured quad. */
static int rd_quad_setup(struct redirect *d)
{
   static const char *const vs = "attribute vec2 p; varying vec2 uv; void main(){ uv = p * 0.5 + 0.5; gl_Position = vec4(p, 0.0, 1.0); }";
   static const char *const fs = "precision mediump float; varying vec2 uv; uniform sampler2D s; void main(){ gl_FragColor = texture2D(s, uv); }";
   static const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
   int abuf = 0, linked = 0;
   const unsigned v = g.glCreateShader(GL_VERTEX_SHADER), f = g.glCreateShader(GL_FRAGMENT_SHADER);
   g.glShaderSource(v, 1, &vs, NULL);
   g.glShaderSource(f, 1, &fs, NULL);
   g.glCompileShader(v);
   g.glCompileShader(f);
   d->prog = g.glCreateProgram();
   g.glAttachShader(d->prog, v);
   g.glAttachShader(d->prog, f);
   g.glBindAttribLocation(d->prog, 0, "p");
   g.glLinkProgram(d->prog);
   g.glDeleteShader(v);
   g.glDeleteShader(f);
   g.glGetProgramiv(d->prog, GL_LINK_STATUS, &linked);
   r.glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &abuf);
   g.glGenBuffers(1, &d->vbo);
   g.glBindBuffer(GL_ARRAY_BUFFER, d->vbo);
   g.glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
   g.glBindBuffer(GL_ARRAY_BUFFER, (unsigned)abuf);
   return linked;
}

/* Start redirecting the current context's window (called after a swap, so a new frame starts). */
static void rd_activate(EGLDisplay dpy, EGLSurface surf)
{
   EGLContext ctx = r.eglGetCurrentContext();
   EGLint w = 0, h = 0;
   if (!ctx || !g.ready || r.eglGetCurrentSurface(EGL_DRAW) != surf)
      return;
   const int ver = ctx_version();
   if (ver < 2 || !r.eglQuerySurface(dpy, surf, EGL_WIDTH, &w) || !r.eglQuerySurface(dpy, surf, EGL_HEIGHT, &h) || w < 1 || h < 1)
      return;
   /* nothing to keep without depth or stencil; multisampled windows are not handled */
   if ((!surface_cfg(dpy, surf, EGL_DEPTH_SIZE, 0) && !surface_cfg(dpy, surf, EGL_STENCIL_SIZE, 0)) ||
       surface_cfg(dpy, surf, EGL_SAMPLES, 0) > 1)
      return;
   struct redirect *d = NULL;
   pthread_mutex_lock(&slot_lock);
   for (unsigned i = 0; i < sizeof slots / sizeof *slots && !d; i++)
      if (!slots[i].ctx) {
         d = &slots[i];
         memset(d, 0, sizeof *d);
         d->ctx = ctx;
      }
   pthread_mutex_unlock(&slot_lock);
   if (!d)
      return;
   d->surf = surf;
   d->es3 = ver >= 3;
   d->alpha = surface_cfg(dpy, surf, EGL_ALPHA_SIZE, 8) > 0;
   r.glGenFramebuffers(1, &d->fbo);
   if (!rd_alloc(d, w, h) || (!d->es3 && !rd_quad_setup(d))) {
      if (debug)
         fprintf(stderr, "[afbcfix] keep mode: no framebuffer for the window, staying with the discard\n");
      rd_deactivate(d);
      keep_depth = 0;
      keep_auto = 0;
      return;
   }
   /* what the application has bound as "the window" is ours from now on */
   int draw = 0, read = 0;
   r.glGetIntegerv(GL_FRAMEBUFFER_BINDING, &draw);
   if (d->es3) {
      r.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
      if (!draw)
         r.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, d->fbo);
      if (!read)
         r.glBindFramebuffer(GL_READ_FRAMEBUFFER, d->fbo);
   } else if (!draw)
      r.glBindFramebuffer(GL_FRAMEBUFFER, d->fbo);
   rd = d;
   n_keep++;
   trace("redirect on: surface %p %dx%d es%d", surf, w, h, ver);
   if (debug)
      fprintf(stderr, "[afbcfix] keep mode: the window (%dx%d) is drawn through a framebuffer object\n", w, h);
}

/* Copy our color into the real window, right before the swap. All state comes back as it was. */
static void rd_present(struct redirect *d)
{
   static const unsigned ds[2] = {GL_DEPTH_EXT, GL_STENCIL_EXT};
   if (d->es3) {
      int draw = 0, read = 0;
      r.glGetIntegerv(GL_FRAMEBUFFER_BINDING, &draw);
      r.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
      const int scissor = r.glIsEnabled(GL_SCISSOR_TEST);
      if (scissor)
         g.glDisable(GL_SCISSOR_TEST);
      r.glBindFramebuffer(GL_READ_FRAMEBUFFER, d->fbo);
      r.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
      g.glBlitFramebuffer(0, 0, d->w, d->h, 0, 0, d->w, d->h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
      r.glInvalidateFramebuffer(GL_DRAW_FRAMEBUFFER, 2, ds);
      r.glBindFramebuffer(GL_READ_FRAMEBUFFER, (unsigned)read);
      r.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (unsigned)draw);
      if (scissor)
         g.glEnable(GL_SCISSOR_TEST);
      return;
   }
   static const unsigned caps[7] = {GL_BLEND, GL_CULL_FACE, GL_DEPTH_TEST, GL_SCISSOR_TEST, GL_STENCIL_TEST,
                                    GL_SAMPLE_ALPHA_TO_COVERAGE, GL_SAMPLE_COVERAGE};
   unsigned char on[7];
   int fb = 0, prog = 0, unit = 0, tex = 0, abuf = 0, vp[4] = {0, 0, 0, 0}, cm[4] = {1, 1, 1, 1};
   int a_on = 0, a_size = 4, a_type = GL_FLOAT, a_norm = 0, a_stride = 0, a_buf = 0;
   void *a_ptr = NULL;
   r.glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
   r.glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
   r.glGetIntegerv(GL_ACTIVE_TEXTURE, &unit);
   g.glActiveTexture(GL_TEXTURE0);
   r.glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
   r.glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &abuf);
   r.glGetIntegerv(GL_VIEWPORT, vp);
   r.glGetIntegerv(GL_COLOR_WRITEMASK, cm);
   g.glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &a_on);
   g.glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_SIZE, &a_size);
   g.glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_TYPE, &a_type);
   g.glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &a_norm);
   g.glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &a_stride);
   g.glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &a_buf);
   g.glGetVertexAttribPointerv(0, GL_VERTEX_ATTRIB_ARRAY_POINTER, &a_ptr);
   for (int i = 0; i < 7; i++)
      if ((on[i] = r.glIsEnabled(caps[i])))
         g.glDisable(caps[i]);
   r.glBindFramebuffer(GL_FRAMEBUFFER, 0);
   g.glViewport(0, 0, d->w, d->h);
   g.glColorMask(1, 1, 1, 1);
   g.glUseProgram(d->prog);
   r.glBindTexture(GL_TEXTURE_2D, d->tex);
   g.glBindBuffer(GL_ARRAY_BUFFER, d->vbo);
   g.glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, NULL);
   g.glEnableVertexAttribArray(0);
   r.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
   if (r.glDiscardFramebufferEXT)
      r.glDiscardFramebufferEXT(GL_FRAMEBUFFER, 2, ds);
   g.glBindBuffer(GL_ARRAY_BUFFER, (unsigned)a_buf);
   g.glVertexAttribPointer(0, a_size, (unsigned)a_type, (unsigned char)a_norm, a_stride, a_ptr);
   if (!a_on)
      g.glDisableVertexAttribArray(0);
   g.glBindBuffer(GL_ARRAY_BUFFER, (unsigned)abuf);
   r.glBindTexture(GL_TEXTURE_2D, (unsigned)tex);
   g.glActiveTexture((unsigned)unit);
   g.glUseProgram((unsigned)prog);
   g.glColorMask((unsigned char)cm[0], (unsigned char)cm[1], (unsigned char)cm[2], (unsigned char)cm[3]);
   g.glViewport(vp[0], vp[1], vp[2], vp[3]);
   for (int i = 0; i < 7; i++)
      if (on[i])
         g.glEnable(caps[i]);
   r.glBindFramebuffer(GL_FRAMEBUFFER, (unsigned)fb);
}

/* kind: -1 eglSwapBuffers, 0/1 the KHR/EXT damage forms. */
static EGLBoolean swap_common(EGLDisplay dpy, EGLSurface surf, const EGLint *rects, EGLint n, int kind)
{
   frame_end();
   struct redirect *d = rd && rd->surf == surf ? rd : NULL;
   if (d)
      rd_present(d);
   const EGLBoolean ok = kind < 0 ? r.eglSwapBuffers(dpy, surf) : r.swap_damage[kind](dpy, surf, rects, n);
   if (d) { /* follow a resized window */
      EGLint w = 0, h = 0;
      if (r.eglQuerySurface(dpy, surf, EGL_WIDTH, &w) && r.eglQuerySurface(dpy, surf, EGL_HEIGHT, &h) && w > 0 && h > 0 &&
          (w != d->w || h != d->h))
         rd_alloc(d, w, h);
   } else if (keep_depth && !rd && enabled && fix_windepth)
      rd_activate(dpy, surf);
   return ok;
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surf)
{
   static int shown;
   init();
   if (!r.eglSwapBuffers)
      return 0;
   if (debug >= 3 || (debug >= 2 && shown < 6)) { /* 3: every swap */
      shown++;
      trace("eglSwapBuffers %p (%d)", surf, shown);
   }
   return swap_common(dpy, surf, NULL, 0, -1);
}

static EGLBoolean swap_damage_khr(EGLDisplay dpy, EGLSurface surf, const EGLint *rects, EGLint n)
{
   return swap_common(dpy, surf, rects, n, 0);
}

static EGLBoolean swap_damage_ext(EGLDisplay dpy, EGLSurface surf, const EGLint *rects, EGLint n)
{
   return swap_common(dpy, surf, rects, n, 1);
}

/* Leaving a window surface flushes its pending frame like glFinish does. A redirect belongs to one
 * context + surface pair: it follows the context between threads and ends when the context gets another
 * draw surface. */
EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx)
{
   init();
   if (!r.eglMakeCurrent)
      return 0;
   struct redirect *old = rd;
   if (old) {
      if (old->ctx == ctx && old->surf == draw)
         return r.eglMakeCurrent(dpy, draw, read, ctx);
      if (old->ctx == ctx) { /* same context, another surface: hand over what was drawn so far */
         rd_present(old);
         rd_deactivate(old);
         old = NULL;
      }
      rd = NULL;
   } else if (enabled && fix_windepth && r.eglGetCurrentSurface) {
      EGLSurface cur = r.eglGetCurrentSurface(EGL_DRAW);
      /* another surface, or another context on the same surface (glmark2 does that between scenes) */
      if (cur && (cur != draw || r.eglGetCurrentContext() != ctx)) {
         trace("eglMakeCurrent leaves surface %p for %p", cur, draw);
         drop_window_depth(0);
         frame_end();
      }
   }
   const EGLBoolean ok = r.eglMakeCurrent(dpy, draw, read, ctx);
   if (!ok) {
      rd = old;
      return ok;
   }
   if (old && old->dead) { /* its context was destroyed while current and is gone now */
      pthread_mutex_lock(&slot_lock);
      memset(old, 0, sizeof *old);
      pthread_mutex_unlock(&slot_lock);
   }
   if (ctx && keep_depth) {
      struct redirect *d = NULL;
      pthread_mutex_lock(&slot_lock);
      for (unsigned i = 0; i < sizeof slots / sizeof *slots && !d; i++)
         if (slots[i].ctx == ctx && !slots[i].dead)
            d = &slots[i];
      pthread_mutex_unlock(&slot_lock);
      if (d && d->surf == draw)
         rd = d;
      else if (d)
         rd_deactivate(d);
   }
   return ok;
}

EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx)
{
   init();
   if (!r.eglDestroyContext)
      return 0;
   trace("eglDestroyContext %p", ctx);
   pthread_mutex_lock(&slot_lock);
   for (unsigned i = 0; i < sizeof slots / sizeof *slots; i++)
      if (slots[i].ctx == ctx) {
         if (rd == &slots[i])
            slots[i].dead = 1; /* still current here: it lives until this thread lets go of it */
         else
            memset(&slots[i], 0, sizeof slots[i]);
      }
   pthread_mutex_unlock(&slot_lock);
   return r.eglDestroyContext(dpy, ctx);
}

/* While a redirect is on, the application's framebuffer 0 is ours. */
void glBindFramebuffer(unsigned target, unsigned fb)
{
   if (__builtin_expect(!r.glBindFramebuffer, 0)) {
      init();
      if (!r.glBindFramebuffer)
         return;
   }
   if (__builtin_expect(rd != NULL, 0) && fb == 0)
      fb = rd->fbo;
   r.glBindFramebuffer(target, fb);
}

void glDeleteFramebuffers(int n, const unsigned *ids)
{
   init();
   if (!g.glDeleteFramebuffers)
      return;
   g.glDeleteFramebuffers(n, ids);
   if (rd) { /* a deleted framebuffer that was bound leaves 0 bound */
      int draw = 0, read = 0;
      r.glGetIntegerv(GL_FRAMEBUFFER_BINDING, &draw);
      if (rd->es3) {
         r.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
         if (!draw)
            r.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, rd->fbo);
         if (!read)
            r.glBindFramebuffer(GL_READ_FRAMEBUFFER, rd->fbo);
      } else if (!draw)
         r.glBindFramebuffer(GL_FRAMEBUFFER, rd->fbo);
   }
}

/* 1 when the framebuffer bound at target is the redirect's. */
static int rd_bound(unsigned target)
{
   int fb = 0;
   if (!rd)
      return 0;
   r.glGetIntegerv(target == GL_READ_FRAMEBUFFER && rd->es3 ? GL_READ_FRAMEBUFFER_BINDING : GL_FRAMEBUFFER_BINDING, &fb);
   return (unsigned)fb == rd->fbo;
}

/* The window's buffers are named GL_COLOR / GL_DEPTH / GL_STENCIL / GL_BACK; ours are attachments. */
static unsigned rd_attachment(unsigned a)
{
   return a == GL_COLOR_EXT || a == GL_BACK ? GL_COLOR_ATTACHMENT0 : a == GL_DEPTH_EXT ? GL_DEPTH_ATTACHMENT :
          a == GL_STENCIL_EXT ? GL_STENCIL_ATTACHMENT : a;
}

void glInvalidateFramebuffer(unsigned target, int n, const unsigned *att)
{
   unsigned mine[8];
   init();
   if (!r.glInvalidateFramebuffer)
      return;
   if (att && n > 0 && n <= 8 && rd_bound(target)) {
      for (int i = 0; i < n; i++)
         mine[i] = rd_attachment(att[i]);
      att = mine;
   }
   r.glInvalidateFramebuffer(target, n, att);
}

void glInvalidateSubFramebuffer(unsigned target, int n, const unsigned *att, int x, int y, int w, int h)
{
   unsigned mine[8];
   init();
   if (!g.glInvalidateSubFramebuffer)
      return;
   if (att && n > 0 && n <= 8 && rd_bound(target)) {
      for (int i = 0; i < n; i++)
         mine[i] = rd_attachment(att[i]);
      att = mine;
   }
   g.glInvalidateSubFramebuffer(target, n, att, x, y, w, h);
}

void glDiscardFramebufferEXT(unsigned target, int n, const unsigned *att)
{
   unsigned mine[8];
   init();
   if (!r.glDiscardFramebufferEXT)
      return;
   if (att && n > 0 && n <= 8 && rd_bound(target)) {
      for (int i = 0; i < n; i++)
         mine[i] = rd_attachment(att[i]);
      att = mine;
   }
   r.glDiscardFramebufferEXT(target, n, att);
}

void glReadBuffer(unsigned src)
{
   init();
   if (!g.glReadBuffer)
      return;
   g.glReadBuffer(src == GL_BACK && rd_bound(GL_READ_FRAMEBUFFER) ? GL_COLOR_ATTACHMENT0 : src);
}

void glDrawBuffers(int n, const unsigned *bufs)
{
   unsigned mine = GL_COLOR_ATTACHMENT0;
   init();
   if (!g.glDrawBuffers)
      return;
   if (bufs && n == 1 && bufs[0] == GL_BACK && rd_bound(GL_FRAMEBUFFER))
      bufs = &mine;
   g.glDrawBuffers(n, bufs);
}

void glGetFramebufferAttachmentParameteriv(unsigned target, unsigned att, unsigned pname, int *params)
{
   init();
   if (!g.glGetFramebufferAttachmentParameteriv)
      return;
   const int mine = rd_bound(target) && rd_attachment(att) != att;
   g.glGetFramebufferAttachmentParameteriv(target, mine ? rd_attachment(att) : att, pname, params);
   if (mine && pname == GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE && params)
      *params = GL_FRAMEBUFFER_DEFAULT;
}

EGLBoolean eglReleaseThread(void)
{
   init();
   if (!r.eglReleaseThread)
      return 0;
   if (rd)
      rd = NULL;
   else
      drop_window_depth(0);
   frame_end();
   return r.eglReleaseThread();
}

/* ---- fences ---- */

/* Waiting for a fence that has not signaled (or polling it with the flush flag) makes the blob flush the
 * window's frame in progress, like glFinish. A fence that has signaled needs nothing, so ask first,
 * without the flush flag. */
static void before_gl_wait(void *sync)
{
   if (!have_window() || !r.glClientWaitSync)
      return;
   const unsigned st = r.glClientWaitSync(sync, 0, 0);
   if (st == GL_ALREADY_SIGNALED || st == GL_CONDITION_SATISFIED)
      return;
   trace("wait for an unsignaled fence");
   drop_window_depth(0);
}

static void before_egl_wait(EGLDisplay dpy, EGLSync sync)
{
   if (!have_window() || !r.eglClientWaitSyncKHR)
      return;
   if (r.eglClientWaitSyncKHR(dpy, sync, 0, 0) == EGL_CONDITION_SATISFIED_KHR)
      return;
   trace("wait for an unsignaled EGL fence");
   drop_window_depth(0);
}

/* Making a fence is enough: the blob then flushes the frame in progress by itself a few milliseconds
 * later, unless the swap comes first (found with a fence made before the swap and never waited for:
 * with 5 ms between the two, every frame faulted and came out black). */
void *glFenceSync(unsigned condition, unsigned flags)
{
   init();
   if (!r.glFenceSync)
      return NULL;
   drop_window_depth(0);
   return r.glFenceSync(condition, flags);
}

EGLSync eglCreateSyncKHR(EGLDisplay dpy, EGLenum type, const EGLint *attribs)
{
   init();
   if (!r.eglCreateSyncKHR)
      return NULL;
   if (type == EGL_SYNC_FENCE_KHR || type == EGL_SYNC_NATIVE_FENCE_ANDROID)
      drop_window_depth(0);
   return r.eglCreateSyncKHR(dpy, type, attribs);
}

EGLSync eglCreateSync(EGLDisplay dpy, EGLenum type, const intptr_t *attribs)
{
   init();
   if (!r.eglCreateSync)
      return NULL;
   if (type == EGL_SYNC_FENCE_KHR || type == EGL_SYNC_NATIVE_FENCE_ANDROID)
      drop_window_depth(0);
   return r.eglCreateSync(dpy, type, attribs);
}

unsigned glClientWaitSync(void *sync, unsigned flags, uint64_t timeout)
{
   init();
   if (!r.glClientWaitSync)
      return 0;
   before_gl_wait(sync);
   return r.glClientWaitSync(sync, flags, timeout);
}

void glWaitSync(void *sync, unsigned flags, uint64_t timeout)
{
   init();
   if (!r.glWaitSync)
      return;
   before_gl_wait(sync);
   r.glWaitSync(sync, flags, timeout);
}

EGLint eglClientWaitSyncKHR(EGLDisplay dpy, EGLSync sync, EGLint flags, uint64_t timeout)
{
   init();
   if (!r.eglClientWaitSyncKHR)
      return 0;
   before_egl_wait(dpy, sync);
   return r.eglClientWaitSyncKHR(dpy, sync, flags, timeout);
}

EGLint eglClientWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags, uint64_t timeout)
{
   init();
   if (!r.eglClientWaitSync)
      return 0;
   before_egl_wait(dpy, sync);
   return r.eglClientWaitSync(dpy, sync, flags, timeout);
}

EGLint eglWaitSyncKHR(EGLDisplay dpy, EGLSync sync, EGLint flags)
{
   init();
   if (!r.eglWaitSyncKHR)
      return 0;
   before_egl_wait(dpy, sync);
   return r.eglWaitSyncKHR(dpy, sync, flags);
}

EGLBoolean eglWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags)
{
   init();
   if (!r.eglWaitSync)
      return 0;
   before_egl_wait(dpy, sync);
   return r.eglWaitSync(dpy, sync, flags);
}

/* ---- Wayland: the modifier list a compositor passes to its clients ---- */

/* The blob lists AFBC modifiers for dma-buf import. A compositor passes that list to its clients, whose
 * window buffers then become AFBC render targets (a fault on every frame). Hide every Arm vendor
 * modifier (AFBC, AFRC), so only linear buffers get negotiated. */
EGLBoolean eglQueryDmaBufModifiersEXT(EGLDisplay dpy, EGLint format, EGLint max, uint64_t *mods, EGLBoolean *ext,
                                      EGLint *num)
{
   init();
   if (!r.eglQueryDmaBufModifiersEXT)
      return 0;
   if (!enabled || !hide_mods)
      return r.eglQueryDmaBufModifiersEXT(dpy, format, max, mods, ext, num);
   EGLint n = 0, got = 0, out = 0;
   if (!r.eglQueryDmaBufModifiersEXT(dpy, format, 0, NULL, NULL, &n))
      return 0;
   uint64_t *m = calloc(n > 0 ? n : 1, sizeof *m);
   EGLBoolean *e = calloc(n > 0 ? n : 1, sizeof *e);
   if (!m || !e || (n > 0 && !r.eglQueryDmaBufModifiersEXT(dpy, format, n, m, e, &got))) {
      free(m);
      free(e);
      return 0;
   }
   int dropped = 0;
   EGLBoolean last_ext = 0;
   for (EGLint i = 0; i < got; i++) {
      if ((m[i] >> 56) == 0x08) { /* DRM_FORMAT_MOD_VENDOR_ARM */
         n_mod++;
         dropped++;
         continue;
      }
      if (max > 0 && out < max) {
         if (mods) mods[out] = m[i];
         if (ext) ext[out] = e[i];
      }
      last_ext = e[i];
      out++;
   }
   if (!out && got > 0) { /* only compressed layouts were listed: offer linear */
      if (max > 0) {
         if (mods) mods[0] = 0;
         if (ext) ext[0] = 0;
      }
      out = 1;
   }
   /* wlroots sends a linux-dmabuf v3 client (the blob's Wayland side binds v3) only "unspecified" when a
    * format has just the linear layout, and the blob then finds no window config. A second, private
    * modifier that nobody can pick keeps "linear" named explicitly. Prototype hack: the clean place for
    * this is the compositor. */
   if (dropped && out == 1) {
      if (max > 1) {
         if (mods) mods[1] = (0xfeULL << 56) | 0xafbcf1;
         if (ext) ext[1] = last_ext;
      }
      out = 2;
   }
   if (debug)
      fprintf(stderr, "[afbcfix] format %.4s: %d modifiers from the blob, %d passed on\n", (char *)&format, got, out);
   if (num)
      *num = max > 0 && out > max ? max : out;
   free(m);
   free(e);
   return 1;
}

fnptr eglGetProcAddress(const char *name)
{
   static const struct {
      const char *name;
      fnptr fn;
   } mine[] = {
      {"eglQueryDmaBufModifiersEXT", (fnptr)eglQueryDmaBufModifiersEXT},
      {"eglWaitGL", (fnptr)eglWaitGL},
      {"eglWaitClient", (fnptr)eglWaitClient},
      {"eglMakeCurrent", (fnptr)eglMakeCurrent},
      {"eglSwapBuffers", (fnptr)eglSwapBuffers},
      {"eglReleaseThread", (fnptr)eglReleaseThread},
      {"eglDestroyContext", (fnptr)eglDestroyContext},
      {"eglCreateSyncKHR", (fnptr)eglCreateSyncKHR},
      {"eglCreateSync", (fnptr)eglCreateSync},
      {"eglClientWaitSyncKHR", (fnptr)eglClientWaitSyncKHR},
      {"eglClientWaitSync", (fnptr)eglClientWaitSync},
      {"eglWaitSyncKHR", (fnptr)eglWaitSyncKHR},
      {"eglWaitSync", (fnptr)eglWaitSync},
      {"glTexImage2D", (fnptr)glTexImage2D},
      {"glTexStorage2D", (fnptr)glTexStorage2D},
      {"glTexStorage2DEXT", (fnptr)glTexStorage2D},
      {"glCopyTexImage2D", (fnptr)glCopyTexImage2D},
      {"glTexImage3D", (fnptr)glTexImage3D},
      {"glTexStorage3D", (fnptr)glTexStorage3D},
      {"glRenderbufferStorage", (fnptr)glRenderbufferStorage},
      {"glRenderbufferStorageOES", (fnptr)glRenderbufferStorageOES},
      {"glRenderbufferStorageMultisample", (fnptr)glRenderbufferStorageMultisample},
      {"glRenderbufferStorageMultisampleEXT", (fnptr)glRenderbufferStorageMultisampleEXT},
      {"glFramebufferTexture2DMultisampleEXT", (fnptr)glFramebufferTexture2DMultisampleEXT},
      {"glGetString", (fnptr)glGetString},
      {"glGetStringi", (fnptr)glGetStringi},
      {"glGetIntegerv", (fnptr)glGetIntegerv},
      {"glFinish", (fnptr)glFinish},
      {"glBindFramebuffer", (fnptr)glBindFramebuffer},
      {"glDeleteFramebuffers", (fnptr)glDeleteFramebuffers},
      {"glInvalidateFramebuffer", (fnptr)glInvalidateFramebuffer},
      {"glInvalidateSubFramebuffer", (fnptr)glInvalidateSubFramebuffer},
      {"glDiscardFramebufferEXT", (fnptr)glDiscardFramebufferEXT},
      {"glReadBuffer", (fnptr)glReadBuffer},
      {"glDrawBuffers", (fnptr)glDrawBuffers},
      {"glGetFramebufferAttachmentParameteriv", (fnptr)glGetFramebufferAttachmentParameteriv},
      {"glClear", (fnptr)glClear},
      {"glReadPixels", (fnptr)glReadPixels},
      {"glReadnPixels", (fnptr)glReadnPixels},
      {"glReadnPixelsEXT", (fnptr)glReadnPixels},
      {"glReadnPixelsKHR", (fnptr)glReadnPixels},
      {"glFenceSync", (fnptr)glFenceSync},
      {"glClientWaitSync", (fnptr)glClientWaitSync},
      {"glWaitSync", (fnptr)glWaitSync},
#define X(n, p, a) {#n, (fnptr)n},
      DRAWS(X)
#undef X
   };
   init();
   if (!name || !enabled)
      return r.eglGetProcAddress ? r.eglGetProcAddress(name) : NULL;
   /* not exported by the blob, so handed out only when the blob has them */
   for (int k = 0; k < 2; k++)
      if (!strcmp(name, k ? "eglSwapBuffersWithDamageEXT" : "eglSwapBuffersWithDamageKHR")) {
         fnptr p = r.eglGetProcAddress ? r.eglGetProcAddress(name) : NULL;
         if (!p)
            return NULL;
         r.swap_damage[k] = (EGLBoolean (*)(EGLDisplay, EGLSurface, const EGLint *, EGLint))p;
         return k ? (fnptr)swap_damage_ext : (fnptr)swap_damage_khr;
      }
   for (unsigned i = 0; i < sizeof mine / sizeof *mine; i++)
      if (!strcmp(name, mine[i].name))
         return mine[i].fn;
   return r.eglGetProcAddress ? r.eglGetProcAddress(name) : NULL;
}
