#!/usr/bin/env bash
#
# Fails if schematic geometry could be emitted through the HUD draw-command channel.
#
# The rule this guards: a schematic is drawn in world space through GL, never as screen-space HUD
# draw commands. A "software overlay" is not world-space rendering, so it must not exist. This
# script is the mechanical half of that rule — the unit-test half lives in
# test/host_tests.cpp (testChromeNeverDrawsSchematicGeometry), which asserts the chrome command
# vocabulary has no geometry primitive at all.
#
# Two checks:
#   1. The HUD command vocabulary (`UiCommand::Kind`) has no triangle/geometry primitive. If a
#      geometry kind is added, this fails and the reviewer must justify it.
#   2. No HUD path builds geometry from a schematic: `MeshBuilder` must not exist, and the chrome
#      command builder must not take a schematic or camera.
#
# Usage: test/hud_geometry_guard.sh
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

fail=0

# 1. The chrome command vocabulary must be Rect/Text only.
if grep -qE 'enum class Kind \{[^}]*Triangle' src/render/HudOverlay.hpp; then
  echo "FAIL: UiCommand::Kind has a Triangle primitive; schematic geometry could be drawn via HUD" >&2
  fail=1
fi
if grep -q 'makeTriangle' src/render/HudOverlay.hpp; then
  echo "FAIL: UiCommand::makeTriangle exists; schematic geometry could be drawn via HUD" >&2
  fail=1
fi

# 2. The old screen-space mesh projector must not exist.
if [ -e src/render/MeshBuilder.hpp ]; then
  echo "FAIL: src/render/MeshBuilder.hpp exists (screen-space projector)" >&2
  fail=1
fi

# The chrome builder must not take a schematic or a camera: it draws chrome, not geometry.
if grep -qE 'buildCommands\([^)]*Schematic' src/render/HudOverlay.hpp; then
  echo "FAIL: HudOverlay::buildCommands takes a Schematic; chrome must not draw geometry" >&2
  fail=1
fi
if grep -qE 'buildMeshCommands' src/render/HudOverlay.hpp; then
  echo "FAIL: HudOverlay::buildMeshCommands exists; the HUD can draw schematic geometry" >&2
  fail=1
fi

if [ "$fail" -ne 0 ]; then
  exit 1
fi

echo "ok: HUD is chrome-only; no schematic geometry path through draw commands"
