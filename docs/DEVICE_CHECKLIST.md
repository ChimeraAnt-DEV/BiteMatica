# Device test checklist

These steps can only be run on a device. The AI cannot run them, so they are listed here for the
human to run after each phase. For every step: note the **expected result**, and if it fails, send
back the **status-line text**, a **screenshot**, and the **log file** from the mod's data directory.

## Phase A — the real renderer ships (delivery plumbing)

1. Install the rebuilt client (preloader pinned at `167226d` or later — see
   `docs/BUMP_PRELOADER.md`) and the new `BiteMatica-<version>.antegg`.
   **Expected:** status line reads `World-space GL (sdk, ...)`.
2. Load `demo_house` and set the origin on a flat area.
   **Expected:** the ghost appears at that world position; nothing is drawn as a flat screen-space
   shape.
3. Walk around the ghost.
   **Expected:** it stays put in the world (it does not follow the camera).
4. Walk behind a real wall so the ghost is on the far side.
   **Expected:** the wall hides the ghost (depth-tested).
5. Temporarily install the mod on an **old** client (preloader before `167226d`).
   **Expected:** the mod still loads, draws **nothing**, and the status line reads
   `World-space renderer unavailable: no render seam - update GlowberryClient`. No screen-space
   overlay appears.
6. Open the menu → Main page → "Rendering".
   **Expected:** `world-space (GL)` when the render seam is live, `unavailable (update launcher)`
   otherwise.
7. Toggle the mod off in the Mod Menu.
   **Expected:** the game looks identical to vanilla; no stale geometry.

## Phase B — renderer correctness (to run after Phase B lands)

1. Stand at the ghost and travel ≥ 100,000 blocks from spawn (or use a far seed).
   **Expected:** no shimmering/jitter of the ghost faces.
2. Rotate the placement 90° four times; mirror twice.
   **Expected:** it returns exactly to the start each time.
3. Background the app and return to the game.
   **Expected:** the ghost reappears, no crash (GL context loss handled).
4. Confirm the game's own rendering is unchanged while the mod is enabled.

*(Later phases append their own checklists here.)*
