/* gbm_probe: headless GBM + EGL + GLES 2 check for a Mali blob (or Mesa),
 * the way a Wayland compositor's renderer starts: open the KMS device, make a
 * GBM device and an EGL display on it, render into a gbm_surface, read back.
 * No modeset, so it runs with the compositor stopped and touches no screen.
 *
 * Usage: gbm_probe [/dev/dri/cardN] [frames]   (default: first card with KMS, 300)
 * Libraries are dlopened: libgbm.so.1, libEGL.so.1, libGLESv2.so.2.
 *
 * Build: aarch64-linux-gnu-gcc -O2 -o gbm_probe gbm_probe.c -ldl
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

typedef void *EGLDisplay, *EGLConfig, *EGLSurface, *EGLContext;
typedef int32_t EGLint;
typedef unsigned int EGLBoolean, EGLenum;
#define EGL_NONE 0x3038
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_ALPHA_SIZE 0x3021
#define EGL_SURFACE_TYPE 0x3033
#define EGL_WINDOW_BIT 0x0004
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_NATIVE_VISUAL_ID 0x302E
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_VENDOR 0x3053
#define EGL_VERSION 0x3054
#define EGL_EXTENSIONS 0x3055
#define EGL_PLATFORM_GBM_KHR 0x31D7
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_TRIANGLES 0x0004
#define GL_FLOAT 0x1406
#define GBM_FORMAT_XRGB8888 0x34325258
#define GBM_FORMAT_ARGB8888 0x34325241
#define GBM_BO_USE_SCANOUT (1 << 0)
#define GBM_BO_USE_RENDERING (1 << 2)

/* DRM_IOCTL_VERSION, to find a KMS-capable card by driver name. */
struct drm_version_s {
   int major, minor, patch;
   size_t name_len;
   char *name;
   size_t date_len;
   char *date;
   size_t desc_len;
   char *desc;
};
#define DRM_IOCTL_VERSION_S _IOWR('d', 0x00, struct drm_version_s)

static double now_ms(void)
{
   struct timespec t;
   clock_gettime(CLOCK_MONOTONIC, &t);
   return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

#define F(ret, name, args) static ret(*name) args;
F(void *, gbm_create_device, (int))
F(void, gbm_device_destroy, (void *))
F(void *, gbm_surface_create, (void *, uint32_t, uint32_t, uint32_t, uint32_t))
F(void, gbm_surface_destroy, (void *))
F(void *, gbm_surface_lock_front_buffer, (void *))
F(void, gbm_surface_release_buffer, (void *, void *))
F(EGLDisplay, eglGetPlatformDisplay, (EGLenum, void *, const intptr_t *))
F(EGLDisplay, eglGetDisplay, (void *))
F(EGLBoolean, eglInitialize, (EGLDisplay, EGLint *, EGLint *))
F(const char *, eglQueryString, (EGLDisplay, EGLint))
F(EGLBoolean, eglChooseConfig, (EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *))
F(EGLBoolean, eglGetConfigAttrib, (EGLDisplay, EGLConfig, EGLint, EGLint *))
F(EGLSurface, eglCreateWindowSurface, (EGLDisplay, EGLConfig, void *, const EGLint *))
F(EGLContext, eglCreateContext, (EGLDisplay, EGLConfig, EGLContext, const EGLint *))
F(EGLBoolean, eglMakeCurrent, (EGLDisplay, EGLSurface, EGLSurface, EGLContext))
F(EGLBoolean, eglSwapBuffers, (EGLDisplay, EGLSurface))
F(EGLBoolean, eglTerminate, (EGLDisplay))
F(EGLint, eglGetError, (void))
F(EGLDisplay, eglGetCurrentDisplay, (void))
F(EGLContext, eglGetCurrentContext, (void))
F(const unsigned char *, glGetString, (unsigned))
F(void, glClearColor, (float, float, float, float))
F(void, glClear, (unsigned))
F(void, glViewport, (int, int, int, int))
F(void, glReadPixels, (int, int, int, int, unsigned, unsigned, void *))
F(void, glFinish, (void))
F(unsigned, glCreateShader, (unsigned))
F(void, glShaderSource, (unsigned, int, const char *const *, const int *))
F(void, glCompileShader, (unsigned))
F(unsigned, glCreateProgram, (void))
F(void, glAttachShader, (unsigned, unsigned))
F(void, glBindAttribLocation, (unsigned, unsigned, const char *))
F(void, glLinkProgram, (unsigned))
F(void, glUseProgram, (unsigned))
F(int, glGetUniformLocation, (unsigned, const char *))
F(void, glUniform1f, (int, float))
F(void, glVertexAttribPointer, (unsigned, int, unsigned, unsigned char, int, const void *))
F(void, glEnableVertexAttribArray, (unsigned))
F(void, glDrawArrays, (unsigned, int, int))
F(void, glEnable, (unsigned))
F(void, glGenTextures, (int, unsigned *))
F(void, glBindTexture, (unsigned, unsigned))
F(void, glTexParameteri, (unsigned, unsigned, int))
F(void, glTexImage2D, (unsigned, int, int, int, int, int, unsigned, unsigned, const void *))
F(void, glTexSubImage2D, (unsigned, int, int, int, int, int, unsigned, unsigned, const void *))
F(void, glCopyTexImage2D, (unsigned, int, unsigned, int, int, int, int, int))
F(void, glPixelStorei, (unsigned, int))
F(void, glGenerateMipmap, (unsigned))
F(unsigned, glGetError, (void))
F(void, glGenFramebuffers, (int, unsigned *))
F(void, glBindFramebuffer, (unsigned, unsigned))
F(void, glFramebufferTexture2D, (unsigned, unsigned, unsigned, unsigned, int))
F(unsigned, glCheckFramebufferStatus, (unsigned))
F(void, glGenRenderbuffers, (int, unsigned *))
F(void, glBindRenderbuffer, (unsigned, unsigned))
F(void, glRenderbufferStorage, (unsigned, unsigned, int, int))
F(void, glFramebufferRenderbuffer, (unsigned, unsigned, unsigned, unsigned))
F(void, glTexStorage2D, (unsigned, int, unsigned, int, int)) /* ES 3 only: may be NULL */
/* optional: a texture backed by a linear dma-buf (the way a compositor's render targets are made) */
F(void *, gbm_bo_create, (void *, uint32_t, uint32_t, uint32_t, uint32_t))
F(int, gbm_bo_get_fd, (void *))
F(uint32_t, gbm_bo_get_stride, (void *))
F(void *, eglCreateImageKHR, (EGLDisplay, EGLContext, EGLenum, void *, const EGLint *))
F(void, glEGLImageTargetTexture2DOES, (unsigned, void *))
F(EGLBoolean, eglDestroyImageKHR, (EGLDisplay, void *))
F(void, glDiscardFramebufferEXT, (unsigned, int, const unsigned *))
F(void, glInvalidateFramebuffer, (unsigned, int, const unsigned *))
/* optional, for the gap variants (ES 3 or extensions: may be NULL) */
F(void, glTexImage3D, (unsigned, int, int, int, int, int, int, unsigned, unsigned, const void *))
F(void, glTexSubImage3D, (unsigned, int, int, int, int, int, int, int, unsigned, unsigned, const void *))
F(void, glTexStorage3D, (unsigned, int, unsigned, int, int, int))
F(void, glFramebufferTextureLayer, (unsigned, unsigned, unsigned, int, int))
F(void, glRenderbufferStorageMultisample, (unsigned, int, unsigned, int, int))
F(void, glRenderbufferStorageMultisampleEXT, (unsigned, int, unsigned, int, int))
F(void, glFramebufferTexture2DMultisampleEXT, (unsigned, unsigned, unsigned, unsigned, int, int))
F(void, glBlitFramebuffer, (int, int, int, int, int, int, int, int, unsigned, unsigned))
F(void, glGenBuffers, (int, unsigned *))
F(void, glBindBuffer, (unsigned, unsigned))
F(void, glBufferData, (unsigned, intptr_t, const void *, unsigned))
F(void, glCompressedTexImage2D, (unsigned, int, unsigned, int, int, int, int, const void *))
F(void, glCopyTexSubImage2D, (unsigned, int, int, int, int, int, int, int))
F(void, glFlush, (void))
F(void, glDisable, (unsigned))
F(void, glGetProgramiv, (unsigned, unsigned, int *))
F(void, glGetProgramInfoLog, (unsigned, int, int *, char *))
F(EGLBoolean, eglSurfaceAttrib, (EGLDisplay, EGLSurface, EGLint, EGLint))
F(void, glBindImageTexture, (unsigned, unsigned, int, unsigned char, int, unsigned, unsigned))
F(void *, glFenceSync, (unsigned, unsigned))
F(unsigned, glClientWaitSync, (void *, unsigned, uint64_t))
F(void, glDeleteSync, (void *))
F(void *, eglCreateSyncKHR, (EGLDisplay, unsigned, const EGLint *))
F(EGLint, eglClientWaitSyncKHR, (EGLDisplay, void *, EGLint, uint64_t))
F(EGLBoolean, eglDestroySyncKHR, (EGLDisplay, void *))
#undef F
#define EGL_LINUX_DMA_BUF_EXT 0x3270
#define EGL_LINUX_DRM_FOURCC_EXT 0x3271
#define EGL_DMA_BUF_PLANE0_FD_EXT 0x3272
#define EGL_DMA_BUF_PLANE0_OFFSET_EXT 0x3273
#define EGL_DMA_BUF_PLANE0_PITCH_EXT 0x3274
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define GBM_FORMAT_ABGR8888 0x34324241
#define EGL_DEPTH_SIZE 0x3025
#define EGL_STENCIL_SIZE 0x3026
#define GL_DEPTH_BUFFER_BIT 0x0100
#define GL_STENCIL_BUFFER_BIT 0x0400
#define GL_DEPTH_TEST 0x0B71

#define L(h, n) if (!(n = dlsym(h, #n))) { fprintf(stderr, "missing %s\n", #n); return 1; }

/* GBMP_TEX variants, to map which GL usage makes a blob pick AFBC for a texture (black-box: GPU faults per run).
 *   GBMP_TEXFMT   rgba (default) rgb 565 4444 5551 l la a | ES 3 sized: rgba8 srgb8a8 r8 rg8 rgba16f rgb10a2
 *   GBMP_TEXSIZE  N or WxH (default 64)
 *   GBMP_TEXPATH  image (default: glTexImage2D with data) | sub (NULL, then one glTexSubImage2D) |
 *                 rows (NULL, then 8-row glTexSubImage2D calls) | storage (glTexStorage2D + sub, ES 3) |
 *                 perframe (sub every frame) | reimage (glTexImage2D every frame) | copy (glCopyTexImage2D) |
 *                 fbo (rendered into through an FBO once) | fboframe (rendered into every frame)
 *                 null (allocated, never filled) | sub1 (NULL, then the first 8 rows only) |
 *                 twice (image, then a full sub) | subtwice (NULL, then two full subs) |
 *                 tinyfirst (NULL, a 1x1 sub, then a full sub) | halves (NULL, top half, bottom half)
 *                 eglimg (linear dma-buf EGLImage texture, filled with a sub) |
 *                 fboimg (the same, rendered into every frame) | fbodyn (twice, then rendered into every frame)
 *                 selfimg (NULL, an EGLImage made FROM the texture and kept, then a full sub) |
 *                 selfimgdrop (the same, image destroyed before the upload) | fboself (selfimg, rendered into)
 *   GBMP_FBODEPTH also: 248 (D24S8 on both points) 32 (D32F, ES 3) 8 (stencil only)
 *   GBMP_FBODEPTHTEX=1    depth as a texture, not a renderbuffer (16 or 24)
 *   GBMP_FBODEPTHIMG=1    make an EGLImage from the depth renderbuffer
 *   GBMP_FBODISCARD=1     discard depth/stencil after each FBO pass
 *   GBMP_EXT=1            print the GL extension string
 *   GBMP_MODS=1           print the dma-buf formats and modifiers EGL lists (what a compositor passes to clients)
 *   GBMP_TEXMIP=gen       glGenerateMipmap + trilinear
 *   GBMP_TEXFILTER=linear GBMP_TEXWRAP=repeat
 *   GBMP_FBODEPTH=16|24   depth renderbuffer on the FBO (fbo paths)
 *   GBMP_ES=3             ES 3 context
 *   GBMP_SYNC=1           glFinish after the upload and after each of the first frames (to place a fault)
 *   GBMP_NOCHECK=1        skip the final mid-frame readback
 * Gap variants (2026-11-13, second round):
 *   GBMP_TEXPATH  cube (six faces with data) | cubesub (NULL, then subs) | cubestorage (ES 3) |
 *                 cubeself (NULL, an EGLImage from +X made and destroyed, subs) | cubeself6 (from all six) |
 *                 cubefbo (+X rendered into every frame) | cubefboself (the same after the +X image)
 *                 t3d | t3dstorage | t3dself (EGL_GL_TEXTURE_3D_KHR image)   3D texture, 4 slices (ES 3)
 *                 arr | arrstorage | arrfbo (layer 0 rendered into every frame)   2D array, 4 layers (ES 3)
 *                 arrself (the 3D image kind on an array: crashes g29p1) | arr2dimg (the 2D image kind) |
 *                 arrtwice (NULL, a 1x1 sub in each layer, then the data) | arrtwice1 (1x1 in layer 0 only) |
 *                 arrzero (NULL, a zero-size sub, then the data) | arrfbotwice (arrtwice, layer 0 rendered into) |
 *                 arrunit (storage, bound to an image unit once, then the data)
 *                 zerofirst (2D: NULL, a zero-size sub, then the data)
 *   GBMP_MSDEPTH=pre|img  fbomsrtt depth renderbuffer: EGLImage step on single-sample storage before the
 *                 multisample storage call (pre), or directly on the multisample renderbuffer (img)
 *                 pbo (glTexImage2D from a pixel unpack buffer) | pbosub (NULL, then a sub from the buffer) |
 *                 pbooff (the data at offset 4096 in the buffer)   (ES 3)
 *                 etc1 | etc2 (glCompressedTexImage2D, one solid block repeated; check expects r,g high)
 *                 fborb (FBO color = renderbuffer in GBMP_TEXFMT 565/4444/5551/rgba8; the check reads the FBO)
 *                 fbomsaa (ES 3: multisampled color + depth renderbuffers, blit-resolved into the texture)
 *                 fbomsrtt (EXT_multisampled_render_to_texture on the texture)
 *                 arrshadow (layer 0 of a depth 2D array rendered every frame and sampled; GBMP_FBODEPTH 16 24 32)
 *                 shadow (a depth texture rendered every frame and sampled; GBMP_FBODEPTH 16 24 32 248)
 *   GBMP_TEXMIP=manual|manualrev   every level uploaded with glTexImage2D, level 0 first / last
 *   GBMP_RBPRE=1          the EGLImage step on every renderbuffer name before it gets storage (it fails)
 *   GBMP_SELFIMG=1        fbomsaa, fbomsrtt: make + destroy an EGLImage from the texture first
 *   GBMP_FBOSAMPLES=N     samples for fbomsaa / fbomsrtt (default 4)
 *   GBMP_MIDFLUSH=read|copy|flush|finish|none   window with GBMP_DEPTH: two depth-tested draws per frame
 *                         with that call between them; GBMP_MIDORDER=farfirst (default) | nearfirst.
 *                         farfirst fails when depth comes back as near values (zeros), nearfirst when it
 *                         comes back as far values (cleared).
 *                         endread | endfinish | endfence (ES 3: fence + client wait): the call after both
 *                         draws, right before the swap
 *                         fence | fence0 (timeout 0) | eglfence | oldfence (the wait is for the fence made at
 *                         the end of the previous frame), each also with the end prefix (ES 3)
 *                         fenceonly: a fence made at the end of every frame and never waited for
 *   GBMP_PRESWAPDELAY=ms  sleep between the last draw and the swap
 *   GBMP_HOLD=1           keep each front buffer locked until the next one is locked (what a KMS client does)
 *   GBMP_MIDDISCARD=1     discard the window's depth + stencil right before that call
 *   GBMP_FRONTCHECK=1     also check the last swapped buffer itself (its dma-buf mapped), not only glReadPixels
 *   GBMP_SAMPLES=N        multisampled window config
 *   GBMP_PRESERVE=1       EGL_SWAP_BEHAVIOR = EGL_BUFFER_PRESERVED on the window */
struct texfmt {
   const char *name;
   unsigned ifmt, fmt, type;
   int bpp;
   const char *swz; /* which channels carry the x and y gradients */
   int es3;
};
static const struct texfmt texfmts[] = {
   {"rgba", 0x1908, 0x1908, 0x1401, 4, "rg", 0},     {"rgb", 0x1907, 0x1907, 0x1401, 3, "rg", 0},
   {"565", 0x1907, 0x1907, 0x8363, 2, "rg", 0},      {"4444", 0x1908, 0x1908, 0x8033, 2, "rg", 0},
   {"5551", 0x1908, 0x1908, 0x8034, 2, "rg", 0},     {"l", 0x1909, 0x1909, 0x1401, 1, "rr", 0},
   {"la", 0x190A, 0x190A, 0x1401, 2, "ra", 0},       {"a", 0x1906, 0x1906, 0x1401, 1, "aa", 0},
   {"rgba8", 0x8058, 0x1908, 0x1401, 4, "rg", 1},    {"srgb8a8", 0x8C43, 0x1908, 0x1401, 4, "rg", 1},
   {"r8", 0x8229, 0x1903, 0x1401, 1, "rr", 1},       {"rg8", 0x822B, 0x8227, 0x1401, 2, "rg", 1},
   {"rgba16f", 0x881A, 0x1908, 0x140B, 8, "rg", 1},  {"rgb10a2", 0x8059, 0x1908, 0x8368, 4, "rg", 1},
};

static uint16_t f2h(float v) /* float in [0,1] to half float */
{
   union { float f; uint32_t u; } x = {v};
   int e = (int)((x.u >> 23) & 0xff) - 127 + 15;
   return v <= 0 || e <= 0 ? 0 : (uint16_t)(e << 10 | ((x.u >> 13) & 0x3ff));
}

/* x gradient in the first channel, y gradient in the second (or their mean for one-channel formats) */
static void *tex_pixels(const struct texfmt *f, int w, int h)
{
   uint8_t *buf = malloc((size_t)w * h * f->bpp), *p = buf;
   for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++, p += f->bpp) {
         unsigned X = w > 1 ? x * 255 / (w - 1) : 0, Y = h > 1 ? y * 255 / (h - 1) : 0;
         uint16_t v16 = 0;
         uint32_t v32;
         switch (f->type) {
         case 0x8363: v16 = (uint16_t)((X >> 3) << 11 | (Y >> 2) << 5 | 16); memcpy(p, &v16, 2); break;
         case 0x8033: v16 = (uint16_t)((X >> 4) << 12 | (Y >> 4) << 8 | 8 << 4 | 15); memcpy(p, &v16, 2); break;
         case 0x8034: v16 = (uint16_t)((X >> 3) << 11 | (Y >> 3) << 6 | 16 << 1 | 1); memcpy(p, &v16, 2); break;
         case 0x8368: v32 = (X * 1023 / 255) | (Y * 1023 / 255) << 10 | 512u << 20 | 3u << 30; memcpy(p, &v32, 4); break;
         case 0x140B: {
            uint16_t hf[4] = {f2h(X / 255.0f), f2h(Y / 255.0f), f2h(0.5f), f2h(1.0f)};
            memcpy(p, hf, 8);
            break;
         }
         default:
            if (f->bpp == 1)
               p[0] = (uint8_t)((X + Y) / 2);
            else {
               p[0] = (uint8_t)X;
               p[1] = (uint8_t)Y;
               if (f->bpp > 2) p[2] = 128;
               if (f->bpp > 3) p[3] = 255;
            }
         }
      }
   return buf;
}

/* Bind a linear dma-buf (GBM buffer -> EGLImage) to the current GL_TEXTURE_2D. */
static int image_texture(void *gbm, EGLDisplay dpy, int w, int h)
{
   if (!gbm_bo_create || !gbm_bo_get_fd || !gbm_bo_get_stride || !eglCreateImageKHR || !glEGLImageTargetTexture2DOES) {
      fprintf(stderr, "no dma-buf image path in these libraries\n");
      return 1;
   }
   uint32_t fourcc = GBM_FORMAT_ABGR8888;
   void *bo = gbm_bo_create(gbm, w, h, fourcc, GBM_BO_USE_RENDERING);
   if (!bo) {
      fourcc = GBM_FORMAT_ARGB8888;
      bo = gbm_bo_create(gbm, w, h, fourcc, GBM_BO_USE_RENDERING);
   }
   if (!bo) {
      fprintf(stderr, "gbm_bo_create failed\n");
      return 1;
   }
   const EGLint at[] = {EGL_WIDTH, w, EGL_HEIGHT, h, EGL_LINUX_DRM_FOURCC_EXT, (EGLint)fourcc,
                        EGL_DMA_BUF_PLANE0_FD_EXT, gbm_bo_get_fd(bo), EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
                        EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLint)gbm_bo_get_stride(bo), EGL_NONE};
   void *im = eglCreateImageKHR(dpy, NULL, EGL_LINUX_DMA_BUF_EXT, NULL, at);
   if (!im) {
      fprintf(stderr, "eglCreateImageKHR failed %#x\n", eglGetError());
      return 1;
   }
   glEGLImageTargetTexture2DOES(0x0DE1, im);
   printf("dma-buf image texture: fourcc %.4s stride %u, gl error %#x\n", (char *)&fourcc, gbm_bo_get_stride(bo), glGetError());
   return 0;
}

/* The EGLImage step on the texture bound to a target: make an image from it and destroy it again. */
static void self_image(EGLDisplay dpy, EGLContext c, unsigned kind, unsigned tex, const char *what)
{
   static const EGLint z0[] = {0x30BD /* EGL_GL_TEXTURE_ZOFFSET_KHR */, 0, EGL_NONE};
   void *im = eglCreateImageKHR ? eglCreateImageKHR(dpy, c, kind, (void *)(uintptr_t)tex, kind == 0x30B2 ? z0 : NULL) : NULL;
   printf("image of %s: %p (egl error %#x)\n", what, im, im ? 0x3000 : eglGetError());
   if (im && eglDestroyImageKHR)
      eglDestroyImageKHR(dpy, im);
}

/* Depth/stencil renderbuffer on the bound FBO: bits 16 24 32 (D32F) 248 (D24S8) 8 (stencil only). */
static void attach_depth_rb(int bits, int w, int h, int samples, int ext_ms)
{
   unsigned rb = 0;
   const unsigned f = bits == 16 ? 0x81A5 : bits == 24 ? 0x81A6 : bits == 32 ? 0x8CAC : bits == 248 ? 0x88F0 : 0x8D48;
   glGenRenderbuffers(1, &rb);
   glBindRenderbuffer(0x8D41, rb);
   if (getenv("GBMP_RBPRE")) self_image(eglGetCurrentDisplay(), eglGetCurrentContext(), 0x30B9, rb, "the empty renderbuffer");
   const char *mst = getenv("GBMP_MSDEPTH");
   if (samples && ext_ms && glRenderbufferStorageMultisampleEXT) {
      if (mst && !strcmp(mst, "pre")) {
         glRenderbufferStorage(0x8D41, f, w, h);
         self_image(eglGetCurrentDisplay(), eglGetCurrentContext(), 0x30B9, rb, "the depth renderbuffer (single-sample)");
      }
      glRenderbufferStorageMultisampleEXT(0x8D41, samples, f, w, h);
      if (mst && !strcmp(mst, "img"))
         self_image(eglGetCurrentDisplay(), eglGetCurrentContext(), 0x30B9, rb, "the depth renderbuffer (multisample)");
   }
   else if (samples && glRenderbufferStorageMultisample)
      glRenderbufferStorageMultisample(0x8D41, samples, f, w, h);
   else
      glRenderbufferStorage(0x8D41, f, w, h);
   if (bits != 8)
      glFramebufferRenderbuffer(0x8D40, 0x8D00 /* DEPTH_ATTACHMENT */, 0x8D41, rb);
   if (bits == 248 || bits == 8)
      glFramebufferRenderbuffer(0x8D40, 0x8D20 /* STENCIL_ATTACHMENT */, 0x8D41, rb);
   glEnable(GL_DEPTH_TEST);
}

static int open_kms(const char *want)
{
   if (want)
      return open(want, O_RDWR | O_CLOEXEC);
   for (int i = 0; i < 4; i++) {
      char p[32], name[64] = "";
      snprintf(p, sizeof p, "/dev/dri/card%d", i);
      int fd = open(p, O_RDWR | O_CLOEXEC);
      if (fd < 0)
         continue;
      struct drm_version_s v = {0};
      v.name_len = sizeof name - 1;
      v.name = name;
      if (ioctl(fd, DRM_IOCTL_VERSION_S, &v) == 0)
         name[v.name_len < sizeof name ? v.name_len : sizeof name - 1] = 0;
      printf("%s: %s\n", p, name);
      /* Skip render-only GPU drivers; we want the display device. */
      if (strcmp(name, "panfrost") && strcmp(name, "lima") && strcmp(name, "panthor")) {
         printf("using %s\n", p);
         return fd;
      }
      close(fd);
   }
   return -1;
}

/* GBMP_MIDFLUSH fence kinds: a fence of this frame waited for (blocking, or polled with timeout 0), the
 * EGL form, or a wait for the fence made at the end of the previous frame. */
static void *old_sync;
static unsigned char mid_px[4];
static unsigned fence_rc;
static void mid_fence(const char *m, EGLDisplay dpy)
{
   if (strstr(m, "fenceonly")) { /* a fence made at the end of every frame, never waited for */
      if (old_sync && glDeleteSync) glDeleteSync(old_sync);
      old_sync = NULL;
   } else if (strstr(m, "oldfence")) {
      if (old_sync) {
         fence_rc = glClientWaitSync(old_sync, 1, 1000000000ull);
         if (glDeleteSync) glDeleteSync(old_sync);
         old_sync = NULL;
      }
   } else if (strstr(m, "eglfence")) {
      if (!eglCreateSyncKHR || !eglClientWaitSyncKHR) { fence_rc = 0xdead; return; }
      void *sy = eglCreateSyncKHR(dpy, 0x30F9 /* EGL_SYNC_FENCE_KHR */, NULL);
      fence_rc = (unsigned)eglClientWaitSyncKHR(dpy, sy, 1, 1000000000ull);
      if (eglDestroySyncKHR) eglDestroySyncKHR(dpy, sy);
   } else {
      void *sy = glFenceSync(0x9117 /* SYNC_GPU_COMMANDS_COMPLETE */, 0);
      fence_rc = glClientWaitSync(sy, 1 /* SYNC_FLUSH_COMMANDS_BIT */, strstr(m, "fence0") ? 0 : 1000000000ull);
      if (glDeleteSync) glDeleteSync(sy);
   }
}

/* Step markers on stderr, unbuffered, so a hard lock still shows how far it got over SSH. */
#define STEP(...) do { fprintf(stderr, "[%.1f] ", now_ms()); fprintf(stderr, __VA_ARGS__); fputc(10, stderr); } while (0)

int main(int argc, char **argv)
{
   setvbuf(stdout, NULL, _IONBF, 0);
   setvbuf(stderr, NULL, _IONBF, 0);
   STEP("dlopen");
   const char *dev = argc > 1 && argv[1][0] == '/' ? argv[1] : NULL;
   int frames = argc > 2 ? atoi(argv[2]) : (argc > 1 && argv[1][0] != '/' ? atoi(argv[1]) : 300);
   void *gbm_l = dlopen("libgbm.so.1", RTLD_NOW | RTLD_GLOBAL);
   void *egl_l = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
   void *gl_l = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_GLOBAL);
   if (!gbm_l || !egl_l || !gl_l) {
      fprintf(stderr, "dlopen: %s\n", dlerror());
      return 1;
   }
   L(gbm_l, gbm_create_device) L(gbm_l, gbm_device_destroy) L(gbm_l, gbm_surface_create)
   L(gbm_l, gbm_surface_destroy) L(gbm_l, gbm_surface_lock_front_buffer) L(gbm_l, gbm_surface_release_buffer)
   eglGetPlatformDisplay = dlsym(egl_l, "eglGetPlatformDisplay");
   L(egl_l, eglGetDisplay) L(egl_l, eglInitialize) L(egl_l, eglQueryString) L(egl_l, eglChooseConfig)
   L(egl_l, eglGetConfigAttrib) L(egl_l, eglCreateWindowSurface) L(egl_l, eglCreateContext)
   L(egl_l, eglMakeCurrent) L(egl_l, eglSwapBuffers) L(egl_l, eglTerminate) L(egl_l, eglGetError)
   L(egl_l, eglGetCurrentDisplay) L(egl_l, eglGetCurrentContext)
   L(gl_l, glGetString) L(gl_l, glClearColor) L(gl_l, glClear) L(gl_l, glViewport) L(gl_l, glReadPixels)
   L(gl_l, glFinish) L(gl_l, glCreateShader) L(gl_l, glShaderSource) L(gl_l, glCompileShader)
   L(gl_l, glCreateProgram) L(gl_l, glAttachShader) L(gl_l, glBindAttribLocation) L(gl_l, glLinkProgram)
   L(gl_l, glUseProgram) L(gl_l, glGetUniformLocation) L(gl_l, glUniform1f) L(gl_l, glVertexAttribPointer)
   L(gl_l, glEnableVertexAttribArray) L(gl_l, glDrawArrays) L(gl_l, glEnable)
   L(gl_l, glGenTextures) L(gl_l, glBindTexture) L(gl_l, glTexParameteri) L(gl_l, glTexImage2D)
   L(gl_l, glTexSubImage2D) L(gl_l, glCopyTexImage2D) L(gl_l, glPixelStorei) L(gl_l, glGenerateMipmap)
   L(gl_l, glGetError) L(gl_l, glGenFramebuffers) L(gl_l, glBindFramebuffer) L(gl_l, glFramebufferTexture2D)
   L(gl_l, glCheckFramebufferStatus) L(gl_l, glGenRenderbuffers) L(gl_l, glBindRenderbuffer)
   L(gl_l, glRenderbufferStorage) L(gl_l, glFramebufferRenderbuffer)
   glTexStorage2D = dlsym(gl_l, "glTexStorage2D");
   gbm_bo_create = dlsym(gbm_l, "gbm_bo_create");
   gbm_bo_get_fd = dlsym(gbm_l, "gbm_bo_get_fd");
   gbm_bo_get_stride = dlsym(gbm_l, "gbm_bo_get_stride");
   eglCreateImageKHR = dlsym(egl_l, "eglCreateImageKHR");
   glEGLImageTargetTexture2DOES = dlsym(gl_l, "glEGLImageTargetTexture2DOES");
   eglDestroyImageKHR = dlsym(egl_l, "eglDestroyImageKHR");
   glDiscardFramebufferEXT = dlsym(gl_l, "glDiscardFramebufferEXT");
   glInvalidateFramebuffer = dlsym(gl_l, "glInvalidateFramebuffer");
   glTexImage3D = dlsym(gl_l, "glTexImage3D");
   glTexSubImage3D = dlsym(gl_l, "glTexSubImage3D");
   glTexStorage3D = dlsym(gl_l, "glTexStorage3D");
   glFramebufferTextureLayer = dlsym(gl_l, "glFramebufferTextureLayer");
   glRenderbufferStorageMultisample = dlsym(gl_l, "glRenderbufferStorageMultisample");
   glRenderbufferStorageMultisampleEXT = dlsym(gl_l, "glRenderbufferStorageMultisampleEXT");
   glFramebufferTexture2DMultisampleEXT = dlsym(gl_l, "glFramebufferTexture2DMultisampleEXT");
   glBlitFramebuffer = dlsym(gl_l, "glBlitFramebuffer");
   glGenBuffers = dlsym(gl_l, "glGenBuffers");
   glBindBuffer = dlsym(gl_l, "glBindBuffer");
   glBufferData = dlsym(gl_l, "glBufferData");
   glCompressedTexImage2D = dlsym(gl_l, "glCompressedTexImage2D");
   glCopyTexSubImage2D = dlsym(gl_l, "glCopyTexSubImage2D");
   glFlush = dlsym(gl_l, "glFlush");
   glDisable = dlsym(gl_l, "glDisable");
   glGetProgramiv = dlsym(gl_l, "glGetProgramiv");
   glGetProgramInfoLog = dlsym(gl_l, "glGetProgramInfoLog");
   eglSurfaceAttrib = dlsym(egl_l, "eglSurfaceAttrib");
   glBindImageTexture = dlsym(gl_l, "glBindImageTexture");
   glFenceSync = dlsym(gl_l, "glFenceSync");
   glClientWaitSync = dlsym(gl_l, "glClientWaitSync");
   glDeleteSync = dlsym(gl_l, "glDeleteSync");
   eglCreateSyncKHR = dlsym(egl_l, "eglCreateSyncKHR");
   eglClientWaitSyncKHR = dlsym(egl_l, "eglClientWaitSyncKHR");
   eglDestroySyncKHR = dlsym(egl_l, "eglDestroySyncKHR");
   const int es = getenv("GBMP_ES") ? atoi(getenv("GBMP_ES")) : 2;
   /* GBMP_DEPTH=<bits> / GBMP_STENCIL=<bits>: ask for depth/stencil in the EGL config and clear them
    * each frame (depth test on), to bisect features a real client uses. */
   const int depth = getenv("GBMP_DEPTH") ? atoi(getenv("GBMP_DEPTH")) : 0;
   const int stencil = getenv("GBMP_STENCIL") ? atoi(getenv("GBMP_STENCIL")) : 0;
   const unsigned clear_bits = GL_COLOR_BUFFER_BIT | (depth ? GL_DEPTH_BUFFER_BIT : 0) | (stencil ? GL_STENCIL_BUFFER_BIT : 0);

   STEP("open_kms");
   int fd = open_kms(dev);
   if (fd < 0) {
      fprintf(stderr, "no KMS device\n");
      return 1;
   }
   STEP("gbm_create_device");
   void *gbm = gbm_create_device(fd);
   if (!gbm) {
      fprintf(stderr, "gbm_create_device failed\n");
      return 1;
   }
   EGLDisplay dpy = eglGetPlatformDisplay ? eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL) : NULL;
   if (!dpy)
      dpy = eglGetDisplay(gbm);
   STEP("eglInitialize");
   EGLint maj = 0, min = 0;
   if (!dpy || !eglInitialize(dpy, &maj, &min)) {
      fprintf(stderr, "EGL init failed %#x\n", eglGetError());
      return 1;
   }
   printf("EGL %d.%d vendor '%s' version '%s'\nEGL extensions: %s\n", maj, min, eglQueryString(dpy, EGL_VENDOR),
          eglQueryString(dpy, EGL_VERSION), eglQueryString(dpy, EGL_EXTENSIONS));

   if (getenv("GBMP_MODS")) {
      EGLBoolean (*qf)(EGLDisplay, EGLint, EGLint *, EGLint *) = dlsym(egl_l, "eglQueryDmaBufFormatsEXT");
      EGLBoolean (*qm)(EGLDisplay, EGLint, EGLint, uint64_t *, EGLBoolean *, EGLint *) =
         dlsym(egl_l, "eglQueryDmaBufModifiersEXT");
      EGLint nf = 0, fm[160];
      if (qf && qm && qf(dpy, 0, NULL, &nf) && qf(dpy, nf > 160 ? 160 : nf, fm, &nf))
         for (int i = 0; i < nf; i++) {
            EGLint nm = 0;
            uint64_t m[96];
            EGLBoolean e[96];
            qm(dpy, fm[i], 0, NULL, NULL, &nm);
            int total = nm;
            qm(dpy, fm[i], nm > 96 ? 96 : nm, m, e, &nm);
            int arm = 0;
            printf("format %.4s: %d modifiers:", (char *)&fm[i], total);
            for (int j = 0; j < nm; j++)
               if ((m[j] >> 56) == 0x08)
                  arm++;
               else
                  printf(" %#llx%s", (unsigned long long)m[j], e[j] ? "(ext)" : "");
            printf(" + %d Arm (AFBC/AFRC)\n", arm);
         }
      else
         printf("no dma-buf format query\n");
   }

   const int wsamples = getenv("GBMP_SAMPLES") ? atoi(getenv("GBMP_SAMPLES")) : 0;
   const EGLint ca[] = {EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0,
                        EGL_DEPTH_SIZE, depth, EGL_STENCIL_SIZE, stencil,
                        0x3032 /* EGL_SAMPLE_BUFFERS */, wsamples ? 1 : 0, 0x3031 /* EGL_SAMPLES */, wsamples,
                        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, es >= 3 ? 0x0040 : EGL_OPENGL_ES2_BIT, EGL_NONE};
   EGLConfig cfgs[64];
   EGLint n = 0;
   STEP("eglChooseConfig");
   eglChooseConfig(dpy, ca, cfgs, 64, &n);
   EGLConfig cfg = NULL;
   uint32_t fmt = GBM_FORMAT_XRGB8888;
   for (int i = 0; i < n && !cfg; i++) {
      EGLint vid = 0;
      eglGetConfigAttrib(dpy, cfgs[i], EGL_NATIVE_VISUAL_ID, &vid);
      if ((uint32_t)vid == GBM_FORMAT_XRGB8888 || (uint32_t)vid == GBM_FORMAT_ARGB8888) {
         cfg = cfgs[i];
         fmt = vid;
      }
   }
   if (!cfg && n > 0)
      cfg = cfgs[0];
   if (!cfg) {
      fprintf(stderr, "no EGL config\n");
      return 1;
   }
   const int w = 640, h = 480;
   STEP("gbm_surface_create %#x", fmt);
   /* GBMP_RENDER_ONLY=1: rendering flag alone; default adds SCANOUT like a KMS client (kmscube, SDL kmsdrm). */
   uint32_t flags = getenv("GBMP_RENDER_ONLY") ? GBM_BO_USE_RENDERING : (GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
   void *gs = gbm_surface_create(gbm, w, h, fmt, flags);
   if (!gs) {
      fprintf(stderr, "gbm_surface_create failed\n");
      return 1;
   }
   STEP("eglCreateWindowSurface");
   EGLSurface s = eglCreateWindowSurface(dpy, cfg, gs, NULL);
   const EGLint cx[] = {EGL_CONTEXT_CLIENT_VERSION, es, EGL_NONE};
   STEP("eglCreateContext");
   EGLContext c = eglCreateContext(dpy, cfg, NULL, cx);
   if (!s || !c || !eglMakeCurrent(dpy, s, s, c)) {
      fprintf(stderr, "surface/context failed %#x\n", eglGetError());
      return 1;
   }
   printf("GL vendor '%s' renderer '%s' version '%s'\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER),
          glGetString(GL_VERSION));
   if (getenv("GBMP_PRESERVE") && eglSurfaceAttrib) {
      EGLBoolean pr = eglSurfaceAttrib(dpy, s, 0x3093 /* EGL_SWAP_BEHAVIOR */, 0x3094 /* EGL_BUFFER_PRESERVED */);
      printf("buffer preserved: %s (egl error %#x)\n", pr ? "set" : "refused", pr ? 0x3000 : eglGetError());
   }
   if (getenv("GBMP_EXT"))
      printf("GL extensions: %s\n", glGetString(0x1F03));

   static const char *vs = "attribute vec2 p; varying vec2 uv; void main(){ uv = p*0.5+0.5; gl_Position = vec4(p,0.0,1.0); }";
   static const char *fs_plain = "precision mediump float; varying vec2 uv; uniform float t;"
                                 "void main(){ gl_FragColor = vec4(uv.x, uv.y, 0.5+0.5*sin(t+uv.x*6.0), 1.0); }";
   static const char *fs_grad = "precision mediump float; varying vec2 uv;"
                                "void main(){ gl_FragColor = vec4(uv.x, uv.y, 0.5, 1.0); }";
   /* GBMP_TEX=1: the same gradient, but sampled from a texture (see the variants above open_kms). */
   const int use_tex = getenv("GBMP_TEX") != NULL;
   const int sync = getenv("GBMP_SYNC") != NULL;
   const char *tpath = getenv("GBMP_TEXPATH") ? getenv("GBMP_TEXPATH") : "image";
   const char *tfname = getenv("GBMP_TEXFMT") ? getenv("GBMP_TEXFMT") : "rgba";
   const int fbodepth = getenv("GBMP_FBODEPTH") ? atoi(getenv("GBMP_FBODEPTH")) : 0;
   int tw = 64, th = 64;
   if (getenv("GBMP_TEXSIZE")) {
      const char *e = getenv("GBMP_TEXSIZE"), *x = strchr(e, 'x');
      tw = th = atoi(e);
      if (x)
         th = atoi(x + 1);
   }
   /* gap variants: other targets and FBO kinds */
   const int p_cube = !strncmp(tpath, "cube", 4), p_3d = !strncmp(tpath, "t3d", 3), p_arr = !strncmp(tpath, "arr", 3);
   const int p_pbo = !strncmp(tpath, "pbo", 3), p_etc = !strncmp(tpath, "etc", 3), p_shadow = !strcmp(tpath, "shadow");
   const int p_arrshadow = !strcmp(tpath, "arrshadow");
   const int p_fborb = !strcmp(tpath, "fborb"), p_ms = !strcmp(tpath, "fbomsaa"), p_msrtt = !strcmp(tpath, "fbomsrtt");
   const int p_gapfbo = p_fborb || p_ms || p_msrtt || !strcmp(tpath, "cubefbo") || !strcmp(tpath, "cubefboself") ||
                        !strncmp(tpath, "arrfbo", 6);
   const int fbosamples = getenv("GBMP_FBOSAMPLES") ? atoi(getenv("GBMP_FBOSAMPLES")) : 4;
   const char *midflush = getenv("GBMP_MIDFLUSH");
   const int nearfirst = getenv("GBMP_MIDORDER") && !strcmp(getenv("GBMP_MIDORDER"), "nearfirst");
   const int mid_end = midflush && !strncmp(midflush, "end", 3), mid_discard = getenv("GBMP_MIDDISCARD") != NULL;
   int front_ok = -1; /* GBMP_FRONTCHECK: 1 pass, 0 fail, -1 not checked */
   const int p_fbo = !strncmp(tpath, "fbo", 3) && !p_gapfbo, p_copy = !strcmp(tpath, "copy");
   /* fboframe, fboimg, fbodyn and the gap FBO kinds: rendered into every frame */
   const int p_fboframe = (p_fbo && strcmp(tpath, "fbo")) || p_gapfbo;
   if (p_cube)
      th = tw;
#define SYNCSTEP(...) do { if (sync) { glFinish(); STEP(__VA_ARGS__); } } while (0)
   if (p_copy)
      tfname = "rgb"; /* the window has no alpha */
   if ((!strcmp(tpath, "storage") || !strcmp(tpath, "cubestorage") || p_3d || p_arr || p_ms) && !strcmp(tfname, "rgba"))
      tfname = "rgba8"; /* glTexStorage2D and the 3D calls need a sized format */
   if ((p_3d || p_arr || p_pbo || p_ms || !strcmp(tpath, "etc2") || !strcmp(tpath, "cubestorage")) && es < 3) {
      fprintf(stderr, "GBMP_TEXPATH %s needs GBMP_ES=3\n", tpath);
      return 1;
   }
   const struct texfmt *tf = NULL;
   for (unsigned i = 0; i < sizeof texfmts / sizeof *texfmts; i++)
      if (!strcmp(texfmts[i].name, tfname))
         tf = &texfmts[i];
   if (!tf || (tf->es3 && es < 3)) {
      fprintf(stderr, "bad GBMP_TEXFMT %s (ES 3 formats need GBMP_ES=3)\n", tfname);
      return 1;
   }
   static char fs_tex[640];
   static const char *vs3 = "#version 300 es\nin vec2 p; out vec2 uv; void main(){ uv = p*0.5+0.5; gl_Position = vec4(p,0.0,1.0); }";
   /* depth-tested passes: z from a uniform, or sloped along x (depth 0.25 .. 0.75 across the target) */
   static const char *vs_z = "attribute vec2 p; varying vec2 uv; uniform float z;"
                             "void main(){ uv = p*0.5+0.5; gl_Position = vec4(p,z,1.0); }";
   static const char *vs_zx = "attribute vec2 p; varying vec2 uv; void main(){ uv = p*0.5+0.5; gl_Position = vec4(p,p.x*0.5,1.0); }";
   static const char *fs_solid = "precision mediump float; void main(){ gl_FragColor = vec4(1.0, 0.0, 1.0, 1.0); }";
   if (p_cube) /* the +X face, laid out like the 2D texture */
      snprintf(fs_tex, sizeof fs_tex,
               "precision mediump float; varying vec2 uv; uniform float t; uniform samplerCube s;"
               "void main(){ gl_FragColor = vec4(textureCube(s, vec3(1.0, 1.0-2.0*uv.y, 1.0-2.0*uv.x)).%s, 0.5+0.5*sin(t), 1.0); }",
               tf->swz);
   else if (p_arrshadow)
      snprintf(fs_tex, sizeof fs_tex,
               "#version 300 es\nprecision mediump float; precision mediump sampler2DArray; in vec2 uv; uniform float t;"
               "uniform sampler2DArray s; out vec4 o;"
               "void main(){ o = vec4((texture(s, vec3(uv, 0.0)).r - 0.25) * 2.0, uv.y, 0.5+0.5*sin(t), 1.0); }");
   else if (p_3d || p_arr)
      snprintf(fs_tex, sizeof fs_tex,
               "#version 300 es\nprecision mediump float; precision mediump %s; in vec2 uv; uniform float t;"
               "uniform %s s; out vec4 o;"
               "void main(){ o = vec4(texture(s, vec3(uv, %s)).%s, 0.5+0.5*sin(t), 1.0); }",
               p_3d ? "sampler3D" : "sampler2DArray", p_3d ? "sampler3D" : "sampler2DArray", p_3d ? "0.125" : "0.0", tf->swz);
   else if (p_shadow)
      snprintf(fs_tex, sizeof fs_tex,
               "precision mediump float; varying vec2 uv; uniform float t; uniform sampler2D s;"
               "void main(){ gl_FragColor = vec4((texture2D(s, uv).r - 0.25) * 2.0, uv.y, 0.5+0.5*sin(t), 1.0); }");
   else
      snprintf(fs_tex, sizeof fs_tex,
               "precision mediump float; varying vec2 uv; uniform float t; uniform sampler2D s;"
               "void main(){ gl_FragColor = vec4(texture2D(s, uv).%s, 0.5+0.5*sin(t), 1.0); }", tf->swz);
   const char *vsrc[5] = {use_tex && (p_3d || p_arr) ? vs3 : vs, vs, vs_z, vs_z, vs_zx};
   const char *fsrc[5] = {use_tex ? fs_tex : fs_plain, fs_grad, fs_grad, fs_solid, fs_grad};
   unsigned progs[5];
   STEP("compile");
   for (int k = 0; k < 5; k++) {
      unsigned v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
      glShaderSource(v, 1, &vsrc[k], NULL);
      glShaderSource(f, 1, &fsrc[k], NULL);
      glCompileShader(v);
      glCompileShader(f);
      progs[k] = glCreateProgram();
      glAttachShader(progs[k], v);
      glAttachShader(progs[k], f);
      glBindAttribLocation(progs[k], 0, "p");
      glLinkProgram(progs[k]);
      int linked = 1;
      if (glGetProgramiv)
         glGetProgramiv(progs[k], 0x8B82 /* LINK_STATUS */, &linked);
      if (!linked) {
         char log[400] = "";
         if (glGetProgramInfoLog)
            glGetProgramInfoLog(progs[k], sizeof log - 1, NULL, log);
         fprintf(stderr, "program %d did not link: %s\n", k, log);
         return 1;
      }
   }
   const unsigned prog = progs[0], pgrad = progs[1], pnear = progs[2], pfar = progs[3], pshadow = progs[4];
   const int uz_near = glGetUniformLocation(pnear, "z"), uz_far = glGetUniformLocation(pfar, "z");
   static const float tri[] = {-1, -1, 3, -1, -1, 3};
   glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, tri);
   glEnableVertexAttribArray(0);

   void *img = NULL;
   unsigned tex = 0, fbo = 0, resolve_fbo = 0;
   int check_fbo = 0, expect_solid = 0;
   if (use_tex) {
      /* GL_TEXTURE_2D, or CUBE_MAP / 3D / 2D_ARRAY for the gap variants */
      const unsigned T = p_cube ? 0x8513 : p_3d ? 0x806F : p_arr ? 0x8C1A : 0x0DE1;
      const char *mipmode = getenv("GBMP_TEXMIP") ? getenv("GBMP_TEXMIP") : "";
      const int mip_manual = !strncmp(mipmode, "manual", 6);
      const int mip = !strcmp(mipmode, "gen") || mip_manual;
      const int linear = getenv("GBMP_TEXFILTER") && !strcmp(getenv("GBMP_TEXFILTER"), "linear");
      const int repeat = getenv("GBMP_TEXWRAP") && !strcmp(getenv("GBMP_TEXWRAP"), "repeat");
      img = tex_pixels(tf, tw, th);
      glPixelStorei(0x0CF5 /* UNPACK_ALIGNMENT */, 1);
      glGenTextures(1, &tex);
      glBindTexture(T, tex);
      glTexParameteri(T, 0x2801 /* MIN_FILTER */, mip ? 0x2703 : linear ? 0x2601 : 0x2600);
      glTexParameteri(T, 0x2800 /* MAG_FILTER */, linear ? 0x2601 : 0x2600);
      glTexParameteri(T, 0x2802 /* WRAP_S */, repeat ? 0x2901 : 0x812F);
      glTexParameteri(T, 0x2803 /* WRAP_T */, repeat ? 0x2901 : 0x812F);
      STEP("texture %s %dx%d path %s%s", tf->name, tw, th, tpath, mip ? " +mips" : "");
      /* GBMP_PREIMG=1|2d: the EGLImage step on the texture name before it has any storage (it fails;
       * does the blob still mark the texture?). 1 = the target's own kind (cube: +X), 2d = the 2D kind. */
      if (getenv("GBMP_PREIMG"))
         self_image(dpy, c, !strcmp(getenv("GBMP_PREIMG"), "cube") || (p_cube && strcmp(getenv("GBMP_PREIMG"), "2d")) ? 0x30B3 : 0x30B1,
                    tex, "the empty texture");
      /* GBMP_POSTIMG=cube|2d: that image kind on the texture right after a NULL allocation (paths sub, cubesub) */
      const char *postimg = getenv("GBMP_POSTIMG");
      const unsigned postkind = postimg && !strcmp(postimg, "cube") ? 0x30B3 : 0x30B1;
      if (!strcmp(tpath, "image") && mip_manual) {
         /* every level with its own gradient, level 0 first (manual) or last (manualrev) */
         int nlev = 1;
         for (int m = tw > th ? tw : th; m > 1; m >>= 1)
            nlev++;
         for (int k = 0; k < nlev; k++) {
            int l = !strcmp(mipmode, "manualrev") ? nlev - 1 - k : k;
            int lw = tw >> l ? tw >> l : 1, lh = th >> l ? th >> l : 1;
            void *lp = tex_pixels(tf, lw, lh);
            glTexImage2D(T, l, tf->ifmt, lw, lh, 0, tf->fmt, tf->type, lp);
            free(lp);
            if (k < 2)
               SYNCSTEP("level %d", l);
         }
      } else if (!strcmp(tpath, "image") || !strcmp(tpath, "perframe") || !strcmp(tpath, "reimage"))
         glTexImage2D(T, 0, tf->ifmt, tw, th, 0, tf->fmt, tf->type, img);
      else if (!strcmp(tpath, "sub")) {
         glTexImage2D(T, 0, tf->ifmt, tw, th, 0, tf->fmt, tf->type, NULL);
         if (postimg)
            self_image(dpy, c, postkind, tex, "the allocated texture (GBMP_POSTIMG)");
         glTexSubImage2D(T, 0, 0, 0, tw, th, tf->fmt, tf->type, img);
      } else if (!strcmp(tpath, "rows")) {
         glTexImage2D(T, 0, tf->ifmt, tw, th, 0, tf->fmt, tf->type, NULL);
         SYNCSTEP("allocated");
         for (int y = 0; y < th; y += 8) {
            glTexSubImage2D(T, 0, 0, y, tw, th - y < 8 ? th - y : 8, tf->fmt, tf->type,
                            (uint8_t *)img + (size_t)y * tw * tf->bpp);
            if (y < 16)
               SYNCSTEP("rows from %d", y);
         }
      } else if (!strcmp(tpath, "null")) {
         glTexImage2D(T, 0, tf->ifmt, tw, th, 0, tf->fmt, tf->type, NULL);
      } else if (!strcmp(tpath, "sub1")) {
         glTexImage2D(T, 0, tf->ifmt, tw, th, 0, tf->fmt, tf->type, NULL);
         SYNCSTEP("allocated");
         glTexSubImage2D(T, 0, 0, 0, tw, th < 8 ? th : 8, tf->fmt, tf->type, img);
      } else if (!strcmp(tpath, "twice")) {
         glTexImage2D(T, 0, tf->ifmt, tw, th, 0, tf->fmt, tf->type, img);
         SYNCSTEP("first upload");
         glTexSubImage2D(T, 0, 0, 0, tw, th, tf->fmt, tf->type, img);
      } else if (!strcmp(tpath, "subtwice")) {
         glTexImage2D(T, 0, tf->ifmt, tw, th, 0, tf->fmt, tf->type, NULL);
         SYNCSTEP("allocated");
         glTexSubImage2D(T, 0, 0, 0, tw, th, tf->fmt, tf->type, img);
         SYNCSTEP("first sub");
         glTexSubImage2D(T, 0, 0, 0, tw, th, tf->fmt, tf->type, img);
      } else if (!strcmp(tpath, "tinyfirst")) {
         glTexImage2D(T, 0, tf->ifmt, tw, th, 0, tf->fmt, tf->type, NULL);
         SYNCSTEP("allocated");
         glTexSubImage2D(T, 0, 0, 0, 1, 1, tf->fmt, tf->type, img);
         SYNCSTEP("1x1 sub");
         glTexSubImage2D(T, 0, 0, 0, tw, th, tf->fmt, tf->type, img);
      } else if (!strcmp(tpath, "zerofirst")) {
         glTexImage2D(T, 0, tf->ifmt, tw, th, 0, tf->fmt, tf->type, NULL);
         SYNCSTEP("allocated");
         glTexSubImage2D(T, 0, 0, 0, 0, 0, tf->fmt, tf->type, img);
         SYNCSTEP("zero-size sub");
         glTexSubImage2D(T, 0, 0, 0, tw, th, tf->fmt, tf->type, img);
      } else if (!strcmp(tpath, "halves")) {
         glTexImage2D(T, 0, tf->ifmt, tw, th, 0, tf->fmt, tf->type, NULL);
         SYNCSTEP("allocated");
         glTexSubImage2D(T, 0, 0, 0, tw, th / 2, tf->fmt, tf->type, img);
         SYNCSTEP("top half");
         glTexSubImage2D(T, 0, 0, th / 2, tw, th - th / 2, tf->fmt, tf->type,
                         (uint8_t *)img + (size_t)(th / 2) * tw * tf->bpp);
      } else if (!strncmp(tpath, "selfimg", 7)) {
         glTexImage2D(T, 0, tf->ifmt, tw, th, 0, tf->fmt, tf->type, NULL);
         void *im = eglCreateImageKHR ? eglCreateImageKHR(dpy, c, 0x30B1 /* EGL_GL_TEXTURE_2D_KHR */, (void *)(uintptr_t)tex, NULL) : NULL;
         printf("image of the texture: %p (egl error %#x)\n", im, im ? 0x3000 : eglGetError());
         if (im && !strcmp(tpath, "selfimgdrop") && eglDestroyImageKHR)
            eglDestroyImageKHR(dpy, im);
         SYNCSTEP("self image");
         glTexSubImage2D(T, 0, 0, 0, tw, th, tf->fmt, tf->type, img);
      } else if (!strcmp(tpath, "eglimg")) {
         if (image_texture(gbm, dpy, tw, th))
            return 1;
         SYNCSTEP("image bound");
         glTexSubImage2D(T, 0, 0, 0, tw, th, tf->fmt, tf->type, img);
      } else if (!strcmp(tpath, "storage")) {
         if (!glTexStorage2D) {
            fprintf(stderr, "no glTexStorage2D\n");
            return 1;
         }
         int levels = 1;
         for (int m = tw > th ? tw : th; mip && m > 1; m >>= 1)
            levels++;
         glTexStorage2D(T, levels, tf->ifmt, tw, th);
         glTexSubImage2D(T, 0, 0, 0, tw, th, tf->fmt, tf->type, img);
      } else if (p_copy) {
         glViewport(0, 0, tw, th);
         glUseProgram(pgrad);
         glClear(GL_COLOR_BUFFER_BIT);
         glDrawArrays(GL_TRIANGLES, 0, 3);
         glCopyTexImage2D(T, 0, tf->ifmt, 0, 0, tw, th, 0);
      } else if (p_fbo) {
         if (!strcmp(tpath, "fboimg")) {
            if (image_texture(gbm, dpy, tw, th))
               return 1;
         } else if (!strcmp(tpath, "fboself")) {
            glTexImage2D(T, 0, tf->ifmt, tw, th, 0, tf->fmt, tf->type, NULL);
            void *im = eglCreateImageKHR ? eglCreateImageKHR(dpy, c, 0x30B1, (void *)(uintptr_t)tex, NULL) : NULL;
            printf("image of the texture: %p (egl error %#x)\n", im, im ? 0x3000 : eglGetError());
         } else if (!strcmp(tpath, "fbodyn")) {
            glTexImage2D(T, 0, tf->ifmt, tw, th, 0, tf->fmt, tf->type, img);
            SYNCSTEP("first upload");
            glTexSubImage2D(T, 0, 0, 0, tw, th, tf->fmt, tf->type, img);
            SYNCSTEP("second upload");
         } else
            glTexImage2D(T, 0, tf->ifmt, tw, th, 0, tf->fmt, tf->type, NULL);
         glGenFramebuffers(1, &fbo);
         glBindFramebuffer(0x8D40, fbo);
         glFramebufferTexture2D(0x8D40, 0x8CE0 /* COLOR_ATTACHMENT0 */, T, tex, 0);
         if (fbodepth && getenv("GBMP_FBODEPTHTEX")) {
            unsigned dt = 0;
            glGenTextures(1, &dt);
            glBindTexture(T, dt);
            glTexParameteri(T, 0x2801, 0x2600);
            glTexParameteri(T, 0x2800, 0x2600);
            glTexParameteri(T, 0x2802, 0x812F);
            glTexParameteri(T, 0x2803, 0x812F);
            glTexImage2D(T, 0, 0x1902 /* DEPTH_COMPONENT */, tw, th, 0, 0x1902, fbodepth == 16 ? 0x1403 : 0x1405, NULL);
            glFramebufferTexture2D(0x8D40, 0x8D00 /* DEPTH_ATTACHMENT */, T, dt, 0);
            glBindTexture(T, tex);
            glEnable(GL_DEPTH_TEST);
         } else if (fbodepth) {
            unsigned rb = 0;
            glGenRenderbuffers(1, &rb);
            glBindRenderbuffer(0x8D41, rb);
            if (getenv("GBMP_RBPRE")) self_image(eglGetCurrentDisplay(), eglGetCurrentContext(), 0x30B9, rb, "the empty renderbuffer");
            glRenderbufferStorage(0x8D41, fbodepth == 16 ? 0x81A5 : fbodepth == 24 ? 0x81A6 : fbodepth == 32 ? 0x8CAC :
                                  fbodepth == 248 ? 0x88F0 : 0x8D48, tw, th);
            if (fbodepth != 8)
               glFramebufferRenderbuffer(0x8D40, 0x8D00 /* DEPTH_ATTACHMENT */, 0x8D41, rb);
            if (fbodepth == 248 || fbodepth == 8)
               glFramebufferRenderbuffer(0x8D40, 0x8D20 /* STENCIL_ATTACHMENT */, 0x8D41, rb);
            if (getenv("GBMP_FBODEPTHIMG") && eglCreateImageKHR) {
               void *im = eglCreateImageKHR(dpy, c, 0x30B9 /* EGL_GL_RENDERBUFFER_KHR */, (void *)(uintptr_t)rb, NULL);
               printf("image of the depth renderbuffer: %p (egl error %#x)\n", im, im ? 0x3000 : eglGetError());
            }
            glEnable(GL_DEPTH_TEST);
         }
         printf("fbo status %#x\n", glCheckFramebufferStatus(0x8D40));
         glViewport(0, 0, tw, th);
         glUseProgram(pgrad);
         glClear(GL_COLOR_BUFFER_BIT | (fbodepth ? GL_DEPTH_BUFFER_BIT : 0));
         glDrawArrays(GL_TRIANGLES, 0, 3);
         glBindFramebuffer(0x8D40, 0);
      } else if (p_cube) {
         const int c_storage = !strcmp(tpath, "cubestorage"), c_self = strstr(tpath, "self") != NULL;
         const int c_fbo = strstr(tpath, "fbo") != NULL;
         const int c_null = c_storage || c_self || c_fbo || !strcmp(tpath, "cubesub");
         if (c_storage) {
            if (!glTexStorage2D) {
               fprintf(stderr, "no glTexStorage2D\n");
               return 1;
            }
            int levels = 1;
            for (int m = tw; mip && m > 1; m >>= 1)
               levels++;
            glTexStorage2D(T, levels, tf->ifmt, tw, tw);
         } else
            for (int f = 0; f < 6; f++)
               glTexImage2D(0x8515 + f, 0, tf->ifmt, tw, tw, 0, tf->fmt, tf->type, c_null ? NULL : img);
         if (c_self)
            for (int f = 0; f < (strstr(tpath, "self6") ? 6 : 1); f++)
               self_image(dpy, c, 0x30B3 + f /* EGL_GL_TEXTURE_CUBE_MAP_POSITIVE_X_KHR + f */, tex, "a cube face");
         if (postimg)
            self_image(dpy, c, postkind, tex, "the allocated cube (GBMP_POSTIMG)");
         SYNCSTEP("cube allocated");
         if (c_fbo) {
            /* the other five faces get data, +X is rendered into every frame */
            for (int f = 1; f < 6; f++)
               glTexSubImage2D(0x8515 + f, 0, 0, 0, tw, tw, tf->fmt, tf->type, img);
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(0x8D40, fbo);
            glFramebufferTexture2D(0x8D40, 0x8CE0, 0x8515, tex, 0);
            if (fbodepth)
               attach_depth_rb(fbodepth, tw, tw, 0, 0);
            printf("fbo status %#x\n", glCheckFramebufferStatus(0x8D40));
            glBindFramebuffer(0x8D40, 0);
         } else if (c_null)
            for (int f = 0; f < 6; f++)
               glTexSubImage2D(0x8515 + f, 0, 0, 0, tw, tw, tf->fmt, tf->type, img);
      } else if (p_arrshadow) {
         /* a depth 2D array (shadow cascades): layer 0 is rendered into every frame and sampled */
         const int b = fbodepth ? fbodepth : 24;
         const unsigned ifmt = b == 16 ? 0x81A5 : b == 24 ? 0x81A6 : 0x8CAC;
         glTexImage3D(T, 0, (int)ifmt, tw, th, 4, 0, 0x1902, b == 16 ? 0x1403 : b == 24 ? 0x1405 : 0x1406, NULL);
         glGenFramebuffers(1, &fbo);
         glBindFramebuffer(0x8D40, fbo);
         if (glFramebufferTextureLayer)
            glFramebufferTextureLayer(0x8D40, 0x8D00, tex, 0, 0);
         printf("fbo status %#x (depth array %d bits, gl error %#x)\n", glCheckFramebufferStatus(0x8D40), b, glGetError());
         glBindFramebuffer(0x8D40, 0);
      } else if (p_3d || p_arr) {
         const int slices = 4;
         const size_t one = (size_t)tw * th * tf->bpp;
         uint8_t *vol = malloc(one * slices);
         for (int z = 0; z < slices; z++)
            memcpy(vol + z * one, img, one);
         const int v_storage = strstr(tpath, "storage") != NULL || strstr(tpath, "unit") != NULL;
         const int v_twice = strstr(tpath, "twice") != NULL, v_zero = strstr(tpath, "zero") != NULL;
         const int v_2dimg = strstr(tpath, "2dimg") != NULL;
         const int v_self = strstr(tpath, "self") != NULL;
         const int v_null = v_self || v_twice || v_zero || v_2dimg;
         const int v_fbo = strstr(tpath, "fbo") != NULL;
         if (!glTexImage3D || !glTexSubImage3D || !glTexStorage3D) {
            fprintf(stderr, "no 3D texture calls\n");
            return 1;
         }
         if (v_storage)
            glTexStorage3D(T, 1, tf->ifmt, tw, th, slices);
         else
            glTexImage3D(T, 0, tf->ifmt, tw, th, slices, 0, tf->fmt, tf->type, v_null || v_fbo ? NULL : vol);
         if (v_self)
            self_image(dpy, c, 0x30B2 /* EGL_GL_TEXTURE_3D_KHR */, tex, "the 3D texture (z 0)");
         if (v_2dimg)
            self_image(dpy, c, 0x30B1 /* EGL_GL_TEXTURE_2D_KHR */, tex, "the array, 2D kind");
         if (strstr(tpath, "unit")) {
            if (!glBindImageTexture) {
               fprintf(stderr, "no glBindImageTexture\n");
               return 1;
            }
            glBindImageTexture(0, tex, 0, 1, 0, 0x88BA /* READ_WRITE */, tf->ifmt);
            printf("image unit bind: gl error %#x\n", glGetError());
            glBindImageTexture(0, 0, 0, 0, 0, 0x88BA, tf->ifmt);
         }
         if (v_twice)
            for (int z = 0; z < (strstr(tpath, "twice1") ? 1 : slices); z++)
               glTexSubImage3D(T, 0, 0, 0, z, 1, 1, 1, tf->fmt, tf->type, vol);
         if (v_zero)
            glTexSubImage3D(T, 0, 0, 0, 0, 0, 0, 0, tf->fmt, tf->type, vol);
         SYNCSTEP("volume allocated");
         if (v_fbo) {
            glTexSubImage3D(T, 0, 0, 0, 1, tw, th, slices - 1, tf->fmt, tf->type, vol);
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(0x8D40, fbo);
            if (glFramebufferTextureLayer)
               glFramebufferTextureLayer(0x8D40, 0x8CE0, tex, 0, 0);
            if (fbodepth)
               attach_depth_rb(fbodepth, tw, th, 0, 0);
            printf("fbo status %#x\n", glCheckFramebufferStatus(0x8D40));
            glBindFramebuffer(0x8D40, 0);
         } else if (v_storage || v_null)
            glTexSubImage3D(T, 0, 0, 0, 0, tw, th, slices, tf->fmt, tf->type, vol);
         free(vol);
      } else if (p_pbo) {
         const size_t sz = (size_t)tw * th * tf->bpp, off = !strcmp(tpath, "pbooff") ? 4096 : 0;
         uint8_t *tmp = calloc(1, sz + off);
         memcpy(tmp + off, img, sz);
         unsigned pb = 0;
         if (!glGenBuffers || !glBindBuffer || !glBufferData) {
            fprintf(stderr, "no buffer calls\n");
            return 1;
         }
         glGenBuffers(1, &pb);
         glBindBuffer(0x88EC /* PIXEL_UNPACK_BUFFER */, pb);
         glBufferData(0x88EC, (intptr_t)(sz + off), tmp, 0x88E4 /* STATIC_DRAW */);
         if (!strcmp(tpath, "pbosub")) {
            glBindBuffer(0x88EC, 0);
            glTexImage2D(T, 0, tf->ifmt, tw, th, 0, tf->fmt, tf->type, NULL);
            glBindBuffer(0x88EC, pb);
            glTexSubImage2D(T, 0, 0, 0, tw, th, tf->fmt, tf->type, (void *)off);
         } else
            glTexImage2D(T, 0, tf->ifmt, tw, th, 0, tf->fmt, tf->type, (void *)off);
         glBindBuffer(0x88EC, 0);
         free(tmp);
      } else if (p_etc) {
         /* one ETC1 block (individual mode, base color 15,15,0 on both halves, all selectors 0) repeated:
          * a solid yellow texture. ETC2 RGB8 decodes ETC1 blocks the same way. */
         const int bw = (tw + 3) / 4, bh = (th + 3) / 4;
         uint8_t *blk = malloc((size_t)bw * bh * 8);
         static const uint8_t one[8] = {0xFF, 0xFF, 0x00, 0x00, 0, 0, 0, 0};
         for (int i = 0; i < bw * bh; i++)
            memcpy(blk + i * 8, one, 8);
         if (!glCompressedTexImage2D) {
            fprintf(stderr, "no glCompressedTexImage2D\n");
            return 1;
         }
         glCompressedTexImage2D(T, 0, !strcmp(tpath, "etc2") ? 0x9274 : 0x8D64, tw, th, 0, bw * bh * 8, blk);
         free(blk);
         expect_solid = 1;
      } else if (p_fborb) {
         /* FBO whose color attachment is a renderbuffer; nothing samples it, the check reads it back */
         unsigned crb = 0;
         const unsigned rf = !strcmp(tf->name, "565") ? 0x8D62 : !strcmp(tf->name, "4444") ? 0x8056 :
                             !strcmp(tf->name, "5551") ? 0x8057 : 0x8058;
         glGenFramebuffers(1, &fbo);
         glBindFramebuffer(0x8D40, fbo);
         glGenRenderbuffers(1, &crb);
         glBindRenderbuffer(0x8D41, crb);
         if (getenv("GBMP_RBPRE")) self_image(eglGetCurrentDisplay(), eglGetCurrentContext(), 0x30B9, crb, "the empty renderbuffer");
         glRenderbufferStorage(0x8D41, rf, tw, th);
         glFramebufferRenderbuffer(0x8D40, 0x8CE0, 0x8D41, crb);
         if (fbodepth)
            attach_depth_rb(fbodepth, tw, th, 0, 0);
         printf("fbo status %#x (color renderbuffer %#x)\n", glCheckFramebufferStatus(0x8D40), rf);
         glBindFramebuffer(0x8D40, 0);
         check_fbo = 1;
      } else if (p_ms || p_msrtt) {
         glTexImage2D(T, 0, tf->ifmt, tw, th, 0, tf->fmt, tf->type, NULL);
         if (getenv("GBMP_SELFIMG"))
            self_image(dpy, c, 0x30B1, tex, "the texture");
         glGenFramebuffers(1, &fbo);
         glBindFramebuffer(0x8D40, fbo);
         if (p_msrtt) {
            if (!glFramebufferTexture2DMultisampleEXT) {
               fprintf(stderr, "no glFramebufferTexture2DMultisampleEXT\n");
               return 1;
            }
            glFramebufferTexture2DMultisampleEXT(0x8D40, 0x8CE0, T, tex, 0, fbosamples);
            if (fbodepth)
               attach_depth_rb(fbodepth, tw, th, fbosamples, 1);
         } else {
            unsigned crb = 0;
            if (!glRenderbufferStorageMultisample || !glBlitFramebuffer) {
               fprintf(stderr, "no multisample renderbuffer calls\n");
               return 1;
            }
            glGenRenderbuffers(1, &crb);
            glBindRenderbuffer(0x8D41, crb);
            if (getenv("GBMP_RBPRE")) self_image(eglGetCurrentDisplay(), eglGetCurrentContext(), 0x30B9, crb, "the empty renderbuffer");
            glRenderbufferStorageMultisample(0x8D41, fbosamples, 0x8058 /* RGBA8 */, tw, th);
            glFramebufferRenderbuffer(0x8D40, 0x8CE0, 0x8D41, crb);
            if (fbodepth)
               attach_depth_rb(fbodepth, tw, th, fbosamples, 0);
            printf("multisample fbo status %#x\n", glCheckFramebufferStatus(0x8D40));
            glGenFramebuffers(1, &resolve_fbo);
            glBindFramebuffer(0x8D40, resolve_fbo);
            glFramebufferTexture2D(0x8D40, 0x8CE0, T, tex, 0);
         }
         printf("fbo status %#x\n", glCheckFramebufferStatus(0x8D40));
         glBindFramebuffer(0x8D40, 0);
      } else if (p_shadow) {
         /* the sampled texture is a depth texture, rendered into every frame (depth-only FBO) */
         const int b = fbodepth ? fbodepth : 16;
         unsigned ifmt = 0x1902 /* DEPTH_COMPONENT */, fmt2 = 0x1902, type2 = b == 16 ? 0x1403 : 0x1405;
         if (b == 248) {
            ifmt = es >= 3 ? 0x88F0 : 0x84F9; /* DEPTH24_STENCIL8 / DEPTH_STENCIL */
            fmt2 = 0x84F9;
            type2 = 0x84FA; /* UNSIGNED_INT_24_8 */
         } else if (es >= 3) {
            ifmt = b == 16 ? 0x81A5 : b == 24 ? 0x81A6 : 0x8CAC;
            if (b == 32)
               type2 = 0x1406; /* FLOAT, DEPTH_COMPONENT32F */
         }
         glTexImage2D(T, 0, (int)ifmt, tw, th, 0, fmt2, type2, NULL);
         if (getenv("GBMP_SELFIMG"))
            self_image(dpy, c, 0x30B1, tex, "the depth texture");
         glGenFramebuffers(1, &fbo);
         glBindFramebuffer(0x8D40, fbo);
         glFramebufferTexture2D(0x8D40, 0x8D00, T, tex, 0);
         if (b == 248)
            glFramebufferTexture2D(0x8D40, 0x8D20, T, tex, 0);
         if (getenv("GBMP_SHADOWCOLOR")) { /* a small color renderbuffer too, for drivers that want one */
            unsigned crb = 0;
            glGenRenderbuffers(1, &crb);
            glBindRenderbuffer(0x8D41, crb);
            if (getenv("GBMP_RBPRE")) self_image(eglGetCurrentDisplay(), eglGetCurrentContext(), 0x30B9, crb, "the empty renderbuffer");
            glRenderbufferStorage(0x8D41, 0x8D62, tw, th);
            glFramebufferRenderbuffer(0x8D40, 0x8CE0, 0x8D41, crb);
         }
         printf("fbo status %#x (depth texture %d bits, internal format %#x)\n", glCheckFramebufferStatus(0x8D40), b, ifmt);
         glBindFramebuffer(0x8D40, 0);
      } else {
         fprintf(stderr, "bad GBMP_TEXPATH %s\n", tpath);
         return 1;
      }
      if (mip && !mip_manual)
         glGenerateMipmap(T);
      printf("gl error after upload: %#x\n", glGetError());
      STEP("texture uploaded");
      if (sync) {
         glFinish();
         STEP("upload finished");
      }
   }
   glUseProgram(prog);
   int ut = glGetUniformLocation(prog, "t");
   glViewport(0, 0, w, h);
   if (depth)
      glEnable(GL_DEPTH_TEST);
   if (midflush && !strcmp(midflush, "copy")) { /* an 8x8 scratch texture (too small to be compressed) */
      unsigned st = 0;
      glGenTextures(1, &st);
      glBindTexture(0x0DE1, st);
      glTexParameteri(0x0DE1, 0x2801, 0x2600);
      glTexImage2D(0x0DE1, 0, 0x1907, 8, 8, 0, 0x1907, 0x1401, NULL);
   }
   /* The window pass: one draw, or for GBMP_MIDFLUSH two depth-tested draws with a flushing call between. */
#define WINDOW_PASS(tval) do { \
      glClearColor(0, 0, 0, 1); \
      glClear(clear_bits); \
      if (midflush) { \
         for (int k_ = 0; k_ < 2; k_++) { \
            const int near_ = (k_ == 0) == nearfirst; \
            glUseProgram(near_ ? pnear : pfar); \
            glUniform1f(near_ ? uz_near : uz_far, near_ ? -0.5f : 0.5f); \
            glDrawArrays(GL_TRIANGLES, 0, 3); \
            if ((k_ == 0) != mid_end) { \
               unsigned char one_[4]; \
               static const unsigned ds_[2] = {0x1801 /* GL_DEPTH_EXT */, 0x1802 /* GL_STENCIL_EXT */}; \
               if (mid_discard && glDiscardFramebufferEXT) glDiscardFramebufferEXT(0x8D40, 2, ds_); \
               else if (mid_discard && glInvalidateFramebuffer) glInvalidateFramebuffer(0x8D40, 2, ds_); \
               if (strstr(midflush, "read")) { glReadPixels(w - 3, h - 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, one_); memcpy(mid_px, one_, 4); } \
               else if (!strcmp(midflush, "copy") && glCopyTexSubImage2D) glCopyTexSubImage2D(0x0DE1, 0, 0, 0, 0, 0, 8, 8); \
               else if (!strcmp(midflush, "flush") && glFlush) glFlush(); \
               else if (strstr(midflush, "finish")) glFinish(); \
               else if (strstr(midflush, "fence") && glFenceSync && glClientWaitSync) mid_fence(midflush, dpy); \
            } \
         } \
         if ((strstr(midflush, "oldfence") || strstr(midflush, "fenceonly")) && glFenceSync && !old_sync) old_sync = glFenceSync(0x9117, 0); \
      } else { \
         glUniform1f(ut, (float)(tval)); \
         glDrawArrays(GL_TRIANGLES, 0, 3); \
      } \
   } while (0)
   STEP("depth %d stencil %d", depth, stencil);

   double t0 = now_ms();
   void *held = NULL;
   const int hold = getenv("GBMP_HOLD") != NULL;
   for (int i = 0; i < frames; i++) {
      if (i < 3) STEP("frame %d draw", i);
      if (use_tex && !strcmp(tpath, "perframe"))
         glTexSubImage2D(0x0DE1, 0, 0, 0, tw, th, tf->fmt, tf->type, img);
      if (use_tex && !strcmp(tpath, "reimage"))
         glTexImage2D(0x0DE1, 0, tf->ifmt, tw, th, 0, tf->fmt, tf->type, img);
      if (use_tex && p_fboframe) {
         glBindFramebuffer(0x8D40, fbo);
         glViewport(0, 0, tw, th);
         glUseProgram(pgrad);
         glClear(GL_COLOR_BUFFER_BIT | (fbodepth ? GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT : 0));
         glDrawArrays(GL_TRIANGLES, 0, 3);
         if (getenv("GBMP_FBODISCARD")) {
            static const unsigned att[2] = {0x8D00, 0x8D20};
            if (glDiscardFramebufferEXT)
               glDiscardFramebufferEXT(0x8D40, 2, att);
            else if (glInvalidateFramebuffer)
               glInvalidateFramebuffer(0x8D40, 2, att);
         }
         if (resolve_fbo) { /* multisample resolve into the sampled texture */
            glBindFramebuffer(0x8CA8 /* READ */, fbo);
            glBindFramebuffer(0x8CA9 /* DRAW */, resolve_fbo);
            glBlitFramebuffer(0, 0, tw, th, 0, 0, tw, th, GL_COLOR_BUFFER_BIT, 0x2600);
         }
         glBindFramebuffer(0x8D40, 0);
         glViewport(0, 0, w, h);
         glUseProgram(prog);
      }
      if (use_tex && (p_shadow || p_arrshadow)) { /* depth-only pass into the depth texture */
         glBindFramebuffer(0x8D40, fbo);
         glViewport(0, 0, tw, th);
         glUseProgram(pshadow);
         glEnable(GL_DEPTH_TEST);
         glClear(GL_DEPTH_BUFFER_BIT | (fbodepth == 248 ? GL_STENCIL_BUFFER_BIT : 0));
         glDrawArrays(GL_TRIANGLES, 0, 3);
         glBindFramebuffer(0x8D40, 0);
         if (!depth && glDisable)
            glDisable(GL_DEPTH_TEST);
         glViewport(0, 0, w, h);
         glUseProgram(prog);
      }
      WINDOW_PASS(i * 0.05);
      if (sync && i < 3) {
         glFinish();
         STEP("frame %d finished", i);
      }
      if (getenv("GBMP_PRESWAPDELAY")) /* ms between the last draw and the swap */
         usleep((useconds_t)atoi(getenv("GBMP_PRESWAPDELAY")) * 1000);
      if (i < 3) STEP("frame %d swap", i);
      if (!eglSwapBuffers(dpy, s)) {
         fprintf(stderr, "eglSwapBuffers failed at frame %d: %#x\n", i, eglGetError());
         return 1;
      }
      if (i < 3) STEP("frame %d lock_front", i);
      void *bo = gbm_surface_lock_front_buffer(gs);
      if (!bo) {
         fprintf(stderr, "lock_front_buffer failed at frame %d\n", i);
         return 1;
      }
      if (i == frames - 1 && getenv("GBMP_FRONTCHECK") && gbm_bo_get_fd && gbm_bo_get_stride) {
         /* The buffer a compositor or the display would get: XRGB/ARGB8888 is B,G,R,X in memory, top row first. */
         glFinish();
         int bfd = gbm_bo_get_fd(bo);
         uint32_t st = gbm_bo_get_stride(bo);
         uint8_t *m = bfd >= 0 ? mmap(NULL, (size_t)st * h, PROT_READ, MAP_SHARED, bfd, 0) : MAP_FAILED;
         if (m == MAP_FAILED)
            printf("front buffer: can't map (fd %d)\n", bfd);
         else {
            const uint8_t *a = m + (size_t)(h - 3) * st + 2 * 4, *b = m + (size_t)2 * st + (size_t)(w - 3) * 4;
            front_ok = a[2] < 16 && a[1] < 16 && b[2] > 240 && b[1] > 240;
            printf("front buffer r,g,b %u,%u,%u / %u,%u,%u -> %s\n", a[2], a[1], a[0], b[2], b[1], b[0],
                   front_ok ? "FRONT PASS" : "FRONT FAIL");
            munmap(m, (size_t)st * h);
         }
      }
      if (hold) { /* like a KMS client: the shown buffer stays locked until the next one is up */
         if (held)
            gbm_surface_release_buffer(gs, held);
         held = bo;
      } else
         gbm_surface_release_buffer(gs, bo);
   }
   double ms = (now_ms() - t0) / frames;
   if (midflush && strstr(midflush, "read"))
      printf("mid read %u,%u,%u,%u\n", mid_px[0], mid_px[1], mid_px[2], mid_px[3]);
   if (midflush && strstr(midflush, "fence"))
      printf("fence wait %#x\n", fence_rc);
   if (getenv("GBMP_NOCHECK")) {
      printf("%d frames 640x480: %.2f ms/frame (%.0f fps)\nRESULT SKIP (no readback)\n", frames, ms, 1000 / ms);
      eglMakeCurrent(dpy, NULL, NULL, NULL);
      eglTerminate(dpy);
      return 0;
   }
   /* Check the last frame's colors at two corners (the shader writes uv into r/g). */
   STEP("final check");
   WINDOW_PASS(0);
   STEP("glFinish");
   glFinish();
   unsigned char px[2][4];
   if (check_fbo) { /* the color renderbuffer FBO itself */
      glBindFramebuffer(0x8D40, fbo);
      glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px[0]);
      glReadPixels(tw - 3, th - 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px[1]);
      glBindFramebuffer(0x8D40, 0);
   } else {
      glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px[0]);
      glReadPixels(w - 3, h - 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px[1]);
   }
   int ok = px[0][0] < 16 && px[0][1] < 16 && px[1][0] > 240 && px[1][1] > 240;
   if (expect_solid) /* solid yellow in r,g */
      ok = px[0][0] > 240 && px[0][1] > 240 && px[1][0] > 240 && px[1][1] > 240;
   if (check_fbo) /* corners of a small target: looser, and low-bit formats round */
      ok = px[0][0] < 32 && px[0][1] < 32 && px[1][0] > 220 && px[1][1] > 220;
   printf("%d frames 640x480: %.2f ms/frame (%.0f fps)\npixels %u,%u,%u / %u,%u,%u -> %s\n", frames, ms, 1000 / ms,
          px[0][0], px[0][1], px[0][2], px[1][0], px[1][1], px[1][2], ok ? "RESULT PASS" : "RESULT FAIL");
   eglMakeCurrent(dpy, NULL, NULL, NULL);
   eglTerminate(dpy);
   gbm_surface_destroy(gs);
   gbm_device_destroy(gbm);
   close(fd);
   return ok ? 0 : 2;
}
