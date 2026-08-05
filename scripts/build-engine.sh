#!/usr/bin/env bash
# Configure and build the ET:Legacy client plus its wasm side modules.
#
# FEATURE_OGG_VORBIS is ON because the trimmed pak ships Vorbis audio.
# Everything network-facing is OFF: a WebXDC app has no internet.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/scripts/env.sh"

emcmake cmake -S "$ROOT/src" -B "$ROOT/build/web" -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SERVER=OFF -DBUILD_CLIENT=ON -DBUILD_MOD=ON \
  -DBUILD_CLIENT_MOD=ON -DBUILD_SERVER_MOD=ON \
  -DBUNDLED_LIBS=ON -DCROSS_COMPILE32=OFF \
  -DFEATURE_CURL=OFF -DFEATURE_SSL=OFF -DFEATURE_AUTH=OFF \
  -DFEATURE_OGG_VORBIS=ON -DFEATURE_THEORA=OFF -DFEATURE_OPENAL=OFF \
  -DFEATURE_FREETYPE=ON -DFEATURE_PNG=OFF \
  -DFEATURE_TRACKER=OFF -DFEATURE_AUTOUPDATE=OFF \
  -DFEATURE_IPV6=OFF -DFEATURE_IRC_CLIENT=OFF -DFEATURE_IRC_SERVER=OFF \
  -DFEATURE_DBMS=OFF -DFEATURE_RENDERER1=ON -DRENDERER_DYNAMIC=OFF \
  -DFEATURE_OMNIBOT=ON -DINSTALL_EXTRA=OFF -DENABLE_MULTI_BUILD=OFF

cmake --build "$ROOT/build/web" -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
