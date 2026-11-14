#!/bin/bash
# Build libafbcfix.so for arm64 (run inside WSL). It is installed under the names libEGL.so.1 and
# libGLESv2.so.2 and must pull in the blob's own libraries, so it gets DT_NEEDED entries for
# libmali-hook.so.1 and libmali.so.1 through empty stub libraries (nothing of the blob is linked).
set -e
cd "$(dirname "$0")"
O=../out/afbcfix
mkdir -p "$O/stub"
for n in libmali-hook.so.1 libmali.so.1; do
  echo "" | aarch64-linux-gnu-gcc -shared -Wl,-soname,$n -o "$O/stub/$n" -x c -
done
# -Bsymbolic: the library's own references (the eglGetProcAddress table) must reach its own wrappers
# even when libmali was loaded first (a program that opens libgbm before libEGL).
aarch64-linux-gnu-gcc -O2 -Wall -Wextra -fPIC -shared -Wl,-Bsymbolic -o "$O/libafbcfix.so" afbcfix.c \
  -Wl,--no-as-needed -L"$O/stub" -l:libmali-hook.so.1 -l:libmali.so.1 -Wl,--as-needed -ldl -lpthread
aarch64-linux-gnu-readelf -d "$O/libafbcfix.so" | grep -E "NEEDED|SONAME"
aarch64-linux-gnu-nm -D --defined-only "$O/libafbcfix.so" | grep -E " T "
ls -la "$O/libafbcfix.so"
