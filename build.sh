#!/usr/bin/env bash
#
# Builds BiteMatica for Android (arm64-v8a) and packages it as a `.AntEgg`.
#
# Usage:
#   ./build.sh                 # build + package
#   ./build.sh --test          # also run the host test suite first
#   ./build.sh --clean         # wipe build/ and dist/ first
#   ./build.sh --ndk <path>    # explicit NDK, else auto-discovered
#   ./build.sh --preloader <path>  # explicit preloader-android checkout
#
# The preloader SDK is a separate repository. When it is not passed and not already
# present under third_party/, this script clones the pinned tag, so the build never
# depends on a machine-local path silently existing.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ABI="arm64-v8a"
BUILD_DIR="$ROOT/build/android-$ABI"
DIST_DIR="$ROOT/dist"
PRELOADER_ROOT="$ROOT/third_party/preloader-android"
# The SDK's default branch; override with PRELOADER_REF to pin a commit or branch.
PRELOADER_REF="${PRELOADER_REF:-main}"
RUN_TESTS=0
CLEAN=0
NDK=""

while [ $# -gt 0 ]; do
  case "$1" in
    --test) RUN_TESTS=1 ;;
    --clean) CLEAN=1 ;;
    --ndk) NDK="$2"; shift ;;
    --preloader) PRELOADER_ROOT="$2"; shift ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
  shift
done

# The mod version is the single source of truth in egg.json.
VERSION="$(python3 -c "import json;print(json.load(open('$ROOT/packaging/egg.json'))['version'])")"

resolve_ndk() {
  if [ -n "$NDK" ]; then echo "$NDK"; return; fi
  for candidate in "${ANDROID_NDK_HOME:-}" "${ANDROID_NDK_ROOT:-}"; do
    [ -n "$candidate" ] && [ -f "$candidate/build/cmake/android.toolchain.cmake" ] && { echo "$candidate"; return; }
  done
  for sdk in "${ANDROID_HOME:-}" "${ANDROID_SDK_ROOT:-}"; do
    [ -z "$sdk" ] && continue
    if [ -d "$sdk/ndk" ]; then
      found="$(find "$sdk/ndk" -maxdepth 1 -mindepth 1 -type d | sort -r | head -1)"
      [ -n "$found" ] && [ -f "$found/build/cmake/android.toolchain.cmake" ] && { echo "$found"; return; }
    fi
    [ -f "$sdk/ndk-bundle/build/cmake/android.toolchain.cmake" ] && { echo "$sdk/ndk-bundle"; return; }
  done
  echo ""
}

if [ "$CLEAN" = "1" ]; then
  rm -rf "$ROOT/build" "$DIST_DIR"
fi

NDK_PATH="$(resolve_ndk)"
if [ -z "$NDK_PATH" ]; then
  echo "error: Android NDK not found. Pass --ndk or set ANDROID_NDK_HOME/ANDROID_NDK_ROOT/ANDROID_HOME." >&2
  exit 1
fi
TOOLCHAIN="$NDK_PATH/build/cmake/android.toolchain.cmake"
echo "==> NDK: $NDK_PATH"

if [ ! -f "$PRELOADER_ROOT/include/pl/Mod.hpp" ]; then
  echo "==> Cloning preloader-android ($PRELOADER_REF) into third_party/"
  mkdir -p "$ROOT/third_party"
  git clone --depth 1 --branch "$PRELOADER_REF" \
    https://github.com/ChimeraAnt-DEV/preloader-android.git "$PRELOADER_ROOT"
fi
echo "==> Preloader: $PRELOADER_ROOT"

if [ "$RUN_TESTS" = "1" ]; then
  echo "==> Host tests"
  HOST_BUILD="$ROOT/build/host"
  cmake -S "$ROOT" -B "$HOST_BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "$HOST_BUILD" --target bitematica_tests bitematica_world_tests bitematica_litematica_tests
  python3 "$ROOT/test/make_fixtures.py" "$ROOT/test/fixtures"
  "$HOST_BUILD/bitematica_tests"
  "$HOST_BUILD/bitematica_world_tests"
  "$HOST_BUILD/bitematica_litematica_tests"
fi

echo "==> Configuring Android build"
cmake -S "$ROOT" -B "$BUILD_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DANDROID_ABI="$ABI" \
  -DANDROID_PLATFORM=android-24 \
  -DCMAKE_BUILD_TYPE=Release \
  -DPRELOADER_ANDROID_ROOT="$PRELOADER_ROOT"

echo "==> Building libbitematica.so"
cmake --build "$BUILD_DIR" --target bitematica --parallel

LIBRARY="$BUILD_DIR/out/$ABI/libbitematica.so"
if [ ! -f "$LIBRARY" ]; then
  echo "error: expected library not produced: $LIBRARY" >&2
  exit 1
fi

echo "==> Packaging .AntEgg"
python3 "$ROOT/tools/pack_antegg.py" \
  --manifest "$ROOT/packaging/egg.json" \
  --preloader-manifest "$ROOT/packaging/manifest.json" \
  --icon "$ROOT/assets/icon/icon.png" \
  --library "$LIBRARY" \
  --entry bin/libbitematica.so \
  --config "$ROOT/packaging/config" \
  --schematics "$ROOT/assets/schematics" \
  --extra "data/block_colors.json=$ROOT/data/block_colors.json" \
  --extra "data/block_translation.json=$ROOT/data/block_translation.json" \
  --out "$DIST_DIR/BiteMatica-$VERSION.antegg"

echo "==> Done: $DIST_DIR/BiteMatica-$VERSION.antegg"
