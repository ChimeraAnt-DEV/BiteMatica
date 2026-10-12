# `BlockSource::getBlock` — static analysis (Android arm64)

Static analysis only. **Nothing here was run on a device.** Every claim carries the
address/disassembly that supports it.

Binaries analysed (arm64-v8a `libminecraftpe.so`, extracted from the APK's
`lib/arm64-v8a/`):

| Game version | APK file | `libminecraftpe.so` | `.text` | `.data.rel.ro` |
| --- | --- | --- | --- | --- |
| 1.26.10 | `minecraft-26-10.apk` | 323,520,800 B | `0x64c64e0` | `0x129c8ac0` |
| 1.26.52 | `minecraft-26-52-arm64-v8a.apk` | 328,419,680 B | `0x64c64e0` | `0x129c8ac0` |
| 1.26.60.30 | `minecraft-26-60-30.apk` | — | — | — |

All three are stripped (`readelf -S` shows no `.symtab`/`.debug*`), ELF64, `ET_DYN`,
AArch64.

## 1. `getBlock` is virtual, in slot 2 of `BlockSource`'s primary vtable

The typeinfo name string is **`11BlockSource`** (length 11), not `12BlockSource` as
`docs/FINDING_GETBLOCK.md` assumed. Resolution replicates the preloader's
`resolveVtableFunction` (`pl/memory/Vtable.cpp`): the name lives in `.rodata`; the
typeinfo object is the word in `.data.rel.ro` that points at it (minus 8); the primary
vtable is the word whose addend is the typeinfo with `offset-to-top == 0`. In a PIE the
typeinfo pointer and vtable entries are `R_AARCH64_RELATIVE` relocations, so the on-disk
words are zero and the addend must be read from `.rela.dyn`.

| Version | `BlockSource` primary vtable | slot 2 (`getBlock`) | slot 12 (`getMaterial`) |
| --- | --- | --- | --- |
| 1.26.10 | `0x12c96bc8` | `0xfc65b1c` | `0xfc67c5c` |
| 1.26.52 | `0x13060d48` | `0xfb91918` | `0xfb93464` |
| 1.26.60.30 | `0x15845568` | `0x11d38924` | `0x11d3ab78` |

**Independent corroboration** (two public projects, a different game build):

- `kimaaaa1/hurtsnail-launcher` → `nativeapi/blocksource.ts`:
  `CANONICAL_SLOTS = { getBlock: 2, getBlockActor: 3, containsAnyLiquid: 7, getMaterial: 12, hasUsableChunkAt: 14, hasChunksAtAabb: 15, hasChunksAtBlockPosRadius: 16, getDimensionType: 18, getDimension: 37 }`
- `WhiteShadow8500/Shadow-Schematic-Client` → `src/Game/Bedrock.cpp` (Bedrock 1.26.4005.0):
  `kGetBlockSlot = 2; // BlockSource::getBlock(BlockPos const&)`, `kDimensionBlockSource = 0xF0`, `kActorDimension = 0x1C8`.

**Our own binary agrees with that map.** Slot 3 (`getBlockActor`) tail-calls slot 2 in all
three builds:

```
slot 3 @ 0xfb91a18:
  cmp  w2, #1
  b.eq 0xfb91a30
  cbnz w2, 0xfb91a3c
  ldr  x8, [x0]          ; vtable
  ldr  x2, [x8, #0x10]   ; 0x10 = slot 2
  br   x2                ; tail-call -> getBlock
```

So `resolveVtableFunction("11BlockSource", 2, "libminecraftpe.so")` returns exactly the
function pointer the vtable holds at slot 2. **Confidence: high.**

## 2. Calling convention (AAPCS64)

Disassembly of slot 2 (`0xfb91918`, 1.26.52) — the head proves the argument and return:

```
sub  sp, sp, #0x40
stp  x29, x30, [sp, #0x10]
...
ldr  w8, [x1, #4]        ; BlockPos.y  -> x1 points at the BlockPos (int x,y,z)
ldrsh w9, [x0, #0x42]    ; this->height - 1
cmp  w8, w9
b.lt 0xfb919b0
ldrsh w9, [x0, #0x40]    ; this->height
cmp  w8, w9
b.ge 0xfb919b0           ; y outside [0,height) -> slow path
...
ldr  x8, [x19]           ; this->vtable
ldr  x8, [x8, #0x148]    ; slot 41
blr  x8
cbz  x0, 0xfb919e0
ldrh w2, [x19, #0x42]
mov  x0, sp
mov  x1, x20
bl   ...                 ; read the sub-chunk index
...
ret                      ; x0 = Block* (or null)
```

| Item | Value | Confidence |
| --- | --- | --- |
| `this` | `x0` = `BlockSource*` | high |
| `BlockPos` argument | `x1` = pointer to `BlockPos` (a `const BlockPos&`; `y` read at `[x1+4]`) | high |
| Return | `x0` = `Block*` (a borrowed pointer; may be null) | high |

`getBlock(BlockSource* self, const BlockPos* pos) -> Block*`. The engine's fast path
answers from a sub-chunk; the slow path (`0xfb919b0`) goes to chunk lookup. **Confidence:
high** for the signature; the internal sub-chunk offset (`this+0x40/0x42`) is incidental.

## 3. Distinguishing it from its neighbours

| Slot | Function | Evidence it is not `getBlock` |
| --- | --- | --- |
| 2 | `getBlock` | takes `&BlockPos`, returns `Block*`, called by 3 |
| 3 | `getBlockActor` | has a 3rd arg (`w2`) — `getBlockActor(pos, type)`; tail-calls slot 2 when `type==1`/`0` |
| 7 | `containsAnyLiquid` | returns `bool` (`and w0, wzr, #1`), calls slot 2 then a type test |
| 12 | `getMaterial` | calls slot 2, then dereferences the result and returns a `Material*` |
| 15/16 | `hasChunksAt*` | returns `bool` |
| 18/37 | `getDimensionType` / `getDimension` | no `&BlockPos`; return a small enum / a `Dimension*` |

## 4. What could NOT be established here (and is required for the reader)

Two things are needed beyond the slot, and this analysis did **not** confirm them with
confidence. They are version-specific *member offsets*, and unlike the vtable slot they
cannot be pinned by RTTI/vtable structure alone — they are plain struct offsets, and the
functions that read them are non-trivial (a `weak_ptr` lock, a `std::string` access), so
they do not show up as a trivial `ldr x0,[x0,#imm]; ret` getter that could be matched to a
named method.

1. **How to obtain the `BlockSource*`.** The engine gets it from the local player:
   `Actor -> Dimension -> BlockSource`. The reference build (1.26.4005.0) uses
   `Actor+0x1C8` (Dimension, a `weak_ptr` — so the field is a two-word control block, not a
   plain pointer) and `Dimension+0xF0` (BlockSource). Neither was verified here.
   The preloader's `LocalPlayerFeed` already resolves the local player (ClientInstance
   slot 31) and knows `Actor+0x230/0x238`; it does not know the Dimension/BlockSource
   offsets.
2. **How to turn `Block*` into a block id.** `Block -> BlockLegacy -> name`. The
   reference uses `BlockLegacy = Block+0x68` and `name = BlockLegacy+0xE8`. A scan of the
   binary found `+0x68` as the most common first deref of `getBlock`'s return (83 sites),
   which is consistent with `BlockLegacy = Block+0x68`. The `name` offset was **not**
   confirmed: scanning for `ldr xR,[x0,#0x68] ; add x0,xR,#imm` shows the dominant
   returned member is `+0xC8` (538 sites), not `+0xE8`, so the name offset differs between
   1.26.4005.0 and 1.26.52 (or the reference's field layout differs).

**Vtable addresses found for the surrounding types** (so a reader can be built once the
member offsets are supplied):

| Type | typeinfo name | primary vtable (1.26.52) |
| --- | --- | --- |
| `BlockSource` | `11BlockSource` | `0x13060d48` |
| `Block` | `5Block` | `0x130f0838` |
| `Dimension` | `9Dimension` | `0x12b1abc0` |
| `Actor` | `5Actor` | `0x12fe7840` |
| `LocalPlayer` | `11LocalPlayer` | `0x12d08f90` |

All three remaining values are the "confirmed reader" the design already requires: they
are supplied by the human per build, and the sanity probe (feet not air, 400 blocks up is
air) catches a wrong offset by disabling the source for the session. **This is why the
probe exists** — the slot is the part that can be derived statically, and the member
offsets are the part that must be confirmed against a live process.

## 5. Config values

```json
{
  "verifyBlockSourceSlot": 2,
  "verifyGameVersionTag": "1.26.52"
}
```

`verifyGameVersionTag` must be the running build's version (the gate compares it). The
slot is **2 for every 1.26 build checked** (10 / 52 / 60.30).

## 6. Reader implementation (the `setBlockReader` lambda)

This is the shape BiteMatica's `GameWorldSource` expects. The three version-specific
values are the only guesses and are marked `CONFIRM`; the slot and the call are confirmed.

```cpp
// Confirmed by static analysis (docs/BLOCKSOURCE_GETBLOCK_ANALYSIS.md):
//   BlockSource::getBlock is vtable slot 2,  getBlock(this=x0, const BlockPos*=x1) -> Block* = x0.
//   resolveVtableFunction("11BlockSource", 2, "libminecraftpe.so") returns it.
constexpr std::size_t kBlockSourceGetBlockSlot = 2;

// CONFIRM per build (from the reference build 1.26.4005.0; sanity-probed at runtime):
constexpr int32_t kActorDimension        = 0x1C8;  // Actor  -> Dimension (weak_ptr; pointer half)
constexpr int32_t kDimensionBlockSource  = 0xF0;   // Dimension -> BlockSource*
constexpr int32_t kBlockBlockLegacy      = 0x68;   // Block -> BlockLegacy*  (consistent: the
                                                   // most common first deref of getBlock's result)
constexpr int32_t kBlockLegacyName       = 0xE8;   // BlockLegacy -> std::string name
                                                   // NOT confirmed on 1.26.52: the dominant returned
                                                   // member after +0x68 is +0xC8, not +0xE8. Verify
                                                   // this one against a live process first.

using GetBlockFn = void *(*)(void *self, const void *blockPos);

bitematica::mod::GameWorldSource::setBlockReader(
    [](const bitematica::core::BlockPos &pos, bitematica::core::BlockState &out) -> bool {
      // 1. the live BlockSource* (from the local player the preloader already tracks)
      void *actor = /* ClientInstance slot 31 -> Actor* */;
      if (actor == nullptr) return false;
      void *dimension = *reinterpret_cast<void **>(
          reinterpret_cast<std::uintptr_t>(actor) + kActorDimension);
      if (dimension == nullptr) return false;
      void *blockSource = *reinterpret_cast<void **>(
          reinterpret_cast<std::uintptr_t>(dimension) + kDimensionBlockSource);
      if (blockSource == nullptr) return false;

      // 2. call getBlock via the confirmed vtable slot
      static GetBlockFn getBlock = reinterpret_cast<GetBlockFn>(
          pl::memory::resolveVtableFunction("11BlockSource", kBlockSourceGetBlockSlot,
                                            "libminecraftpe.so"));
      if (getBlock == nullptr) return false;

      struct { int x, y, z; } blockPos{pos.x, pos.y, pos.z};
      void *block = getBlock(blockSource, &blockPos);
      if (block == nullptr) return false;   // unloaded chunk -> caller reports Unknown

      // 3. decode Block -> BlockLegacy -> name
      void *blockLegacy = *reinterpret_cast<void **>(
          reinterpret_cast<std::uintptr_t>(block) + kBlockBlockLegacy);
      if (blockLegacy == nullptr) return false;
      // BlockLegacy+0xE8 is a std::string (libc++ layout: 24 bytes, size at +8, cap at +16)
      auto *nameStr = reinterpret_cast<const std::string *>(
          reinterpret_cast<std::uintptr_t>(blockLegacy) + kBlockLegacyName);
      out.name = *nameStr;                  // e.g. "minecraft:stone"
      out.states.clear();                   // block states need a separate walk
      return !out.name.empty();
    });
```

Everything stays behind the existing gates: the version tag, the reader's presence, and
the sanity probe. If any `CONFIRM` offset is wrong, the probe fails and the source is
disabled with a named reason — it never colours the schematic wrong.

## 7. On-device test steps

1. Set `verifyBlockSourceSlot: 2` and `verifyGameVersionTag` to the running build.
2. Install the reader above (with the `CONFIRM` offsets for your build).
3. Stand on solid ground; enable verification.
4. Expected: the status line reads `world source ready (slot 2, version ...)`, and the
   probe passes (feet not air, 400 up is air).
5. If it reads `world source probe failed: the block under the player read as air`,
   the `kActorDimension`/`kDimensionBlockSource`/`kBlockBlockLegacy` offsets are wrong
   for this build — re-derive them (they are the only unconfirmed values) and retry.
6. Once the probe passes: place 5 blocks of the schematic and confirm the verifier
   colours them `correct`.
