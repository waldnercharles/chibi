# Neon-Swarm material rendering

The shaders and radiance dispatch core are adapted from the user's local
Neon-Swarm reference project. Original shader comments are retained. Source
paths are `src/lighting.c`, `src/render_systems.c`, `shaders/entity.shd`,
`shaders/hrc`, and their shared shader includes.

`material_raster` accepts a game-independent list of geometry and straight linear
material coefficients. It writes scene, emission, absorption and albedo textures.
Stable per-tile lists preserve source draw order without a fixed entity limit per
tile. The entity layout carries per-entity simulation time and full float material
coefficients. The shader uses the source distance and compositing equations.

`radiance` borrows those textures, solves four directional hierarchical radiance
cascades, and composites into a caller-owned canvas. Its source passes are
prefilter, trace, extend, merge, gather, blur, bounce and composite. The saved
reference configuration sets ambient to 0, emission intensity to 7.5 and fog to 1.
A black background replaces the reference game's separate space background.

The current Cute shader compiler rejects the source packed R11G11B10 storage
qualifier. These intermediate images use RGBA16F instead, with matching shader
qualifiers and eight-byte reset uploads. Material images are also RGBA16F. The
light transport and composite equations are unchanged.

Create after the Cute app and after registering custom shape snippets. Set the
shader directory once before creating the renderers. Resize, reset and draw must
run with an active GPU frame and an empty Cute draw queue; bounce and composite
flush drawing into their own canvases. Per-canvas clear overrides preserve host
clear state. Destroy resources before destroying the Cute app.

Material rasterization and final composition use the viewport's pixel resolution.
Only the radiance work grid uses half resolution, capped at 640 pixels on the
longer edge. Camera or target changes and restart reset temporal history.
GLES3 retains ordinary shape rendering. Movement trails use the source rear cone, emission rate, lifetime, drag, size
curve and color transition. Existing impact particles retain their simpler shape
and fade.
