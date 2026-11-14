#!/bin/sh
# Route R probe on a ROCKNIX H700: Panfrost -> ROCKNIX's mali_kbase + the g29p1 blob, then back.
# Nothing persistent: kbase is insmod'ed from $M, Panfrost (built in) is unbound/rebound in sysfs,
# the blob reaches sway/ES through an overlay mount on /usr/lib. `restore` (or a reboot) undoes it.
#
#   route_r.sh state     record the starting state (run first)
#   route_r.sh switch    stop ES + sway, unbind Panfrost, insmod kbase, report the probe
#   route_r.sh gbm       headless GBM/EGL/GLES check through the blob (sway stopped)
#   route_r.sh up        overlay the blob over /usr/lib, start sway + ES on it
#                        (FIX=1: with libafbcfix.so from $M/fix in front, the no-AFBC workaround;
#                        HEAPENV=1: CMA through the blob's own heap setting instead of the bind mount)
#   route_r.sh down      stop sway + ES and drop the overlay, keep kbase
#   route_r.sh restore   stop ES + sway, drop the overlay, rmmod kbase, rebind Panfrost, start sway + ES
#
# Files expected in $M (default /storage/mwl): mali_kbase.ko, gbm_probe, blob/ (libmali.so.1.10.0,
# libmali-hook.so.1.10.0, mali/ wrappers). Logs go to $M/log/.
M=${M:-/storage/mwl}
L=$M/log
mkdir -p "$L"
GPU=$(basename "$(ls -d /sys/bus/platform/devices/*.gpu 2>/dev/null | head -1)")
drv() { basename "$(readlink -f /sys/bus/platform/devices/$GPU/driver 2>/dev/null)" 2>/dev/null; }
say() { echo "[route_r] $*"; }

stop_gui() {
  systemctl stop essway 2>/dev/null
  systemctl stop sway 2>/dev/null
  sleep 1
  pidof sway emulationstation >/dev/null && { say "sway/ES still running: $(pidof sway emulationstation)"; return 1; }
  return 0
}
start_gui() {
  systemctl start sway
  sleep 2
  systemctl start essway
  sleep 4
  say "sway $(pidof sway) es $(pidof emulationstation)"
}

case "$1" in
state)
  {
    date; uname -a; grep -E '^(VERSION|BUILD_ID|BUILD_DATE)' /etc/os-release
    echo "gpu device: $GPU, driver: $(drv)"
    lsmod | grep -E '^(panfrost|mali_kbase)'
    ls -la /dev/dri /dev/mali0 2>&1
    for f in compatible interrupt-names clock-names; do
      echo "dt $f: $(tr '\0' ' ' < /proc/device-tree/soc/gpu@1800000/$f 2>/dev/null)"
    done
    ls /proc/device-tree/soc/gpu@1800000/ 2>&1 | tr '\n' ' '; echo
    grep -E 'Cma(Total|Free)' /proc/meminfo
    df -T /storage | tail -1
    systemctl is-active sway essway
    ls /usr/share/glvnd/egl_vendor.d/ /usr/lib/libEGL* /usr/lib/libgbm* 2>&1
  } > "$L/state.txt" 2>&1
  cat "$L/state.txt"
  ;;
switch)
  [ "$(drv)" = panfrost ] || { say "GPU not on panfrost (driver: $(drv)); stop"; exit 1; }
  stop_gui || exit 1
  dmesg -c > "$L/dmesg_before_switch.txt"
  echo "$GPU" > /sys/bus/platform/drivers/panfrost/unbind || { say "unbind failed"; start_gui; exit 1; }
  sleep 1
  # panfrost's remove leaves a runtime-PM reference that kbase refuses to probe with
  insmod "$M/pmrt_put.ko" || say "pmrt_put failed"
  if ! insmod "$M/mali_kbase.ko"; then
    say "insmod failed; rebinding panfrost"
    echo "$GPU" > /sys/bus/platform/drivers/panfrost/bind; start_gui; exit 1
  fi
  sleep 2
  dmesg > "$L/dmesg_kbase_probe.txt"; sync
  say "driver now: $(drv)"
  # same clock as panfrost (ROCKNIX caps it at 600 MHz there)
  for G in /sys/class/devfreq/*gpu*; do echo 600000000 > $G/max_freq; echo performance > $G/governor; say "gpu $(cat $G/governor) $(cat $G/cur_freq)"; done
  ls -la /dev/mali0 2>&1
  cat /sys/class/misc/mali0/device/gpuinfo 2>/dev/null
  cat /sys/module/mali_kbase/version 2>/dev/null
  ls /sys/class/misc/mali0/device/devfreq/ 2>/dev/null && cat /sys/class/misc/mali0/device/devfreq/*/available_frequencies /sys/class/misc/mali0/device/devfreq/*/cur_freq 2>/dev/null
  grep -i -E "mali|kbase|gpu" "$L/dmesg_kbase_probe.txt" | tail -25
  ;;
gbm)
  [ -e /dev/mali0 ] || { say "no /dev/mali0"; exit 1; }
  B=$M/blob
  ln -sf libmali.so.1.10.0 "$B/libmali.so.1"; ln -sf libmali-hook.so.1.10.0 "$B/libmali-hook.so.1"
  chmod +x "$M/gbm_probe"
  # output straight to the caller (no pipe buffering): a hard lock still shows the last step
  LD_LIBRARY_PATH=$B/mali:$B timeout 60 "$M/gbm_probe" ${FRAMES:-300} 2>&1
  dmesg | tail -8
  ;;
up)
  [ -e /dev/mali0 ] || { say "no /dev/mali0"; exit 1; }
  pidof sway >/dev/null && { say "sway running; stop it first"; exit 1; }
  U=$M/ovl/upper; W=$M/ovl/work
  rm -rf "$M/ovl"; mkdir -p "$U" "$W"
  cp "$M/blob/libmali.so.1.10.0" "$M/blob/libmali-hook.so.1.10.0" "$U/"
  ln -s libmali.so.1.10.0 "$U/libmali.so.1"; ln -s libmali.so.1 "$U/libmali.so"
  ln -s libmali.so.1 "$U/libmali-bifrost-g31-g29p1.so"
  ln -s libmali-hook.so.1.10.0 "$U/libmali-hook.so.1"; ln -s libmali-hook.so.1 "$U/libmali-hook.so"
  for f in libEGL.so.1 libGLESv1_CM.so.1 libGLESv2.so.2 libgbm.so.1; do cp "$M/blob/mali/$f" "$U/$f"; done
  # FIX=1: libafbcfix.so (tools/afbcfix) in front of the blob, under the EGL and GLES names
  if [ -n "$FIX" ] && [ -e "$M/fix/libafbcfix.so" ]; then
    cp "$M/fix/libafbcfix.so" "$U/"
    ln -sf libafbcfix.so "$U/libEGL.so.1"; ln -sf libafbcfix.so "$U/libGLESv2.so.2"; ln -sf libafbcfix.so "$U/libGLESv1_CM.so.1"
    say "afbcfix in front of libEGL.so.1 + libGLESv2.so.2 + libGLESv1_CM.so.1"
  fi
  mount -t overlay overlay -o lowerdir=/usr/lib,upperdir="$U",workdir="$W" /usr/lib || { say "overlay failed"; exit 1; }
  # The blob allocates window and GBM buffers from the "system" dma-heap and imports them into the
  # display driver; sun4i-drm (no IOMMU) only takes contiguous memory. Default: the CMA heap
  # bind-mounted over "system" (proven with sway clients and SDL KMSDRM).
  # HEAPENV=1: the blob's own setting MALI_WSIALLOC_MEMORY_HEAP_NAME (a full device path) instead: in
  # systemd's environment (sway, ES and what ES launches) and in a profile.d file, which PortMaster's
  # launchers read through /etc/profile; bare shells source /tmp/route_r.env. Verified 2026-11-13 with
  # the probes, sway + ES, glmark2, Torus Trooper (sway and SDL KMSDRM) and a LOVE game. The profile.d
  # file is the one persistent piece (down/restore remove it; it does nothing under Panfrost).
  H=$(ls /dev/dma_heap/ | grep -v -E '^(system|reserved)$' | head -1)
  if [ -z "$HEAPENV" ]; then
    mount | grep -q ' /dev/dma_heap/system ' || {
      [ -n "$H" ] && mount --bind "/dev/dma_heap/$H" /dev/dma_heap/system && say "dma-heap system -> $H"; }
  elif [ -n "$H" ]; then
    systemctl set-environment "MALI_WSIALLOC_MEMORY_HEAP_NAME=/dev/dma_heap/$H"
    echo "export MALI_WSIALLOC_MEMORY_HEAP_NAME=/dev/dma_heap/$H" > /tmp/route_r.env
    cp /tmp/route_r.env /storage/.config/profile.d/096-route-r-heap
    say "window buffers from $H (MALI_WSIALLOC_MEMORY_HEAP_NAME; shells: . /tmp/route_r.env)"
  fi
  say "overlay up: $(readlink -f /usr/lib/libEGL.so.1) $(ls -la /usr/lib/libmali.so.1)"
  start_gui
  sleep 3
  for p in $(pidof sway) $(pidof emulationstation); do
    echo "pid $p ($(cat /proc/$p/comm)): $(grep -c libmali /proc/$p/maps) libmali maps"
  done
  grep -E 'CmaFree' /proc/meminfo
  journalctl -u sway -u essway --no-pager -n 15 2>/dev/null | tail -15
  ;;
down)  # stop sway + ES and drop the overlay; kbase stays loaded (for the headless probes, or another `up`)
  stop_gui
  mountpoint -q /usr/lib && { umount /usr/lib 2>/dev/null || umount -l /usr/lib; say "overlay removed"; }
  systemctl unset-environment MALI_WSIALLOC_MEMORY_HEAP_NAME 2>/dev/null
  rm -f /tmp/route_r.env /storage/.config/profile.d/096-route-r-heap
  rm -rf "$M/ovl"
  ;;
restore)
  stop_gui
  mountpoint -q /usr/lib && { umount /usr/lib 2>/dev/null || umount -l /usr/lib; say "overlay removed"; }
  systemctl unset-environment MALI_WSIALLOC_MEMORY_HEAP_NAME 2>/dev/null
  rm -f /tmp/route_r.env /storage/.config/profile.d/096-route-r-heap
  while mount | grep -q ' /dev/dma_heap/system '; do umount /dev/dma_heap/system || break; say "dma-heap bind removed"; done
  if lsmod | grep -q '^mali_kbase'; then
    rmmod mali_kbase || say "rmmod failed: $(lsmod | grep mali_kbase)"
  fi
  lsmod | grep -q '^pmrt_put' && rmmod pmrt_put
  [ "$(drv)" = panfrost ] || echo "$GPU" > /sys/bus/platform/drivers/panfrost/bind
  sleep 1
  say "driver now: $(drv)"
  start_gui
  rm -rf "$M/ovl"
  ;;
*)
  sed -n '2,17p' "$0"; exit 1 ;;
esac
