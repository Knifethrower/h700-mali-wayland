#!/bin/sh
# Phase 0 GLES A/B on a ROCKNIX G31 handheld: glmark2-es2-wayland under sway,
# once fullscreen (presentation included) and once off-screen at 720x720
# (GPU + driver only). Usage: glbench.sh <tag>   (tag = panfrost | libmali)
# Results: /tmp/mwl/<tag>/ ; run with the front end stopped.
TAG=${1:?tag}
D=/tmp/mwl/$TAG
mkdir -p "$D"
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/var/run/0-runtime-dir}
export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-1}

S=5.0
cat > "$D/bench.txt" <<EOF
build:use-vbo=false:duration=$S
build:use-vbo=true:duration=$S
texture:texture-filter=nearest:duration=$S
texture:texture-filter=linear:duration=$S
texture:texture-filter=mipmap:duration=$S
shading:shading=gouraud:duration=$S
shading:shading=blinn-phong-inf:duration=$S
shading:shading=phong:duration=$S
bump:bump-render=high-poly:duration=$S
bump:bump-render=normals:duration=$S
effect2d:kernel=0,1,0;1,-4,1;0,1,0;:duration=$S
effect2d:kernel=1,1,1,1,1;1,1,1,1,1;1,1,1,1,1;:duration=$S
pulsar:light=false:quads=5:texture=false:duration=$S
desktop:blur-radius=5:effect=blur:passes=1:separable=true:windows=4:duration=$S
desktop:effect=shadow:windows=4:duration=$S
buffer:columns=200:interleave=false:update-dispersion=0.9:update-fraction=0.5:update-method=map:duration=$S
buffer:columns=200:interleave=false:update-dispersion=0.9:update-fraction=0.5:update-method=subdata:duration=$S
buffer:columns=200:interleave=true:update-dispersion=0.9:update-fraction=0.5:update-method=map:duration=$S
ideas:speed=duration:duration=$S
jellyfish:duration=$S
terrain:duration=$S
shadow:duration=$S
refract:duration=$S
conditionals:fragment-steps=0:vertex-steps=0:duration=$S
function:fragment-complexity=low:fragment-steps=5:duration=$S
loop:fragment-steps=5:fragment-uniform=false:vertex-steps=5:duration=$S
EOF

{
  echo "date: $(date)"
  uname -a
  grep -E '^gpu.driver' /storage/.config/system/configs/system.cfg
  lsmod | grep -E 'panfrost|mali'
  for f in /sys/class/devfreq/*gpu*; do
    echo "gpu devfreq $f: $(cat $f/governor) cur $(cat $f/cur_freq) avail $(cat $f/available_frequencies)"
  done
  echo "cpu gov: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor) max $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq)"
  mount | grep -E 'libEGL|libGLES|libgbm' | head
  top -b -n 1 | head -15
} > "$D/env.txt" 2>&1

# GPU and CPU clock every 0.5 s while the runs go.
( while :; do
    g=$(cat /sys/class/devfreq/*gpu*/cur_freq 2>/dev/null | head -1)
    c=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq)
    echo "$(cut -d' ' -f1 /proc/uptime) $g $c"
    sleep 0.5
  done ) > "$D/freq.log" 2>&1 &
FP=$!

timeout 300 glmark2-es2-wayland --fullscreen --benchmark-file "$D/bench.txt" > "$D/onscreen.txt" 2>&1
echo "onscreen exit $?" >> "$D/env.txt"
timeout 300 glmark2-es2-wayland --off-screen --size 720x720 --benchmark-file "$D/bench.txt" > "$D/offscreen.txt" 2>&1
echo "offscreen exit $?" >> "$D/env.txt"

kill $FP 2>/dev/null
echo done > "$D/DONE"
