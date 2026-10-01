# Coordinated performance implementation

## Current delivery note, 2026-10-01

The [runtime ready batch](runtime-ready-20261001.md) records no-map source
`e0c766b4`, ready renderer/storage/Wasm work, and the exact scope of retained
combined-candidate validation. Its [evidence JSON](runtime-ready-20261001.json)
contains source, log and frozen-artifact hashes. Earlier body/fracture and
profiling checkpoints and this ready batch are pushed. The
[generic-map decision](runtime-decisions-20261001.md) rejects the map as the
default after its complete matched matrix; gas p50 regresses 29.23%. The
[current compiler measurements](compiler-measurements-20261001.md) record
accepted internal Wasm and native-loop checks. External LuaJIT, V8 and
JavaScriptCore comparisons remain deferred. The
[gas activation report](gas-activation-20261001.md) accepts existing-grid cell-ID
reuse at `4490156b`, tested against current KDL main `897dd6de`. All six native
timing cases and eight native/scalar/four-worker parity cases pass their audits;
gas-flow kernel gains do not establish frame-tail improvement. The separate
unused-index skips remain unmerged after a targeted liquid case regressed in
every pair. The historical results and source scopes below remain unchanged.

The first coordinated batch reconciles performance defaults, field evaluation,
material rendering, replay parity, and current main before implementing two
further optimizations. Its combined source is
`7fa805dbffc5e3a7737d29d48266889258908897`. Documentation commits follow that
source without changing the engine. Delivery is local, with no remote push or
deployment.

## Source and ownership

Both implementation owners measured against
`38118fd2cdb6d45c18d31c79c5a2094c19d42e36`. That common baseline integrates
performance defaults `9f4cd895`, field work `164112bb`, and renderer/replay work
`25ae04e7` with main's committed Studio source. Later Studio commits through
`b4ce8216` are included in the combined source. The status-bar fixture now expects
the committed theme's 28dp height, matching the primary checkout's existing edit.

| Owner | Source | Change |
| --- | --- | --- |
| Compiler chat | `9d186d82` | Fuse safe predicate chains into native conditional branches |
| Implement frame-time roadmap chat | `20454465` | Reuse solved solid contacts before spatial overlap queries |
| Coordinator | `77353396`, `7fa805db` | Combine accepted slices with current main and validate integration |

The coordinator owns shared build/controller/test policy and timing reservations.
The runtime chat owns physics, materials, rendering, and scene measurements.
The compiler chat owns behavior JIT, query generation, managed graphs, and numeric
Wasm boundaries. Accepted timing runs were sequential, using pinned builds,
assets, inputs, worker budgets, and replay checks.

Automatic worker selection and serial recovery remain the ordinary web launch
path. The scalar app and controller contain zero SIMD instructions. Worker builds
retain pthreads, SIMD, and strict floating-point contraction policy.
`CHIBI_FIELD_SIMD=OFF` disables only the field translation unit's SIMD, preserving
the pressure solver's SIMD and its strict numeric flags.

## Native predicate fusion

The native emitter branches directly on supported comparisons and optional
boolean inversions. Single-use and control-entry guards reject unsafe chains.
Floating-point inversions preserve ordered/unordered behavior for NaNs. Boolean
loads use their byte width, and boolean register allocation reserves context and
result registers. Budgets, diagnostics, inspection, and partial effects retain
their original behavior.

Five paired Release trials rotated bytecode, SLJIT, and generated-C body order.
The generated-C source and shared library were byte-identical between versions.
Body timings include snapshot production; activation timings also include setup
and teardown. All trials used the effective 10,000-step production budget. The
retained JSON's `production_budget: 0` is the activation CLI's default sentinel;
the runtime substitutes 10,000. The body driver explicitly sets 10,000.

| Entities | SLJIT body median, before → after, ms | SLJIT activation median, before → after, ms |
| ---: | ---: | ---: |
| 1,000 | 0.008670 → 0.008450 | 0.011125 → 0.010848 |
| 5,000 | 0.045390 → 0.043825 | 0.051150 → 0.049459 |
| 20,000 | 0.176735 → 0.173885 | 0.188316 → 0.180494 |

At 20,000 entities, body time falls 1.6% and activation time 4.2%. Four of five
pairs improve each metric, but candidate variation is 3.75% for body time and
6.09% for activation, including one slower activation trial. The unchanged C
control also varies. These are modest query gains, without a whole-frame claim.
The [retained query results](query-predicate-results.json) include every sample,
commands, source commits, compiler, executable hashes, and variation.

## Solved-contact reuse

The collector seeds existing per-worker deduplication scratch with accepted
solved solid contacts. Compound manifolds and later overlap callbacks avoid
redundant exact-distance work and duplicate pairs. Sensors, sweeps, speculative
contacts, filters, reset/re-entry, midpoint bits, and canonical event order retain
their existing behavior. Independent compound and positive-gap fixtures validate
the generalization without combat rules.

The 20,000-body crowd gives the larger measured benefit. Values below are medians
of three paired runs, measured by headless controllers in milliseconds.

| Target | Pair collection mean, before → after | CPU frame p95, before → after |
| --- | ---: | ---: |
| Native | 1.917 → 1.695 | 19.894 → 15.828 |
| Scalar web | 16.505 → 14.327 | 85.849 → 80.548 |
| Worker web | 4.627 → 3.927 | 54.686 → 51.946 |

Crowd CPU p95 falls 20.4%, 6.2%, and 5.0%, respectively. All native pairs improve;
the third scalar and worker pairs have worse frame tails. Other initial workloads
have mixed or higher tails. Five additional worker fog pairs give nearly equal
p95, 1.455104 → 1.461504 ms, and do not reproduce the original large tail.
No fog, terrain, all-scene, rendered-frame, or GPU speedup is established.

All 82 profiles pass replay, frame-count, dropped-time, and fingerprint gates.
Endpoint contact/event hashes and bounded native initial/final inspection match;
this does not establish a complete per-frame trajectory trace. Nested contact
wait/collection costs and these separately measured compiler/runtime slices are
not additive. See the [contact report](contact-collection-optimization.md) and
[retained results](contact-collection-results.json) for all controls and limits.

## Combined validation

The [retained validation manifest](performance-coordination-validation.json)
identifies source states, executable hashes, log hashes, and local evidence paths.

| Source | Completed checks |
| --- | --- |
| `38118fd2` | Full native sanitizer suite; focused thread-sanitizer field/material/runtime checks; scalar/worker builds; field-SIMD OFF isolation; browser worker/script/module/CSP recovery |
| `77353396` | Full native ASAN/UBSAN `make test`; scalar/worker app, controller, backend and material checks; zero-SIMD scalar disassembly; automatic and explicit serial browser rendering; native/scalar/worker replay-profile parity |
| `7fa805db` | Full native ASAN/UBSAN `make test`; incremental `make web`; Studio rendering at 1280×800 and 800×600 with automatic workers |

The final UI merge changes only three stylesheets. Scalar/worker controller JS
and Wasm hashes remain identical to the `77353396` validation. Sparse Player
replay/profile hash is `f86cf824a4ef67e2`; named Firefight is `b2978f57047786db`
across native/scalar/worker modes. The no-input control differs as expected.
Thread-sanitizer and field-OFF/fault-injection evidence remains scoped to the
common baseline; those checks were not repeated on the final source.

Native checks use LLVM 23.1.1 and the macOS 26.5 SDK. Web checks use Emscripten
6.0.9 with a four-worker budget. Native validation runs in the isolated checkout,
so it does not certify the primary checkout's unfinished editor edits. Integration
backs up and preserves those edits and untracked files. Owned test servers,
browsers, and profiling processes are closed.

## Fog and liquid across solver choices

The user's additional requirement is that current fog/liquid work should carry
across to MPM, FLIP, or a Paradigm-like solver using GPU sparse grids. This constrains
the shared infrastructure; it does not claim those solvers are implemented.
[Theory Accelerated](https://www.theoryaccelerated.com/) identifies Paradigm as a
sparse GPU liquid solver. Its public vendor pages checked here do not document
the specific FLIP/SPH hybrid, so that combination is not an implementation premise.

The present gas path already has mass-weighted particle-to-grid deposition,
grid-to-particle sampling, staggered velocities, obstacle boundary velocities,
and staged pressure projection in `src/runtime/material_flow.c`. Its persistent
pressure arrays and ordered parallel/SIMD sweeps are useful references for FLIP.
The field baker's retained per-worker workspaces and bounded row jobs are useful
CPU execution patterns. Liquid density/relaxation equations and view-space optical
rendering remain specific to their current jobs.

| Shared requirement | Current and future use |
| --- | --- |
| Sparse activation in simulation coordinates | Work on occupied cells/tiles plus required stencil halos; rendering coverage and terrain disk residency are separate concerns |
| Spatial ordering and flat channels | Contiguous cell/particle ranges support neighbor queries, particle-grid transfers, SIMD, and later device buffers |
| Persistent workspace ownership | Reuse CPU worker scratch now; give GPU buffers separate device ownership and synchronization |
| Explicit phase inputs, outputs, and joins | Preserve current pressure dependencies; support future P2G, grid updates, and G2P with declared write/reduction ownership |
| Measured storage and transfer costs | Profile indexing, clearing, packing, reductions, and kernel work separately; later include GPU dispatch, transfers, synchronization, and complete frame time |

The current grid is an individual-cell hash with AoS records. Its indices depend
on insertion order, particles can move during removal compaction, and P2G scatter
is serial. The rendering bins are screen-space fragment inputs. None is a ready
GPU sparse-grid allocator or compute backend. Keep hot simulation channels and
stable identity/order explicit when improving current storage; extract shared
interfaces when a concrete second use establishes their contract.

This direction follows the shared costs described by the primary
[GPU sparse FLIP paper](https://onlinelibrary.wiley.com/doi/abs/10.1111/cgf.13350)
and [GPU MPM paper](https://doi.org/10.1145/3272127.3275044): sparse topology,
particle/grid mapping, local data access, and transfer reductions. This is an
architectural inference, not measured performance in our future solvers.

Current changes must retain native/scalar/worker semantics and replay parity.
A future GPU solver needs its own documented numeric tolerances, mass/momentum
checks, divergence or constitutive checks, finite-value and boundary tests, and
fixed-step trajectories. CPU hash equality alone does not validate different GPU
reduction orders. FLIP delta transfer and MPM affine/deformation/stress state are
additional solver work. The runtime owner has received this requirement and owns
the next measured fog/liquid implementation.
