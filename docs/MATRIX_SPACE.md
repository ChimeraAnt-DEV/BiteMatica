# The SDK camera matrix space (Phase B1)

This is the written answer to "does the preloader's `viewProjection` map world coordinates or
camera-relative coordinates to clip space?". It is determined **from the preloader source**, not
guessed. Source: `preloader-android` commit `167226d`, files `src/pl/render/CameraSource.cpp` and
`src/pl/render/RenderApi.cpp`.

## Answer

**`RenderContext::viewProjection` maps WORLD coordinates to clip space.** Not camera-relative.

## Why

1. `CameraSource::ResolveOnce()` scans the game's writable data for a perspective projection matrix
   and takes the 16 floats **immediately preceding** it as a candidate view matrix, validating it as a
   rigid transform (bottom row `0,0,0,1`, orthonormal upper-left 3×3). Those are the game's own view
   and projection matrices, which the game uses to draw the world.
2. `CameraSource::ReadFrame()` sets `frame.viewProjection = Mat4::multiply(frame.projection, frame.view)`.
   A view matrix is `world → view` (it carries the camera translation in its last column), so
   `projection * view` is `world → clip`. There is no extra "subtract the camera" step, which is what
   a camera-relative matrix would require.
3. `RenderApi.cpp` derives the eye position from the view matrix as `-Rᵀ·t` (the inverse translation),
   i.e. `cameraPosition` is the camera's **world** position. A camera-relative `viewProjection` would
   place the camera at the origin of that space, making this derivation meaningless. The fact that the
   preloader derives a world eye from the matrix confirms the matrix consumes world coordinates.

## Axis conventions

The preloader's `MakePerspective` / `MakeView` (used for the fallback camera) establish the convention
the mod also assumes:

- **Right-handed** view and projection (the OpenGL convention; `MakeView` builds a standard
  right-handed look-at).
- **Y-up** world (`MakeView(eye, forward, {0,1,0})`).
- **Column-major** matrices, uploadable to a GLES uniform without transposing.
- Bedrock yaw is measured clockwise from `-Z`; forward is
  `(-sin(yaw)·cos(pitch), -sin(pitch), -cos(yaw)·cos(pitch))`.

## Consequence for BiteMatica

Because the matrix is world→clip, the mod can:

- Use `viewProjection` directly as `uMvp` when the schematic vertices are in **world** space.
- Render **camera-relative** for float precision at large world coordinates: compute
  `placementOrigin − cameraPosition` in double precision on the CPU, upload that small offset, and
  build a camera-relative view-projection (`viewProjection * translate(−cameraPosition)`). The
  relative offset stays small even at world coordinate ±30,000,000, so no large float offset ever
  enters the shader.

## The runtime guard

The preloader resolves the camera by a **probe** (`CameraSource`), so the mod does not blindly trust
that a given matrix is world-space. `render::looksWorldSpace` (in `src/render/MathTypes.hpp`) accepts
a matrix only if a point one unit in front of the camera projects near screen centre with `w > 0` — a
property only a world→clip matrix has. If the check fails, the mod falls back to the first-person
camera built from the local player feed. This is the fail-closed guard the brief asks for, and it is
host-tested in `test/host_tests_render.cpp`.

## What could not be verified without a device

- That the game actually populates that view/projection pair on every target build (the preloader's
  own validation is the only evidence available off-device).
- That the probe finds the pair on the user's specific game build.

Both are covered by the device checklist: if the camera is not resolved, the status line says so and
the mod draws nothing rather than drawing at a guessed position.
