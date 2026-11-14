/* gles1_probe: a GLES 1 client on a GBM surface, for the AFBC checks of Route R (see gbm_probe.c for the
 * GLES 2/3 probe). Libraries are dlopened: libgbm.so.1, libEGL.so.1, libGLESv1_CM.so.1, so whatever is
 * installed under the GLES 1 name is what gets tested.
 *
 * Every frame: (G1P_FBO=1) a textured, depth-tested quad into an OES framebuffer object (color texture +
 * depth renderbuffer), then a textured, depth-tested quad into the window (depth 24), then
 * (G1P_FINISH=1) glFinish, then the swap. The last swapped buffer is mapped and checked, and the frame
 * after it is read back with glReadPixels.
 *   G1P_MIP=1   GL_GENERATE_MIPMAP on the uploaded texture
 *   usage: gles1_probe [frames]     prints RESULT PASS / RESULT FAIL; GPU faults are counted by the caller
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

typedef void *EGLDisplay, *EGLConfig, *EGLSurface, *EGLContext;
typedef int EGLint;
typedef unsigned EGLBoolean;
#define FOURCC_XR24 0x34325258u

#define LOAD(lib, ret, name, args) \
   ret (*name) args = (ret (*) args)dlsym(lib, #name); \
   if (!name) { \
      fprintf(stderr, "missing %s\n", #name); \
      return 1; \
   }

int main(int argc, char **argv)
{
   setvbuf(stdout, NULL, _IONBF, 0);
   const int frames = argc > 1 ? atoi(argv[1]) : 120;
   const int use_fbo = getenv("G1P_FBO") != NULL, use_mip = getenv("G1P_MIP") != NULL, use_finish = getenv("G1P_FINISH") != NULL;
   void *gbm_l = dlopen("libgbm.so.1", RTLD_NOW | RTLD_GLOBAL);
   void *egl_l = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
   void *gl = dlopen("libGLESv1_CM.so.1", RTLD_NOW | RTLD_GLOBAL);
   if (!gbm_l || !egl_l || !gl) {
      fprintf(stderr, "dlopen: %s\n", dlerror());
      return 1;
   }
   LOAD(gbm_l, void *, gbm_create_device, (int))
   LOAD(gbm_l, void *, gbm_surface_create, (void *, uint32_t, uint32_t, uint32_t, uint32_t))
   LOAD(gbm_l, void *, gbm_surface_lock_front_buffer, (void *))
   LOAD(gbm_l, void, gbm_surface_release_buffer, (void *, void *))
   LOAD(gbm_l, int, gbm_bo_get_fd, (void *))
   LOAD(gbm_l, uint32_t, gbm_bo_get_stride, (void *))
   LOAD(egl_l, EGLDisplay, eglGetPlatformDisplay, (unsigned, void *, const intptr_t *))
   LOAD(egl_l, EGLBoolean, eglInitialize, (EGLDisplay, EGLint *, EGLint *))
   LOAD(egl_l, EGLBoolean, eglChooseConfig, (EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *))
   LOAD(egl_l, EGLBoolean, eglGetConfigAttrib, (EGLDisplay, EGLConfig, EGLint, EGLint *))
   LOAD(egl_l, EGLSurface, eglCreateWindowSurface, (EGLDisplay, EGLConfig, void *, const EGLint *))
   LOAD(egl_l, EGLContext, eglCreateContext, (EGLDisplay, EGLConfig, EGLContext, const EGLint *))
   LOAD(egl_l, EGLBoolean, eglMakeCurrent, (EGLDisplay, EGLSurface, EGLSurface, EGLContext))
   LOAD(egl_l, EGLBoolean, eglSwapBuffers, (EGLDisplay, EGLSurface))
   LOAD(egl_l, EGLint, eglGetError, (void))
   LOAD(gl, const unsigned char *, glGetString, (unsigned))
   LOAD(gl, unsigned, glGetError, (void))
   LOAD(gl, void, glGenTextures, (int, unsigned *))
   LOAD(gl, void, glBindTexture, (unsigned, unsigned))
   LOAD(gl, void, glTexParameteri, (unsigned, unsigned, int))
   LOAD(gl, void, glTexImage2D, (unsigned, int, int, int, int, int, unsigned, unsigned, const void *))
   LOAD(gl, void, glTexEnvi, (unsigned, unsigned, int))
   LOAD(gl, void, glEnable, (unsigned))
   LOAD(gl, void, glEnableClientState, (unsigned))
   LOAD(gl, void, glVertexPointer, (int, unsigned, int, const void *))
   LOAD(gl, void, glTexCoordPointer, (int, unsigned, int, const void *))
   LOAD(gl, void, glViewport, (int, int, int, int))
   LOAD(gl, void, glClearColor, (float, float, float, float))
   LOAD(gl, void, glClear, (unsigned))
   LOAD(gl, void, glDrawArrays, (unsigned, int, int))
   LOAD(gl, void, glFinish, (void))
   LOAD(gl, void, glReadPixels, (int, int, int, int, unsigned, unsigned, void *))
   LOAD(gl, void, glGenFramebuffersOES, (int, unsigned *))
   LOAD(gl, void, glBindFramebufferOES, (unsigned, unsigned))
   LOAD(gl, void, glFramebufferTexture2DOES, (unsigned, unsigned, unsigned, unsigned, int))
   LOAD(gl, void, glGenRenderbuffersOES, (int, unsigned *))
   LOAD(gl, void, glBindRenderbufferOES, (unsigned, unsigned))
   LOAD(gl, void, glRenderbufferStorageOES, (unsigned, unsigned, int, int))
   LOAD(gl, void, glFramebufferRenderbufferOES, (unsigned, unsigned, unsigned, unsigned))
   LOAD(gl, unsigned, glCheckFramebufferStatusOES, (unsigned))

   int fd = -1;
   for (int i = 0; i < 4 && fd < 0; i++) {
      char p[32];
      snprintf(p, sizeof p, "/dev/dri/card%d", i);
      fd = open(p, O_RDWR | O_CLOEXEC);
   }
   void *gbm = fd >= 0 ? gbm_create_device(fd) : NULL;
   EGLDisplay dpy = gbm ? eglGetPlatformDisplay(0x31D7 /* EGL_PLATFORM_GBM_KHR */, gbm, NULL) : NULL;
   EGLint maj = 0, min = 0;
   if (!dpy || !eglInitialize(dpy, &maj, &min)) {
      fprintf(stderr, "no display (fd %d, gbm %p)\n", fd, gbm);
      return 1;
   }
   const EGLint ca[] = {0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3021, 0, 0x3025 /* DEPTH */, 24, 0x3033, 4 /* WINDOW */,
                        0x3040 /* RENDERABLE_TYPE */, 1 /* OPENGL_ES_BIT */, 0x3038};
   EGLConfig cfgs[64], cfg = NULL;
   EGLint n = 0;
   eglChooseConfig(dpy, ca, cfgs, 64, &n);
   for (int i = 0; i < n && !cfg; i++) {
      EGLint vid = 0;
      eglGetConfigAttrib(dpy, cfgs[i], 0x302E /* NATIVE_VISUAL_ID */, &vid);
      if ((uint32_t)vid == FOURCC_XR24)
         cfg = cfgs[i];
   }
   if (!cfg) {
      fprintf(stderr, "no GLES 1 window config (%d candidates)\n", n);
      return 1;
   }
   const int w = 640, h = 480, tw = 64, th = 64;
   void *gs = gbm_surface_create(gbm, w, h, FOURCC_XR24, 1 << 2 /* RENDERING */);
   EGLSurface s = gs ? eglCreateWindowSurface(dpy, cfg, gs, NULL) : NULL;
   const EGLint cx[] = {0x3098 /* CONTEXT_CLIENT_VERSION */, 1, 0x3038};
   EGLContext c = eglCreateContext(dpy, cfg, NULL, cx);
   if (!s || !c || !eglMakeCurrent(dpy, s, s, c)) {
      fprintf(stderr, "surface/context failed %#x\n", eglGetError());
      return 1;
   }
   printf("GL version '%s' renderer '%s'\n", glGetString(0x1F02), glGetString(0x1F01));

   uint8_t *img = malloc((size_t)tw * th * 4);
   for (int y = 0; y < th; y++)
      for (int x = 0; x < tw; x++) {
         uint8_t *p = img + ((size_t)y * tw + x) * 4;
         p[0] = (uint8_t)(x * 255 / (tw - 1));
         p[1] = (uint8_t)(y * 255 / (th - 1));
         p[2] = 128;
         p[3] = 255;
      }
   unsigned tex[2] = {0, 0}, fbo = 0, rb = 0;
   glGenTextures(2, tex);
   glBindTexture(0x0DE1, tex[0]);
   glTexParameteri(0x0DE1, 0x2801 /* MIN_FILTER */, use_mip ? 0x2700 /* NEAREST_MIPMAP_NEAREST */ : 0x2600);
   glTexParameteri(0x0DE1, 0x2800, 0x2600);
   glTexParameteri(0x0DE1, 0x2802, 0x812F);
   glTexParameteri(0x0DE1, 0x2803, 0x812F);
   if (use_mip)
      glTexParameteri(0x0DE1, 0x8191 /* GENERATE_MIPMAP */, 1);
   glTexImage2D(0x0DE1, 0, 0x1908, tw, th, 0, 0x1908, 0x1401, img);
   if (use_fbo) {
      glBindTexture(0x0DE1, tex[1]);
      glTexParameteri(0x0DE1, 0x2801, 0x2600);
      glTexParameteri(0x0DE1, 0x2800, 0x2600);
      glTexParameteri(0x0DE1, 0x2802, 0x812F);
      glTexParameteri(0x0DE1, 0x2803, 0x812F);
      glTexImage2D(0x0DE1, 0, 0x1908, tw, th, 0, 0x1908, 0x1401, NULL);
      glGenFramebuffersOES(1, &fbo);
      glBindFramebufferOES(0x8D40, fbo);
      glFramebufferTexture2DOES(0x8D40, 0x8CE0, 0x0DE1, tex[1], 0);
      glGenRenderbuffersOES(1, &rb);
      glBindRenderbufferOES(0x8D41, rb);
      glRenderbufferStorageOES(0x8D41, 0x81A5 /* DEPTH_COMPONENT16 */, tw, th);
      glFramebufferRenderbufferOES(0x8D40, 0x8D00, 0x8D41, rb);
      printf("fbo status %#x\n", glCheckFramebufferStatusOES(0x8D40));
      glBindFramebufferOES(0x8D40, 0);
   }
   printf("gl error after setup: %#x\n", glGetError());

   static const float verts[8] = {-1, -1, 1, -1, -1, 1, 1, 1}, uvs[8] = {0, 0, 1, 0, 0, 1, 1, 1};
   glEnableClientState(0x8074);
   glVertexPointer(2, 0x1406, 0, verts);
   glEnableClientState(0x8078);
   glTexCoordPointer(2, 0x1406, 0, uvs);
   glEnable(0x0DE1);
   glEnable(0x0B71 /* DEPTH_TEST */);
   glTexEnvi(0x2300, 0x2200, 0x1E01 /* REPLACE */);
   glClearColor(0, 0, 0, 1);

   int front_ok = 0;
   for (int i = 0; i <= frames; i++) {
      if (use_fbo) {
         glBindFramebufferOES(0x8D40, fbo);
         glViewport(0, 0, tw, th);
         glClear(0x4100);
         glBindTexture(0x0DE1, tex[0]);
         glDrawArrays(5, 0, 4);
         glBindFramebufferOES(0x8D40, 0);
      }
      glViewport(0, 0, w, h);
      glClear(0x4100);
      glBindTexture(0x0DE1, tex[use_fbo ? 1 : 0]);
      glDrawArrays(5, 0, 4);
      if (i == frames)
         break; /* the frame for the readback below */
      if (use_finish)
         glFinish();
      if (!eglSwapBuffers(dpy, s)) {
         fprintf(stderr, "eglSwapBuffers failed at frame %d: %#x\n", i, eglGetError());
         return 1;
      }
      void *bo = gbm_surface_lock_front_buffer(gs);
      if (!bo) {
         fprintf(stderr, "lock_front_buffer failed at frame %d\n", i);
         return 1;
      }
      if (i == frames - 1) { /* XRGB8888 is B,G,R,X in memory, top row first */
         glFinish();
         int bfd = gbm_bo_get_fd(bo);
         uint32_t st = gbm_bo_get_stride(bo);
         uint8_t *m = bfd >= 0 ? mmap(NULL, (size_t)st * h, PROT_READ, MAP_SHARED, bfd, 0) : MAP_FAILED;
         if (m != MAP_FAILED) {
            const uint8_t *a = m + (size_t)(h - 3) * st + 2 * 4, *b = m + (size_t)2 * st + (size_t)(w - 3) * 4;
            front_ok = a[2] < 16 && a[1] < 16 && b[2] > 240 && b[1] > 240;
            printf("front buffer r,g,b %u,%u,%u / %u,%u,%u -> %s\n", a[2], a[1], a[0], b[2], b[1], b[0],
                   front_ok ? "FRONT PASS" : "FRONT FAIL");
            munmap(m, (size_t)st * h);
         } else
            printf("front buffer: can't map (fd %d)\n", bfd);
      }
      gbm_surface_release_buffer(gs, bo);
   }
   unsigned char px[2][4] = {{0}};
   glReadPixels(2, 2, 1, 1, 0x1908, 0x1401, px[0]);
   glReadPixels(w - 3, h - 3, 1, 1, 0x1908, 0x1401, px[1]);
   const int ok = px[0][0] < 16 && px[0][1] < 16 && px[1][0] > 240 && px[1][1] > 240;
   printf("%d frames, gl error %#x\npixels %u,%u,%u / %u,%u,%u -> %s\n", frames, glGetError(), px[0][0], px[0][1], px[0][2],
          px[1][0], px[1][1], px[1][2], ok && front_ok ? "RESULT PASS" : "RESULT FAIL");
   eglMakeCurrent(dpy, NULL, NULL, NULL);
   return ok && front_ok ? 0 : 2;
}
