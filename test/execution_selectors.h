/* CPU-only entry points reviewed for agent iteration. Unknown/new selectors
 * fail closed until classified; tools/agent.py routes each one. */
/* Bounded prepared scalar scopes and value-host admission; no source I/O. */
CHIBI_CPU_SELECTOR("--execution-scope")
CHIBI_CPU_SELECTOR("--source-systems")
CHIBI_CPU_SELECTOR("--system-schedule")
CHIBI_CPU_SELECTOR("--asset-values")
CHIBI_CPU_SELECTOR("--ui-document")
CHIBI_CPU_SELECTOR("--text-files")
CHIBI_CPU_SELECTOR("--fmt")
CHIBI_CPU_SELECTOR("--particles")
CHIBI_CPU_SELECTOR("--png-encoding")
CHIBI_CPU_SELECTOR("--reference-index")
CHIBI_CPU_SELECTOR("--asset-source")
CHIBI_CPU_SELECTOR("--behavior-loader")
CHIBI_CPU_SELECTOR("--engine-clock")
CHIBI_CPU_SELECTOR("--preview-layout")
CHIBI_CPU_SELECTOR("--fields")
/* C-owned URL gesture lifecycle cases through Node; no browser/network/GPU. */
CHIBI_CPU_SELECTOR("--player-url")
/* Both platform clock initializers with a recorded limiter setter; no clocks. */
CHIBI_CPU_SELECTOR("--player-clock")
/* PCM analysis and a dummy SDL audio device; no graphics or real recording. */
CHIBI_CPU_SELECTOR("--audio-analysis")
/* Three generated shaders compiled to WGSL/MSL without opening a device. */
CHIBI_CPU_SELECTOR("--shader-webgpu")
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
CHIBI_CPU_SELECTOR("--shader-bake")
/* Owned CPU intermediate/source snapshots; no device, files or resolver IO. */
CHIBI_CPU_SELECTOR("--shader-source-debug")
/* Authored provenance policy on two CPU backends, supplied tables only. */
CHIBI_CPU_SELECTOR("--shader-source-chibi")
CHIBI_CPU_SELECTOR("--asset-compiler-host")
CHIBI_CPU_SELECTOR("--asset-packaging-chibi")
CHIBI_CPU_SELECTOR("--material-bake")
CHIBI_CPU_SELECTOR("--environment-authoring")
CHIBI_CPU_SELECTOR("--terrain-seams")
CHIBI_CPU_SELECTOR("--material-surface-mesh")
CHIBI_CPU_SELECTOR("--scene3d-camera")
CHIBI_CPU_SELECTOR("--scene3d-volume")
CHIBI_CPU_SELECTOR("--scene3d-terrain")
CHIBI_CPU_SELECTOR("--terrain-checkpoint")
CHIBI_CPU_SELECTOR("--terrain-storage")
CHIBI_CPU_SELECTOR("--terrain-storage-commands")
CHIBI_CPU_SELECTOR("--terrain-playable")

/* Explicit opt-in checks against installed products. */
CHIBI_GPU_SELECTOR("--studio-source-edit")
CHIBI_GPU_SELECTOR("--studio-graph-visibility")
CHIBI_GPU_SELECTOR("--studio-scene-move")
CHIBI_GPU_SELECTOR("--studio-outliner-move")
CHIBI_GPU_SELECTOR("--studio-graph-pointer")
CHIBI_GPU_SELECTOR("--studio-asset-pointer")
CHIBI_GPU_SELECTOR("--studio-debugger-source-history")
CHIBI_GPU_SELECTOR("--studio-scene-camera")
CHIBI_GPU_SELECTOR("--studio-scene-gizmo3d")
CHIBI_GPU_SELECTOR("--studio-scene-collider3d")
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
/* Six 64x64 readbacks of adjacent terrain chunks: borders, LOD and restored edits. */
CHIBI_GPU_SELECTOR("--material-terrain-seams")
CHIBI_GPU_SELECTOR("--terrain-playable-render")
/* One hidden 64x64 draw of twelve mesh/SDF surfaces and two texture readbacks:
 * independent visible color, absorption/filter coefficients and graph cutouts. */
CHIBI_GPU_SELECTOR("--surface-transport")
CHIBI_GPU_SELECTOR("--material-mesh-cache")
CHIBI_GPU_SELECTOR("--scene3d-terrain-gpu")
CHIBI_GPU_SELECTOR("--scene3d-volume-gpu")
CHIBI_GPU_SELECTOR("--hrc-spot")
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
/* One hidden 64x64 app, seven glTF transform/control comparisons, 21 frames. */
CHIBI_GPU_SELECTOR("--texture-transform")

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
/* 3D tone mapping, bloom, exposure and draw state over an HDR emitter
 * pattern in a hidden app, with readback. */
CHIBI_GPU_SELECTOR("--scene3d-post")
/* 3D primitive framing, picking, bounds and drawing in a hidden 128x128 app,
 * with readback. */
CHIBI_GPU_SELECTOR("--scene3d-shapes")
