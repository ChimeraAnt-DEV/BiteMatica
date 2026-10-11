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
                GameWorldSource, BiteMaticaMod (preloader glue)
test/           host test suites + fixture generator
tools/          pack_antegg.py, generate_demo.py, generate_icon.py
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
./build/host/bitematica_world_tests  # 64 checks: placement, mesher, verification, translation
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
- **GL state save/restore is exhaustive on purpose.** `SchematicRenderer::saveState`/`restoreState`
  cover program, VAO, both buffer bindings, the texture binding, blend, depth test, cull, depth
  mask, depth func, cull mode, front face and blend factors, and `restoreState` runs even when no
  section is drawn. A stale binding left behind corrupts the game's next draw.
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
