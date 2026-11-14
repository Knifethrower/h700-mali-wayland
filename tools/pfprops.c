/* pfprops: print every DRM_PANFROST_PARAM the kernel answers, to diff a GPU's feature registers
 * between SoCs (e.g. the H700's Mali-G31 vs the RK3326's).
 * Usage: pfprops [/dev/dri/renderD128]
 * Build: aarch64-linux-gnu-gcc -O2 -o pfprops pfprops.c
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct drm_panfrost_get_param {
   uint32_t param;
   uint32_t pad;
   uint64_t value;
};
#define DRM_IOCTL_PANFROST_GET_PARAM _IOWR('d', 0x40 + 0x04, struct drm_panfrost_get_param)

static const char *names[] = {
   "GPU_PROD_ID", "GPU_REVISION", "SHADER_PRESENT", "TILER_PRESENT", "L2_PRESENT", "STACK_PRESENT",
   "AS_PRESENT", "JS_PRESENT", "L2_FEATURES", "CORE_FEATURES", "TILER_FEATURES", "MEM_FEATURES",
   "MMU_FEATURES", "THREAD_FEATURES", "MAX_THREADS", "THREAD_MAX_WORKGROUP_SZ", "THREAD_MAX_BARRIER_SZ",
   "COHERENCY_FEATURES", "TEXTURE_FEATURES0", "TEXTURE_FEATURES1", "TEXTURE_FEATURES2", "TEXTURE_FEATURES3",
   "JS_FEATURES0", "JS_FEATURES1", "JS_FEATURES2", "JS_FEATURES3", "JS_FEATURES4", "JS_FEATURES5",
   "JS_FEATURES6", "JS_FEATURES7", "JS_FEATURES8", "JS_FEATURES9", "JS_FEATURES10", "JS_FEATURES11",
   "JS_FEATURES12", "JS_FEATURES13", "JS_FEATURES14", "JS_FEATURES15", "NR_CORE_GROUPS", "THREAD_TLS_ALLOC",
   "AFBC_FEATURES", "SYSTEM_TIMESTAMP", "SYSTEM_TIMESTAMP_FREQUENCY", "ALLOWED_JM_CTX_PRIORITIES",
   "SELECTED_COHERENCY",
};

int main(int argc, char **argv)
{
   const char *dev = argc > 1 ? argv[1] : "/dev/dri/renderD128";
   int fd = open(dev, O_RDWR | O_CLOEXEC);
   if (fd < 0) {
      perror(dev);
      return 1;
   }
   for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) {
      if (i == 41) /* SYSTEM_TIMESTAMP changes every call */
         continue;
      struct drm_panfrost_get_param p = {.param = i};
      if (ioctl(fd, DRM_IOCTL_PANFROST_GET_PARAM, &p) == 0)
         printf("%-26s 0x%08llx\n", names[i], (unsigned long long)p.value);
      else
         printf("%-26s (not answered)\n", names[i]);
   }
   close(fd);
   return 0;
}
