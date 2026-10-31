# Authored LLVM CLI driver (bounded WIP)

`tools/native_aot/Driver.chibi` is a real Chibi consumer of emitted LLVM IR.
Its entries call the `native_aot` collection's package in
`tools/native_aot/driver`, so the build host runs it with
`--collection native_aot=DIR/driver`, where DIR holds `Driver.chibi`.
It uses the existing build-host IO/process capabilities to run LLVM verification,
optional optimization, object emission and native linking. No Python orchestration,
C source generation or C wrapper is involved in that build path. This does not
replace the shared frontend or introduce a public IR, ABI or general build graph.

## Contract and invocation

The `Build` entry preserves its six positional arguments:

```
INPUT.ll OUTPUT_DIRECTORY OPT_EXECUTABLE LLC_EXECUTABLE LD_LLD_EXECUTABLE 0|2
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
  --collection native_aot="$ROOT/tools/native_aot/driver" \
  --core "$CORE" --compiler "$COMPILER" --compiler-core "$CORE" \
  --entry Build --timeout-ms 120000 -- \
  "$ROOT/test/native_aot/driver/fixtures/scalar/Main.ll" "$ROOT/out/scalar-2" \
  "$OPT" "$LLC" "$LLD" 2
```

`SDK`, `OPT`, `LLC` and `LLD` must name already installed, verified tools. The
driver does not download or install anything. The linker is LLVM's `ld.lld`,
which links the emitted object with a start object the driver generates; no C
compiler, C library or startup file takes part (see
[linking without a C compiler](#linking-without-a-c-compiler)).

The driver snapshots input bytes into an exclusively reserved directory, then
executes fixed argument vectors:

1. `opt -S -passes=verify` for mode0, or
   `opt -S -passes=verify,default<O2>,verify` for mode2.
2. Admit the verified canonical output's exact triple and data layout:
   `x86_64-unknown-linux-gnu`, `arm64-apple-macosx11.0.0` or
   `x86_64-pc-windows-msvc`, each with the layout LLVM 18 gives it. `Build`
   links all three.
3. `llc -filetype=obj -O=0|2 -relocation-model=pic` without a target
   override. A static object's absolute references, such as the table O2
   lowers a dense switch through, cannot link into the shared libraries the
   native tests load.
4. For `Build` only: the same `llc` arguments on the generated `start.ll`,
   giving `start.o`, then the target's link:
   - Linux x86-64:
     `ld.lld -flavor gnu -static --gc-sections -e _start object.o start.o -o program`.
   - macOS arm64: `ld.lld -flavor darwin -arch arm64 -platform_version macos
     11.0.0 11.0.0 -dead_strip -e _main object.o start.o libSystem.tbd -o
     program`.
   - Windows x64: `ld.lld -flavor link /lib /nologo /def:kernel32.def
     /machine:x64 /out:kernel32.lib`, then `ld.lld -flavor link /nologo
     /subsystem:console /entry:mainCRTStartup /nodefaultlib /opt:ref
     /out:program.exe object.o start.o kernel32.lib`.

   The driver runs the linker it resolved, which may be lld's generic driver,
   so it names the flavor rather than relying on the `ld.lld` name.

The accepted Linux data layout is
`e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128`.
It was measured from the actual LLVM 18 and 19 backends, not guessed; LLVM 18
to 23 give the same x86-64 layouts. The arm64 layout changes with the LLVM
major: LLVM 19 appends `-Fn32` and LLVM 22 and 23 add the x86 address spaces.
The driver admits those three, measured, as the same ABI, and rejects others. The source emitter
provides a triple but no layout; LLVM canonicalization adds this layout. Missing
or foreign canonical target metadata and conflicting layouts reject before
object emission. Comments cannot supply metadata. LLVM resolves duplicate
triple declarations to the final one: a supported canonical result is admitted,
a foreign one rejects. This checks declared-target metadata, not source
provenance or arbitrary LLVM semantics, and does not parse LLVM independently.

Only successful object creation (plus an executable file for `Build`), unchanged
direct tool hashes, and a completed receipt permit no-replace publication.
`source.ll`, `module.ll`, `object.o` and `receipt.txt` are published together;
`Build` additionally publishes `start.ll`, `start.o` and the program,
`program.exe` on Windows. Every receipt explicitly records
`output-kind object` or `output-kind executable`, resolved tool paths/SHA256,
actual argv, mode and input/module/object hashes. An object receipt contains no
linker identity, link command or program hash, and `Object` does not resolve,
hash or execute a linker. An executable receipt additionally records those
linker/program facts, the start object's hash and the command that built it. Receipts identify direct executables, not every
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

## Linking without a C compiler

`Build` links programs with `ld.lld` alone, on Linux or macOS, for every
target. A Linux program's only
runtime is `start.ll`, which the driver writes and compiles with the same `llc`
arguments as the module. Like Odin's no-CRT `_start` and musl's `crt1`, it
clears the frame pointer, aligns the stack down to 16 bytes, calls the module's
entry `chibi_scalar() -> i32` as `scalar_llvm` emits it, and passes the result
to the `exit_group` system call. The link line follows rustc's direct `ld.lld`
flavor (`-flavor gnu`, `--gc-sections`, `-static`). Emitted scalar
objects have no undefined symbols, so the static link needs no C library,
startup file or compiler driver, and an empty `PATH` changes nothing. Programs
that overflow their stack end with the system's SIGSEGV, as before (D4).

The other targets need no SDK of their own system:

- macOS arm64: `start.ll` defines `main`, which returns `chibi_scalar()`;
  dyld calls it through `LC_MAIN` and exits with its result. A program that
  allocates gets the same context a dynamically linked Linux program does,
  with libSystem's `calloc`. The program links against `libSystem.tbd`, a text
  stub that names `/usr/lib/libSystem.B.dylib`, as Zig's stubs stand in for the
  SDK. It exports the functions the module declares, which its foreign calls
  name, with `abort`, `calloc`, the copies `llc` may call and
  `dyld_stub_binder`, and dyld binds them in libSystem, which re-exports the C
  library. The SDK's own stub is not used: newer SDKs name architectures that
  older lld rejects. Every macOS process loads `libSystem`, and lld signs
  arm64 output ad hoc, which the kernel requires.
- Windows x64: `start.ll` defines `mainCRTStartup`, which passes
  `chibi_scalar()` to `ExitProcess`, imported through a `kernel32.lib` that
  lld writes from a one-line `.def` file, as rustc's `raw-dylib` does. It also
  defines `_fltused`, which code using floating point references, and
  `__chkstk`, the stack probe Windows code calls for a frame larger than a
  page: compiler-rt's, which touches each page from the top of the frame down.

The checks:

- `executable_links_with_the_named_linker_and_start_object` checks the exact
  start-object and link argument vectors and the receipt's linker, start-object
  hash and commands, with controlled tools.
- `executable_links_llvm_objects_without_a_c_compiler` links the scalar fixture
  at O0 and O2 with the `opt`, `llc` and `ld.lld` in `CHIBI_LLVM_BIN`, an empty
  `PATH`, and runs each program to its exit status 42. Without `CHIBI_LLVM_BIN`
  it says it did not run. Each case compiles the driver in the build host, so
  run this selector against a Release `test_tools`
  (`CHIBI_TOOLS_BUILD_DIR=build-release/test/tools`); under the sanitizers the
  compile outlasts the cases' 10-second deadlines. CI links real programs
  through `chibi build --native` instead: the `native-build` selector in the
  projects group (see `docs/guide/toolchain.md`).
- In `native-build`, `native_build_links_macos_and_windows_programs` checks
  the macOS and Windows link lines, stubs and start objects with controlled
  tools, and `native_build_links_other_targets_with_llvm` links real programs
  for both with `CHIBI_LLVM_BIN`. With `CHIBI_NATIVE_BUNDLE` set, those
  programs go into the bundle that `native-targets` runs on macOS and Windows
  (`test/native_aot/targets/README.md`).

`fixtures/scalar/Main.ll` is the source path's own output for
`fixtures/scalar/Main.chibi`: the bootstrap compiler's `CollectScalar` rows,
lowered by `scalar_llvm.EmitBytes`. It exports `chibi_scalar`, which the start
object calls. The earlier hand-run emitter output exported `main`, which no
current producer emits.

## Genuine emitter fixtures and provenance

The checked-in source fixtures use canonical SDK formatting. Both were rerun
through the unchanged reviewed Front/Back artifacts on bytecode and SLJIT; each
source pipeline forwards the exact Front stdout envelope unchanged to Back.
Formatting changed source hashes, but both emitted IR files were measured
byte-identical to their original reviewed outputs (not assumed or hand-edited).
The scalar fixture originally exported `main` returning 42 with CHECK budget 1;
it was regenerated through the source path (see above), so its hash below is
historical. The small
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
  "$OPT" "$LLC" "$LLD"
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
corrects it and regenerates fixture provenance. At that object checkpoint, the
standard repository dispatcher and platform CI were not run. Later C regression
evidence is recorded below. The first driver checkpoint had an
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

## Regression cases

`chibi build test native-aot-driver` runs `DriverTests.chibi`, which imports
the driver's package and checks it directly: tool admission of aliases and
unusable paths, cleanup that removes only the names a run owns, target
admission of each triple with the layouts LLVM gives it, library names read from
the declaration alone, and an optimizer-argv oracle that a permissive
stand-in cannot satisfy by succeeding. It then runs the publication cases in a
build host of their own with an empty `PATH`: existing outputs and stale
partials are kept, relocated tools with spaces build an object with no linker,
`Build` links with the named linker and a start object, a macOS program links
against a libSystem stub that exports what it imports, missing tools
reject before reserving output, a failed, signalled or missing object never
publishes, and a tool whose bytes change during the build prevents
publication. With `CHIBI_LLVM_BIN` set it also links the scalar fixture for
the host with real LLVM tools at O0 and O2 and runs each program to exit
status 42. CI runs it on Linux and on macOS.

`fixtures/stage-tool.sh` supplies the controlled `opt`, `llc` and linker: it
rejects malformed argument shapes, records each argument on its own line and
uses absolute utility paths. Its object bytes are a marker, not an ELF object,
so these cases test orchestration and publication, not LLVM behavior. The
host's own cancellation is `build-process`'s to check.
