# Architecture

BiteMatica is a client-side Bedrock/Android mod. It is split so that everything except the game
glue is pure C++ that builds and is tested on a host, with no Android, no GPU and no preloader.

## Data flow

```
 schematic file (.litematic / .schem / .schematic / .mcstructure / .nbt)
        |
        v
 format/  readers  ──  NBT decode (gzip/zlib/raw)  ──>  core::Schematic
        |                                              (palette + bit-packed blocks)
        v
 core/  Schematic, BlockPalette, BlockShape, MaterialList, LiveMaterials
        |
        +--> translate/  BlockTranslation (Java -> Bedrock id/state)
        |                BlockStateTransform (rotate/mirror a state)
        |
        +--> mesh/  SectionMesher   (16^3 sections, exposed-face cull, greedy merge,
        |                            non-cube shapes, mirror baked in, block-edge outlines)
        |                |
        |                v  MeshRequest (immutable shared_ptr<const Schematic> + config)
        |         [ worker thread ] ── PendingMesh (mutex-guarded slot) ──>
        |                |
        |                v  [ render thread ]
        +--> gles/  SchematicRenderer   (camera-relative mvp, polygon offset, two programs,
        |                                exhaustive GL state save/restore)
        |
        +--> world/  Placement, Verifier, IncrementalVerifier, Selection, WorldSource
        |                |
        |                v  BlockSource::getBlock (confirmed reader, never guessed)
        |         [ game thread ]
        |
        +--> litematica/  ToolMode, BlockTargeting (DDA raycast), EasyPlace, PasteJob,
        |                 PlacementManager (+ undo), PlacementStore, BlockInfo, PlacementEdit
        |
        v
 ui/  MenuModel, MenuBuilder, MenuRenderer, Strings   (menu pages, host-tested layout)
 mod/  Config, JsonConfig, SchematicLibrary, ControllerNavigator, LocalPlayerFeed,
       GameWorldSource, SdkCompat, BiteMaticaMod (glue)
```

## Threading

| Thread | Owns | Never touches |
| --- | --- | --- |
| UI / input | `mConfig`, `mPlacement`, `mSelection`, `mPlacements`, menu state, library | GL, `mSchematicPtr`'s contents beyond `schematic()` |
| Mesh worker | the `MeshRequest` copy (immutable schematic + config) | any live member, GL |
| Render (SDK/EGL callback) | GL resources, `RenderSnapshot`, `PendingMesh` drain, frame counters | live config/placement (reads the snapshot) |

The two rules that keep this correct:

1. **The schematic is immutable and only ever replaced, never mutated.** `shared_ptr<const
   Schematic>` is published once; the worker and the render thread read the pointee safely without a
   copy.
2. **No GL call ever runs outside the render callback.** Uploads, draws and resource release all
   happen on the render thread with the context current.

Hand-off is by two mutex-guarded slots: `MeshRequest` (UI -> worker) and `PendingMesh` (worker ->
render). The render thread also reads a `RenderSnapshot` (UI -> render) so input can mutate state
mid-frame without a race.

## Two drawing paths, deliberately

- **World-space geometry** goes through GL (`gles/SchematicRenderer`), is depth-tested, and is
  occluded by terrain. It is camera-relative so it stays exact at world coordinate ±3e7.
- **UI chrome** (toolbar, menu, material panel, status line) goes through HUD draw commands and is
  always on top. The HUD command vocabulary has no geometry primitive, so schematic geometry cannot
  be drawn through it even by mistake (`test/hud_geometry_guard.sh`).

## Fail-closed surfaces

Each of these reports a named reason and draws nothing rather than guessing:

- **No render seam** (old preloader, no EGL hook): `World-space renderer unavailable: no render seam`.
- **Non-GLES-3 backend**: `world-space renderer unavailable: <GL_VERSION>`.
- **Unverified block getter**: `BlockSource::getBlock` slot/reader/version gate/probe.
- **Unreadable block during verification**: `Unknown`, never `Missing`.
- **Over-cap schematic**: refused with the block count and the cap.
- **Unparseable file / config / placements**: a specific reason, never a partial read.

## Tests

Four host suites (`test/host_tests*.cpp`) plus a benchmark (`test/bench.cpp`):

- `bitematica_tests` — NBT, format readers, chrome layout, config, migration.
- `bitematica_world_tests` — placement transforms, mesher, verification, translation, writers,
  world-source gate/probe, incremental scan, materials.
- `bitematica_litematica_tests` — tool modes, layers, raycast, Easy Place, paste, placement store,
  state transform, block info, menus.
- `bitematica_render_tests` — camera-relative precision, transform-once, culling, mirror, backend
  gate, outlines, shapes, colours, mismatch modes, verdicts, render plan.

CI also runs `test/link_check.sh` (weak render symbols) and `test/hud_geometry_guard.sh`.
