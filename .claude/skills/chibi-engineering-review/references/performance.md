# Performance

Three kinds of performance work exist. *Optimization* measures the machine's limit and closes the gap; it is rare and reserved for measured hot spots. *Non-pessimization* means writing code that does only the work the problem needs, and it applies to every line. *Fake optimization* repeats speed tips without measuring them. This pass is about non-pessimization, and it uses the brief's estimate as its yardstick.

## Contents

- Estimate
- The multipliers, in order
- Structural pessimization
- Hoisting, batching and specialization
- Byte and record loops
- Claims and posture
- Measuring in this repository

## Estimate

- **Growth first.** Before any constant factor: can the work grow faster than linearly in a size the user controls? Assume that size reaches a thousand times what was tested. A linear search inside a loop over growing input, a rescan per item, or a lock held across input-sized work is the usual cause. *Evidence:* time or count the operation at n and 4n; quadratic work grows sixteen-fold.
- **Minimum work.** Count the items (nodes, tokens, entities, bytes, triangles) and the operations each one truly needs, and multiply. Add the bytes each item moves.
- **Machine scale.** A core runs on the order of 10⁹ simple operations per second, about 8× that across 8-wide SIMD lanes, and has a few cores. An L1 hit costs about 4 cycles, L2 about 12, main memory hundreds. L1 holds about 32 KB and L2 a few hundred KB. Aim for the right order of magnitude: should this take a frame (16 ms), a second or a minute?
- **Actual work.** Trace one item through every call it reaches, including library helpers, and list what each step does. Reading a callee is part of the review: the general routine "does what you want, but not the way you want".
- **Anomalies, not just the top.** Profile for code that costs more than the estimate says it should, including mid-ranked functions (0.05–1% of the time); the top of the profile is often where everyone already looked.
- **Gap.** Actual over minimum. A gap of 10× or more is a `PERF` finding without a profiler. A gap of 100× or more usually means the structure, not a loop, has to change.

## The multipliers, in order

Apply them in this order. Each later one only pays once the earlier ones are done.

1. **Waste:** instructions the problem doesn't need. This is almost always the largest factor. Look for:
   - allocation per element, including growth inside an append loop and allocations hidden in containers or string helpers;
   - copies: value copies of arrays, tables or records (Chibi arrays copy on write, and tables passed in and returned as `more_*` are the usual suspect), and building strings only to compare or look them up;
   - hashing, map inserts and string comparisons inside per-element loops, including kinds compared by name;
   - re-parsing or re-deriving a fact an earlier step already had (type names kept as text, offsets turned back into line and column);
   - quadratic rescans: searching or scanning from the start for every item;
   - repeated traversal: several passes over the same data where one would do, or a gather repeated per item (rays × entities instead of rays + entities);
   - per-item calls across an expensive boundary (VM to native, host to guest, the OS, the file system) where a batch would do;
   - work for rare or legacy cases paid on the common path;
   - memory cleared or initialized and then immediately overwritten.
2. **Instructions per clock.** Look for long serial dependency chains, unpredictable branches inside hot homogeneous loops (sort or group by kind first), and pointer chasing that serializes loads. Classify each hot branch as predictable or data-random; a predictable branch costs almost nothing, so branchless rewrites pay only for data-random ones.
3. **SIMD width.** Can a hot loop over many same-shape items process 4 or 8 at a time? That needs flat arrays of same-size values, no per-item calls, and branchless accumulation with one reduction at the end. Raise it only after the waste is gone.
4. **Caching and data movement.** Count bytes moved per item. Look for hot and cold fields mixed in one record, padding, pointer graphs where flat arrays fit, and record size × count against cache size. The layout checks themselves are in [data.md](data.md).
5. **Threads.** Only after the single-threaded inner loop is right, because that loop defines what the threads distribute. Multithreading a structurally slow routine is a finding against the structure.

## Structural pessimization

These are architecture choices that close off the fast path, so later fixes become rewrites. Each one is an `ARCH` or `PERF` finding, depending on whether the cost is structural or local.

- A per-object heap graph or array of pointers where a flat array of values, or one array per kind, would do.
- Virtual dispatch, callbacks or a hierarchy around hot code. The call itself is cheap; the real loss is that the compiler can't inline or merge work across it.
- A chatty per-item API where the consumer naturally takes a batch.
- An interpreter or indirection layer inside a hot path.
- Data kept in the form its producer had rather than the form its hot consumer queries. Precompute a query-shaped form when data is read far more often than it changes: a grid instead of a general tree when every query points the same way, baked assets, compiled graphs.
- A general structure where the caller knows an invariant: static data, a fixed direction, bounded counts, a closed set of kinds.
- "Fix the hot spots later." Costs spread through layout and dependencies don't gather into hot spots. Ask whether this decision closes off a later fast path.

## Hoisting, batching and specialization

- Is loop-invariant work moved out of the loop, and data gathered once before it?
- Is capacity reserved before appending in a loop of known size?
- Does the routine compute only the outputs the caller uses? Is "absent" encoded in a sentinel when that is free, instead of a separate flag?
- Would specializing to the caller's invariants delete general machinery outright? The best result is that the old code is gone, not made faster.
- Does an algorithm terminate reliably with a tunable bound, or does it retry until an arbitrary failure count?


## Byte and record loops

- Does a byte loop classify bytes with a 256-entry table, rather than chains of comparisons?
- Is the input padded so the hot loop needs no per-byte bounds check, with errors accumulated and checked once at the end?
- Are escapes and other rare cases on a separate path?
- Is a lookup table rebuilt on every call when it could be built once per process?
- Is memory reused across frames and files? Count first touches and zeroing of fresh pages, not only allocator calls; zeroing a fresh 8 MB buffer every frame costs milliseconds.
## Claims and posture

- **Fake optimization.** A speed claim without a measurement on this code ("arrays beat lists", "this is faster") is a finding against the claim, not support for it.
- **Misplaced tuning.** Hand-tuned code outside a measured hot spot raises the read and change costs for nothing. Treat it as `SIMPLIFY`.
- **Layer posture.** Hold hot layers (frame loop, VM, compiler passes, asset import) to cost from the start. Tools, the CLI and probes may be simple until people wait on them; once they do, the second pass is due.
- **Two report formats.** A kernel claim (bytes per cycle, ns per bytecode) comes from an optimized build without sanitizers, with setup outside the timed region, many trials, input the branch predictor can't memorize, and the minimum and mean. A compile, build or frame claim gives elapsed time, its share of the total and the spread, plus the worst frame, against a tuned baseline. Check a surprising timing with the matching hardware counter (cache misses, mispredictions, instructions per cycle). Differences within about one standard deviation are noise.
- **Prediction first.** A performance change states the baseline and a numeric prediction before the work. Without the prediction, a lucky result can't be told from an understood one.
- **Every `PERF` finding states three things:** the work removed (per item and in total), the estimated factor, and how to confirm it (a benchmark selector, `--explain-costs`, a timed run).

## Measuring in this repository

- `chibi check <project> --explain-costs [--json]` reports static allocation, copy and retention evidence for each finalized function and its callees. See `docs/cost-explanations.md` for what it does and doesn't cover.
- `make benchmark` builds a release configuration and runs the `*-benchmark` selectors in the `benchmarks` group of `tools/test_policy.py`.
- For anything else, time the workload before and after on the same machine, with the same input, and report both the average and the maximum.
