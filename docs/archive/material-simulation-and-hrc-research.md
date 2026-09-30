# Material simulation and HRC research

Research date: September 19, 2026. This note records the initial investigation.
The subsequent [gas-flow implementation](neon-gas-flow.md) and
[performance results](neon-material-performance.md#persistent-flow-and-hrc-follow-up)
describe what was adopted and measured.

## Recommendation

Build a persistent 2D velocity field for gas motion first. Use pressure projection
and controlled vorticity confinement, with fine particles or density carried by
that flow. Reduce the current velocity-based render stretching. This directly
addresses the missing billows and wakes. Under zero gravity, motion should come
from bodies, exhaust, combustion expansion and authored disturbances; upward
buoyancy must not be an unconditional smoke behavior.

MPM is worth a bounded prototype for liquids and deformable materials. It is a
reusable numerical foundation, but it is not an established performance win for
this engine. Keep Box2D for rigid bodies and the editable terrain field for cave
geometry. Adopt a replacement only after its behavior and complete frame cost
beat the current implementation at the intended quality.

Prioritize HRC measurements and small optimizations alongside the gas upgrade.
Investigate sparse optical data before replacing the complete cascade layout.

## What our measurements establish

The last recorded release benchmark on an Apple M4 Pro, with adaptation disabled:

| Output | HRC scale | Frame median | Frame p95 | Simulation mean | Render/present mean |
| --- | --- | --- | --- | --- | --- |
| 1920×1080 | 0.5 | 14.31 ms | 15.21 ms | 2.78 ms | 11.45 ms |
| 1920×1080 | 1.0 | 37.77 ms | 39.22 ms | 2.83 ms | 34.96 ms |

These are existing results, re-read for this investigation. Render/present is
host elapsed time including GPU waiting, not an isolated GPU-pass measurement.
The scale comparison makes rendering the first performance target. It does not
identify the cost of an individual HRC shader. These are not Steam Deck results.
See [the benchmark method and artifacts](neon-material-performance.md).

## MPM: performance, quality and reuse

Our current [particle solver](../src/runtime/material_fluid.c) uses double-density
relaxation for liquids and compressed-neighbor repulsion for gases. The spatial
hash finds neighbors; it is not a grid on which fluid velocity is solved.

The [MLS-MPM reference code](https://github.com/yuanming-hu/taichi_mpm/blob/3a680ab0fdc6e95077f08a91bc9079e1ef41478e/mls-mpm88-explained.cpp)
transfers each 2D particle to a fixed 3×3 grid stencil and back. This can avoid the
growing neighbor work of crowded particle neighborhoods. It also clears and
updates a grid, evaluates material stress, and updates deformation. Its small
demonstration timestep is not a promise of one step per game frame.

Our inference: a well-organized MPM implementation could scale better for dense
material regions, but stiffness, stability substeps, grid size, transfer contention
and body coupling can consume that gain. Moving simulation to the GPU also shares
resources with HRC and requires a deliberate path for returning Box2D impulses.
Compare complete frames, including synchronization, at comparable behavior.

[APIC](https://doi.org/10.1145/2766996) retains local rotation and shear during
particle/grid transfers and conserves angular momentum across those transfers.
It is a transfer scheme, not a complete fluid solver. It can improve the lively
motion of particle/grid fluids without requiring every material to use MPM.

[MLS-MPM with CPIC](https://yuanming.taichi.graphics/publication/2018-mlsmpm/)
is particularly relevant to the longer-term plan: the work includes cutting and
two-way rigid-body coupling. Those are extensions to implement, not behavior
provided by attaching an MPM label to our current particles.

[Augmented MPM](https://www.disneyanimation.com/publications/augmented-mpm-for-phase-change-and-varied-materials/)
demonstrates heat transport, melting and solidification. It needs additional
material and thermal solves. Its authors also explicitly discuss the efficiency
advantage a specialized solver can have when only one material is needed.

For this engine, reuse should mean one optional material capability with authored
properties and narrow APIs for sources, collision geometry, body impulses and
render data. Gas, liquids and soft solids can use different numerical treatments
inside that capability. Terrain, enemy death rules, ignition rules, surface
tension and rendering remain explicit responsibilities. Validate reuse with a
second scene without NeonSwarm combat. Avoid a generic solver/plugin framework
before there is a concrete need, and retire replaced code after adoption.

[Bridson's smoke notes](https://www.cs.ubc.ca/~rbridson/fluidsimulation/2006/fluids_notes.pdf)
explain pressure-based flow, the loss of detail through numerical advection,
vorticity confinement and sharper interpolation. These are a closer match for
our immediate smoke problem than solid stress models. More particles alone
cannot recover motion missing from the velocity solution.

## HRC work in priority order

These are proposed experiments. Savings and visual acceptance remain unmeasured.

1. Obtain GPU timings for material/fluid raster, prefilter, trace, extension,
   merge, gather, blur and composite. Use a native GPU capture if the graphics
   abstraction does not expose timestamps. Keep HRC adaptation disabled and run
   both 0.5 and 1.0 scale. CPU Tracy zones remain useful for simulation and
   submission work.
2. Specialize the gather path when canvas and work dimensions match. The
   [current shader](../src/render/shaders/hrc/gather.shd) still expresses four
   neighbor loads per quadrant and edge interpolation even though the bilinear
   fraction is zero at scale 1. An explicit path can remove those unnecessary
   source-level operations; actual compiled savings need measurement.
3. Compare `trace_levels = 1` against the current 3. This seeds the shortest
   intervals directly and begins ray extension earlier. It may reduce tracing
   and stabilize moving tiny lights through greater angular smoothing. It can
   also soften detail. [Yaazarai's implementation](https://github.com/Yaazarai/Volumetric-HRC)
   reports this tradeoff; it is not evidence of a win on our backend.
4. Inspect memory traffic and workgroup layout in the measured expensive stages.
   Quadrant scratch reuse and the duplicate even-direction extension optimization
   are already implemented. Our [merge output](../src/render/shaders/hrc/merge.shd)
   stores RGB plus a constant fourth channel in eight bytes. A smaller HDR fluence
   format is an experiment distinct from compressing intervals, which also need
   optical depth. Validate dark gradients and saturated lights before adopting
   lower precision.
5. Improve reconstruction where coarse lighting loses visibility information.
   Full-resolution material raster and short local visibility tracing already
   exist. Their current gather traces at most two additional samples per
   quadrant. Explore stronger visibility tests only near difficult boundaries,
   with a bounded fallback cost. Merely interpolating more carefully cannot
   recover a distant shadow that the coarse representation never captured.
6. Cache terrain optical/distance tiles and rebuild edited regions plus their
   sampling borders. Composite dynamic bodies and volumes separately. Measure
   the raster savings and cache maintenance before extending this to lighting.

The [HRC paper](https://arxiv.org/abs/2505.02041), sections 4.3 and 6, identifies
the interval acceleration structure as the major memory cost and discusses
compression and alternative tracing structures. Dense HRC deliberately avoids
dependence on empty-space traversal, which is valuable in scenes filled with fog.
Replacing its interval storage trades predictable work for scene-dependent
traversal; it is not free sparsity.

## What sparse lighting would mean here

Sparse material storage, sparse ray traversal and sparse lighting probes are
different changes. For our caves, small active tiles are a reasonable first
experiment. A tile can describe empty space, opaque interior or a varying medium,
allowing specialized work where that classification is conservative.

Empty space can carry light. An empty interval has zero emitted radiance and
unit transmittance; its far radiance still has to propagate. Darkness is not an
occupancy test, and a moving light can alter a distant shadow without changing
local terrain. Cached final lighting therefore needs dependency invalidation,
camera reprojection and changed-material handling beyond dirty terrain tiles.

Surface-only probes cannot supply all fog and liquid lighting. A volume-aware
sparse implementation needs samples wherever those media require illumination,
plus the dependencies needed to transport incident light. Widely distributed fog
may leave little work to remove. Classification, compaction and indirection also
cost time on an integrated GPU.

Two concrete sources help establish the tradeoffs:

- [Sparse RC 2D](https://github.com/IceKontroI/sparse_rc_2d) offers air-filled and
  surface-edge probe modes. It is conventional RC, not HRC. Its README records
  partially occupied workgroups, compressed-color limitations and remaining
  dense textures. Treat it as a data-structure experiment to study.
- [Split Radiance Cascades](https://arxiv.org/abs/2607.20384) and its
  [Chandra implementation](https://github.com/entropylost/chandra) use sparse
  world-space probes and ray splitting for 3D diffuse GI. Its reported timings
  exclude G-buffer creation and shadow rays. It is relevant research, not a
  demonstrated replacement for our 2D direct and volumetric HRC lighting.

## Quality and other references

Our gather averages the four quadrant contributions into one RGB result. The
composite then applies material color, edge-light sampling and artistic volume
exposure. Preserving some incident-light direction could improve surface and
volume shading, but needs a concrete model of the apparent depth of our 2D scene.
It would not turn the current representation into full 3D volumetric transport.

The lower-resolution color/full-resolution luminance proposal remains worth an
experiment if profiling identifies bandwidth pressure. Producing full-resolution
luminance still requires a lighting solution; adding a coarse RGB solve also has
cost. Test color boundaries and moving occluders. The final reconstruction multiply
is inexpensive, but cannot create missing visibility information.

[Amitabha](https://github.com/entropylost/amitabha) is the HRC authors' reference
implementation and is useful for checking integration and merging correctness.
[Raikiri's LegitCascades](https://github.com/Raikiri/LegitCascades) includes
polygonal and energy-conserving HRC experiments. The inspected polygonal scripts
are basis/propagation experiments; their existence does not establish a complete,
faster production renderer for our volumes.

## Acceptance for the next experiments

- Gas: persistent wakes, rolling clouds, bounded compression and stable density
  around moving ship geometry. Preserve the engine trail's approved appearance.
- MPM prototype: two zero-gravity blobs split and rejoin, one soft body deforms,
  and the same rigid-body interaction works in a small independent scene. Compare
  representative particle counts, substeps, memory and complete frame p95.
- Lighting: thin cave walls, moving one-pixel emitters, colored crossing lights,
  fog filling most of the view, liquid boundaries, destruction and camera motion.
  Compare against the current full-scale output and analytic fixtures where
  possible; full-scale HRC itself is not mathematical ground truth.
- Performance: repeat the deterministic 1080p and 1280×800 scenario at fixed
  settings. Record time and quality separately. Require actual Deck validation
  before claiming the integrated-GPU target is met.

Source revisions inspected: taichi_mpm `3a680ab0fdc6`, amitabha `38b7ee236480`,
sparse_rc_2d `7ea6ec32ecc4`, chandra `0d46002d310a`, LegitCascades `82993030166b`.
The HRC and Split RC PDFs were read locally after the web reader rejected their
file sizes. Research artifacts are under `/tmp/mpm-hrc-research`.
