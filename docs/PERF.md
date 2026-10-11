# Performance baseline

Measured on the host (`./build/host/bitematica_bench`), Release build. These are the numbers to
compare against when a change touches the mesher or the verifier. On a device the absolute numbers
are higher; the ratios are what matter.

| Build | Blocks | Mesher | Verifier |
| --- | --- | --- | --- |
| 16³ | 4,096 | 5.6 ms | 0.08 ms |
| 32³ | 32,768 | 150 ms | 0.65 ms |
| 48³ | 110,592 | 1,078 ms | 2.25 ms |

## What the numbers mean

- **The verifier is cheap** (linear in blocks). It runs on the UI thread today; the
  `IncrementalVerifier` budgets it per frame so a large schematic does not stall.
- **The mesher is super-linear** because the greedy merge scans a growing rectangle for each face.
  It runs **only on the worker thread**, never the render thread, so a slow mesh costs a delay, not
  a hitch. For very large schematics the triangle budget (`maxTriangles`) caps the work and the
  status line reports truncation.

## Budgets that keep the frame safe

- `uploadSectionsPerFrame` (default 2): GPU uploads are spread over frames; the live geometry is
  swapped in only when a generation completes, so no frame is left empty.
- `maxTriangles`: the mesher stops and reports truncation past this.
- `renderDistance`: a distance cull in placement-local space.
- `verifyBlocksPerFrame` (default 2000): the incremental verifier's per-call budget.
- `maxSchematicBlocks` (default 32,000,000): a schematic above this is refused with a named error
  rather than risking an OOM crash.

## How to reproduce

```bash
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Release
cmake --build build/host --target bitematica_bench
./build/host/bitematica_bench
```
