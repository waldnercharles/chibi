# 3D Radiance Cascades Implementations and SDF Integration

Research checked 2026-09-10

Radiance Cascades (RC) is a multiscale representation and merge schedule for radiance. It does not require one particular probe layout, ray tracer, storage format, or GPU API. That distinction matters: "3D RC" can mean a dense world volume, a screen-space field whose intervals are traced in world space, a sparse hash of surface probes, or a different angular representation such as HRC. The quality and cost of each result come at least as much from those surrounding choices as from the cascade merge itself.

## Executive findings

1. The strongest current candidate for general 3D diffuse GI is sparse world-space RC with ray splitting. The Split Radiance Cascades paper describes visible-surface seeding, distance-based ray splitting, and sparse hash-map probes; the accompanying Chandra implementation is a useful reference, but both are early research rather than a mature drop-in renderer.
2. There are two useful first experiments. A PoE2-style screen-probe and screen-march path is the cheapest control when the camera and visible world are bounded. A screen-probe and world-interval path is the better first general 3D prototype because it can still see off-screen emitters and occluders.
3. Compute shaders are an execution and storage choice, not a competing RC family. They pair naturally with sparse accumulation, ping-pong cascade passes, and SDF tracing, but they also impose API, synchronization, format, and lifecycle requirements.
4. Dense volumetric RC is the clearest choice for participating media and full off-screen coverage, and the least forgiving choice for memory. A 3D base grid is cubic in resolution before angular storage is counted.
5. Holographic Radiance Cascades (HRC) has strong 2D evidence and an interesting volumetric variant. The HRC paper is explicitly 2D; a general, independently benchmarked 3D HRC implementation is not established. Polygonal HRC and Linespace are promising research directions, not safe foundations for a production 3D SDF renderer.
6. SDFs are most valuable at the visibility boundary. An analytic path such as [DivisionEngine](https://github.com/NorthRocStudios/DivisionEngine) evaluates authored primitives directly, while the dynamic SDF design described by [Mike Turitzin](https://www.youtube.com/watch?v=il-TXbn5iMA) caches distances in sparse bricks and clipmaps. Both can supply conservative segment hits, distances, normals, material identity, emission, and optional transmittance; neither supplies a volume density or phase function by itself.
7. For c-blueprints, the sensible sequence is a shared surface/segment contract, then a screen-probe/world-interval prototype, then a sparse ray-splitting comparison, and only then a volumetric or HRC experiment if a real scene needs it.

## Scope and evidence

The review covers the formal RC paper, author demonstrations, public implementations, and related line-space and surfel work. GitHub links are included throughout and collected in the catalog near the end. Measurements quoted from project READMEs are identified as author-reported; they should not be treated as cross-project benchmarks. Claims such as "scene complexity independent" or "constant time" are implementation goals or author descriptions unless the cited source supplies a controlled evaluation.

"Linespace" is not a single standard RC variant. Here it means the classical four-dimensional space of oriented lines and line-based storage, plus the line-function approach used by the Nihil HRC experiment. That is the interpretation most useful when comparing it with HRC and sparse ray representations.

## 1. The RC mental model

### Why cascades exist

The original RC work observes that a diffuse penumbra has high spatial frequency and low angular frequency near a receiver, while it has lower spatial frequency and higher angular frequency farther away. The algorithm spends samples where the signal needs them and merges intervals as distance grows. This is the central idea behind both flatland and 3D variants; probe grids, rays, and shaders are replaceable pieces.

The formal source is [Radiance Cascades](https://arxiv.org/html/2408.14425), with reference material in [Raikiri/RadianceCascadesPaper](https://github.com/Raikiri/RadianceCascadesPaper).

### Interval representation and merge

An interval stores radiance and optical depth over a segment:

~~~text
R[a,b](p,w) = (L[a,b](p,w), tau[a,b](p,w))
T[a,b]       = exp(-tau[a,b])

merge(near, far) =
    (L_near + T_near * L_far,
     T_near * T_far)
~~~

The exact notation varies by implementation. The important invariant is that the far interval is attenuated by the near interval before the two are combined. For opaque surface GI, the interval often degenerates into visibility plus returned radiance. For participating media, the optical-depth part is essential.

### Scaling is a budget, not a law

An example 2D schedule with a spatial scale of two uses one quarter as many probes and four times as many directions at the next cascade. A 3D world volume has one eighth as many probe cells at that spacing, so its memory drops by about one half per cascade when angular samples grow by four. This explains why upper cascades are cheap in count but why the base 3D grid is still expensive.

| Representation | Probe-cell scaling at cascade i | Angular scaling | Approximate storage trend |
| --- | ---: | ---: | ---: |
| 2D flatland | 2^(-2i) | 2^(2i) | roughly constant per cascade |
| 3D volume | 2^(-3i) | 2^(2i) | 2^(-i); total approaches about 2× base |
| 2D surface or screen field | 2^(-2i) | 2^(2i) | roughly constant per cascade |

These are illustrative powers from the RC construction, not requirements for every implementation. HRC and polygonal variants change the angular representation and therefore change the constants.

## 2. Three independent design axes

Comparing names alone hides the real trade-offs. Every implementation can be described along three axes:

1. Spatial domain: screen/depth buffer, regular world volume, sparse world hash, surface/surfel field, or a 2D HRC plane.
2. Angular and interval representation: directional texels, cubemap or octahedral maps, HRC frusta, polygons, lines, or a compact cone.
3. Visibility backend: screen raymarch, SDF sphere tracing, BVH or hardware ray tracing, raster depth queries, or a volumetric density marcher.

Compute versus fragment shaders is a fourth, mostly orthogonal axis. It changes how the chosen representation is scheduled and synchronized; it does not remove the representation's memory or visibility costs.

## 3. Comparison at a glance

| Family | Best use | Main advantages | Main costs and failure modes |
| --- | --- | --- | --- |
| Traditional regular-grid RC | Controlled 2D or small 3D test scenes | Simple theory, noise-free multiscale reuse, easy visualization | 3D base grid is cubic; regular-grid work ignores empty space; interpolation leaks and ringing need fixes |
| Screen-space RC with screen tracing | Fixed-camera 2D/2.5D effects | Very low geometry setup cost; can be fast on a depth buffer | Cannot know off-screen emitters/occluders or geometry behind the depth buffer |
| Screen probes with world intervals | First 3D GI prototype in a raster engine | Reuses G-buffer; world-space tracing recovers off-screen visibility; works with BVH or SDF | Requires G-buffer, reprojection/disocclusion handling, and a segment tracer; screen coverage still limits where probes are born |
| Dense volumetric RC | Fog, smoke, sky, full off-screen volume | Natural medium integration and environment coverage | Cubic probe memory, many empty cells, expensive angular storage and updates |
| Sparse RC | Large or mostly empty worlds | Memory follows occupied probes; hash storage can cover off-screen space | Hash overhead, atomics, probe holes, difficult interpolation and invalidation |
| Ray-splitting RC | Single-frame or temporally accumulated diffuse 3D GI | Work follows visible surfaces and hit distance; avoids tracing every high cascade separately | Screen-driven workload varies; small details below base spacing disappear; leakage and angular bias remain |
| Traditional HRC | 2D sharp-shadow/volumetric experiments | Frustum and plane construction can preserve sharp features with short extensions | Published result is 2D; 3D storage and angular growth are unresolved; glossy behavior and aliasing need care |
| Volumetric HRC | 2D/flatland participating media | Integrates media with logarithmic ray extensions; promising quality/performance trade | Directional and frustum assumptions are specialized; public timings are demo measurements |
| Polygonal HRC | Research into sparse, alias-aware angular storage | Polygon overlap can represent a single-pixel source without ordinary raymarch noise | Gather can be O(N²); implementation language and topology are specialized; no mature 3D evaluation |
| Linespace | Static coherent line or light-field data | Four-dimensional line parameterization supports adaptive reuse and view interpolation | Discontinuities, dynamic SDF topology, and volumes are difficult; it is adjacent to RC, not a standard RC algorithm |
| Surfel RC | Surface-only GI with persistent patches | Stable surface records and explicit maintenance can suit sparse geometry | Needs surfel lifecycle and coverage management; does not directly represent empty volume |

## 4. Topic deep dives

### Traditional RC

The formal construction starts from a regular probe grid, traces short intervals in the base cascade, then extends and merges intervals into progressively coarser cascades. The paper describes a generalization to three dimensions using a regular 3D probe grid and a directional representation such as a cubemap or octahedral map. Its asymptotic storage argument is favorable after the base level, but the base level still scales with scene volume and angular resolution.

Strengths:

- The signal model is explicit: near and far intervals have different spatial and angular bandwidth.
- Work is highly parallel and largely deterministic, with no Monte Carlo noise in the basic formulation.
- The same merge algebra supports opaque visibility, emissive surfaces, and participating media when optical depth is retained.
- The regular layout is easy to debug, serialize, and compare across resolutions.

Weaknesses:

- A regular 3D base grid spends memory and tracing work in empty space.
- Bright opaque sources can produce ringing. The paper's bilinear fix reduces the artifact but costs additional traces or lookups.
- Interpolation can leak across thin walls, depth discontinuities, and unrelated materials.
- Dynamic scenes need invalidation or temporal handling; the original construction does not solve that policy.
- Upper cascades can dominate runtime when intervals are long. The scientific DexRT implementation reports a 3072×2048 case where its highest cascade consumed most of the GPU time, a reminder that "few probes" does not mean "free."

The [RadianceCascadesPaper repository](https://github.com/Raikiri/RadianceCascadesPaper) is the best place to study the formal derivation and sample implementations. It is a research reference rather than an engine integration layer.

### Screen-space RC

There are two materially different designs.

Screen tracing keeps both probes and interval visibility in screen space. Path of Exile 2 is the frequently cited example: the paper describes a flatland-like screen-space field populated by screen-space raymarching, with a reported roughly 3 ms cost on a GTX 1050. This is attractive when the camera is fixed or when only visible 2D/2.5D lighting is needed.

Screen probes with world-space intervals place probes on the depth buffer but trace each interval in world space. The WIP RC paper describes bilateral spatial interpolation, directional interpolation, and interval endpoints reconstructed from the depth buffer. The approach can see off-screen emitters and occluders if its world tracer can reach them. That tracer may be a volume marcher, software or hardware ray tracing, or an SDF marcher.

The trade is therefore about the backend, not the word "screen":

- Screen tracer: fastest setup and often the lowest cost, but invisible geometry is genuinely unavailable.
- World interval tracer: more setup and visibility cost, but off-screen and behind-depth geometry are representable.

Useful references are [CodyJasonBennett/three-rc](https://github.com/CodyJasonBennett/three-rc), which combines screen-space probes with a BVH, and [tmpvar's demo](https://tmpvar.com/poc/radiance-cascades/), which shows the world-interval idea and alternative depth layouts. Three-rc reports noise-free diffuse and specular paths without temporal accumulation, but it is an unlicensed experimental repository; treat it as a design reference until licensing is clarified.

Screen-space RC interacts well with rasterization:

- A depth, world-position, normal, albedo, emission, material ID, and motion-vector G-buffer supplies probe seeds and interpolation guards.
- A short screen gather can provide contact detail while world intervals provide broader indirect light.
- Reprojection improves stability but creates ghosting and disocclusion work.

It interacts poorly with:

- Off-screen lights and occluders when the interval tracer is also screen-space.
- Large camera cuts, thin geometry, and high depth complexity unless probes are placed on multiple depth layers or min/max depth.
- A renderer that has no public depth/normal/emission targets.

### Path of Exile 2: what the public record actually says

The public evidence for the Path of Exile 2 deployment is the [ExileCon 2023 rendering talk](https://www.youtube.com/watch?v=TrHHTQqmAaM) and the implementation section in the [Radiance Cascades paper](https://github.com/Raikiri/RadianceCascadesPaper/blob/main/RadianceCascades.tex).

PoE2 uses a flatland-like hierarchy of probes in screen space and populates it with screen-space raymarching. It does not build a general 3D radiance volume. The choice is deliberate: the talk says the camera is fixed and the game does not need light sources behind the camera. The paper makes the corresponding limitation explicit: off-screen emitters, invisible occluders, and geometry behind the depth buffer do not contribute to this path.

The performance comes from several choices working together:

- A fixed probe hierarchy. The grid and cascade count do not adapt to the number of monsters, particles, or emissive objects on screen. The GI pass therefore has a predictable workload even when the scene is busy. This says nothing about the cost of rasterizing those objects.
- Spatially graded work. The first cascade covers a tiny neighborhood at the highest spatial resolution. Farther ranges use fewer probes and more angular samples, so the broad part of the field runs on smaller images. The talk describes the first cascade as AO-like and shows each later level carrying lower-frequency transport.
- Screen-space visibility. A depth buffer and screen-space radiance are cheaper to query than a world BVH, hardware RT structure, or 3D SDF at every interval. That is the largest reason a PoE2-style path should not be compared directly with a world-space volume.
- Ray bundling and bit packing. The paper says the production path raymarches four rays simultaneously and stores occlusion in a 128-bit mask per cascade texel. The exact shader is not public, so the safe conclusion is that PoE2 packs many binary visibility results into a small, coherent representation. It is not safe to assume that four rays map directly to one hardware SIMD instruction on every GPU.
- No per-light GI loop. Emissive and directly lit surfaces enter the screen-space radiance input. The GI pass reuses that field rather than launching a separate shadow or transport loop for each source.
- No required temporal denoiser in the basic RC presentation. The paper describes rebuilding the field per frame without previous-frame data and still avoiding an extra denoising stage. That is an algorithm/reference property; it does not prove that every shipped PoE2 setting has no temporal work.

The often-quoted approximately 3 ms on a GTX 1050 is an author-reported PoE2 GI result. The ExileCon Q&A also gives an approximate 1 ms figure. Those numbers likely describe different builds, settings, or measurement boundaries, and there is no public shader or benchmark that reconciles them. They are evidence that this constrained path can be fast on old hardware, not a portable budget for a world-space 3D implementation, a free camera, or an SDF scene with off-screen lighting.

The most important lesson is the constraint match. PoE2's camera, level presentation, custom engine, and screen-visible lighting let the team throw away generality. Reproducing the number while keeping arbitrary cameras and off-screen transport would require a different visibility backend and would give up much of the advantage.

### PoE2-style performance experiments

The following ideas are ordered by how directly they follow from the public PoE2 design. Each should be measured against the same image error and camera path.

| Experiment | Expected benefit | Cost or risk |
| --- | --- | --- |
| Add a screen-only control path | Establishes the low-cost ceiling and exposes how much off-screen lighting matters | Fails by design for lights and occluders outside the view |
| Keep a fixed cascade grid and fixed loop counts | Makes GPU time stable and avoids scene-dependent spikes | Spends work in empty screen tiles; this is acceptable only when the target view is bounded |
| Render cascade 0 at a reduced resolution | Cuts the dominant spatial workload; upscale or gather only where the near field is needed | Contact shadows and thin features lose detail first |
| Use a smaller image for each farther cascade | Matches the PoE2 hierarchy and reduces bandwidth for broad transport | Incorrect extent or interpolation causes seams and missing long-range light |
| Trace four rays in lockstep per invocation | Reuses depth fetches, direction math, and loop control | Divergent ray exits can raise register use; benchmark scalar, vector, and subgroup versions |
| Store binary visibility as a bit mask | Replaces many floating-point visibility values with a few integer words | Packing and unpacking cost can dominate if the mask is consumed only once |
| Precompute ray directions and cascade offsets | Removes repeated trigonometry and address arithmetic | Increases constant data and needs a clear convention for camera changes |
| Limit each ray to its cascade interval | Avoids marching past the range represented by the current level | Requires correct near/far bounds and a miss value that merges cleanly |
| Keep a short screen gather for contacts | Recovers local detail without increasing all cascades | Adds another pass and can double-count indirect light |
| Update only selected tiles or emitters | Can save work in mostly static or dark regions | Breaks PoE2's predictable cost and can create temporal popping |
| Reproject coarse cascades | Amortizes long-range work while keeping cascade 0 responsive | Ghosting and disocclusion; requires reliable motion and invalidation |
| Pack radiance to FP16 and visibility to integer masks | Reduces bandwidth and cache pressure | HDR range, denormals, and atomic format support need device testing |
| Use SDF or BVH only as a fallback | Preserves the cheap screen path while covering selected off-screen primitives | Lighting becomes inconsistent at the boundary unless the two tracers share materials and units |
| Query fewer cascades for rough specular lobes | Uses the angular bandwidth that the BRDF actually needs | Too few levels blur highlights or bias reflections |

Several tempting hacks are poor first choices:

- Replacing the screen marcher with a full 3D SDF volume removes PoE2's largest savings and creates a cubic storage problem.
- Adding a temporal denoiser before measuring the fixed, no-history path hides whether cascade packing and interpolation are correct.
- Iterating over every light or emissive object inside the GI shader destroys the scene-independent pass cost.
- Applying a screen-space path to a free camera and then trying to repair off-screen misses with blur produces plausible averages but cannot recover missing transport.

For c-blueprints, the clean experiment is to keep the PoE2-style screen-only path as a benchmark control, while the production candidate remains screen probes with a world-space segment query. That lets the profiler answer a useful question: how much of the cost comes from RC scheduling and how much comes from general 3D visibility?

The focused [PoE2 evidence and experiment note](poe2-radiance-cascades-performance.md) records the public claims, uncertainty around the reported timings, and a measurement matrix for the proposed optimizations.

### Compute shaders and RC

Compute is an implementation strategy that can serve every family above. It is especially helpful when each cascade is a separate dispatch, when probe cells are sparse, or when atomics are needed to splat contributions.

[uziiDevelopment/providentia-cascades](https://github.com/uziiDevelopment/providentia-cascades) is a clear native example. It ports a Shadertoy 3D RC demo to Rust and wgpu, uses an RGBA16F atlas with six cascade layers and ping-pong storage, dispatches one compute pass per cascade from high to low, and samples parent cascades during the merge. It also reuses the first cascade for multi-bounce feedback. The shader hardcodes demonstration geometry, so the scheduling pattern is more reusable than the scene code.

Other useful compute references:

- [alexmalyutindev/unity-urp-radiance-cascades](https://github.com/alexmalyutindev/unity-urp-radiance-cascades) explores direction-first, 3D probe, cubemap, voxel, and compute variants. The README labels the project work in progress.
- [TypeGPU radiance cascades](https://docs.swmansion.com/TypeGPU/ecosystem/typegpu-radiance-cascades/) exposes an SDF callback and a custom rayMarch callback that returns color and transmittance. This is a strong example of keeping the RC scheduler separate from the scene query.
- [kornelski/bevy_flatland_radiance_cascades](https://github.com/kornelski/bevy_flatland_radiance_cascades) is a small WGSL 2D implementation with pre-averaging, a direction-first layout, and a "gear fix." It is useful for layout ideas, but it omits mipmaps and bounce and uses a density-map marcher rather than a 3D SDF.

Compute strengths:

- Explicit dispatch dimensions and memory formats.
- Natural ping-pong buffers for extend, merge, and temporal history.
- Atomics and hash-table updates for sparse or ray-splitting schemes.
- Easier separation of tracing, accumulation, and composite passes than a large fragment shader.

Compute costs:

- Barriers, texture transitions, and indirect dispatches can erase theoretical parallelism.
- Sparse hashes require collision handling, clearing, and deterministic enough accumulation.
- FP16 precision and atomic support vary by API and device.
- Shader resources must be created, resized, and reset at well-defined GPU-frame boundaries.
- A fallback path may be needed where compute is unavailable. In c-blueprints, the existing radiance constructor returns null on GLES3, so a compute-based 3D experiment should remain optional rather than silently changing the baseline renderer.

### Volumetric RC

Dense volumetric RC puts probes in a regular 3D volume and stores directional radiance or intervals at each cell. It is the most direct extension when the scene contains fog, smoke, clouds, or a sky volume, and it naturally handles lights that are not currently visible on a surface. Its downside is the product of three-dimensional spatial resolution, directional resolution, cascade count, and history buffers.

The implementation question is whether the medium is represented as:

- A density field with extinction, emission, and a phase function, marched through each interval.
- A surface visibility field where opaque hits terminate the interval.
- A hybrid where surfaces and media share the interval algebra but use different hit and attenuation rules.

The [Volumetric-HRC repository](https://github.com/Yaazarai/Volumetric-HRC) is an instructive 2D/flatland hybrid. It traces rays in a 90-degree cone, halves probes along one axis while doubling rays per cascade, extends a ray at most logarithmically many times, and averages four frustum directions. Its README reports around 9-12 ms at 2048² on an RTX 3080; the two numbers differ within the README, so they are best read as a demo range, not a benchmark. The repository also documents a key ordering rule: merge near and far fluence before interpolation. Interpolating first breaks volumetric attenuation.

For a scientific, non-realtime data point, [DexRT](https://arxiv.org/html/2408.14425) uses GPU RC for radiative-transfer calculations. A 3072×2048 snapshot with six cascades and multiple wavelength batches consumed substantial GPU time and memory, with the upper cascades dominating timing. This validates the method's numerical usefulness while warning against using that implementation as a realtime cost estimate.

SDF and volume are complementary:

- An SDF gives opaque-surface distance, normal, and material data.
- A medium needs density or extinction, emission, and a phase function.
- SDF distance can skip empty space or bound an opaque object, but it must not replace the medium's extinction integral.
- If a ray starts inside an SDF solid, the tracer needs an explicit inside-material rule; a signed distance alone does not define a physically correct exit interval.

### Sparse RC

Sparse RC replaces the regular world grid with a hash map, brick pool, clipmap, or another occupied-cell structure. It is compelling when the world is large but lit surfaces or active probes occupy a small fraction of it.

[IceKontroI/sparse_rc_2d](https://github.com/IceKontroI/sparse_rc_2d) is a useful 2D proof of concept. It keeps the raymarch and merge logic but stores only populated records conceptually, compares an empty-air representation with an edge/surface representation, and discusses slab chains, z-order, direction-major layouts, and RGBA8 quality. The project reports high utilization gains in a confetti-like scene while also documenting that interpolation is the hard part: missing neighbors and thin features make naive interpolation unstable.

The sparse design choices are:

- Hash versus brick: hashes save space but cost indirection and collision work; bricks improve locality but can reintroduce empty-space overhead.
- Surface-only versus volume occupancy: surface probes are cheaper and fit diffuse GI; volume occupancy is needed for media and reliable off-surface visibility.
- Eager versus demand-driven allocation: eager allocation gives stable frames; demand-driven allocation follows the camera but produces variable work.
- Renormalized interpolation versus nearest probe: renormalization avoids darkening when neighbors are missing, but can brighten across walls unless visibility and material guards reject the sample.

Sparse RC works well with compute shaders and world-space SDF/BVH queries. It works poorly with a dense full-resolution temporal history, because clearing, allocating, and reprojection can cost more than the saved probe storage. It also needs explicit invalidation when SDF topology, transforms, material emission, or world scale changes.

### Ray splitting

Ray splitting is the central idea in [Split Radiance Cascades](https://arxiv.org/abs/2607.20384). Instead of tracing every ray independently for every high cascade, it traces rays from visible surfaces and assigns their contribution to a cascade based on the distance at which the ray reaches useful information.

A practical split pipeline is:

1. Seed probes from visible surface pixels or tiles.
2. Trace a ray through the world, returning a miss, a transparent interval, or an opaque hit with distance and material data.
3. Convert the hit or interval distance to a cascade index using the cascade boundaries.
4. Accumulate the contribution in the sparse cell and angular bin for that cascade. Transparent entries can continue to a farther interval; an opaque hit terminates at its selected interval.
5. Store radiance, transmittance, and a sample count, usually with atomic updates.
6. Interpolate only existing probes, renormalizing weights and rejecting samples across depth, normal, material, or visibility discontinuities.

The [entropylost/chandra](https://github.com/entropylost/chandra) repository contains a reference implementation with trace, splat, merge, cache, and denoising paths. The source is valuable for the data flow, but its license should be checked before copying it into a product. [wroughtinator/split-radiance-cascades-webgpu](https://github.com/wroughtinator/split-radiance-cascades-webgpu) adds a software BVH, dynamic invalidation, and validation scenes. Those extensions are useful engineering ideas, but they are third-party additions and their README timings are self-reported.

Why splitting helps:

- Work follows visible surfaces instead of every cell in a world volume.
- SDF sphere tracing gives the exact hit distance needed for cascade assignment.
- Off-screen geometry is available as long as the world tracer and sparse map can reach it.
- A single frame can produce a useful result; temporal accumulation is optional.

Where splitting hurts:

- Frame cost follows visible pixel count and ray complexity, so it can vary sharply with camera and geometry.
- Details smaller than the base probe spacing can vanish before any interpolation.
- Low angular resolution biases glossy or anisotropic response.
- Sparse interpolation can leak through walls or leave holes without visibility-aware guards.
- The published method targets diffuse GI; mirror transport, caustics, and high-frequency specular need a different sampling strategy.

SDF is a particularly good fit for splitting because the signed-distance tracer already returns a monotonic hit distance. The fit fails if the SDF is not conservative, if material transparency is collapsed into a binary hit, or if the renderer cannot provide a stable mapping from SDF hit to shared material and emission data.

### Holographic Radiance Cascades (HRC)

The [Holographic Radiance Cascades paper](https://arxiv.org/abs/2505.02041) is explicitly a 2D global-illumination method. It combines short ray intervals into a holographic representation and reports 1.85 ms at 512² and 7.67 ms at 1024² on an RTX 3080 Laptop. Those figures are useful for the paper's 2D implementation, not evidence of 3D performance.

HRC's useful ideas are:

- Store a compact representation of directional wavefronts or frusta rather than a conventional per-pixel path.
- Extend rays logarithmically so far transport is represented with fewer operations.
- Preserve sharp source information by combining angular boundaries carefully.
- Treat volumetric attenuation as part of the interval algebra when near and far fluence are merged before interpolation.

[entropylost/amitabha](https://github.com/entropylost/amitabha) is a reference HRC variant. Its README claims hard shadows and volumetrics without an added pass, and links to earlier radial-HRC and volumetric experiments. These are repository claims; the code is best used to understand data layout and merge order.

HRC is attractive for 2D SDF scenes because an SDF can provide the short interval endpoints, normal, and emission used by the base pass. It is less certain for 3D:

- A 3D holographic representation needs a direction-space and plane/frustum storage strategy whose memory and interpolation behavior are not settled by the 2D paper.
- Glossy lobes and mirror-like transport can alias or moire when angular boundaries are sparse.
- A production implementation would need a clear policy for arbitrary 3D occluders, disocclusion, and dynamic invalidation.

Treat HRC as a targeted 2D or flatland experiment, or as a research branch after the conventional 3D screen/world and sparse baselines have measurements.

### Polygonal HRC

[Raikiri/LegitCascades](https://github.com/Raikiri/LegitCascades) contains early Linespace scripts, including PolygonalHRC45.ls. The script uses nine cascades, a base probe spacing of 10, a line spacing of 10, and one direction before doubling line spacing and directional coverage at each cascade. Its visible path is LoadCascade, ExtendCascade, and GatherCascade: destination polygons are mapped to source lines, then polygon overlap is gathered across the two frustum halves.

The author's early demo describes an alias-free single-pixel source, logarithmic ray extension, and a gather cost that can be O(N²). The code and interactive [Legit Script Editor](https://radiance-cascades.github.io/LegitScriptEditor/?gh=Raikiri/LegitCascades/PolygonalHRC45.ls&rev=master) are valuable for experimentation, but there is no formal 3D paper or independent evaluation.

Potential advantages:

- Polygon overlap can retain a sharp angular boundary without increasing the resolution of every neighboring probe.
- A sparse polygonal representation may be useful for stylized hard-shadow or 2D/2.5D effects.
- The extension/gather split exposes a clean place to plug in analytic or SDF segment queries.

Risks:

- Polygon clipping and overlap are harder to vectorize and debug than directional texels.
- Topology changes in dynamic SDF scenes can invalidate many cached polygons.
- The gather cost and irregular memory access can dominate any savings from sparse storage.
- The representation has no established recipe for full 3D volumes, arbitrary BRDFs, or temporal reprojection.

### Linespace

Classical line space represents an oriented 3D line with four coordinates. The graphics literature uses linetrees, adaptive error tests, and quadrilinear interpolation to reuse coherent visibility or radiance samples. [Bala, Dorsey, and Teller's line-space paper](https://people.csail.mit.edu/teller/pubs/BalaDorseyTellerEGWR1999.pdf) is a useful primary reference; the [Cornell interactive ray-tracing lecture notes](https://www.cs.cornell.edu/courses/cs665/2004fa/Lectures/Lec11_InteractiveRayTracing_web_6page.pdf) provide a concise overview.

Linespace is adjacent to RC rather than a standard RC family. It can be valuable when:

- Geometry and visibility are static or change slowly.
- Many views reuse the same coherent set of lines.
- Adaptive refinement can pay for itself over many queries.

It is a poor first fit for dynamic SDF rendering:

- SDF silhouettes and topology changes create discontinuities in line space.
- Invalidating a linetree or line cache after deformation can approach rebuilding it.
- A volume needs more than surface line visibility; density, phase, and multiple scattering still need an interval model.
- Four-dimensional interpolation across an occlusion discontinuity can leak more severely than a guarded local probe interpolation.

The [Nihil experiment](https://github.com/entropylost/nihil) treats lines as continuous functions with bilinear discretization and explicitly reports worse quality and performance than its normal HRC path. That is a useful warning against assuming that a mathematically compact line representation is automatically a faster RC implementation.

### Surfel RC

[mxcop/src-dgi](https://github.com/mxcop/src-dgi) is an archived MIT-licensed surfel-based RC experiment. It maintains persistent surface patches, traces and merges radiance, and composites the result. The associated [surfel-maintenance article](https://m4xc.dev/articles/surfel-maintenance/) explains hash-grid maintenance and spawning patches from screen tiles.

Surfel RC is a useful alternative when the renderer wants persistent surface records rather than a volume. It is compatible with SDFs that can emit stable surface positions, normals, and material IDs. It does not solve participating media or empty-space visibility, and the surfel lifecycle becomes a first-order system: spawn, merge, retire, and invalidate patches as the SDF or camera changes.

### SDF implementation references: DivisionEngine and dynamic cached fields

These two SDF sources answer different questions. [NorthRocStudios/DivisionEngine](https://github.com/NorthRocStudios/DivisionEngine) is a concrete analytic SDF renderer. The linked [Mike Turitzin video](https://www.youtube.com/watch?v=il-TXbn5iMA) describes how to store and update a large, editable SDF world. The first is useful for shader-level cost analysis; the second is useful for deciding what the scene query should cache.

#### DivisionEngine: a concrete analytic GPU path

The repository describes itself as an SDF-based C# game-engine preview that generates HLSL from .NET with ComputeSharp. Its README labels the project experimental and intended for preview, experimentation, and education. The current repository is GPL-3.0 licensed, so the code is an implementation reference rather than an assumed dependency.

The [3D SDF compute shader](https://github.com/NorthRocStudios/DivisionEngine/blob/85d5371c963e63a28e74fd892499d8c5de6142bc/DivisionEngine.Core/Rendering/SDFShader3D.cs) exposes several patterns that transfer directly to an RC visibility backend:

- `EvaluatePrimitiveDistanceFast` takes a primitive type and one `float4` of parameters instead of passing a larger object record through every normal sample. This keeps generated shader code and argument flow small.
- `FastNormalSingleObject` evaluates four offsets against the already identified object. For a hard-min union, this changes normal work from four full world queries to four single-object queries. It is not valid for smooth blends, and the source excludes terrain from this shortcut.
- `Raymarch` uses a fixed maximum step count, a scale-aware epsilon, and a short binary refinement when a sample crosses from positive to negative distance. That combination makes cost predictable while reducing the visible error at a surface hit.
- The shader can map each frame to half the horizontal pixels in a checkerboard pattern. The [render pipeline](https://github.com/NorthRocStudios/DivisionEngine/blob/85d5371c963e63a28e74fd892499d8c5de6142bc/DivisionEngine.Core/Rendering/RenderPipeline.cs) dispatches the reduced width and the shader derives the alternating column from frame count and row parity.
- Material DTOs carry precomputed PBR values and texture metadata, so the ray hit does not need to rebuild those values during every SDF sample.

The same code also shows where a direct per-object path stops scaling. `WorldSDF` scans the complete SDF object buffer for every distance evaluation. There is no BVH, brick indirection, clipmap, or sparse distance cache in the inspected renderer. That is acceptable for a small analytic scene, but an RC pass multiplies the cost: every interval sample can invoke `WorldSDF`, and a high-resolution base cascade can invoke it many times per pixel.

The render pipeline allocates quarter-resolution depth and color textures, but the current snapshot does not dispatch the SDF shader into those textures; its active dispatch targets the full-resolution render and depth-normal textures, with checkerboard as the separate half-width path. Treat the low-resolution allocation as scaffolding until a capture proves a complete downsample and reconstruction path. The renderer does run optional denoising passes after the SDF dispatch, so a checkerboard or reduced-resolution experiment must measure reconstruction, edge quality, and history behavior as one pipeline.

DivisionEngine therefore supplies good local tricks for a prototype, but it does not solve large-world SDF storage. Its highest-value lesson for RC is to keep the primitive evaluator, hit refinement, normal calculation, material lookup, and world acceleration as separate seams. Replacing the linear `WorldSDF` scan with a bounded BVH or cached brick query can improve every RC family without changing interval merge code.

#### Turitzin's dynamic SDF engine: cache, sparsity, and clipmaps

The linked video is not an RC implementation and does not publish a renderer repository. It is an author-described architecture for a game engine built around editable SDF geometry. The chapter sequence names the relevant techniques directly: caching SDF distances, interpolating cached values, sparse caching, a brick map and brick atlas, geometry clipmaps, incremental updates, physics/collision, and terrain.

The proposed storage model is a useful complement to RC:

- Cache distance samples on a grid so a ray does not reevaluate a long CSG edit list at every sphere-tracing step.
- Allocate only sparse bricks near represented geometry, with an indirection map pointing into a brick atlas. This keeps memory closer to surface area than to the cube containing the whole world.
- Use clipmap levels centered on the player or camera to keep a bounded, multiresolution active region while retaining a large apparent draw distance.
- Track edited shapes in an AABB or BVH-like structure and regenerate only the bricks whose bounds overlap a changed edit. Preserve unchanged bricks across frames.
- Keep render SDFs and physics data separate. The video describes extracting mesh or collision representations for physics rather than using the render approximation as the gameplay collider.

These are design claims from the video, not independently benchmarked numbers. The video does not establish a particular brick size, distance encoding, interpolation kernel, GPU API, or update budget for a production engine. Those choices still need measurement on the target hardware.

The key correctness issue is interpolation. A trilinear interpolation of cached distances is not automatically a conservative distance bound, especially across a sign change or a thin wall. A sphere tracer that trusts an overestimated interpolated distance can step through an occluder. Use a safety factor or conservative min bound, carry sign and occupancy metadata near brick boundaries, and record when a ray falls back to a finer brick or analytic query.

#### What transfers to Radiance Cascades

| SDF technique | Benefit for RC | New obligation |
| --- | --- | --- |
| Cached distance samples | Lowers the per-step cost of screen/world intervals and split rays | Cache build and update bandwidth; conservative interpolation and stale-data detection |
| Sparse bricks plus atlas indirection | Makes off-screen world intervals possible without a dense 3D volume | Hash or page lookup, brick-boundary filtering, allocation and eviction policy |
| Geometry clipmaps | Bounds active SDF memory and gives coarse distance data for far RC intervals | Align two LOD hierarchies so RC cascade ranges do not straddle an SDF level discontinuity |
| Dirty-region updates | Lets dynamic SDF edits invalidate nearby probes instead of rebuilding all cascades | Conservative influence radius, probe reseeding, and temporal-history reset |
| Analytic primitive fallback | Preserves exact small-object hits and avoids baking every transient effect | Keep analytic and cached materials, units, and hit semantics identical |
| Render/physics separation | Prevents visual SDF approximations from changing collision behavior | Maintain a separate collision update path and test it independently |

The combined recommendation is a two-tier scene query. Keep DivisionEngine-style analytic primitives for small authored objects and transient effects. Add a sparse cached SDF with clipmaps for large terrain or heavily edited geometry. Expose both through the same bounded `trace_segment` contract. Sparse or split RC can then use cached world intervals, while the PoE2-style screen-only path remains the low-cost control for fixed-camera scenes.

There are two failure modes to test before claiming a win. A cache can make sphere tracing faster while making hits wrong if its distance bound is not conservative. A clipmap can make memory bounded while creating visible lighting steps if RC cascades and SDF levels change at different distances. Thin walls, tunnels, edit boundaries, camera cuts, and rays that begin inside solids should be dedicated validation scenes.

## 5. SDF rendering contract

The RC code should depend on a narrow scene-query interface, not on a particular SDF implementation:

~~~text
trace_segment(origin, direction, t_near, t_far)
    -> miss
    -> transparent interval { t_exit, transmittance, emission }
    -> opaque hit { t_hit, normal, material_id, albedo, emission }
~~~

An implementation may split this into separate visibility and shading calls, but the semantic results should be equivalent.

### Requirements for a useful SDF tracer

- Conservative distance bound. A non-conservative SDF can step through a thin occluder and assign the wrong cascade.
- Explicit t_near and t_far. Cascade intervals need finite boundaries, and world-space screen probes need a reliable reconstruction range.
- Maximum steps and a failure classification. A miss caused by step exhaustion should be observable during validation instead of becoming a silent unlit sample.
- Bounds or an AABB before sphere tracing. This prevents a long empty ray from consuming a large fixed step budget.
- Surface bias and inside handling. Offset secondary rays by a scale-aware epsilon; define how rays that start inside a solid find an exit or terminate.
- Shared transforms and units. Probe spacing, SDF gradients, mesh bounds, and light radii must use the same world scale.
- Material and emission lookup. The SDF hit must map to the same authored material model as raster meshes; a separate "SDF color" path will drift.
- Optional transmittance for transparent or participating segments. A binary hit is insufficient for volumetric RC and ray splitting through transparent surfaces.

### Surface versus medium

An SDF describes a boundary. A participating medium needs density or extinction, emission, and a phase function. Keep those as authored fields or volume components. The SDF can bound the medium, skip empty regions, and terminate at opaque surfaces, but it should not be used as a substitute for the medium integral.

### Hybrid raster and SDF rendering

A practical hybrid renderer can rasterize meshes into a G-buffer, add SDF primitives to a separate visibility structure, and let the interval tracer query both. The merge layer should not care whether a hit came from the depth buffer, a BVH, or an SDF. During development, a simpler first version can use the raster G-buffer for probe placement and an SDF-only tracer for an authored test scene.

Interpolation must reject or downweight samples that cross:

- Large depth or world-position differences.
- Opposing or sharply different normals.
- Different material or medium IDs.
- An SDF sign boundary or an opaque/transparent classification change.
- A motion-vector or temporal validity mismatch.

Sparse interpolation should renormalize over neighbors that actually exist. Renormalization prevents holes from becoming black, but it must be combined with the guards above or it can brighten across a wall.

### Invalidation

Invalidate or reproject probe data when any of these change materially:

- SDF primitive transform, topology, bounds, or smooth-union parameter.
- Material albedo, emission, transparency, or medium coefficients.
- Probe scale, cascade boundaries, world origin, or clipmap region.
- Camera projection or a depth layer used for screen probes.

Temporal accumulation can hide invalidation bugs for many frames, so a debug mode should expose age, sample count, and tracer failure reason.

## 6. Systems that combine well, and systems that fight

### Strong combinations

| Combination | Why it works | Conditions |
| --- | --- | --- |
| Compute + sparse RC | Dispatches can update hash/brick storage in parallel and keep extend, merge, and denoise separate | Atomics, barriers, clearing, and collision policy must be explicit |
| Ray splitting + SDF | SDF hit distance directly selects the useful cascade interval | The distance bound must be conservative and material hits must be stable |
| Screen probes + world-space SDF or BVH intervals | Existing raster visibility seeds the field while world tracing supplies off-screen transport | G-buffer data, disocclusion handling, and a bounded tracer are required |
| Raster G-buffer + SDF proxy geometry | Meshes retain the fast primary path while SDFs cover authored primitives or effects | Both paths use the same units, materials, and visibility rules |
| HRC + volumetric density | HRC interval algebra can carry fluence and optical depth through a medium | Merge before interpolation and keep phase-function assumptions explicit |
| RC + optional temporal reprojection | History reduces repeated tracing and stabilizes sparse updates | Reset on camera, SDF, material, and cascade changes; expose ghosting in tests |
| Surfel storage + sparse RC | Persistent surface records avoid allocating an empty volume for surface-only GI | Surfel maintenance must keep coverage and normals current |

### Combinations that usually fight

| Combination | What goes wrong |
| --- | --- |
| Dense full-resolution world volume + high-resolution 3D SDF | Cubic storage is multiplied by expensive sphere tracing in cells that may never contribute |
| Screen-only probes + screen-only tracing + off-screen lighting goals | Off-screen emitters, occluders, and geometry behind the depth buffer are absent from the data |
| Sparse probes + unguarded trilinear interpolation | Missing neighbors and thin walls produce holes, darkening, or light leaks |
| HRC or polygonal angular storage + mirror transport | Sparse angular boundaries cannot represent a narrow glossy lobe reliably; moire and temporal shimmer are likely |
| Linespace cache + rapidly deforming SDF topology | The cache's coherent-line assumption collapses and invalidation approaches a rebuild |
| CPU orchestration + many high-resolution per-ray dispatches | Submission, synchronization, and readback can cost more than the saved arithmetic |
| Visual SDF gradient reused as a physics collider | Rendering approximations, smooth unions, and scale-dependent bias do not define stable gameplay collision |

## 7. What gets better or worse

The following matrix is a design forecast, not a universal benchmark. "Good" means the representation has a natural path to the goal; it does not mean the result is free.

| Goal | Traditional 3D grid | Screen + world intervals | Sparse / split RC | Dense volume | HRC / polygonal |
| --- | --- | --- | --- | --- | --- |
| Diffuse indirect light | Good in small scenes | Very good first step | Best scaling candidate | Good but costly | Good in 2D; unproven in 3D |
| Contact shadows | Needs a short local pass | Excellent with a screen gather | Good if base spacing is fine | Overkill | Sharp in 2D, specialized |
| Large off-screen emitters | Good | Good | Good | Excellent | Depends on world embedding |
| Hard shadow edges | Ringing/interpolation fixes | Short world intervals help | Visibility guards needed | Accurate at high cost | HRC can preserve edges in 2D |
| Glossy/specular | Angular memory grows quickly | BVH/RT fallback often needed | Low angular bins bias highlights | Very expensive | Poor first target |
| Multiple diffuse bounces | Feedback or extra passes | Temporal or extra cascades | Natural cache feedback | Natural but costly | Demonstrated mainly in 2D |
| Participating media | Needs density field | World interval marcher | Requires occupied medium probes | Most natural | Volumetric HRC is promising |
| Dynamic SDF topology | Invalidate grid regions | Reconstruct visible probes | Invalidate sparse cells and history | Rebuild/clear expensive | Polygon/line caches fragile |
| Predictable memory | Poor at high 3D resolution | Mostly screen-sized | Depends on occupancy | Poor | Depends on angular topology |
| Cross-platform fallback | Fragment path is broadest | Requires compute only for some designs | Usually compute-first | Usually compute-first | Experimental shader support |

RC often improves noisy Monte Carlo GI by replacing random paths with structured reuse. It can make a renderer worse when the signal has high-frequency glossy detail, when interpolation crosses a thin occluder, or when the base spacing is too coarse for authored geometry. The right comparison is therefore against the existing goal and error budget, not against a hypothetical perfect path tracer.

## 8. Fit for c-blueprints

The repository already contains two separate rendering concerns:

- The desktop 2D path builds scene, emission, absorption, and albedo textures, then runs its RC passes and composite. See [src/render/material_raster.c](../../src/render/material_raster.c) and [src/render/radiance.c](../../src/render/radiance.c).
- The 3D path collects ECS geometry, lights, particles, and trails and submits depth-tested meshes through src/render/arena3d.c (`src/render/arena3d.c`; historical repository reference) and src/render/shaders/arena3d.frag (`src/render/shaders/arena3d.frag`; historical repository reference). It does not yet expose a public 3D G-buffer or indirect-light target.

The existing SDF code is primarily 2D coverage support, while 3D authored content is represented by analytic or canned meshes such as boxes, spheres, capsules, cones, and prisms. That makes a new 3D RC experiment a renderer subsystem, not a small extension of the current 2D radiance pass.

### Recommended sequence

#### Phase 0: establish the query seam

Add a renderer-private segment query abstraction with the semantics in the SDF contract above. Implement one SDF scene and one mesh/BVH scene behind it. Keep materials and emission authored in the existing asset model. Add debug output for hit distance, normal, material ID, transmittance, and sphere-trace failure.

Success criteria:

- The same interval merge code consumes mesh and SDF hits.
- A fixed test scene produces identical hit classifications when geometry is represented by either backend.
- No gameplay or physics code depends on the visual SDF.

#### Phase 1A: PoE2-style screen-only control

If an authored scene has a bounded or mostly fixed camera, implement the cheap control path first. Use screen probes, screen-space depth/radiance marching, cascade-specific resolutions, fixed loop counts, and a packed visibility mask. Do not call this the general 3D solution. Its job is to measure the payoff from PoE2's constraints and to make the later world-space cost visible.

Record failures for off-screen emitters, behind-depth occluders, camera cuts, and thin geometry. Those are expected behavior for this control path.

#### Phase 1B: screen probes with world intervals

Add depth, world position, normal, albedo, emission, material ID, and motion validity to a 3D G-buffer. Seed probes from visible pixels or tiles, trace world-space intervals through the segment query, and composite diffuse indirect light after the primary mesh pass and before post-processing. Keep a short screen-space gather for contact detail only if measurements justify it.

This phase has the smallest general-purpose architectural jump and directly exercises off-screen SDF visibility. The screen-only control gives a lower-cost reference against which sparse and split methods can be judged.

#### Phase 2: sparse ray-splitting comparison

Reuse the segment query and surface seed list. Add a compute-based sparse hash or brick pool, distance-based cascade assignment, atomic accumulation, renormalized guarded interpolation, and explicit invalidation. Compare dense screen/world storage with the sparse path on the same scenes and camera paths.

The comparison should include:

- Same base probe spacing and angular budget.
- Same SDF step limits and material model.
- Single-frame and temporal modes.
- Thin walls, small emissive details, large off-screen lights, and camera cuts.

#### Phase 3: volumetric or HRC branch

Only add a dense volume or Volumetric-HRC branch when a real authored scene needs fog, smoke, or a 2D/flatland effect. Keep medium coefficients separate from the SDF and validate merge-before-interpolation ordering. A 3D HRC branch should be treated as a research prototype with a clear exit criterion rather than as the default GI path.

### Measurement gates

The repository's 3D rendering research sets a useful engineering bar in [docs/research/arena-3d-rendering.md](arena-3d-rendering.md): use an M4 Pro-class target at 1920×1080, aim for p95 at or below 16.7 ms and p99 at or below 20 ms, and admit GI only if it is useful within roughly a 2.5 ms GPU p95 budget. Treat those as project gates, not claims about RC implementations.

Measure:

- GPU time by cascade, trace, merge, interpolation, composite, and synchronization.
- Probe count, hash occupancy, atomic contention, and sample age.
- SDF steps, early exits, bounds misses, and step-exhaustion failures.
- Light leaks across thin walls and disocclusion recovery after camera motion.
- Memory for base level, all cascades, history, and transient ping-pong storage.
- Quality at a fixed frame time, including any upscaling choice.

## 9. GitHub implementation catalog

| Repository | Focus | License or status | What it is good for |
| --- | --- | --- | --- |
| [Raikiri/RadianceCascadesPaper](https://github.com/Raikiri/RadianceCascadesPaper) | Formal RC paper and examples | MIT; research reference | Derivation, scaling, interval merge, artifact fixes |
| [CodyJasonBennett/three-rc](https://github.com/CodyJasonBennett/three-rc) | 3D screen probes plus BVH | No usable license shown | Screen/world hybrid architecture and off-screen visibility ideas |
| [uziiDevelopment/providentia-cascades](https://github.com/uziiDevelopment/providentia-cascades) | Rust/wgpu compute 3D demo | Check repository terms | Ping-pong atlases, per-cascade dispatch, multibounce feedback |
| [entropylost/chandra](https://github.com/entropylost/chandra) | Sparse Split RC reference | Check repository terms | Sparse hash, trace/splat/merge flow, denoising |
| [wroughtinator/split-radiance-cascades-webgpu](https://github.com/wroughtinator/split-radiance-cascades-webgpu) | WebGPU sparse Split RC | Check repository terms | Software BVH, invalidation, validation scenes; third-party extensions |
| [alexmalyutindev/unity-urp-radiance-cascades](https://github.com/alexmalyutindev/unity-urp-radiance-cascades) | Unity 2D/3D and voxel experiments | MIT; WIP | Direction-first, 3D probe, cubemap, and compute variants |
| [entropylost/amitabha](https://github.com/entropylost/amitabha) | HRC sample and variants | Check repository terms | HRC layout, radial ideas, hard-shadow and volume experiments |
| [Yaazarai/Volumetric-HRC](https://github.com/Yaazarai/Volumetric-HRC) | Volumetric HRC | Unlicense | Ray extensions, frustum averaging, merge-before-interpolate rule |
| [Raikiri/LegitCascades](https://github.com/Raikiri/LegitCascades) | Polygonal HRC / Linespace scripts | MIT | Polygon extension and gather experiments |
| [IceKontroI/sparse_rc_2d](https://github.com/IceKontroI/sparse_rc_2d) | Sparse 2D RC proof of concept | MIT/Apache-2.0 | Occupancy layouts, slab chains, interpolation pitfalls |
| [s-macke/CoreCascade](https://github.com/s-macke/CoreCascade) | Go RC with experimental 3D SDF renderer | MIT | Small CPU implementation and SDF scene representation |
| [NorthRocStudios/DivisionEngine](https://github.com/NorthRocStudios/DivisionEngine) | C#/ComputeSharp analytic SDF engine | GPL-3.0; preview/experimental | Compute SDF raymarching, checkerboard dispatch, object-specific normals, and material lookup patterns |
| [tmpvar/radiance-cascades-3d-grid](https://github.com/tmpvar/radiance-cascades-3d-grid) | Dense world-space 3D grid | MIT; rough/unoptimized | World-probe storage sketch |
| [CptNemo0/RadianceCascades](https://github.com/CptNemo0/RadianceCascades) | C++ OpenGL 2D thesis app | Check repository terms | Classic versus cached/path-traced comparisons and profiling |
| [mxcop/src-dgi](https://github.com/mxcop/src-dgi) | Surfel RC | MIT; archived | Surface-patch maintenance and RC data flow |
| [software-mansion/TypeGPU](https://github.com/software-mansion/TypeGPU/tree/main/packages/typegpu-radiance-cascades) | TypeScript/WebGPU compute RC | Check package terms | Explicit SDF and custom rayMarch callback contract |
| [kornelski/bevy_flatland_radiance_cascades](https://github.com/kornelski/bevy_flatland_radiance_cascades) | Bevy/WGSL 2D RC | CC0 | Compact shader layout, pre-averaging, gear fix |
| [Hybrid46/RadianceCascade2DGlobalIllumination](https://github.com/Hybrid46/RadianceCascade2DGlobalIllumination) | GLSL 330 screen-space 2D | Check repository terms | JFA distance field feeding a conventional RC pass |
| [tlegoc/RadianceCascadesGIVulkan](https://github.com/tlegoc/RadianceCascadesGIVulkan) | Vulkan 2D RC | Check repository terms; TODOs remain | Vulkan resource and profiling skeleton |

Licenses in this table are a reading aid, not legal advice. Confirm the current repository license and dependency terms before incorporating code. Experimental repositories can be more useful as algorithm notebooks than as production dependencies.

## 10. Research gaps and decision risks

- There is no apples-to-apples public benchmark across dense, screen/world, sparse, split, and HRC 3D paths. Resolution, angular budget, tracing backend, temporal history, and scene complexity vary too much.
- The formal RC paper gives a 3D construction, but most polished public demos remain 2D or 2.5D. A 3D implementation should be judged by its data layout and visibility backend, not by the RC name alone.
- Sparse interpolation is still the main quality risk. Occupancy, visibility, normal, material, and temporal guards should be considered part of the algorithm.
- HRC, polygonal HRC, and Linespace have interesting sharp-feature behavior but lack a shared 3D representation and a stable production cost model.
- Hardware ray tracing can make world intervals accurate, but it changes the cost comparison enough that a screen tracer, SDF marcher, BVH, and RT path must be measured separately.
- SDF quality is a renderer input constraint. Bad bounds, non-conservative distance fields, or inconsistent transforms can make any RC family appear unstable.
- A useful implementation should expose enough counters to distinguish RC error from tracer failure, sparse allocation failure, interpolation leakage, and temporal ghosting.

## Sources

### Radiance Cascades and HRC

- [Radiance Cascades paper (arXiv HTML)](https://arxiv.org/html/2408.14425)
- [Radiance Cascades paper repository](https://github.com/Raikiri/RadianceCascadesPaper)
- [Radiance Cascades project hub](https://radiance-cascades.com/)
- [Grinding Gear Games: ExileCon 2023 rendering talk](https://www.youtube.com/watch?v=TrHHTQqmAaM)
- [GM Shaders: Radiance Cascades](https://mini.gmshaders.com/p/radiance-cascades)
- [GM Shaders: Radiance Cascades 2](https://mini.gmshaders.com/p/radiance-cascades2)
- [Jason McGhee: RC](https://jason.today/rc)
- [Jason McGhee: GI](https://jason.today/gi)
- [tmpvar: Radiance Cascades demos](https://tmpvar.com/poc/radiance-cascades/)
- [Holographic Radiance Cascades paper](https://arxiv.org/abs/2505.02041)
- [HRC author project page](https://adrianmargel.ca/projects/holographicRadianceCascades/)

### Public implementations

- [three-rc](https://github.com/CodyJasonBennett/three-rc)
- [providentia-cascades](https://github.com/uziiDevelopment/providentia-cascades)
- [Split Radiance Cascades paper](https://arxiv.org/abs/2607.20384)
- [Chandra Split RC](https://github.com/entropylost/chandra)
- [Split RC WebGPU experiment](https://github.com/wroughtinator/split-radiance-cascades-webgpu)
- [Unity URP Radiance Cascades](https://github.com/alexmalyutindev/unity-urp-radiance-cascades)
- [Amitabha HRC](https://github.com/entropylost/amitabha)
- [Volumetric-HRC](https://github.com/Yaazarai/Volumetric-HRC)
- [LegitCascades / Polygonal HRC](https://github.com/Raikiri/LegitCascades)
- [Legit Script Editor](https://radiance-cascades.github.io/LegitScriptEditor/?gh=Raikiri/LegitCascades/PolygonalHRC45.ls&rev=master)
- [sparse_rc_2d](https://github.com/IceKontroI/sparse_rc_2d)
- [CoreCascade](https://github.com/s-macke/CoreCascade)
- [DivisionEngine](https://github.com/NorthRocStudios/DivisionEngine)
- [DivisionEngine README](https://github.com/NorthRocStudios/DivisionEngine/blob/master/README.md)
- [DivisionEngine license](https://github.com/NorthRocStudios/DivisionEngine/blob/master/LICENSE.txt)
- [DivisionEngine SDF compute shader](https://github.com/NorthRocStudios/DivisionEngine/blob/85d5371c963e63a28e74fd892499d8c5de6142bc/DivisionEngine.Core/Rendering/SDFShader3D.cs)
- Mike Turitzin, [I'm making a game engine based on dynamic signed distance fields (SDFs)](https://www.youtube.com/watch?v=il-TXbn5iMA), 2026-01-06.
- [tmpvar 3D grid](https://github.com/tmpvar/radiance-cascades-3d-grid)
- [mxcop/src-dgi](https://github.com/mxcop/src-dgi)
- [TypeGPU RC documentation](https://docs.swmansion.com/TypeGPU/ecosystem/typegpu-radiance-cascades/)
- [TypeGPU RC source](https://github.com/software-mansion/TypeGPU/tree/main/packages/typegpu-radiance-cascades/src)

### Linespace and adjacent representations

- [Bala, Dorsey, and Teller: line-space paper](https://people.csail.mit.edu/teller/pubs/BalaDorseyTellerEGWR1999.pdf)
- [Cornell interactive ray-tracing lecture notes](https://www.cs.cornell.edu/courses/cs665/2004fa/Lectures/Lec11_InteractiveRayTracing_web_6page.pdf)
- [Nihil line-function HRC experiment](https://github.com/entropylost/nihil)
- [Surfel maintenance article](https://m4xc.dev/articles/surfel-maintenance/)

### Repository-specific context

- [c-blueprints 3D rendering research](arena-3d-rendering.md)
- [c-blueprints radiance README](../../src/render/README.md)
- c-blueprints 3D renderer (`src/render/arena3d.c`; historical repository reference)
