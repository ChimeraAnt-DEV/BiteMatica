# BiteMatica — repository guide

A client-side schematic mod for **Minecraft: Bedrock Edition on Android**, shipped as a
**GlowberryClient `.AntEgg`** native mod. It is a from-scratch Bedrock/Android
reimplementation of the *idea* behind Java Edition's Litematica + malilib. It reuses no
Litematica/malilib code, and it does not run Java Edition mods.

## Layout

```text
src/nbt/        NBT decode/encode (gzip + zlib + raw), zlib-backed inflate
src/format/     format readers + StructureWriter (.mcstructure) + LitematicWriter
src/core/       Schematic, BlockPalette, MaterialList, LiveMaterials, BlockPos
src/mesh/       SectionMesher (16^3 sections, exposed-face cull, greedy merge)
src/world/      Placement, Verifier, WorldSource, Selection, CheatPlacement
src/translate/  BlockTranslation (Java -> Bedrock block states)
src/render/     MathTypes (matrices/frustum), HudOverlay (2D toolbar), ViewCamera (legacy)
src/gles/       Gles (runtime-resolved entry points), SchematicRenderer (GL state save/restore)
src/mod/        Config, JsonConfig, SchematicLibrary, ControllerNavigator,
                GameWorldSource, SdkCompat (weak render API), BiteMaticaMod (glue)
test/           host test suites + fixture generator
tools/          pack_antegg.py, generate_demo.py, generate_icon.py, verify_antegg.py
packaging/      egg.json, manifest.json, default config + schema
assets/         demo schematic, generated icon
```

## Two drawing paths (do not mix them)

World-space geometry goes through **GL** via the preloader's `pl/Render.hpp` per-frame callback
(render thread, GL context current), and is depth-tested so terrain occludes it. The toolbar and
material panel go through **Mod Menu HUD draw commands** and are always on top. A toolbar that
terrain could occlude is not a toolbar, so the two must stay separate.

The mod's own matrix type (`render::Mat4`) is deliberately separate from the SDK's
(`pl::cosmetics::Mat4`) so the host tests never pull in the preloader; the two are converted at the
single boundary in `BiteMaticaMod.cpp` (`toSdkMatrix`).

## Build

Both scripts auto-discover the NDK and clone `preloader-android` (its `main` branch, or
`PRELOADER_REF`) into `third_party/` when it is absent. There is no machine-local path
dependency. The SDK repo publishes no release tags, so a branch is the only thing to pin.

```bash
./build.sh --test         # host tests, then build + package
./build.sh                # build + package
./build.ps1 -Test         # Windows equivalent
```

Output: `dist/BiteMatica-<version>.antegg`. The version is read from `packaging/egg.json`;
change it there, not in the scripts.

## Test

The whole format/mesh/world/UI stack is Android-free and builds on a host:

```bash
python3 test/make_fixtures.py test/fixtures
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Release
cmake --build build/host --target bitematica_tests bitematica_world_tests
./build/host/bitematica_tests        # 85 checks: formats, projector, toolbar, controller, config
./build/host/bitematica_world_tests  # 95 checks: placement, mesher, culling, verification, translation
```

The suites read **generated real schematic files** through the production readers. Do not
replace a fixture read with a mock; the point is that the readers accept genuine wire formats.
The world suite uses a `FakeWorld : WorldSource` for the verification cases — that is a real
implementation of the interface, not a mock of the code under test.

Without CMake available, the same targets compile directly:
`g++ -std=c++20 -I src test/host_tests.cpp src/nbt/*.cpp src/format/*.cpp
src/render/HudOverlay.cpp src/mod/ControllerNavigator.cpp src/mod/JsonConfig.cpp -lz`.

## Things that are easy to get wrong here

- **NBT list elements carry no type byte.** A `TAG_List` declares its element type once in
  its own header, and list entries are never named and never prefixed with a type byte —
  they are payload only. `NbtWriter::writeTag` writes type+name+payload; list children go
  through `writePayload`, which is payload-only. Emitting a type byte per element still
  *reads* back (the header type is authoritative) but misaligns every tag after it, which
  shows up as a truncated document, not an obvious error.
- **The `zlib`-detected flags live on the document, not on the raw parse.**
  `NbtReader::readFile` inflates first and then calls `readRaw`, so it must copy
  `compressionGzip`/`compressionZlib` onto the raw-parsed document.
- **A compound used as a list element is a body, not a full tag.** In `make_fixtures.py`,
  `compound_body(...)` is for list elements; `compound(...)` is for named children.
  Appending an extra `TAG_End` to a `compound()` output double-terminates the element.
- **Face culling means a 3×3×3 solid cube has 26 visible blocks, not 27.** The centre block
  has no exposed face and is skipped. The mesh test pins 26.
- **The toolbar and the controller navigator share one button order.** `HudOverlay`'s
  `kButtons` array and `ControllerNavigator::actionForIndex`'s `order[]` must stay in the
  same sequence (19 buttons, 6 columns); `host_tests.cpp` walks every index and compares them.
  Adding a toolbar button means updating the count assertions in both suites.
- **The per-frame render callback lives in the SDK (`pl/Render.hpp`), not the mod.** It runs on
  the render thread with the GL context current and is dispatched from the player-render detour.
  Do not add a second hook; register through `pl::render::RegisterRenderCallback`.
- **Meshing must not run on the render thread.** `BiteMaticaMod` builds meshes on a worker and
  hands finished `SectionMesh`es to the render thread through a mutex-guarded slot; the render
  callback only uploads buffers and draws.
- **The worker reads only a snapshot.** `rebuildMeshAsync` copies the schematic, placement and
  meshing config into a `MeshRequest` under `mRequestMutex`; the worker must never touch the live
  members, which input callbacks mutate. `drawWorldSpace` likewise reads a `RenderSnapshot`
  (`mRenderMutex`), refreshed by `refreshRenderSnapshot` (called from `submitHud`, `moveToPlayer`
  and load). Do not read `mSchematic`/`mPlacement`/`mConfig` directly from either thread.
- **`mMeshWorkerStop` must be cleared before starting the worker.** `disable()`/`unload()` set it
  and join; `installRenderCallback` stores `false` first. Forgetting that leaves the new thread
  exiting immediately, so nothing meshes after a re-enable.
- **The worker must always be joined.** A joinable `std::thread` destroyed while joinable calls
  `std::terminate`. `disable()`, `unload()` and the destructor all stop-and-join.
- **Culling happens in placement-local space, not world space.** `SchematicRenderer::draw` takes a
  frustum built from `viewProjection * placementLocalToWorld(origin, yaw)` and a camera transformed
  by `worldToPlacementLocal`. Section boxes are axis-aligned in local space, so a rotated placement
  is no longer wrongly culled at the screen edges, and the distance test compares like-for-like.
  `placementLocalToWorld` must match the vertex shader's rotation signs exactly (test-pinned).
- **GL state save/restore is exhaustive on purpose.** `SchematicRenderer::saveState`/`restoreState`
  cover program, VAO, both buffer bindings, `GL_ACTIVE_TEXTURE`, blend, depth test, cull, depth
  mask, depth func, cull mode, front face, both blend factor pairs, both blend equations, stencil,
  scissor and colour mask; `restoreState` runs even when no section is drawn. The shader binds no
  texture, so the 2D texture binding is deliberately not touched — only the active unit is restored.
- **Uniform locations are resolved once at init**, not per frame.
- **The render API must stay weak-linked (`src/mod/SdkCompat.hpp`).** A native mod whose `.so` has
  an undefined, *non-weak* symbol the running preloader does not export fails to load: `dlopen`
  cannot resolve it and the launcher shows "Built for a different game build (missing symbol)".
  GlowberryClient pins a preloader commit that predates `pl/Render.hpp`, so `pl::render::*` is
  declared weak in `SdkCompat.hpp` and every call goes through `bitematica::mod::SdkRender`, which
  checks the pointer. **When the render API is absent the mod draws nothing and says so on the status
  line** — it does not fall back to a screen-space overlay. Two guards: `test/link_check.sh` (the
  linked `.so` has no strong `pl::render::` reference) and `tools/verify_antegg.py` (the packaged
  `.so`). Both run in CI. Do not include `pl/Render.hpp` without the compat header, and do not add a
  direct `pl::render::` call that skips `SdkRender`.
- **The HUD is chrome only; the schematic is never drawn through draw commands.** `UiCommand::Kind`
  is `Rect`/`Text` only — there is no geometry primitive, so schematic geometry cannot be emitted via
  HUD commands even by mistake. `HudOverlay::buildCommands` takes no schematic and no camera.
  `test/hud_geometry_guard.sh` (grep guard) and `testChromeNeverDrawsSchematicGeometry`
  (`host_tests.cpp`) enforce it; both run in CI. If you are tempted to add a Triangle kind to make a
  2D preview, don't — that is the "software overlay" this project exists to avoid.
- **The minimum preloader for world-space rendering is commit `167226d`** (adds `pl/Render.hpp`);
  GlowberryClient's pin must be at or after it. See `docs/BUMP_PRELOADER.md`. The SDK's
  `viewProjection` is **world→clip** (documented in `docs/MATRIX_SPACE.md`) — do not treat it as
  camera-relative.
- **`uMvp` is the camera-relative clip matrix, and the placement transform is applied exactly once.**
  `render::computePlacementCull(projection, view, ...)` returns `mvp = projection * rotation(view)`
  and `cameraRelativeOrigin = placementOrigin − cameraPosition` (subtracted in **double** precision,
  then narrowed to float). The vertex shader applies the placement's yaw and that relative origin
  itself, so no large world offset ever enters a float and the ghost is stable at ±3e7. Passing the
  world `viewProjection * localToWorld` (or folding the placement transform into `mvp`) transforms
  every vertex twice — `testCameraRelativeMvpExcludesPlacementTransform` fails if that creeps back in.
- **Meshing and outlines run on the worker, not the render thread.** `SectionMesher::build` and
  `SectionMesher::buildOutlines` produce geometry; the render callback only uploads buffers and
  draws. Outlines are block-edge `GL_LINES` (surface blocks only), a second tiny program
  (`mLineProgram`), with a smaller polygon offset than the fill pass. Fill uses
  `glPolygonOffset(factor, units)` (negative, configurable) so ghost faces do not z-fight real ones.
- **The GL backend is gated on `gles::isGles3OrLater(GL_VERSION)`.** On a non-GLES-3 (e.g. Vulkan)
  backend `initialize()` returns false, the mod records a one-time named reason and draws nothing.
  The predicate is header-only and unit-tested in `host_tests_render.cpp`.
- **GL resources are freed on the render thread.** `disable()`/`unload()` set `mGlShutdownRequested`;
  the next render frame calls `releaseGlResources()` (buffers + programs) with the context current,
  then unregisters the callbacks. Never free GL objects off the render thread.
- **Block colours are data (`data/block_colors.json`), loaded over an embedded default.** `tools/gen_block_colors.py` generates the file (hand table by default, or measured from a vanilla resource pack with `--textures`). Unknown blocks get a deterministic hash colour and are **recorded** (`BlockColors::unknownBlocks()`), never fabricated silently. The file is packaged via `build.sh`/`build.ps1 --extra`.
- **Non-cube blocks are meshed from `core/BlockShape.hpp`, not as full cubes.** `boxesFor(state)` returns axis-aligned boxes; `isCube` is true only for a full block. The mesher's greedy full-cube path skips non-cubes (but they still count as solid for neighbour culling); they are emitted as boxes afterward. `neverRenders` (air, structure void, light, barrier-unless-toggled) blocks are neither drawn nor solid.
- **The verifier has six verdicts, not four.** `Correct, Missing, WrongBlock, WrongState, Extra, Unknown`. `Unknown` (an unreadable/unloaded block) is never counted as `Missing` or as `Correct`, and is excluded from `percentComplete`'s denominator. `LiveMaterials` counts `WrongBlock`/`WrongState` as outstanding and ignores `Unknown`. `MismatchPalette` (default/colour-blind/high-contrast presets) colours them.
- **The camera comes from `LocalPlayerFeed`, not a matrix scan.** It hooks `ClientInstance`'s
  local-player slot 31 **by RTTI name** and reads `Actor+0x230` (position) / `+0x238` (yaw,pitch),
  the same offsets the launcher's own feed uses; they are shared by 1.26.33 / 1.26.50 / 1.26.51.
  `buildLocalCamera` turns that into a view+projection. The old `GameCamera` module-scan is kept for
  the SDK-matrix path but is no longer the camera source. Do not reintroduce a scan as the primary
  path: it is slow and speculative, and the accessor hook is proven.
- **The EGL hook install must be retried, not latched.** The mod enables before the game has
  necessarily mapped a GLES library, so `EglFrameSource::install()` returns false early and
  `retryRenderSeam()` is called from `enable` and from every touch/key event. A failed install is
  deliberately not latched.
- **Failures are on screen.** `refreshStatusLine` prints the render seam, `LocalPlayerFeed::diagnostic()`
  and the drawn/no-camera frame counters. When something does not appear, read that line first.
- **The SDK's camera matrix is only trusted after `looksWorldSpace`.** The SDK resolves it by a
  probe, so its coordinate space is not guaranteed; `looksWorldSpace` rejects a camera-relative
  matrix and the caller then uses the first-person fallback.
- **Verification is off until a build's `BlockSource::getBlock` slot is set** (`verifyBlockSourceSlot`,
  default `-1`). There is no runtime discovery, and a wrong slot faults the game thread. To enable
  it on a new build: confirm the slot the way the SDK's own hooks do, set it in the config, and
  watch the `BiteMatica: world source ...` log line.
- **The menu is drawn through HUD commands and opened by a tap, not a key.** `MenuRenderer::layout`
  returns draw commands **and** hitboxes from one pass; the touch handler walks exactly those
  hitboxes, so a drawn row and a tappable row cannot drift. A floating icon (`layoutIcon`) is always
  on screen so the menu is reachable without a keyboard or the launcher's Mod Menu. `handleTouch`
  checks the menu first, so its taps are never swallowed by the toolbar underneath. Tests pin the
  icon hitbox, that every hitbox is inside the panel, a minimum touch size, and that no two hitboxes
  overlap.
- **Menu pages are rebuilt on every `submitHud`** (`rebuildMenuPages`), so the menu always reflects
  the live config and placements. A widget id that is not handled by `applyMenuConfig`/
  `applyMenuAction` silently does nothing — add the case when adding a widget.
- **The package must contain its `egg.json` entry point.** `pack_antegg.py` refuses to run
  when `egg.json`'s `entry_point` and `--entry` disagree; the loader would reject it later.

## Packaging rules (`.AntEgg`)

`.AntEgg` is a ZIP with the `.antegg` extension. The layout this repo produces:

```text
egg.json                       manifest at the archive root (required)
bin/libbitematica.so           the arm64-v8a native mod (entry_point)
config/config.json             defaults
config/config.schema.json      launcher config-editor schema
schematics/demo_house.litematic
```

`egg.json` requires `name`, `version` (semver), `author`, `type` (`native`|`script`),
`entry_point`. For `type: "native"` the entry must end in `.so`. The loader extracts to
`getExternalFilesDir()/AntEggs/<slug>/`, so the mod's runtime root (and therefore its
`schematics/` folder) is `<that>/bitematica/`.

**A native `.AntEgg` needs two manifests.** `egg.json` is only what `.AntEgg` validation
reads. The preloader's own `ModManifest.cpp` separately requires `manifest.json` at the
extracted mod root and refuses to load a library unless it is present, has
`type: "preload-native"`, and its `entry` resolves to an existing `.so` inside the mod
directory. Ship both, and keep their entry paths identical — `pack_antegg.py` enforces
this. Without `manifest.json` the package validates and extracts, then fails at
injection with `Failed to parse mod manifest`. The `icon` path in `manifest.json` must
also resolve inside the package (shipped as `icon.png`).

## Attribution constraints

Keep the README's attribution: the concept is Litematica's (maruohon) and malilib's; this
codebase is an independent Bedrock/Android implementation. Never imply Litematica code was
reused or that Java Edition mods run here.
