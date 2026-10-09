/* CPU-only entry points reviewed for agent iteration. Unknown/new selectors
 * fail closed until classified; the hardware suite retains every assertion. */
CHIBI_CPU_SELECTOR("--asset-documents")
CHIBI_CPU_SELECTOR("--code-source")
CHIBI_CPU_SELECTOR("--automation-authoring-readonly")
CHIBI_CPU_SELECTOR("--automation-search")
CHIBI_CPU_SELECTOR("--authoring-preview-lifecycle")
CHIBI_CPU_SELECTOR("--source-presentation")
CHIBI_CPU_SELECTOR("--source-edit")
CHIBI_CPU_SELECTOR("--source-systems")
CHIBI_CPU_SELECTOR("--source-session")
CHIBI_CPU_SELECTOR("--system-schedule")
CHIBI_CPU_SELECTOR("--execution-request-lifecycle")
CHIBI_CPU_SELECTOR("--code-intelligence")
CHIBI_CPU_SELECTOR("--kit-code")
CHIBI_CPU_SELECTOR("--rcss")
CHIBI_CPU_SELECTOR("--iframe-updates")
CHIBI_CPU_SELECTOR("--kit-documents")
CHIBI_CPU_SELECTOR("--ui-tab-list")
CHIBI_CPU_SELECTOR("--tree-watch")
CHIBI_CPU_SELECTOR("--graph-arrange")
CHIBI_CPU_SELECTOR("--graph-editing")
CHIBI_CPU_SELECTOR("--graph-inline")
CHIBI_CPU_SELECTOR("--graph-signature")
CHIBI_CPU_SELECTOR("--graph-optimize")
CHIBI_CPU_SELECTOR("--runtime-execution")
CHIBI_CPU_SELECTOR("--system-arithmetic")
CHIBI_CPU_SELECTOR("--model-sampled")
CHIBI_CPU_SELECTOR("--behavior-parity")
CHIBI_CPU_SELECTOR("--compiled-behaviors")
CHIBI_CPU_SELECTOR("--whole-function-audit")
CHIBI_CPU_SELECTOR("--collab")
CHIBI_CPU_SELECTOR("--material-thermal")
CHIBI_CPU_SELECTOR("--material-mpm")

/* Explicit opt-in checks retain reference resolution and assertions. */
CHIBI_GPU_SELECTOR("--model-sampled-gpu")
CHIBI_GPU_SELECTOR("--shader-detail-gpu")
/* Fixed prepared SpaceCadet artifact, UI/font-only manifest; four hidden
 * 1280x720 frames per backend, each under the shared 60-second supervisor. */
CHIBI_GPU_SELECTOR("--player-space-cadet-vm")
CHIBI_GPU_SELECTOR("--player-space-cadet-sljit")
CHIBI_GPU_SELECTOR("--studio-starter-startup")

CHIBI_CPU_SELECTOR("--compile-installed-space-cadet")
CHIBI_GPU_SELECTOR("--player-installed-space-cadet-vm")
CHIBI_GPU_SELECTOR("--player-installed-space-cadet-sljit")

CHIBI_GPU_SELECTOR("--studio-source-edit")
/* Cute's interior border on two shapes in a hidden 64x64 app, through the
 * instanced and tiled draw paths, with readback. */
CHIBI_GPU_SELECTOR("--interior-border")
/* Meshes and SDFs hiding each other in a hidden 96x80 app, with and without
 * hidden-surface rejection and its depth snapshot copies, with readback. */
CHIBI_GPU_SELECTOR("--sdf-visibility")
/* The production warped-SDF shape in a hidden 128x128 app, through the
 * instanced and tiled draw paths, against its CPU field, with readback. */
CHIBI_GPU_SELECTOR("--sdf-coverage")
/* The 3D renderer's cases in a hidden 64x64 app, with readback. */
CHIBI_GPU_SELECTOR("--scene3d")
/* The same cases on GLES, under xvfb-run on Linux. */
CHIBI_GPU_SELECTOR("--scene3d-gles")
/* Color ramp table cases through 2D and material raster shaders in a hidden
 * 64x64 app, with readback. */
CHIBI_GPU_SELECTOR("--material-ramps")
