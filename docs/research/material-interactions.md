# Material interactions and frame cost

Decision, October 2, 2026: use a hybrid material world. Keep destructible cell masks
as solid geometry, Box2D for moving fragments, and continuous particles/grid flow
for fluids. Add local material interactions to those representations. This is the
first implemented step toward a Noita-like world.

For the finer particle appearance requested after that first demo, run
`MaterialDetail`. It contains 4,024 simulated grains, with a separate visible
radius of 0.22 world units. The [7.7-second recording](material-detail-frames/motion.mp4)
and [mixed state](material-detail-frames/mixed.png) show the result. `ThermalStudy`
remains the original coarse thermal regression fixture.

Noita's developer describes local cellular rules, dirty regions and separate rigid
bodies, with simplified material interactions. That supports prioritizing shared
consequences and avoiding inactive work. Our zero-gravity game also needs sustained
momentum and fluid blobs, so copying downward falling-sand movement would be a poor
fit. See the [developer interview](https://80.lv/articles/noita-a-game-based-on-falling-sand-simulation)
and [Noita's official description](https://noitagame.com/).

MPM is useful when deformable solids, granular plasticity or constitutive fracture
are visible requirements. It introduces particle/grid transfers and material-model
costs; stiffness can require more substeps. FLIP/APIC is a candidate for improving
liquid motion, but it does not supply chemistry or destructible-world ownership.
APIC reduces PIC dissipation and FLIP noise through affine velocity transfer. Those
are future measured alternatives to our existing liquid relaxation, rather than
replacements implemented here. Primary references: [MLS-MPM/CPIC](https://yuanming.taichi.graphics/publication/2018-mlsmpm/),
[SideFX MPM workflow](https://www.sidefx.com/docs/houdini/mpm/workflow.html),
[APIC paper](https://doi.org/10.1145/2766996).

The user's [Tutti Space Program reference](https://tsp.grantkot.com/) provides a
concrete dense-particle target. Its deployed runtime uses an MPM interface in
threaded CPU WebAssembly, initializes 300,000 particles, and draws interpolated
particle quads with WebGL2. This makes sparse CPU MPM a relevant next comparison
for higher populations. The current change improves actual sampling and drawing
without replacing the motion solver. [Primary-source inspection](tutti-fluid-reference.md)
records what the deployment establishes and what remains unknown.

## Implemented behavior

Attach `MaterialThermal` to a `FluidVolume` or a fluid `ParticleEmitter`:

- `capacity` is specific heat capacity per unit particle mass. Zero disables
  thermal exchange. Effective heat capacity is `mass * capacity`.
- `conductivity` is an authored local exchange rate; zero insulates. Values must
  be finite and nonnegative, with positive capacity for conductive material.
- `Combustion.extinction` enables extinguishing when positive and below
  `ignition`. Its default of zero preserves the previous combustion behavior.

Heat is a gameplay temperature-like value, not Kelvin; conductivity is not an SI
material constant. Exchange uses the existing particle spatial index and two
joined worker passes over one immutable snapshot. Symmetric pair conductance and
a symmetric aggregate bound conserve conductive energy and prevent dense
neighborhoods from exceeding their starting temperature range. Heated and cold
solid obstacles both block exchange. Tests cover grazing boundaries and roundoff
at saturated conductance.

Cooling below extinction stops combustion before the next heat release. It retains
unspent fuel and restores its unburned appearance; later heating can ignite it
again. Conduction also runs without a combustion component. Cold combustion scenes
skip ignition neighbor searches, and uniform temperatures skip thermal passes.

Snapshot inspection now reports `thermal_particles`, `thermal_energy` and
`fuel_remaining`; deterministic material hashes include thermal and saved visual
state. Heat exchange remains optional for authored assets.

## Demonstration and correctness

Open `ThermalStudy` in Arena2D/NeonSwarm. Two liquid blobs approach a destructible
separator in zero gravity. A four-node behavior opens the separator after 0.5 s.
`ThermalStudyClosed` has the same initial state and keeps the separator intact.

At 8 s, the breached scene has **10 burning particles versus 32** in the control,
and retains **30.72559 fuel units versus 29.44019**. All 64 thermal particles and
all 32 fuel particles remain. The remaining outer fuel is still burning; this is
partial extinguishing. The control's conductive energy remains unchanged across
the closed wall. Pre-breach states match, repeated runs match, and native and
WebAssembly world/material states match at frames 29 and 480 for both scenes.
Separate tests prove complete quenching and re-ignition.

Saved previews show the [sealed barrier](material-thermal-frames/sealed.png) and
the [partially extinguished fuel after the breach](material-thermal-frames/quenched.png).

```sh
python3 tools/material_thermal_scenario.py --build build-release
# Recreate just this demonstration through Chibi MCP:
python3 tools/material_thermal_scenario.py --build build-release --author
```

Native sanitizer and Web material tests cover unequal heat capacities, conservation,
dense/coincident temperature bounds, cold and heated walls, dormant particles,
quenching/appearance/fuel retention, re-ignition, real worker scheduling, emitter
and volume propagation, validation and state hashing. The serial Web build reports
that worker scheduling coverage is skipped; the native test uses 2,304 particles
to cross the actual parallel-dispatch threshold.

The new graph exposed an existing publication bug: automatic layout coordinates
were being persisted, which broke graph round trips. The controller now uses the
existing canonical disk serializer. Required formatting of 94 existing/new assets
was checked by parsing before and after: only automatic placement metadata changed.
Explicit placement and graph behavior are preserved. No project-version bump is
needed because earlier assets still load.

## Fine particle demonstration

`MaterialDetail` uses spacing 1 and kernel 3, compared with spacing 8 and kernel
24 in `ThermalStudy`. The two filled ellipses contain 2,012 particles each.
Each parcel's mass and fuel are scaled by 32/2,012; burn rate uses the same scale.
Total initial mass, fuel and heat capacity therefore match the original fixture.
The offset incoming flows produce shear through the opening. Smaller interaction
neighborhoods change mixing and thermal diffusion, so the fine scene is evaluated
against its own sealed control rather than expecting identical motion or cooling
to the coarse scene.

`FluidVolume.grain_radius` enables individual antialiased discs when positive.
Zero retains the continuous liquid/gas presentation. The physical radius remains
1.5 while the demo draws grains at radius 0.22, about two pixels across at
1280×720. Heat, fuel, pressure and collisions still operate on all 4,024 particles.
Burnout restores the existing gas/smoke presentation. Raster bins are 8×8 pixels;
granules use the existing material and lighting targets, with per-grain optical
source density and no extra render canvases.

At eight seconds, the open scene retains 1,329 burning parcels versus 2,012 in
`MaterialDetailClosed`, and 29.99801 fuel units versus 29.44021. All 4,024 parcels
remain. The sealed control retains its initial 0.128 heat energy. Tests prove
appearance changes leave simulated positions, velocities, body impulses and
thermal energy identical, reject invalid grain radii without mutating particles,
and preserve matter when fuel becomes smoke.

```sh
make play PROJECT=projects/arena2d SCENE=scenes/MaterialDetail.chibi
python3 tools/material_detail_scenario.py --build build-release --profile
# Recreate the fine scenes through Chibi MCP:
python3 tools/material_detail_scenario.py --build build-release --author
```

Native ASan/UBSan `make test`, serial Web material tests and the Web behavior
checks pass. The WebGL2 build was visually inspected. Native and WebAssembly
world/material hashes match at frames 29 and 480 for both fine scenes. Each
profiled/rendered endpoint matches the independent replay. Retained results and
binary/shader hashes are in [material-detail.json](material-detail-frames/material-detail.json).
The recorded video uses frame readback and is not a performance measurement.

On the same M4 Pro, three optimized runs measure 2.977 ms median / 3.217 ms p95
for the complete headless CPU frame and 7.408 ms median / 8.531 ms p95 for a
1280×720 Metal frame including simulation and GPU completion. These are medians
of each run's percentiles, with fixed HRC resolution 0.5 and the same warmup and
frame range described below. Builds and the reference/local browser simulations
were stopped during timing. This is a small scene result; the current solver has
not been demonstrated at Tutti's particle population.

## Measurements

Apple M4 Pro, macOS 26.7, LLVM 23, SDK 26.5. Optimized native code, no sanitizers.
SDK 27's Network.framework headers failed to parse with the installed LLVM, so both
comparison binaries use SDK 26.5. No builds ran during timing; this is a local
interactive-machine measurement, not a hardware guarantee.

The kernel comparison uses the same driver against frozen commit `29a0e24f` and
the implementation. Each configuration has three runs of 600 fixed steps;
comparison runs alternate binary order. Cold/exchange cases use five warmup steps;
quenching includes the initial transient with no warmup. Values are medians of
per-run means. The table uses 4,096 particles and four
workers. Cold cases preserve all fuel and have no heat sources. Gas grid flow is
disabled here; these numbers cover particle integration, indexing and reactions.

| Workload | Before, ms/tick | After, ms/tick | Reduction |
| --- | ---: | ---: | ---: |
| Cold combustible gas particles | 0.598 | 0.505 | 15.5% |
| Cold combustible liquid particles | 2.036 | 1.811 | 11.0% |

The new heat exchange costs **0.288 ms** for its reaction phase and **0.789 ms** for
the whole particle tick in a 4,096-particle hot/cold gas fixture. Its temperature
range remains 0.02747–1.45051 after measurement, and conductive energy remains
4,096.00002 from an initial 4,096. Quenching costs 0.369 ms in the reaction phase
and 0.873 ms/tick averaged across its transient and settled states; two particles
remain burning. Neither case is a before/after speedup comparison, since the old
solver lacks this behavior. These fixtures have no solid obstacles; obstruction
checks add cost in geometry-heavy scenes.

The complete 64-particle demo measures as follows, using three runs through frame
480. Headless profiles use one warmup step and 479 measured steps. Rendered runs
use native Metal at 1280×720, fixed HRC resolution 0.5, one trace level, 240 frozen
renderer warmup frames, then 479 advancing frames with the first 16 excluded.
Every profiled and rendered endpoint matches the independent headless replay.

| Measurement | Median / p50 | p95 |
| --- | ---: | ---: |
| Native headless CPU frame | 0.105 ms | 0.140 ms |
| Serial WebAssembly under Node | 0.170 ms | 0.231 ms |
| Native rendered, GPU-complete frame | 4.312 ms | 4.976 ms |

Rendered mean simulation cost is 0.180 ms and mean render/present cost is 4.171 ms.
Those means do not sum to the frame percentile. This small demonstration meets the
16.67 ms frame budget on this machine; it does not establish large-world or browser
performance. The cold kernel savings are not a claim of an equal whole-game FPS gain.

[Retained numeric results and provenance](https://github.com/waldnercharles/chibi/blob/60d58df57666732a3f6a9ff759195b091c6b05d8/docs/research/material-thermal-results.json) include all
kernel runs, scene measurements, hashes and the canonicalization audit. Current
kernel results can be reproduced with the CMake `material_thermal_benchmark` target:

```sh
make build-release BUILD_TARGET='chibi_ctl material_thermal_benchmark'
build-release/material_thermal_benchmark cold 64 600 4
build-release/material_thermal_benchmark cold-liquid 64 600 4
build-release/material_thermal_benchmark exchange 64 600 4
build-release/material_thermal_benchmark quench 64 600 4
```

## Current boundary

This adds fluid-to-fluid heat exchange and extinguishing. Solids block heat but do
not yet conduct it. Sleeping regions retain state and do not wake from thermal
influence. Phase changes, solid burning, corrosion, electricity and a common
material identity across all representations remain unimplemented. Existing
igniters and combustion are authored heat sources; mixed thermal/legacy ignition
and environmental cooling do not form a closed energy-conserving system.

Larger scenes still need measured neighbor/obstacle cost, activation halos and
bounded conversion work. Liquid-motion quality and frame cost still come from the
existing relaxation solver; no MPM, FLIP, APIC or new pressure solver is included.
