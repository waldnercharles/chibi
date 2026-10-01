# Physics and material performance scenarios

Add four repeatable workloads before changing the Box2D integration: sustained
player fire into terrain, sustained fire into destructible enemies, a dense
destructible crowd, and interacting fog/liquid. Assess MPM coupling separately.

## Context and ownership

`tools/check_neon_swarm.py` supplies the native controller and Chibi MCP authoring
seam. `tools/benchmark_neon_materials.py` already measures rendered material
workloads in isolated project copies. New fixtures share those asset operations
and retain the current Box2D, destruction and fluid implementations.

The shipping project and unrelated multiplayer `PLAN.md` remain authoritative
for their existing work. Each benchmark owns an isolated project, fixed input
timeline, workload checks, requests and output reports. Native and WASM use the
same prepared assets. Authoring, snapshots and replay checks stay outside timed
intervals; CPU profiles and GPU-complete rendered timings are recorded separately.

## Changes

- `tools/physics_material_scenarios.py`: prepare the four fixtures through
  hash-checked MCP operations and verify that they exercise their intended load.
- `tools/benchmark_physics_materials.py`: sequential controller profiling,
  deterministic replay, workload counts, executable/project fingerprints and
  native/WASM comparison using retained reports.
- `tools/benchmark_neon_materials.py`: render the same new fixtures through the
  existing fixed-quality renderer benchmark boundary.
- `docs/research/physics-material-benchmarks.md`: fixture definitions, invocation,
  validation results, baseline timings and measurement limits.
- `docs/research/mpm-box2d-coupling.md`: primary-source research on liquid/fog,
  particle/grid transfers, rigid-body exchange, rendering and native/WASM limits.

## Verification

Validate every authored prototype/scene, assert projectile impact and terrain or
enemy damage against a no-fire control, retain a dense collision population,
and require both fog and liquid to remain active. Run the four fixtures natively
and in serial WASM, compare deterministic responses and repeated-run hashes,
then exercise their actual rendered workflow. Record CPU phases, allocations,
population, contact counts, material state and GPU-complete frame percentiles.
Short smoke runs establish correctness; repeated warmed runs establish a baseline.

The revised barrages use a 120-degree fan at roughly 240 bullets/s, with an
early live-projectile check for both angle and rate. The crowd defaults to 20,000
destructible enemies, created by an authored 200-per-tick spawner. Its warmup
finishes creation and allows 20 settling ticks; the private pursuit radius spans
the whole grid. Aggregate material/population counts and profiler body/contact
counts validate the full load while inspection samples at most 256 entities.

No second solver or Box2D behavior change is part of this batch. A future MPM
prototype must preserve rigid-body interaction and meet measured quality and
frame-tail criteria before replacing the current fluid path.

## Completion

The [workload report](physics-material-benchmarks.md) records the revised fixtures,
native and serial-WASM three-run CPU baselines, twelve native rendered runs and
browser smoke checks. Complete replay responses match across platforms. The full
sanitized native suite and both Release material-engine checks pass. Retained
[results](physics-material-results.json) include workload counts, phase timings,
fingerprints and native/browser frame images. The crowd exceeds a 60 Hz budget
on both builds and exposes rendering, behavior and contact costs for future
matched experiments. MPM remains an assessed, unimplemented experiment.
