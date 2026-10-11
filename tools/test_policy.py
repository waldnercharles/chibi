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
    ('--studio-scene-collider3d',): 'First supervised measurement: independently reviewed three in-memory prototypes/actors, queued SDL scaled-box face, sphere radius and capsule height edits, typed undo/redo, parallel-axis/nonfinite refusal and move-face Escape/history restoration, hidden-handle refusal and H-during-drag cancellation; twenty-six hidden1920x1080frames fixed1/60, disposable3file fixture, shared60s',
    ('--studio-scene-gizmo3d',): 'First supervised measurement: independently reviewed six in-memory prototypes/eight actors, queued SDL world/local movement and snapping, orientation and mirrored-child rotation, local scaling, undo/redo, Escape/focus-loss/hidden-panel cancellation, shear/singular-parent refusal; forty-two hidden1920x1080frames fixed1/60, disposable3file fixture, shared60s',
    ('--studio-scene-camera',): 'First supervised measurement: independently reviewed one in-memory cube, SDL orbit/pan/anchored dolly, same-frame RMB+W flight, fractional diagonal wheel pan, above-horizon press and checkpoint lens retention, focus-loss cleanup; thirty hidden1920x1080frames, disposable3file fixture, shared60s',
    ('--preview-components',): 'Independently reviewed first CPU measurement: four tiny project files, one declared float component and entity, two previews with two steps each, missing-scene and invalid-declaration cleanup, no GPU/process/network; shared60s',
    ('--studio-scene-move',): 'Reviewed installed SDK hierarchy move integration: bounded 2D picking and two-object 3D model/checkpoint cases, atomic moves, history and collaboration; two hidden frames, shared60s',
    ('--studio-outliner-move',): 'Reviewed installed Studio pointer and keyboard hierarchy moves: one three-object 2D scene, before/inside/after/root, grouped selection and rejection; twenty-one hidden frames, shared60s',
    ('--tilemap-project',): 'Reviewed first CPU measurement: fixed sibling Celeste project, pinned local git baseline56ca9d48<=64KiB, Room<=16KiB, Level<=4KiB,1472cells230oldtiles/fiveactors, three owned controller children<=10s each including two headless simulation frames<=15entities, no GPU/network; shared60s',
    ('--tilemap',): 'Independently reviewed eight fixed CPU grid/cache/point/SAT/bounds/native-binding/prototype cases, at most four parsed cells and three created ECS entities, two <=4KiB KDL texts, three 64x16 PNG preparations and one rejected 128x128 map, rejected overflow dimensions, no engine frames/GPU/process/network; shared60s',
    ('--web-storage',): 'First supervised measurement; independently reviewed thirteen C-owned browser storage and platform cases, tiny in-memory FS and ZIP fixtures executed under Node, no network or GPU, shared60s',
    ('--webgpu-staging',): 'Reviewed C-owned isolated headless Chrome: one64x64quad for120 frames, peak64MiB staging/reuse, two pending-readback ownership checks, allocator restore before callback drain, one app recreation with stale loss, one validation error and real device loss; callback phases<=1s within startup+execution55s/shared60s; docs/testing.md',
    ('--studio-graph-visibility',): 'Reviewed actual installed Studio graph visibility, scene draft history and tilemap viewport bounds/layer picking; two temporary entities and four CPU picks, disposable three-file fixture and isolated prefs, ten hidden 1920x1080 frames at fixed 1/60, shared60s',
    ('--studio-graph-pointer',): 'First supervised measurement: independently reviewed Counter node pointer selection, wire keys, Find result keys/click/focus, nineteen hidden1920x1080frames, disposable3file fixture, shared60s',
    ('--studio-asset-pointer',): 'First supervised measurement: independently reviewed four tiny prototypes, SDL filtered list/grid selection and modifiers, twenty hidden1920x1080frames, disposable3file fixture, shared60s',
    ('--studio-debugger-source-history',): 'First supervised measurement: independently reviewed one in-memory Counter actor, SDL breakpoint/play/invalid text/undo/redo/step/stop, retained cursor and runtime value assertions, forty-seven hidden1920x1080frames, at most ten source-level steps, shared60s',
    ('--studio-source-edit',): 'Reviewed actual installed Studio edit/undo/redo, disposable3file212byteproject+isolatedprefs, fourhidden1920x1080frames, shared60s',
    ('--compile-installed-space-cadet',): 'Pinned installed SpaceCadet fixed36files/144741bytes compiler-only, exact8resource scope, shared60seconds',
    ('--player-installed-space-cadet-vm',): 'Pinned installed player VM, four hidden1280x720frames, exact8resources, shared60seconds',
    ('--player-installed-space-cadet-sljit',): 'Pinned installed player SLJIT, four hidden1280x720frames, exact8resources, shared60seconds',
    ('--interior-border',): 'Reviewed hidden 64x64 app: Cute interior border on a circle and a diamond, opaque and translucent, instanced and tiled draw paths with readback; GPU; docs/testing.md',
    ('--viewport-resize',): 'Reviewed hidden 64x64 app: viewport initial/later resize behavior, captures presses above a perspective horizon without a ground point, plus one 64x64 UI frame/readback checking translucent rectangle and glyph source-over against the public straight-alpha texture; GPU, shared 60s; docs/testing.md',
    ('--sdf-visibility',): 'Reviewed hidden 96x80 app: mesh and SDF occlusion with hidden-surface rejection on and off, depth snapshot copies, readback; GPU; docs/testing.md',
    ('--radiance',): 'Reviewed hidden 128x128 app: HRC emitter, wall, absorption, bounce and transparent-feedback transport, reset, resize and reused capacity, split color, fractional cells, reduced reconstruction, fitted lighting, every resolution combination, the adaptive ladder, GPU pass timing, and the extend and filter passes alone, with readback; GPU; docs/testing.md',
    ('--radiance-depth',): 'Reviewed hidden 32x32 app with 32x32 and 128x128 targets: HRC Depth thickness light through slabs that block by height unless they glow, from small, moving, edge and mixed-color sources, its falloff, perspective, depth gain on closed receivers, closed receivers taking the light HRC brings open ones, open receivers shading HRC light by facing and height, each light as much as HRC brings it even alone in reach, soft visibility keeping lights on the floor plane lit, gloss, every quality, history, segments and steep planes, and the surface gather by quadrant, crest, reach and turning view, with readback; GPU; docs/testing.md',
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
    ('--shader-parameters-prepared',): 'Independently reviewed two CPU cases: tiny material/two-parameter codec and generation rejection, plus three-node parameterized model/four prepared records<64KiB staged within2MiB; no GPU; docs/testing.md',
    ('--source-systems',): 'First supervised measurement; independently reviewed eleven pico source system cases on VM/SLJIT, fixed pumps and at most 259 entities, CPU only; docs/testing.md',
    ('--new-asset',): 'One new-asset scaffolding case: one temporary content folder, three written assets and six refusals, CPU only; docs/testing.md',
    ('--asset-refactor',): 'One asset rename case: four in-memory prototypes, planned rewrites of a nested child and of a child that an including prototype adds, no files, CPU only; docs/testing.md',
    ('--source-edit',): 'Independently reviewed nine fixed source-edit cases, tiny inputs and checked transactions including extraction/inline/signature/binding/capture and layout, no files/world/runtime/GPU; docs/testing.md',
    ('--ecs-query-benchmark',): 'Reviewed three fixed ECS query workloads, 4096 entities x 2000 passes each, CPU only; run through benchmark_trials',
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
    ('--prototype-documents',): 'Prototype and scene documents with retained components; 8 in-memory cases, CPU only; docs/testing.md',
    ('--reference-index',): 'Reference index over prototypes, scenes and UI pages; 5 cases, CPU only; docs/testing.md',
    ('--asset-source',): 'Asset source edits, completion and hover over reflected components; 6 cases, CPU only; docs/testing.md',
    ('--behavior-loader',): 'All-or-nothing behavior loading and failed preview launches; 2 cases, CPU only; docs/testing.md',
    ('--engine-clock',): 'Engine clock, pause, step and headless engine frames; 6 cases, CPU only; docs/testing.md',
    ('--preview-layout',): 'Preview world layout; 2 cases, CPU only; docs/testing.md',
    ('--fields',): 'Field graphs, sampling, cave fixtures, source-library linking/defaults and publication rollback with retained drafts and name-stable source calls; 18 cases, CPU only; docs/testing.md',
    ('--model-graphs',): 'Model graphs compiled to distance programs, bounds, hit resolvers and sampled SDFs; 39 cases, in-memory, CPU only; docs/testing.md',
    ('--model-catalog',): 'Model parameters, material scope, validation, materials and surfaces in the shader catalog; 29 cases including live model registration, diagnostics and rename, CPU only; docs/testing.md',
    ('--render-geometry',): 'Appearance and mesh surface geometry, including finite scaled relief; 8 cases, CPU only; docs/testing.md',
    ('--arena',): 'The support arena allocator; 2 cases, CPU only; docs/testing.md',
    ('--shader-graphs',): 'Shader graphs evaluated on the CPU and compiled to GLSL; 15 cases, CPU only; docs/testing.md',
    ('--graph-document',): 'Graph documents over the composed node palette: comments, frames, edge indexes across edits and paste, concurrent readers, ID stability and literals; 14 cases, in-memory, CPU only; docs/testing.md',
    ('--graph-presentation',): 'Inline folding, arrangement, expression text, find and hover sentences over the palette; 14 cases, in-memory, CPU only; docs/testing.md',
    ('--graph-functions',): 'Function and macro signatures, collapse/expand and linked shader library problems; 18 cases, in-memory, CPU only; docs/testing.md',
    ('--vector-nodes',): 'Vector kernels and the palette vector node bindings and defaults; one case, CPU only; docs/testing.md',
    ('--source-presentation',): 'Independently reviewed five small source/palette cases, graph ownership/layout, invalid drafts, callable-local scope and readable formulas with retained calls/effects, plus two component presentation cases on two in-memory documents, CPU only; docs/testing.md',
}
EXAMPLE_LABS = ('local_mesh_sampled', 'hrc_lab', 'modeling_lab', 'sdf_lab', 'suzanne_sampled')
for lab in EXAMPLE_LABS:
    NATIVE[(f'--installed-example-{lab}',)] = (
        'Independently reviewed first measurement: one pinned lab tree<=512files/32MiB, '
        'verified installed package pins, one compile<=20s, source removal/relocation, '
        'four hidden320x180frames/readback<=25s; sampled box also missing-input compile<=10s and two CLI rejections<=5s each; C PNG landmarks, shared60s')
TOOLS = {
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
    'tilemap-project': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--tilemap-project'],
    'tilemap': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--tilemap'],
    'studio-graph-visibility': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-graph-visibility'],
    'studio-graph-pointer': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-graph-pointer'],
    'studio-asset-pointer': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-asset-pointer'],
    'studio-debugger-source-history': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-debugger-source-history'],
    'studio-source-edit': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--studio-source-edit'],
    'compile-installed-space-cadet': [sys.executable, str(ROOT / 'tools/agent.py'), 'test', '--', '--compile-installed-space-cadet'],
    'player-installed-space-cadet-vm': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--player-installed-space-cadet-vm'],
    'player-installed-space-cadet-sljit': [sys.executable, str(ROOT / 'tools/agent.py'), 'gpu-test', '--', '--player-installed-space-cadet-sljit'],
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
    # The validate_project cases, each a scaffolded temporary project and a few runs of an
    # unsanitized chibi_ctl, since sanitizers make its starts too slow for one deadline.
    'validate-project': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--validate-project',
                         str(ROOT / os.environ.get('CHIBI_CTL', 'build-release/bin/chibi_ctl'))],
    # Two validate_project cases whose projects read past the shared prototype expansion
    # budget, against the same unsanitized chibi_ctl.
    'prototype-expansion': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--prototype-expansion',
                            str(ROOT / os.environ.get('CHIBI_CTL', 'build-release/bin/chibi_ctl'))],
    'shader-parameters-prepared': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--shader-parameters-prepared'],
    # Reviewed fixed ECS query workloads; compare builds through benchmark_trials.
    'ecs-query-benchmark': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--ecs-query-benchmark'],
    # Two reviewed arena cases: aligned growth, block reuse after reset and a zero-initialized arena.
    'arena': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--arena'],
    # Reviewed four fixed scheduling cases, including verified source cohorts and mixed phase membership.
    'system-schedule': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--system-schedule'],
    # Same admitted eleven fixed source-system cases through installed public consumer, VM and SLJIT.
    'source-systems': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--source-systems'],
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
    # Four reviewed hidden-app cases: initial/later viewport resizing and one 64x64 UI frame
    # with one readback checking source-over plus public straight-alpha texture semantics.
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
    # Nine independently reviewed bounded snapshot hierarchy/transform cases and an in-memory
    # five-entity scene codec round trip; at most 257 nodes, no files, process or GPU.
    'scene-hierarchy': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--scene-hierarchy'],
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
    'language-format': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                        '--format', '--corpus',
                        str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'corpus.txt')],
    # Reviewed finite producer process and runtime-only consumer with one shared deadline.
    'language-prepared': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                          '--prepared',
                          str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language')],
    # Reviewed public module consumer; finite ECS/behavior lifecycle on VM and SLJIT.
    'language-headless': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                         '--headless'],
    # Reviewed CPU-only scalar benchmark: VM/SLJIT x inline/helper, five 200-dispatch trials
    # each with 100 loop iterations, four warmups, no frames/files/GPU; shared60s.
    'script-benchmark': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                         '--script-benchmark'],
    # Reviewed source-to-runtime event/system/binding cases with finite supplied-time steps.
    'language-program': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                        '--program'],
    # Reviewed finite public builder/VM/native contracts and verifier rejection cases.
    'language-execution': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                           '--execution'],
    # Reviewed finite semantic contracts with inline sources and an immutable catalog.
    'language-semantic': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                          '--semantic'],
    # Reviewed finite C syntax cases and a corpus capped at 256 files/8 MiB.
    'language': [str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'test_language'),
                 '--syntax', '--corpus', str(ROOT / os.environ.get('CHIBI_LANGUAGE_BUILD_DIR', 'build/test/language') / 'corpus.txt')],
    'web-storage': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--web-storage'],
    'webgpu-staging': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'), '--webgpu-staging'],
    'native-tools': [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools')],

}
# Every admitted selector of each built driver, run under one shared deadline.
for lab in EXAMPLE_LABS:
    TOOLS[f'installed-example-{lab}'] = [str(ROOT / os.environ.get('CHIBI_TOOLS_BUILD_DIR', 'build/test/tools') / 'test_tools'),
                                        f'--installed-example-{lab}']

GROUPS = {
    'native': ['native-runtime', 'native-catalog', 'native-values', 'physics', 'physics3d',
               'transforms', 'box3d', 'sky-files', 'tilemap', 'runtime-scene', 'scene-prepared',
               'scene-hierarchy', 'render-resources', 'persistent-store', 'whole-alias',
               'whole-integer', 'whole-conversion', 'whole-select', 'whole-math', 'owned-tick',
               'parallel-tick', 'source-native-features', 'source-native-numeric-queries',
               'source-native-material', 'source-presentation', 'system-schedule',
               'source-systems', 'source-edit', 'new-asset', 'asset-refactor',
               'shader-parameters-prepared', 'language-prepared', 'ecs-query-benchmark',
               'preview-components', 'graph-document', 'graph-presentation', 'graph-functions',
               'vector-nodes', 'asset-values', 'ui-document', 'text-files', 'fmt', 'particles',
               'png-encoding', 'project-document', 'prototype-documents', 'reference-index',
               'asset-source', 'behavior-loader', 'engine-clock', 'preview-layout', 'fields',
               'model-graphs', 'model-catalog', 'render-geometry', 'arena', 'shader-graphs'],
    # Whole projects, imported assets and the Release chibi_ctl: feature coverage,
    # project validation and prototype expansion, mesh and model import, and the
    # controller and render fixture command-response cases.
    'projects': ['mesh-files', 'model-import', 'validate-project', 'prototype-expansion',
                 'controller', 'feature-coverage', 'render-fixture'],
    # Independently reviewed fixed CPU workloads: cave<=2048x768, flow<=192x192,
    # two192-particle3000-stepMPM pools, four workers, no windows/children/network.
    # First measurement of the combined candidate remains under one shared60s.
    'materials': ['material-destruction', 'material-fluids', 'material-mpm', 'material-thermal',
                  'material-simulation', 'material-ecs', 'material-render', 'material-benchmark',
                  'contact-benchmark'],
    'language': ['language', 'language-semantic', 'language-execution', 'language-program', 'script-benchmark',
                 'language-headless', 'language-format', 'language-navigation', 'language-publication',
                 'field-runtime', 'prototype-document', 'ui-authoring', 'text-format', 'graph-model',
                 'graph-authoring', 'model-runtime'],
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
        if arguments[0] in tuple(f'installed-example-{lab}' for lab in EXAMPLE_LABS) + ('player-installed-space-cadet-vm', 'player-installed-space-cadet-sljit', 'studio-source-edit',
                            'studio-graph-visibility', 'studio-graph-pointer', 'studio-asset-pointer', 'studio-debugger-source-history', 'studio-scene-move', 'studio-outliner-move', 'studio-scene-camera', 'studio-scene-gizmo3d', 'studio-scene-collider3d', 'interior-border', 'viewport-resize', 'sdf-visibility',
                            'radiance', 'radiance-depth', 'material-sampling', 'scene2d', 'scene3d',
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
