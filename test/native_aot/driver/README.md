# Authored LLVM CLI driver (bounded WIP)

`tools/native_aot/Driver.chibi` is a real Chibi consumer of emitted LLVM IR.
It uses the existing build-host IO/process capabilities to run LLVM verification,
optional optimization, object emission and native linking. No Python orchestration,
C source generation or C wrapper is involved in that build path. This does not
replace the shared frontend or introduce a public IR, ABI or general build graph.

## Contract and invocation

The `Build` entry preserves its six positional arguments:

```
INPUT.ll OUTPUT_DIRECTORY OPT_EXECUTABLE LLC_EXECUTABLE LINKER_EXECUTABLE 0|2
```

The explicit `Object` entry takes exactly five, with no dummy linker:

```
INPUT.ll OUTPUT_DIRECTORY OPT_EXECUTABLE LLC_EXECUTABLE 0|2
```

Both entries share the same private implementation and admission/publication
rules. `Build` emits and links an executable; `Object` publishes the verified
object without linking, synthesizing an entry wrapper or changing scalar ABI.

All paths are absolute. The input is a regular LLVM text file. Tool paths are
explicit trusted capabilities, resolved and hashed; there is no PATH discovery
for production tools, shell command construction or caller-supplied flag list.
Path control characters are rejected. The output parent must already exist.
Both the final directory and its sibling `.partial` must be absent. Repeating a
build requires a fresh output directory; this is not a cache or replacement API.

Example with the SDK from engine `d0de4a84ecf50badcab661411470bdf6f3da5fa2`:

```sh
ROOT=$(pwd)
HOST="$SDK/libexec/chibi/chibi-build-host"
CORE="$SDK/share/chibi/core"
COMPILER="$SDK/share/chibi/compiler"
mkdir -p "$ROOT/out"
"$HOST" --root "$ROOT" --source tools/native_aot/Driver.chibi \
  --core "$CORE" --compiler "$COMPILER" --compiler-core "$CORE" \
  --entry Build --timeout-ms 120000 -- \
  "$ROOT/test/native_aot/driver/fixtures/scalar/Main.ll" "$ROOT/out/scalar-2" \
  "$OPT" "$LLC" "$CC" 2
```

`SDK`, `OPT`, `LLC` and `CC` must name already installed, verified tools. The
driver does not download or install anything. The linker is a Linux compiler
driver such as `cc`, used only to link the emitted object and ordinary native
startup dependencies, never to compile C source.

The driver snapshots input bytes into an exclusively reserved directory, then
executes fixed argument vectors:

1. `opt -S -passes=verify` for mode0, or
   `opt -S -passes=verify,default<O2>,verify` for mode2.
2. Admit the verified canonical output's exact
   `x86_64-unknown-linux-gnu` triple and observed LLVM19 x86-64 data layout.
3. `llc -filetype=obj -O=0|2` without a target override.
4. For `Build` only: `cc object.o -Wl,--gc-sections -o program`.

The accepted canonical data layout is
`e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128`.
It was measured from the actual LLVM19 backend, not guessed. The source emitter
provides a triple but no layout; LLVM canonicalization adds this layout. Missing
or foreign canonical target metadata and conflicting layouts reject before
object emission. Comments cannot supply metadata. LLVM resolves duplicate
triple declarations to the final one: a supported canonical result is admitted,
a foreign one rejects. This checks declared-target metadata, not source
provenance or arbitrary LLVM semantics, and does not parse LLVM independently.

Only successful object creation (plus an executable file for `Build`), unchanged
direct tool hashes, and a completed receipt permit no-replace publication.
`source.ll`, `module.ll`, `object.o` and `receipt.txt` are published together;
`Build` additionally publishes `program`. Every receipt explicitly records
`output-kind object` or `output-kind executable`, resolved tool paths/SHA256,
actual argv, mode and input/module/object hashes. An object receipt contains no
linker identity, link command or program hash, and `Object` does not resolve,
hash or execute a linker. An executable receipt additionally records those
linker/program facts. Receipts identify direct executables, not every
shared library, linker subprocess or startup file: this is not a hermetic build
or protection against malicious tools/concurrent mutation.

Ordinary stage failure removes only the known filenames inside this invocation's
reserved partial directory. A failed reservation is never cleaned. An existing
complete output or partial directory is left untouched. Host capability errors,
whole-operation timeout or cancellation can abort authored cleanup and leave an
unpublished partial. Failures before publication leave no final output, but an
interruption may occur after atomic publication and before the host returns.
After an uncertain interruption, inspect both the final receipt and the partial
before retrying; do not infer publication state solely from the host status.
The host already supervises process groups
and has bounded IO, output, instruction and time budgets; this is not a sandbox
for untrusted LLVM or executables. The driver uses native `build.lines`, not
quadratic remainder-slicing, for canonical metadata. Existing 16MiB text and
8192-line array bounds remain unchanged.

## Genuine emitter fixtures and provenance

The checked-in source fixtures use canonical SDK formatting. Both were rerun
through the unchanged reviewed Front/Back artifacts on bytecode and SLJIT; each
source pipeline forwards the exact Front stdout envelope unchanged to Back.
Formatting changed source hashes, but both emitted IR files were measured
byte-identical to their original reviewed outputs (not assumed or hand-edited).
The scalar fixture exports `main` returning 42 with CHECK budget 1. The small
parameter fixture exports `chibi_scalar(i32, i1) -> i32`, returning its integer
argument when its boolean argument is true, otherwise zero, with CHECK budget 64.
It is a genuine existing parameterized-emitter consumer, not a renamed `main`.

| Fixture | Source SHA256 | LLVM IR SHA256 |
|---|---|---|
| scalar/Main | `4e9a488c5a7f57e8dc27fdf19c538caa364cff1689538d9cb5823efe9e228b1b` | `648eac9e8c640789e865c9a5366ffbbe664740e926124a2567be61405d2afd5a` |
| parameters/Main | `4b40f1406c3335aa68f512e25e1dd74f1c75dc22219f2468513885f80c198bea` | `33f3be70f7d0d6d75c2e973a9f25f839af3d96411e60bafc2c305f7b71a463ed` |

Fixture regeneration measured Front/Back calls of 1,847/123 for scalar and
2,783/453 for parameters on each selected VM backend, with zero fallback.

The retained [scalar proof checkpoint3571908d](https://github.com/waldnercharles/chibi/commit/3571908d0cc4f3ad395b6a4fceb66641445c3d07)
binds the source→shared-frontend→checked-plan→emitter evidence. Its NUL-free Front
artifact is `a8b1793f23f4add35d427ad8177c1685c5fdb41790197bbf154918a06f4544dd`;
Back is `a859ab34adbee34772150f4e87a3482e4415b42d97706cb3a540733b5cf6aaa5`.
That measured frontend used current-fa sources and a reserved Native accessor's
effects-binding/rejection delta. This branch does **not** publish that delta,
import the old compiler bridge, or claim production current-main source compiler
integration. It consumes the exact emitted IR independently on a clean d0de base.

## Focused verification

The authored CLI integration fixture needs nine arguments: host, driver source,
compiler directory, core directory, emitted42 IR, fresh test directory, opt,
llc and linker. It invokes the actual driver through the actual SDK host:

```sh
"$HOST" --root "$ROOT" --source test/native_aot/driver/Tests.chibi \
  --core "$CORE" --compiler "$COMPILER" --compiler-core "$CORE" \
  --entry Tests --timeout-ms 120000 -- \
  "$HOST" "$ROOT/tools/native_aot/Driver.chibi" "$COMPILER" "$CORE" \
  "$ROOT/test/native_aot/driver/fixtures/scalar/Main.ll" "$ROOT/out/driver-tests" \
  "$OPT" "$LLC" "$CC"
```

The exact candidate passed real O0/O2 builds and native exit42, paths with spaces
in input/output and copied real tool executables, receipt hashes, verifier and
object/link-stage failures, canonical target/layout negatives, final-output
preservation and stale-partial preservation. Fixture-generated binaries and tool
copies are removed after success; receipts and IR remain bounded evidence.

The largest IR in the existing 16-source authored corpus (3857 bytes, SHA256
`55092e2670626dfc4f0c7f33ee967065f0823118763fbd7e12255ad649c5eec8`)
also passed O0/O2 to native exit42, with no undefined object symbols. The existing
all-table-max emitter case (73187 bytes, SHA256
`37fac099ac8d389d0e4341d8777ecc346a5ace19f67b5cfd800345f722f5f44e`)
previously passed verification and object emission but rejected executable
linking because it exports eight-argument `chibi_scalar`, not `main`. With the
new explicit `Object` entry it now publishes a real ELF x86-64 relocatable object
at O0 and O2 under the same unchanged budgets. Both objects have `chibi_scalar`
and no undefined symbols. The large module stays scratch evidence rather than
a duplicated checked-in fixture; no executable wrapper or new ABI was invented.

The `Objects` authored test entry takes eight arguments (the same settings as
`Tests`, but parameter IR and no linker). For example:

```sh
"$HOST" --root "$ROOT" --source test/native_aot/driver/Tests.chibi \
  --core "$CORE" --compiler "$COMPILER" --compiler-core "$CORE" \
  --entry Objects --timeout-ms 120000 -- \
  "$HOST" "$ROOT/tools/native_aot/Driver.chibi" "$COMPILER" "$CORE" \
  "$ROOT/test/native_aot/driver/fixtures/parameters/Main.ll" "$ROOT/out/object-tests" \
  "$OPT" "$LLC"
```

Both authored suites pass on the exact formatted candidate. Object tests cover
real O0/O2 emission, paths with spaces, explicit kind and input/tool/object hashes,
absence of linker/program claims, verifier and emission failures (including a
zero-exit tool that produces no object), invalid mode, and preservation of prior
complete output and stale partials. Separate validation-only `readelf`/`nm`
checks identify the small and maximum outputs as x86-64 relocatable objects with
no undefined symbols. A test harness linked the small objects as shared libraries
(no C source or wrappers) and checked three existing boundary calls per mode:
INT_MIN/true, INT_MAX/true and INT_MIN/false. This harness is validation only;
the authored `Object` production path ends at object publication.

Tested tool identities:
- Official d0de SDK build host:
  `1d8e9c9a1556e82f0120e9d352f20175c4be88f67f2b795355a685b85c6f5cc3`.
- Official Debian LLVM19 package `1:19.1.7-3+b1`, SHA256
  `0b1b7263f4f45e261099240fdbf00fc14b42b88410fd829e041bf83ea45866cb`;
  extracted in scratch, no system installation. `opt`:
  `6d32545b2590f82c9b2d5021b8c13236225b1241728ffb123f77956940f9f87a`;
  `llc`: `757877e6d5dd8bd9607a1515284e8ff39c8acb784b5f0ed789187531129cd790`.
- Current-fa compiler seed:
  `41905e8bc4afc3998cf48097a98d6c1ca0ce89ff2de6a6dadcffb6fb06f1c71e`.

The SDK CLI selects bytecode for the authored driver. No unmeasured SLJIT-driver
claim is made. The current first-party C build host is transient bootstrapping
debt, not a permanent exemption; first-party C is absent from the emitted scalar
object/executable. The official SDK formatter (`chibi-compile`, SHA256
`9b9e7d1108e68cec34b75cd490f49131eda2fadd86fc7c05517e24564fc67817`)
formatted all candidate Chibi files, then `--format --check` passed. The earlier
352aa34c WIP had unrun formatting, subsequently found noncanonical; this candidate
corrects it and regenerates fixture provenance. Standard repository test
dispatcher and platform CI remain unrun. The first driver checkpoint had an
independent source/process review, reproduced suite, 15 extra controls and a real
publication-race check. Independent exact review of this object addition passed:
formatter and both authored suites were independently reproduced; 15 prior
`Build` controls and 14 additional `Object` controls passed. Both real object
fixtures passed O0/O2 with empty PATH, matching hashes, exactly four published
files, and ELF/symbol checks. The larger source-corpus executable still returned
42 in both modes, and actual publication races for both entries preserved the
competing final directory and cleaned the owned partial. Fixture regeneration
provenance/counters were independently bound to the frozen source/artifact
identities, not independently rerun. This is not broader platform or integration
acceptance. Foreign allocation, typed memory, output/free, production
frontend integration and wider toolchain/platform support remain open.

## C regression cases

`python3 tools/test_policy.py tool native-aot-driver` selects the C/pico cases in
`test/tools/native_aot_driver.c` through the existing `test_tools` dispatcher.
It requires the private `chibi_build_host` target. A minimal compiler-enabled SDK
and tooling build is sufficient; no renderer or GPU is needed. As with other
tooling selectors, `CHIBI_TOOLS_BUILD_DIR` can name that tooling build directory.
The dispatcher takes `--native-aot-driver ABSOLUTE_DRIVER_SOURCE`; the policy
supplies the production driver. An explicit source path permits a temporary
negative control without changing the checked-in driver.

`Contracts.chibi` contains only adapters for private `Tool`/`Cleanup` helpers and
the host's process capability. The C runner appends those adapters to unchanged
driver text in memory. It does not change production source, compiler,
capabilities or platform admission.

Three portable cases passed on macOS arm64 with 30 assertions:

- Resolved executable aliases with spaces; missing, non-executable, directory,
  dangling, relative and control-character tool paths.
- Cleanup's known-file list, unknown-file preservation and sibling output
  preservation.
- Cancellation after a controlled tool writes object bytes. This host-capability
  control requires the exact `build process cancelled` diagnostic. It does not
  execute the Linux-only `Run` path on macOS.

Two temporary negative controls fail as intended. Changing only `Tool`'s final
`return resolved` to `return path` in a scratch driver copy fails the alias
assertion. Disabling the C runner's cancellation pointer in a scratch C copy
makes the process reach its 10-second deadline, yielding `build process timeout`.
The cancellation case then fails its exact diagnostic assertion. The original
source passes. Neither mutation is published.

Six additional Linux-only cases cover:

- Existing final files and stale partial object/receipt bytes for both entries,
  with no optimizer execution before rejection.
- Physical relocation of tools and IR, paths with spaces, a resolved tool alias,
  exact object argv at O0/O2, and `Object` succeeding with empty PATH and no
  linker file or linker/program receipt fields.
- Missing opt, llc and, for `Build`, linker capabilities before reservation.
- An object written before exit71 or SIGTERM, and a zero-exit tool producing no
  object. Both entries must leave no final or partial output and never link.
- Mutation of opt or llc bytes after object creation, preventing publication.
- Cancellation after an object is written, requiring the exact cancellation
  diagnostic, leaving an unpublished partial that a retry refuses without
  altering its bytes. A deadline failure cannot satisfy the cancellation check.

`fixtures/stage-tool.sh` supplies controlled executable capabilities for these
failure paths, following the tooling suite's existing shell stand-in pattern.
It rejects malformed argument shapes, records each argument on its own line,
and uses absolute utility paths. Its object bytes are a marker, not an ELF
object. These cases test orchestration and publication, not LLVM behavior;
the real LLVM fixtures and authored suites above remain separate coverage.
The cancellation fixture signals its direct C test parent only after writing
the object, then waits for host process-group cleanup. Every invocation has a
10-second deadline. All fixtures live in one disposable directory removed by
the C runner. Only the bytecode backend is selected.

The coordinator approved minimal registration in `test/tools/main.c`,
`test/tools/CMakeLists.txt` and `tools/test_policy.py`. Existing selectors,
group membership, budgets and cleanup policy are unchanged. Registration was
validated through the normal policy route against a Release compiler-enabled
SDK/tooling build with assertions, no sanitizers, dead stripping and at most two
jobs. The prebuilt third-party store matched the SHA256 pinned by this
checkout's unchanged `third_party/archives.tsv`; first-party code was built from
this checkout.

The focused C build, portable cases, canonical source formatting and shell syntax
passed. Linux C branches passed a syntax-only compile on macOS with `__linux__`
defined; this does not count as a Linux build or execution. Linux driver
publication/interruption execution, real LLVM reruns and broader
native/material/project/language/tool suites remain unexecuted. Separate Linux
verification and independent review remain required before landing this draft.
