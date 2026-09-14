# Path of Exile 2 Radiance Cascades and Performance Techniques

## Executive answer

Yes, the public material explains the key reason Radiance Cascades works well in Path of Exile 2: PoE2 uses a flatland style radiance cascade hierarchy in screen space, populated by screen space ray marching. The choice is tied to the game's fixed or strongly constrained camera. This avoids the storage and visibility cost of a full 3D radiance volume and lets the implementation exploit image space data and regular access patterns.

The author has also disclosed two unusually concrete implementation details:

1. Four rays are marched simultaneously.
2. Occlusion is packed into a 128 bit mask, representing 128 binary visibility samples per radiance cascade texel.

The author's WIP paper reports approximately 3 ms on a GTX 1050 for the PoE2 example. In the ExileCon Q&A, the same implementation is described more casually as costing about 1 ms. Neither number has a published resolution, complete pass breakdown, driver version, or independent reproduction, so they should be treated as author reported measurements rather than a portable benchmark.

The public record does not disclose the shader source, exact render resolution, cascade count, texture formats, workgroup or wave layout, ray step schedule, or whether every PoE2 platform uses exactly the same path. There is no public PoE2 renderer repository to inspect. The useful conclusion is therefore a design pattern rather than a line by line reproduction:

> Use a fixed cost, screen space cascade schedule; make the first, highest detail cascade the only expensive part; traverse visibility in small packets; store visibility compactly; and accept screen space's missing world geometry when the camera and game design make that trade acceptable.

## What the primary sources establish

| Claim | Public evidence | Confidence |
| --- | --- | --- |
| PoE2 uses radiance cascades for global illumination | Alexander Sannikov identifies the PoE2 GI solution in the WIP paper and in the ExileCon 2023 talk | High |
| The hierarchy is flatland like and stored in screen space | The paper's PoE2 section describes a hierarchy of screen space radiance probe cascades with flatland encoding | High |
| The cascades are populated with screen space ray marching | Explicitly stated in the paper | High |
| The camera constraint is a deliberate optimization | The paper cites PoE2's fixed camera; in the talk Q&A Sannikov says the engine can exploit the fact that relevant lights are on screen | High |
| Four rays are marched together | Explicitly stated in the WIP paper | High |
| Occlusion is stored in a 128 bit mask | Explicitly stated in the WIP paper | High |
| Approximate cost is 3 ms on GTX 1050 | Figure caption in the WIP paper; the example is a glowing character and does not define a full frame budget | Medium |
| Approximate cost is about 1 ms | Informal ExileCon Q&A answer; no exact benchmark context | Low to medium |
| Cost is independent of the number of visible lights or objects | Sannikov makes this claim in the Q&A and the RC analysis motivates fixed scene independent work | Medium as a design property, low as an independently reproduced PoE2 measurement |
| PoE2's GI is single frame and does not require temporal reuse | The paper says its applications explicitly do not rely on temporal reuse; this is an author statement, not a public PoE2 implementation test | Medium |

The primary implementation description is in the paper's [Path of Exile 2 GI section](https://github.com/Raikiri/RadianceCascadesPaper/blob/main/RadianceCascades.tex#L2359-L2389). The presentation is the official [ExileCon 2023 Rendering Path of Exile 2 talk](https://www.youtube.com/watch?v=TrHHTQqmAaM); the GI discussion begins around 27 minutes and the implementation discussion is around 30 to 39 minutes.

## How the PoE2 design gets its performance

### 1. It reduces the dimensionality before it applies Radiance Cascades

A full 3D radiance field needs position in three dimensions plus direction. Even though the cascade hierarchy reduces the asymptotic cost, the first 3D probe grid still has a cubic storage term. The PoE2 implementation instead works from the visible image and uses a 2D, flatland style hierarchy. Every sample can use the depth, color, normal, and direct lighting information already present in the renderer's screen buffers.

The fixed camera is the enabling product decision. In the ExileCon Q&A, Sannikov explains that the engine does not need to calculate lighting from behind the camera because the camera model and game view make the relevant lights visible. The WIP paper makes the same connection: screen space ray marching is selected because PoE2 has a fixed camera.

This is more than an implementation detail. It is a scope restriction that removes entire classes of work:

- no world space probe placement over the whole level;
- no 3D occupancy or voxelization for the GI path;
- no ray queries against all hidden geometry;
- no need to preserve indirect light from a source that has left the view;
- no need to solve arbitrary view changes at the same quality.

The trade is equally clear. The paper states that PoE2's screen space input cannot receive indirect light from offscreen emitters, cannot see invisible occluders, and has no geometry behind the depth buffer. The limitation comes from how the intervals are calculated, not from the fact that the cascade textures happen to be stored in screen space.

### 2. It spends most of the budget on the first cascade

The cascade schedule follows the penumbra observation:

- nearby contributions need many spatial probes and relatively few directions;
- farther contributions need fewer probes and more directions;
- each later cascade covers a longer interval with lower spatial cost.

For the flatland scaling used to explain PoE2, halving the number of probes along each image dimension reduces the probe count by four while doubling the directional samples. The stored interval count and work therefore fall geometrically. Merging a high cascade into the lower one is a small local operation, averaging a few source texels and combining radiance intervals.

The talk's visual explanation makes the performance shape explicit: the first cascade is the expensive one; later cascades become exponentially cheaper. The Q&A says the grid hierarchy is fixed and independent of scene complexity, so one fireball and many fireballs do not change the planned amount of work. That is a useful property for an action game because an effect heavy encounter does not create an unbounded GI workload.

The paper also warns that asymptotic scaling hides the real constant. A high detail cascade zero can already cost milliseconds, and the choice of its probe spacing, angular spacing, and interval range requires tuning. In practice, the first cascade resolution is the most important performance control.

### 3. It turns many visibility tests into a packet operation

The WIP paper says PoE2 does not cast one independent ray for every radiance interval. It marches four rays simultaneously and records occlusion in a 128 bit mask. The paper equates the mask to 128 binary occlusion rays per radiance cascade texel.

The exact shader is private, but the likely performance mechanism is clear:

- four related rays share screen coordinate math, depth reconstruction, and loop control;
- visibility can be accumulated as bit operations instead of a large array of scalar values;
- a later merge or gather can use the mask as a compact angular coverage representation;
- the expensive part is paid once per packet or per sample step instead of once per conceptual ray.

This should not be confused with the claim that 128 full radiance values fit in one texel. The public description specifically calls the mask binary occlusion. The associated radiance or direct lighting values still require their own representation.

### 4. It uses deterministic reuse rather than noisy temporal recovery

The paper argues that sharing radiance intervals across spatial scales can produce a sufficiently accurate result without relying on temporal reuse. It says the applications in the paper react immediately to scene, viewport, and lighting changes. That is consistent with the PoE2 design goal: dynamic combat lighting should not wait for a history buffer to converge.

This does not prove that every production path in every PoE2 renderer setting is entirely history free. It does establish that the core RC result is intended to be a single frame reconstruction. A temporal pass can still be useful as an optional quality or bandwidth trade, but adding it changes the behavior: moving emitters and camera changes can leave lag or ghosting.

### 5. The output is a directional field rather than a scalar irradiance

The paper shows diffuse and specular examples and explains that a directional radiance field can be convolved with a BRDF or BSDF. This helps explain why the PoE2 demonstration looks useful beyond a blurred ambient term: normal maps can select directional lighting, and rough materials can gather a wider cone.

There is a cost distinction here. Diffuse lighting can merge the cascades and integrate a few broad directions. Glossy or mirror like lighting needs a narrower cone query and more directional fidelity. A first implementation should therefore measure diffuse GI separately from the optional specular gather.

## Performance techniques worth trying

The following list distinguishes techniques that are directly supported by the PoE2 evidence from experiments that are reasonable extrapolations.

| Technique | Evidence level | Expected result | Main risk |
| --- | --- | --- | --- |
| Fixed screen space cascade grid | Directly described for PoE2 | Predictable cost and regular memory access | Offscreen light and hidden geometry disappear |
| Four ray traversal packet | Directly described for PoE2 | Shares coordinate work and loop control | Packet divergence at depth discontinuities |
| Compact visibility mask | Directly described for PoE2 | Lower storage and cheap angular coverage operations | Banding, mask pressure, and unclear 128 bit layout |
| Make cascade zero the primary quality knob | Follows directly from the paper's cost analysis | Large performance wins with a simple resolution control | Contact lighting loses detail first |
| Fixed loop bounds and fixed cascade count | Directly consistent with the talk's constant cost claim | Avoids worst case spikes and dynamic branching | Wastes work in empty or simple regions |
| Inline local merges while producing the cascade | Engineering inference | Fewer intermediate texture passes and reads | More register pressure and harder debugging |
| Half or reduced resolution GI working image | Common screen space optimization; not disclosed as PoE2 detail | Reduces the dominant cascade zero cost | Contact shadows and thin geometry become unstable |
| 32, 64, and 128 bit mask variants | Supported by independent bitmask research | Finds the best ALU, bandwidth, and quality point for the target GPU | PoE2's 128 bit result may depend on a different packet layout |
| Use a small SDF or BVH fallback only for invalid screen samples | Engineering inference | Repairs screen edges and disocclusions without paying world tracing everywhere | Fallback paths can reintroduce divergent, scene dependent work |
| Interval extension on a regular world grid | Explicitly described for regular grids in the paper | Replaces long linear ray marching with short marching plus logarithmic extension | Does not directly apply to 3D screen probes with per probe depth |
| Temporal reuse as an optional lower quality mode | Paper describes reprojection for another world interval implementation | Reduces work or increases convergence when latency is acceptable | Ghosting and slow response in combat |

### The mask width deserves a real benchmark

The 128 bit mask is one of the most tempting PoE2 details to copy blindly. An independent visibility bitmask paper found that 32 sectors fit in one unsigned integer, while a 128 sector version required four integers and vector instructions; its measured 128 sector variant was roughly 5 to 10 percent slower than the 32 sector variant in that experiment. That paper is not a description of PoE2, and PoE2 reports that its 128 bit mask is effectively free in its four ray scheme. The two observations are compatible if PoE2's mask is aligned with a four lane packet or benefits from a different GPU compiler and data layout.

The practical experiment is straightforward:

1. Keep the cascade schedule and ray step count fixed.
2. Compare 32, 64, and 128 visibility sectors.
3. Measure GPU time, register count, occupancy, memory traffic, and edge quality.
4. Run on the lowest target GPU as well as a desktop development GPU.

Do not infer that a wider mask is always better. The mask needs enough angular sectors to avoid banding, but any extra width must earn its bandwidth and instruction cost.

### Keep work fixed before making it adaptive

The talk's strongest production claim is constant cost. An adaptive ray count or adaptive cascade count can save time in simple scenes, but it can also cause wave divergence and make performance depend on the number and arrangement of visible effects. Start with fixed loop bounds, fixed cascade count, and a fixed working resolution. Add local adaptation only after profiling shows that the fixed baseline misses the frame budget.

If adaptation is added, prefer coarse decisions that preserve packet coherence:

- choose one resolution or cascade policy per tile;
- use a depth complexity or motion category per tile;
- keep neighboring pixels on the same ray step schedule;
- cap every fallback and record how often it is used.

Per pixel early exits are attractive in shader code but can be worse on a wavefront when only a few lanes terminate.

### Do not pay world space cost for a screen space problem

The paper's own comparison provides a strong warning. A screen space probe hierarchy with world space intervals can recover offscreen lighting, and its intervals may be generated by SDF marching, software ray tracing, or hardware ray tracing. But the author reports that this arrangement works one to two orders of magnitude less efficiently than the PoE2 screen space ray marcher. A separate SDF world space demo takes roughly 30 to 50 ms per frame on an RTX 3060 and needs temporal accumulation to converge new views.

That does not make SDF integration useless. It changes where SDF belongs:

- use screen space as the fast path for visible surfaces;
- use SDF or BVH tracing for a bounded fallback set;
- use a world space sparse or split cascade only if offscreen light is a core requirement;
- do not make every PoE2 style screen ray call a general SDF evaluator.

For a regular world space probe grid, the paper describes an interval extension trick: march a short interval, then repeatedly extend the interval exponentially. This can make the ray marching time acceptable, but the probe zero storage remains expensive. It is a useful prototype optimization for a small bounded scene, not a replacement for the PoE2 screen space design.

## A practical PoE2 style pipeline for c-blueprints

For a first engine experiment, the closest defensible reproduction is:

1. Render a working image containing depth, world or view normal, direct lit color, emission, and material or validity data.
2. Allocate a fixed flatland style cascade hierarchy over that image.
3. Generate the short, high resolution cascade with four ray packets and a compact visibility mask.
4. Generate longer cascades at lower probe density and higher angular density.
5. Merge from the far cascade toward cascade zero with fixed bounded loops.
6. Gather diffuse irradiance at visible surfaces.
7. Add an optional glossy cone gather only after diffuse cost and image quality are understood.
8. Mark samples invalid at screen edges, depth discontinuities, and disocclusions.
9. Add either ambient fallback or a small, separately metered SDF or BVH fallback for invalid samples.

The data contract for an SDF fallback should be a bounded segment query, not a general purpose ray marcher embedded in the cascade shader:

~~~text
trace_segment(origin, direction, near_distance, far_distance)
    -> hit or miss
    -> hit distance
    -> surface normal and material
    -> emission or direct-light value
    -> optional transmittance
~~~

This keeps the RC code independent of the visibility backend. Screen depth, mesh BVH, and SDF tracing can satisfy the same interval query while preserving the PoE2 fast path.

## What combines well, and what conflicts

### Strong combinations

- Flatland screen cascades plus a fixed or constrained camera.
- Screen cascades plus a G-buffer containing direct lighting and normals.
- Four ray packets plus a bitmask visibility representation.
- Fixed cascades plus a separate low resolution direct light or emission buffer.
- Diffuse RC plus normal mapping, where a few broad incoming directions are sufficient.
- Screen RC plus a tightly capped SDF or BVH fallback at screen edges.
- Regular world probes plus interval extension, when the world volume is small and bounded.

### Weak combinations

- Dense 3D probe cascades plus a large streamed world. The cascade hierarchy may be efficient after cascade zero, but cascade zero still scales with the volume.
- Full world space SDF marching for every screen probe when the desired result is only visible screen lighting. The public SDF demonstration is far above the PoE2 budget.
- Temporal accumulation as the only way to hide inadequate spatial or angular sampling. It creates responsiveness and ghosting problems in a dynamic action game.
- Per pixel adaptive ray counts inside a four ray packet. The saved samples can be offset by divergence.
- Screen space RC as the sole GI solution for unrestricted camera rotation, large camera zoom changes, or important offscreen emitters.
- High quality glossy RC added before diffuse and visibility errors are measured. Narrow specular cones expose interpolation and screen space holes quickly.

## Recommended measurement matrix

The following benchmark would identify whether a PoE2 style path is helping the engine:

| Variable | Values |
| --- | --- |
| Working resolution | Full, two thirds, half |
| Cascade zero probe spacing | 1, 2, and 4 image pixels |
| Cascade count | 3, 4, 5, and 6 |
| Visibility mask | 32, 64, and 128 sectors |
| Ray packet | Scalar, two ray, and four ray |
| Visibility backend | Depth buffer, SDF fallback, BVH fallback |
| Fallback share | 0, 1, 5, and 10 percent of queried intervals |
| Scene motion | Static, moving camera, moving emissive, moving occluder |
| Output | Diffuse only, diffuse plus glossy cone |

Record p50, p95, and p99 GPU time; texture reads and writes; register count; occupancy; wave divergence; mask operations; fallback count; and memory use. Image checks should include thin geometry, screen edge movement, offscreen emissive objects, small bright sources, contact shadows, and rapidly moving lights.

The most informative first comparison is not against a path tracer. It is against the renderer's current direct lighting plus a simple ambient or bent normal term, using the same working resolution and frame budget. A PoE2 style RC implementation has succeeded if it adds stable directional bounce lighting without becoming a new source of encounter dependent frame spikes.

## Evidence limits

The PoE2 renderer is proprietary. The paper and talk expose the representation and several performance ideas, but they do not expose enough information to reproduce the exact implementation. In particular, the public sources do not establish:

- the exact screen buffer formats;
- whether the public 3 ms number includes all cascade generation, merging, and final gather;
- the render resolution or dynamic resolution policy used for that number;
- whether the 128 bit mask is four 32 bit words, two 64 bit words, or another packed representation;
- how ray directions are laid out in memory;
- the number of cascade levels in production;
- how screen edge, disocclusion, and thin geometry cases are handled;
- whether different hardware paths use different mask widths or shaders.

Those gaps are where implementation folklore tends to become overconfident. The safe approach is to reproduce the disclosed structure, expose the unknowns as tunable parameters, and benchmark the whole pass on the intended hardware.

## Sources

1. Alexander Sannikov, "Radiance Cascades: A Novel Approach to Calculating Global Illumination" (WIP source, especially the Path of Exile 2, scaling, world space, and limitations sections). [GitHub source](https://github.com/Raikiri/RadianceCascadesPaper/blob/main/RadianceCascades.tex).
2. Alexander Sannikov, "ExileCon 2023: Rendering Path of Exile 2," official Path of Exile YouTube channel, November 2023. [Video](https://www.youtube.com/watch?v=TrHHTQqmAaM).
3. Christopher M. J. Osborne and Alexander Sannikov, "Radiance Cascades: A Novel High-Resolution Formal Solution for Multidimensional Non-LTE Radiative Transfer," arXiv:2408.14425. [Paper](https://arxiv.org/abs/2408.14425) and [HTML text](https://arxiv.org/html/2408.14425).
4. Olivier Therrien, Yannick Levesque, and Guillaume Gilet, "Screen Space Indirect Lighting with Visibility Bitmask," arXiv:2301.11376. [Paper](https://arxiv.org/abs/2301.11376) and [HTML text](https://arxiv.org/html/2301.11376).
5. Alexander Sannikov, "Sparse 3d Radiance Cascades," author video, November 2025. This is a later world space experiment rather than the PoE2 implementation, but it is useful evidence about the author's direction after the screen space solution. [Video](https://www.youtube.com/watch?v=TGLAxW0xVDU).
