#!/usr/bin/env bash
# Stage the built engine + game data into web/ for local testing.
# Run after every build. Uses symlinks, so it is cheap to re-run; the .xdc
# packaging step (stage-xdc.sh) makes real copies.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WEB="$ROOT/web"
BUILD="$ROOT/build/web"
ASSETS="$ROOT/assets"

# ASSET_DIR lets the trimmed pak set stand in for the stock one:
#   ASSET_DIR=assets-trimmed ./scripts/stage-web.sh
ASSET_DIR="${ASSET_DIR:-assets}"
ASSETS="$ROOT/$ASSET_DIR"

[ -d "$ASSETS/etmain" ] || { echo "no $ASSETS/etmain - fetch the paks first" >&2; exit 1; }
[ -f "$BUILD/etl" ] || { echo "no $BUILD/etl - build first" >&2; exit 1; }

ln -sf "$BUILD/etl"      "$WEB/etl.js"
ln -sf "$BUILD/etl.wasm" "$WEB/etl.wasm"

rm -rf "$WEB/files"
mkdir -p "$WEB/files/etmain" "$WEB/files/legacy"

for f in "$ASSETS"/etmain/*.pk3; do
  ln -sf "$f" "$WEB/files/etmain/$(basename "$f")"
done

# cgame/ui ride inside the mod pk3 and are dlopen'd from there; qagame is a
# loose file, exactly as on a native server, for the in-browser listen server
legacy_pk3="$(ls "$BUILD"/legacy/legacy_*.pk3 | head -1)"
ln -sf "$legacy_pk3" "$WEB/files/legacy/$(basename "$legacy_pk3")"
ln -sf "$BUILD/legacy/qagame.mp.wasm32.wasm" "$WEB/files/legacy/qagame.mp.wasm32.wasm"
# Omni-Bot AI side module. The engine loader dlopen()s the bare name
# "omnibot_et.so", which Emscripten resolves against the FS root (/et).
# Stage it at files/ root so it lands at /et/omnibot_et.so in the wasm FS.
[ -f "$BUILD/legacy/omnibot_et.so" ] && ln -sf "$BUILD/legacy/omnibot_et.so" "$WEB/files/omnibot_et.so"

# manifest: path = destination under /et in the wasm FS, url = where to read it
{
  echo '{ "files": ['
  first=1
  for f in "$WEB"/files/etmain/*.pk3 "$WEB"/files/legacy/*.pk3 "$WEB"/files/legacy/*.wasm "$WEB"/files/omnibot_et.so; do
    [ -e "$f" ] || continue
    rel="${f#"$WEB/files/"}"
    size="$(stat -Lf%z "$f" 2>/dev/null || stat -Lc%s "$f")"
    [ $first -eq 1 ] || echo ','
    first=0
    printf '  { "path": "%s", "url": "files/%s", "size": %s }' "$rel" "$rel" "$size"
  done
  echo ''
  echo '] }'
} > "$WEB/manifest.json"

total="$(du -shL "$WEB/files" | cut -f1)"
echo "staged $ASSET_DIR -> web/ (game data: $total)"
