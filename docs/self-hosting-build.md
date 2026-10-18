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

Stage zero builds `chibi-build-host` on Linux and macOS. The SDK installs the
private executable in `libexec/chibi`; its capabilities stay out of ordinary
game catalogs and public SDK library exports. Windows retains the
existing stage-zero path; its equivalent process-tree implementation is pending.

```text
chibi-build-host --root DIR --source FILE --entry NAME --timeout-ms N [-- ARGUMENTS...]
```

The host reads one regular `.chibi` source file, at most 1 MiB, and invokes one
named zero-argument function returning `int`. Relative source paths resolve from
the explicit root. The CLI uses bytecode; the private C invocation API also
supports SLJIT. Tests verify backend entry counters. Source preparation and
execution share the invocation budget. A supervised invocation may omit
`--timeout-ms`; an unsupervised invocation must supply a finite positive budget.

The private catalog includes `RunBuildStep(arguments: string[]) -> int` and
`BuildEnvironment(name: string) -> string`. A step runs direct argv in the root
directory. PATH lookup selects the executable separately from the authored
`argv[0]`, preserving every argument, including empty strings and shell syntax.
No shell interprets the arguments. The host copies the root, arguments and
allowlisted environment for the child lifetime and never changes parent cwd or
environment.

The environment snapshot includes compiler, flags, CMake, SDK, dependency-cache,
ccache, locale and sanitizer controls, together with PATH, HOME, temporary paths
and the inherited test deadline. `environment_names` in `build_host.c` owns the
explicit allowlist. Missing allowed names read as empty strings; other names
are capability errors. The snapshot is limited to 1 MiB. Each invocation permits
64 steps and each step permits
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
without configuring or building the engine. Compiler self-hosting belongs to
later milestones.

## Installed stage-zero build program

`tools/toolchain/Build.chibi` owns native profile selection, compiler and SDK
precedence, CMake arguments, configuration-cache decisions, process sequencing
and failure reporting. The SDK installs it in `share/chibi/tools/toolchain`.
Run it through the private host with an explicit engine source checkout as root:

```sh
SDK/libexec/chibi/chibi-build-host --root SOURCE \
  --source SDK/share/chibi/tools/toolchain/Build.chibi \
  --entry Build --timeout-ms 1800000 -- \
  tool-host BUILD --target chibi-build-host --jobs 8 -- \
  -DCMAKE_BUILD_TYPE=Debug -DCHIBI_ENABLE_SANITIZERS=OFF
```

Use absolute SDK paths when source and installation are in different directories.
`native` configures the root CMake project. `tool-host` configures `sdk/` with
the compiler enabled and native/desktop packages disabled; explicit CMake
definitions come last. Repeated `--target` selects build targets, `--jobs` selects
parallelism, and `--configure-only` stops before compilation. Arguments after
`--` pass directly to CMake. The public `chibi build` still builds authored games.

`make build` uses this program when its private host reports protocol version 2
through `--protocol-version`. A cold build or an older host
uses `make stage-zero-build`, which retains the existing shell/CMake recovery
path and builds the host for subsequent invocations. `BUILD_HOST` and
`BUILD_PROGRAM` can select a relocated installation. The explicit recovery
target remains available if the authored driver cannot run.

The program gives explicit compiler/toolchain/preload options precedence over
fresh-build defaults, retains cached selections, and discovers the matching
macOS SDK. The configuration stamp tracks caller arguments, environment,
loaded CMake files, new configuration files, dependency inputs and compiler/tool
identities. It is written atomically only after successful configuration. Build
targets and parallelism do not invalidate configuration.

Three installed Python helpers remain transitional dependencies:
`configure_inputs.py` retains the existing read-only signature, compiler SDK and
dependency-override probes; `dependency_package.py` and `fetchcontent_cache.py`
retain dependency acquisition and cache ownership. Their replacement and removal
gates remain in the toolchain inventory. CMake still owns the stage-zero graph.

The private primitives expose exact arguments, platform and root paths,
executable lookup, bounded regular-text reads, atomic writes, directory creation,
last-process output and string/array operations. File and text values are limited
to 1 MiB, arrays to 8,192 entries and 1 MiB of text, and retained helper storage
to 32 MiB per invocation. Last-output accessors reject truncation and embedded
NUL bytes, so authored protocol parsing cannot accept a partial result. Returned
values remain owned by the invocation until it finishes; parent cwd and
environment are never mutated.

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

## Private compiler binary handoff

The raw-syntax/binary transport increment passes its selectors on macOS (see
`PLAN.md`). The contracts below are not a self-hosting completion claim.

The existing raw parser bridge now retains aligned Tree-sitter named/extra flags
and a syntax root separate from the document's routing root. Its private native
result ABI changes from thirteen to fifteen results; prepared closures must be
regenerated. The actual eight-package snapshot closure is unchanged in scope,
and incompatible native signatures must reject before invocation.

The existing Linux/Darwin `chibi-compiler-host` has an optional private
`--output-artifact FILE` mode. Its other arguments remain `--artifact FILE`,
`--entry HEXID`, `--input FILE`, `--backend bytecode|sljit`, `--budget N` and a
finite `--timeout-ms N` or inherited `CHIBI_TEST_DEADLINE`. Binary output and
`--batch` are mutually exclusive. Without the new switch, JSON stdout and JSON
batch behavior are unchanged. Public `chibi`, `chibi-compile` and `chibi-build`
selection, cache and installed-format contracts do not change.

`compiler_host_invoke_bytes` accepts the existing owned JSON argument array and
requires exactly one primitive `byte[]` return declaration. Address/reference
argument or result provenance rejects before JSON decoding. The caller chooses
an output limit from 1 through 1,000,000 octets; the CLI uses the upper bound.
After successful return, the host validates the runtime array shape and item
types and copies each full `pin_value` item's `.u8` into malloc-owned bytes
before releasing runtime storage. Empty success is a null pointer and zero
length; failure is also null/zero with a false result and diagnostic. Returned
bytes survive input, prepared-artifact and host teardown. The API is format-
agnostic and preserves all 256 octets without text encoding or JSON projection.
The 64 MiB represented-output ceiling includes header, declared item capacity
and packed bytes. These are transport/storage checks, not a whole-compilation
allocator sandbox; supervised hard deadlines still cover decode/JIT/execution.

The CLI validates the exact returned bytes through the existing runtime-only
`compiler_host_load` on bytecode before publication. Its output must therefore
be a target-compatible closed `CHIBICLO` compiler artifact admitted by the
text/array/JSON/raw-syntax catalog. It is not a generic game-artifact output
route. The validator neither authors nor serializes an executable and does not
link source authoring, semantic analysis or lowering into the product host.

Module-set validation measures dependency length and fingerprint through
`execution_prepared_measure`, so nothing on the runtime read path references the
public `execution_prepared_write` entry point. The Linux GNU/Clang production
execution target's private function/data sections and Darwin's dead-strip link
therefore drop it from the host. Shared canonical encoding used by reader
dependency fingerprints remains reachable. The writer remains available in the
static archive and must remain present in real producer/tooling binaries that
call it. No validation-only flags may supply this boundary; actual unstripped
binary/link-input evidence is required on each platform.

Publication uses a private 0600 `mkstemp` file beside the destination, `FD_CLOEXEC`,
bounded writes with partial-write/EINTR handling, checked close and a final
readiness check before POSIX rename. Rename is the sole commit point. Handled
precommit failure or cancellation leaves the existing destination unchanged
and removes the operation's staging file. The final symlink entry is replaced,
not followed for truncation; parent-directory symlinks keep normal POSIX path
resolution. Missing parent directories are not created. No crash-durability or
fsync guarantee is made, and an uncatchable kill can leave staging behind.

Binary mode ignores SIGPIPE before invoking the compiler, emits and explicitly
flushes checked execution counters before starting publication, and leaves
stdout empty. After successful rename there is no further diagnostic I/O or
deadline/cancellation/status reclassification. SIGPIPE stays ignored through
normal process exit. Atomic visibility is not an instantaneous cancellation
fence around the rename syscall. This mechanism is scoped to Linux and Darwin;
Windows publication needs separate implementation and validation.

The finite compiler-artifact fixture described in `docs/testing.md` is a
transport/publication check. Stage zero still prepares and serializes its tiny
echo artifact. It cannot establish authored serialization or compiler
self-compilation. Remaining work includes the complete authored package driver,
semantic/lowering ownership, canonical prepared/container encoding, target and
native ABI facts, recovery metadata, source-free installed build/link wiring,
and real stage-one/stage-two compilation from the full compiler source manifest.
No self-hosting or serializer completion is claimed by this handoff.

The final normal installed workflow remains subject to `PLAN.md`: it must
compile Chibi and drive build/link with `cmake`, `clang` and `gcc` executables
unavailable, including hidden or renamed equivalents. Explicit platform
assemblers/linkers, SDKs, native backends and runtime libraries may remain behind
narrow bindings. Current native tools are stage-zero construction/recovery
inputs during migration; this transport increment does not satisfy that final
clean-checkout stage-one/stage-two and checked-game proof.
