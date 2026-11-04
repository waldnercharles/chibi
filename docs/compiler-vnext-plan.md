# Chibi vNext compiler: plan and acceptance gates

Status: approved direction, implementation starting on a separate development branch.
Date: 2026-11-04 UTC.
Starting source: `02c86287430def964445ec90185961e20319536d`, tree
`780ba87ea9f29308f2f20a94381cdce8cc5d26ca`.

This work proceeds alongside the existing native self-hosting branch. It does not
replace that branch's pending correctness, platform, bootstrap, or release gates.
No release or default-toolchain switch is part of this first checkpoint.

## Goal

Reach at least **100,000 physical input lines per second for a full,
non-incremental development build**, including a usable linked executable or
fully finalized eager JIT output. Faster iteration must not come from unusably
slow generated programs, missing diagnostics, unsafe ownership, skipped work, or
unreported caches. Optimized shipping builds have a separate latency and code
quality budget; the development target is not a promise of LLVM-quality
optimization in the same time.

Current syntax, semantics, runtime design, hashing, and LLVM are all open to
change. The language is currently used internally. Compatibility is a choice
supported by actual consumers, not a universal acceptance requirement. Every
deliberate break nevertheless needs a documented new rule, migration example,
lost capability, and tests for the new behavior.

## Architecture direction

1. Reuse useful flat indexed tokenizer/parser snapshots, package resolution,
   names, signatures, and generic/type logic where they fit the chosen contract.
   A new parser is not a goal by itself.
2. Construct one authoritative typed function body when semantic decisions are
   made. Record resolved calls, types, narrowed values, places, source origins,
   and effects. Native lowering must not infer these facts again from syntax.
3. Lower language/runtime operations once into explicit target operations.
   Arithmetic, ownership, cleanup, roots, layout, and any retained task behavior
   belong here. A backend's private IR is disposable, not a second authority for
   Chibi semantics.
4. Use one compiler process and dependency-aware jobs, contiguous stores,
   interned identities, and compilation/scratch/result lifetimes. Batch work at
   real boundaries rather than launching a process or crossing FFI per element.
5. Share lowered bodies between eager JIT and AOT. JIT finalizes callable code;
   AOT emits objects and invokes a platform linker. Keep inputs and generated
   bodies in memory until an output requires materialization.
6. Expose small ordinary Chibi build and inspection APIs over the same model.
   Add capabilities for demonstrated consumers rather than exposing every
   mutable compiler implementation detail.

The current replacement target is the repeated prepared-emission/body traversal,
sparse facts, scalar rows, LLVM plan, LLVM text, and external `opt`/`llc` chain.
Prepared bytecode contains VM-specific choices; `compiler_facts` is incomplete;
`scalar_lowering` is only a bounded bool/int bridge. None is automatically the new
universal representation. Temporary comparison paths must have deletion gates.

### Backend decision

**Cranelift is the leading external-backend candidate, not a predetermined
winner.** Evaluate compilation-focused settings, code quality, distribution,
target support, and debugging before committing to it. Use a versioned C ABI
and prebuilt backend/runtime binary. Rust's native ABI must not cross the
boundary. Send batches of functions/modules rather than one FFI call per IR
instruction.

A bounded TPDE-LLVM experiment can isolate machine-code generation cost on
supported Linux inputs. It supports only part of LLVM IR and Linux ELF
x86-64/AArch64; record or disable fallback. It is not an all-platform promise or
a whole-build speedup claim. Its public benchmark multipliers are not Chibi
measurements.

A custom direct emitter is an alternative if measured requirements cannot be
met economically by a library backend. That choice owns instruction selection,
register handling, ABIs, relocations, object formats, stack probing, unwind and
debug support. Do not take on that maintenance merely to avoid a dependency.
LLVM may remain an optional optimizing backend if its code quality earns its
cost. No backend is removed before consumers have a deliberate replacement.

### Language decisions and migration ledger

These are decisions to resolve with concrete examples during the work, not
already implemented changes:

- Retire legacy library-wrapper declarations and their distinct retained bulk
  ABI unless a real program needs them. Prefer ordinary packages/functions.
- Compare explicit scoped borrow/inout parameters against body-dependent calling
  conventions. Keep managed references distinct; do not introduce a general
  lifetime-annotation system without a demonstrated consumer.
- Start the compiler/tooling slice with synchronous functions. The audited
  non-test tools/packages/apps sources contained no executable `await`/`yield`
  tokens, but separate games/editors were not audited. Product task support is a
  later explicit choice, not silently deleted. Retaining tasks requires a chosen
  state-machine/runtime lowering, not merely replacing `llc`.
- Compare current COW values with mutable containers plus explicit clone.
  `native_driver/Assertions.chibi` and `TestProgram.chibi` deliberately copy and
  then mutate snapshots. Port those consumers before changing assignment to
  alias. Consider explicit mutable builders for append-heavy compiler passes.
- Compare current cross-generation migration with exact-schema retention and
  cancellation/restart, or plain rebuild/restart. Current host/watch code has a
  real live-state consumer. Lost state/continuations is an explicit trade-off.
- Consider scoped borrows and per-compilation arenas for compiler graphs whose
  durable links are indices. No escaping-pointer proof exists yet. Reject or
  migrate escapes before removing collection; a process-lifetime arena is not a
  substitute for releasing memory between compilations.
- Specify integer/float behavior across compile-time and runtime evaluation.
  Do not acquire undefined behavior or nondeterministic floating point by
  accident. Changed arithmetic gets positive, boundary, and rejection tests.
- Keep native Linux/macOS/Windows and browser requirements visible. The first
  diagnostic target may be Linux x86-64; this does not establish product scope.
  Cranelift native output does not itself provide a browser Wasm backend.

Each decision records the actual uses, old/new examples, benefit hypothesis,
ergonomic cost, migration inventory, acceptance tests, and deletion list.

### JIT, runtime, and build effects

JIT compilation is explicit editor/load/background work, not surprise work in a
latency-sensitive frame. Background compilation also competes for CPU and memory;
measure frame-time tails and enforce scheduling budgets. Publish complete
generations at safe points. Do not free code while it can execute or be reached
through callbacks/tasks. Adaptive tiering is not a default requirement.

Scalars stay unboxed, aggregate layouts are declared per target, and ownership
of containers, references, roots, OS resources, and diagnostics is explicit.
During development, real C-compiled fixtures test integer extension, bools,
floats, pointers, borrowed arrays, argument order, temporary lifetime, callbacks,
and reentry. Ordinary users should not need a C/Rust development toolchain to
compile Chibi programs; ship the selected runtime/backend binaries.

Generated source or transformed bodies must be checked again. Compile-time
execution uses the chosen language arithmetic/effect contract and resource
budgets. Record build-time files, environment, and other inputs. Replacing the
evaluator with JIT must not silently change accepted effects or budget semantics.

## Measurement contract

- Pin the source commit/tree, complete source closure, compiler/backend/runtime
  identities, target, flags, hardware, OS, CPU limits, and worker count.
- Start the outer monotonic timer before launching a fresh compiler process and
  stop when its output is runnable or all required JIT functions are callable.
  Include startup and initialization, source loading, imports, resolution, checking,
  specialization, compile-time work, lowering, verification, code generation,
  object handling, and finalization/linking.
- Use empty compilation caches and fresh output directories. No persistent
  compiler server, reused AST/semantic/IR/object/application artifact, lazy
  omission of required bodies, or precompiled application dependencies counts
  as a full build. Installed compiler/runtime binaries are declared prebuilt
  dependencies; rebuilding them is a separate workload.
- Report warm OS page-cache runs for a comparable working-loop measure. Report
  genuinely measured cold-storage runs separately. A new output directory is
  not evidence of cold source reads.
- Count physical original source lines including comments/blanks, alongside
  noncomment/nonblank lines, bytes, tokens, generated code, specializations,
  typed/emitted functions and instructions, and machine-code bytes. Do not pad
  source or count repeated specializations as additional original input.
- Measure real compiler/tools and at least one real application. Synthetic
  1x/2x/4x growth cases diagnose complexity, not headline throughput.
- Collect exclusive phase wall time and CPU time, child-process count, peak
  memory, allocations/clones/RC activity, and IR bytes where supported. Do not
  invent a breakdown from whole-command time or perturb hot loops with per-node
  logging. Record absent counters explicitly.
- Start with one warmup and five balanced exploratory repetitions. Near
  acceptance use twenty fresh full builds and report median, p95, and maximum,
  including failures rather than selecting favorable results.
- Before selecting a backend winner, freeze which statistic must meet 100k
  LOC/s, how failed runs affect acceptance, and numerical produced-program
  throughput, startup, memory, code-size and frame-time regression limits. Derive
  those limits from the real baseline and application needs; do not move them
  after seeing a preferred backend's result.
- Include produced-program startup, memory, code size, throughput, edit-to-run,
  and relevant frame-time gates. Compare development and release modes apart.
- Evidence hashing outside the timed interval identifies inputs; hashing actually
  performed by the compiler stays in the timing. Cache hashes, semantic stable
  identities, and name-lookup hashes serve different purposes. The existing raw
  `--compiler ... --output ...` route already bypasses `CacheSnapshot`.

The frozen native-driver closure reads 121 files in 31 packages: 3,830,672
bytes and 76,592 original physical lines, including both target import branches.
The throughput denominator is 76,569 original physical lines passed unchanged
to parsing: the 23-line target template is replaced by 16 generated lines,
reported separately. At 100k original parsed LOC/s the budget is 0.76569 seconds.
Neither count is active-target-only source. A current compiler is now admitted
from the pinned starting source, with raw-identical stage3/stage4 outputs and
573 compiler tests passed. The first independently checked full current-default
O2 baseline has a 96.516-second median over five fresh builds, about 793 original
parsed LOC/s. See [the O2 baseline](compiler-current-o2-baseline.md) for exact
samples and limitations. It is not development O0, a vNext result or a direct
Jai comparison. Historical timings used other source/tool states.

## Phases and exit gates

### 1. Plan, baseline, and semantic seam

Write this plan on the isolated branch; retain source-linked evidence and open
decisions. Prepare a bounded baseline runner with exact input identities, fresh
outputs, finite deadlines, preserved failures, and tests of its own guards.
Use the admitted current builder and measure the current full pipeline, then
identify the largest costs and the precise
frontend/backend seam. Freeze the acceptance statistic, failure policy and
numerical runtime/memory/frame budgets before choosing a winner. Exit with
reproducible receipts and a backend experiment decision, not a preferred-library
announcement.

### 2. Bounded vertical slice

Write caller-first programs for modules, synchronous functions, integer/bool
values, control flow, direct calls, and a real C ABI call. Reuse parsing and
signature facilities; one semantic path creates the canonical typed body.
Verify types, block edges, calls, diagnostics, and selected arithmetic. Feed the
same lowered bodies to eager JIT and AOT and compare observable results.
Compile/run a small real tool. A hand-authored IR fixture, reference evaluator,
schema test, or successful C helper build is useful scaffolding but is not this
exit gate and is not Chibi compile-speed evidence.

### 3. Compiler workload

Extend only for actual compiler/build consumers: approved records/unions,
collections, generics and compile-time execution. Port explicit semantic changes
and test intended new behavior. Compile representative real compiler modules
with source-correct diagnostics. Measure the complete pipeline early; do not
wait until every optional feature has been rebuilt. Exit with a real compiler
workload and measured growth/runtime behavior, not merely a larger toy corpus.

### 4. Runtime and platform integration

Prove selected ownership, collection/reference semantics, roots, reentry,
callbacks, layouts and generation lifetimes. Add tasks/reload only to the chosen
contract. Use differential tests for retained behavior and expectation tests for
deliberate changes. Execute actual platform tests; filenames or cross-linking do
not establish Windows/macOS runtime acceptance.

### 5. Performance, self-hosting, and distribution

Pass the agreed full-compilation and produced-program gates on real corpora.
Verify deterministic successive compiler stages with pinned backend/runtime
inputs and a fresh distribution that compiles ordinary programs without
developer C/Rust toolchains. Do not weaken correctness or reproducibility checks
to meet a throughput number.

### 6. Transition and deletion

Complete selected editor/browser/platform needs, migration instructions and a
rollback path. Delete superseded analysis/emission/VM paths and associated flags,
aliases, tests and documentation after consumers migrate. Keep experimental
comparison code off the production critical path.

## Evidence from the requested 2017 talk

Source: [Jonathan Blow, Reboot Develop 2017: Making Game Programming Less
Terrible](https://www.youtube.com/watch?v=De0Am_QcZiQ).

Evidence limit: dense automatic-caption samples span the full 1:02:48 timeline,
but this was **not uninterrupted audiovisual viewing or a complete transcript**.
Playback buffered, words/gaps can be missing, and timestamps below are approximate
seek offsets. Slide details and exact API spellings were not visually verified.

- [About 16:05–21:40](https://www.youtube.com/watch?v=De0Am_QcZiQ&t=965s):
  real game/editor/engine work and manual code adaptation test language choices.
- [About 22:05–24:55](https://www.youtube.com/watch?v=De0Am_QcZiQ&t=1325s):
  roughly 54–55k physical lines, including comments/blanks; reported roughly
  104k LOC/s in the warm full-rebuild loop. Approximately 0.52 seconds includes
  approximately 0.17 seconds in `link.exe`. The initial cold number is ambiguous
  and is not used. Do not infer a different rate from rounded figures.
- [About 25:25–32:15](https://www.youtube.com/watch?v=De0Am_QcZiQ&t=1525s):
  ordinary compile-time programs drive builds; one process schedules compilation
  work instead of repeated per-file compiler/process/intermediate-file work.
- [About 34:30–47:45](https://www.youtube.com/watch?v=De0Am_QcZiQ&t=2070s):
  shared type/program information removes repeated registration and supports
  inspectable generated glue and application-level policy.
- [About 50:15–55:05](https://www.youtube.com/watch?v=De0Am_QcZiQ&t=3015s):
  validate data-layout ideas on demanding programs; grow interop by actual need.
- [About 55:10–1:00:20](https://www.youtube.com/watch?v=De0Am_QcZiQ&t=3310s):
  predictable frame time motivates JIT skepticism; simple direct native output
  coexists with LLVM optimized output. Our explicit JIT scheduling reconciles
  that concern with the retained JIT objective.
- [About 1:00:32–1:02:30](https://www.youtube.com/watch?v=De0Am_QcZiQ&t=3632s):
  rewriting an unchanged slow design in a new language does not make it fast.

These observations motivate experiments; they do not prove a Chibi speedup,
backend choice, or language rewrite. A million-LOC/s goal cited elsewhere belongs
to another talk, not this recording.

## Primary implementation references

- [Current semantic/scalar boundary](https://github.com/waldnercharles/chibi/blob/02c86287430def964445ec90185961e20319536d/tools/compiler/package_compiler/Scalar.chibi#L4340-L4405)
- [Indexed parser state](https://github.com/waldnercharles/chibi/blob/02c86287430def964445ec90185961e20319536d/tools/compiler/source_parser/Parser.chibi#L39-L70)
- [Existing snapshot dependency](https://github.com/waldnercharles/chibi/blob/02c86287430def964445ec90185961e20319536d/tools/compiler/native_driver/Assertions.chibi#L34-L50)
- [Current host generation consumer](https://github.com/waldnercharles/chibi/blob/02c86287430def964445ec90185961e20319536d/apps/host/Host.chibi#L1-L9)
- [Cranelift goals](https://cranelift.dev/)
- [Cranelift settings](https://docs.rs/cranelift-codegen/latest/cranelift_codegen/settings/struct.Flags.html)
- [Cranelift JIT lifetime obligations](https://docs.rs/cranelift-jit/latest/cranelift_jit/struct.JITModule.html)
- [Cranelift object output](https://docs.rs/cranelift-object/latest/cranelift_object/struct.ObjectModule.html)
- [TPDE-LLVM limits](https://docs.tpde.org/tpde-llvm-main.html)

## Initial checkpoint status

- Plan and separate local branch: prepared.
- Current baseline: initially unmeasured. The later [O2 baseline](compiler-current-o2-baseline.md)
  now records five independently checked full builds with an admitted builder.
- Baseline runner and caller-first slice corpus: in progress.
- JIT/AOT vertical-slice acceptance: not run.
- Backend dependencies: not installed by this checkpoint.
- Native self-host branch: unchanged by this work.

Publication and CI results must name the verified remote commit and actual runs.
A feature-branch push alone does not trigger this repository's full CI workflow.
