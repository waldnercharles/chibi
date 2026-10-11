/* CPU-only entry points reviewed for agent iteration. Unknown/new selectors
 * fail closed until classified; tools/agent.py routes each one. */
CHIBI_CPU_SELECTOR("--source-presentation")
CHIBI_CPU_SELECTOR("--source-edit")
CHIBI_CPU_SELECTOR("--source-systems")
CHIBI_CPU_SELECTOR("--system-schedule")
CHIBI_CPU_SELECTOR("--compile-installed-space-cadet")
/* Model graphs compiled to distance programs, bounds and hit resolvers. */
CHIBI_CPU_SELECTOR("--model-graphs")
/* Models and materials in the renderer's shader catalog, without a window. */
CHIBI_CPU_SELECTOR("--model-catalog")
/* Appearance and mesh surface geometry, without a window. */
CHIBI_CPU_SELECTOR("--render-geometry")
/* The support arena allocator. */
CHIBI_CPU_SELECTOR("--arena")
/* Shader graphs evaluated on the CPU and compiled to GLSL, without a window. */
CHIBI_CPU_SELECTOR("--shader-graphs")

/* Explicit opt-in checks against installed products; each runs under the
 * shared 60-second supervisor. */
CHIBI_GPU_SELECTOR("--player-installed-space-cadet-vm")
CHIBI_GPU_SELECTOR("--player-installed-space-cadet-sljit")
CHIBI_GPU_SELECTOR("--studio-source-edit")
CHIBI_GPU_SELECTOR("--studio-graph-visibility")
/* Cute's interior border on two shapes in a hidden 64x64 app, through the
 * instanced and tiled draw paths, with readback. */
CHIBI_GPU_SELECTOR("--interior-border")
/* An engine viewport in a hidden app takes its first requested size at once
 * and later sizes after two draws. */
CHIBI_GPU_SELECTOR("--viewport-resize")
/* Meshes and SDFs hiding each other in a hidden 96x80 app, with and without
 * hidden-surface rejection and its depth snapshot copies, with readback. */
CHIBI_GPU_SELECTOR("--sdf-visibility")
/* HRC's transport, adaptive steps, timing and single passes in a hidden
 * 128x128 app, with readback. */
CHIBI_GPU_SELECTOR("--radiance")
/* HRC's Depth thickness surface light and its surface gather from material
 * inputs written by hand, in a hidden 32x32 app drawing 32x32 and 128x128
 * targets, with readback. */
CHIBI_GPU_SELECTOR("--radiance-depth")
/* How the material raster samples shapes in a hidden 65x49 app: emission
 * energy on the pixel grid, tile order, tile groups and merged borders, with
 * readback. */
CHIBI_GPU_SELECTOR("--material-sampling")
/* The 2D scene renderer's lighting cases in a hidden 64x64 app, with
 * readback. */
CHIBI_GPU_SELECTOR("--scene2d")
/* The 3D renderer's cases in a hidden 64x64 app, with readback. */
CHIBI_GPU_SELECTOR("--scene3d")
/* The same cases on GLES, under xvfb-run on Linux. */
CHIBI_GPU_SELECTOR("--scene3d-gles")
/* Color ramp table cases through 2D and material raster shaders in a hidden
 * 64x64 app, with readback. */
CHIBI_GPU_SELECTOR("--material-ramps")
/* HRC's surface light through relief materials in a hidden 128x128 app, with
 * readback. */
CHIBI_GPU_SELECTOR("--radiance-surface")
/* HRC's relief shadows by four and eight horizons, their hierarchy and their
 * cache in a hidden 128x128 app, with readback. */
CHIBI_GPU_SELECTOR("--radiance-horizons4")
CHIBI_GPU_SELECTOR("--radiance-horizons8")
/* The material raster's graph materials and fluids, the light HRC gives
 * fluids, and crystal reflections in a hidden 128x128 app, with readback. */
CHIBI_GPU_SELECTOR("--material-pixels")
/* Compute shaders of shader graph GLSL against the CPU evaluator in a hidden
 * 32x32 app, with readback. */
CHIBI_GPU_SELECTOR("--shader-parity")
/* Ramp recompiles, program tables and image texture routes through raster,
 * fragment and 3D preview programs in a hidden 64x64 app, with readback. */
CHIBI_GPU_SELECTOR("--shader-programs")
/* Orbit cameras over material raster meshes, SDFs and 2D layers in a hidden
 * 96x80 app, with readback. */
CHIBI_GPU_SELECTOR("--material-cameras")
/* Material raster meshes' faces, depth, layers, coverage and imported UVs in
 * a hidden 96x80 app, with readback. */
CHIBI_GPU_SELECTOR("--mesh-materials")
/* SDF surface quality, trace work, gradients and spatial scratch groups in a
 * hidden 96x80 app, with readback. */
CHIBI_GPU_SELECTOR("--sdf-quality")
