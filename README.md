<p align="center">
  <img src="assets/icon/bitematica-256.png" width="160" height="160" alt="BiteMatica">
</p>

<h1 align="center">BiteMatica</h1>

<p align="center"><strong>Client-side schematics for Minecraft: Bedrock Edition on Android — as a GlowberryClient <code>.AntEgg</code> mod.</strong></p>

<p align="center">
  <a href="https://github.com/ChimeraAnt-DEV/BiteMatica/actions/workflows/build.yml"><img src="https://img.shields.io/github/actions/workflow/status/ChimeraAnt-DEV/BiteMatica/build.yml?style=flat-square&label=build%20.AntEgg" alt="Build"></a>
  <a href="https://github.com/ChimeraAnt-DEV/BiteMatica/releases"><img src="https://img.shields.io/github/v/release/ChimeraAnt-DEV/BiteMatica?style=flat-square&color=7CF0C0" alt="Release"></a>
  <a href="https://github.com/ChimeraAnt-DEV/BiteMatica/releases"><img src="https://img.shields.io/github/downloads/ChimeraAnt-DEV/BiteMatica/total?style=flat-square&color=4AE0A0" alt="Downloads"></a>
  <a href="https://github.com/ChimeraAnt-DEV/BiteMatica/blob/main/LICENSE"><img src="https://img.shields.io/github/license/ChimeraAnt-DEV/BiteMatica?style=flat-square" alt="License"></a>
  <a href="https://github.com/ChimeraAnt-DEV/BiteMatica/issues"><img src="https://img.shields.io/github/issues/ChimeraAnt-DEV/BiteMatica?style=flat-square&color=red" alt="Issues"></a>
  <a href="https://github.com/ChimeraAnt-DEV/BiteMatica/stargazers"><img src="https://img.shields.io/github/stars/ChimeraAnt-DEV/BiteMatica?style=flat-square&color=yellow" alt="Stars"></a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/platform-Android%209%2B-3DDC84?style=flat-square&logo=android&logoColor=white" alt="Android 9+">
  <img src="https://img.shields.io/badge/ABI-arm64--v8a-blue?style=flat-square" alt="arm64-v8a">
  <img src="https://img.shields.io/badge/Minecraft-Bedrock-62B47A?style=flat-square" alt="Bedrock">
  <img src="https://img.shields.io/badge/format-.AntEgg-7CF0C0?style=flat-square" alt=".AntEgg">
  <img src="https://img.shields.io/badge/input-touch%20%2B%20controller-9b7cf0?style=flat-square" alt="Touch and controller">
</p>

---

## What this is

BiteMatica brings the *Litematica* experience to **Minecraft: Bedrock Edition on Android**, packaged as a native **`.AntEgg`** mod for [GlowberryClient](https://github.com/ChimeraAnt-DEV/GlowberryClient).

Load a schematic, see it drawn **in the world** where you want it built, read off exactly how many of which blocks it needs, **verify it against the world as you build**, and export a Bedrock-native structure file when you are done. It is a **client-side viewer and helper** — it never touches a server, never places blocks on its own, and makes no attempt to hide what it is.

If you know Litematica on Java Edition, the mental model is the same: a schematic is a box of blocks, the overlay shows you where each block belongs, and the material list tells you what to gather. BiteMatica adds the part that matters most — telling you what is already placed and what is still missing.

### Why "BiteMatica"?

It is a *bite-sized Litematica for Bedrock* — rebuilt from the ground up for a touchscreen and a gamepad, not ported from a keyboard-and-mouse Java mod.

---

## Features

### Reads every schematic format that matters
| Format | Extension | Where it comes from |
| --- | --- | --- |
| **Litematica** | `.litematic` | Java Edition Litematica / malilib |
| **Sponge / WorldEdit** | `.schem` | WorldEdit, BuildTools, most servers |
| **MCEdit legacy** | `.schematic` | older tools and many Bedrock packs |
| **Bedrock structure** | `.nbt` / `.mcstructure` | the game's own structure blocks |

Detection is by **content first**, not the file extension: BiteMatica decompresses the file, reads its NBT, and inspects the root compound to decide what it is holding. A `.litematic` renamed to `.schem` still opens. A file that is not a schematic fails with a specific reason ("schem is missing a Palette compound"), never with a vague "unsupported file".

### Works on every 1.26 build

World-space rendering on Bedrock needs a per-frame point on the render thread with the GL context current. The launcher's own render callback is tied to specific game builds, so BiteMatica does not depend on it: it hooks **`eglSwapBuffers`**, which every GLES build calls exactly once per frame and whose signature the EGL standard fixes. That makes the render seam **version-independent**, so world-space geometry works on **1.26 through 1.26.52** and beyond.

The camera is read from the game's own view and projection matrices (found once, then read cheaply each frame), so no per-build player accessor is needed either.

If a launcher *does* export the SDK render callback, BiteMatica uses it instead; if neither is available it falls back to a software overlay and says so in the status line. Nothing is silently broken, and the mod always loads.

### World-space rendering
- The schematic is drawn as **real 3D geometry in the world**, not a screen-space projection — so it moves with the world as you walk, and it sits at a real position you can build toward.
- **Depth-tested**: ghost blocks are hidden behind real terrain. A wall you have already built occludes the ghost of the wall behind it, which is the whole point of an overlay you can build against.
- **A GLES3 renderer inside the mod** with strict save/restore of every piece of GL state it touches (program, VAO, buffers, blend, depth, cull, texture bindings). Corrupting the game's renderer is the worst possible bug, so the restore is exhaustive and runs even when a draw is skipped.
- **A proper mesher**: the schematic is split into **16³ sections**, each block contributes only its **exposed faces** (a face whose neighbour is solid is never emitted), and coplanar faces of the same colour are **greedily merged** — a flat wall becomes a handful of quads instead of hundreds. Meshes are uploaded **once** as VBOs and drawn with **frustum and distance culling**.
- **Meshing runs off the render thread** on a worker; the render callback only uploads finished buffers and draws, so the render thread never meshes.

### Render modes
- **Translucent ghost blocks** (the default), **outlines only**, and **mismatch highlighting** (see verification below).
- **Adjustable opacity** (10–100%) so the build never fights the world behind it.
- A **triangle budget** (default 1.5M, up to 4M) and a **distance cull** (default 256 blocks) so a mega-build cannot stall your frame rate; when the budget bites, the status line says so instead of silently dropping geometry.

### The Litematica toolset

BiteMatica implements Litematica's tool model, adapted for touch and controller:

**Nine tool modes** (cycle with the `T` button, or **L1/R1** on a controller):

| Mode | What it does |
| --- | --- |
| **Area Selection** | Set corner 1 and corner 2 at the crosshair; the selection is the region to save or fill |
| **Schematic Placement** | Move the placement origin to the block you are looking at |
| **Fill / Replace** | Fill the selection with the held block (creative) |
| **Delete** | Clear the selection (creative) |
| **Move** | Move a region from A to B |
| **Paste** | Generate the commands that paste the schematic into the world |
| **Grid** | Repeat the selection on a grid |
| **Rebuild / Verify** | Scan the world against the schematic and highlight mismatches |
| **Pick Block** | Read the schematic block under the crosshair |

**A full on-screen menu, opened by tapping an icon.** There is a floating menu button on screen at all times — tap it and the menu opens; tap the **X** or anywhere outside and it closes. No keyboard, no controller, no trip to the launcher's Mod Menu. Every row is a touch target you can tap: toggles flip, choices cycle, sliders step, buttons fire. Tabs are tappable across the top.

The pages match Litematica's layout: **Main**, **Config** (generic, visuals, colors, performance, hotkeys, info switches), **Materials**, **Placements** (rotation, mirroring, sub-region toggles, locking), **Render Layers**, **Area Editor**, **Loaded Schematics**, and **Tasks**.

**Easy Place** — look at a ghost block and the mod works out which block to place and which way it should face, so stairs and the like orient correctly. It reports the decision; it does not place anything without your command.

**Block targeting** — a voxel ray-cast from the camera finds the block under the crosshair, which is what every tool mode acts on.

**Paste to world** — generates `/setblock` and `/fill` commands (merging runs of the same block) and writes them to `exports/paste_commands.txt`. That is the reliable placement route: the game does the placing, and nothing touches the network.

### Layer modes
Show **all layers**, a **single layer**, **all below**, **all above**, or a **range**, with step up/down — the standard way to build a floor-by-floor structure without the upper floors hiding the one you are working on.

### Verify against the world
The feature that makes a schematic tool actually useful: **compare the schematic to the live world** and colour every block by its state.
- **Correct** (green), **Missing** (red, needs placing), **Wrong** (amber, wrong block), **Extra** (blue, should be removed).
- The comparison is strict about block type but tolerant about states, because Bedrock's state names for the same block differ from Java's.
- It is **fail-closed**: the mod resolves the game's block getter by RTTI vtable name (the same approach the SDK's own hooks use), and if the slot is not configured for your build, verification reports "the world is not readable" rather than inventing block states and colouring everything wrong.
- **Per-build slot required.** There is no reliable way to discover the getter's vtable slot at runtime, and a wrong slot would fault the game thread, so it ships **off** (`verifyBlockSourceSlot: -1`). Once you have confirmed the slot for your game build — the same exercise the SDK's own hooks go through to add a new version — set it in the config and verification turns on. `AGENTS.md` records the procedure.

### World-anchored placements
The schematic has a **world origin**, not a screen position. You can **move it to the player**, **rotate it in 90° steps**, **mirror it**, and **offset it vertically** — the toolbar has a button for each.

### Live material list
When verification has run, the material panel shows **required / placed / missing** for every block family and updates as you build. It exports as **CSV** for planning off-device.

### Java → Bedrock block translation
Java Edition schematics do not load correctly on Bedrock without translation: `oak_stairs[facing=north,half=bottom]` is not a Bedrock block state. BiteMatica carries a **translation table** for the families where the state actually differs — stairs, doors, trapdoors, fences, walls, logs, leaves, redstone, waterlogging — including the counter-intuitive ones (Java's `facing` maps to Bedrock's `weirdo_direction` with *inverted* meaning; Java's `half=top` becomes `upside_down_bit=1`). Blocks whose id is identical on both editions pass through unchanged.

### Selection tool and `.litematic` writer
Capture a region out of the live world and **save it as a `.litematic`** — the same format Java Edition's Litematica reads — so a build made on Bedrock can be shared with Java players.

### Cheat-route placement
Exporting a **`.mcstructure`** and running `/structure load` is the reliable way to place a build, and it is exactly what the game itself supports. The toolbar's export button prints the ready-to-run command for your current placement, rotation included. Nothing about this mod touches the network or places blocks on its own.

### Material list
The material panel counts how many of each block the schematic needs, sorted by quantity and grouped by **base block id**. You care that you need 340 stone, not that 120 of them are `axis=y`. It shows the top 16 and a "+N more" line.

### Export to Bedrock
Press **S** (the *Export* button) and the loaded schematic is written to the mod's `data/exports/` folder as a **`.mcstructure`** — a file the game's own structure block, or any Bedrock tool, can read. This is the round-trip half of the `.nbt` reader, and it is covered by a test that exports and re-imports a build and checks the block counts match.

### Built for touch and for a controller
- **Touch**: a six-by-two toolbar of 40 px buttons with generous hit boxes, each tap consumed so it never falls through to the world. Every button has a one-letter glyph and a live status line under the bar.
- **Controller**: a full focus-based navigation model. The d-pad moves focus across the same grid the touch buttons use, **A** activates the focused action, **B** releases focus, and the shoulder buttons are a direct shortcut to rotate left/right. The focus hint is drawn on screen so you always know what A will do.
- Touch and controller share **one** definition of the toolbar layout, so a button a controller can focus is always a button a finger can tap.

### The toolbar

| Glyph | Action | What it does |
| --- | --- | --- |
| `<` `>` | Previous / next file | Cycles the schematics in the mod's `schematics/` folder |
| `M` | Render mode | Overlay → Skeleton → Hidden |
| `L` | Material list | Shows or hides the block-count panel |
| `Q` `E` | Rotate left / right | Turns the view 15° (also **L1** / **R1**) |
| `+` `-` | Zoom in / out | Scales the projection |
| `^` `v` | Nudge up / down | Lifts or drops the build |
| `G` | Ghost placement | Outlines where the build would land |
| `S` | Export | Writes a `.mcstructure` and prints the `/structure load` command |
| `R` | Reload | Re-scans the schematics folder |
| `V` | Verify | Compares the schematic to the live world and colours it |
| `Y` | Layers | All → single → range |
| `P` | Move to player | Re-anchors the placement at your position |
| `R` | Rotate placement | Turns the placement 90° |
| `X` | Mirror | Mirrors the placement |
| `C` | Export CSV | Writes the live material list as CSV |
| `W` | Save `.litematic` | Captures the region from the world and saves it |
| `T` | Tool mode | Cycles the nine tool modes (also **L1/R1**) |
| `A` | Use tool | Acts with the current tool mode |
| `N` | Menu | Opens the menu (the floating icon does the same) |
| `Tab` | Menu tab | Advances the menu page |

### Mod Menu integration
BiteMatica registers a native Mod Menu module with a master toggle and live config: **Render Mode**, **Opacity**, **Layers**, **Material List**, **Ghost Placement**, and **Verify World**. Turning the module off clears the overlay instead of leaving stale geometry on screen.

---

## Install

BiteMatica is a **GlowberryClient `.AntEgg` mod**. It needs [GlowberryClient](https://github.com/ChimeraAnt-DEV/GlowberryClient) to run.

1. Download the latest `BiteMatica-<version>.antegg` from the [Releases page](https://github.com/ChimeraAnt-DEV/BiteMatica/releases).
2. Open GlowberryClient and import the `.antegg` (Mods tab → import, or open the file with the launcher).
3. Enable **BiteMatica** in the Mod Menu.
4. Put your schematics in the mod's `schematics/` folder (created on first load; the path is the mod's own directory under `AntEggs/bitematica/schematics/`). A demo hut ships in the package so you see something immediately.
5. Press **R** in the toolbar after adding files.

> **Package layout** — an `.AntEgg` is a ZIP with the `.antegg` extension. A native mod needs **both** manifests: `egg.json` is what `.AntEgg` validation reads, and `manifest.json` is what the preloader reads at the extracted mod root — it refuses to load a library unless that manifest is present, is `type: "preload-native"`, and names an entry that exists inside the package.
> ```
> BiteMatica-1.0.1.antegg
> ├── egg.json                     .AntEgg manifest (name/version/author/type/entry_point)
> ├── manifest.json                preloader manifest (type: preload-native, entry, icon)
> ├── bin/libbitematica.so         the arm64-v8a native mod
> ├── icon.png                     mod icon shown by the launcher
> ├── config/config.json           default settings
> ├── config/config.schema.json    schema for the launcher's config editor
> └── schematics/demo_house.litematic
> ```

---

## Requirements

- **GlowberryClient** installed and working.
- **Android 9.0 (API 28)** or newer, **arm64-v8a**. (This mod is arm64-only, matching the launcher's own build.)
- Minecraft: Bedrock Edition, any version GlowberryClient supports.

---

## Build from source

The native mod is C++20, built with the Android NDK, and links the [preloader-android](https://github.com/ChimeraAnt-DEV/preloader-android) SDK (the same SDK GlowberryClient's other native mods use).

### Prerequisites
- An Android NDK (r25+; r28c is what CI uses).
- CMake 3.22+ and Ninja.
- Python 3 (for fixture generation, packaging, and the icon).
- Git.

### One command
```bash
./build.sh --test          # run the host tests, then build and package
```
On Windows:
```powershell
.\build.ps1 -Test
```

Output:
```
dist/BiteMatica-1.0.0.antegg
```

`build.sh` discovers the NDK from `ANDROID_NDK_HOME` / `ANDROID_NDK_ROOT` / `ANDROID_HOME`, or takes `--ndk <path>`. If the preloader SDK is not already under `third_party/preloader-android`, it clones the SDK's `main` branch automatically, so the build has no hidden machine-local dependency. Set `PRELOADER_REF` to pin a branch or commit.

### The host test suites
Everything that can be wrong without a GPU is pure C++ with **no Android dependency**, so it is built and run on the host:

```bash
python3 test/make_fixtures.py test/fixtures
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Release
cmake --build build/host --target bitematica_tests bitematica_world_tests
./build/host/bitematica_tests        # formats, mesh projector, toolbar, controller, config
./build/host/bitematica_world_tests  # placement transforms, mesher, verification, translation
```

**238 checks across the three suites.** They read **real, generated schematic files** — gzip NBT, packed long arrays, multi-region files with per-region palettes, and negative-size regions — through the production readers, not mocks. `test/make_fixtures.py` writes them, and its encoders are shared with the demo-schematic generator, so there is one definition of each wire format.

The world suite covers the parts most likely to be subtly wrong and hardest to debug on a device: the placement transform (every rotation × mirror round-trips `toWorld`/`toLocal`), the mesher (a solid 3×3×3 collapses to a handful of merged quads, and interior faces are culled), placement-local frustum culling (the local→world transform is pinned against the shader's own rotation signs), verification (missing/wrong/extra counted against a fake world), the Java→Bedrock direction inversion, and the `.litematic` write→read round trip.

---

## Configuration

Settings live in `config/config.json` inside the installed mod. The launcher's config editor reads `config/config.schema.json`, and the in-game Mod Menu can change the common ones live.

```json
{
  "version": 1,
  "enabled": true,
  "renderMode": 0,
  "opacity": 60,
  "showMaterialList": true,
  "showToolbar": true,
  "ghostPlacement": false,
  "onlyExposed": true,
  "maxRenderBlocks": 60000,
  "maxTriangles": 1500000,
  "renderDistance": 256,
  "layerMode": 0,
  "verifyAgainstWorld": false,
  "verifyExtras": false
}
```

| Key | Meaning |
| --- | --- |
| `renderMode` | `0` overlay · `1` skeleton · `2` hidden |
| `opacity` | overlay alpha in percent (10–100) |
| `onlyExposed` | skip fully enclosed blocks when meshing |
| `maxRenderBlocks` | per-frame meshing cap (500–250000) |
| `maxTriangles` | mesher triangle budget (1000–4000000) |
| `renderDistance` | distance cull in blocks (0 disables) |
| `layerMode` | `0` all · `1` single · `2` range |
| `verifyAgainstWorld` | colour blocks by world match |
| `verifyExtras` | also flag world blocks where air is wanted |
| `toolbarX`/`toolbarY` | toolbar anchor in HUD pixels |
| `materialX`/`materialY` | material-panel anchor in HUD pixels |

A hand-edited value that is out of range is **clamped** on load, so a typo cannot take the overlay off screen.

---

## How it works

```
bitematica_core   (host-testable, no Android)
  nbt/        NbtReader, NbtWriter        gzip / zlib / raw NBT
  format/     Litematica, Sponge, MCEdit, Bedrock structure readers + writers
  core/       Schematic, BlockPalette, MaterialList, LiveMaterials
  mesh/       SectionMesher   16^3 sections, exposed-face culling, greedy merge
  world/      Placement, Verifier, WorldSource, Selection, CheatPlacement
  translate/  BlockTranslation   Java -> Bedrock block states
  render/     MathTypes (matrices, frustum), HudOverlay (2D toolbar)
  mod/        Config, JsonConfig, SchematicLibrary, ControllerNavigator
        |                                       |
        |  world-space draw                     |  UiCommand -> HUD commands
        v                                       v
  gles/       Gles, SchematicRenderer      mod/BiteMaticaMod.cpp (preloader glue)
        |                                       |
        |  GLES3 draw with saved/restored state |
        v                                       v
  pl/Render.hpp render callback          pl/modmenu draw commands
```

A schematic is normalized into one shape — a palette plus one palette index per block — so the mesher, the verifier and the material list never know or care which dialect it came from.

### Honest engineering notes
- **Two drawing paths, deliberately.** World-space geometry is GL (depth-tested, occluded by terrain); the toolbar and material panel are HUD commands (always on top). A toolbar that terrain could occlude would not be a toolbar.
- **The per-frame render callback is a new public SDK API.** Mods had no way to run code on the render thread with the GL context, so it was added to the preloader (`pl/Render.hpp`) rather than worked around. The mod does not invent a hook it cannot guarantee.
- **Fail-closed everywhere.** A missing file, a malformed manifest, a truncated archive, an unreadable config, an unresolved camera, an unverified block getter: each becomes a named, user-visible reason in the status line, never a crash and never a guessed value.
- **The demo that ships is real.** The bundled hut is written by the same encoders the tests use, and it is loaded back through the production reader before packaging.

---

## Attribution

BiteMatica is inspired by **[Litematica](https://github.com/maruohon/litematica)** by **maruohon** and its **[malilib](https://github.com/maruohon/malilib)** library, both client-side mods for Minecraft **Java Edition**. This project is a **from-scratch reimplementation for Bedrock Edition on Android**: it uses no Litematica or malilib code, and it does not run Java Edition mods. The concept — load a schematic, overlay it, count the materials — is theirs; the Bedrock/Android implementation is this repository's.

BiteMatica is an independent third-party mod and is not affiliated with Mojang, Microsoft, Litematica, or GlowberryClient's authors beyond depending on the launcher's mod format.

---

## License

Licensed under the **Apache License 2.0** — see [LICENSE](LICENSE).