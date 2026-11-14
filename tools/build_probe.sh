#!/bin/bash
# Build the GBM probe and the kbase trace preload for arm64 (run inside WSL).
set -e
cd "$(dirname "$0")"
mkdir -p out
aarch64-linux-gnu-gcc -O2 -Wall -Wextra -Wno-unused-parameter -o out/gbm_probe gbm_probe.c -ldl -lm
aarch64-linux-gnu-gcc -O2 -shared -fPIC -o out/kbfbtrace.so kbfbtrace.c -ldl -lpthread
aarch64-linux-gnu-gcc -O2 -Wall -Wextra -o out/gles1_probe gles1_probe.c -ldl
ls -la out/gbm_probe out/kbfbtrace.so out/gles1_probe
