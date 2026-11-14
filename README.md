# Rockchip's Mali blob on the Allwinner H700, under Wayland

Rockchip's Wayland/GBM `libmali` for the Mali-G31 (`bifrost-g31-g29p1`) running on an Allwinner H700
handheld under ROCKNIX's sway, on ROCKNIX's `mali_kbase` r54p2.

It does not work out of the box, for one reason: the H700's Mali-G31 was built **without AFBC**, and the
blob uses AFBC for textures, renderbuffers, window depth and window buffers without asking. Every AFBC
write is a GPU fault and what was written reads back as zeros. This repository holds what makes it work:

- `tools/afbcfix/`: a small library that sits in front of the blob and keeps it off AFBC.
- `kernel/patches/`: two patches for `mali_kbase` on the H700.
- `tools/`: the probes and device scripts everything was found and tested with.

**Status: a working prototype, tested on one device** (Anbernic RG34XXSP, ROCKNIX 20261101, kernel 7.2).
Nothing here has been proposed to ROCKNIX.

No blob code was read or disassembled. Everything about the blob was found black-box: what it draws,
which calls fault, what the kernel driver reports.

## What was tested

All on the RG34XXSP with the library in front of the blob, GPU clock at 600 MHz unless noted. The
RetroArch and suspend rows were run with the previous version of the library, the rest with this one.

| Check | Result |
|---|---|
| Probe sets `matrix`, `gaps`, `open` (158 cases: texture formats and sizes, mipmaps, cube maps, arrays, FBOs, renderbuffers, multisampling, depth textures, fences, mid-frame readbacks, GLES 1) | all correct, 0 GPU faults |
| sway + EmulationStation | correct, 0 faults |
| glmark2 off-screen, 26 scenes | 374 (Panfrost on the same device is about the same) |
| glmark2 on-screen | 60-62, 0 faults |
| Torus Trooper (SDL, GLES 2) under sway and through SDL KMSDRM | correct, 0 faults, lower CPU load than on Panfrost |
| Slope and Dressmaker (Unity, GLES 3) | correct, 0 faults |
| Dressmaker's GPU-bound 3D stages (both drivers at 648 MHz, one run each) | 1.4-1.8x the frame rate of Panfrost |
| A LÖVE 11.5 game | correct, 0 faults |
| mpv (`--vo=gpu --gpu-api=opengl --gpu-context=wayland`) | correct, 0 faults |
| RetroArch menu, `gl` driver | correct, 0 faults |
| Suspend and resume, 3 cycles | clean |

Raw probe output and a few screenshots are in `results/rg34xxsp/`.

## Known limits

- **RetroArch's `glcore` driver** draws its menu wrong on this blob, with or without the library. It is
  not an AFBC matter. Use `video_driver = "gl"`.
- **`EXT_multisampled_render_to_texture` is hidden** and its calls are made single-sample, because its
  depth buffer can't be kept off AFBC. Core ES 3 multisample renderbuffers work.
- **Keep mode** (see below) is not used for multisampled window configs or GLES 1, and it starts one
  frame late.
- **A wlroots detail:** the library adds a private placeholder modifier next to "linear" in the list the
  compositor gets from `eglQueryDmaBufModifiersEXT`. Without it wlroots tells linux-dmabuf v3 clients
  only "unspecified", and the blob's Wayland client then finds no window config. The clean fix is a small
  wlroots patch (name linear explicitly for v3 clients). It is not written yet.
- The blob uses about 50 MB more system memory than Panfrost in a large Unity game on this 1 GB device.
- Other blob versions were not tested. The texture trick relies on undocumented behavior, so every
  version needs the probe sets run against it.

## How the library works

`tools/afbcfix/afbcfix.c` has the full description at its top. In short:

- **Textures:** before a texture gets storage, the library asks EGL for an image of it with a kind that
  can't fit (a cube-face image of a 2D texture). The blob refuses and creates nothing, but never
  compresses that texture afterwards. The GL call itself is passed on unchanged.
- **Renderbuffers:** after the storage call, an EGLImage is made from the renderbuffer and destroyed
  again. Same lasting effect.
- **Window depth:** the blob writes the window's depth as AFBC whenever a frame is flushed before its
  swap (`glFinish`, `glReadPixels`, making a fence, changing the context). The library discards depth
  and stencil right before those calls. When an application goes on drawing depth-tested geometry after
  such a call, the library switches that process to **keep mode**: the window is drawn through a
  framebuffer object of the library and copied to the real window at each swap.
- **Window buffers:** Arm's modifiers are removed from the list a compositor passes to its clients, so
  clients get linear buffers.

Options (environment): `AFBCFIX=0` off, `AFBCFIX_DEBUG=1` counters at exit, `AFBCFIX_DEBUG=2` EGL-level
calls into `/dev/kmsg` next to the kernel's fault lines, `AFBCFIX_KEEPDEPTH=1|0` force or forbid keep
mode, `AFBCFIX_WINDEPTH=0`, `AFBCFIX_MSRTT=0`, `AFBCFIX_MODS=0`.

## What is not in this repository

- **The blob.** Take `libmali` for `bifrost-g31-g29p1` (Wayland/GBM) and `libmali-hook` from a ROCKNIX
  RK3326 build or from Rockchip's libmali distribution. Tested version string:
  `g29p1-12eac0`, files `libmali.so.1.10.0` and `libmali-hook.so.1.10.0`.
- **Kernel modules.** Build them for your own kernel (below).

## Importing it into a ROCKNIX build

These are the pieces an H700 image needs. The first three are proven on the device; how they are wired
into ROCKNIX's packages is up to the maintainers.

1. **Kernel driver.** Build ROCKNIX's `mali_kbase` (the `mali-bifrost` package, as the RK3326 device
   builds it: devicetree platform, with the RK3326 `mali-bifrost` patches) for the H700 as well, and add
   the two patches from `kernel/patches/` on top:
   - `0101-h700-deassert-resets.patch`: release the GPU reset line when kbase takes the resets.
   - `0102-h700-runtime-pm-like-panfrost.patch`: reset around power-domain off/on on runtime resume.

   Build options used: `CONFIG_MALI_MIDGARD=m CONFIG_MALI_PLATFORM_NAME=devicetree CONFIG_MALI_REAL_HW=y`.
   No device-tree change was needed on ROCKNIX 20261101. Expected kernel log: `GPU identified as 0x3 arch 7.0.9 r0p0`,
   then `/dev/mali0` and a devfreq node with 420-648 MHz.
2. **Panfrost out of the way.** kbase and Panfrost can't both own the GPU. The H700 would join the
   GPU driver switch the Rockchip devices already have (Panfrost as a module, or left unbound when the
   blob is selected). With Panfrost built in, unbinding it in sysfs leaves a runtime-PM reference that
   makes kbase refuse to probe; `kernel/pmrt_put/` is a test-only helper module for exactly that case.
3. **The blob and the library.** Install the blob as on RK3326, and put the library in front of it
   under three names, so that every way of loading GL finds it first:

   ```
   libafbcfix.so
   libEGL.so.1       -> libafbcfix.so
   libGLESv2.so.2    -> libafbcfix.so
   libGLESv1_CM.so.1 -> libafbcfix.so
   ```

   The library has `DT_NEEDED` entries for `libmali-hook.so.1` and `libmali.so.1`, so those two must be
   on the library path; the blob's own `libEGL.so.1` / `libGLESv2.so.2` / `libGLESv1_CM.so.1` wrappers
   are replaced by the links above. `libgbm.so.1` stays the blob's.

   A cleaner home for the same code is Rockchip's open `libmali-hook`, which already sits in front of
   the blob under every library name. That port is not done.
4. **Contiguous window buffers.** The display driver (sun4i-drm, no IOMMU) only takes contiguous
   memory, and the blob allocates window and GBM buffers from the `system` dma-heap. Point it at the CMA
   heap with its own setting, as a full device path:

   ```
   MALI_WSIALLOC_MEMORY_HEAP_NAME=/dev/dma_heap/default_cma_region
   ```

   It has to be in the environment of the compositor, the front end and every game: a `profile.d` file
   (PortMaster's launchers source `/etc/profile`) plus the sway unit's environment. The heap's name
   depends on the kernel (`ls /dev/dma_heap/`). The default 32 MB CMA pool was enough: sway plus one
   fullscreen client hold five window buffers of 1.4 MB each at 720x480.
5. **RetroArch:** `video_driver = "gl"` when the blob is selected.
6. **Optional:** the wlroots patch from "Known limits", after which the placeholder modifier in
   `afbcfix.c` can go.

## Trying it on a device without building an image

`tools/device/route_r.sh` switches a running ROCKNIX H700 from Panfrost to the blob and back. Nothing is
installed: the kernel module is loaded with `insmod`, the blob reaches the system through an overlay
mount on `/usr/lib`, and `restore` or a reboot undoes everything.

Put these in `/storage/mwl` on the device:

```
mali_kbase.ko            built for the device's exact kernel, with both patches
pmrt_put.ko              kernel/pmrt_put, built for the same kernel
blob/libmali.so.1.10.0
blob/libmali-hook.so.1.10.0
blob/mali/               the blob's libEGL.so.1, libGLESv1_CM.so.1, libGLESv2.so.2, libgbm.so.1
fix/libafbcfix.so        tools/afbcfix/build.sh
gbm_probe  gles1_probe   tools/build_probe.sh
route_r.sh  afbc_probe.sh  kms_test.sh
```

Then, as root:

```
cd /storage/mwl
sh route_r.sh switch          # stop ES + sway, unbind Panfrost, load mali_kbase
FIX=1 sh route_r.sh up        # overlay the blob + the library on /usr/lib, start sway + ES
...
sh route_r.sh restore         # back to Panfrost, sway + ES restarted
```

`FIX=1` is what puts the library in front; without it the blob faults on every texture. By default `up`
gives the blob contiguous memory by bind-mounting the CMA heap over the `system` dma-heap, which needs
no environment. `HEAPENV=1` uses the blob's own setting from step 4 instead.

Counting GPU faults around a run:

```
echo "afbc_probe mark" > /dev/kmsg
... run something ...
dmesg | awk '/afbc_probe mark/ { n = 0 } /GPU fault/ { n++ } END { print n + 0 }'
```

The expected count is 0. Each fault costs about 2 ms and loses that job's output.

## Building

Cross-compile on Linux (or WSL) with `aarch64-linux-gnu-gcc`:

```
bash tools/afbcfix/build.sh     # tools/out/afbcfix/libafbcfix.so
bash tools/build_probe.sh       # tools/out/gbm_probe, gles1_probe, kbfbtrace.so
```

Neither build needs the blob or GL headers: the library links against empty stub libraries with the
right names, and the probes load everything with `dlopen`.

## Testing a build or another blob version

With kbase loaded and sway stopped (`route_r.sh switch`):

```
FIX=1 sh afbc_probe.sh matrix   # texture sizes, formats, upload paths, FBOs, window depth
FIX=1 sh afbc_probe.sh gaps     # cube maps, arrays, buffer uploads, compressed formats, multisampling, depth textures
FIX=1 sh afbc_probe.sh open     # fences, depth kept across a mid-frame call, depth arrays, GLES 1
```

Every line should read `RESULT PASS` (or `RESULT SKIP, FRONT PASS`) with `+0 faults`. Leave out `FIX=1`
to see what the blob does on its own. The probe variants are documented at the top of
`tools/gbm_probe.c`; `tools/device/kms_test.sh` runs a game through SDL KMSDRM.

## Licenses

See `LICENSES.md`. The library, probes and scripts are 0BSD. The kernel patches and the helper module
are GPL-2.0, like the driver they belong to.
