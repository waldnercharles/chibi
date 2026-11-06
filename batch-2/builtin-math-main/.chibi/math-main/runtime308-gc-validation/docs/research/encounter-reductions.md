# Reflected entity reductions and retained runtime failures

The former Arena encounter scan fails at 5,000 enemies with the production
10,000-step budget (`FLOW_ERR_STEP_BUDGET`, node 2329). The replacement completes
at 20,000 enemies without changing ordinary loop budgets. This is primarily a
scale-correctness improvement; the complete 1,000-enemy Scene does not show a
material frame-time improvement from this change alone.

## Shared implementation

`Count Matching Entities` accepts an `ecs.entity` snapshot array and an array of
reflected `EntityCondition` values. Conditions are ANDed. `component` is a
canonical component key, `field` is a reflected scalar field, `test` is one of
`has`, `missing`, `==`, `!=`, `<`, `<=`, `>` or `>=`, and `value` is a double.
Presence tests require an empty field. Boolean, signed/unsigned 8/16/32-bit
integers, float and double fields are supported. Other field types are rejected;
64-bit handles/integers are not rounded through double.

The node counts live input entries, including duplicates; stale handles are
ignored. Empty conditions count all live entries. It consumes the supplied
snapshot, so membership/retirement filtering remains the snapshot producer's
responsibility. No mutation, user-function call or suspension occurs inside the
scan. Ordinary ForEach execution, step budgets and debugging remain intact.

Valid constant conditions resolve reflection metadata during publication and
expand to the shared typed IR. SLJIT emits the entity checks, branches, loads,
comparisons and counters. Portable bytecode and generated C consume that same
IR. A checked component-column lookup occurs before the loop; the reusable
`BF_ROW` operation tests membership and addresses a row without repeating schema
lookup. Sequential reductions reuse two hidden frame locals while earlier
results retain independent value slots, allowing SLJIT to keep the counters in
registers. Integral int32 thresholds use integer comparisons; the shared cast
operation now also handles narrow integers, bool and uint32 correctly.

Dynamic condition arrays use one explicit compiled C helper which resolves the
conditions and performs the scan. Invalid static conditions also retain the
normal runtime error behavior through that helper. Neither route calls the
interpreter. Snapshot production remains the existing explicit host operation.

Arena's authored `Count Living Members` function now calls `Count Members
Snapshot`, then writes its three existing state variables. Four reductions share
one snapshot: living enemies, pending enemy spawn markers, living players, and
dead members. The existing role, health, marker and pending-death rules are
preserved. Only those authored graphs are changed; the new node has no Arena
names or policy. Both affected graphs were arranged using the editor's layout
engine.

## Runtime inspection

`behavior_instance_last_failure` exposes a copied diagnostic containing backend,
program revision, event, graph, node, pin, code, step count and message. It
survives activation release/recycling, subsequent successful execution and hot
apply. Pool release, owner rebind, destruction and explicit clear discard it.
Held failed activations are inspectable too. The optional record is allocated
only on failure. Child frame cleanup cannot replace the root event's snapshot.

Scene inspection and behavior preview show the error; automation entity
snapshots expose it as `last_failure`. The Scene scale test checks retained
encounter and enemy behavior failures, so releasing a failed activation cannot
hide a failed acceptance run.

## Measurements

Release/O3 ARM64 native builds, sanitizers disabled. Three trials alternate the
old/new activation path and rotate bytecode/SLJIT/C body order. The old graph's
isolated reference uses a **test-only** 1,000,000-step budget so it can finish;
the new graph uses the unchanged production default. Both assert the same
counter results, SLJIT selection and zero oracle calls.

| Enemies | Old activation, µs | New activation, µs | Bytecode body, µs | SLJIT body, µs | Generated C body, µs |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 16.268 | 15.786 | 267.170 | 12.870 | 9.710 |
| 5,000 | 75.616 | 69.911 | 1321.090 | 66.110 | 50.140 |
| 20,000 | 300.566 | 310.663 | 5530.850 | 274.550 | 206.020 |

Activation measurements include creation and release. Body measurements reuse
context and buffers and include snapshot production. They exclude renderer and
physics work. Native body cost is about 1.32x generated C at 5,000 and 1.33x at 20,000;
SLJIT is about 20x faster than bytecode for this scan. This is not a claim about
whole-scene AOT export performance.

The first four-pass implementation regressed to roughly 117 µs at 5,000.
Hoisting column lookup and reusing scratch counters removed that regression.
The final trials improve the isolated activation by about 8% at 5,000. At
20,000 the final activation median is 3% slower, with overlapping trial ranges
(old 293–316 µs, new 279–323 µs); the earlier run was faster. There is no reliable
activation speedup claim at that population. A fused scan or maintained
aggregate could still avoid repeated traversals; neither was implemented at this
checkpoint. The subsequent [query fusion pass](query-fusion.md) combines these
counts automatically and exposes source-linked performance diagnostics.

Three paired 1,000-enemy combat runs have identical position checksums and exact
per-frame combat traces. Median frame p50 is 8.363 ms before / 8.354 ms after;
behavior p50 is 0.572 / 0.621 ms. Frame time is effectively unchanged. The final behavior median is 49 µs
higher (about 9%), so these trials do not establish a Scene CPU improvement.
The new 5,000-enemy run completes 240 rendered combat frames, kills, experience
collection and restart with four physics workers, no event backlog and zero
oracle calls. The old Scene's broken encounter scan is not a valid timing
baseline for that population. This does not establish 20,000-enemy gameplay:
only the encounter reduction has been measured at that size.

[Reduction trials](encounter-reduction-results.json) and
[paired Scene trials](encounter-reduction-scene-results.json) retain logs, binary
hashes and compiler information. The [5,000-enemy acceptance log](encounter-reduction-scale-smoke.json) records the larger functional run. Reproduce from preserved before/after builds:

```sh
python3 tools/bench_encounter_query.py \
  --before build-performance/test_suite-before-entity-query \
  --before-root /tmp/cblueprints-query-baseline \
  --output /tmp/reductions.json
python3 tools/bench_arena_scene_scale.py \
  --before build-performance/test_suite-before-entity-query \
  --before-root /tmp/cblueprints-query-baseline --combat --profile \
  --counts 1000 --frames 240 --trials 3 --expect-after-workers 4 \
  --output /tmp/reduction-scene.json
```

The before executable is `9c680738` plus the measurement-only encounter query
entry point. Its working directory contains that commit's assets. The paired
Scene driver supports a separate baseline asset root because the original
executable cannot load the new node.

## Acceptance

`make build` and `make test` passed with native sanitizers, including Scene documents, the
whole-function audit, automation/MCP checks and the headless application.
`make web` passed; its Emscripten smoke tests execute the reduction and failure
lifetime tests through BYTECODE and assert SLJIT is unavailable. A separate
native build with `CHIBI_ENABLE_SJLIT=OFF` passes both hard-failure smoke tests
with no interpreter fallback. Focused coverage includes all three backends, instrumented and ordinary execution,
static/dynamic conditions, missing/invalid fields, empty filters, duplicates,
stale handles, NaN/infinity, unsigned values, reflected component presence,
source diagnostics, pooled failure retention and hot apply. Existing encounter
parity exercises Scene and prototype hosts, damage, death, pending arrivals,
experience and restart. Prepared Tick failure tests check retained diagnostics.
