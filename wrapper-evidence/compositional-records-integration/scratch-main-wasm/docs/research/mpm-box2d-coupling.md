# MPM, Box2D, and fog/liquid coupling

## Recommendation

Keep Box2D responsible for projectiles, terrain contacts, and enemy rigid-body
collisions. A bounded CPU MLS-MPM/APIC experiment is reasonable for the liquid
in the fog-plus-liquid workload, especially if the current cohesion model cannot
produce the desired split/rejoin behavior or body reaction. For visual fog,
first compare a separately advected optical-density field with particle splats;
MPM is also a plausible experiment if the goal is coherent particle-grid motion
and momentum exchange with solid bodies, or if the fog represents inertial
particles such as dust or droplets. It still needs an appropriate pressure or
constitutive model plus explicit density, source, and dissipation transport.
MPM still needs a material model and a measured frame budget before adoption.

## Current integration seam

`physics_tick` steps Box2D once with a fixed delta and its configured
internal substep count, then calls `solve_fluid`. The fluid receives each
collider's previous/current pose, linear and angular velocity, inverse mass, and
circle or box obstacle proxies (polygon proxies currently become AABBs here).
Liquid-particle collisions accumulate an
equal-and-opposite **linear** impulse on dynamic bodies; `solve_fluid` applies
that impulse at each body's center after Box2D has stepped. The next Box2D step
consumes it, and no liquid torque is returned. The optional gas grid imposes
moving-boundary velocities and projects pressure, but does not integrate a
pressure reaction back into Box2D. See
[`physics.c`](../../src/runtime/scenes/physics.c),
[`material_fluid.h`](../../src/runtime/material_fluid.h), and
[`material_flow.c`](../../src/runtime/material_flow.c).

The current adapter also sets `fixedRotation` for dynamic bodies. Returning a
point impulse alone therefore cannot demonstrate angular response in today's
dynamic-body path. A rotating MPM probe needs an explicit optional authored
rotation capability in that adapter before the torque acceptance test can pass.
Keep the existing fixed-rotation behavior for scenes that rely on it.

The gas field is a sparse staggered velocity grid with advection, vorticity
confinement, and a 32-iteration pressure projection. Gas and liquid appearance
currently comes from particle splats into the material canvas before HRC. The
gas grid advects velocity and samples that flow back to tracer velocities; it
does not currently transport a separate optical-density scalar. A scalar field
is therefore a proposed fog-transport baseline, not behavior already provided
by the grid. An MPM prototype can preserve the rendering stages through an
adapter: the current raster accepts `material_fluid` particles and their optical
fields, not MPM mass or grid density. Converting mass into optical depth needs
an explicit mapping; a grid-density input would require a new raster path. The prior
[material/HRC research](../material-simulation-and-hrc-research.md) and
[gas-flow implementation notes](../neon-gas-flow.md) already identify MPM as a
liquid candidate and scalar advection as the closer match for fog.

## What MPM changes

MPM stores material state on particles, transfers mass and momentum to a
temporary Cartesian grid (P2G), evolves grid velocities and material forces,
transfers velocity back (G2P), then advects particles. MLS-MPM makes the
transfer compact; APIC adds a local affine velocity state that retains rotation
and shear and conserves angular momentum through particle/grid transfers. It
still needs a liquid constitutive model, active-grid management, and boundary
coupling. MLS-MPM/APIC transfers alone do not supply cohesion or surface tension;
the desired split/rejoin behavior needs an explicit model and tuning. The first-party [Taichi MPM code](https://github.com/yuanming-hu/taichi_mpm)
is useful as a reference, but its small 2D example uses `dt = 1e-4` and renders
every `1e-3` seconds (10 solver steps per displayed frame); another first-party
88-line example runs 50 `2e-4` steps per rendered frame. Those examples are not
cost estimates for this game, but they warn against assuming one MPM update per
60 Hz tick will be stable or sufficient.

For two-way rigid coupling, pass body surface velocity to nearby grid nodes,
including rotation: `v + omega × (x - center)`. Measure the grid's momentum
change at the interface and return its opposite as a Box2D impulse at the
corresponding world point; summing `r × impulse` preserves the angular reaction.
Box2D's point-impulse API changes both linear and angular velocity. The MLS-MPM
paper's CPIC method couples rigid bodies in the same timestep using rigid
distance fields and impulses, and allows an external rigid solver. That is a
design precedent, not a ready-made Box2D adapter. Ordinary grid interpolation
can leak across thin walls; CPIC or an explicit solid-separated stencil is a
later requirement if the prototype crosses narrow cave geometry.

The smallest integration is loosely coupled: run MPM against interpolated
Box2D poses during the fixed gameplay tick, then queue the fluid's point
impulses for the next Box2D tick. It is compatible with the current phase order,
but has one-tick reaction latency and may lag or add energy for heavy, fast
interactions. Tighter coupling requires both solvers to exchange state at a
shared substep. Box2D exposes a world step plus internal solver substeps, not a
callback for an external fluid solver between those internal substeps. Calling
the whole Box2D world repeatedly at smaller deltas changes its contact/event
cadence and costs more broadphase work. Start loosely coupled; consider a
synchronized outer substep only if measured liquid/body tests expose visible
lag or unstable momentum exchange. See the [Box2D step and impulse guidance](https://box2d.org/documentation/md_simulation.html).

Keep destructible terrain's authored occupancy mask and Box2D shapes as the
gameplay authority. A fluid grid can derive a coarse solid mask or signed
distance field from that occupancy, rebuild only edited tiles, and rasterize
moving Box2D shapes with their boundary velocity. Never make a coarse MPM cell
decide whether a projectile damages terrain. For HRC, map MPM particle density
and material optical properties into the existing gas/liquid splats first;
only add a grid-density raster if the particle count or coverage quality makes
that a measured bottleneck. A scalar optical-density field advected with the
velocity grid is a simpler fog baseline; smoke methods transport density and
temperature along the flow. If the target fog motion depends on particle-grid
momentum transfer or two-way solid coupling, compare MPM as a second option and
still carry optical density as material state or a transported scalar. Keep
optional wisps or ballistic exhaust as particles.

## Bounded experiment against the four planned workloads

| Workload | MPM role | Acceptance gate |
| --- | --- | --- |
| Player fires many projectiles into terrain | None; retain Box2D and exact terrain-mask damage | Same swept-hit ordering, cuts, and contact trace; no MPM work added to this path |
| Player fires many projectiles into destructible enemies | None; retain Box2D rigid contacts and authored damage | Same projectile hits, enemy cuts/deaths, and event trace |
| Many destructible enemies collide | None; MPM does not solve rigid-rigid contacts | Same Box2D contacts and destruction behavior, with no extra MPM tick cost |
| Fog plus liquid | First compare scalar density advection; also test optional MLS-MPM/APIC for liquid and, if coherent particulate fog/body coupling is desired, fog | Fog keeps continuous wakes/coverage; liquid visibly improves cohesion/split/rejoin and returns bounded linear and angular body response |

Create a coupling variant of the fourth fixture with a deterministic seed,
one compact liquid region, a static bowl plus one dynamic probe, and bounded
active tiles with preallocated scratch. Compare matched liquid mass/particle count at two
populations and 2/4/8 MPM substeps per 1/60 s tick against the existing two
half-step relaxation solver. Start single-threaded on CPU; parallel particle
scatter needs deterministic tile ownership or worker-local accumulators and an
ordered reduction. Run the same CPU path in native and single-threaded Wasm;
do not require GPU compute or threaded-Wasm deployment for the prototype. The
fluid shader source includes a GLSL ES path that emulates its storage buffers
with texel fetches ([fluid shader](../../src/render/shaders/hrc/fluid_common.shd),
[raster](../../src/render/fluid_raster.c)); this supersedes the older plan's
claim that the fluid pass has no GLES fallback. Still verify the complete
fog/liquid fixture in the browser rather than inferring support from the solver
build alone. Report memory, active cells, solver time, complete-frame
p50/p95/p99, and momentum/torque drift.
Only call it a performance optimization if it lowers complete-frame p95 at
matched quality. Otherwise, consider it only as an explicitly valued liquid
quality feature that stays within the frame budget. Repeat all four workloads
on native and Wasm to confirm the new optional path does not perturb the three
Box2D-only fixtures.

The current 20k contact-heavy arena measurement shows why those first three
workloads need their own rigid-physics work: across three native Release runs,
median physics p50/p95 was 13.256/21.100 ms with 19,981 bodies and 120,292 pair
candidates; Box2D solve and pair collection medians were 7.099 and 3.272 ms.
That fixture does not contain a material solve and does not imply MPM can reduce
rigid collision cost. See the raw
[physics attribution results](physics-arena-attribution-results.json).

## Primary sources

- Hu et al., [MLS-MPM with displacement discontinuity and two-way rigid-body coupling](https://yuanming.taichi.graphics/publication/2018-mlsmpm/) and its [first-party implementation](https://github.com/yuanming-hu/taichi_mpm/blob/3a680ab0fdc6e95077f08a91bc9079e1ef41478e/mls-mpm88-explained.cpp).
- Jiang et al., [The Affine Particle-In-Cell Method](https://doi.org/10.1145/2766996).
- Stam, [Stable Fluids](https://graphics.stanford.edu/courses/cs468-05-fall/Papers/p121-stam.pdf), which advects smoke density as a scalar field through a velocity solution.
- Box2D, [simulation, fixed-step, substep, and impulse documentation](https://box2d.org/documentation/md_simulation.html) and [body point-impulse API](https://box2d.org/documentation/group__body.html).
