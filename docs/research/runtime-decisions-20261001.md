# Runtime decisions, 2026-10-01

The ready batch `ba538384` is pushed to main. Its renderer cache, retained storage
scratch and Wasm validation evidence stay frozen in the ready report. The shared
generic spatial map remains unmerged: its native timing matrix rejects it as the
default. The separate existing-grid activation-ID candidate `cfccc152`, a direct
child of the ready batch, is under coordinator correctness and timing review and
has no acceptance decision yet.

## Generic-map decision

The independently audited native matrix completed 36 profile calls: 18 paired
trials across six scenes, three pairs per scene. Timing gates passed under the
reviewed point-in-time quiet-boundary policy. The implementation failed the
performance gate. Gas-only median profile p50 rose from `0.349` to `0.451 ms`,
`+29.23%`. Each gas pair regressed: `+24.45%`, `+29.23%`, `+24.57%`. Median gas-flow
cost rose `84.16%` and index cost rose `120.41%`.

| Native workload | Median p50 change | Three paired p50 changes |
| --- | ---: | --- |
| Gas only | +29.23% | +24.45%, +29.23%, +24.57% |
| Liquid only | -2.20% | -2.20%, -5.34%, -2.12% |
| Mixed fog/liquid | -14.49% | -11.22%, -27.59%, -9.30% |
| Terrain barrage | +2.87% | +4.19%, +2.19%, +2.44% |
| Enemy barrage | +1.45% | +1.45%, -0.42%, +4.23% |
| Crowd collisions | -3.17% | -3.17%, +11.58%, -8.43% |

Liquid and mixed-scene gains do not justify the consistent gas regression. Crowd
and enemy results vary in direction between pairs. These are native headless CPU
profile results, not rendered whole-frame or GPU measurements. Execution metadata
matches across all 18 pairs, but the raw profiles omit actual worker counts.

All eight same-version native/scalar/four-worker endpoint and hash-parts parity
cases pass for the generic-map candidate. Correctness parity does not accept its
performance or prove complete per-frame trajectory equality. The retained audit
also separates exact short-profile work sums from selected-frame long diagnostics.

The unmerged `a6003cb5` and `ea03d0aa` histories and their raw artifacts remain
available. The measured baseline is `e0c766b4`; the generic-map candidate is
`a6003cb5`. The new `cfccc152` candidate reuses activation cell IDs in the existing
gas grid. Its status and source identity are separate from the rejected map.

## Reusable direction and limits

Sparse spatial neighborhoods, stable cell lookup and particle/grid transfer
storage are useful groundwork for future FLIP, SPH and MPM sparse-grid solvers.
The generic-map experiment can inform those interfaces even though this version
failed the default performance gate. Future solver equations, transfer reductions,
GPU buffers, compute dispatch and GPU sparse-grid ownership are not shipped by
this work. They need their own numeric, physical and performance validation.

Equivalent LuaJIT, V8 and JavaScriptCore comparisons remain deferred. No engine
superiority claim follows from the current results.

The companion [decision evidence](runtime-decisions-20261001.json) records full
source identities, source-tree/file hashes, audited raw-report/manifests hashes
and this report's digest. This report uses retained files only; it adds no builds,
controller executions, probes or timing runs. It does not alter the published
ready report or its frozen evidence.
