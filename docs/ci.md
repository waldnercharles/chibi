# CI

`.github/workflows/ci.yml` is the only workflow. PRs, pushes to `main`, and
manual `mode=ci` run the native Linux and macOS suites, the Windows compiler
fixed point and runtime probes, and both consumers of the Linux cross-target
bundle. Jobs test the actual checkout, including a PR's synthetic merge.
Source identity is checked before and after the native proof. A manual `ci` run
with all three Windows bootstrap pins selects only the Windows proof; partial
pins preserve the canonical jobs, and `full` retains its broader coverage.

## Native products and runtime checks

Linux and macOS build the native CLI/compiler, asset compiler and compile-only
facade archive once into `build-release`. The registered Graph, native program,
project and language selectors run through the native compiler. The native
proof then checks compiler reproduction and the owning runtime suites, including
processes, caches, diagnostics, failures, tasks, collections, math, formatting,
retained generations, CLI execution and reload.

The fixed point requires stages two and three to match byte for byte. An older
pinned seed may require one additional stage, which must prove stages three and
four equal. There is no retry loop or relaxed comparison. Windows cross-builds
are executed on Windows, where the compiler reproduces itself with that runner's
LLVM before runtime probes use the fresh compiler.

After reproduction, the fixed compiler runs the mandatory Driver/Graph command
contract. Independent fixtures check successful and rejected Driver statuses,
complete stdout and stderr, and every ordered compile/link entry for five
selected native and test-owned C targets in two private build roots. Only build
root spelling is normalized. A changed-tool control must change both tables.
Any failed contract stops the proof with its original status.

Windows runs the complete `tools/compiler` test root after its fixed point and
before product checks. It preserves the native status and console log. The
transitional 120-minute Windows job bound includes compiler tests and the
runtime/product probes; it does not change individual process contracts.

Native profile evidence binds the clean source commit, recorded native product
commands, source/tool fingerprints, output hashes, and facade archive receipt.
It does not claim C sanitizer instrumentation on LLVM-generated code. Generic C
flag and sanitizer handling are tested through the test-owned foreign-code
Graph fixtures. C ABI consumers link the authored facade archive and exercise
its public header without a C compiler host or VM.

The initial Release build uses the verified seed. Later core evidence and
selector preparation pass that Release compiler explicitly through
`--native-driver`. The independent native fixed-point proof retains its
verified-seed starting point.

Normal CI job timeouts bound each job. Individual process regressions retain
checks for deadlines, cancellation, exit status and descendant cleanup. Test
selection follows the affected behavior and the registered suites, without a
global test-duration admission policy.

## Cross-target bundle

Linux emits the same arithmetic modules, function-call manifests and executable
cases for the macOS and Windows consumers. Each consumer validates the manifest,
links the modules with its native linker and runs every required call and
program. The C reference runner remains beside the Chibi runner until both
platforms' differential acceptance is recorded. Missing manifests remain errors.

## Additional modes

| Trigger or mode | Work |
| --- | --- |
| PR, main push, manual `ci` | Native platform builds, runtime suites, fixed points and cross-target consumers |
| Daily schedule or manual `full` | Canonical suites plus clean default checkout build, SDK relocation/ABI checks and no-C compiler proof |
| Manual `graph` | Native SDK products, generic C graph fixtures, discovery, incremental commands, SDK relocation and native selectors |
| Maintenance scripts | Verified native seed preparation and optional exact-source bundle |

The no-C job removes C compilers and uses the verified native seed, LLVM and
native vendor program route. Prebuilt third-party archives remain supported.
The installed `chibi-compile` alias is the native compiler executable. It is
independent of the retired C program with that name.

C reflection, generated C registries, VM comparison/recovery hosts, prepared
compiler-runtime seed jobs and their recovered-compiler caches are removed.
The compile-only prepared encoder and native/Wasm public asset ABI remain.
Windows keeps its checksum-verified LLVM tool cache. Seed publication is separate
from routine CI and is described in [maintenance](ci-maintenance.md).
