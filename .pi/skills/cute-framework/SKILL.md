---
name: cute-framework
description: Implement or review this project's Cute Framework integration. Use for `cf_*` APIs, Cute Framework application lifecycle, input, audio, rendering, canvases, shaders, physics, virtual files, or gamepad support.
---

# Cute Framework

This project uses Cute Framework as a host library, not as the owner of gameplay
architecture. Keep project policy in the project's C modules and use Cute
Framework for platform, rendering, input, audio, physics, and filesystem mechanisms.

## Ground truth

1. Read `CMakeLists.txt` for the pinned Cute Framework revision.
2. Inspect the nearest project interface before using framework APIs. In particular,
   `app/cute_host.h` intentionally exposes the host surface used by the app.
3. When the required API is not already wrapped, inspect the matching header under
   `build/_deps/cute_framework-src/include/` after configuring the build. Use the
   header and its adjacent documentation as the version-specific authority.

## Integration rules

- Include `<cute.h>` only in modules that own a direct framework boundary. Keep
  framework types out of core graph and authoring models unless that boundary
  requires them.
- Pair every creation API with its corresponding destroy API on the same ownership
  path. Handle IDs become invalid after destruction.
- Use `cf_draw_push()` and `cf_draw_pop()` around local draw-state changes. Queue
  draw calls before the render target is flushed.
- Treat input as frame state: `*_just_pressed` and `*_just_released` are edges;
  `*_down` is held state. Convert it into project actions before gameplay consumes it.
- Keep simulation on Cute Framework's fixed clock when using its physics helpers;
  render independently and interpolate only presentation.
- Use the virtual filesystem for portable game assets and a user write directory
  for saves. Do not make working-directory assumptions for shipped data.
- For shaders, target Cute Framework's supported GLSL subset and match its resource
  set layout. Regenerate precompiled bytecode when the pinned framework revision changes.

## Project boundary

The existing editor renders scene previews through an offscreen `CF_Canvas` in
`engine/viewport.c` and presents it through Dear ImGui. Preserve that separation:
scene rendering targets the canvas, and the editor owns the ImGui presentation and
input focus.
