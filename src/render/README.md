# Neon-Swarm material rendering

`material_raster` and `radiance` and their shaders (`shaders/entity.shd`,
`shaders/hrc`) are adapted from the Neon-Swarm reference project's
`src/lighting.c`, `src/render_systems.c` and shaders. Original shader comments
are retained.

`material_raster` accepts a game-independent list of geometry and straight linear
material coefficients. It writes scene, emission, absorption and albedo textures.
Stable per-tile lists preserve source draw order without a fixed entity limit per
tile. The entity layout carries per-entity simulation time and full float material
coefficients. The shader uses the source distance and compositing equations.

`radiance` borrows those textures, traces hierarchical radiance cascades in
four directional quadrants, gathers received light, and composites into a
caller-owned canvas. `GameHRC2D` authors its transport `resolution`,
`color_resolution`, `gather_resolution` and adaptive resolution, which steps
detail down to keep the scene's GPU time within `target_fps`; see
[the HRC lab](../../docs/hrc-lab.md#resolution-settings). Material and
intermediate images are RGBA16F, since Cute's shader compiler rejects the
source's packed R11G11B10 storage qualifier. Material rasterization and final
composition use the viewport's pixel resolution. Camera or target changes,
resize and restart reset temporal history.

Create the renderers after the Cute app, and set the shader directory once
before creating them. Resize, reset and draw must run with an active GPU frame
and an empty Cute draw queue; bounce and composite flush drawing into their own
canvases. Per-canvas clear overrides
preserve host clear state. Destroy resources before destroying the Cute app.
