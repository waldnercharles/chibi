# Runtime ready batch, 2026-10-01

The ready source is `e0c766b41d168908ec9716ccfec2a072a1395915` on
`codex/runtime-common-ready-20261001`. It includes the renderer border cache,
retained terrain/debris preparation scratch, Wasm worker trial instrumentation
and corrected backend fixtures, plus committed main through `2062f93a`.
The earlier body/fracture batch was already pushed at `2aac7fe0`; the earlier
ready/profiling checkpoint was pushed at `47ee7f93`.

This batch excludes the shared fluid spatial map. Its nine differing paths
against combined candidate `edb8b29a86cbfd6ae202da158582330cf266746f` are recorded
in the [evidence JSON](runtime-ready-20261001.json). The renderer, mask identity,
storage scratch and Wasm/backend files recorded there are byte-identical across
those two sources. The no-map native Release controller is frozen in
`outputs/fluid-spatial-repair/baseline-native-final/manifest.json` at the ready
source. Freezing that binary does not establish a timing result.

## Delivered behavior

The renderer owns a cache of border rectangles in local cell coordinates. Mask
incarnation, revision, dimensions and border reach determine reuse. Transforms,
colors, material values and draw ordering remain live. Reset and world changes
clear the cache. Replacement and allocator address reuse receive new mask
identities; revision wrap also changes identity.

Retention is bounded at 65,536 entries and 16 MiB for entry/bucket arrays and
rectangle payloads. Hash lookup avoids scanning every cached mask. Entries used
in a collection stay resident; excess masks use the original reusable scratch
builder. Complex geometry can exceed the byte budget and require rebuilding each
frame. Scratch, allocator overhead, cold admission and live-edit costs remain.
The cache does not eliminate draw submission or GPU execution.

Terrain views and debris blast preparation retain processing-owned scratch.
Their regressions check warmed allocation reuse, replacement and bounded
lifetimes. The Wasm smoke now retains raw worker and handoff trials and checks
F32 rounding, NaN results, eligibility and ordered argument binding. Graph
expressions and flow dispatch use separate modules and coverage checks. Trial timings emitted during
build validation are correctness diagnostics; isolated measurements are pending.

## Validation scope

All paths and SHA-256 digests below are retained in the evidence JSON. These are
combined-map candidate checks with identical ready production slices. They are
not an exact no-map full-suite run.

| Evidence | Source and scope |
| --- | --- |
| `native-test-main-integrated.log` | Full native ASan/UBSan `make test` at candidate parent `02102545`; final candidate delta changes only the backend smoke test |
| `native-backend-final.log` | Focused native backend and physics PASS at `edb8b29a` |
| `web-build-attempt4.log` | Scalar and four-worker app builds, backend and physics checks PASS at `edb8b29a` |
| `serial-controller-material-build.log`, `worker-controller-material-build.log` | Both controller builds and material checks PASS at `edb8b29a` |
| `native-radiance.log` | Real native GPU fixtures PASS at `25d2a7e2`, with identical renderer production source |
| `parity-final/summary.json` | Eight complete same-version native/scalar/worker endpoint and hash-parts comparisons at `edb8b29a` |
| `parity-work-audit-final.json` | Independent audit of 66 raw responses and 24 exact three-frame counter sums |

The GPU fixture retains 20,000 independent masks in 3,377,152 bytes, below the
16,777,216-byte limit. Two warmed sweeps produce 40,000 hits, zero geometry builds
and zero allocation calls. Coverage/order, invalidation, replacement, isolation,
eviction and pressure fallback checks pass. Cached geometry matches the
independent row renderer across five GPU targets with maximum error `0.000244`,
below the `0.002` tolerance, while transforms and material values change.

The endpoint matrix covers terrain barrage, enemy barrage, enemy collisions,
fog/liquid, gas-only, liquid-only, MaterialStudy and HRCVolume. Scalar uses one
worker; the threaded web mode uses four. Native uses the Auto material pool
independently of authored physics workers. Endpoint/hash equality does not prove
complete per-frame trajectory equality. Diagnostic long profiles expose selected
frames; exact counter-sum assertions apply to the separate three-frame controls.

## Remaining acceptance

New shared-map timing remains pending review and is not accepted for delivery.
The map is absent from this ready source. No whole-frame speedup or comparison
against LuaJIT, V8 or JavaScriptCore follows from these checks. Future comparisons
need equivalent workloads and controlled cold/warm compilation and host costs.

Fluid bins and lookup can provide reusable infrastructure for later FLIP, SPH or
MPM work. Those future solvers are not implemented. This report adds no new
builds, controller executions, browser checks, deployment or timing runs.
