#!/usr/bin/env bash
# Build gl4es (desktop GL -> GLES2) as a static PIC library for the client.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/tools/gl4es"
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release -DSTATICLIB=ON \
  -DNOX11=ON -DNOEGL=ON -DNO_LOADER=ON -DNO_INIT_CONSTRUCTOR=ON \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_C_FLAGS="-DDEFAULT_ES=2 -O3 -fPIC"
cmake --build build-web -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
