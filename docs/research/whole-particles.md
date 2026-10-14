# Whole particle emission

September 12, 2026. Emit Particles and Emit Directed Particles now lower through
explicit typed host entry points. The [catalog audit](whole-particles-audit.txt)
reaches 26/33 events and 25/31 effectful local functions, with 39/39 pure local
functions and 4/4 library functions unchanged. ProjectileHitBehavior.Contact
Begin and its Impact Particles function are newly whole compiled bodies.
The live Arena check reaches 19/22 entries. Seven catalog events and six
effectful functions still have exclusions; this is not completed coverage.

## Execution boundary

The whole compiler evaluates arguments, loads and stores plain vector fields,
marshals typed host inputs, branches on errors, and executes enclosing calls and
continuations. SLJIT, bytecode and generated C share that lowered program.
Generated execution calls the particle engine directly through its typed host
binding. It never enters the flow callback, eval_node or flow_run interpreter.

The C helper packs a burst/spray and calls particles_emit or
particles_emit_directed. The existing native particle engine owns capacity,
validation, its local RNG, and particle initialization. Keeping that engine call
avoids duplicating its implementation in the behavior compiler. It is not a
claim that every small marshalling helper is faster than emitted instructions.
The direct C host baseline below exposes the remaining marshalling cost.

A node_host_call can now declare missing_service. Its compiler-generated service
check executes before argument evaluation. This preserves the particle nodes'
existing ordering: a missing particle host must fail before a random input
consumes a draw or another input fails. BF_HOST_SERVICE is a pointer load in
SLJIT/generated C and a typed bytecode instruction. The ordinary callbacks and
compiled host entry points share the same particle implementation and error text.
Host-sink effects retain their serial scheduling rules.

## Body measurements

[Five rotated trials](whole-particles-results.json) use Release O3 without
sanitizers, Clang 23.1.1, 50,000 invocations and 64 warmup invocations per case.
Each invocation emits twice through a nested function, then completes. Every
iteration clears the particle collection before executing, retaining capacity
and resetting its local RNG. That clear cost is included in all modes.

SLJIT and generated C call the whole entry directly. Bytecode reuses activation
scratch. C_HOST calls the typed C helper twice with prepacked arguments; it is a
service baseline without graph argument loads, frame budgets or source checks.
It is not an equivalent full Scene or a handwritten ECS update benchmark.
Timed execution adds no VM or host-bank allocations and makes no oracle calls.
All modes produce the same particle count and bytewise state checksum.

| Emission | Particles per emission | SLJIT ns | Generated C ns | Bytecode ns | C host ns | SLJIT / C |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Burst | 0 | 26.60 | 17.88 | 376.00 | 7.28 | 1.488 |
| Burst | 1 | 34.60 | 25.88 | 389.24 | 16.22 | 1.337 |
| Burst | 32 | 251.68 | 250.14 | 601.88 | 234.72 | 1.006 |
| Spray | 0 | 76.18 | 52.16 | 1,075.26 | 16.96 | 1.461 |
| Spray | 1 | 97.62 | 71.98 | 1,094.90 | 33.96 | 1.356 |
| Spray | 32 | 647.40 | 623.98 | 1,648.78 | 576.72 | 1.038 |

All cases meet the advisory 1.5x generated-C target. The zero/one-particle cases
show real remaining overhead, particularly with the spray's 19 arguments. For
32-particle emissions, SLJIT is within about 1-4% of generated C. Neither result
justifies claiming that the helper boundary is optimal. Static host bindings,
less argument-bank traffic and whole-program optimization remain measurable
opportunities, including for AOT exports.

## Arena measurements

[Five paired Scene trials](whole-particles-scene-results.json) initially showed
unchanged native behavior p50/p95 at 0.011/0.026 ms and bytecode behavior p50/p95
at 0.019/0.034 before versus 0.019/0.037 ms after. A
[nine-pair repeat](whole-particles-scene-repeat-results.json) investigated that
small bytecode tail increase. Native behavior p50 remained 0.011 ms; bytecode
p50 remained 0.019 ms and p95 changed from 0.037 to 0.035 ms. Simulation p50 was
unchanged at 0.015 ms native and 0.024 ms bytecode. The increase did not repeat.
Both runs are retained rather than selecting one result as a claimed speedup.

The workload is authored Arena3D firing and restart through the native Scene
host, 180 fixed steps with 30 warmup steps, profiling disabled and no frame
presentation. Both backends assert successful gameplay and zero oracle calls.
This short scenario establishes no material Scene speedup and does not cover
large population rendering or physics acceptance. Particle emission is not the
main steady enemy Tick cost.

## Validation

Targeted native parity and backend smoke passed. Generated-C parity also passed
before timing. Coverage includes normal and directed bursts, missing service,
missing/non-finite vectors, negative counts, invalid lifetimes/shapes, capacity,
caller-owned input mutation, random evaluation order, nested calls, waits,
cancellation, step budgets, breakpoints, stepping, watches, call stacks and
traversals. Oracle, bytecode, SLJIT and generated C match particle state and
inspection snapshots.

`make test` and `make build` passed. The sanitized native Arena firing/restart
check passed on SLJIT and bytecode, including 19 ProjectileHitBehavior.Contact
Begin dispatches on each backend and zero oracle calls. `make web` built the
application and passed the expanded particle BYTECODE smoke; the web cache has
SLJIT disabled. The native build with SLJIT disabled reported `UNAVAILABLE` and
rejected execution without interpreter fallback. Formatting, Python compilation
and `git diff --check` passed.

The coverage audit intentionally still returns nonzero for the remaining seven
event and six effectful-function exclusions. Managed-field ownership is the next
compiler target; scheduler/data locality and complete standalone Scene exports
remain part of the accepted work in the [completion inventory](behavior-completion.md).

Reproduce sequentially, without concurrent builds or benchmarks:

```sh
python3 tools/bench_particles.py --trials 5 \
  --output docs/research/whole-particles-results.json
python3 tools/bench_arena_scene.py \
  --before build-performance/test_suite-before-particles \
  --after build-performance/test_suite --trials 9 \
  --output docs/research/whole-particles-scene-repeat-results.json
build/test_suite --whole-particles
build/test_suite --whole-function-audit
```
