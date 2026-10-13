# Compiler and scheduling opportunities

The original query, signature-unit and escape-analysis audit used
`78d155015eaa9833ae7c652176b5971359c6d805`. The bounded optimization batch uses
main `f01160ad8ea0135e0543b691a5b6f14beab91815`, with benchmark-only baseline
`de67c3dffdb3c8bf86c7e1b3dca8819ea3bc49a6`. Allocation and operation counters
establish removed work; tiny workload timings do not establish frame-time gains.

## Query rows and memory

A relationship generator creates one execution row per matching tuple. Several
independent generators can multiply the row count. An exclusive relationship
limits targets per source; it does not prevent different sources from sharing
the same target.

Independent row state gives each tuple its own timers, state and suspended
tasks. Its storage necessarily grows with retained rows and live continuations.
Stateless synchronous generator systems stream through a reusable row header;
they do not retain a coroutine per row. Serial synchronous invocations now reuse
one idle activation per program, up to 64 KiB of fixed frame storage. The cache
rebinds owner, arguments, services and state, and releases host, payload, nested
call, task and request ownership. Failed, cancelled, inspected and resumed work
is excluded. Prepared batches still need simultaneous activations per row.
Value filters use a temporary admission bitmap to preserve evaluation before
body writes. Query cache limits bound cached
matching rows, not the separately owned state or task results.
Low retained memory does not imply zero allocation traffic: predicate evaluators,
helper calls and body activations still set up temporary storage. Measure both
peak retained bytes and allocation counts before making a gameplay speed claim.

The original audit's native 64-bit Debug build has a 288-byte `execution_instance`
header and a 984-byte `execution_activation` header, excluding owned buffers.
For example, 80,000 retained tuples use about 22 MiB in instance headers; if all
80,000 also suspend, activation headers add about 75 MiB. State banks, payloads,
query indexes and allocator overhead are additional. Eligible streaming systems
retain neither header per tuple. These are compiler layout measurements, not a
universal memory budget; use `execution_program_memory_usage` for the actual
program. The row hash index retains its peak capacity for reuse.

Published-write change detection avoids snapshots of old component values. Its
tradeoff is revision storage and write bookkeeping, and it deliberately treats
a same-value write as a change. Mutable native row pointers still require the
normal publication call; an unreported write supplies no change-detection proof.
Each retained row stores 32 bytes per `changed` term for consumed and sampled
source/revision pairs. Shared world tracking costs eight bytes per entity slot
per tracked component and is allocated only for components that need it.

## Implemented bounded reuse

These changes exploit immutable generations and existing execution rules without
new author annotations. The microbenchmarks use C fixtures in the existing
dispatchers and preserve a benchmark-only baseline commit. Allocation traffic
and retained bytes are reported separately; timings are workload measurements,
not frame-rate promises.

| Optimization | Established fact | Retained storage or remaining work |
| --- | --- | --- |
| Synchronous activation reuse | A successful first invocation has released all transient ownership before its fixed storage is reused for the same immutable function. | One idle frame per program, at most 64 KiB; host/payload/nested-call allocation remains. |
| Reactive binding order cache | The complete ordered live work stream and program generation identities still match the cached dependency topology. Values and dynamic targets are reevaluated. | One schedule per owning program, at most 16 programs, 1,024 rows and 64 KiB. Cache misses use the existing sorter; larger workloads execute normally. |
| Unchanged scheduler registration | Every copied descriptor field, callback, effect, phase and dependency is equal, and no prior error needs recovery. | Registration retains its existing descriptor. Batch scratch retains at most 128 entries; recursive and larger batches use temporary storage. |
| Presentation-only compilation reuse | Capability category, keywords and description have no executable meaning; all semantic metadata and binding generations still match. | Fresh catalogs own current display text. Parsing/declaration discovery and semantic edits still do their existing work. |
| Read-only array snapshot borrowing | The complete synchronous body cannot mutate, retain or observe the identity of a plain array. Runtime guards also check item headers and aliases with mutable scalar banks. | Linear header validation remains; managed or inconsistent arrays use the original deep copy. Suspension, inspection and opaque calls keep snapshot ownership. |

Cache limits constrain retained optimization storage, not gameplay entities,
task counts or valid program sizes. `activation_cache_bytes`,
`binding_schedule_bytes` and `schedule_scratch_bytes()` expose the corresponding
idle storage costs. Allocation counters do not measure peak or total memory.

### Tiny benchmark evidence

The paired native measurement used Clang 19.1.7, Release `-O3`, SLJIT and VM,
sanitizers off and test assertions enabled. Baseline and optimized executables
ran sequentially on CPU 2 under the unchanged 60-second supervisor. Timed loop
bodies and workloads match the benchmark-only baseline above; new correctness
assertions run outside the timed loops. Each row reports the median of five
trials, with the full minimum-to-maximum range in parentheses. This is a shared
host measurement, not a confidence interval or a frame-time prediction.

| Workload | Baseline time | Optimized time | Allocation calls per trial, before → after |
| --- | --- | --- | --- |
| SLJIT synchronous system, ns/root | 390 (359–651) | 388 (318–480) | 9,600 → 0 |
| SLJIT event, ns/root | 623 (491–831) | 735 (689–804) | 9,600 → 0 |
| SLJIT helper event, ns/root | 1,550 (1,386–2,001) | 1,881 (1,767–2,147) | 48,000 → 38,400 |
| SLJIT stable bindings, µs/maintenance | 54.05 (24.94–77.79) | 18.34 (12.57–35.84) | 2,364 → 2,112 |
| SLJIT binding membership churn, µs/maintenance | 43.66 (24.27–161.18) | 44.22 (20.81–84.62) | 2,406 → 2,238 |
| SLJIT one-item array, µs/dispatch | 0.438 (0.437–0.697) | 0.364 (0.362–1.840) | 288 → 128 |
| SLJIT 128-item array, µs/dispatch | 2.090 (1.594–3.925) | 0.871 (0.869–0.896) | 288 → 128 |
| VM 128-item array, µs/dispatch | 59.25 (42.19–73.58) | 89.52 (42.66–127.69) | 352 → 128 |
| Presentation-only edit, ms/preparation | 0.611 (0.499–1.296) | 0.365 (0.201–0.793) | 624 → 203 |
| Unchanged-source control, ms/preparation | 0.359 (0.224–0.684) | 0.561 (0.165–1.056) | 203 → 203 |
| Identical scheduler refresh, ns/pass | 5,106 (3,751–13,654) | 2,037 (1,239–6,383) | 8,320 → 0 |
| Warm scheduler dispatch, ns/pass | 650 (630–964) | 2,340 (642–2,918) | 384 → 0 |

The compiled 128-item loop is the clearest timing improvement in this sample
(about 2.4×). Its allocation traffic falls from 367,104 to 46,592 requested bytes
per 32 dispatches. That combined fixture also benefits from activation reuse;
the separate ownership tests isolate the eliminated snapshot copy.
Presentation edits reuse all four prepared sources instead of none, reducing
requested bytes from 256,860 to 75,048 per preparation. Scheduler scratch retains
3,312 bytes for this 12-system fixture (26,160 bytes at the 128-entry cap on this
ABI). These retained bytes are separate from allocation traffic.

Timing evidence for the other paths is inconclusive: several medians regress,
including the unchanged-source control, and ranges are broad. An earlier warm
scheduler sample improved from 988 to 658 ns/pass, reversing the paired result.
The repeatable evidence there is removed allocations and avoided rebuilding,
not a demonstrated wall-time speedup. Keep these distinctions when deciding
whether to extend the caches or claiming gameplay performance.

## Remaining work to evaluate

| Opportunity | Evidence and possible gain | Cost or required proof |
| --- | --- | --- |
| Reuse host and nested-call storage | Fixed activation reuse leaves host banks and nested calls allocating on each invocation. | Reset all managed and native ownership, bound retained capacity and measure helper-heavy workloads. |
| Scale binding cache misses | Stable schedules avoid rebuilding; topology changes still use the original sorter. | A cheaper graph algorithm must preserve stable ordering, conservative dependencies and cycle rejection before writes. |
| Prepare independent scheduler batches | `runtime/engine.c` disables source preparation when a native system shares the phase. `runtime/behaviors/source.c` combines source parallel eligibility across a whole cohort. | Prepare after preceding dependencies have committed; preserve observer/debug behavior, structural barriers and deterministic failure ordering. |
| Narrow compilation invalidation further | Declaration discovery still reparses sources; semantic catalog changes remain a global reuse gate. | Retain syntax with explicit ownership and track actual semantic dependencies, including debugger/source ranges. |
| Extend collection borrowing | `execution/borrow.c` rejects borrowing when it encounters broad classes of calls and writes. Per-value escape and mutation information could permit more cases. | Prove no retention, no conflicting mutation and valid lifetime across every call; debugger snapshots and suspension remain ownership boundaries. |

## Language contracts that could help

Prefer inferred, verified facts over mandatory performance annotations. A `pure`
function does not by itself prove that a passed object is unretained. Callee
bodies and exact native access/lifetime contracts must establish that fact.
Likewise, writing only to `self` does not prove query rows disjoint when a root
has several target bindings.

Three future language directions merit separate design review:

- **Call-scoped read-only collection views:** prove that a callee cannot mutate,
  return, store or suspend with a borrowed view. This could remove copies through
  helpers and native calls. The cost is a restriction on retaining those views;
  callers that need retention still require an owning snapshot. Infer the facts
  from authored bodies where possible and verify native boundary contracts.

- **Explicit reductions:** shared-target sums or minima could be combined safely
  in parallel rather than issuing conflicting writes. Integer overflow rules
  must be defined; floating-point combination order changes rounding and needs
  an explicit reproducibility decision.
- **Checked access contracts:** inferred read/write footprints can retain more
  precision through calls and joins. Optional checked declarations could help
  library authors state intent, but ordinary code should receive the same safe
  optimizations. Field-level independence needs alias and layout proofs, not
  just different field names.

Units in function signatures improve compile-time dimensional checking. They
are erased and do not inherently make arithmetic faster. Range, alias, lifetime
and effect invariants can justify optimizations only when every producer and
mutation path enforces them.
