# Chibi build tooling and self-hosting

The recording proof and private real-process host use existing Chibi syntax on
bytecode and SLJIT. Neither changes the game catalog or compiler grammar.
`PLAN.md` owns milestone status and current validation evidence.

## First increment

`test/language/BuildWorkflow.chibi` is an ordinary library whose `Build`
function constructs argv arrays, requests configuration, stops on failure and
requests the build only after success. Its `RunBuildStep` capability exists only
in the C test's private catalog. The host copies borrowed arguments into fixed
recording storage and supplies deterministic statuses. Nothing launches CMake.
CMake embeds the one source fixture into the existing language test driver.

Eight fixed C pico_unit cases exercise both bytecode and the platform compiled
backend: exact argv/order, configuration failure, build failure, negative host
status, repeated invocation, missing binding, missing service and invalid source.
Each invocation permits at most two recorded calls, six arguments per call,
127 bytes per argument and a 1,000-step execution budget. The complete workload
has at most 20 callback entries and 18 successful recordings. The authored
fixture and recording host perform no file, process, network, clock, sleep or
GPU operations and create no ECS entities or frames. The existing pico_unit
driver and production supervisor retain their normal timing and process control.

The admitted `language-build-workflow` tooling command passed all eight cases
and 90 assertions under the shared 60-second production deadline. Actual backend
entry counters verify bytecode and SLJIT execution, including failure paths; a
requested backend label alone is not evidence. The finite run took 0.056 seconds
on the measured Linux host; this is a test measurement, not a performance claim.
The exact standalone language target was built separately. Broader native,
materials, projects, language and tooling groups were not run for this proof;
unmeasured and quarantined checks are not passed.

## Private process host

Stage zero builds `chibi-build-host` on Linux and macOS. It is private, absent
from installed SDK exports and ordinary game catalogs. Windows retains the
existing stage-zero path; its equivalent process-tree implementation is pending.

```text
chibi-build-host --root DIR --source FILE --entry NAME --timeout-ms N
```

The host reads one regular `.chibi` source file, at most 1 MiB, and invokes one
named zero-argument function returning `int`. Relative source paths resolve from
the explicit root. The CLI uses bytecode; the private C invocation API also
supports SLJIT. Tests verify backend entry counters. Source preparation and
execution share the invocation budget. A supervised invocation may omit
`--timeout-ms`; an unsupervised invocation must supply a finite positive budget.

The private catalog contains `RunBuildStep(arguments: string[]) -> int` and
`BuildEnvironment(name: string) -> string`. A step runs direct argv in the root
directory. PATH lookup selects the executable separately from the authored
`argv[0]`, preserving every argument, including empty strings and shell syntax.
No shell interprets the arguments. The host copies the root, arguments and
allowlisted environment for the child lifetime and never changes parent cwd or
environment.

The environment allowlist is `PATH`, `HOME`, `TMPDIR`, `TMP`, `TEMP`, `SDKROOT`,
`DEVELOPER_DIR`, `CHIBI_TEST_DEADLINE` and the controlled fixture's
`CHIBI_BUILD_WORKER`. Missing allowed names read as empty strings; other names
are capability errors. Each invocation permits 64 steps and each step permits
256 arguments totaling 64 KiB. Captured stdout and stderr each retain at most
4,095 bytes across all steps; truncation remains visible in the result and CLI.

Child exits return `0..255`; signal termination returns the negative signal
number. Invalid input, spawn failure, timeout, cancellation and host failure
are capability errors with stable diagnostics. The CLI maps a returned negative
signal number to the conventional `128 + signal` exit status.

One absolute deadline bounds every child, including the inherited production
test deadline. The native clock matches Python's supervisor on each platform:
macOS uptime excludes sleep, while Linux uses `CLOCK_MONOTONIC`. The host kills
the complete child process group and reaps its direct child on termination,
including when a leader exits before its descendants. This is process-group
cleanup, not containment of children that deliberately create detached sessions.

`BuildProcessFixture.chibi` runs only the controlled C worker supplied through
its private environment. The original `BuildWorkflow.chibi` also runs unchanged
against a disposable worker named `cmake`, testing sequence and failure behavior
without configuring or building the engine. Real CMake orchestration belongs to
M2; compiler self-hosting belongs to later milestones.

## Bootstrap and backend milestones

1. Keep the current C compiler as a documented stage-0 recovery path.
2. Port a narrow, independently testable compiler component to ordinary Chibi,
   using fixtures shared with C. Select it from measured complexity and missing
   language capabilities, rather than rewriting the compiler all at once.
3. Extend toward a Chibi compiler that compiles its own sources. Verify stage-1
   and stage-2 semantic/bootstrap reproducibility, with documented toolchain and
   external library dependencies. Binary byte equality is required only where
   builds are made deterministic; semantic equivalence remains essential.
4. Evaluate LLVM AOT and LLVM runtime JIT separately from this port. LLVM can be
   used from the existing C compiler; self-hosting is not a prerequisite. Compare
   complete workloads against bytecode/SLJIT, including compile/startup/reload
   cost, steady-state/tail latency, memory, artifact size, diagnostics and ABI.
   Require matching semantics, not assumed speedups.

## Testing and library replacement

Make tests easy to keep near code, with useful assertions, deterministic ECS
fixtures, seeded replay and explicit allocation/work budgets. Keep one semantic
fixture set across bytecode, SLJIT and any future LLVM AOT/JIT. Review discovery,
assertion and fixture syntax alternatives before adding language syntax. Existing
C regression tests and the shared test supervisor remain gates during migration.
Replace libraries selectively when Chibi improves ownership, observability or
maintenance and passes equivalent correctness/performance tests. OS, graphics,
codec and security boundaries may continue to depend on external libraries.

## Reference limits

The official Odin FAQ describes its current C++ compiler and LLVM backend; it
explicitly postpones self-hosting. This separates implementation language from
backend choice: https://odin-lang.org/docs/faq/
LLVM's C API exposes ORC LLJIT, but this does not establish a Chibi performance
benefit: https://llvm.org/doxygen/group__LLVMCExecutionEngineLLJIT.html
The supplied Jai video https://www.youtube.com/watch?v=V5M9tVMfOUE could not be
retrieved through the available primary page/transcript routes. Its specific
self-build demonstration is unverified. A build script invoking an existing
compiler and a compiler whose implementation is in that language are distinct
claims; neither is inferred from the link alone.

Jacket is a useful third-party design reference, not an official Jai language
specification: https://github.com/matteopolak/jacket
At revision `16870e1d51cd19a8e71d372047e4595a1d262a74`, its compiler is written
in Rust while its formatter is written in Jai. Language-native tools and build
orchestration can therefore advance separately from compiler self-hosting.
This proof copies no Jacket code; its compiler/tool licensing needs separate
review before any reuse.
