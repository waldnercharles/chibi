#!/usr/bin/env python3
"""Temporary command-level test quarantine; remove only after timing review."""
import argparse
import ctypes
import json
import math
import os
from pathlib import Path
import signal
import shlex
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[1]
LIMIT = 60
DEADLINE = 'CHIBI_TEST_DEADLINE'
PROCESS_GROUP = 'CHIBI_TEST_PROCESS_GROUP'
# Exact invocations, not selector prefixes. Historical times are not guarantees.
NATIVE = {
    ('--c-export-alias',): 'Bounded combined C case: twelve I32/I64 DIV/MOD alias fixtures, one authored pause/continuation, two guarded-array fixtures, two four-instruction query-variable functions three five-instruction scalar-component BOOL/U64/F64 readers and three checked scalar writers; one shared-library compiler child with existing 10s limit, source<128KiB, sixteen query-variable calls plus 132 scalar reads/failures/recoveries with stale cached columns and 204 paired generated-C/VM write calls (17 fixed cases x3 types x2 inspection modes x2 backends); native POSIX source build with authoring and configured sanitizers; no new executable/frames/GPU/network; shared60s; independent combined-workload admission required; docs/testing.md',
    ('--ecs-query',): 'Reviewed bounded C ECS query cases: membership/binding cache invalidation, live values, partial traversal, generation safety, allocation-free stable iteration, candidate costs and capped Cartesian retention; no processes/files/frames/GPU; shared60s; docs/testing.md',
    ('--reflect-ranges',): 'First supervised measurement: two reviewed existing C reflection cases, twelve children with existing 10s limits, fixed sources <2KiB, numeric units/ranges/hints/defaults, malformed and unsafe endpoints, transactional failed outputs; no engine frames/GPU/network; shared 60s; docs/testing.md',
    ('--player-clock',): 'Reviewed two deterministic C cases compiling actual player clock initialization for native and browser with only the Cute limiter setter recorded; no clocks/sleeps/window/GPU; 2cases4asserts passed inside native group3.534s/shared60s; browser case fails with the prior unconditional60FPS setter; docs/testing.md',
    ('--audio-browser',): 'Reviewed two C browser PCM age regressions against production capture transport with fake permission/device endpoints, no recording or GPU; native launcher1case8asserts/browser2cases6asserts1.372s; requires matching Wasm test_tools; shared60s',
    ('--player-url',): 'Reviewed nine C-owned gesture lifecycle cases through nine short Node invocations of the production EM_JS bridge; trusted keyboard/pointer gestures, copied URL ownership, cancellation/expiry/restart and one-open limits; fake browser APIs, no browser/network/GPU; measured9cases/67asserts2.278s/shared60s; docs/testing.md',
    ('--audio-analysis',): 'Reviewed twelve fixed CPU cases: PCM bands/levels/onset, chunk/format/channel/source state, snapshot bindings and dummy-SDL mixer lifetime with a shared two-second callback limit; no GPU or real recording; combined measurement12cases/298asserts1.238s/shared60s; docs/testing.md',
    ('--shader-webgpu',): 'Reviewed three CPU Cute compiler cases: noise fragment WGSL plus indexed/unindexed particle vertex WGSL/MSL; one checked-in vertex source<64KiB, no app/window/GPU; combined measurement3cases/32asserts0.770s/shared60s; docs/testing.md',
    ('--studio-scene-collider3d',): 'First supervised measurement: independently reviewed three in-memory prototypes/actors, queued SDL scaled-box face, sphere radius and capsule height edits, typed undo/redo, parallel-axis/nonfinite refusal and move-face Escape/history restoration, hidden-handle refusal and H-during-drag cancellation; twenty-six hidden1920x1080frames fixed1/60, disposable3file fixture, shared60s',
    ('--studio-scene-gizmo3d',): 'First supervised measurement: independently reviewed six in-memory prototypes/eight actors, queued SDL world/local movement and snapping, orientation and mirrored-child rotation, local scaling, undo/redo, Escape/focus-loss/hidden-panel cancellation, shear/singular-parent refusal; forty-two hidden1920x1080frames fixed1/60, disposable3file fixture, shared60s',
    ('--studio-scene-camera',): 'First supervised measurement: independently reviewed one in-memory cube, SDL orbit/pan/anchored dolly, same-frame RMB+W flight, fractional diagonal wheel pan, above-horizon press and checkpoint lens retention, focus-loss cleanup; thirty hidden1920x1080frames, disposable3file fixture, shared60s',
    ('--preview-components',): 'Independently reviewed first CPU measurement: four tiny project files, one declared float component and entity, two previews with two steps each, missing-scene and invalid-declaration cleanup, no GPU/process/network; shared60s',
    ('--studio-scene-move',): 'Reviewed installed SDK hierarchy move integration: bounded 2D picking and two-object 3D model/checkpoint cases, atomic moves, history and collaboration; two hidden frames, shared60s',
    ('--studio-outliner-move',): 'Reviewed installed Studio pointer and keyboard hierarchy moves: one three-object 2D scene, before/inside/after/root, grouped selection and rejection; twenty-one hidden frames, shared60s',
    ('--tilemap',): 'Independently reviewed eight fixed CPU grid/cache/point/SAT/bounds/native-binding/prototype cases, at most four parsed cells and three created ECS entities, two <=4KiB KDL texts, three 64x16 PNG preparations and one rejected 128x128 map, rejected overflow dimensions, no engine frames/GPU/process/network; shared60s',
    ('--web-storage',): 'First supervised measurement; independently reviewed thirteen C-owned browser storage and platform cases, tiny in-memory FS and ZIP fixtures executed under Node, no network or GPU, shared60s',
    ('--webgpu-staging',): 'Reviewed C-owned isolated headless Chrome: one64x64quad for120 frames, peak64MiB staging/reuse, two pending-readback ownership checks, allocator restore before callback drain, one app recreation with stale loss, one validation error and real device loss; callback phases<=1s within startup+execution55s/shared60s; docs/testing.md',
    ('--studio-graph-visibility',): 'Reviewed actual installed Studio graph visibility, scene draft history and tilemap viewport bounds/layer picking; two temporary entities and four CPU picks, disposable three-file fixture and isolated prefs, ten hidden 1920x1080 frames at fixed 1/60, shared60s',
    ('--studio-graph-pointer',): 'First supervised measurement: independently reviewed Counter node pointer selection, wire keys, Find result keys/click/focus, nineteen hidden1920x1080frames, disposable3file fixture, shared60s',
    ('--studio-asset-pointer',): 'First supervised measurement: independently reviewed four tiny prototypes, SDL filtered list/grid selection and modifiers, twenty hidden1920x1080frames, disposable3file fixture, shared60s',
    ('--studio-debugger-source-history',): 'First supervised measurement: independently reviewed one in-memory Counter actor, SDL breakpoint/play/invalid text/undo/redo/step/stop, retained cursor and runtime value assertions, forty-seven hidden1920x1080frames, at most ten source-level steps, shared60s',
    ('--studio-source-edit',): 'Independently reviewed installed Studio source edit/undo/redo on one tiny Counter draft with leading authored comment: fused calls and editable folded Double(3) Value without Target, actual Value pin menu open-function/back and checked inline transaction, two state Get/Set palette creations with exact catalog freshness after inline/undo, Members declaration/Initial rendering and rename, actual Members Rml Variables+ click and queued activation twice with64dp overflowing list, eight finite first-frame panel compositions including post-layout reveal retries and selected-row containment, filtered Add auto-selection/clear, graph/frame and Members-list reveal queued for bool/entity plus Properties rename and Entity type picker callback with no synthetic Initial, selected Entity actual Add/filter clear/selection and undo, Properties Member retention after cached-node invalidation and explicit different-node switch, undo-all current presentation contains only original integer Count, two ed_connect flow gestures (reorder and move into branch); each edit undone to exact draft before baseline literal edit/undo/redo; one crossframe FrameProbe declaration append/reveal/deselect/undo in Both checks visible card then its removal and graph texture revision change within same four frames; disposable three-file fixture and isolated prefs, four hidden1920x1080frames, shared60s',
    ('--interior-border',): 'Reviewed hidden 64x64 app: Cute interior border on a circle and a diamond, opaque and translucent, instanced and tiled draw paths with readback; GPU; docs/testing.md',
    ('--viewport-resize',): 'Reviewed nine bounded hidden64x64 app cases: viewport resize and above-horizon press capture, two lens settings across four aspect requests with project/ray inverse checks, finite near-pi lens fit, displayed-image pointer rays retaining the presented camera until the next draw, and one64x64 UI frame/readback for straight-alpha source-over; GPU, shared60s; docs/testing.md',
    ('--sdf-visibility',): 'Reviewed hidden 96x80 app: mesh and SDF occlusion with hidden-surface rejection on and off, depth snapshot copies, readback; GPU; docs/testing.md',
    ('--radiance',): 'Reviewed hidden 128x128 app: HRC emitter, wall, absorption, bounce and transparent-feedback transport, reset, resize and reused capacity, split color, fractional cells, reduced reconstruction, fitted lighting, every resolution combination, the adaptive ladder, GPU pass timing, and the extend and filter passes alone, with readback; GPU; docs/testing.md',
    ('--radiance-depth',): 'Reviewed hidden 32x32 app with 32x32 and 128x128 targets: HRC Depth thickness light through slabs that block by height unless they glow, from small, moving, edge and mixed-color sources, its falloff, perspective, depth gain on closed receivers, closed receivers taking the light HRC brings open ones, open receivers shading HRC light by facing and height, each light as much as HRC brings it even alone in reach, eight 32x32 bright-source solves preserving shading under reciprocal source/albedo scaling with both visibility modes and fresh/retained history, soft visibility keeping lights on the floor plane lit, gloss, every quality, history, segments and steep planes, and the surface gather by quadrant, crest, reach and turning view, with readback; GPU; docs/testing.md',
    ('--material-sampling',): 'Reviewed hidden 65x49 app: material raster emission energy over sub-pixel phases and two resolutions, overlapping tile order, tile groups for moving masks, and merged borders against their cells in five targets, with readback; GPU; docs/testing.md',
    ('--scene2d',): 'Reviewed hidden 64x64 app: 2D scene renderer drawing a scene without lighting settings unlit, default lighting keeping materials visible, and the unlit view or removed settings stopping lighting, normal-mapped sprites facing toward and away from a light with and without a Material, sprite maps that are missing or the wrong size and relief that is not finite, and an Appearance lamp lighting a closed Depth thickness floor by its relief scaled with its visual, with readback; GPU; docs/testing.md',
    ('--scene3d',): 'Reviewed hidden 64x64 app: 3D renderer cases for lights, shadows, exposure, sky, ramps, imported meshes and their normal maps, imported glTF Standard materials and tangent frames, Transform3D placement and mirroring, occlusion, ambient occlusion, alpha modes and masked shadows with readback; GPU; docs/rendering-3d.md',
    ('--scene3d-gles',): 'Reviewed hidden 64x64 app on GLES under xvfb-run: the same 3D renderer cases; GPU, Linux; docs/rendering-3d.md',
    ('--scene3d-gi',): 'Reviewed hidden 64x64 app: 3D global illumination cases for bounced and emitted light, open ground, bulbs and frames around lights, mesh boxes, the probes\' fallback, the BVH, vast solids, light over frames and refits, limits, pass timing and a 16-sampler material, with readback; GPU; docs/rendering-3d.md',
    ('--scene3d-gi-gles',): 'Reviewed hidden 64x64 app on GLES under xvfb-run: the same 3D global illumination cases; GPU, Linux; docs/rendering-3d.md',
    ('--material-ramps',): 'Reviewed hidden 64x64 app: four color ramp table cases through 2D and material raster shaders; GPU; docs/testing.md',
    ('--shader-parity',): 'Reviewed hidden 32x32 app: six compute-shader cases comparing shader graph GLSL with the CPU evaluator for decals, effects, ramps, cells, images and sampled SDFs, plus a sampled image reload, with readback; GPU; docs/testing.md',
    ('--material-cameras',): 'Reviewed hidden 96x80 app: six orbit camera cases for projected bounds, centered circles, frontmost mesh and SDF surfaces, 2D layer planes, a centered four-actor cross and emissive SDF centroids, with readback; GPU; docs/testing.md',
    ('--mesh-materials',): 'Reviewed hidden 96x80 app: eight material raster mesh cases for visible faces, depth between meshes, ordered layers, planes, subpixel coverage, padded views, imported mesh UVs and perspective cards, with readback; GPU; docs/testing.md',
    ('--sdf-quality',): 'Reviewed hidden 96x80 app: ten SDF surface cases for program specialization, analytic spheres, thin rings, planes and creases, trace work, analytic gradients and their fallback, and spatial scratch groups, with readback; GPU; docs/testing.md',
    ('--scene3d-post',): 'Reviewed hidden app: nine 3D post-processing cases for the HDR target and Neutral tone map, bloom threshold, strength, radius, edges and resizes, exposure, non-finite sources, draw state and retained output, with readback; GPU; docs/testing.md',
    ('--scene3d-shapes',): 'Reviewed hidden 128x128 app: fourteen 3D primitive cases for overhead framing, picking, turned bounds, flat triangles, drawing, instanced batching, depth order, parallel geometry, prisms, octahedra, composed parts, polygons and box frame parts, with readback; GPU; docs/testing.md',
    ('--shader-programs',): 'Reviewed hidden 64x64 app: eleven ramp recompile, preview, raster, offset, capacity and program-id cases and image texture routes through raster and fragment programs, with readback; GPU; docs/testing.md',
    ('--radiance-surface',): 'Reviewed hidden 128x128 app: HRC silhouettes, relief facing and below the light, world-space surface depth, directional surface colors in every shadow mode, surface light history, and sky light past overhangs and closed rooms, with readback; GPU; docs/testing.md',
    ('--radiance-horizons4',): 'Reviewed hidden 128x128 app: four-direction horizons following moving relief, their hierarchy against the fine scan, and their cache against fresh horizons over 15 stages for 8 option sets, with readback; GPU; docs/testing.md',
    ('--radiance-horizons8',): 'Reviewed hidden 128x128 app: the same horizon cases with eight directions; GPU; docs/testing.md',
    ('--material-pixels',): 'Reviewed hidden 128x128 app: material parameter overrides, world-space and graph materials, shape graphs and fragment programs, fluid placement, gas, liquid, fuel, plume, parcel and wisp pixels, fluid light and shadow, liquid refraction, crystal reflections, compact uploads of mixed graph, plain and mesh batches, sprite normal and height maps, sprite and shape relief raised by a material Height, with readback; GPU; docs/testing.md',
    ('--texture-transform',): 'Reviewed hidden 64x64 app: seven glTF texture transform/baked-UV comparisons with negative controls, 21 frames; GPU; docs/rendering-3d.md',
    ('--shader-parameters-prepared',): 'Reviewed ten bounded CPU cases: material/model parameter codecs and staging, particle signature/default/helper validation, particle staging and rejected mesh-material use, preview reload/rename/rejection, reconstructed model parameter generations, and 129 zero-count field entities for source budget reclamation; no GPU; docs/testing.md',
    ('--source-systems',): 'First supervised measurement; independently reviewed eleven pico source system cases on VM/SLJIT, fixed pumps and at most 259 entities, CPU only; docs/testing.md',
    ('--source-reductions',): 'Reviewed fixed VM/SLJIT cases: <=4 target owners and 256 contributor rows, worker gate <=2s, ordered row merge/failure rollback, singleton scheduler hook/dependent publication, instrumentation fallbacks, metadata preflight, PostPhysics pause/restart/cancellation; no rendering/files/children; shared 60s; docs/testing.md',
    ('--source-reduction-benchmark',): 'Reviewed five modes x one target/256 contributors, two systems, one warmup+5x8passes; joined two-worker pool, no files/frames/GPU; docs/testing.md',
    ('--source-execution-benchmark',): 'Reviewed eight fixed single-program modes: 16/1024 rows, one/64 integer loop iterations, serial/requested two-worker rows, ordered heavy and unordered sums, one warmup+5x4passes; joined workers, no files/frames/GPU; shared60s; docs/testing.md',
    ('--source-query-rows',): 'First supervised measurement; reviewed finite tuple/filter lifecycle cases on VM/SLJIT, at most 33 entities and six supplied-time steps per fixture, CPU only; docs/testing.md',
    ('--new-asset',): 'One new-asset scaffolding case: one temporary content folder, three written assets and six refusals, CPU only; docs/testing.md',
    ('--asset-refactor',): 'One asset rename case: four in-memory prototypes, planned rewrites of a nested child and of a child that an including prototype adds, no files, CPU only; docs/testing.md',
    ('--source-edit',): 'Independently reviewed fifteen fixed source-edit cases, tiny literal inputs and checked transactions including extraction/inline/signature/binding/capture/layout, fused-call delete/move/expression/insert/extraction, named-action disable/enable edits and Value input replace/connect preserving its assignment destination, source Get/Set creation with qualified quoted state identity/defaults/typed producers and lexical shadow checks, declaration append after leading line/block comments and component compound Get/Set metadata remap with distinct positions/comments/watched pins, checked state retype preserving comments/compatible initializers and rejecting ill-typed uses, no files/world/runtime/process/GPU; shared60s; docs/testing.md',
    ('--ecs-query-benchmark',): 'Reviewed three fixed ECS query workloads, 4096 entities x 2000 passes each, CPU only; run through benchmark_trials',
    ('--scheduler-benchmark',): 'Reviewed tiny C scheduler workloads: 12 systems x128 passes x5 trials, stable refresh and run-only, owned-descriptor/dependency/failure checks; no threads/files/frames/GPU; shared60s; docs/testing.md',
    ('--system-schedule',): 'First supervised measurement; independently reviewed fixed dependency/order cases, 62 small engine frames and serial/parallel 128 entities x 20 frames, CPU only; docs/testing.md',
    ('--material-destruction',): 'Material damage, terrain preparation, debris, blasts and their impacts on bodies; 26 cases, fixed grids, CPU only; docs/testing.md',
    ('--material-fluids',): 'Fluid flow, cohesion, buoyancy and cave fields; 12 cases, fixed grids, CPU only; docs/testing.md',
    ('--material-mpm',): 'MPM particles and their grid transfer; 9 cases, fixed grids, CPU only; docs/testing.md',
    ('--material-thermal',): 'Heat transfer, phase changes and energy conservation; 12 cases, fixed grids, CPU only; docs/testing.md',
    ('--material-simulation',): 'Solver steps, mixed cells, obstacles, particle index and reactions; 27 cases, fixed grids, CPU only; docs/testing.md',
    ('--material-ecs',): 'Material ECS blasts, constitutive response, detail and impulses; 10 cases, CPU only; docs/testing.md',
    ('--material-render',): 'Material coefficient extraction and level of detail; 6 cases, CPU only; docs/testing.md',
    ('--material-benchmark',): 'Fixed DDR, MPM, thermal and gas workloads; serial and four-worker hashes must match, energy conserved; CPU only; docs/testing.md',
    ('--contact-benchmark',): 'Fixed 2D contact workload with its timing; 2 cases, CPU only; docs/testing.md',
    ('--asset-values',): 'KDL asset text and native value trees: limits, ranges, malformed documents, snapshots and equality; 9 in-memory cases, CPU only; docs/testing.md',
    ('--ui-document',): 'UI documents and RCSS: composition, widgets, slots, scoping, events, nesting and line maps; 15 cases on a few temporary files, CPU only; docs/testing.md',
    ('--text-files',): 'File watch, text overlay and text edits; 3 cases on temporary files, CPU only; docs/testing.md',
    ('--fmt',): 'Vendored fmt arguments and the Format Text specifier path; 2 cases, CPU only; docs/testing.md',
    ('--particles',): 'Particle bursts without a renderer: curves, determinism, limits and expiry; 3 cases, CPU only; docs/testing.md',
    ('--png-encoding',): 'Cute PNG encoder window and pixel round trip; one case, no window or GPU; docs/testing.md',
    ('--project-document',): 'Project manifest versions, new projects and fixture content roots; 3 cases on temporary projects, CPU only; docs/testing.md',
    ('--project-declarations',): 'Bounded compatibility check: one pico case, exact two Studio sources/two stale sidecars totaling 36450 bytes plus two fixed call/unary control snippets under 1 KiB, six direct migrations/three explicit current parses/four metadata checks/two transactions, at most 33 current/historical parses including migration internals, four then five file inputs including one 32-byte malformed source, at most six content/staging files; no authored execution/backend/children/render/network/extra corpus; shared 60s; docs/testing.md',
    ('--prototype-documents',): 'Prototype and scene documents with retained components; 8 in-memory cases, CPU only; docs/testing.md',
    ('--reference-index',): 'Reference index over prototypes, scenes and UI pages; 5 cases, CPU only; docs/testing.md',
    ('--asset-source',): 'Asset source edits, completion and hover over reflected components; 6 cases, CPU only; docs/testing.md',
    ('--behavior-loader',): 'All-or-nothing behavior loading, failed preview launches and scene override preparation; 3 cases, CPU only; docs/testing.md',
    ('--engine-clock',): 'Engine clock, pause, step and headless engine frames; 6 cases, CPU only; docs/testing.md',
    ('--preview-layout',): 'Preview world layout; 2 cases, CPU only; docs/testing.md',
    ('--fields',): 'Field graphs, sampling, cave fixtures, source-library linking/defaults, every field node and function-to-function calls, and publication rollback with retained drafts and name-stable source calls; 20 cases, CPU only; docs/testing.md',
    ('--model-graphs',): 'Model graphs compiled to distance programs, bounds, hit resolvers and sampled SDFs; 39 cases, in-memory, CPU only; docs/testing.md',
    ('--model-catalog',): 'Model parameters, material scope, validation, materials and surfaces in the shader catalog; 29 cases including live model registration, diagnostics and rename, CPU only; docs/testing.md',
    ('--render-geometry',): 'Appearance and mesh surface geometry, including finite scaled relief; 8 cases, CPU only; docs/testing.md',
    ('--arena',): 'The support arena allocator; 2 cases, CPU only; docs/testing.md',
    ('--shader-graphs',): 'Shader graphs evaluated on the CPU and compiled to GLSL; 16 cases, CPU only; docs/testing.md',
    ('--shader-bake',): 'Bounded material channel baking; eight CPU cases, no files or GPU.',
    ('--material-bake',): 'Independently reviewed bounded CPU controller baking: tiny grids, three children under shared60s, no GPU.',
    ('--environment-authoring',): 'Three terrain cases: one authored Tick, two bounded physics probe steps across a seam, and one controller validation child under shared60s, no GPU.',
    ('--terrain-seams',): 'Bounded CPU terrain contour and exact collision coverage over fixed small grids, no window or files.',
    ('--material-surface-mesh',): 'Fixed CPU geometry specimens checking relief, recess, cuts, seams, normals, stable source frames and terrain volumes, no window or files; shared60s.',
    ('--scene3d-camera',): 'Three bounded CPU projection, parallel-ray, ground-picking and perspective regression cases; no window or files; shared60s.',
    ('--scene3d-volume',): 'Three CPU default-disabled no-op, physical interval and bounded quality cases; no window or files; shared60s.',
    ('--scene3d-terrain',): 'Seven bounded CPU mesh-shadow range, occupancy/proxy, revision, empty/reset/retirement, capacity, terrain-volume incremental rebuild and box-budget cases; empty-volume regression bakes one constant 3x3x3 lattice and checks initial, cached and moved origins; no frames/GPU; shared60s.',
    ('--scene3d-terrain-gpu',): 'One hidden32x32 context, tiny live-mesh upload/reuse/cut/retirement case; shared60s.',
    ('--scene3d-volume-gpu',): 'One hidden64x64 context, five-frame real 3D blocker, shadow-toggle and analytic extinction readback case; shared60s.',
    ('--material-mesh-cache',): 'One hidden64x64 context, bounded mesh upload/cut/retire lifecycle and absorption readback; shared60s.',
    ('--hrc-spot',): 'One hidden64x64 context, eighteen aimed-light, thin-blocker, slab, offscreen-source and single-scattering readbacks; shared60s.',
    ('--terrain-storage',): 'Bounded CPU storage queue, reset, failure and atomic binary attachment cases; no GPU.',
    ('--terrain-storage-commands',): 'Bounded controller save/load commands in fresh children on a tiny terrain fixture under one shared60s deadline; no GPU.',
    ('--terrain-playable',): 'Authored terrain-lab controls and restart restore in VM and SLJIT fresh children under one shared60s deadline; no GPU.',
    ('--terrain-checkpoint',): 'Bounded finite and streamed terrain snapshots, malformed input refusal and one fresh-process restore child under shared60s; no GPU.',
    ('--terrain-playable-render',): 'One installed player child, four hidden 640x360 frames with bounded PNG readback under shared60s; compilation occurs before the test.',
    ('--surface-transport',): 'One hidden 64x64 draw of twelve mesh/SDF actors, two bounded texture readbacks checking absorption/filter and opacity; shared60s.',
    ('--material-terrain-seams',): 'Reviewed hidden 64x64 app: six terrain readbacks covering shared edge contours, borders, LOD and checkpoint restore, under one shared60s deadline.',
    ('--graph-document',): 'Graph documents over the composed node palette: comments, frames, edge indexes across edits and paste, concurrent readers, ID stability and literals; 14 cases, in-memory, CPU only; docs/testing.md',
    ('--graph-presentation',): 'Inline folding, arrangement, expression text, find and hover sentences over the palette; 14 cases, in-memory, CPU only; docs/testing.md',
    ('--graph-functions',): 'Function and macro signatures, collapse/expand and linked shader library problems; 18 cases, in-memory, CPU only; docs/testing.md',
    ('--vector-nodes',): 'Vector kernels and the palette vector node bindings and defaults; one case, CPU only; docs/testing.md',
    ('--source-presentation',): 'Independently reviewed twenty fixed source/palette cases and two component cases: tiny literal programs for ownership/layout, invalid drafts, callable scope/formulas, linear/branch/loop/await projection, Set/Get origins, Value-only setters, editable indexed/component receivers, natural handler completion and explicit End Event, named typed calls/clone/edit, pure single-result folding with watch/breakpoint/shared-use visibility, namespace field projection and keyed catalog generation, implicit empty entity/array declarations without fake Initial versus authored initializer, callable target resolution and owned hover descriptions, one eager named state-read snapshot, explicit and short-circuit effects; static palette and two in-memory component documents, CPU parse/analyze/projection only, no files/world/runtime/process/GPU; shared60s; docs/testing.md',
}
TOOLS = {
    'player-clock': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--player-clock'],
    'player-url': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--player-url'],
    'audio-browser': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--audio-browser'],
    'audio-analysis': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--audio-analysis'],
    'shader-webgpu': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--shader-webgpu'],
    # Five independently reviewed CPU controller cases: seven tiny files, two entities,
    # thirteen owned children <=10s, twenty fixed simulation frames, shared60s.
    'controller': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--controller',
                   str(ROOT / os.environ.get('CHIBI_CTL', 'build-release/bin/chibi_ctl'))],
    # Independently reviewed ten hidden Studio frames, two actors, 225 panel syncs.
    # CHIBI_STUDIO_IDLE_PROJECT must be a disposable acceptance/project clone.
    'studio-panel-idle': [str(Path(os.environ.get('CHIBI_STUDIO_BUILD', 'build-studio')) / 'bin/studio-acceptance'),
                          '--panel-idle', '--project-root', os.environ.get('CHIBI_STUDIO_IDLE_PROJECT', '')],
    'studio-scene-collider3d': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-scene-collider3d'],
    'studio-scene-gizmo3d': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-scene-gizmo3d'],
    'studio-scene-camera': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-scene-camera'],
    'studio-scene-move': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-scene-move'],
    'studio-outliner-move': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-outliner-move'],
    'tilemap': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--tilemap'],
    'studio-graph-visibility': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-graph-visibility'],
    'studio-graph-pointer': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-graph-pointer'],
    'studio-asset-pointer': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-asset-pointer'],
    'studio-debugger-source-history': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-debugger-source-history'],
    'studio-source-edit': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-source-edit'],
    # Existing admitted source-edit and shader persistence cases, now installed SDK consumers.
    'source-edit': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--source-edit'],
    # Catalog coverage of the all-features fixture, and four frames of each of its scenes.
    'feature-coverage': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                         '--feature-coverage', str(ROOT / 'test/fixtures/feature-coverage')],
    # Independently reviewed first measurement: one CPU Rendering3D snapshot,
    # four simulation frames, ten live entities,
    # one controller child limited to ten seconds inside the shared deadline.
    'render-fixture': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--render-fixture',
                       str(ROOT / os.environ.get('CHIBI_CTL', 'build-release/bin/chibi_ctl'))],
    'preview-components': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--preview-components'],
    # One new-asset scaffolding case in a temporary content folder.
    'new-asset': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--new-asset'],
    # One asset rename case over in-memory prototypes.
    'asset-refactor': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--asset-refactor'],
    # Scaffolded project cases use Release chibi_ctl to bound process startup cost.
    'validate-project': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--validate-project',
                         str(ROOT / os.environ.get('CHIBI_CTL', 'build-release/bin/chibi_ctl'))],
    # One existing unit-diagnostic publication/hot-reload case; original child limits.
    'unit-validation': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--unit-validation',
                        str(ROOT / os.environ.get('CHIBI_CTL', 'build-release/bin/chibi_ctl'))],
    # Six fixed measured sources in two fixtures; bounded semantic-only native catalogs.
    'unit-fixtures': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--unit-fixtures'],
    # Two validate_project cases whose projects read past the shared prototype expansion
    # budget, against the same unsanitized chibi_ctl.
    'prototype-expansion': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--prototype-expansion',
                            str(ROOT / os.environ.get('CHIBI_CTL', 'build-release/bin/chibi_ctl'))],
    'shader-parameters-prepared': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--shader-parameters-prepared'],
    # Existing query correctness cases, including bounded cache/optimizer regressions.
    'ecs-query': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--ecs-query'],
    # Reviewed fixed ECS query workloads; compare builds through benchmark_trials.
    'ecs-query-benchmark': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--ecs-query-benchmark'],
    # Two reviewed arena cases: aligned growth, block reuse after reset and a zero-initialized arena.
    'arena': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--arena'],
    # Reviewed four fixed scheduling cases, including verified source cohorts and mixed phase membership.
    'system-schedule': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--system-schedule'],
    # Same admitted eleven fixed source-system cases through installed public consumer, VM and SLJIT.
    'source-systems': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--source-systems'],
    'source-reductions': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--source-reductions'],
    'source-reduction-benchmark': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--source-reduction-benchmark'],
    'source-execution-benchmark': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--source-execution-benchmark'],
    'source-query-rows': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--source-query-rows'],
    # Reviewed six tiny immutable module generations, two backend worlds, aggregate/string ownership.
    'persistent-store': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--persistent-store'],
    # Reviewed three source presentation/default/layout and actual native palette composition cases, CPU only.
    'source-presentation': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--source-presentation'],
    # Graph documents over the composed palette: comments, frames, edge indexes, IDs and literals, CPU only.
    'graph-document': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--graph-document'],
    # Inline folding, arrangement, expression text, find and hover sentences over the palette, CPU only.
    'graph-presentation': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--graph-presentation'],
    # Function and macro signatures, collapse/expand and shader library problems, CPU only.
    'graph-functions': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--graph-functions'],
    # Vector kernels and the palette's Direction To and Make_CF_V2 defaults, CPU only.
    'vector-nodes': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--vector-nodes'],
    # Reviewed fixed SDK/player install, managed migration and packaging fixtures; one shared deadline.
    'package': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--package'],
    # One finite CLI case: seven launcher calls and one executable copy; fake sibling
    # compiler/player, one tiny authored project, no renderer/network; shared 60s.
    'chibi-cli': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                  '--chibi-cli', str(ROOT / os.environ.get('CHIBI_CLI', 'build/sdk/chibi'))],
    # Fixed POSIX process/VM/SLJIT cases over one controlled worker and disposable
    # root. Bounded capture, short cancellation/timeout cases; no engine builds.
    'build-process': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                      '--build-process',
                      str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'policy_worker'),
                      str(ROOT / os.environ.get('CHIBI_BUILD_HOST', 'build/sdk/chibi-build-host'))],
    # Six tooling cases plus one producer case, four supervised children <=10s
    # each under shared60s: producer, two five-request batches, bad-magic CLI.
    # Actual eight-package closure:16preparation parses,4C+12raw parser calls,
    # 15Build calls@3m. Three malformed reads stop before decode; one tiny
    # reference-entry artifact rejects beforeexecution. Artifacts<=16MiB,
    # inputs<=8KiB,output<=256KiB. No renderer/GPU/network; separate build.
    'compiler-artifact': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                         '--compiler-artifact',
                         str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                         str(ROOT / os.environ.get('CHIBI_COMPILER_HOST', 'build/sdk/chibi-compiler-host'))],
    # Six fixed orchestration cases: 38 API invocations on both backends, one CLI,
    # ten tiny Git roots, controlled CMake workers and Make routing; no engine builds.
    'build-toolchain': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                        '--build-toolchain',
                        str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'policy_worker'),
                        str(ROOT / os.environ.get('CHIBI_BUILD_HOST', 'build/sdk/chibi-build-host'))],
    # Seven offline desktop dependency ownership cases; sixteen children, each <=10s.
    'desktop-dependencies': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--desktop-dependencies'],
    # Two configured-ownership cases:22children,14rejects,13tiny C units and one PCH.
    # Host-produced .res and declared-manifest evidence; native Windows execution untested.
    # Produced fixture programs never execute; existing 10s child/60s route caps.
    'package-ownership': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--package-ownership'],
    # Reviewed nominal alias ownership and nested error-source case, both backends, bounded 16 preparations.
    'whole-alias': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--whole-alias'],
    # Four reviewed public integer edge/flags/live-register/budget cases; 28k fixed calls, 46 tiny functions.
    'whole-integer': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--whole-integer'],
    # One reviewed public scalar conversion case; 18 supported pairs, 12 values, both backends.
    'whole-conversion': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--whole-conversion'],
    # One reviewed public scalar Select case; six widths, two conditions, both backends.
    'whole-select': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--whole-select'],
    # Two reviewed bounded primitive math/ABI cases; 4594 invocations, VM and SLJIT.
    'whole-math': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--whole-math'],
    # Two reviewed owned snapshot cases; four value kinds, three owners/frames, both backends.
    'owned-tick': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--owned-tick'],
    # Two reviewed registry service rebinding and incremental attachment cases; four owners.
    'parallel-tick': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--parallel-tick'],
    # Three reviewed source reaction/rhythm/particle cases; fixed waits/emission and both backends.
    'source-native-features': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--source-native-features'],
    # Five reviewed source math/query cases; numeric samples, nested membership,
    # ten fractional rectangle/point poses with live declared-field/stale-handle edits,
    # 3D collider queries and one two-cell tilemap with two bounded dispatches per VM/SLJIT backend.
    'source-native-numeric-queries': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--source-native-numeric-queries'],
    # One reviewed source material case; four payload types, two backends, CPU shader stage only.
    'source-native-material': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--source-native-material'],
    # Material masks, destruction, fragments, debris, cave fields and streamed terrain; CPU only.
    'material-destruction': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--material-destruction'],
    # Particle fluid sleep, cohesion, splits, body response, gas flow and worker parity; CPU only.
    'material-fluids': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--material-fluids'],
    # MPM transfer, recovery, transactions, worker parity, colliders and two 3000-step pools; CPU only.
    'material-mpm': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--material-mpm'],
    # Fluid heat exchange, walls, dormancy, quenching, worker parity and authored thermal state; CPU only.
    'material-thermal': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--material-thermal'],
    # Fluid containment, mixed phases, obstacle and particle indexes and contact responses; CPU only.
    'material-simulation': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--material-simulation'],
    # Blast craters, constitutive models, appearance detail and fluid impulses through ECS components; CPU only.
    'material-ecs': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--material-ecs'],
    # Four fixed fluid workloads on four workers with timings; compare builds through benchmark_trials.
    'material-benchmark': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--material-benchmark'],
    # Four fixed 2D collider lattices with first and steady step timings; compare through benchmark_trials.
    'contact-benchmark': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--contact-benchmark'],
    # CPU-only material raster coefficients and mask LOD levels; no window or GPU.
    'material-render': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--material-render'],
    # Reviewed CPU-only renderer cases: typed transaction/lifetime, sprite image sets, sky face layout
    # and halves, sky irradiance, the split-sum table, sky publication, HRC view layout and lighting
    # normalization, and pure desktop key/stick mapping; no window, GPU, fonts or files.
    'render-resources': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--render-resources'],
    # Seven reviewed CPU-only render geometry cases: appearance bounds, picking and rotation, and mesh
    # surfaces' bounds, banking, frontmost picks, compound parts and primitive normals; no window.
    'render-geometry': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--render-geometry'],
    # Two reviewed hidden-app 64x64 cases: Cute's interior border on a circle and a diamond,
    # opaque and translucent, through the instanced and tiled draw paths. The agent runs the
    # render-enabled test_tools of its selected build directory.
    'interior-border': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--interior-border'],
    # Nine reviewed hidden-app cases: resize/horizon capture, aspect-preserving camera and
    # wide-angle fit, presented-camera pointer rays through resize, and a 64x64 UI readback.
    'viewport-resize': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--viewport-resize'],
    # Fourteen reviewed hidden-app scenes of meshes and SDFs hiding each other, compared with
    # hidden-surface rejection on and off, plus Cute's whole-canvas depth copy on its own.
    'sdf-visibility': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--sdf-visibility'],
    # Hidden-app 128x128 HRC cases: transport through emitters and walls, reset and resize,
    # lighting settings and the adaptive ladder, pass timing, and the extend and filter passes.
    'radiance': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--radiance'],
    # Hidden-app HRC Depth thickness light into 32x32 and 128x128 targets from hand-written
    # material inputs: slabs, small and moving sources, falloff, gain, gloss, quality, history
    # and surface gather.
    'radiance-depth': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--radiance-depth'],
    # Hidden-app 65x49 material raster sampling: emission energy on the pixel grid, tile order,
    # tile groups, and merged borders against their cells.
    'material-sampling': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--material-sampling'],
    # Hidden-app 64x64 2D renders: a scene without lighting settings draws unlit, and default
    # lighting keeps it visible.
    'scene2d': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--scene2d'],
    'material-terrain-seams': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--material-terrain-seams'],
    # Hidden-app 64x64 3D renders: rhythm, ambient, lights, shadows, exposure, the Environment
    # sky and its light, and color ramps.
    'scene3d': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--scene3d'],
    # The same renders on GLES, the WebGL 2 fallback's backend. GLES needs a display, so the
    # agent runs it under xvfb-run; Linux only.
    'scene3d-gles': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--scene3d-gles'],
    # Hidden-app 64x64 renders of global illumination, apart from the rest so each selector
    # compiles its shaders within the time limit; then the same on GLES under xvfb-run.
    'scene3d-gi': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--scene3d-gi'],
    'scene3d-gi-gles': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--scene3d-gi-gles'],
    # Four reviewed hidden-app 64x64 cases: a ramp past 4 KB of the ramp table through a 2D draw
    # shader and the material raster, every program kind compiling the table, and the table
    # counted among a raster batch's samplers.
    'material-ramps': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--material-ramps'],
    # Six reviewed hidden-app 32x32 compute cases: shader graph GLSL for decals, effects, ramps,
    # cells, images and sampled SDFs against the CPU evaluator, and a sampled image reload.
    'shader-parity': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--shader-parity'],
    # Eleven reviewed hidden-app 64x64 cases: ramp edits that recompile only for structure through
    # fragment, 3D preview and raster programs, offsets, capacity, program ids and image routes.
    'shader-programs': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--shader-programs'],
    # Six reviewed hidden-app 96x80 orbit camera cases: projected bounds and centered circles,
    # frontmost mesh and SDF surfaces, 2D layer planes, a four-actor cross and emitter centroids.
    'material-cameras': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--material-cameras'],
    # Eight reviewed hidden-app 96x80 material raster mesh cases: faces, depth, ordered layers,
    # planes, subpixel coverage, padded views, imported mesh UVs and perspective cards.
    'mesh-materials': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--mesh-materials'],
    # Ten reviewed hidden-app 96x80 SDF surface cases: specialization, analytic spheres, thin rings,
    # planes and creases, trace work, gradients and their fallback, and spatial scratch groups.
    'sdf-quality': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--sdf-quality'],
    # Nine reviewed hidden-app 3D post-processing cases: HDR target and tone map, bloom, exposure,
    # non-finite sources, edges, radius across sizes, draw state and retained output.
    'scene3d-post': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--scene3d-post'],
    # Thirteen reviewed hidden-app 128x128 3D primitive cases: framing, picking, bounds, drawing,
    # depth order, parallel geometry, outlines, composed parts and box frame parts.
    'scene3d-shapes': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--scene3d-shapes'],
    # Hidden-app 128x128 HRC surface light through relief materials: silhouettes, relief facing
    # the light, world-space depth, directional colors in every shadow mode and history.
    'radiance-surface': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--radiance-surface'],
    # Hidden-app 128x128 horizon shadows for one direction count each: moving relief, the
    # hierarchy against the fine scan, and the horizon cache against fresh horizons.
    'radiance-horizons4': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--radiance-horizons4'],
    'radiance-horizons8': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--radiance-horizons8'],
    # Hidden-app 128x128 material raster pixels: graph materials and shapes, compact uploads of
    # mixed batches, fluids and their HRC light, liquid refraction and crystal reflections.
    'material-pixels': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--material-pixels'],
    # Seven independently reviewed glTF transform/baked-UV comparisons and negative controls;
    # one hidden 64x64 app, 21 frames, fixed 4x4 images and disposable import files.
    'texture-transform': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--texture-transform'],
    # Two independently reviewed bounded reflected scene codec/lifetime cases, no file/process/GPU.
    'scene-prepared': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--scene-prepared'],
    # Fourteen independently reviewed bounded hierarchy/transform/snapshot lifetime cases,
    # including a five-entity codec round trip; at most 257 nodes, no files, process or GPU.
    'scene-hierarchy': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--scene-hierarchy'],
    # Proposed diagnostic: three fixed scene-read cases on bytecode and SLJIT; six in-test
    # source preparations, eighteen budgeted dispatches, at most three nodes, no world/process/GPU.
    'scene-snapshot-read': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--scene-snapshot-read'],
    # Five independently reviewed model/shader/mesh preparation, persistence and lifetime cases; no files, world or GPU.
    'model-runtime': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--model-runtime'],
    # One independently reviewed finite constant-folding case through the public authoring interface.
    'graph-authoring': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--graph-authoring'],
    # One independently reviewed full native catalog consumer, generated ABI call and typed observer metadata.
    'native-catalog': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--native-catalog'],
    # Two independently reviewed public graph ownership/topology cases, no process/world/GPU.
    'graph-model': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--graph-model'],
    # One independently reviewed bounded native comparison/conversion ABI and ownership case.
    'native-values': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--native-values'],
    # Two independently reviewed finite formatting and owned-value cases, no world/process/GPU.
    'text-format': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--text-format'],
    # One reviewed in-tree Box3D package case: a falling box comes to rest and three raycasts, CPU only.
    'box3d': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--box3d'],
    # Four reviewed in-tree Radiance .hdr cases: flat and run-length decoding, malformed files,
    # preparation from mounted content and import without replacement; one temporary folder, no GPU.
    'sky-files': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--sky-files'],
    # Six reviewed in-tree indexed .mesh cases: tangents from texture directions, preparation from
    # mounted content, refused triangles and groups, unreadable files, the largest baked mesh and a
    # model export bundle; one temporary folder, no GPU.
    'mesh-files': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--mesh-files'],
    # Twenty-two reviewed in-tree glTF import cases: the files a .gltf or .glb writes, names, refusals with
    # nothing left behind, texture transforms, warnings, mesh limits and tangent frames including prepared normals;
    # one temporary folder, no GPU.
    'model-import': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--model-import'],
    # Thirty-nine reviewed model graph compile cases: solids, transforms, booleans, functions, arrays,
    # profiles, sweeps, anchors, parameters, hits, instanced references, inspection and sampled SDFs;
    # no files, world or GPU.
    'model-graphs': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--model-graphs'],
    # Twenty-seven reviewed cases of models and materials in the renderer's shader catalog: runtime
    # parameters and their prepared texts, slot-scoped material parameters, saved-source validation,
    # material slots, hits and limits, surfaces, bounds and picking; one temporary folder, no GPU.
    'model-catalog': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--model-catalog'],
    # Fifteen reviewed shader graph evaluation cases: mappings, bump and normal frames, triplanar and
    # decal detail, effects, ramps, images and authored roundtrips; no files, window or GPU.
    'shader-graphs': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--shader-graphs'],
    # Tiny fixed grids exercise float channels, slices, ownership, derivatives and failures.
    'shader-bake': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--shader-bake'],
    'terrain-seams': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--terrain-seams'],
    'material-surface-mesh': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--material-surface-mesh'],
    'scene3d-camera': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--scene3d-camera'],
    'scene3d-volume': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--scene3d-volume'],
    'scene3d-terrain': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--scene3d-terrain'],
    'scene3d-terrain-gpu': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--scene3d-terrain-gpu'],
    'scene3d-volume-gpu': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--scene3d-volume-gpu'],
    'material-mesh-cache': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--material-mesh-cache'],
    'hrc-spot': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--hrc-spot'],
    'terrain-checkpoint': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--terrain-checkpoint'],
    'terrain-storage': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--terrain-storage'],
    'terrain-storage-commands': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--terrain-storage-commands', str(ROOT / os.environ.get('CHIBI_CTL', 'build/bin/chibi_ctl'))],
    'terrain-playable': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--terrain-playable', str(ROOT / 'test/fixtures/terrain-lab')],
    'surface-transport': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--surface-transport'],
    'terrain-playable-render': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--terrain-playable-render',
        str(ROOT / os.environ.get('CHIBI_TERRAIN_PLAYER', 'dist/sdk/bin/chibi-player')),
        os.environ.get('CHIBI_TERRAIN_ARTIFACT', ''), os.environ.get('CHIBI_TERRAIN_RESOURCES', ''),
        str(ROOT / os.environ.get('CHIBI_TERRAIN_SHADERS', 'dist/sdk/share/chibi/shaders')),
        str(ROOT / os.environ.get('CHIBI_TERRAIN_RENDER_OUTPUT', '.chibi/environment-authoring/terrain-playable-render'))],
    'material-bake': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--material-bake',
                      str(ROOT / os.environ.get('CHIBI_CTL', 'build/bin/chibi_ctl'))],
    'environment-authoring': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--environment-authoring',
                              str(ROOT / os.environ.get('CHIBI_CTL', 'build/bin/chibi_ctl'))],
    # Nine reviewed native cases: retained spawn generations, a three-step physics world, prepared player handoff/rejection, named scene loading and conflicting-binding refusal, sound requests, declared components, per-step action state and recorded taps, and held actions across a pause.
    'native-runtime': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--native'],
    # 2D physics through ECS and Contact Begin events: motion, contacts, masks, queries and parallel collection.
    'physics': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--physics'],
    # 3D physics through ECS and Contact 3D events: shapes, gravity, velocities, teleports, followers, sensors, interpolation and parallel steps.
    'physics3d': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--physics3d'],
    # Transform hierarchy propagation: 2D and 3D composition, cascade order, moves and reparenting.
    'transforms': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--transforms'],
    'ui-authoring': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--ui-authoring'],
    'prototype-document': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--prototype-document'],
    # Five reviewed bounded field cases, including atomic typed argument codec handoff.
    'field-runtime': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--field-runtime'],
    # Reviewed fixed in-memory source identity/refactor and transactional publication cases.
    'language-navigation': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--navigation'],
    'language-publication': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--publication'],
    # Two reviewed scene lifecycle cases, isolated public SDK consumer, no GPU.
    'runtime-scene': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--scene'],
    # KDL asset text and native value trees: typed scalars, exact reals, ownership, errors.
    'asset-values': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--asset-values'],
    # UI documents and RCSS: composition, widgets, source lines, scoping, events and imports.
    'ui-document': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--ui-document'],
    # File watches, text overlays and text edits in a temporary folder.
    'text-files': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--text-files'],
    # The vendored fmt library and the two local fixes Format Text relies on.
    'fmt': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--fmt'],
    # Particle bursts: curves, fixed-step parity, bounds, rejection and expiry.
    'particles': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--particles'],
    # Cute's PNG encoder: zlib window header and a round trip, no window or GPU.
    'png-encoding': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--png-encoding'],
    # Project manifests: version gates, creation and the two contract fixture projects.
    'project-document': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--project-document'],
    # One exact-real-input migration diagnostic; deliberately outside all aggregates.
    'project-declarations': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--project-declarations'],
    # Prototype rebases, detached components, includes and scene documents in memory.
    'prototype-documents': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--prototype-documents'],
    # Reference index over assets, graphs, documents and calls; scene UI page renames.
    'reference-index': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--reference-index'],
    # Asset source completion, hover, navigation and reconciliation of drafts.
    'asset-source': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--asset-source'],
    # The application source loader and a failed preview scene launch, in a temporary folder.
    'behavior-loader': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--behavior-loader'],
    # The engine's fixed clock: cadence, overload, pause and Step, time scale, interpolation.
    'engine-clock': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--engine-clock'],
    # Preview graph layout: arranging automatic nodes is not an edit.
    'preview-layout': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--preview-layout'],
    # Field programs, terrain chunks, streaming tiles and deterministic trigonometry.
    'fields': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--fields'],
    # Reviewed whitespace-only CST formatter, finite cases and bounded production source manifest.
    # Package separator case: 21 fixed formatting pairs, 42 formatter calls and
    # 84 canonical parses; input/output strings <=58B, maps and idempotence.
    # Adds 1726 assertions: 40 fixed cases/4490 assertions before the corpus.
    # The addition requires independent exact-source/workload admission; shared60s.
    'language-format': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                        '--format', '--corpus',
                        str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'corpus.txt')],
    # Two finite root-storage canonicalization cases: five fixed inputs, no files,
    # catalog additions, runtime frames or child processes; shared 60s supervisor.
    'language-root-state': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--root-state'],
    # Reviewed finite producer process and runtime-only consumer with one shared deadline.
    'language-prepared': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                          '--prepared',
                          str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language')],
    # First supervised measurement: eight fixed recording-host cases, sixteen preparations,
    # at most fourteen runs and twenty callback entries across bytecode and SLJIT.
    # Two calls/run, six arguments/call, 127 bytes/argument, 1000 steps/run; no host
    # process/files/network/GPU/clock use. Existing test-driver timing remains; shared60s.
    'language-build-workflow': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                '--build-workflow'],
    # Reviewed public module consumer; finite ECS/behavior lifecycle on VM and SLJIT.
    'language-headless': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                         '--headless'],
    # Reviewed CPU-only scalar benchmark: VM/SLJIT x inline/helper, five 200-dispatch trials
    # each with 100 loop iterations, four warmups, no frames/files/GPU; shared60s.
    'script-benchmark': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                         '--script-benchmark'],
    'scheduler-benchmark': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--scheduler-benchmark'],
    # Reviewed source-to-runtime event/system/binding cases with finite supplied-time steps.
    'language-query-program': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--query-program'],
    # Bounded in-process SDK generation reuse and invalidation, with four tiny source overlays.
    'language-incremental': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--incremental'],
    # Fixed module-boundary specimen: seven C cases, at most 22 catalogs, 60 sources,
    # 48 lowering requests/42 finalization attempts, 20 programs/56 dispatches; no GPU.
    'language-modules': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--modules'],
    # Bounded scalar proof tests: runtime IR reductions and VM/compiled/inspection parity.
    'language-ranges': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--ranges'],
    # First supervised measurement: 22 direct exact-core pico cases; 30 owner
    # attempts (one intentional refusal), at most 29 owners and one live owner.
    # Per owner <=128 visits, 8388608 charged work, 32768 retained bytes;
    # totals <=3712 visits/243269632 work; <=4096-bit magnitudes, 1026-byte token.
    # No parser/backend/runtime programs, files, children, network or GPU.
    # Separate exact-candidate build and finite-workload admission required; shared60s.
    'language-numeric-exact-core': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                    '--numeric-exact-core'],
    # First supervised measurement requires independent exact-source/workload admission.
    # 23 private package graph pico cases; 28 constructor attempts, 12 refusals,
    # at most two tiny owners or one 256-file/package, 255-import chain owner.
    # Own heap envelope320KiB, borrowed descriptors/scratch64KiB; finite scans,
    # no recursion, parser, files, children, backend, world, frames, network or GPU.
    # Existing owning dispatcher and shared60s; build timing is separate.
    'language-package-graph': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                               '--package-graph'],
    # First measurement requires independent exact-source and workload admission.
    # Eleven fixed pico cases, 31 canonical parses plus one edit; 32 extractions
    # (18 accepted, 14 refused), nine graph attempts (four accepted, five refused).
    # At most four snapshots/files, three packages, five imports; each text<256B,
    # total submitted snapshot text<8192B, owned descriptor/name envelope16KiB.
    # No authored execution, files, subprocesses, backend, world, frames or GPU.
    # Existing owning dispatcher, separate build timing, shared60s; no aggregate.
    'language-package-syntax': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                '--package-syntax'],
    # Seven fixed package cases: 32 parses, 15 analyses, twelve lowers, eight
    # backend finalizations and sixteen direct calls with instruction budget2000.
    # At most three documents per snapshot; twelve fixed source texts each<1KiB.
    # No files, children, worlds, network or GPU; separate build and shared60s.
    # First measurement requires independent exact-source/workload review.
    'language-package-execution': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                   '--package-execution'],
    # Existing23 graph cases compare28 constructions on both backends:
    # 56 Builds+348 bounded queries, 684 packed File/590 Import rows total.
    # Two reused owners (four source parses), max256 files/255 imports.
    # Two max Builds permit50m instructions; other402 calls permit100k each.
    # Sparse edges, scalar arrays<=256; no filesystem/process/services/GPU.
    # Independent exact-source/resource review before first run, separate build,
    # one shared60s ceiling; standalone route outside the language aggregate.
    'language-package-graph-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                     '--package-graph-chibi'],
    # Fourteen shared sources plus two before/edited/fresh triples:20 raw trees,
    # 20 C oracle snapshots and40 authored calls capped at1m instructions each.
    # Two retained owners (four source parses), source<=2KiB, nodes<=1024,
    # depth<=128. No files, processes, services or GPU; separate build/shared60s.
    # Independently review the exact workload before first run; no aggregate.
    'language-syntax-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                              '--syntax-chibi'],
    # Existing11 extraction cases/32 attempts,32 independent raw parses and
    # 64 Extract invocations across2backends (including nested Diagnose work),
    # each capped at1m instructions. Two reused owners prepare3actualsources
    # each; fixture text<256bytes, rawtree<=1024nodes/depth128. No runtimefiles,
    # children/services/GPU; independent review, separate build, shared60s.
    'language-package-syntax-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                      '--package-syntax-chibi'],
    # Three cases,26 fixed lexical requests,26 C oracle calls,52 authored calls
    # capped at1m instructions each. Two owners prepare Paths+core:4parses.
    # Four fixed buffers<=4097bytes; no runtime IO, children, services or GPU.
    # Independent review before first run; separate build/shared60s, no aggregate.
    'language-package-paths-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                     '--package-paths-chibi'],
    # Forty fixed requests (13roots,11files,16imports) plus27root setups:
    # 67 C oracle calls,134 authored calls capped at1m instructions each.
    # Two retained owners prepare4sources each; inputs<=128bytes/3roots/17dirs.
    # No runtime files/children/services/GPU; private external JSON decoding only.
    # Independent first-run review, separate build/shared60s, outside aggregate.
    'language-package-routes-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                      '--package-routes-chibi'],
    # Fifteen cases:45 snapshots,90 authored calls capped at3m instructions.
    # Two retained owners prepare8sources each; <=5files/4imports per request.
    # <=71 C parses and142 raw authored parses; sources<1KiB, trees<=1024nodes.
    # No runtime files/children/services/GPU; first-run independent admission,
    # separate build/shared60s, outside the language aggregate.
    'language-package-snapshot-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                        '--package-snapshot-chibi'],
    # Three cases:38 queries,76 authored calls capped at2m instructions each.
    # Two owners prepare IR+Proof:4parses;2short source parse/analyzes.
    # <=257expressions/32symbols/4arguments/256visits perquery; no runtime IO,
    # children/services/GPU. Independent first-run review, separate build/shared60s.
    'language-constant-proof-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                     '--constant-proof-chibi'],
    # Three cases:70 fixed requests,140 authored calls capped at2m instructions.
    # Two owners prepare4sources each (<64KiB/source):8preparation parses.
    # <=7expressions/1symbol/0aggregate arguments perrequest; no full65536visit
    # exhaustion, runtime IO, children/services/GPU. Independent first-run review,
    # separate build/shared60s, outside the language aggregate.
    'language-constant-scalar-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                      '--constant-scalar-chibi'],
    # Three cases:40 requests,80 authored calls capped at2m instructions each.
    # Two owners prepare3sources each;4real fact parse/analyzes:10parses total.
    # <=32symbols/8scopes/4functions/2documents,220byte names,128byte keys,
    # three diagnostics. No runtime IO, children/services/GPU; separate build,
    # independent first-run review/shared60s, outside the language aggregate.
    'language-lexical-binding-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                      '--lexical-binding-chibi'],
    # Three cases:12raw +4analyzed header requests,32authored calls at100k each.
    # Two owners prepare4sources each (<16KiB);4actual inputs<256bytes:
    # 12parses total. <=1symbol/1function/2scopes/2documents,strings<128bytes.
    # No runtime IO/children/services/GPU; separate build, independent first-run
    # review/shared60s, outside the language aggregate.
    'language-nominal-binding-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                      '--nominal-binding-chibi'],
    # Three cases:36requests,72authored calls at2m each; two owners prepare4
    # sources each (<32KiB) plus4fact parse/analyzes:12parses,6JSONcodec calls.
    # Caps32symbols/8scopes/4functions/2docs/8entries/3imports/exports/4uses,
    # 8tokens/64types/128IDs,220name/256text bytes,128fact nodes/depth16.
    # No runtime IO/children/services/GPU; separate build, independent first-run
    # review/shared60s, outside the language aggregate.
    'language-named-binding-chibi': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                    '--named-binding-chibi'],
    # Eight fixed resolver cases: 24 snapshot constructions, at most 56 parses
    # and 34 imports, five files/four imports per snapshot, three owners live.
    # Each source<1KiB; no filesystem, authored execution, children or GPU.
    # First measurement requires independent review; separate build, shared60s.
    'language-package-resolution': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                    '--package-resolution'],
    # Eleven fixed import/preparation cases: 45 owners, 145 offered files,
    # 80 imports, <=129 package visits before reuse, 34 calls with budget2000,
    # one pure catalog snapshot and one extra parse/nine semantic-only analyses.
    # At most five files<1KiB and four live owners.
    # No filesystem/process/GPU; independent first-run review, separate build,
    # and one shared60s production execution deadline.
    'language-package-imports': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                 '--package-imports'],
    # Eight fixed POSIX filesystem package cases: 22 loader calls, 12 package
    # preparations, <=63 parses/30 imports, <=36 package visits before reuse,
    # and ten VM/SLJIT calls with budget2000. Four files/20 entries per forest;
    # eight disposable roots, <=19 recorded cleanup paths per case, five links,
    # one 512KiB+1 sparse oversized file and one non-opened FIFO. No children,
    # network, engine frames or GPU. Native Linux/macOS only; focused route,
    # separate build and independent exact-source review before first execution.
    'language-package-files': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                               '--package-files'],
    # Four fixed pure core package cases, both backends: 12 preparations,
    # 24 files/12 imports, <=24 package visits, 66 calls with budget10000.
    # Actual core source<16KiB, app<2KiB, arrays<=3 elements, three owners.
    # No filesystem/process/services/GPU; independent first-run review,
    # separate build and one shared60s execution deadline.
    'language-package-core': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                              '--package-core'],
    # Duplicate subset of language-component-write: one C case, eight sizing maps;
    # no source compilation or JIT execution. Already covered once by its group member.
    'language-component-write-layout': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--component-write-layout'],
    # Bounded checked scalar publication through authoritative ECS callbacks.
    'language-component-write': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--component-write'],
    # Fixed six-case selected copy/backend/intern workload; source/admission manifest
    # in docs/evidence/runtime-attribution. First runtime requires independent approval.
    'language-runtime-attribution': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--runtime-attribution'],
    # Fixed legacy array callback case plus native-only scalar/handle globals.
    'language-record-compatibility': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--record-compatibility'],
    # Three fixed complete-document record cases; no authored execution.
    'language-record-document': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--record-document'],
    # Three fixed record state/watch/private frame-input cases, standalone only.
    'language-record-state': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--record-state'],
    # Three fixed native globals/headless UI record cases; no frames or renderer.
    'language-record-services': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--record-services'],
    # Five fixed record catalog/publication cases; standalone, no authored calls.
    'language-record-publication': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--record-publication'],
    # Single VM publication specimen, used unchanged for fixture-only expected-red.
    'language-record-publication-repro': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--record-publication-repro'],
    # CUT-79 fixed 22 semantic documents, four preparations/initializers and 48 calls.
    # Standalone only; exact source/resource review required before first execution.
    'language-inferred-string-arrays': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--inferred-string-arrays'],
    # Optional first semantic specimen only, with the same expected-success assertion.
    'language-inferred-string-array-repro': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--inferred-string-array-repro'],
    # Fixed six-case authored-layout foundation; independent admission required.
    'language-record-values': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--record-values'],
    # Seven fixed semantic record cases; independent workload admission required.
    'language-record-semantic': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--record-semantic'],
    # Five fixed owned prepared-record cases; independent admission, standalone only.
    'language-record-prepared': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--record-prepared'],
    # Two fixed owned-input Outliner projection cases; also in language/Wasm groups.
    'language-outliner-projection': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--outliner-projection'],
    # Seven fixed counted-row proof/fallback/lifetime cases; also in language/Wasm groups.
    'language-row-builder': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--row-builder'],
    # Five fixed row-array construction/alias/collection cases; also in language/Wasm groups.
    'language-row-array': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--row-array'],
    # Five fixed record execution/format cases; independent admission, no aggregate expansion.
    'language-record-runtime': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--record-runtime'],
    # Bounded union scalar replacement and checked component-read differential/control-flow cases.
    'language-scalarize': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--scalarize'],
    # Union-array regression adds2preparations/4dispatches@10k,arrays<=2items;
    # local writes preserve snapshots and retainedstate; no IO/services/GPU.
    'language-program': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                        '--program'],
    # Reviewed finite public builder/VM/native contracts and verifier rejection cases.
    'language-wasm': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--wasm'],
    'language-execution': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                           '--execution'],
    # Reviewed finite semantic contracts with inline sources and an immutable catalog.
    'language-semantic': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                          '--semantic'],
    # Reviewed finite C syntax cases and a corpus capped at 256 files/8 MiB.
    'language': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                 '--syntax', '--corpus', str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'corpus.txt')],
    # Reviewed fixed C driver cases in headless Chrome: no GPU, five selectors,
    # 55s internal deadline, bounded HTTP/output, shared production 60s deadline.
    'wasm-browser': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--wasm-browser'],
    'web-storage': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--web-storage'],
    'webgpu-staging': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--webgpu-staging'],
    'native-tools': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools')],
    'reflect-ranges': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--reflect-ranges'],
    # One launcher, two-module producer and installed runtime-only consumer; ten calls.
    'record-installed-prepared': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                                  '--record-prepared',
                                  str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                                  str(ROOT / os.environ.get('CHIBI_RECORD_RUNTIME_BUILD_DIR', 'build/test/record-runtime') / 'test_tools')],
    # One fixed record C-export case; ten calls and one compiler child; standalone.
    'record-c-export': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--record-c-export'],
    # One fixed canonical generated-C row-builder case; also in compiler-enabled native-tools.
    'row-builder-c': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--row-builder-c'],
    'c-export-alias': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--c-export-alias'],

}
# Tiny fixed C optimizer workloads; correctness checks accompany timing samples.
for selector in ('activation-benchmark', 'binding-benchmark', 'borrow-benchmark', 'incremental-benchmark'):
    TOOLS['language-' + selector] = [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--' + selector]

# Reviewed fixed ownership/topology counterexamples, no frames or child processes.
for selector in ('binding-schedule', 'borrow', 'activation-cache', 'reductions'):
    TOOLS['language-' + selector] = [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'), '--' + selector]

# The same finite C dispatcher compiled for Wasm, with no JavaScript assertions.
# wasm-format shares the 40 fixed C cases/4490 assertions without a corpus,
# including the 1726-assertion package separator case; remains standalone.
wasm_build = ROOT / os.environ.get('CHIBI_WASM_BUILD_DIR', 'build-wasm-language')
for selector in ('wasm', 'execution', 'program', 'headless', 'format', 'root-state', 'publication', 'script-benchmark', 'query-program', 'ranges', 'component-write', 'scalarize', 'activation-benchmark', 'binding-benchmark', 'borrow-benchmark', 'incremental-benchmark', 'binding-schedule', 'borrow', 'activation-cache', 'reductions', 'modules', 'runtime-attribution', 'record-values', 'record-semantic', 'record-runtime', 'record-prepared', 'record-document', 'record-state', 'record-compatibility', 'row-array', 'row-builder', 'outliner-projection'):
    TOOLS['wasm-' + selector] = ['node', str(wasm_build / 'test_language.js'), '--' + selector]

GROUPS = {
    'wasm': ['wasm-wasm', 'wasm-execution', 'wasm-program', 'wasm-headless',
             'wasm-publication', 'wasm-modules', 'wasm-runtime-attribution', 'wasm-script-benchmark', 'wasm-ranges', 'wasm-scalarize', 'wasm-component-write',
             'wasm-activation-benchmark', 'wasm-binding-benchmark', 'wasm-borrow-benchmark', 'wasm-incremental-benchmark', 'wasm-binding-schedule', 'wasm-borrow', 'wasm-activation-cache', 'wasm-reductions', 'wasm-row-array', 'wasm-row-builder', 'wasm-outliner-projection'],
    'native': ['native-runtime', 'native-catalog', 'native-values', 'audio-analysis', 'shader-webgpu',
               'player-url', 'player-clock',
               'physics', 'physics3d',
               'transforms', 'box3d', 'sky-files', 'tilemap', 'runtime-scene', 'scene-prepared',
               'scene-hierarchy', 'render-resources', 'persistent-store', 'whole-alias',
               'whole-integer', 'whole-conversion', 'whole-select', 'whole-math', 'owned-tick',
               'parallel-tick', 'source-native-features', 'source-native-numeric-queries',
               'source-native-material', 'source-presentation', 'system-schedule',
               'source-systems', 'source-query-rows', 'source-reductions', 'source-reduction-benchmark', 'source-execution-benchmark', 'source-edit', 'new-asset', 'asset-refactor',
               'shader-parameters-prepared', 'language-prepared', 'ecs-query-benchmark', 'scheduler-benchmark',
               'preview-components', 'graph-document', 'graph-presentation', 'graph-functions',
               'vector-nodes', 'asset-values', 'ui-document', 'text-files', 'fmt', 'particles',
               'png-encoding', 'project-document', 'prototype-documents', 'reference-index',
               'asset-source', 'behavior-loader', 'engine-clock', 'preview-layout', 'fields',
               'model-graphs', 'model-catalog', 'render-geometry', 'arena', 'shader-graphs', 'shader-bake', 'terrain-seams', 'terrain-storage',
               'material-surface-mesh', 'scene3d-camera', 'scene3d-volume', 'scene3d-terrain'],
    # Whole projects, imported assets and the Release chibi_ctl: feature coverage,
    # project validation and prototype expansion, mesh and model import, the
    # controller and render fixture command-response cases, and Terrain Lab's
    # authored save/load restart.
    'projects': ['mesh-files', 'model-import', 'validate-project', 'prototype-expansion',
                 'controller', 'feature-coverage', 'render-fixture', 'material-bake', 'environment-authoring', 'terrain-checkpoint', 'terrain-storage-commands',
                 'terrain-playable'],
    # Independently reviewed fixed CPU workloads: cave<=2048x768, flow<=192x192,
    # two192-particle3000-stepMPM pools, four workers, no windows/children/network.
    # First measurement of the combined candidate remains under one shared60s.
    'materials': ['material-destruction', 'material-fluids', 'material-mpm', 'material-thermal',
                  'material-simulation', 'material-ecs', 'material-render', 'material-benchmark',
                  'contact-benchmark'],
    'language': ['language', 'language-semantic', 'language-wasm', 'language-execution', 'language-program', 'language-incremental', 'language-ranges', 'language-scalarize', 'language-component-write', 'script-benchmark',
                 'language-activation-benchmark', 'language-binding-benchmark', 'language-borrow-benchmark', 'language-incremental-benchmark',
                 'language-binding-schedule', 'language-borrow', 'language-activation-cache', 'language-reductions',
                 'language-headless', 'language-format', 'language-navigation', 'language-publication', 'language-modules', 'language-runtime-attribution',
                 'field-runtime', 'prototype-document', 'ui-authoring', 'text-format', 'graph-model',
                 'graph-authoring', 'model-runtime', 'language-row-array', 'language-row-builder', 'language-outliner-projection'],
}
NOTICE = ('TEMPORARY TEST QUARANTINE: full native/render and '
          'unadmitted checks DISABLED, NOT PASSED. See docs/testing.md#temporary-60-second-policy.')


def admitted(arguments):
    return tuple(arguments) in NATIVE


def selector_arguments(name):
    if name not in ('TEST_ARGS', 'AGENT_SELECTOR'):
        raise ValueError('unsupported selector environment variable')
    # Tokenize quoted positional arguments without executing shell syntax.
    return shlex.split(os.environ.get(name, ''))


def absolute_deadline(seconds, env):
    if not math.isfinite(seconds) or not 0 < seconds <= LIMIT:
        raise ValueError('test deadline must be at most 60 seconds')
    deadline = time.monotonic() + seconds
    if DEADLINE in env:
        inherited = float(env[DEADLINE])
        if not math.isfinite(inherited):
            raise ValueError('invalid inherited test deadline')
        deadline = min(deadline, inherited)
    return deadline


def process_children():
    # Shared portable PID/parent snapshot. Exclude the probe itself: it has
    # already been reaped when we use the snapshot and is not owned test work.
    probe = subprocess.Popen(['ps', '-A', '-o', 'pid=,ppid='],
                             stdout=subprocess.PIPE, text=True)
    try:
        rows, _ = probe.communicate(timeout=1)
    finally:
        if probe.poll() is None:
            probe.kill()
        probe.wait()
    if probe.returncode:
        raise RuntimeError('test cleanup process inspection failed')
    children = {}
    own_process_seen = False
    for row in rows.splitlines():
        pid, parent = map(int, row.split())
        own_process_seen |= pid == os.getpid()
        if pid != probe.pid:
            children.setdefault(parent, []).append(pid)
    if not own_process_seen:
        raise RuntimeError('test cleanup cannot inspect its own process')
    return children


def proc_children():
    path = Path(f'/proc/self/task/{os.getpid()}/children')
    return [int(pid) for pid in path.read_text().split()]


def cleanup_reader(subreaper):
    # Verify the selected discovery capability before launching test work.
    if subreaper:
        try:
            proc_children()
            return proc_children
        except (OSError, ValueError):
            pass
    try:
        process_children()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        raise RuntimeError('test cleanup unavailable: no usable proc children reader or ps; no test started') from error
    return lambda: process_children().get(os.getpid(), [])


def cleanup_children(read_children):
    # This dedicated subreaper owns all its children, including double-forked or
    # setsid descendants adopted when their parent/nested supervisor is killed.
    # Kill parents, reap, then repeat for newly adopted children until none remain.
    while True:
        children = read_children()
        for pid in children:
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        try:
            pid, _ = os.waitpid(-1, os.WNOHANG)
        except ChildProcessError:
            return
        if not pid:
            time.sleep(.001)


def cleanup_subtree(child):
    # Portable nested cleanup cannot kill the shared ancestor process group.
    # Freeze the live child tree before killing it so parent death cannot hide
    # descendants by reparenting them while we discover the owned subtree.
    if child.poll() is not None:
        return
    owned, pending = [child.pid], [child.pid]
    try:
        while pending:
            for pid in pending:
                try:
                    os.kill(pid, signal.SIGSTOP)
                except ProcessLookupError:
                    pass
            children = process_children()
            pending = []
            frontier = list(owned)
            seen = set(owned)
            for parent in frontier:
                for pid in children.get(parent, []):
                    if pid not in seen:
                        seen.add(pid)
                        frontier.append(pid)
                        pending.append(pid)
            owned.extend(pending)
    finally:
        for pid in reversed(owned):
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        child.wait()


def wait_until(child, deadline):
    # Popen.wait adds a KeyboardInterrupt grace period; do not grant test work
    # extra runtime before cleanup on an interrupt.
    while child.poll() is None:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise subprocess.TimeoutExpired(child.args, 0)
        time.sleep(min(.005, remaining))
    return child.returncode


def enable_subreaper():
    # Linux additionally owns detached/orphaned descendants. Other supported
    # POSIX hosts retain the portable process-group deadline path.
    if not sys.platform.startswith('linux'):
        return False
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(36, 1, 0, 0, 0):  # PR_SET_CHILD_SUBREAPER
        raise OSError(ctypes.get_errno(), 'cannot own test descendants')
    return True


def inherited_group(environment):
    return environment.get(PROCESS_GROUP) == str(os.getpgrp())


def supervise(commands):
    subreaper = enable_subreaper()
    read_children = cleanup_reader(subreaper)
    os.environ[PROCESS_GROUP] = str(os.getpgrp())
    child = None
    deadline = absolute_deadline(LIMIT, os.environ)
    try:
        for command in commands:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return 124
            child = subprocess.Popen(command, cwd=ROOT)
            try:
                result = wait_until(child, deadline)
            except subprocess.TimeoutExpired:
                print('FAIL: test command exceeded its aggregate deadline', file=sys.stderr)
                return 124
            if time.monotonic() >= deadline:
                return 124
            if result:
                return result if result > 0 else 128 - result
        return 0
    finally:
        # The caller reaches the same deadline and sends SIGTERM as we start
        # cleanup. signal.signal runs a pending handler before it installs
        # the new one, so retry until discovery and polling are protected.
        while True:
            try:
                signal.signal(signal.SIGTERM, signal.SIG_IGN)
                signal.signal(signal.SIGINT, signal.SIG_IGN)
                break
            except (KeyboardInterrupt, TerminationRequested):
                pass
        if subreaper:
            cleanup_children(read_children)
        elif child is not None:
            cleanup_subtree(child)


def run_commands(commands, seconds=LIMIT, env=None, output=None):
    """One inherited absolute deadline and one isolated descendant owner."""
    environment = dict(os.environ if env is None else env)
    # A caller-supplied environment may not erase an enclosing command's limit.
    if DEADLINE in os.environ:
        environment[DEADLINE] = str(min(float(environment.get(DEADLINE, 'inf')),
                                      float(os.environ[DEADLINE])))
    deadline = absolute_deadline(seconds, environment)
    environment[DEADLINE] = str(deadline)
    started = time.monotonic()
    if deadline <= started:
        return 124
    owns_group = not inherited_group(environment)
    child = subprocess.Popen([sys.executable, str(Path(__file__).resolve()),
                              '_supervise', json.dumps(commands)], cwd=ROOT,
                             env=environment, start_new_session=owns_group,
                             stdout=output, stderr=output)
    try:
        try:
            result = wait_until(child, deadline)
        except subprocess.TimeoutExpired:
            return 124
        if time.monotonic() >= deadline:
            return 124
        return result if result >= 0 else 128 - result
    finally:
        if child.poll() is None:
            # Let the supervisor unwind first; Linux also reaps descendants
            # which escaped the portable process group.
            child.terminate()
        child.wait()
        if owns_group:
            # Also used on macOS: nested managed wrappers stay in this group.
            # Arbitrary descendants that call setsid need Linux subreaping.
            try:
                os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        print(f'Test command wall time: {time.monotonic() - started:.3f}s; ceiling {seconds}s; '
              'includes in-test fixture setup; external configuration/build excluded.', file=output or sys.stdout, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['inventory', 'disabled', 'unit', 'short', 'tool', 'group', 'admit', 'native', 'admit-env', 'native-env', 'agent-env', '_supervise'])
    parser.add_argument('arguments', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    arguments = args.arguments
    if arguments[:1] == ['--']:
        arguments = arguments[1:]
    if args.action == '_supervise':
        return supervise(json.loads(arguments[0]))
    print(NOTICE, flush=True)
    if args.action == 'inventory':
        for command, evidence in NATIVE.items():
            print('ADMITTED (60s ceiling):', ' '.join(command), '-', evidence)
        for name in TOOLS:
            print('ADMITTED (60s ceiling): python3 tools/test_policy.py tool', name)
        print('ADMITTED: make test / test-short (C execution-policy units, one deadline)')
        print('ADMITTED: make test-tools (all C tooling and process integrations, one deadline)')
        for name in GROUPS:
            print(f'ADMITTED (60s ceiling): python3 tools/test_policy.py group {name}')
        print('Retired script suites are absent. Other native commands remain quarantined; see docs/test-quarantine.md.')
        return 0
    if args.action == 'disabled':
        print('DISABLED:', ' '.join(arguments), file=sys.stderr)
        return 2
    if args.action in ('unit', 'short'):
        if len(arguments) != 1:
            parser.error('unit/short requires the native C tooling test binary')
        command = [str(Path(arguments[0]).resolve())]
        if args.action == 'unit':
            command.append('--unit')
        return run_commands([command])
    if args.action == 'group':
        if len(arguments) != 1 or arguments[0] not in GROUPS:
            parser.error('unknown test group')
        return run_commands([TOOLS[name] for name in GROUPS[arguments[0]]])
    if args.action == 'tool':
        if len(arguments) != 1 or arguments[0] not in TOOLS:
            parser.error('unadmitted tooling command')
        if arguments[0] in ('studio-source-edit',
                            'studio-graph-visibility', 'studio-graph-pointer', 'studio-asset-pointer', 'studio-debugger-source-history', 'studio-scene-move', 'studio-outliner-move', 'studio-scene-camera', 'studio-scene-gizmo3d', 'studio-scene-collider3d', 'interior-border', 'viewport-resize', 'sdf-visibility',
                            'radiance', 'radiance-depth', 'material-sampling', 'scene2d', 'material-terrain-seams', 'surface-transport', 'material-mesh-cache', 'scene3d-terrain-gpu', 'scene3d-volume-gpu', 'hrc-spot', 'scene3d',
                            'scene3d-gles', 'scene3d-gi', 'scene3d-gi-gles',
                            'material-ramps', 'radiance-surface', 'radiance-horizons4',
                            'radiance-horizons8', 'material-pixels', 'shader-parity',
                            'shader-programs', 'material-cameras', 'mesh-materials',
                            'sdf-quality', 'scene3d-post', 'scene3d-shapes', 'texture-transform'):
            return run_commands([[sys.executable, str(ROOT / 'tools/agent.py'),
                                  'gpu-test', '--', '--' + arguments[0]]])
        return run_commands([TOOLS[arguments[0]]])
    if args.action in ('admit-env', 'native-env', 'agent-env'):
        expected = 1 if args.action == 'admit-env' else 2
        if len(arguments) != expected:
            parser.error('invalid environment-selector invocation')
        name = 'AGENT_SELECTOR' if args.action == 'agent-env' else arguments[0]
        selectors = selector_arguments(name)
        if not admitted(selectors):
            print('DISABLED: unadmitted native invocation: ' + ' '.join(selectors), file=sys.stderr)
            return 2
        if args.action == 'admit-env':
            return 0
        if args.action == 'agent-env':
            if arguments[1] not in ('test', 'gpu-test'):
                parser.error('invalid agent test mode')
            return run_commands([[sys.executable, str(ROOT / 'tools/agent.py'),
                                  '--build-dir', arguments[0], arguments[1], '--', *selectors]])
        return run_commands([[arguments[1], *selectors]])
    selectors = arguments[1:] if args.action == 'native' else arguments
    if not admitted(selectors):
        print('DISABLED: unadmitted native invocation: ' + ' '.join(selectors), file=sys.stderr)
        return 2
    if args.action == 'admit':
        return 0
    return run_commands([arguments])


class TerminationRequested(BaseException):
    pass


def terminate(_signal, _frame):
    raise TerminationRequested()


if __name__ == '__main__':
    signal.signal(signal.SIGTERM, terminate)
    signal.signal(signal.SIGINT, terminate)
    try:
        sys.exit(main())
    except (KeyboardInterrupt, TerminationRequested):
        sys.exit(130)
    except (OSError, ValueError, RuntimeError) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        sys.exit(2)
