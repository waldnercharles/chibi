# Sparse directional probes and HRC performance

Research checked September 26, 2026. This is an assessment of the current dirty
working tree, not a renderer replacement. The two supplied sketches are treated
as proposed layouts. New numerical results below are CPU algebra checks and
allocation/dependency counts, not GPU speed measurements.

## Recommendation

Keep the regular optical scene and the current receiver reconstruction. Evaluate
a sheared, direction-indexed interval structure behind the ray-query boundary.
This separates the useful part of the proposal from a simultaneous change to
probe placement, interpolation, sparse allocation and final shading.

There are three distinct proposals:

1. **Sparse interval storage:** omit certified empty optical segments, while
   retaining enough information to transmit light from farther away.
2. **Sparse receiver/merge work:** evaluate only the radiance probes needed by
   surfaces, participating media and their complete merge dependencies.
3. **Directional alignment:** arrange a particular ray family's samples on the
   same lines so interval composition can use a tree or scan.

They can coexist, but none implies the other two. The supplied parent rule is
correct for a binary tree's actual children. Geometric nearest neighbors are
only sufficient when they are the complete dependency stencil. Matching sparse
vanilla RC memory remains a hypothesis, and needs a specified angular schedule,
receiver set, precision, metadata layout and distribution of occupied cells.

The immediate opportunities are smaller than a new GI algorithm: compact known
unused short intervals, reduce expensive offscreen material storage, and test
transport sizes that avoid the 1,024-pixel cascade threshold. A sparse directional
accelerator is the larger research candidate. None has a new GPU speedup claim
in this report.

## What is expensive now

The authored Sandbox retains RGB transport scale 0.5, reconstruction scale 1.0,
three directly traced cascades, a 64-world-unit margin, and no independent
luminance solve. It is not a uniformly half-resolution renderer. Material
rasterization and final composition remain full resolution. See
[the current measurements](../neon-hrc-optimization.md#feature-costs-and-follow-up-experiments-september-26)
and [authored settings](../../projects/arena2d/content/prototypes/HRC.chibi).

| Existing measurement | Result | Interpretation |
| --- | ---: | --- |
| Frozen Sandbox, margin 0 / 64 | 14.11 / 19.68 ms median | Margin costs 5.57 ms in this fixture; removing it also removes offscreen light |
| Frozen, reconstruction 1 / 0.5 | 19.87 / 18.02 ms median | Retained optional reduction, with local shadow/pinhole differences |
| Moving camera and firing, reconstruction 1 / 0.5 | 24.01 / 22.13 ms median | Includes simulation; neither establishes sustained 60 FPS |
| Synchronized pass probe, margin 64 | Merge 4.69, material raster 3.13, extension 3.05, trace 2.43 ms | Identifies expensive regions; these are not GPU timestamps and cannot be added to ordinary frame time |

These are previously recorded matched runs, not reruns during this research.
Current timing code calls `cf_gpu_sync` at every marker. Detailed GPU counters
remain unavailable on this host, so bandwidth, occupancy and encoder overhead
are hypotheses, not measured bottleneck classifications. Source:
[gpu_timing.c](../../src/render/gpu_timing.c).

The interval hierarchy is large. For the default 1094 x 674 transport grid, a
fresh RGB allocation requests 83.48 MiB for optical intervals and 105.26 MiB for
intervals, merge scratch and four output frusta combined. This excludes material
images, histories, weights, allocator overhead and capacity retained after a
resize. Four quadrants already reuse interval/scratch storage. RGB fluence is
already four bytes; RGB optical intervals are eight bytes and scalar intervals
four. Source: [radiance.c](../../src/render/radiance.c), `s_create_work_resources`.

For one orientation, the actual element counts are

```text
T_n = ceil(W / 2^n) * H * (2^n + 1)
R_n = ceil(W / 2^n) * H * 2^n
```

Away from boundary rounding, each HRC level therefore retains approximately
`W*H` angular samples. Higher levels do not become dramatically smaller. The
default 11 levels and four quadrants issue 12 trace, 32 extension and 44 merge
dispatches, before prefilter, gather and postprocessing. Source:
[s_solve_trace/extend/merge](../../src/render/radiance.c).

The HRC paper already separates its fluence hierarchy from its interval
accelerator, identifies the latter as the main memory cost, and allows another
tracer in its place. Its construction directly traces the first three levels.
Dense volume support is one reason to retain this implementation as a control.
Source: [HRC paper, sections 4.1–4.3 and 6](https://arxiv.org/pdf/2505.02041).

## Can a regular grid be converted into a sheared grid?

Yes. A coordinate transformation is straightforward. What must be specified is
which quantity is being transformed.

| Input | Conversion | What it preserves |
| --- | --- | --- |
| Raw geometry or optical coefficients | Re-rasterize/integrate segments in sheared coordinates | Can preserve geometry to the chosen integration accuracy |
| Short, unmerged optical intervals with matching endpoints/directions | Reindex and compose | Exact apart from arithmetic/storage precision when segments coincide |
| Sampled intervals at different origins or directions | Resample/retrace | Approximate; boundaries and visibility require special care |
| Already gathered irradiance or averaged probe radiance | Interpolate a lighting estimate | Cannot reconstruct missing occlusion or arbitrary segment transport |

An invertible continuous shear does not make discrete resampling lossless.
For example, a completely dark empty scene and a completely dark scene containing
a wall can have the same gathered radiance and different visibility. No
conversion of those radiance values alone can recover the wall.

Even optical intervals lose correlations when interpolated before composition.
In two neighboring ray lanes, put a near blocker in lane A and a far light
behind it. Lane B is open nearby and has no far light. Both complete paths return
zero radiance. Averaging the near intervals and far intervals separately creates
50% near transmission and 50% far radiance; composing them returns 25% radiance.
This is a reproducible counterexample, not a prediction about every resampling
scheme. It motivates preserving complete path correspondence or tracing from
the original optical data. See `resampling_counterexample` in the model script.

For the proposed hybrid, the regular grid remains the scene/receiver layout.
The directional structure stores segment radiance and transmittance, or optical
depth, rather than final irradiance. A query returns the interval between two
positions on a supported ray. Gather, surface shading and history can continue
to consume regular outputs. This query is an optical transport query; obtaining
the first geometric collision also requires hit-location/material information.

A helper that only accelerates our short DDA traces addresses the Trace pass.
It leaves the larger extension and merge workload intact. A more substantial
version would answer the longer `T_n` queries during merging, replacing part of
the stored interval hierarchy. That trades saved T-buffer traffic and extension
dispatches for tree construction and repeated queries. Keeping R dense also
leaves its merge count unchanged. Measure those alternatives separately.

## Alignment and spacing

For a unit direction `d=(cos(theta), sin(theta))`, choose the axis with the larger
absolute component. In the positive-x-dominant case, use

```text
u = x
v = y - m*x,                 m = d_y / d_x
x = u
y = v + m*u
```

A ray parallel to `d` keeps `v` constant. With grid pitch `h`, increasing the
along-ray index advances by `(h, m*h)` and increasing the row index by `(0,h)`.
Swap axes for y-dominant directions and handle signs consistently. This avoids
division by a component near zero.

Our geometric derivation gives:

```text
cell area                  = h^2
distance along ray/sample  = h / max(abs(cos(theta)), abs(sin(theta)))
perpendicular row spacing  = h * max(abs(cos(theta)), abs(sin(theta)))
```

If a constant perpendicular pitch `p` is desired, choose
`h = p / max(abs(cos(theta)), abs(sin(theta)))`. That is a different sampling
policy from preserving a fixed cell area.

For cascades, separate plane pitch `P` from ray-intercept pitch `Q`:

```text
x = i*P
y = j*Q + m*i*P
cell area = P*Q
perpendicular line spacing = Q*abs(cos(theta))   // x-dominant sector
```

Our current HRC already has aligned rays for each fixed direction. At level
`n`, let `S=2^n`, `offset=2*k-S`, and `m=offset/S`. The ray intercept is
`b=y-m*x`; at plane `i`, its row is `y=b+i*offset`. Current storage includes
every integer row, with `P=S` and `Q=1`. Reindexing these rows by `b` is a change
of layout. Reducing the stored intercepts is the actual sparsification.
Source: [trace.shd](../../src/render/shaders/hrc/trace.shd).

This alignment holds within one direction family. Between cascades, newly
introduced odd directions still lack exact same-direction children in the
lower level. The current extension approximates them with two bent paths.
Changing coordinates alone does not remove that angular approximation. With
`Q>1`, even an existing endpoint can land between stored rows; define its lookup
or retrace policy rather than rounding silently.

Keep the shear origin and phase anchored to world coordinates. A continuously
moving screen-relative lattice would reintroduce the sampling changes behind
the original flicker concern. Changes to direction, pitch or phase must also
invalidate or correctly remap affected history. Conservative occupancy must
cover the complete emission/absorption filtering footprint, not just source
centers. Maximum-opacity filtering can prevent holes while over-darkening edges;
it is not interchangeable with energy-preserving path averaging.

As a universal pitch, `2*min(abs(cos(theta)),abs(sin(theta)))` collapses to zero
at axis directions. Without absolute values it can also be negative. It may
describe a particular projected extent in the sketch, but it is not a complete
grid-spacing rule. The projection, plane tangent and normalization need to be
defined first. This derivation agrees with the dominant-axis construction in
[Sannikov's Sweeping.ls](https://github.com/Raikiri/LegitCascades/blob/82993030166be694004fee806c81d9cc2e496316/Sweeping.ls#L5-L25).

For a rectangular scene, clip each ray line to the scene bounds and pack its
valid interval. In the positive-x case, `v` spans approximately
`[-m*W,H]` for `m>=0`; each row has its own valid `u` range. Allocating that whole
bounding rectangle wastes space. At 45 degrees, a square's sheared bounding
rectangle has twice the area even though the shear itself preserves area.

Stretching planes across the scene can avoid missing coverage, but stretching
sample positions changes the sampling density. Prefer clipped row ranges and
explicit offsets. A plane can be removed when no requested output depends on
it. A zero-length segment is the identity interval, but its boundary can still
carry incoming light. The proposed corner omissions are conditional on the
quadrant, gather support and far-boundary policy; they are not a global rule
for all four directions. Current terminal allocations have already been removed.

## Which sparse parents must exist?

Two masks serve different purposes:

- An **optical mask** marks segments containing absorption, emission or reflected
  source terms. Certified empty segments have `L=0, T=1` and may be implicit.
- A **demand mask** marks radiance outputs that some receiver or lower cascade
  needs. Empty space may still need radiance because it transmits remote light
  or contains visible participating media.

For a fixed, aligned binary interval tree, the optical rule is exactly

```text
present(parent) = present(left_child) OR present(right_child)
```

Those children must partition the parent's segment. A nonzero source or blocker
anywhere inside that segment must reach the leaf mask first. Sampling just two
probe centers does not certify an interval as empty.

For sparse radiance demand, start from the requested fine outputs and mark every
coarser sample they actually read:

```text
needed_next(q) = OR over needed_current(p): dependency(p, q)
```

`dependency` includes angular children, shifted endpoints, interpolation support
and boundary halos. This is the reverse dependency relation of the merge. It
reduces to a two-child rule only for a layout whose complete dependency relation
really has that form.

A small interpolation counterexample shows why geometry alone is insufficient.
Place coarse probes at 0.5 and 2.5 and fine probes at integer positions. Fine
probe 2 needs both coarse probes, with weights 0.25 and 0.75. The two nearest
fine probes to coarse probe 0.5 are 0 and 1. If only fine probe 2 is active, the
proposed nearest-two test removes a required coarse probe. This does not rule
out the aligned construction in the sketch; it identifies the invariant its
index maps must satisfy.

Our current shaders are not a two-neighbor graph. Odd-direction extension reads
two alternative two-segment paths. Even-plane merging reads near and shifted
far fluence for two angular children. A sparse version must close over those
reads, or derive and validate a replacement reconstruction. Sources:
[extend.shd](../../src/render/shaders/hrc/extend.shd),
[merge.shd](../../src/render/shaders/hrc/merge.shd).

Do not use a missing allocation to mean both "vacuum" and "not computed".
Likewise, an opaque black wall has zero emitted radiance but nonidentity optical
transport. A surface-only seed policy needs an additional volume receiver policy
for the requested fog and liquid effects.

## What logarithmic ray queries can guarantee

For an ordered, discretized ray, store each interval as `(L,T)`. Define

```text
(L_a,T_a) compose (L_b,T_b) = (L_a + T_a*L_b, T_a*T_b)
identity                   = (0,1)
```

This operator is associative and order-dependent. A balanced segment tree can
answer any interval with grid-aligned endpoints using `O(log L)` stored segment
aggregates, where `L` is the number of leaves along that ray. One leaf update
requires `O(log L)` ancestor recomputations. Building the tree requires `O(L)`
work and fewer than `2L` nodes for power-of-two capacity. The code verifies
queries against direct serial integration, including fully opaque intervals.

| Cells in one ray | Checked queries/updates | Maximum sampled query node reads | Merges per update |
| ---: | ---: | ---: | ---: |
| 16 | 2,100 | 6 | 4 |
| 64 | 2,100 | 9 | 6 |
| 256 | 2,100 | 13 | 8 |
| 1,024 | 2,100 | 15 | 10 |

Across 8,400 checks, maximum error against serial double-precision integration
was `4.44e-16`. The sampled maximum is not a universal bound; standard range
decomposition uses at most a constant number of nodes per tree level. This
validates the algebra for one discretized direction, not a complete GI renderer.

"Fixed cost" here means bounded depth for a fixed maximum grid size. It does
not mean independent of resolution or identical runtime for every sparse scene.
It also does not establish `O(log L)` for arbitrary geometric rays through a
quadtree/BVH. Such rays may visit many occupied nodes. Non-grid endpoints need
partial-leaf integration; directions between stored ray families need another
sampling rule or tracing fallback.

If most queries ask for the suffix from a point to a boundary, a work-efficient
ordered scan is another candidate. It computes all suffixes with linear total
work and logarithmic parallel depth, then permits direct lookup. Arbitrary
subsegments still favor a tree. Subtracting/dividing two radiance prefixes is
unsafe across opaque intervals where transmission is zero. The GPU algorithmic
reference is [NVIDIA's parallel scan chapter](https://developer.nvidia.com/gpugems/gpugems3/part-vi-gpu-computing/chapter-39-parallel-prefix-sum-scan-cuda);
its use for these intervals is our proposed application.

## Why equal memory is not established

One coordinate-preserving OR tree with 1,024 potential leaves produced these
counts in the model. Unary ancestor chains are retained, as they would be in a
straightforward fixed-level sparse hierarchy.

| Pattern | Active leaves | Stored nodes across levels |
| --- | ---: | ---: |
| One occupied cell | 1 | 11 |
| 64 contiguous cells | 64 | 135 |
| 64 scattered cells, fixed seed | 64 | 332 |
| Dense | 1,024 | 2,047 |

Equal occupied-leaf counts therefore do not determine memory. Path compression
or a balanced tree over sorted occupied intervals can reduce unary overhead,
but introduces coordinate ranges and a lookup problem. Sparse RC also has its
own parent/merge storage, so these numbers are not a direct comparison with it.

The full accounting must include all ray families, angular refinement, sparse
indexing, compaction scratch, halos, interval payloads and any regular copy kept
alongside the sheared one. For example, 64 complete directional copies of the
current 1094 x 674 grid with approximately two eight-byte tree nodes per cell
would cost about 720 MiB before metadata. That is a deliberately naive design,
not a claim that the proposed sparse version must cost this much. It shows why
directional duplication and the cascade sampling schedule cannot be omitted.

An example dense 2D RC schedule halves probe density along both axes while
doubling directions, so elements fall by half per level. A fourfold angular
increase instead keeps elements roughly constant. HRC halves density along one
axis and doubles directions, also keeping approximately constant elements per
level. "Same as vanilla RC" must specify which schedule and quality target is
being matched. This is direct sample-count arithmetic, not a universal RC rule.

Dense fog makes many intervals nonempty. A useful design should select compact
or dense storage per block using measured occupancy, rather than assuming the
sparse path is always cheaper. Certified homogeneous medium blocks can use
analytic integration; heterogeneous blocks need the detailed field or a tested
error bound.

## Opportunities in this renderer

Rows marked "candidate" have not been GPU-benchmarked in this research.

| Opportunity | Why it could help here | Limits / status |
| --- | --- | --- |
| Compact unused short intervals | With `trace_levels=3`, even-position `T0` entries are unread; at `T1`, odd-direction entries on even planes are unread | About 7 MiB potential saving at the default work size; retain all three traced levels; candidate |
| Cascade-aware transport size | Scale 0.468 produces 1024 x 631 instead of 1094 x 674 while keeping the 64-unit world margin | 12.37% fewer work cells and 22.60% fewer requested hierarchy bytes; quality/time unmeasured |
| Separate visible materials from offscreen optical data | The margin currently expands five RGBA16F images and one RG16F image, 44 bytes per pixel before mips | Keep offscreen absorption, emission and bounce receivers; material normals/relief need only consumer support; candidate |
| Reduced diffuse reconstruction | Already isolates gather, history/filter and bounce-image size from crisp material geometry | Measured 7.8–9.3% median frame reduction in the two workloads; local quality loss; retained opt-in |
| Coarse exterior and incoming boundary light | Preserve world coverage without expanding every inner buffer or adding a full inner cascade | Must transport directional radiance and occlusion across the boundary, avoid seams/double counting; candidate |
| Clipped plane ranges and deterministic dependency pruning | Avoid processing padding and samples with no consumers | Derive from actual read stencils and gather halos; coarse levels are not automatically empty; candidate |
| Sparse optical tiles and indirect dispatch | Skip certified empty tiles and pack work into coherent groups | Includes classification, scan, metadata and query overhead; dense fog needs a dense path; candidate |
| Sheared interval accelerator with regular receivers | Tests the user's design without replacing final reconstruction | Construction and angular duplication may dominate; exact leaf integration and path correspondence required; research candidate |
| Sparse receiver dependency closure | Avoid radiance for regions never consumed by surfaces or media | Background/fog/bounce may need apparently empty regions; measure the full closed set; research candidate |
| Work-efficient directional scans | Compute many related suffix queries together | Fixed ray families can require too much angular storage or alias point lights; benchmark against trees, not assumed better |
| Workgroup-local merging / fused stages | Reduce global interval traffic and some of 88 transport dispatches | Cross-level spatial reads need halos and synchronization; register/shared-memory pressure can erase savings; candidate |
| Shader specialization for the first three traces | These have fixed short lengths and directions | Current compiler may already simplify them; inspect generated shaders and measure; candidate |
| Opaque visibility masks plus a volumetric path | Compact visibility for solid terrain, retain full transport in media | Binary visibility cannot represent colored transmission, emission or arbitrary fog; separate classification cost; candidate |
| World-aligned caches of stable optical blocks | Reuse static terrain transport while lights move | Lighting and geometry have different invalidation; moving smoke can invalidate many ancestors; distinct from rejected material-copy cache |
| Coarser broad diffuse plus bounded local direct detail | Give large-scale light less resolution while keeping selected sharp shadows | Changes the quality model and adds direct-light cost; must avoid double-counting sources; candidate |
| Glossy/reflection queries separate from diffuse | Narrow lobes need directional information that coarse diffuse reconstruction discards | Broadening diffuse is not a complete reflection or caustic solution |
| GPU counters and workload isolation | Determine whether to target traffic, ALU, occupancy or pass overhead | Current per-pass waits perturb scheduling; background load can explain variation but has not been isolated as its cause |

The first-row dependency model conservatively requests every `R0` output. On a
64 x 64 grid it reads 4,096 of 8,192 `T0` entries and 5,120 of 6,144 `T1` entries
when the first three levels are traced independently. With only level zero
traced, all 8,192 `T0` entries are needed by extension. Thus this pruning must
depend on the trace cutoff. It is an index-graph result, not scene sparsity.
The model also checks non-square, non-power-of-two dimensions.

At scale 0.468, fresh hierarchy allocations become 81.48 MiB instead of
105.26 MiB; optical intervals alone become 63.62 MiB instead of 83.48 MiB.
These are allocation calculations. Runtime capacity reuse can retain the larger
allocation until resources are recreated. A smaller work grid still changes
sampling and needs motion/shadow comparisons.

The current Cute checkout does perform GPU tile binning using count, scan and
gather passes. Our material renderer already uses compact CPU-generated tile
lists. Cute is a useful compaction reference, but adopting its path blindly
could add overhead for the small entity counts in these scenes. Local reference:
`build-release/_deps/cute_framework-src/src/cute_draw.cpp`, commit
`a13f4661f5ba27e8d45027b52f121c1704786425`.

The SDL GPU backend begins a compute pass for each dispatch. That is a place to
investigate pass overhead and fusion; it is not evidence that 88 independent
command buffers are submitted. Local reference:
`build-release/_deps/cute_framework-src/src/cute_graphics_sdlgpu.cpp`,
`cf_sdlgpu_dispatch_compute`.

Do not count previously rejected work as an available speedup. The copied static
material cache increased memory by about 124 MiB and regressed the moving case.
Alternative surface-blur dispatches/taps, shared merge weights, changed workgroup
shape, transmission-only storage and interval packing variants have already
failed either performance or image checks. Details and configuration differences
are in [the optimization log](../neon-hrc-optimization.md).

## Other implementations inspected

GitHub branch heads were resolved to the immutable revisions below on the
research date. These are design references; no external demo was benchmarked
on this host during the investigation.

| Implementation | Relevant finding | Implication for this proposal |
| --- | --- | --- |
| [Amitabha, main](https://github.com/entropylost/amitabha/tree/38b7ee2364807a4523af6de93cbb2871968c3b85) | Authors' HRC reference; README distinguishes the earlier Radial HRC experiment | Use the established HRC as a reference, and inspect radial separately |
| [Amitabha, radial](https://github.com/entropylost/amitabha/blob/0a4372692f90ceb03d7ee3e041356087ea9c0b65/src/main.rs) | Direction-specific grids, child-direction transforms, endpoint tracing during merges | Closest inspected example to the first sketch, but dense and not a validated sparse design |
| [LegitCascades, Sweeping.ls](https://github.com/Raikiri/LegitCascades/blob/82993030166be694004fee806c81d9cc2e496316/Sweeping.ls) | Explicit dominant-axis shear; test displays the lattice; raymarch helper still samples along each ray | Useful coordinate reference, not evidence of tree traversal or a speedup |
| [LegitCascades, polygonal experiments](https://github.com/Raikiri/LegitCascades/blob/82993030166be694004fee806c81d9cc2e496316/PolygonalHRC45.ls) | Direction- and parity-dependent mappings between planes and angular regions | Useful for deriving overlap/dependencies; not a ready sparse allocator |
| [Sparse RC 2D](https://github.com/IceKontroI/sparse_rc_2d/blob/7ea6ec32ecc4a1f8488d8d8b0d750d194d78e7a5/README.md) | SparseFilled seeds non-solid cells; SparseEdge seeds surface edges; dense distance/JFA support remains | Probe sparsity and traversal sparsity are separate decisions |
| [Yaazarai Volumetric-HRC](https://github.com/Yaazarai/Volumetric-HRC/tree/ed153d18f284acce9515880614b1091e9669adcf) | Texture-based HRC, with extension and directly traced variants | Useful alternative storage/tracing comparison and 2D volume fixture reference |
| [SplitRC paper](https://arxiv.org/abs/2607.20384) and [Chandra](https://github.com/entropylost/chandra/tree/0d46002d310a1a4630c8b24af83b7e098d08cf73) | Sparse world-space probe cache; screen-seeded rays deposited according to hit distance | A reference for demand-driven sparse work, but a different 3D algorithm rather than a drop-in 2D ray tree |

The radial prototype is a useful warning about directional duplication. Its
four initial 2048 x 2048 grids use one `f32` per cell. Each level doubles grid
count while halving the along-ray dimension, for ten stored levels. The source
therefore requests 640 MiB of ray data before other allocations. This is a
calculation from that prototype's chosen capacity, not its measured resident
memory or a lower bound for the user's proposal. Source:
[RayData and allocation](https://github.com/entropylost/amitabha/blob/0a4372692f90ceb03d7ee3e041356087ea9c0b65/src/main.rs#L79-L267).

Sparse RC 2D explicitly reports approximately 60% thread utilization in its
confetti example due to partially filled slabs, lower precision in the sparse
color path, and unresolved probe interpolation. Those are author-reported
limitations of that implementation, not properties of every sparse algorithm.
Source: [limitations](https://github.com/IceKontroI/sparse_rc_2d/blob/7ea6ec32ecc4a1f8488d8d8b0d750d194d78e7a5/README.md#limitationsbugs).

Chandra's [trace path](https://github.com/entropylost/chandra/blob/0d46002d310a1a4630c8b24af83b7e098d08cf73/src/algorithm/trace.rs)
and [split/deposit path](https://github.com/entropylost/chandra/blob/0d46002d310a1a4630c8b24af83b7e098d08cf73/src/algorithm/rc.rs)
still pay for ray tracing and cache construction. Its contribution is how rays
populate the sparse radiance hierarchy, not a proof that arbitrary rays traverse
geometry in logarithmic time.

The earlier [PoE2 research](poe2-radiance-cascades-performance.md) also identifies
packet tracing and compact binary visibility as comparison candidates. The
underlying renderer is proprietary and its fixed-camera assumptions differ
from our offscreen-light requirement. No published timing from these projects
is an apples-to-apples comparison with our complete M4 Pro frame.

## Bounded hybrid experiment

The comparison should preserve the current three directly traced levels,
offscreen coverage, bounce settings and receiver quality. Proposed stages:

1. Produce the existing optical field with stable world coordinates. Maintain
   conservative emission/extinction occupancy separately from receiver demand.
2. Construct one directional family at a time, or a small batch, using clipped
   sheared rows. Build leaves from the optical field. Record crossing length,
   preserve thin blockers and normalize emitter coverage.
3. Compare a compact interval tree with a work-efficient suffix scan. Keep
   exact path composition in transmission space, with suitably robust storage.
4. Feed segment queries into a fixed receiver/merge reconstruction. Account for
   endpoint corrections and any angular resampling explicitly.
5. Switch dense/sparse representation per sufficiently large block based on
   measured total cost. Preserve all needed cross-block dependencies.

Compacted arrays with tile offsets and occupancy bitsets are the first layout to
test. Per-node pointers or global hashing add metadata and irregular loads.
Grouping queries by direction and neighboring line improves locality, but sort
and dispatch costs belong in the result. An incomplete or overflowed sparse
result must fall back to valid data rather than silently reading as vacuum.

Benchmark the complete construction/query/merge/gather path, not just isolated
ray queries. Record active leaves, nodes after closure, bytes including scratch,
query reads, metadata loads, rebuild cost, dense-fallback fraction, p50/p95 frame
and render times, and changes across consecutive frames. Use the current HRC as
a performance control and an independent long-ray or analytic reference for
quality, since matching HRC is not proof of physical accuracy.

Required fixtures include an empty scene, one tiny moving emitter, thin and
diagonal walls, a pinhole, separated colored sources, camera travel across the
margin, disconnected terrain fragments, dense fog, moving liquid and a burst of
destruction. Evaluate source filtering and temporal stability as well as final
image error. Freeze/replay state, alternate candidate order, and run no competing
build or GPU benchmark from this task. These requirements protect the user's
existing stability improvements while testing whether the sparse path earns
its extra machinery.

MPM still has its own simulation cost. Fog and liquids can share an optical
field with GI, but their simulation/rasterization is not free. Caustics require
direction-changing transport or a separate approximation; a straight-ray tree
does not supply it. Layered 2D volumes and parallax should use a bounded layer
count rather than multiplying the entire HRC solve for every visual layer.

## Reproduction

Run:

```sh
python3 tools/analyze_hrc_sparse.py > /tmp/hrc-sparse-models.json
```

The script uses the Python standard library, a fixed random seed, current HRC
allocation/index formulas, independent serial interval integration and explicit
counterexamples. It does not launch the engine. No runtime defaults or assets
were changed in this research.
