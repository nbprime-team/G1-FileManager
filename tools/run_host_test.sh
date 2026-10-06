#!/bin/sh
# Compile fm.cpp on the host (with a shim of the G1 SDK) and run the font unit tests.
set -e
cd "$(dirname "$0")/.."
FONT_DIR="${1:-build/font-cjk}"
[ -f "$FONT_DIR/font_uf.h" ] && [ -f build/font-gb/font_gb.h ] || { echo "missing $FONT_DIR (run: make font)"; exit 1; }
mkdir -p build/host
g++ -std=gnu++11 -O1 -g -fno-exceptions -fno-rtti -w \
    -I host/shim -I "$FONT_DIR" -I build/font-gb -I src -I lang \
    -o build/host/host_test \
    host/host_test.cpp
exec ./build/host/host_test
