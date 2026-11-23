#!/bin/sh
# Rebuild a firmware's browser simulator (sim.wasm) from its app.c and fm1-base's headers. Needs clang with the wasm32 target.
# usage: scripts/build-sim.sh path/to/app-source-dir  path/to/sim.c  out.wasm
# (the source dir holds app.c and app.h; fm1-base's include/plat.h, plat_uid.h and fastmath.h are fetched next to the build)
set -e
SRC=$1; SIMC=$2; OUT=$3
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
for f in plat.h plat_uid.h fastmath.h; do curl -sfL -o "$TMP/$f" "https://raw.githubusercontent.com/charlesvestal/fm1-base/main/include/$f"; done
clang --target=wasm32 -O2 -nostdlib -ffreestanding -fno-builtin -ffp-contract=off -DPLAT_HOST \
  -I"$TMP" -I"$SRC" -Wl,--no-entry -Wl,--export-dynamic -Wl,-z,stack-size=65536 -Wl,--initial-memory=1048576 \
  "$SIMC" -o "$OUT"
echo "built $OUT"
