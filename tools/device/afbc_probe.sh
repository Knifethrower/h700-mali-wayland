#!/bin/sh
# AFBC-switch probe on a ROCKNIX H700 (no AFBC in its G31): does the RK3326 g29p1 blob read any setting
# that keeps it off AFBC? Black-box only: what it asks libc for (envtrace.so), then trying those settings.
# Needs kbase loaded (route_r.sh switch) and sway stopped.
#
#   afbc_probe.sh base             textured GBM probe as is (expect RESULT FAIL + GPU faults: AFBC)
#   afbc_probe.sh trace            the same under envtrace: every env var / file libmali asks for
#   afbc_probe.sh try VAR=VAL ...  the textured probe with those settings: RESULT and new GPU faults
#   afbc_probe.sh sweep FILE       'try' each VAR from FILE (one name per line) with each of $VALUES
#   afbc_probe.sh cfg LINE ...     the probe with those lines in a config file (both MALI_*_CONFIG vars)
#   afbc_probe.sh syntax           which config-file syntax the parser takes (options that print things)
#   afbc_probe.sh found            the AFBC options named in the blob's strings, in each syntax, + env forms
#   afbc_probe.sh matrix           which GL usage trips AFBC: texture sizes, formats, upload paths, FBOs, depth
#   afbc_probe.sh gaps             second round: cube maps, 3D/array, buffer uploads, compressed, manual mips,
#                                  color renderbuffers, multisampling, depth textures, mid-frame flushes
#   afbc_probe.sh open             third round: fences, depth kept across a mid-frame call, depth arrays, GLES 1
#   afbc_probe.sh v VAR=VAL ...    one matrix line for these probe settings
#   afbc_probe.sh when VAR=VAL ... where in the run a fault lands: probe steps, job submits, job events in order
#
# Files in $M (default /storage/mwl): blob/ (g29p1 libmali.so.1.10.0, libmali-hook.so.1.10.0, mali/ wrappers),
# gbm_probe, gles1_probe, envtrace.so. GBMP_TEX=1 samples a texture, the case that faulted in Phase 1.
# FIX=1 (v, matrix, gaps, open, when): put $M/fix (libafbcfix.so as libEGL.so.1, libGLESv2.so.2 and
# libGLESv1_CM.so.1) in front of the blob.
M=${M:-/storage/mwl}
B=${B:-$M/blob}   # B=$M/rx: another build (e.g. rxp0) with libEGL.so.1/libGLESv2.so.2/libgbm.so.1 links
L=$M/log
mkdir -p "$L"
[ -e "$B/libmali.so.1.10.0" ] && ln -sf libmali.so.1.10.0 "$B/libmali.so.1"
[ -e "$B/libmali-hook.so.1.10.0" ] && ln -sf libmali-hook.so.1.10.0 "$B/libmali-hook.so.1"
chmod +x "$M/gbm_probe"
[ -e /dev/mali0 ] || { echo "no /dev/mali0 (route_r.sh switch first)"; exit 1; }
# The blob's GBM buffers get imported into sun4i-drm, which only takes contiguous memory: point the blob
# at the CMA heap with its own setting (a full device path; without it eglSwapBuffers fails with
# EGL_BAD_ALLOC). A bind mount of that heap over "system" (route_r.sh up) does the same.
mount | grep -q " /dev/dma_heap/system " || {
  H=$(ls /dev/dma_heap/ | grep -v -E "^(system|reserved)$" | head -1)
  [ -n "$H" ] && export MALI_WSIALLOC_MEMORY_HEAP_NAME=/dev/dma_heap/$H; }

# GPU faults since the last mark. A plain count of dmesg lines stalls once the ring buffer is full
# (found 2026-11-13: after ~700 faults every run read "+0"), so each run writes a marker first.
mark() { echo "afbc_probe mark" > /dev/kmsg; }
faults() { dmesg | awk '/afbc_probe mark/ { n = 0 } /GPU fault/ { n++ } END { print n + 0 }'; }
probe() { # probe [VAR=VAL ...]: one line: result, new faults
  mark; n0=0
  r=$(env "$@" GBMP_RENDER_ONLY=1 GBMP_TEX=1 LD_LIBRARY_PATH="$B/mali:$B" timeout 30 "$M/gbm_probe" 120 2>&1 |
      grep -o -E "RESULT (PASS|FAIL)|eglInitialize failed|failed.*" | head -1)
  n1=$(faults)
  echo "${r:-no result} | +$((n1 - n0)) GPU faults | $*"
}

variant() { # variant VAR=VAL ...: result | new faults | frame time | corner pixels | GL error | settings
  mark; n0=0
  env "$@" GBMP_RENDER_ONLY=1 LD_LIBRARY_PATH="${FIX:+$M/fix:}$B/mali:$B" timeout 30 "$M/gbm_probe" ${FRAMES:-120} > "$L/variant.txt" 2>&1
  rc=$?
  n1=$(faults)
  r=$(grep -o -E "RESULT (PASS|FAIL|SKIP)" "$L/variant.txt" | head -1)
  ms=$(grep -o -E "[0-9.]+ ms/frame" "$L/variant.txt" | head -1)
  px=$(grep -o -E "pixels [0-9,]+ / [0-9,]+" "$L/variant.txt" | head -1)
  ge=$(grep -o -E "gl error after upload: [0-9a-fx]+" "$L/variant.txt" | sed 's/gl error after upload: /glerr /')
  fr=$(grep -o -E "FRONT (PASS|FAIL)" "$L/variant.txt" | head -1)
  [ -n "$fr" ] && r="$r, $fr"
  # setup notes worth seeing: incomplete FBOs, refused images, missing calls, the fix library's counters
  xi=$(grep -E "fbo status 0x[0-9a-f]+|image of .*\(nil\)|did not link|needs GBMP|^no |refused|^bad |image unit bind|can.t map|mid read|fence wait|\[afbcfix\] (tex|depth-tested)" "$L/variant.txt" |
       grep -v "fbo status 0x8cd5" | sed 's/^\[afbcfix\] //' | tr '\n' ';')
  echo "${r:-no result rc=$rc} | +$((n1 - n0)) faults | ${ms:--} | ${px:--} | ${ge:--} | $*${xi:+ | $xi}"
}

g1() { # g1 VAR=VAL ...: the GLES 1 probe, one line like variant
  mark; n0=0
  chmod +x "$M/gles1_probe"
  env "$@" LD_LIBRARY_PATH="${FIX:+$M/fix:}$B/mali:$B" timeout 30 "$M/gles1_probe" ${FRAMES:-120} > "$L/variant.txt" 2>&1
  rc=$?
  n1=$(faults)
  r=$(grep -o -E "RESULT (PASS|FAIL)" "$L/variant.txt" | head -1)
  px=$(grep -o -E "pixels [0-9,]+ / [0-9,]+" "$L/variant.txt" | head -1)
  xi=$(grep -E "fbo status 0x[0-9a-f]+|gl error|missing|failed|FRONT FAIL|\[afbcfix\] tex" "$L/variant.txt" |
       grep -v -E "fbo status 0x8cd5|gl error after setup: 0$|gl error 0$" | sed 's/^\[afbcfix\] //' | tr '\n' ';')
  echo "${r:-no result rc=$rc} | +$((n1 - n0)) faults | ${px:--} | GLES 1 $*${xi:+ | $xi}"
}

case "$1" in
base)
  probe
  ;;
trace)
  mark; n0=0
  LD_PRELOAD="$M/envtrace.so" GBMP_RENDER_ONLY=1 GBMP_TEX=1 LD_LIBRARY_PATH="$B/mali:$B" timeout 30 \
    "$M/gbm_probe" 60 > "$L/afbc_trace_raw.txt" 2>&1
  echo "probe: $(grep -o -E 'RESULT (PASS|FAIL)' "$L/afbc_trace_raw.txt" | head -1), +$(( $(faults) - n0 )) GPU faults"
  grep '^\[envtrace\]' "$L/afbc_trace_raw.txt" | sort -u > "$L/afbc_trace.txt"
  echo "== environment variables asked for:"
  grep -E '\] (secure_)?getenv ' "$L/afbc_trace.txt" | sed 's/^\[envtrace\] //'
  echo "== files opened or probed (not /dev, /proc, /sys):"
  grep -E '\] (open|openat|fopen|access) ' "$L/afbc_trace.txt" | grep -v -E ' /(dev|proc|sys)/' | sed 's/^\[envtrace\] //'
  ;;
try)
  shift
  probe "$@"
  ;;
sweep)
  for v in $(grep -v '^#' "$2"); do
    for val in ${VALUES:-0 1}; do
      probe "$v=$val"
    done
  done
  ;;
cfg)  # cfg LINE ...: the probe with those lines in a config file named by both config variables
  shift
  mkdir -p "$M/cfg"; f=$M/cfg/try.config
  printf '%s\n' "$@" > "$f"
  echo "$(probe MALI_PLATFORM_CONFIG="$f" MALI_DEBUG_CONFIG="$f" | cut -d'|' -f1-2)| cfg: $*"
  ;;
syntax)  # which config syntax the parser takes: options with visible output, output lines vs. no config
  mkdir -p "$M/cfg"; f=$M/cfg/try.config
  run() { env "$@" GBMP_RENDER_ONLY=1 GBMP_TEX=1 LD_LIBRARY_PATH="$B/mali:$B" timeout 30 "$M/gbm_probe" 30 2>&1; }
  run > "$L/syn_base.txt"
  echo "no config: $(wc -l < "$L/syn_base.txt") lines"
  for case in upper lower; do
    for sep in "=" " " ": "; do
      : > "$f"
      for k in GLES_VERBOSE REPORT_MEM_USAGE GLES_PRINT_TEST_INFO; do
        [ $case = lower ] && k=$(echo $k | tr A-Z a-z)
        echo "$k$sep""1" >> "$f"
      done
      run MALI_PLATFORM_CONFIG="$f" MALI_DEBUG_CONFIG="$f" > "$L/syn.txt"
      echo "$case '$sep': $(wc -l < "$L/syn.txt") lines; new: $(grep -v -x -F -f "$L/syn_base.txt" "$L/syn.txt" | head -3 | tr '\n' '|')"
    done
  done
  ;;
found)  # the AFBC options named in g29p1's strings (read 2026-11-13 with the user's OK)
  for case in upper lower; do
    for sep in "=" " " ": "; do
      t=GLES_DISABLE_TEXTURE_AFBC s=GLES_DISABLE_SURFACE_AFBC
      [ $case = lower ] && t=$(echo $t | tr A-Z a-z) s=$(echo $s | tr A-Z a-z)
      sh "$0" cfg "$t$sep""1" "$s$sep""1"
    done
  done
  sh "$0" cfg "GLES_DISABLE_TEXTURE_AFBC=true" "GLES_DISABLE_SURFACE_AFBC=true"
  sh "$0" cfg "GLES_DISABLE_TEXTURE_AFBC=1" "GLES_DISABLE_SURFACE_AFBC=1" "GLES_FORCE_LINEAR_LAYOUT_UNCOMPRESSED_TEXTURE=1" "WAYLAND_AFBC=0" "GLES_AFBC_UPLOAD_HEURISTIC=0"
  probe GLES_DISABLE_TEXTURE_AFBC=1 GLES_DISABLE_SURFACE_AFBC=1
  probe MALI_GLES_DISABLE_TEXTURE_AFBC=1 MALI_GLES_DISABLE_SURFACE_AFBC=1
  probe MALI_DRM_MODIFIER=DRM_FORMAT_MOD_LINEAR
  probe MALI_DFC_ALLOW_ARM_COMPRESSION=0
  ;;
matrix)  # GL-usage map (probe variants are documented in gbm_probe.c); one line per variant
  echo "== baselines"
  variant
  variant GBMP_TEX=1
  variant GBMP_TEX=1 GBMP_NOCHECK=1
  echo "== sizes (rgba, glTexImage2D with data)"
  for s in 2 4 8 15 16 17 31 32 33 128 256 512 1024 100x60 64x8 8x64; do variant GBMP_TEX=1 GBMP_TEXSIZE=$s; done
  echo "== formats (64x64)"
  for f in rgb 565 4444 5551 l la a; do variant GBMP_TEX=1 GBMP_TEXFMT=$f; done
  for f in rgba rgba8 srgb8a8 r8 rg8 rgba16f rgb10a2; do variant GBMP_TEX=1 GBMP_ES=3 GBMP_TEXFMT=$f; done
  echo "== upload paths (rgba 64x64)"
  for p in sub rows perframe reimage copy fbo fboframe; do variant GBMP_TEX=1 GBMP_TEXPATH=$p; done
  variant GBMP_TEX=1 GBMP_ES=3 GBMP_TEXPATH=storage
  variant GBMP_TEX=1 GBMP_ES=3 GBMP_TEXPATH=sub
  echo "== sampling state"
  variant GBMP_TEX=1 GBMP_TEXFILTER=linear
  variant GBMP_TEX=1 GBMP_TEXWRAP=repeat
  variant GBMP_TEX=1 GBMP_TEXMIP=gen
  variant GBMP_TEX=1 GBMP_TEXMIP=gen GBMP_TEXSIZE=256
  variant GBMP_TEX=1 GBMP_TEXMIP=gen GBMP_TEXPATH=sub
  echo "== FBO variants"
  variant GBMP_TEX=1 GBMP_TEXPATH=fbo GBMP_TEXSIZE=256
  variant GBMP_TEX=1 GBMP_TEXPATH=fbo GBMP_TEXFMT=565
  variant GBMP_TEX=1 GBMP_TEXPATH=fbo GBMP_FBODEPTH=16
  variant GBMP_TEX=1 GBMP_TEXPATH=fbo GBMP_FBODEPTH=24
  variant GBMP_TEX=1 GBMP_TEXPATH=fboframe GBMP_FBODEPTH=16
  echo "== window depth / stencil (no texture)"
  variant GBMP_DEPTH=16
  variant GBMP_DEPTH=24
  variant GBMP_DEPTH=24 GBMP_NOCHECK=1
  variant GBMP_DEPTH=24 GBMP_STENCIL=8
  variant GBMP_DEPTH=24 GBMP_STENCIL=8 GBMP_NOCHECK=1
  ;;
gaps)  # second round (2026-11-13): the GL paths the first matrix left out
  T="GBMP_TEX=1"; E3="GBMP_TEX=1 GBMP_ES=3"
  echo "== cube maps"
  for p in cube cubesub cubeself cubeself6 cubefbo cubefboself; do variant $T GBMP_TEXPATH=$p; done
  variant $T GBMP_TEXPATH=cube GBMP_TEXMIP=gen
  variant $T GBMP_TEXPATH=cube GBMP_TEXSIZE=256
  variant $T GBMP_TEXPATH=cubefbo GBMP_FBODEPTH=16
  variant $E3 GBMP_TEXPATH=cubestorage
  echo "== 3D and array textures (ES 3)"
  for p in t3d t3dstorage t3dself arr arrstorage arrfbo; do variant $E3 GBMP_TEXPATH=$p; done
  echo "== uploads from a pixel unpack buffer (ES 3)"
  for p in pbo pbosub pbooff; do variant $E3 GBMP_TEXPATH=$p; done
  echo "== compressed textures"
  variant $T GBMP_TEXPATH=etc1
  variant $E3 GBMP_TEXPATH=etc2
  variant $E3 GBMP_TEXPATH=etc2 GBMP_TEXSIZE=256
  echo "== manual mip levels (256 and 1024)"
  for m in manual manualrev; do for s in 256 1024; do variant $T GBMP_TEXMIP=$m GBMP_TEXSIZE=$s; done; done
  echo "== FBO color renderbuffers"
  for f in 565 4444 5551; do variant $T GBMP_TEXPATH=fborb GBMP_TEXFMT=$f; done
  variant $E3 GBMP_TEXPATH=fborb GBMP_TEXFMT=rgba8
  variant $T GBMP_TEXPATH=fborb GBMP_TEXFMT=565 GBMP_FBODEPTH=16
  echo "== multisampling"
  variant $E3 GBMP_TEXPATH=fbomsaa
  variant $E3 GBMP_TEXPATH=fbomsaa GBMP_SELFIMG=1
  variant $E3 GBMP_TEXPATH=fbomsaa GBMP_SELFIMG=1 GBMP_FBODEPTH=24
  variant $T GBMP_TEXPATH=fbomsrtt
  variant $T GBMP_TEXPATH=fbomsrtt GBMP_SELFIMG=1
  variant $T GBMP_TEXPATH=fbomsrtt GBMP_SELFIMG=1 GBMP_FBODEPTH=16
  variant $T GBMP_TEXPATH=fbomsrtt GBMP_SELFIMG=1 GBMP_FBODEPTH=24
  echo "== depth textures, rendered every frame and sampled"
  for b in 16 24 248; do variant $T GBMP_TEXPATH=shadow GBMP_FBODEPTH=$b; done
  for b in 16 24 32 248; do variant $E3 GBMP_TEXPATH=shadow GBMP_FBODEPTH=$b; done
  for b in 16 24; do variant $T GBMP_TEXPATH=shadow GBMP_FBODEPTH=$b GBMP_SELFIMG=1; done
  variant $E3 GBMP_TEXPATH=shadow GBMP_FBODEPTH=32 GBMP_SELFIMG=1
  echo "== array textures: more"
  variant $E3 GBMP_TEXPATH=arrfbo GBMP_FBODEPTH=24
  variant $E3 GBMP_TEXPATH=arr GBMP_TEXSIZE=256
  echo "== manual mip levels, not a power of two"
  for m in manual manualrev; do variant $T GBMP_TEXMIP=$m GBMP_TEXSIZE=100x60; done
  echo "== window depth and flushes before the swap (the swapped buffer itself is checked)"
  W="GBMP_NOCHECK=1 GBMP_FRONTCHECK=1 GBMP_DEPTH=24"
  variant $W
  for m in endfinish endread; do variant $W GBMP_MIDFLUSH=$m; done
  variant $W GBMP_STENCIL=8 GBMP_MIDFLUSH=endfinish
  for m in endfinish endread endfence; do variant $W GBMP_ES=3 GBMP_MIDFLUSH=$m; done
  for o in farfirst nearfirst; do
    for m in none read copy flush finish; do variant $W GBMP_MIDFLUSH=$m GBMP_MIDORDER=$o; done
  done
  echo "== the plain window with the probe's own final check (glFinish + glReadPixels before the last swap)"
  variant GBMP_DEPTH=24
  variant GBMP_DEPTH=24 GBMP_STENCIL=8
  variant GBMP_DEPTH=16
  ;;
open)  # third round (2026-11-13 night): fences, depth kept across a mid-frame call, depth arrays, GLES 1
  E3="GBMP_ES=3"; W="GBMP_NOCHECK=1 GBMP_FRONTCHECK=1 GBMP_DEPTH=24"
  echo "== fences, the wait right before the swap"
  for m in endfence endfence0 endeglfence endoldfence; do variant $W $E3 GBMP_MIDFLUSH=$m; done
  echo "== fences, the wait between two depth-tested draws"
  for o in farfirst nearfirst; do
    for m in fence fence0 eglfence oldfence; do variant $W $E3 GBMP_MIDFLUSH=$m GBMP_MIDORDER=$o; done
  done
  echo "== depth kept across a mid-frame readback or finish (nearfirst fails when depth is lost)"
  for m in read finish; do variant $W GBMP_MIDFLUSH=$m GBMP_MIDORDER=nearfirst; done
  for m in read finish; do variant $W $E3 GBMP_MIDFLUSH=$m GBMP_MIDORDER=nearfirst; done
  variant $W GBMP_STENCIL=8 GBMP_MIDFLUSH=read GBMP_MIDORDER=nearfirst
  variant $W GBMP_MIDFLUSH=read GBMP_MIDORDER=farfirst
  echo "== depth array textures"
  for b in 16 24 32; do variant GBMP_TEX=1 $E3 GBMP_TEXPATH=arrshadow GBMP_FBODEPTH=$b; done
  echo "== GLES 1"
  g1; g1 G1P_MIP=1; g1 G1P_FBO=1; g1 G1P_FINISH=1; g1 G1P_FBO=1 G1P_MIP=1 G1P_FINISH=1
  ;;
v)  # v VAR=VAL ...: one matrix line for these probe settings
  shift
  variant "$@"
  ;;
when)  # when VAR=VAL ...: probe steps, job submits and job events in order (kbfbtrace.so: kernel interface only)
  shift
  mark; n0=0
  env "$@" GBMP_SYNC=1 GBMP_RENDER_ONLY=1 LD_PRELOAD="$M/kbfbtrace.so" LD_LIBRARY_PATH="${FIX:+$M/fix:}$B/mali:$B" timeout 30     "$M/gbm_probe" ${FRAMES:-4} > "$L/when_raw.txt" 2>&1
  echo "+$(( $(faults) - n0 )) faults; $(grep -o -E 'RESULT (PASS|FAIL|SKIP)' "$L/when_raw.txt" | head -1); $(wc -l < "$L/when_raw.txt") log lines; $*"
  grep -E '^\[[0-9.]+\] (texture|upload|frame|final|glFinish|depth|compile)|JOB_SUBMIT|  atom |event code|fbo status|gl error|pixels' "$L/when_raw.txt"
  dmesg | grep -E "GPU fault|error detected from slot|job status" | tail -4
  ;;
*)
  sed -n '2,18p' "$0"; exit 1 ;;
esac
