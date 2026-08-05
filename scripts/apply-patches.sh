#!/usr/bin/env bash
# Apply the superproject's patches to its submodules.
#
# Submodules (src/libs, tools/gl4es, tools/emsdk) track upstream remotes, so we
# must not commit local edits into them. The engine fixes this project needs on
# top of upstream live as patch files under patches/<submodule-path>/ and are
# applied onto the submodule working tree here. This makes a fresh
# `git clone --recurse-submodules` + ./setup-build.sh reproduce the build.
#
# Idempotent: a patch that is already applied (detected with --reverse --check)
# is skipped, so this is safe to re-run. Run it again after any
# `git submodule update` that resets a submodule to a clean upstream commit.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PATCHES_DIR="$ROOT/patches"

[ -d "$PATCHES_DIR" ] || { echo "no patches/ directory - nothing to do"; exit 0; }

applied=0
skipped=0
failed=0

# patches/<dir>/ holds patches for the submodule at <dir>
for dir in "$PATCHES_DIR"/*/; do
	[ -d "$dir" ] || continue
	# submodule path relative to repo root, e.g. patches/src-libs/ -> src-libs
	rel="$(basename "$dir")"
	# map the patches dir name to the real submodule path
	case "$rel" in
		src-libs)   sub="$ROOT/src/libs" ;;
		tools-gl4es) sub="$ROOT/tools/gl4es" ;;
		tools-emsdk) sub="$ROOT/tools/emsdk" ;;
		*)          sub="$ROOT/$rel" ;;
	esac

	if [ ! -d "$sub/.git" ] && [ ! -f "$sub/.git" ]; then
		echo "WARNING: $sub is not an initialised submodule; skipping its patches" >&2
		continue
	fi

	for patch in "$dir"*.patch; do
		[ -e "$patch" ] || continue
		if git -C "$sub" apply --reverse --check "$patch" >/dev/null 2>&1; then
			echo "  skip (already applied): $(basename "$patch")"
			skipped=$((skipped + 1))
		elif git -C "$sub" apply --check "$patch" >/dev/null 2>&1; then
			git -C "$sub" apply "$patch"
			echo "  applied: $rel/$(basename "$patch")"
			applied=$((applied + 1))
		else
			echo "ERROR: cannot apply $patch to $sub (upstream changed?)" >&2
			failed=$((failed + 1))
		fi
	done
done

echo "patches: $applied applied, $skipped already present, $failed failed"
[ "$failed" -eq 0 ]
