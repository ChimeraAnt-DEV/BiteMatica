#!/usr/bin/env bash
#
# Links the Android glue into a real .so on the host (with stubbed Android/fmt headers) and checks
# that its undefined preloader symbols are weak.
#
# This is the regression test for the "Built for a different game build (missing symbol)" failure:
# a strong-undefined pl::render::* symbol makes dlopen fail on a preloader that predates the render
# API, so the mod never loads. The compat header weak-links them, and this asserts it.
#
# Usage: test/link_check.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/build/link-check"
mkdir -p "$OUT"

PRELOADER_INCLUDE="${PRELOADER_INCLUDE:-$ROOT/third_party/preloader-android/include}"
if [ ! -f "$PRELOADER_INCLUDE/pl/Render.hpp" ]; then
  echo "skip: no preloader checkout with pl/Render.hpp at $PRELOADER_INCLUDE" >&2
  exit 0
fi

# The core preloader symbols (mod/modmenu/input/memory) are NOT weak: every preloader exports them.
# Only the render API is weak, because an older preloader does not. This allow-list encodes that.
CORE_SYMBOLS='pl::mod::NativeMod::current|pl::modmenu::|pl::input::|pl::memory::'

ZINCLUDE="${ZLIB_INCLUDE:-/acp-node/include/node}"

# Compile every source the Android .so contains, then link it as a shared object.
SOURCES=(
  src/mod/BiteMaticaMod.cpp
  src/mod/GameWorldSource.cpp
  src/mod/LocalPlayerFeed.cpp
  src/gles/Gles.cpp
  src/gles/SchematicRenderer.cpp
  src/gles/EglFrameSource.cpp
)
CORE_SOURCES=(
  src/nbt/NbtReader.cpp src/nbt/NbtWriter.cpp
  src/format/SchematicFormat.cpp src/format/LitematicReader.cpp src/format/LitematicWriter.cpp
  src/format/SpongeReader.cpp src/format/McEditReader.cpp src/format/BedrockStructureReader.cpp
  src/format/StructureWriter.cpp
  src/mesh/SectionMesher.cpp
  src/world/Verifier.cpp src/world/WorldSource.cpp src/world/Selection.cpp
  src/world/CheatPlacement.cpp
  src/translate/BlockTranslation.cpp src/translate/BlockStateTransform.cpp
  src/core/LiveMaterials.cpp
  src/litematica/PasteJob.cpp src/litematica/EasyPlace.cpp src/litematica/PlacementStore.cpp
  src/render/GameCamera.cpp
  src/ui/MenuBuilder.cpp
  src/render/HudOverlay.cpp
  src/mod/ControllerNavigator.cpp src/mod/JsonConfig.cpp
)

g++ -std=c++20 -fPIC -O1 -shared \
  -I "$ROOT/src" -I "$ROOT/test/stubs" -I "$PRELOADER_INCLUDE" -I "$ZINCLUDE" \
  "${SOURCES[@]/#/$ROOT/}" "${CORE_SOURCES[@]/#/$ROOT/}" \
  -o "$OUT/libbitematica.so" \
  -Wl,--allow-shlib-undefined

echo "linked $OUT/libbitematica.so"

# Every undefined pl::render:: symbol must be WEAK, or an older preloader fails to dlopen the mod.
# (The core pl:: symbols are checked only that they exist; they are not weak by design.)
UNDEF=$(readelf -sW "$OUT/libbitematica.so" | awk '$7=="UND" {print $5, $8}' | c++filt)

# `grep -c` exits non-zero when it counts zero, which `set -e` would treat as a failure, so each
# count is taken with `|| true`.
STRONG_RENDER=$(printf '%s\n' "$UNDEF" | grep "GLOBAL" | grep "pl::render::" || true)
if [ -n "$STRONG_RENDER" ]; then
  echo "FAIL: strongly undefined pl::render symbols (would fail dlopen on an older preloader):" >&2
  printf '%s\n' "$STRONG_RENDER" >&2
  exit 1
fi

WEAK_RENDER=$(printf '%s\n' "$UNDEF" | grep "WEAK" | grep -c "pl::render::" || true)
if [ "${WEAK_RENDER:-0}" -eq 0 ]; then
  echo "FAIL: no weak pl::render symbols found; the compat header is not in effect" >&2
  exit 1
fi

# And confirm the core symbols are still referenced (a sanity check that the mod is intact).
if ! printf '%s\n' "$UNDEF" | grep -qF "pl::modmenu::registerModule"; then
  echo "FAIL: the mod does not reference pl::modmenu::registerModule" >&2
  exit 1
fi

echo "ok: $WEAK_RENDER weak pl::render symbol(s), no strong pl::render references"
