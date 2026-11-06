# Material showcase

These independent Arena2D scenes demonstrate the current material runtime. Launch a release build from the repository root:

```sh
make play SCENE=scenes/MaterialShowcaseLiquidDDR.kdl
```

`PROJECT` defaults to `projects/arena2d`; use `SCENE=scenes/<name>.kdl` for any name below. **R** restarts, **Escape** pauses, **Enter** resumes, and **Q** quits while paused. [Launch target](../../Makefile), [controls](../../src/host/main.c)

| Scene | Seeded parcels | What to watch |
| --- | ---: | --- |
| `MaterialShowcaseLiquid` | 19,116 | A timed reservoir breach releases liquid over ledges into a basin. |
| `MaterialShowcaseLiquidDDR` | 19,116 | The same authored liquid fixture using DDR as a solver control. |
| `MaterialShowcaseGas` | 8,946 | Two heated gas clouds interact with a rotating mixing paddle. |
| `MaterialShowcaseGasSpeed` | 8,946 | The gas fixture with velocity debug coloring. |
| `MaterialShowcaseThermal` | 64,336 | Breaching a divider lets cold coolant contact burning fuel and quench it. |
| `MaterialShowcaseThermalClosed` | 64,336 | The same thermal fixture with the divider intact. |
| `MaterialShowcaseDestruction` | 12,979 | A timed cut releases rigid fragments into liquid. |
| `MaterialShowcaseOrganic` | 17,952 | A gently elastic specimen deforms against a pillar under gravity. |
| `MaterialShowcaseOrganicStrain` | 17,952 | The organic fixture with strain debug coloring. |
| `MaterialShowcaseExplosion` | 10,442 | Timed ignition burns a charge, cuts a crater and accelerates loose blocks. |

Counts describe seeded runtime parcels: the authored sampling lattice and material footprint determine candidates, and candidates inside solid obstacles are excluded. Liquid and elastic spacing is 0.25 project units; gas spacing is 0.375. Debug variants change presentation without changing the simulated fixture. These scenes have closed outer walls; reservoir gates and internal specimens can be destroyed. [Authored scenes](../../projects/arena2d/content/scenes), [seeding](../../src/runtime/material_ecs_fluid.c)

Each scene has no HUD. Walls overlap at the corners; acceptance checks also compare particle bounds against the inside faces.

Recordings of six simulated seconds: [liquids](material-showcase-frames/motion/LiquidDDR.mp4), [gas mixing](material-showcase-frames/motion/Gas.mp4), [organic deformation](material-showcase-frames/motion/Organic.mp4), [explosion](material-showcase-frames/motion/Explosion.mp4), [thermal contact](material-showcase-frames/motion/Thermal.mp4). These recordings show simulated motion at 60 steps per second; they are not real-time performance measurements.

## Runtime model

A chunk membership index supplies sorted particle candidates for active-region and visible-region queries. Particle storage remains a retained array; chunk queries do not provide particle paging. [Index](../../src/runtime/material_particle_index.h), [fluid runtime](../../src/runtime/material_fluid.c), [raster preparation](../../src/render/fluid_raster.c)

The optional sparse CPU MLS-MPM solver uses quadratic APIC transfers for liquids and elastic material. Liquids use a Tait pressure law; organic material uses fixed corotated elasticity. Adaptive explicit substeps bound advection, wave speed and deformation, with a configured maximum. DDR remains the default liquid solver. It currently gives the more convincing pool in this comparison; MPM can stretch dense liquid into thin strands. Gas uses a sparse staggered velocity grid with pressure projection, vorticity and heat-driven buoyancy, carrying parcel tracers. [MPM](../../src/runtime/material_mpm.c), [gas flow](../../src/runtime/material_flow.c)

`MaterialShowcaseGasSpeed` maps speed from blue at rest through cyan and yellow to red at 64 project units per second. `MaterialShowcaseOrganicStrain` maps principal stretch logarithmically: blue is undeformed; red means roughly twice the length or half the length along an axis. Rotation alone does not register as strain. These debug variants preserve the simulation state.

Node-owned MPM accumulation and independent particle passes use CPU workers. The dense-grid path schedules accumulation by particle workload even when only a few grid cells are occupied. Sorted per-node sums retain exact serial/worker results.

Particles share thermal exchange, fuel and combustion reactions. Destructible masks produce Box2D fragments; fluid contact exchanges linear and angular impulses with bodies that opt into rotation. The existing GPU material rendering and HRC lighting remain in use: CPU simulation does not become a GPU solver, and asynchronous GPU execution can overlap CPU work. [Thermal/reactions](../../src/runtime/material_fluid.c), [destruction](../../src/runtime/material_destruction.c), [body adapter](../../src/runtime/scenes/physics.c), [renderer](../../src/render/scene_renderer.c)

## Measured cost

Measured on an Apple M4 Pro with 48 GiB, a Release build and eight native workers, at 1280×720 with fixed HRC resolution 0.5. Each run begins at simulation step 1 and advances 480 more steps. The renderer warms for 240 frozen frames; the first 16 advancing frames are discarded, leaving 464 timing samples. This includes the timed cuts and ignition. Profiling and capture jobs were stopped before measuring. Every rendered endpoint matches an independent replay.

| Scene | Seeded parcels | Median ms | p95 ms |
| --- | ---: | ---: | ---: |
| MPM liquid | 19,116 | 15.8 | 19.8 |
| DDR liquid | 19,116 | 18.7 | 22.3 |
| Gas mixing | 8,946 | 7.9 | 9.1 |
| Organic | 17,952 | 15.9 | 19.1 |
| Destruction | 12,979 | 29.2 | 41.5 |
| Explosion | 10,442 | 21.4 | 27.0 |
| Thermal stress | 64,336 | 49.8 | 75.6 |

These times include CPU update/simulation and completion of scene GPU rendering, including GI. The screen blit and presentation are submitted after the scene GPU fence and are not separately fenced. The intentional 60 Hz sleep is excluded. The live player can overlap CPU/GPU work, so these figures do not directly establish presented FPS. The raw `performance.particles` field counts cosmetic particles; `material_particles` is recorded separately. Explosion ends the longer performance interval with 9,994 material parcels because some smoke has expired.

The dense destruction and thermal cases exceed 16.7 ms even before rendering on average. These populations are stress settings, not a demonstrated 60 Hz world budget. MPM cell size is 3 for liquid, thermal, destruction and explosion, and 2 for organic matter; rendered parcel spacing stays 0.25. The larger simulation cells reduce work but lose motion detail. Gas uses a separate 4-unit flow grid. [Rendered results and scope](material-showcase-frames/performance/summary.json)

A separate matched 16,384-parcel collision microbenchmark measured mean CPU tick cost of 53.39→9.23 ms for DDR and 4.32→3.40 ms for MPM when moving from one to eight workers. Serial and worker runs produced identical hashes. That fixture uses spacing 1, kernel 3, MPM cell size 2, bulk modulus 2, zero gravity and 120 steps; it does not measure the dense scenes above or rendering. Reproduce with `build-release/material_solver_benchmark ddr 128 120 8 collision` and the equivalent `mpm` and one-worker runs. [Raw CPU results](material-showcase-frames/performance/cpu)

## Authoring and replay

Build the release controller, then run the stored-scene checks and previews:

```sh
make build-release BUILD_TARGET=chibi_ctl
python3 tools/material_showcase_scenario.py --profile
python3 tools/material_explosion_scenario.py --profile
```

Full `make test`, native sanitizer material checks, `make web`, serial and threaded WebAssembly material tests, and all scene acceptance checks passed. Gas and strain rendering were also inspected in the threaded browser build.

The first script covers nine scenes; the second covers Explosion. Add `--author` to regenerate their authored assets through local MCP, or `--scene Liquid` to select one of the first script's fixtures. `--skip-preview` omits PNG captures; `--output` chooses the report directory. Defaults are `/tmp/chibi-material-showcase` and `/tmp/chibi-material-explosion`. Normal execution validates assets, samples fixed simulation steps, checks enclosure bounds and material outcomes, and compares deterministic replay hashes. It does not rewrite assets. The thermal pair checks quenching and retained fuel; debug pairs check identical material hashes. [Showcase tool](../../tools/material_showcase_scenario.py), [explosion tool](../../tools/material_explosion_scenario.py), [MCP workflow](../mcp.md)

## Current limits

Solver selection is per material world. DDR liquid and MPM elastic matter do not yet run together inside one world.

MPM liquid is compressible. The coarse-grid liquid comparison ends with parcel volume ratios ranging from 0.39 to 5.41 and visible thin strands, so it is not a general liquid-quality upgrade over DDR. Complete transfer stencils can still exchange momentum across thin walls; collision projection does not supply CPIC separation. Gas is a coarse velocity field with tracers, without composition diffusion. Explosion pressure is an authored gameplay blast kernel whose density and strength need tuning, not a resolved detonation model.

At eight seconds, the open thermal scene has extinguished 325 of 32,168 fuel parcels, about 1%, while the sealed control remains burning. It demonstrates local quenching, not rapid bulk extinguishing. Organic strain reaches about 13.5% in the final sample; the scene demonstrates gentle elastic contact without tearing. [Acceptance report](material-showcase-frames/scenes/material-showcase.json)

`materials.mpm.limited` means mechanics advanced only part of the requested step after reaching a substep or stability limit; inspect `advanced_time` before interpreting a run. The showcase checks reject sampled limited steps. With `--profile`, they also reject any limited step over the full replay interval. This continuation does not add out-of-core particle paging, full chemistry, attachment joints, structural stress fracture or plastic tearing. Particle count alone is not a frame-budget measurement. [Solver contract](../../src/runtime/material_mpm.h), [step budget and blast](../../src/runtime/material_fluid.c)
