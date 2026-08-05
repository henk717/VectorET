source "tools/emsdk/emsdk_env.sh"
./scripts/build-gl4es.sh
./scripts/build-engine.sh
./scripts/build-assets.sh
ASSET_DIR=assets-trimmed ./scripts/stage-web.sh
./scripts/package-xdc.sh
