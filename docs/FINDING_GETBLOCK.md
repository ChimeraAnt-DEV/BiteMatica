# Finding `BlockSource::getBlock` for a game build

This is the guide the brief asks for (Phase F2). BiteMatica **will not guess** the vtable slot or the
calling convention for `BlockSource::getBlock`. This document is how a human confirms them, once, per
game build. Until that is done, verification reports "the world is not readable" and nothing is
guessed.

## Why this cannot be automated safely

`BlockSource` is a C++ class with a vtable. Resolving a slot by RTTI name (which the preloader's
`pl::memory::resolveVtableFunction` does) returns a **real function** on every build — but on an
unconfirmed slot it is a real, *unrelated* function. Calling it with `getBlock`'s arguments is a
type-confusion: it will read the wrong arguments, return the wrong type, and fault the game thread.
There is no signature that reliably identifies the getter across builds (it is small, and inlined
variants exist), so the honest answer is a per-build confirmation.

## What you need

- The `libminecraftpe.so` for the target build (pull it from the APK or the device).
- A disassembler: IDA, Ghidra, or `radare2`. Ghidra is free and sufficient.
- The running game version string (the launcher shows it, or the build's `version.json`).

## Locating the getter

1. **Load `libminecraftpe.so`** in Ghidra with the default ARM64 (AArch64) processor.
2. **Find `BlockSource`.** Search symbols/strings for `BlockSource`. The class is stripped, but
   RTTI typeinfo names survive: look for the string `12BlockSource` (the length-prefixed mangled
   type name) in `.data.rel.ro`. The vtable sits near it.
3. **Find a caller of the getter.** The most stable anchor is a function that calls the getter and
   is itself identifiable. Good anchors:
   - The block-placement code path (search for a caller that also references the
     `setBlock`-adjacent vtable slot).
   - The world-save code (a caller that loops over a region and calls the getter per block).
4. **Derive the slot.** From the vtable base of `BlockSource`, count the function pointers to the
   candidate. Confirm it by checking the arguments: `getBlock(const BlockPos&, bool)` (or the
   build's equivalent) takes a pointer to a 3-int position and a bool, and returns a block pointer or
   a small struct.
5. **Confirm it in the preloader.** Use the preloader's `pl::memory::resolveSignature` on a byte
   pattern around the caller, then cross-check that
   `pl::memory::resolveVtableFunction("12BlockSource", slot, "libminecraftpe.so")` returns the same
   address the vtable holds.

## What to put in the config

```json
{
  "verifyBlockSourceSlot": <the confirmed slot>,
  "verifyGameVersionTag": "<the game version you confirmed it on, e.g. 1.26.51>"
}
```

BiteMatica then:
- opens the version gate only for a running version that matches `verifyGameVersionTag` (or, when the
  running version cannot be read, treats the tag as your acknowledgement);
- refuses to call anything until a **confirmed reader** is installed;
- runs a **sanity probe** on the first read: the block under your feet must not be air, and 400
  blocks above must be. If the probe fails, the source is disabled for the session with a named
  reason.

## The confirmed reader (the one code change)

Because the calling convention is build-specific, the actual call is a small function you write once
per build and hand to `GameWorldSource::setBlockReader`. It is a handful of lines in
`src/mod/BiteMaticaMod.cpp` (or a new file next to it), e.g.:

```cpp
// Confirm the argument and return types against the disassembly before writing this.
bitematica::mod::GameWorldSource::setBlockReader(
    [](const bitematica::core::BlockPos &pos, bitematica::core::BlockState &out) {
      // Call the confirmed getter with the world/BlockSource pointer the SDK tracks.
      // Convert its result (a runtime block id) to a BlockState via the game's own id table.
      // Return false when the chunk is not loaded.
      return readBlockNative(pos, out);
    });
```

The signature is deliberately not guessed here: it is exactly what the disassembly tells you.

## Checklist to send back

When you have confirmed a build, send:

- [ ] The exact game version string.
- [ ] The confirmed `BlockSource::getBlock` vtable slot.
- [ ] The function's arguments and return type (from the disassembly).
- [ ] The byte signature you used to confirm it (so it can be cross-checked).
- [ ] The `setBlockReader` implementation for that build.
- [ ] The status-line text after enabling verification (it should read
      `world source ready (slot N, version ...)`).

## Alternative data sources (F3, documented, not implemented)

If a slot cannot be confirmed, two alternatives exist; both are worse and are listed for
completeness:

- **Read blocks from loaded chunk memory.** Walk the client's loaded-chunk map and decode the block
  storage directly. Trade-off: the chunk layout is as build-specific as the vtable, and a wrong
  offset is a crash or silent garbage; it also duplicates a lot of the game's paletting logic.
- **Capture block ids from the chunk-render hook.** Intercept the mesh-building call and record the
  block ids it reads. Trade-off: only sees blocks that were actually rendered, and the callback
  frequency is high. Good enough for "what is visible", not for verification of a whole build.

Neither is implemented, because a confirmed `getBlock` slot is both simpler and exact.
