#!/usr/bin/env bash
# One-time setup after cloning. Safe to re-run.
#
#   git clone --recurse-submodules https://github.com/henk717/VectorET
#   cd VectorET && ./setup-build.sh
#
# (If you cloned without --recurse-submodules this script initialises them for you.)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

# 1. Fetch the pinned submodule checkouts (engine libs, gl4es, emsdk).
git submodule update --init --recursive

# 2. Apply the superproject's engine fixes on top of the pristine submodules.
#    These live in patches/ because the submodules track upstream remotes and
#    must not carry local commits. Idempotent.
./scripts/apply-patches.sh

# 3. Install + activate the Emscripten SDK the build uses.
tools/emsdk/emsdk install latest
tools/emsdk/emsdk activate latest

mkdir assets
mkdir assets/etmain
cd assets/etmain
wget https://mirror.etlegacy.com/etmain/pak0.pk3
wget https://mirror.etlegacy.com/etmain/pak1.pk3
wget https://mirror.etlegacy.com/etmain/pak2.pk3
cd ../../
echo
echo "Setup complete. Build with:  ./build.sh"
