#!/bin/sh
# Build and test the Abyssmael firmware from src/ against a local fm1-base checkout.
# usage: scripts/build-fw.sh [host|device|all]   (default all)
#   FM1_BASE   path of the fm1-base checkout (default ~/src/fm1-base; see BUILD.md to set it up)
#   BUILD_LABEL  optional word for the splash (test, profile ...); the splash tagline reads "v<src/VERSION> [label] <git sha>[+]"
#   OUT        where the results go (default build-out/ in this repo; ignored by git)
# host   : builds the host simulator, runs src/tests/basic.txt (the expect checks), writes screens and wavs to $OUT/host
# device : builds the device package, writes $OUT/abyssmael.fwsc and prints its sha256
set -e
MODE=${1:-all}
REPO=$(cd "$(dirname "$0")/.." && pwd)
FM1_BASE=${FM1_BASE:-$HOME/src/fm1-base}
OUT=${OUT:-$REPO/build-out}
[ -x "$FM1_BASE/.venv/bin/python" ] || { echo "no $FM1_BASE/.venv: set FM1_BASE or follow BUILD.md" >&2; exit 2; }
# a throwaway app dir inside fm1-base holding a copy of src/ (fm1-base builds apps from its own tree)
APP=abyssmael_build
rm -rf "$FM1_BASE/$APP"; mkdir -p "$FM1_BASE/$APP"; cp -r "$REPO/src/." "$FM1_BASE/$APP/"
# the version on the splash screen (APP_TAGLINE in app.h): src/VERSION, the label if any, this checkout's commit (+ when dirty)
VER=$(cat "$REPO/src/VERSION" 2>/dev/null || echo dev)
SHA=$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo local)
[ -n "$(git -C "$REPO" status --porcelain src 2>/dev/null)" ] && SHA="$SHA+"
TAG="v$VER${BUILD_LABEL:+ $BUILD_LABEL} $SHA"
sed -i "s|^#define AMB_BUILD_TAG .*|#define AMB_BUILD_TAG \"$TAG\"|" "$FM1_BASE/$APP/app.h"
echo "splash: $TAG"
cd "$FM1_BASE"
mkdir -p "$OUT"
if [ "$MODE" = host ] || [ "$MODE" = all ]; then
  .venv/bin/python tools/build.py $APP --host
  rm -rf "$OUT/host"; mkdir -p "$OUT/host"
  ./$APP/build/host/*_host $APP/tests/basic.txt "$OUT/host/" | tee "$OUT/host.log" | tail -n 15
  if grep -q "FAIL\|fail" "$OUT/host.log"; then echo "HOST TESTS FAILED (see $OUT/host.log)"; exit 1; fi
  echo "host tests: $(grep -c '^  ok' "$OUT/host.log") ok"
fi
if [ "$MODE" = device ] || [ "$MODE" = all ]; then
  .venv/bin/python tools/build.py $APP 2>&1 | tee "$OUT/device.log" | tail -n 25
  cp "$APP"/build/*.fwsc "$OUT/abyssmael.fwsc"
  sha256sum "$OUT/abyssmael.fwsc"
fi
rm -rf "$FM1_BASE/$APP"
