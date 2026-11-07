#!/bin/sh
# Builds the cable and one main<L> per frame size in build/, leaving Lab1/ untouched.
set -e
cd "$(dirname "$0")"
mkdir -p build work
gcc -Wall -o build/cable ../Lab1/cable/cable.c
for L in 125 250 500 1000; do
    rm -rf "build/src$L"
    cp -R ../Lab1/src "build/src$L"
    sed -i '' "s/^#define DATA_CHUNK_SIZE .*/#define DATA_CHUNK_SIZE ($L - DATA_HEADER_SIZE)/" "build/src$L/application_layer.c"
    gcc -Wall -o "build/main$L" "build/src$L"/*.c
done
