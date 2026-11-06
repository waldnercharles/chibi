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
one idle activation per program, up to 64 KiB including fixed host banks,
completed helper frames and retired payload backing. The cache rebinds owner, arguments, services and state,
clears borrowed values, and releases live payload, task and request ownership.
Failed, cancelled, inspected and resumed work
is excluded. Prepared reduction batches retain compact owner/variable IDs and
reuse a streaming activation when eligible. Other prepared paths still need
simultaneous activations per row.
Value filters use a temporary admission bitmap to preserve evaluation before
body writes. Query cache limits bound cached
matching rows, not the separately owned state or task results.
Predicate evaluators share a separate 64 KiB per-program idle budget, including
their host banks, helper frames and arenas. Each active evaluator is detached
from the cache, so reentrant work cannot reuse its live values. Low retained
memory does not imply zero allocation traffic: managed snapshots and query
results can still require temporary storage. Measure both
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
| Synchronous activation reuse | A successful first invocation has released all transient ownership before its storage is reused for the same immutable function. | One idle frame per program, at most 64 KiB including host banks, VM scratch, idle helper frames and retired payload backing; oversized or escaping payloads still allocate. |
| Predicate evaluator reuse | Only a boolean result escapes a synchronous predicate; each immutable function's scratch is reset and services are rebound. | All idle evaluators together retain at most 64 KiB per program; failures and oversized scratch are released. |
| Reduction field metadata | Registered component schemas are immutable; cached field shape cannot change inside the program's world. | Eight fixed entries per program; live entity generation, component presence and layout are still checked for every contribution and preflight record. |
| Reactive binding order cache | The complete ordered live work stream and program generation identities still match the cached dependency topology. Values and dynamic targets are reevaluated. | One schedule per owning program, at most 16 programs, 1,024 rows and 64 KiB. Cache misses use the existing sorter; larger workloads execute normally. |
| Unchanged scheduler registration | Every copied descriptor field, callback, effect, phase and dependency is equal, and no prior error needs recovery. | Registration retains its existing descriptor. Batch scratch retains at most 128 entries; recursive and larger batches use temporary storage. |
| Presentation-only compilation reuse | Capability category, keywords and description have no executable meaning; all semantic metadata and binding generations still match. | Fresh catalogs own current display text. Parsing/declaration discovery and semantic edits still do their existing work. |
| Read-only array snapshot borrowing | The complete synchronous body cannot mutate, retain or observe the identity of a plain array. Runtime guards also check item headers and aliases with mutable execution storage. | Linear header validation remains; managed or inconsistent arrays use the original deep copy. Suspension, inspection and opaque calls keep snapshot ownership. |

Cache limits constrain retained optimization storage, not gameplay entities,
task counts or valid program sizes. `activation_cache_bytes`,
`query_cache_bytes`, `binding_schedule_bytes` and `schedule_scratch_bytes()` expose the corresponding
idle storage costs. Retired payload backing belongs to activation_cache_bytes,
not live payload_capacity. Allocation counters do not measure peak or total memory.

### Tiny benchmark evidence

The paired native measurement used Clang 19.1.7, Release `-O3`, SLJIT and VM,
sanitizers off and test assertions enabled. Baseline and optimized executables
ran sequentially on CPU 2 through the production supervisor. Timed loop
bodies and workloads match the benchmark-only baseline above; new correctness
assertions run outside the timed loops. Each row reports the median of five
trials, with the full minimum-to-maximum range in parentheses. This is a shared
host measurement, not a confidence interval or a frame-time prediction. Array
rows were remeasured after extending the guards to saved and VM scratch storage.

| Workload | Baseline time | Optimized time | Allocation calls per trial, before → after |
| --- | --- | --- | --- |
| SLJIT synchronous system, ns/root | 390 (359–651) | 388 (318–480) | 9,600 → 0 |
| SLJIT event, ns/root | 623 (491–831) | 735 (689–804) | 9,600 → 0 |
| SLJIT helper event, ns/root | 1,550 (1,386–2,001) | 1,881 (1,767–2,147) | 48,000 → 38,400 |
| SLJIT stable bindings, µs/maintenance | 54.05 (24.94–77.79) | 18.34 (12.57–35.84) | 2,364 → 2,112 |
| SLJIT binding membership churn, µs/maintenance | 43.66 (24.27–161.18) | 44.22 (20.81–84.62) | 2,406 → 2,238 |
| SLJIT one-item array, µs/dispatch | 0.437 (0.435–0.728) | 0.426 (0.396–4.623) | 288 → 128 |
| SLJIT 128-item array, µs/dispatch | 1.648 (1.611–9.286) | 0.946 (0.932–8.746) | 288 → 128 |
| VM 128-item array, µs/dispatch | 45.59 (41.72–76.70) | 56.00 (46.00–59.62) | 352 → 128 |
| Presentation-only edit, ms/preparation | 0.611 (0.499–1.296) | 0.365 (0.201–0.793) | 624 → 203 |
| Unchanged-source control, ms/preparation | 0.359 (0.224–0.684) | 0.561 (0.165–1.056) | 203 → 203 |
| Identical scheduler refresh, ns/pass | 5,106 (3,751–13,654) | 2,037 (1,239–6,383) | 8,320 → 0 |
| Warm scheduler dispatch, ns/pass | 650 (630–964) | 2,340 (642–2,918) | 384 → 0 |

The compiled 128-item loop's median improves by about 1.7× in the refreshed
sample, with outliers in both builds. Its allocation traffic falls from 367,104 to 46,592 requested bytes
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

### Call views and shared-target reductions

This follow-up starts at main
`cf00d3ccf76dd81e94ddba55a0210b86434e2100`. Its helper benchmark baseline is that
tree plus the benchmark-only helper workload, with identical timed bodies in the
candidate. Native measurements use Clang 19.1.7, Release `-O3`, test assertions enabled
and sanitizers off. Correctness runs separately with ASan/UBSan and WasmJIT.

The paired helper measurements ran sequentially on CPU 2. Each cell is the median
of five trials, with the minimum–maximum in parentheses, in µs per dispatch.
Allocation traffic is requested bytes through `xalloc` per 32 dispatches, not
live or peak memory. One warmup precedes the measured trials.

| SLJIT array workload | Baseline time | Call-view time | Requested bytes, before → after |
| --- | --- | --- | --- |
| Direct, 128 items (control) | 0.934 (0.921–3.164) | 2.153 (0.977–5.420) | 46,592 → 46,592 |
| One helper, 1 item | 0.840 (0.834–0.881) | 1.091 (1.088–1.137) | 274,176 → 270,336 |
| One helper, 128 items | 2.183 (2.160–3.895) | 1.755 (1.747–24.119) | 667,392 → 270,336 |
| Two helpers, 1 item | 1.258 (1.206–3.141) | 1.725 (1.714–6.406) | 483,840 → 480,256 |
| Two helpers, 128 items | 6.416 (3.099–11.863) | 2.497 (2.414–4.731) | 1,140,224 → 480,256 |

The 128-item helper paths remove about 59% of allocation traffic, with no new
author annotation. The one-item paths regress in this sample: alias/header
validation has a cost even when little data would be copied. The unchanged direct
control also varies substantially, so these timings cannot establish universal
speedups. VM helper paths reduce allocation traffic too; interpreter and shared
host timing noise remain large. Further work should reduce repeated guards or
reuse nested-call storage before adding more view syntax.

The reduction fixture uses two SLJIT systems, 256 contributors, one shared target
and two fields: 1,024 contributions per pass. Five trials each run eight passes
after one warmup. The final measurement uses CPUs 2–3 and a verified two-worker
budget; both worker IDs participated in every parallel trial. These are µs per
pass, with the same median (minimum–maximum) convention.

| Update mode | Time | Stored records per pass | Published writes per eight-pass trial |
| --- | --- | --- | --- |
| Ordinary writes, serial | 2,500 (2,036–3,605) | 0 | 8,192 |
| Ordered reductions, serial | 2,930 (2,514–3,380) | 1,024 | 8,192 |
| Ordered reductions, two workers | 2,273 (2,057–3,065) | 1,024 | 8,192 |
| Unordered sums, serial | 1,936 (1,800–2,177) | 4 | 32 |
| Unordered sums, two workers | 1,503 (1,472–2,024) | 4 | 32 |

Unordered sums reduce the median by about 34% relative to ordered sums in both
executor modes for this shared-target fixture. They reduce allocated log/hash
capacity across the two programs from 73,824 to 2,624 bytes. That gain purchases
different rounding and publication counts; it is opt-in, not a compiler rewrite
of ordinary ordered addition. Ordered serial reductions are slower than ordinary
writes here. Parallel stable reductions preserve ordering without guaranteeing
that every workload amortizes collection and worker dispatch.

This measurement also includes streaming for simple stateless reducers. It
retains zero per-entity instance headers, 4,192 bytes of row-ID capacity and
3,376 bytes of idle activation storage across the two programs. The ordinary
write control still retains 512 instance headers totaling 147,456 bytes. Each
individual reduction-log, row-ID and activation cache has a 64 KiB idle cap;
active work may exceed it and remains proportional to the workload.

Before enabling this streaming path, the reduction trial requested 37,083,328
bytes through 118,752 allocation calls; afterwards it requests 30,196,288 bytes
through 106,512 calls. The same counts apply to ordered and unordered modes:
coalescing removes publication work but does not eliminate the remaining host
and evaluator allocations. The initial parallel sample also exposed a native
build bug: a web-only macro overrode the requested two-worker budget with four.
That gate is now restricted to threaded web builds. Initial four-worker timings
are not a paired comparison for the final two-worker measurements.

## Execution-storage and row-worker measurements

This follow-up starts from main
`2b1d9139523c4676a6cb0dfc7a38b58dc5a49b22`. Preserved executables from that
tree provide the baseline for unchanged activation, array and two-system
reduction benchmark bodies. The candidate uses Clang 19.1.7, Release `-O3`,
SLJIT, assertions enabled and sanitizers off. Runs are sequential, with CPU 2
for activation/array fixtures and CPUs 2–3 for reductions. Configuration and
builds finish before measurement; every selector uses the production supervisor.
The following values are medians of five trials, with minimum–maximum ranges.

| Two-system fixture, µs/pass | Main baseline | Complete reuse and metadata cache |
| --- | --- | --- |
| Ordinary writes, serial | 2,516 (2,061–3,029) | 1,458 (1,332–1,599) |
| Ordered reductions, serial | 2,787 (2,545–3,329) | 711 (662–855) |
| Ordered reductions, system workers | 2,524 (2,372–2,788) | 921 (739–969) |
| Unordered sums, serial | 2,354 (2,035–3,423) | 586 (533–651) |
| Unordered sums, system workers | 1,653 (1,564–2,080) | 777 (732–1,206) |

These are the existing 256-contributor, two-system workloads, not the new row
executor. All modes reduce warm allocation calls from 106,512 to 16 and
requested bytes from 30,196,288 to 576 per eight passes. The remaining 16 calls
allocate source dispatch cohorts, not per-row values. Reducer activation caches
grow from 3,376 to 13,896 bytes across both programs. This sample supports a
reduction-path improvement, but does not show
system workers beating the new serial path at this size.

An intermediate fixed-storage candidate, `441b30cf774dcfca261bf968c469827679ada055`,
still allocated completed payload backing on every row. The source allocation
model accounted exactly for its remaining 12 calls and 1,660 bytes per row.
Reusing that backing through the existing arena reset path preserves the same
end-of-invocation lifetime and includes every retained byte in the cache cap.
A sequential paired run against that intermediate executable reduces the ordered
serial median from 1,059 (747–1,407) to 711 (662–855) µs, and warm traffic from
49,168 calls / 6,799,936 bytes to 16 / 576. Retained activation backing increases
by 3,320 bytes across both programs. The new ownership/cap tests separately check
escaped strings, changed inputs, failure/wait boundaries and oversized eviction.

At the fixed-storage stage, the scalar helper-event fixture falls from 38,400 SLJIT allocation calls and
24,012,800 requested bytes to zero per 3,200 invocations. Every direct, one-helper
and two-helper array mode also reaches zero warm allocations; the two-helper
SLJIT fixture previously made 640 calls requesting 480,256 bytes per 32
dispatches. VM fixtures reach zero as well. This is bounded reuse, not an
allocation-free language guarantee: escaping state/task results and query snapshot
items still allocate in other workloads. Tiny timings remain inconclusive.
For example, the one-item two-helper SLJIT median improves from 1.72 to 1.41 µs,
while its 128-item median regresses from 2.40 to 3.27 µs amid broad outliers.
The scalar helper-event median also regresses from 1.60 to 2.33 µs in this sample.

The final single-program benchmark separately compares serial execution with
requested two-worker rows. Each trial performs four passes; the heavy helper
performs 64 integer-mixing iterations per row. An earlier fixed-storage run had severe variation
(the unordered heavy serial median was 20.56 ms while ordered was 3.47 ms), so
one diagnostic repeat tested whether that difference persisted. It did not.
Final payload-reuse timings below still have overlapping ranges and are not a
robust speedup claim.

| Row workload, µs/pass | Serial | Requested two-worker rows |
| --- | --- | --- |
| 16 light, unordered | 20.2 (17.9–51.3) | 17.8 (17.8–58.2), serial fallback |
| 1,024 light, unordered | 1,973 (1,786–2,117) | 2,526 (1,976–4,682) |
| 1,024 heavy, unordered | 3,014 (2,400–6,035) | 2,439 (2,170–2,788) |
| 1,024 heavy, ordered | 2,934 (2,802–3,664) | 2,861 (2,507–3,731) |

Both worker IDs participated in every large parallel trial. Unordered rows
retain 39,588 bytes of private worker scratch, plus 8,240 row-ID bytes and 1,312
parent log/hash bytes; ordered rows retain 36,964 worker bytes. Every unordered
mode now has zero warm allocation calls. Ordered logs at
this size exceed the idle cap and are freed, so repeated dispatches still
allocate. Active contribution storage scales with rows. Unordered workers
preserve two globally coalesced records per system; ordered work preserves all
2,048 records and their publication order. The 16-row control launches no jobs.

Measured allocation removal justifies the bounded reuse. The row executor is
correctness-verified but these timings do not establish a general parallel gain
or justify broadening eligibility. More selective scheduling should use measured
body/merge costs rather than entity count alone. No timing threshold is a test
assertion, and no automatic regrouping of stable sums is introduced.

## Remaining work to evaluate

| Opportunity | Evidence and possible gain | Cost or required proof |
| --- | --- | --- |
| Remove repeated reference construction | Completed payload backing now reuses storage; each component path still constructs and validates transient reference objects. | Prove presence, layout and storage stability across the exact body; suspension, structural changes and opaque calls invalidate such proofs. |
| Scale binding cache misses | Stable schedules avoid rebuilding; topology changes still use the original sorter. | A cheaper graph algorithm must preserve stable ordering, conservative dependencies and cycle rejection before writes. |
| Broaden proven worker eligibility | Preparation occurs per dependency batch. Scalar helper closures can be proved independent; helpers with component access, managed values or opaque effects remain conservative. | Carry verified component origins/effects through calls; preserve observer/debug barriers, failure ordering and serial publication. |
| Narrow compilation invalidation further | Declaration discovery still reparses sources; semantic catalog changes remain a global reuse gate. | Retain syntax with explicit ownership and track actual semantic dependencies, including debugger/source ranges. |
| Extend collection borrowing | Synchronous authored helper chains now qualify; opaque native calls, recursion, managed elements and potentially escaping results still use owning copies. | Prove per-value retention and mutation, verify native lifetime contracts and preserve debugger/suspension ownership. |

## Language contracts that could help

Prefer inferred, verified facts over mandatory performance annotations. A `pure`
function does not by itself prove that a passed object is unretained. Callee
bodies and exact native access/lifetime contracts must establish that fact.
Likewise, writing only to `self` does not prove query rows disjoint when a root
has several target bindings.

The call-view/reduction follow-up implements two of the proposed contracts:

- **Call-scoped read-only collection views:** inferred for plain arrays through
  verified synchronous authored helpers. No keyword is required. Mutation,
  retention, recursion, opaque callbacks and suspension retain owning copies.
  Runtime guards check actual headers and aliases, including future callee state
  banks. Native boundary contracts remain future work.

- **Explicit reductions:** ordered add/min/max collect contributions privately
  and publish in stable system/query order. `reduce unordered` opts into per-target
  sum aggregation within a system. Integer addition wraps at its storage width;
  ordered writes preserve per-write range clamps. Unordered sums require unbounded
  fields and may change floating-point results and write-revision counts. Systems
  can run concurrently. A sufficiently large stateless reducer can also split
  its rows into contiguous worker chunks: private logs merge in original row
  order, then publish serially. Global coalescing preserves one unordered write
  per destination per system. A producer cannot otherwise access its reduced
  component or suspend.

The row executor requires a proved synchronous body and allows reachable numeric
scalar helper chains without references, world access or side effects. Query
admission happens before worker execution. Debugging, profiling, state, tasks,
imports, ordinary writes, mixed ordered/unordered modes and insufficient proofs
keep serial execution. The scheduler chooses row work only for a singleton
system batch; independent multi-system batches keep their existing executor.
Worker scratch retains at most 1 MiB in total, with at most the executor's worker
budget (capped at 32) private contexts. Active logs remain proportional to work.

Further language changes should earn their cost with measured workloads:

- **Checked access contracts:** inferred read/write footprints can retain more
  precision through calls and joins. Optional checked declarations could help
  library authors state intent, but ordinary code should receive the same safe
  optimizations. Field-level independence needs alias and layout proofs, not
  just different field names.
- **Verified native view contracts:** let a native helper promise and enforce
  no retention or mutation of borrowed inputs. This could extend copy elimination
  across the host boundary; incorrect declarations would be an engine bug.
- **Ordinary row writes:** unique target ownership would help prove write
  independence, but parallel execution must also preserve first-failure behavior.
  Staging writes or proving the entire row cannot fail would be necessary before
  extending the implemented reduction path. An exclusive relationship alone does
  not prove that different sources have different targets.

Units in function signatures improve compile-time dimensional checking. They
are erased and do not inherently make arithmetic faster. Range, alias, lifetime
and effect invariants can justify optimizations only when every producer and
mutation path enforces them.
