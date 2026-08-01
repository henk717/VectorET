#!/usr/bin/env bash
# Build environment for VectorET. Source this before any emcmake/emmake.
#
# emsdk_env.sh misbehaves under zsh, and emsdk itself refuses to run on the
# Xcode python (3.9). Both are sidestepped by setting PATH explicitly and
# pinning EMSDK_PYTHON at a Homebrew interpreter.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
EMSDK="$ROOT/tools/emsdk"

export EMSDK
export EMSDK_PYTHON=/opt/homebrew/bin/python3.11
export EMSDK_NODE="$(ls -d "$EMSDK"/node/*/bin/node 2>/dev/null | head -1)"
export EM_CONFIG="$EMSDK/.emscripten"

PATH="$EMSDK:$EMSDK/upstream/emscripten:$(dirname "$EMSDK_NODE"):/opt/homebrew/bin:$PATH"
export PATH

export GL4ES_ROOT="$ROOT/tools/gl4es"
