---
name: chibi-engineering-review
description: Deep, nitpicky engineering review of Chibi changes that improves architecture, performance and code style, covering waste and cost estimates, data layout, abstraction and layers, APIs, lifetimes, compiler and language design, and leftover code. Use before every commit, when reviewing a PR, branch, commit or diff, and whenever asked for a thorough, strict or picky review of Chibi `.chibi`, C or KDL code.
---

# Chibi engineering review

Make the change cheaper to run, read and change. Architecture, performance and code style are the subject of this review and get most of the effort. Tracing the code for the estimate will surface bugs; record them in a short list at the end rather than running a separate bug hunt.

Two leading ideas run through every pass:

- **Non-pessimization.** Most slow code is slow because it does work the problem doesn't need, not because nobody hand-tuned it. Find that work, size it, and show the version without it.
- **Evidence.** Every finding carries a count, an estimate, a measurement or a named consumer. A claim you can't back is a question for "What I would measure", not a finding.

## 1. Scope

Review what the user named. Otherwise review the branch: `git diff origin/main...HEAD` plus uncommitted changes.

Read each changed file in full, not only the hunks. Read the callers and consumers of every changed declaration too (LSP references when available, otherwise grep). Done when you can say, for each changed declaration, who calls it and who reads what it produces.

## 2. Brief and estimate

Write the brief before any finding. It opens the report.

- **Transform:** what data comes in and what goes out.
- **Counts:** how many records in the typical and worst case. Count them from fixtures, the bootstrap or a scene, or estimate with arithmetic and show it.
- **Consumers:** who reads the output.
- **Layer:** hot (frame loop, VM, compiler pass, asset import, the build and test loop developers wait on) or cold (one-off tools and probes).
- **Minimum work:** the work the problem truly needs, as items × operations per item, plus bytes moved. Say whether it should take a frame, a second or a minute.
- **Actual work:** trace one item through every call it reaches, into library helpers (`append`, map inserts, `strings.*`, `xmalloc`, `pin_array_get`), and list what each step does. Multiply by the counts.
- **Growth:** whether any step grows faster than linearly in a size the user controls.
- **Gap:** actual over minimum. A gap of ten times or more is a finding even without a profiler.

Done when every line has a number or an explicit "unknown", and every unknown appears again under "What I would measure".

## 3. Passes

Run every pass that applies, in this order. Each reference file is a flat list of checks; apply each check the change touches.

| Pass | Checks | Run when |
|---|---|---|
| Performance | [references/performance.md](references/performance.md) | always |
| Data and representation | [references/data.md](references/data.md) | always |
| Abstraction, layers, control flow, functions | [references/structure.md](references/structure.md) | always |
| Failure design and lifetime | [references/failure-lifetime.md](references/failure-lifetime.md) | always |
| APIs | [references/api.md](references/api.md) | the change adds or alters `pub` declarations, runtime or SDK entry points, CLI commands or file formats |
| Compiler and language | [references/compiler-language.md](references/compiler-language.md) | the change touches `tools/compiler`, `src/language`, `Build.chibi`, syntax or semantics |
| Runtime, VM and data formats | [references/runtime.md](references/runtime.md) | the change touches the VM, JIT, WebAssembly or native backends, components and the ECS, asset loading, prepared artifacts or Edit-and-Continue state |
| Deletion and tests | [references/deletion-tests.md](references/deletion-tests.md) | always |
| Style | [references/style.md](references/style.md) | always, last |

For a change over about 400 lines, or when asked for maximum depth, run each pass in its own subagent. Give each one the brief, the changed-file list and its reference file, and have it return candidate findings with evidence. Otherwise run the passes yourself, in order.

## 4. Target shape

When the passes find structural waste or a representation problem, sketch the version that doesn't have it: the records and arrays (fields, sizes, counts), the inner loop, and the layers that remain. Keep it to a few lines of code or a short table. Often the best result is that the old code disappears rather than gets faster; say what disappears.

## 5. Verify

For each candidate finding:

1. Re-read the exact lines, and the callers the claim depends on.
2. Collect the evidence yourself: grep for the count, do the arithmetic for the estimate, run the measurement when it is cheap (`chibi check <project> --explain-costs`, a `*-benchmark` selector, a timed run), or name the consumer's file.
3. Drop it if the evidence doesn't hold, if it is on the [do-not-raise list](#do-not-raise), or if the fix costs more than the defect.

Done when every remaining finding has evidence you checked during this review.

## 6. Report

Open with the brief and the target shape. Then list findings, ordered by the cost they remove, largest first. Write each one as:

```
[CATEGORY] path:line — what is wrong
Cost: which cost it raises (run, read, change, write, debug); for PERF, the work per item and in total
Evidence: the count, estimate, measurement or consumer
Fix: the smallest change that removes it, and what it deletes
```

| Category | Meaning |
|---|---|
| `PERF` | Work the problem doesn't need, or a layout or structure that makes the work slower than the machine allows at the counts in the brief |
| `ARCH` | A representation, layer, dispatch scheme, API or lifetime design that makes the problem harder, hides cost or closes off the fast path |
| `SIMPLIFY` | Needless abstraction, state, indirection, dependency, duplicate implementation or leftover code |
| `STYLE` | A local readability or naming defect with a concrete reader cost |
| `TEST` | Missing or misdirected verification of behavior or performance |

Then **Correctness notes**: bugs you noticed, one or two lines each with path:line and evidence. List a build or `main` break first. Keep this list short; it is not the point of the review.

End with three sections:

- **What I would delete:** code, paths, flags and files the change makes obsolete or leaves dead.
- **What I would measure:** the unknowns from the brief, and the claims that need numbers.
- **What I would leave alone:** code that looks improvable where the cleanup would cost more than it saves, and why.

If nothing substantive turns up, say so plainly and still give the three sections.

When you are reviewing your own work before a commit, fix the findings you accept, rerun the affected tests and measurements, and note any you reject and why. When a `ReportFindings` tool is available and the user asked for the review, report findings through it (category is the category in lowercase, `bug` for correctness notes) and keep the brief, target shape and three sections in your message.

## Do not raise

Raise only defects that have a cost and evidence. These recurring reviewer reflexes have neither, so skip them:

- Function length by itself. A long procedure is a finding only when it isn't a lexer or state machine, a `switch` over a closed set, or a pipeline of commented blocks, or when it hides sections that deserve their own names.
- Extracting single-use sequential code into helpers.
- An interface, option, configuration point or generalization for a caller that doesn't exist.
- Hiding fields, adding getters and setters, or visibility ceremony inside a package.
- Merging short duplicates whose copies may diverge or read better apart.
- Defensive NULL checks, fallback values or "log and continue" for states only a bug produces.
- A unit test for a helper whose behavior a real-path test already covers.
- SoA, handles, SIMD, threading or caching without the access pattern and batch size, or while waste remains in the loop.
- Micro-tuning (manual unrolling, branch hints, intrinsics, bit tricks) outside a measured hot spot.
- Lock-free structures or atomics without measured contention.
- Speed folklore ("arrays beat lists", "X is faster than Y") without a measurement on this code.
- Anything the formatter decides.
- "Another style is possible."

## Calibration

A good finding names one cost, sizes it and shows the version without it. The first example comes from a review of a test commit; the others point at code on `main`:

```
[PERF] tools/compiler/package_compiler/CallCensus.chibi:76-96 — LineOf and ColumnOf rescan the document from byte 0 for every call site
Cost: run — O(sites × bytes): two scans per site, each half the document on average, ≈ 1,779 × 191,198 ≈ 340 million ByteAt calls for Body.chibi alone, against zero for stored fields
Evidence: SnapshotNode already stores row and column (Snapshot.chibi:37-38); 1,779 call sites in the 191,198-byte Body.chibi
Fix: read forest[node].row + 1 and column + 1; delete LineOf, ColumnOf and the two CallSite fields
```

```
[PERF] tools/compiler/package_compiler/Tree.chibi:51 — node kinds compared by string inside every tree walk
Cost: run — one string comparison per node per kind test, in every pass over every compiled file
Evidence: 662 lines in tools/compiler call `Is(forest, ...)`; SnapshotNode.kind is a string
Fix: intern kinds to an integer tag when the forest is built and compare tags; the string table moves to diagnostics
```

```
[ARCH] tools/compiler/package_compiler/Body.chibi:2633 — EmitBody threads about 30 parameters through its callees
Cost: change — adding pass state means editing every signature and call site in the chain
Evidence: the same parameter group appears in the signatures of its callees
Fix: one emitter state record passed by reference
```

Bad findings restate a rule without cost or evidence:

```
[SIMPLIFY] Body.chibi:2633 — EmitBody is over 1,000 lines; split it into smaller functions
[PERF] Body.chibi:3001 — use an array instead of a map here, arrays are faster
```

The first procedure dispatches over a closed set of node kinds, so its length is its role; its real defect is the threaded parameters. The second gives no count, no access pattern and no measurement.
