# Current simulation delivery and solver status, 2026-10-01

Profiling source `4e7a09bbd2c15c6d161f26bba1494e5f82ff2fdf` is pushed to
`origin/main`, directly after `611ff81d`. Accepted gas activation-ID code
`4490156b` and earlier renderer/storage/compiler work remain included. The
coordinator preserved the primary checkout's unrelated edits and build caches.
This report adds documentation only; its source validation is pinned to `4e7a09bb`.

## Shipped profiling

`profile_scenario` accepts `behavior_details: false` to retain frame, physics,
allocation and endpoint diagnostics while preserving ordinary prepared execution.
The default remains detailed profiling. Executed dispatch counters are exposed as
`behavior_work`; their categories overlap and must not be added together. Their
scope is the last fixed-step update of each sampled advancing frame. Worker counts
reflect participating worker IDs, not simultaneous utilization.

The full native ASAN/UBSAN `make test` passes, as do native Release, scalar and
four-worker WebAssembly controller builds and Release MCP authoring smoke. The
focused correction retry also passes. Independent audits validate 48 general
controller calls and 15 repeated-span calls, frozen source/dependency/artifact
identity, copied fixtures, endpoint hashes and full replay hash components,
physics/fluid work, malformed flags, warmup exclusion and zero measured frames.

In one measured native crowd update, ordinary mode executes 1,600 parallel rows
in seven tasks across six worker IDs; native-loop count is 1,603 and System count
is 1,600. Detailed mode reports zero native/System/parallel work. Endpoints match
across modes and all three backends. WebAssembly has SLJIT disabled and reports no
behavior parallel dispatch in these fixtures. This establishes native participation
and correct reporting, with no timing gain or WebAssembly behavior-parallel claim.

Initial source `7575b7f3` failed a test that incorrectly expected a repeated System
span from a singleton. Corrected source `4e7a09bb` asserts zero System work for that
fixture and retains its positive ordinary native-loop proof. Initial source and
failure logs remain preserved. Completed checks are incremental builds with
`CCACHE_DISABLE=1`, `MallocNanoZone=0`, sanitizer leak detection disabled, and the
GPU MCP check omitted. They do not certify a clean rebuild, GPU execution, or the
primary checkout's unfinished edits.

## Independent current-simulation candidates

| Source | Completed evidence | Delivery decision |
| --- | --- | --- |
| Compact fluid working data `03a41812` | Full native and scalar/worker checks; independently audited eight-case parity; actual production translation-unit coordinate oracle under ASAN/UBSAN | Unmerged. Three matched timing attempts stopped after 18, 2 and 6 profiles when other builds/tests appeared. Each attempt is incomplete and unaccepted. No performance gain is established. |
| Empty fluid/debris collision proxies `af294872` | Full native and scalar/four-worker backend checks; independently audited eight-case correctness | Unmerged. Timing setup qualified two quiet points but failed before any controller calls because immutable fingerprint maps overlapped. A separately reviewed retry driver accepts equal duplicate fingerprints and rejects conflicts. The fresh retry launcher is prepared but has not been activated; performance is unverified. |

No partial timing samples are pooled into acceptance. The latest compact attempt
qualified after another checkout's test suite ended, then a new make/cmake build
blocked the first liquid pair. All old attempts and the empty-proxy setup failure
remain retained. No unrelated process was stopped and no process exclusion was
added. Live cwd observations identify only the processes present at those times;
older blocking PIDs are not retrospectively assigned to those owners.

Generic shared spatial-map and unused-index skip experiments remain rejected by
their existing workload regressions. The accepted gas cell-ID work has its own
[scoped kernel and frame timing report](gas-activation-20261001.md). Correctness,
physical behavior, executed work, timing and rendered/GPU performance remain
separate conclusions.

## FLIP and SPH

We are still optimizing the current simulation. Earlier solver requirements were
interpreted as reusable infrastructure requirements; no new FLIP, calibrated SPH,
MPM or GPU sparse-grid solver has been implemented. The remaining current-sim
optimizations are independent of starting a new solver.

Current liquid uses SPH-inspired double-density relaxation. Current gas already
has a sparse staggered grid, mass-weighted particle-to-grid deposition, moving
boundary velocities, advection and pressure projection. Its gather overwrites
particle velocity; the previous grid supports advection rather than FLIP's
current-step velocity-difference transfer.

Simulation-space spatial lookup remains useful for SPH neighbors and FLIP
particle/grid mapping. Particle lifecycle, local flat working data, explicit
phase joins and worker scratch are reusable. Those contracts do not establish a
new solver or make the rejected generic map a suitable default.

The assessed first deliverable is a selectable 2D liquid PIC/FLIP path with static
walls, a resting tank and a dam-break scene. It requires liquid/air/solid
classification, valid-face extrapolation, normalized liquid transfers, a
current-step grid velocity snapshot, free-surface pressure boundaries,
residual/divergence diagnostics, PIC/FLIP blending and CFL-controlled advection.
Selected FLIP liquid must bypass the existing relaxation corrections. Acceptance
needs mass/volume drift, momentum/energy accounting, divergence/residual,
finite-state/wall checks, timestep/grid refinement, serial/worker agreement and
lifecycle/regression coverage. An SPH hybrid, moving-body pressure coupling and GPU
sparse grids are additional implementation work. This is a reviewed assessment,
not implemented solver code.

External LuaJIT, V8 and JavaScriptCore comparisons remain postponed until further
notice. [The evidence JSON](performance-next-wave-20261001.json) binds source,
validation, preserved failures, artifacts and delivery receipts.
