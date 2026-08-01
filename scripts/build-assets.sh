#!/usr/bin/env bash
# Produce assets-trimmed/ from the stock paks. Wraps trim-pak.py so the mod
# binaries are always fed in - the mod registers assets from string literals,
# and missing them silently strips textures off things the mod spawns.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MAPS="${MAPS:-radar goldrush}"

MOD_PAK="$(ls "$ROOT"/build/web/legacy/legacy_*.pk3 2>/dev/null | head -1 || true)"
[ -n "$MOD_PAK" ] || { echo "build the mod first (cmake --build build/web)" >&2; exit 1; }

exec python3 "$ROOT/scripts/trim-pak.py" \
  --maps $MAPS \
  --src "$ROOT/assets/etmain" \
  --out "$ROOT/assets-trimmed/etmain" \
  --mod-pak "$MOD_PAK" \
  --mod-binary "$ROOT/build/web/legacy/qagame.mp.wasm32.wasm" \
  --transcode-audio \
  "$@"
