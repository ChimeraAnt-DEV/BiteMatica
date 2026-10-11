# Bump instructions: pin the preloader to the commit with the render API

BiteMatica draws the schematic in **world space** through the preloader's per-frame render callback
(`pl/Render.hpp`). GlowberryClient currently pins a preloader commit that **predates** that API, so on
the shipped client the mod finds no render seam and draws nothing (with a named status message).

This document is the one change that makes the real renderer ship. It is a change in
**GlowberryClient**, not in BiteMatica.

## Facts

| Item | Value |
| --- | --- |
| GlowberryClient submodule | `app/src/main/cpp/preloader` |
| Submodule URL | `https://github.com/ChimeraAnt-DEV/preloader-android` |
| Commit GlowberryClient currently pins | `52ac08848020bdc1a443f79623083d5cc1914406` (2026-10-11 02:49 UTC) |
| Commit that adds `pl/Render.hpp` | `167226d30b216445605c7da170edb5626cda8463` (2026-10-11 04:05 UTC) |
| Relationship | `167226d` is the direct child of the current pin (`52ac0884`), 1 commit ahead |

`pl/Render.hpp` does **not** exist at the current pin (a raw fetch of
`include/pl/Render.hpp` at `52ac0884` returns 404). It exists on `main`, and the only commit that
touches it is `167226d`. So the fix is exactly one submodule step forward.

## The change (run in the GlowberryClient checkout)

```bash
cd /path/to/GlowberryClient
git submodule update --init app/src/main/cpp/preloader
cd app/src/main/cpp/preloader
git fetch origin
git checkout 167226d30b216445605c7da170edb5626cda8463
cd ../..
git add app/src/main/cpp/preloader
git commit -m "Pin preloader to 167226d (adds the per-frame render callback API)"
```

If you prefer to pin the tip of `main` instead of this exact commit:

```bash
cd app/src/main/cpp/preloader
git fetch origin main
git checkout origin/main
```

Either works for BiteMatica — it only needs `pl/Render.hpp` to be present and exported. Pinning the
exact commit is recommended so the client stays reproducible.

## Rebuild

```bash
cd /path/to/GlowberryClient
./gradlew assembleDebug    # or the release task you ship
```

## How to confirm it worked

1. Install the rebuilt client and BiteMatica.
2. Load a schematic.
3. The status line must read `World-space GL (sdk, ...)`.
4. If it instead reads `World-space renderer unavailable: no render seam - update GlowberryClient`,
   the running preloader still lacks the render API — re-check that the submodule was checked out and
   the client rebuilt.

BiteMatica's own `test/link_check.sh` already guarantees the mod still **loads** on the old preloader
(the render symbols are weak), so a half-updated client degrades to "draws nothing + says why"
rather than failing to load.

## What the render API provides (for reference)

`pl/Render.hpp` (commit `167226d`) exports:

- `RegisterRenderCallback(RenderCallback)` — invoked once per frame, on the render thread, with the
  GL context current.
- `UnregisterRenderCallbacks(bool all)`.
- `IsRenderCallbackLive()`, `RenderFrameTick()`.
- `ComputeViewProjection(...)`, `MakePerspective(...)`, `MakeView(...)`.
- `ReadLocalPlayerPosition(float[3])`, `ReadLocalPlayerRotation(float[2])`.

`RenderContext` carries `viewProjection` (world→clip, see `docs/MATRIX_SPACE.md`), `view`,
`projection`, `cameraPosition`, `viewportWidth/Height` and `frameTick`, each with a `has*` flag so
the mod fails closed on any field the preloader could not resolve.
