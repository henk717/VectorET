#!/usr/bin/env bash
# Build VectorET.xdc - a .xdc is just a zip of the app tree.
#
# Everything the app needs must be inside: a WebXDC app has no network, so
# the paks travel in the archive and boot.js reads them from relative URLs.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WEB="$ROOT/web"
OUT="${1:-$ROOT/VectorET.xdc}"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

[ -f "$WEB/manifest.json" ] || { echo "run stage-web.sh first" >&2; exit 1; }
[ -e "$WEB/etl.wasm" ]      || { echo "no etl.wasm - build first" >&2; exit 1; }

# -L: web/ holds symlinks into build/ and assets/; the zip needs real bytes
cp -RL "$WEB/files"          "$STAGE/files"
cp -L  "$WEB/etl.js"         "$STAGE/etl.js"
cp -L  "$WEB/etl.wasm"       "$STAGE/etl.wasm"
cp     "$WEB/boot.js"        "$STAGE/boot.js"
cp     "$WEB/webxdc-net.js"  "$STAGE/webxdc-net.js"
cp     "$WEB/gamepad.cfg"    "$STAGE/gamepad.cfg"
cp     "$WEB/manifest.json"  "$STAGE/manifest.json"
cp     "$WEB/manifest.toml"  "$STAGE/manifest.toml"
cp     "$ROOT/icon.png"      "$STAGE/icon.png"

# The dev shim stands in for a real WebXDC host; shipping it would let the app
# silently fall back to a BroadcastChannel instead of reporting a missing host.
grep -v 'webxdc-dev-shim.js' "$WEB/index.html" > "$STAGE/index.html"

rm -f "$OUT"
( cd "$STAGE" && zip -q -r -9 "$OUT" . -x '.*' )

size_mb=$(( $(stat -f%z "$OUT" 2>/dev/null || stat -c%s "$OUT") / 1048576 ))
echo "built $OUT (${size_mb} MB)"

# Vector rejects archives past 500 MB outright (miniapps/state.rs)
if [ "$size_mb" -gt 500 ]; then
  echo "WARNING: over Vector's 500 MB cap" >&2
fi

unzip -l "$OUT" | tail -3
