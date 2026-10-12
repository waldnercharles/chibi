/* CPU-only entry points reviewed for agent iteration. Unknown/new selectors
 * fail closed until classified; tools/agent.py routes each one. */
CHIBI_CPU_SELECTOR("--source-presentation")
CHIBI_CPU_SELECTOR("--source-edit")
CHIBI_CPU_SELECTOR("--source-systems")
CHIBI_CPU_SELECTOR("--system-schedule")
CHIBI_CPU_SELECTOR("--compile-installed-space-cadet")
/* PCM analysis and a dummy SDL audio device; graphics and real input disabled. */
CHIBI_CPU_SELECTOR("--audio-analysis")
/* Seven CPU cases: one noise shader, two particle vertex shaders and four
 * authored curve widths with metadata roundtrips and evaluation, compiled to
 * WGSL without opening a device. Curve cases require graph compilation. */
CHIBI_CPU_SELECTOR("--shader-webgpu")

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
/* The 3D renderer's cases in a hidden 64x64 app, with readback. */
CHIBI_GPU_SELECTOR("--scene3d")
/* Two 64x64 frames check solid and additive particle silhouette coverage. */
CHIBI_GPU_SELECTOR("--scene3d-aa")
/* Two one-particle fields, four hidden 64x64 frames and pixel readbacks check
 * opaque/additive depth order independently of persistent field cache order. */
CHIBI_GPU_SELECTOR("--particle-depth")
/* Eleven 64x64 float readbacks cover retained particle state and resets. */
CHIBI_GPU_SELECTOR("--particle-state")
/* The same cases on GLES, under xvfb-run on Linux. */
CHIBI_GPU_SELECTOR("--scene3d-gles")
/* Color ramp table cases through 2D and material raster shaders in a hidden
 * 64x64 app, with readback. */
CHIBI_GPU_SELECTOR("--material-ramps")
