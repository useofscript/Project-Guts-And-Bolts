#!/bin/sh
# Builds website/app/gbcrypto.wasm (the account crypto for the website) from
# Monocypher. Needs clang with the wasm32 target and wasm-ld. Run after a normal
# CMake configure, which downloads Monocypher into build/_deps.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../.."
mono="${MONOCYPHER_DIR:-$root/build/_deps/monocypher-src/src}"
clang --target=wasm32 -O2 -nostdlib -fno-builtin -ffreestanding \
    -Wl,--no-entry -Wl,--strip-all -Wl,--initial-memory=42991616 \
    -I"$mono" "$here/gbcrypto.c" "$mono/monocypher.c" \
    -o "$root/website/app/gbcrypto.wasm"
echo "Built website/app/gbcrypto.wasm"
