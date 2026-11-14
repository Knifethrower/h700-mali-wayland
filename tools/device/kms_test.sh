#!/bin/sh
# kms_test.sh heapenv|bind : Torus Trooper through SDL KMSDRM on the blob, for the Route R heap check.
# Needs kbase loaded (route_r.sh switch), sway stopped and the blob overlay up (route_r.sh up, then
# `systemctl stop essway sway`), or LIBS=1 to use $M/fix + $M/blob through LD_LIBRARY_PATH instead.
#   heapenv: window buffers through MALI_WSIALLOC_MEMORY_HEAP_NAME (no bind mount)
#   bind:    the CMA heap bind-mounted over the system dma-heap for the run
# Prints CPU load, thread count, dma-buf count, GPU faults, and saves the scanout plane to
# $M/log/kms_<mode>.raw (drmgrab from /tmp).
M=${M:-/storage/mwl}
H=/dev/dma_heap/default_cma_region
L="/storage/roms/ports/Torus Trooper.sh"
EXTRA=""
[ "$1" = heapenv ] && EXTRA="MALI_WSIALLOC_MEMORY_HEAP_NAME=$H"
[ -n "$LIBS" ] && EXTRA="$EXTRA LD_LIBRARY_PATH=$M/fix:$M/blob/mali:$M/blob"
while mount | grep -q " /dev/dma_heap/system "; do umount /dev/dma_heap/system || break; done
[ "$1" = bind ] && mount --bind $H /dev/dma_heap/system
sed "s|^\./\$BINARY|SDL_VIDEODRIVER=kmsdrm AFBCFIX_DEBUG=1 $EXTRA ./\$BINARY|" "$L" > /tmp/tt_kms.sh
grep -n "BINARY$" /tmp/tt_kms.sh | tail -1 | cut -c1-150
echo "afbc_probe mark" > /dev/kmsg
(bash /tmp/tt_kms.sh > /tmp/tt_kms.txt 2>&1 &)
sleep 11
P=$(pidof torustrooper.aarch64)
if [ -z "$P" ]; then
  echo "== $1: not running"
  grep -a -i -E "exception|unable|error" /tmp/tt_kms.txt | grep -v ipc-client | head -3 | cut -c1-200
else
  a=$(awk '{print $14+$15}' /proc/$P/stat); sleep 3; b=$(awk '{print $14+$15}' /proc/$P/stat)
  echo "== $1: pid $P cpu $(( (b - a) * 100 / 300 ))% threads $(ls /proc/$P/task | wc -l) dma-bufs $(grep -c -E '^[0-9]+' /sys/kernel/debug/dma_buf/bufinfo) faults $(dmesg | awk '/afbc_probe mark/ { n = 0 } /GPU fault/ { n++ } END { print n + 0 }') main thread in: $(cat /proc/$P/wchan)"
  /tmp/drmgrab "$M/log/kms_$1.raw" /dev/dri/card1 2>&1 | tail -1
fi
pkill -x torustrooper.aa; pkill -x gptokeyb2
for i in 1 2 3 4; do pidof torustrooper.aarch64 >/dev/null || break; sleep 1; done
pidof torustrooper.aarch64 >/dev/null && { echo "needed kill -9"; pkill -9 -x torustrooper.aa; sleep 1; }
pkill -f tt_kms.sh
grep -a "afbcfix" /tmp/tt_kms.txt | tail -1 | cut -c1-200
[ "$1" = bind ] && umount /dev/dma_heap/system
sleep 3
