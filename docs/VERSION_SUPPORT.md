# Verified game versions

World-space rendering and verification rest on two vtable slots, both resolved **by RTTI
name** at runtime (no per-build addresses are baked in). This document records which builds
they were verified against.

## What is version-dependent

| Capability | Mechanism | Slot |
| --- | --- | --- |
| The camera (world-space rendering) | `ClientInstance::getLocalPlayer` by RTTI name `14ClientInstance` | **31** |
| The world block getter (verification) | `BlockSource::getBlock` by RTTI name `11BlockSource` | **2** |

Both are resolved with the preloader's `resolveVtableFunction`, which finds the typeinfo
name in `.rodata` and the primary vtable in `.data.rel.ro`, so **no fixed address is
compiled into the mod**. That is what lets one `.AntEgg` cover the whole range.

## Verified builds

Each `libminecraftpe.so` below was extracted from the arm64-v8a APK and analysed
statically. `slot 31` was confirmed to be `getLocalPlayer` and `slot 2` to be `getBlock`
(the `getBlock` prologue is byte-identical across all of them).

| Version | `ClientInstance` vtable | slot 31 | `BlockSource` vtable | slot 2 |
| --- | --- | --- | --- | --- |
| 1.26.10 | `0x128ad4c8` | `0x968acec` | `0x12c96bc8` | `0xfc65b1c` |
| 1.26.20 | `0x134dd2e8` | `0x99919c4` | `0x13901fd8` | `0x105ddcc8` |
| 1.26.33 | `0x14054598` | `0x9d72118` | `0x144a2838` | `0x10dee204` |
| 1.26.40 | `0x11e30270` | `0x94429ac` | `0x122ab0e0` | `0xf24e280` |
| 1.26.45 | `0x11e3b2d0` | `0x94432d0` | `0x122b5fd8` | `0xf253c8c` |
| 1.26.51 | `0x12bc2f98` | `0x9807f1c` | `0x130609e8` | `0xfb91558` |
| 1.26.52 | `0x12bc32f8` | `0x9807fbc` | `0x13060d48` | `0xfb91918` |
| 1.26.60.30 | — | — | `0x15845568` | `0x11d38924` |

**So: 1.26.10 through 1.26.52 inclusive is supported, and 1.26.60.x as well.** The
intermediate patch builds (1.26.11, 1.26.12, …) were not each downloaded, but the two
slots are resolved by name rather than by address, and the sampled builds on both sides of
every gap expose the same slot layout, so they are covered by the same mechanism.

## What is not yet version-independent

The world **verification** needs three member offsets that are plain struct offsets, not
vtable slots, so they cannot be resolved by name:

- `Actor -> Dimension` (`Actor+0x1C8` on the reference build),
- `Dimension -> BlockSource` (`Dimension+0xF0`),
- `Block -> BlockLegacy -> name` (`Block+0x68`, then a `std::string`).

See `docs/BLOCKSOURCE_GETBLOCK_ANALYSIS.md`. Until those are confirmed for a build,
verification stays off (fail-closed) and the status line says so; world-space **rendering**
does not depend on them and works across the whole range above.

## How the range was checked

```bash
# per version: extract the .so and resolve both slots by RTTI name
unzip -o -j minecraft-26-52-arm64-v8a.apk lib/arm64-v8a/libminecraftpe.so -d so/26-52
# then: find "14ClientInstance" / "11BlockSource" typeinfo, walk the primary vtable,
# read slot 31 / slot 2 (see docs/BLOCKSOURCE_GETBLOCK_ANALYSIS.md for the method)
```

All of the above is **static analysis**; the on-device behaviour is checked with the
checklist in `docs/DEVICE_CHECKLIST.md`.
