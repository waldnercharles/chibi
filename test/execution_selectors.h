/* CPU-only entry points reviewed for agent iteration. Unknown/new selectors
 * fail closed until classified; tools/agent.py routes each one. */
CHIBI_CPU_SELECTOR("--source-presentation")
CHIBI_CPU_SELECTOR("--source-edit")
CHIBI_CPU_SELECTOR("--source-systems")
CHIBI_CPU_SELECTOR("--system-schedule")
CHIBI_CPU_SELECTOR("--asset-values")
CHIBI_CPU_SELECTOR("--ui-document")
CHIBI_CPU_SELECTOR("--text-files")
CHIBI_CPU_SELECTOR("--fmt")
CHIBI_CPU_SELECTOR("--particles")
CHIBI_CPU_SELECTOR("--png-encoding")
CHIBI_CPU_SELECTOR("--project-document")
CHIBI_CPU_SELECTOR("--prototype-documents")
CHIBI_CPU_SELECTOR("--reference-index")
CHIBI_CPU_SELECTOR("--asset-source")
CHIBI_CPU_SELECTOR("--compile-installed-space-cadet")
CHIBI_CPU_SELECTOR("--graph-document")
CHIBI_CPU_SELECTOR("--graph-presentation")
CHIBI_CPU_SELECTOR("--graph-functions")
CHIBI_CPU_SELECTOR("--vector-nodes")
CHIBI_CPU_SELECTOR("--material-destruction")
CHIBI_CPU_SELECTOR("--material-fluids")
CHIBI_CPU_SELECTOR("--material-mpm")
CHIBI_CPU_SELECTOR("--material-thermal")
CHIBI_CPU_SELECTOR("--material-simulation")
CHIBI_CPU_SELECTOR("--material-ecs")
CHIBI_CPU_SELECTOR("--material-benchmark")
CHIBI_CPU_SELECTOR("--contact-benchmark")
CHIBI_CPU_SELECTOR("--material-render")

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
/* The 3D renderer's global illumination cases in a hidden 64x64 app, with
 * readback, and the same on GLES under xvfb-run on Linux. */
CHIBI_GPU_SELECTOR("--scene3d-gi")
CHIBI_GPU_SELECTOR("--scene3d-gi-gles")
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
