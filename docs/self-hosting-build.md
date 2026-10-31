# Chibi build tooling and self-hosting

The recording proof and private real-process host use existing Chibi syntax on
bytecode and SLJIT. Neither changes the game catalog or compiler grammar.

## First increment

`test/language/BuildWorkflow.chibi` is an ordinary package whose `Build`
function constructs argv arrays, requests configuration, stops on failure and
requests the build only after success. Its `build.run` capability exists only
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

The `language-build-workflow` tooling command passed all eight cases
and 90 assertions. Actual backend
entry counters verify bytecode and SLJIT execution, including failure paths; a
requested backend label alone is not evidence. The finite run took 0.056 seconds
on the measured Linux host; this is a test measurement, not a performance claim.
The exact standalone language target was built separately. Broader native,
materials, projects, language and tooling groups were not run for this proof;
unexecuted checks are not passed.

## Private process host

The native build produces `chibi-build-host` on Linux and macOS. The SDK installs
the private executable in `libexec/chibi`; its capabilities stay out of ordinary
game catalogs and public SDK library exports. Windows retains the
existing stage-zero path; its equivalent process-tree implementation is pending.

```text
chibi-build-host --root DIR --source FILE [--core DIR] [--collection NAME=DIR]... [--compiler DIR] [--compiler-core DIR] --entry NAME --timeout-ms N [-- ARGUMENTS...]
```

The host reads one regular `.chibi` source file, at most 1 MiB, and invokes one
named zero-argument function returning `int`. The file is the one file of the
package at its directory, so it starts with a `package` header. With `--core`,
it may import that collection's packages, such as `core:strings`; a relative
`--core` resolves from the root. Each `--collection NAME=DIR`, at most eight,
adds another collection the program imports as `NAME:`, as Odin's
`-collection:name=path` does. Besides the `build.` natives, a program has
the text and array natives every host offers, such as `format_text` and
`length`. Nothing resumes a build program, so waiting or starting a task fails
it with a diagnostic. Relative source paths resolve from
the explicit root. The CLI uses bytecode; the private C invocation API also
supports SLJIT. Tests verify backend entry counters. The operation deadline
starts before source I/O and is passed to the build invocation and its children.
Every CLI invocation supplies a positive `--timeout-ms` argument.
The CLI's preparation guard also enforces that deadline and handles SIGINT and
SIGTERM during compiler recovery, loading and program compilation. It exits
directly while no build child can exist. Before authored execution, the host
disarms that guard; execution retains the process supervisor's cancellation,
deadline and child-group cleanup.

Every private native is named under `build.`, as Zig's build API sits under
`std.Build`: `build.run`, `build.read_file`, `build.join` and so on. The
qualifier is part of each native's catalog name, the way an imported package's
functions are named, so a build program needs no import, and an unqualified or
`Build`-prefixed name is an unknown callable.

The private catalog includes `build.run(arguments: string[]) -> int`,
`build.probe(arguments: string[]) -> int` and
`build.environment(name: string) -> string`. A step runs direct argv in the root
directory. PATH lookup selects the executable separately from the authored
`argv[0]`, preserving every argument, including empty strings and shell syntax.
No shell interprets the arguments. The host copies the root, arguments and
allowlisted environment for the child lifetime and never changes parent cwd or
environment.

The environment snapshot includes compiler, flags, CMake, SDK, cache-directory,
ccache, locale and sanitizer controls, together with PATH, HOME, temporary paths,
the GitHub CLI's `GH_TOKEN` for archive downloads.
`environment_names` in `build_host.c` owns the explicit allowlist.
Missing allowed names read as empty strings; other names are capability errors.
`build.environment_names()` lists the allowed names the
snapshot holds, so set-but-empty differs from unset. The snapshot is limited to
1 MiB. Each invocation permits 4,096 steps, probes included, and each step permits
1,023 arguments totaling at most 64 KiB, as many as the process layer passes
and about twice the 497 of `test_tools`' link. A step's own stdout and stderr each hold up to
1 MiB for `build.last_stdout` and `build.last_stderr`. A
reported step also adds its output to the invocation's result, which retains at
most 4,095 bytes of each across reported steps; truncation remains visible in
the result and CLI. A probe's output is data only: compiler reports and tool
queries stay out of the build's output and leave room for its diagnostics.

`build.start(arguments: string[]) -> int` begins a reported step and returns
its handle at once; `build.wait_for(handle: int, milliseconds: int) -> int`
waits at most that long for that step (a negative limit waits without one) and
gives its status as `build.run` would, or 65536 when the step still runs, which
is left running;
`build.kill(handle: int) -> int` stops a step and everything it started, reaps
it and returns its status, the negative signal number for a killed step.
`build.spawn(arguments: string[]) -> int` starts a step like `build.start` whose
standard output and error are the build's own, as they are written, and which
leaves `build.last_stdout` alone. While a program waits for one step, the
host drains every other started step's output, so steps that write more than a
pipe holds still finish. Up to 64 started steps run at once, and each counts
toward the invocation's steps. A wait makes its step's output the last output
and reports it, so the result holds output in wait order. A handle can be
waited for once. Steps the program never waits for are killed with their
process groups when it returns, and a wait past the deadline fails the
program. Like Zig's `std.process.Child` `spawn` and `wait`, this is how a build
graph runs independent compiles in parallel; the compiles are ordinary steps,
not a separate executor such as Ninja.

Child exits return `0..255`; signal termination returns the negative signal
number. Invalid input, spawn failure, timeout, cancellation and host failure
are capability errors with stable diagnostics. The CLI maps a returned negative
signal number to the conventional `128 + signal` exit status.

The caller's absolute operation deadline bounds its children. The native clock
uses macOS uptime excluding sleep or Linux `CLOCK_MONOTONIC`. The host kills
the complete child process group and reaps its direct child on termination,
including when a leader exits before its descendants. This is process-group
cleanup, not containment of children that deliberately create detached sessions.

`BuildProcessFixture.chibi` runs only the controlled C worker supplied through
its private environment. The original `BuildWorkflow.chibi` also runs unchanged
against a disposable worker named `cmake`, testing sequence and failure behavior
without configuring or building the engine. Those original process fixtures are
separate from the compiler bootstrap and build-program compilation checks.

## Build-program compiler

The host compiles the build program with an owned Chibi compiler generation.
It collects source bytes and target facts, invokes the authored package compiler,
then loads its prepared closure through the runtime-only native linker. This
replaces `authoring_packages_prepare` and C semantic preparation in the build-host
path. Native services still provide raw syntax facts, I/O, linking and execution.

The prepared compiler supports `yield` and task calls. The private host rejects
direct suspension with `a build program cannot wait` and an unawaited task call
with `a build program cannot start a task`.

Compiler inputs are separate from the build program's imports:

| Option | Meaning |
| --- | --- |
| `--compiler DIR` | Compiler sources, including `bootstrap.kdl`, `bootstrap/bootstrap.json` and `bootstrap/compiler.chibip`, the seed `tools/bootstrap/seed.sh` fetches from the release `tools/bootstrap/seed.tsv` names. |
| `--compiler-core DIR` | The core collection used to recover that compiler. |
| `--core DIR` | An optional core collection imported by the build program itself. |
| `--collection NAME=DIR` | Another collection imported by the build program. |

Explicit compiler paths resolve from the invocation's working directory.
Without those options, the host searches for `share/chibi/compiler` and
`share/chibi/core` relative to its executable directory: first `../share/chibi`,
then `../../share/chibi`, then `./share/chibi`. This covers build-tree, standalone
SDK and installed `libexec/chibi` layouts without relying on the caller's cwd.
The SDK and Chibi build graph stage the full compiler/bootstrap tree and core
sources in those shared directories. Make's Build and Graph invocations pass
the checkout's `tools/compiler` and `packages/core` explicitly so local edits
are visible.

`chibi_build_compiler_load` validates the closure checksum, bootstrap version
contract, full prepared target/native ABI and source completeness. Matching
source hashes load the recorded closure directly. Changed existing source bytes
load stage one: the seed compiles the current compiler, which has the sources'
logic. That costs one whole-compiler compile on the VM instead of the two or
three the recovery proof needs (stage one, stage two, and stage three when stage
two differs). The proof stays in `chibi build bootstrap-recover` and the
compiler self-build workflow, which run it on every push. Both paths return an
owned generation and write no artifact into the SDK.

The recovered compiler is kept in a cache so that only the first start after a
compiler-source change pays for the recovery, as Zig keeps its global cache. The
build host stores it in `chibi/compiler-cache` under `$XDG_CACHE_HOME`, else
`~/.cache`, so every checkout and worktree on the machine shares it, or in the
directory `CHIBI_COMPILER_CACHE` names; `CHIBI_COMPILER_CACHE=off` disables it.
Past 2 GiB the oldest entries are removed first.
An entry is named by the SHA-256 of what decides its bytes: the seed's closure,
Compile's arguments (every compiler and core source's path and text, and the
target description) and the prepared format. The host is not part of the key:
recovery is deterministic and checks its own fixed point, so Debug, Release,
sanitized and relocated hosts share one entry, as Zig keys its cache by
compiler version rather than by the binary.
Each entry is framed with the SHA-256 of its payload, written to a temporary
file and renamed into place under a publication lock, so a reader sees a whole
entry or none. A load that finds an entry reads it back, checks its frame and
loads it as a compiler for this target; any failure removes it and recovers
again. This is safe where a snapshot of the sources would not be: the key
covers every input, nothing is trusted unread, and a stale entry is never
named. It has no eviction, because a key changes only with a compiler edit;
delete the directory to reclaim it. Build hosts keep the build programs they
compile in the same directory, as `program-` entries named by the SHA-256 of
the compiling closure and the compile input, so a host start reuses its
program instead of recompiling it (about 13 s for the graph program on Linux
CI). Windows does not cache. The recovery proofs
never use it: `chibi-compiler-host --bootstrap ... [--recover FILE]`, `chibi build
test bootstrap-recovery` and the compiler-recovery workflow call the check path, which
neither reads nor writes the cache, so they always recover from the seed.

The C API borrows the generation through `chibi_build_options.compiler`; reuse
it sequentially and reload it after compiler source edits. Release it with
`chibi_build_compiler_free`. `chibi_build_compiler_load` takes the cache
directory, or NULL for no cache.

When changing the prepared format, the checked bootstrap cannot yet run on the
new runtime. `chibi build bootstrap-artifact` first builds the compiler-only SDK language
driver with `CHIBI_SDK_BUILD_NATIVE=OFF`, `CHIBI_SDK_BUILD_DESKTOP=OFF` and
`CHIBI_SDK_BUILD_ASSET_COMPILER_HOST=OFF`. Native authoring depends on the
generated asset service, so it is built after the matching bootstrap exists.
The recovery driver omits that component and retains artifact validation.
After regeneration the target builds the real asset compiler with the component
enabled, its default, before SDK or product validation.

Build-program compilation invokes `CompileClosureWithEffects`, or
`CompileWithEffects` for rejection diagnostics. Their first twelve arguments
match the existing `Compile`/`CompileClosure` bootstrap contract; argument thirteen
is `native_effects: int[]`, carrying the private build catalog's actual per-native
effects. The authored compiler derives effects from emitted native calls and
propagates them through local call chains, recursion and imported summaries before
writing `Entry.effects`. The twelve-argument wrappers supply zeros for the verified
pure bootstrap catalog, preserving the ABI used by pinned runtime seeds. The
`Native` record, target JSON shape and prepared ABI stay unchanged. Build programs
use the effects-aware entries for process, file and state operations.

This is a build-program compiler cutover. Public project/game compilation
still needs declared-component, library, system and projection preparation and
bindings to move. `StageOne.chibi` still uses the C catalog for input export and
final game assembly. Native runtime construction, SDK/full-engine and Wasm builds, Studio
consumers and command-parity checks retain their current CMake responsibilities.

`chibi-compile --stage-one-input FILE` writes the eighteen-argument rich game
compiler input, including nominal port identities and native effect masks, and
reports its executable source count on stdout. `StageOne.chibi` uses the matching
`CompileBehaviorsWithCatalog` and `BehaviorRejectionWithCatalog` entries. For zero
sources, `StageOne.chibi` invokes `chibi-compile --resources-only --output FILE`
with the project root before recovering or invoking the behavior compiler.
This mode assembles scenes, prototypes and resources with no supplied modules;
it rescans the project and refuses any executable source. Nonempty projects
retain the existing compiler and `--modules FILE` assembly route. Empty compiler
results and empty `--modules` files remain failures; CHIBICLO still requires a
root and at least one module.

The `game-equivalence-empty` C dispatcher case builds a disposable resource-only
project through that route with an unavailable behavior-compiler host. It checks
zero modules, artifact and resource-manifest equality with native preparation,
and CPU play traces through frame 30. It also checks that resource-only assembly
refuses a behavior source while preserving prior output, and that malformed or
empty module closures cannot produce an artifact.

## Installed build program

`tools/toolchain/Build.chibi` owns native profile selection, compiler and SDK
precedence, CMake arguments, configuration-cache decisions, process sequencing
and failure reporting. The SDK installs it in `share/chibi/tools/toolchain`.
Run it through the private host with an explicit engine source checkout as root:

```sh
SDK/libexec/chibi/chibi-build-host --root SOURCE \
  --source SDK/share/chibi/tools/toolchain/Build.chibi \
  --core SDK/share/chibi/core --entry Build --timeout-ms 1800000 -- \
  tool-host BUILD --target chibi-build-host --jobs 8 -- \
  -DCMAKE_BUILD_TYPE=Debug -DCHIBI_ENABLE_SANITIZERS=OFF
```

Use absolute SDK paths when source and installation are in different directories.
`native` configures the root CMake project. `tool-host` configures `sdk/` with
the compiler enabled and native/desktop packages disabled; explicit CMake
definitions come last. Repeated `--target` selects build targets, `--jobs` selects
parallelism, and `--configure-only` stops before compilation. Arguments after
`--` pass directly to CMake. The public `chibi build` still builds authored games.

On Linux and macOS `chibi build` compiles with the Chibi build graph instead
of CMake. `Build.chibi third-party BUILD_DIR` prepares the pinned
third-party archives, then the host runs `tools/toolchain/Graph.chibi` with
the profile name (`sanitizer` by default, or `debug` and `release` from
`chibi build --debug` and `chibi build --release`) and
`CCACHE_BASEDIR` set to the checkout, as CMake sets it, and `--all-commands
on`, which writes every target's compile commands; it links the root
`compile_commands.json` to them, as CMake did, for clangd. `LOCAL_CMAKE_FLAGS`
no longer apply there; the graph finds the profile's toolchain on the
machine. Without a current host, the driver (`tools/toolchain/Driver.chibi`, run as `chibi build` or
`tools/build.sh`) fetches the pinned first one with `tools/bootstrap.sh` and the graph builds its own. The SDK, web and Studio builds
still use CMake as below, so a source, define or library CMake gains
belongs in `tools/graph/engine/Targets.chibi` too.

### Linux native graph

Linux uses the same default graph for native products and the full
tooling/language test drivers. `chibi build` builds the Release `chibi-build-host` and `chibi-compiler-host` in `build-release` first and generates the asset compiler with them, because Debug hosts exceed the compiler deadline from a cold tree; `--asset-generator-hosts DIR` names other hosts. For a fast Debug O1 build with Release generators:

```sh
chibi build --release --target chibi --target chibi-build-host --target chibi-compiler-host
chibi build --debug --debug-optimization 1 \
  --debug-frame-pointers on --host "$PWD/build-release/sdk/chibi-build-host" \
  --asset-generator-hosts build-release/sdk \
  --target chibi --target chibi-compile --target test_tools --target test_language
```

Use the existing Linux development prerequisites and pinned third-party archive;
player, renderer and native-export support are not removed. `LIBCLANG_ROOT` is
forwarded explicitly. Graph pairs versioned compiler drivers such as `clang-18`
and `clang++-18`, and discovers OpenGL development headers/libraries through the
explicit include/library search paths and the selected compiler. Direct Graph
invocations can supply `--opengl-include`, `--opengl-library` and `--glx-library`;
`--profile explicit` performs no discovery. `GRAPH_REFLECT` forwards the existing
`--reflect` override for cross or relocated toolchains without extending the
build host's environment allowlist.

Plain `chibi build` retains the sanitizer profile. Native CI uses fast Debug O1
on ordinary PRs/pushes and full ASan+UBSan Debug O1 for daily/manual sanitizer
runs; actual compile/link records, assertions and sanitizer symbols are checked
before the unchanged test groups. Release planning/generation helpers do not
replace the selected-profile products or test drivers.

The separate `ci-linux-graph` (fast) and `ci-linux-graph-sanitizers` labels request
full parity, generated/resource, group and compiler-tools relocation proof for
that exact same-repository PR head. Manual dispatch selects the same profiles.
These checks are opt-in, not a second full suite on every push.

The CMake build that supplied the `build-cmake` parity tree (the Makefile's
`graph-reference` target) and the retained CMake native route were removed in
ef553b24b, and the Makefile was retired in e4aa7b4e2. The driver fetches the
pinned first host with `tools/bootstrap.sh`.

The graph parses compiler dependency files in Chibi, reading their bytes once
and treating line continuations as spaces. It unescapes only paths that need
it. Large record and command-database joins use the existing `build.join`
primitive rather than repeatedly copying an accumulated prefix. Small path
joins remain ordinary Chibi strings so their temporary storage can be reclaimed.
Unknown or missing `--target` names fail before toolchain discovery or writes.

Elsewhere `chibi build` uses this program when its private host reports protocol version 15
through `--protocol-version`. The protocol rises, with the gate in `tools/build.sh`, whenever
the compiler bootstrap or `Build.chibi` needs a prepared format, capability or
syntax an older host lacks; version 3 adds the
file primitives and probes and the redesigned syntax, version 4 names the
natives under `build.`, version 5 runs programs as packages that may import
`core:`, version 6 has the array methods and the natives `append` and
`extend` of project version 32, and version 7 has what the macOS graph build
needs: `build.replace`, `build.byte_list` and steps of up to 1,023
arguments. Version 8 uses the validated Chibi compiler generation to compile
build programs and adds the separate compiler source/core options above.
Version 9 runs prepared format 21, which that compiler now writes, so an older
host that cannot load the code it compiled is rebuilt.
Version 13 loads the collections-as-values compiler bootstrap with prepared
format 26. Protocol-12 hosts must take first-host recovery before loading that
bootstrap, even when an existing build directory contains an executable host.
Version 14 reuses native-result storage after the VM imports each callback's
results. Older hosts take first-host recovery before a warm full graph can
exhaust their retained helper storage while trying to rebuild them.
`--protocol-version` reports the version without loading compiler inputs or
starting a build. A cold build or an older host
fetches the pinned host with `tools/bootstrap.sh` (the driver's bootstrap step). `BUILD_HOST` and
`BUILD_PROGRAM` can select a relocated installation. The explicit recovery
target remains available if the authored driver cannot run.

The program gives explicit compiler/toolchain/preload options precedence over
fresh-build defaults, retains cached selections, and discovers the matching
macOS SDK. The configuration stamp tracks caller arguments, environment,
loaded CMake files, new configuration files and compiler/tool identities. It is written atomically only after successful configuration. Build
targets and parallelism do not invalidate configuration.

Before configuring, the program makes sure the cache holds every third-party
archive `third_party/archives.tsv` pins for its host (`third_party/README.md`).
It downloads a missing one with `gh release download`, refuses it unless its
SHA-256 matches, unpacks it with `tar` beside the store and renames it into
`.git/chibi/third_party/<sha256>` in the clone's Git directory, which all its
worktrees share, then passes that store to CMake as `CHIBI_THIRD_PARTY_CACHE`.
CMake picks the archive for the build's variant. Afterwards the program removes
archives that no worktree's `third_party/archives.tsv` pins
(`git worktree list`).

On macOS the program finds the SDK itself when neither `SDKROOT`, the cache nor
an explicit definition names one. It splits the selected compiler into words as
Python's `shlex.split` does, unless the compiler names one file, probes its
`-### -x c -fsyntax-only -` report and takes the directory `-isysroot` names,
following links; `xcrun --show-sdk-path` answers otherwise. Apple clang's report
is about 4.1 KiB.

One installed Python helper remains a transitional dependency:
`configure_inputs.py` retains the read-only signature probe. Its replacement and
removal gate remains in the toolchain inventory. CMake still owns the stage-zero
graph.

The private primitives expose exact arguments, platform and root paths,
executable lookup, bounded regular-text reads, atomic writes, directory creation,
last-process output, the file primitives below and string/array operations. File
and text values are limited to 16 MiB, enough for a build graph's
`compile_commands.json`, arrays to 8,192 entries and 16 MiB of text, and
each helper bank to 256 MiB of retained arena blocks. Native-result storage is
reused on the next callback after the VM imports the results; invocation
arguments remain in their own bank. Last-output accessors
reject truncation and embedded NUL bytes, so authored protocol parsing cannot
accept a partial result. Returned values keep their ordinary language lifetimes
in VM storage; parent cwd and environment are never mutated.

### Build-host file primitives

The remaining configuration and packaging Python reads file metadata, walks
trees, copies, renames, removes and hashes. Zig's `std.fs.Dir`, Rust's
`std::fs` (which Cargo build scripts use) and Odin's `core:os` expose the same
small layer: one-level directory iteration with walks built over it
(`Dir.walk`, Odin's `Walker`, `fs::read_dir` recursion), a stat record, atomic
copy that keeps permissions, rename, and single-entry removal with recursive
removal built over it. Hashing lives in their libraries
(`std.crypto.hash.sha2`, `core:crypto`). The host keeps to that layer; walks
and recursive removal are authored Chibi. Dependencies come prebuilt, as
Odin vendors its libraries, so the dependency fetch and caches, and the
interprocess lock they needed, went away rather than moved.

| Primitive | Result |
| --- | --- |
| `build.file_kind(path) -> string` | `file`, `directory`, `link` or `other` for the path itself, not a link's target; `""` when nothing is there. Follow links by resolving the path first. |
| `build.file_stamp(path) -> string` | Opaque change stamp of the file a path names after links: device, inode, size, mode, and modification and change times, as Python's configuration signature records. `""` when nothing is there. |
| `build.list_directory(path) -> string[]` | Entry names except `.` and `..`, sorted by byte; no names for a missing directory. At most 8,192 names and 16 MiB of names. |
| `build.copy_file(source, destination) -> bool` | Replaces destination atomically with a regular file's bytes and permission bits through a temporary beside it, removed on failure. Follows a source link. |
| `build.rename(source, destination) -> bool` | `rename(2)`. |
| `build.remove(path) -> bool` | Removes a file, link or empty directory; true when nothing is there. |
| `build.hash_file(path) -> string`, `build.hash_text(text) -> string` | Lowercase SHA-256 hex of a regular file's bytes, streamed, or of text; a missing file hashes to `""`. |
| `build.embed_file(input, output, symbol) -> bool` | Writes a C file defining `const unsigned char symbol[]` with a regular input file's bytes (at most 64 MiB) and `symbol_size`, in the form `cmake/embed_binary.cmake` writes, so the bytes need no Chibi loop. An output that already holds that text is left alone and keeps its stamp; the result says whether it changed. A symbol that is not a C identifier or an input that is not a regular file fails the program. |
| `build.replace(text, find, with) -> string` | Text with every occurrence of a non-empty `find` replaced by `with`, left to right, in one native pass; `core:strings.ReplaceAll` scans byte by byte in the interpreter, and rewriting Cute's 96 KB of codec headers took it 11 s. A result past the text limit fails the program. |
| `build.byte_list(path) -> string` | A regular file's bytes as `0xHH,` for each, the C initializer CMake writes from `file(READ ... HEX)` and its `0x\\1,` replacement, so embedding a source needs no Chibi loop. At most 3.2 MiB of input, a fifth of the text limit. |

Queries return a capability error for failures other than absence; mutations
return false. Every primitive checks the deadline and cancellation, and copies
and hashes check them between 64 KiB chunks. `support/sha256.c` is shared with
the bootstrap artifact's digests.

These primitives do not yet cover everything the Python does. The configuration
signature hashes the full environment; children see only the allowlist, so
`build.environment_names` suffices. It no longer tracks dependency packages or
source caches. Packaging also needs sizes,
modification-time order, symbolic-link reads and unique staging directories.
Each invocation may run 100 million instructions, as a package test may; the
build graph raised it from 100,000 because it parses a dependency file per
object. `chibi_build_options.instruction_budget` sets a smaller one. The
deadline still bounds the time.

## Build graph (M6)

The build graph replaces CMake's internal build graph one target at a time.
Its library is the `graph:` collection in `tools/graph`, and
`tools/toolchain/Graph.chibi` is the program that runs it:

```sh
build/sdk/chibi-build-host --root . --source tools/toolchain/Graph.chibi \
  --compiler tools/compiler --compiler-core packages/core \
  --core packages/core --collection graph=tools/graph --entry Libraries \
  --timeout-ms 600000 -- --build "$PWD/build-graph" --profile sanitizer
```

- `graph:c` compiles C sources into static libraries. Its functions read like
  Jai's `BuildCpp` rather than a declarative target language: `c.Stale`,
  `c.Start`, `c.Finish` and `c.Archive` take a `c.Tools` record (compiler,
  optional launcher such as ccache, archiver, ranlib, jobs) and a `c.Library`
  (root, object directory, archive). Sources and flags are arrays beside the
  record, because records hold only scalar fields.
- `graph:engine` lists the libraries it owns in dependency order (`Targets`)
  and gives each one's sources, public and private headers and the libraries
  whose headers it uses, by name. `engine.Build` stages every selected
  library's headers, then starts the compiles that changed across all of them,
  up to `jobs` at once, then archives each. A compile needs only staged
  headers, so no library waits for another's archive. The graph executes the
  compiles itself through `build.start` and `build.wait_for`, as Zig's build
  runner does, with no Ninja.
- Every run first writes `BUILD/compile_commands.json` in the `arguments` form,
  as CMake exports its own; `--commands-only on` stops there. `--target NAME`,
  repeated, builds only the named libraries.
- Each compile writes `-MD` dependencies. After it succeeds, the graph writes
  a record beside the object: the command, then the path and, on its own
  line, the `build.file_stamp` of every input and of the object. A later run
  recompiles a source when its record is missing, its command differs, or any
  recorded stamp changed; the archive is rebuilt when an object was or its own
  record differs. A failed compile leaves no record, so the next run retries
  it. `build.lines` scans each record once. Paths and stamps are separate
  lines, so validation compares whole fields without another split. The VM
  imports native results before another callback reuses the helper bank;
  checking more records does not retain every old native line buffer. The
  helper bank's 256 MiB retained-block bound, 16 MiB text bound and 8,192-entry
  array bound remain in force.
- Headers a library publishes are staged below `BUILD/sdk/public/COMPONENT`,
  and those only its sources include below `BUILD/sdk/private/COMPONENT`, as
  CMake stages them. A header is copied only when its contents differ, so
  unchanged headers keep their stamps, and a staged file no header names is
  removed, as `chibi_sdk_headers` does.
- A compile's flags are its defines, then the source's own include
  directories, then the target's in CMake's order (staged `public/NAME` and
  `private/NAME` directories, source-tree directories such as
  `third_party/fmt`, or `third-party/` directories in the third-party
  archive), then its system include directories as `-isystem`, such as the
  staged Cute math headers, then the profile flags root `CMakeLists.txt`
  adds, in its order: the ccache debug directory, the build type's flags,
  the target's `-std` (`gnu23` unless it declares another), `-arch` and
  `-isysroot`, `-fPIC` for position-independent targets, the warning set,
  the sanitizers and Clang's `-Wno-gnu-zero-variadic-macro-arguments`; then
  the target's own options, such as `-w` for vendored C, and the source's.
  A C++ source takes the target's C++ standard, or no `-std` when it declares
  none. An Objective-C source starts with `-x objective-c`, takes CMake's
  `gnu2x` for C23 and has no ccache debug directory: root `CMakeLists.txt`
  adds that before `sdk/desktop.cmake` enables Objective-C, a CMake defect the
  graph copies so their commands stay equal.

`graph:engine`'s `Targets.chibi` lists the 104 libraries the graph owns on
macOS and Linux, from `chibi_support` through the shader, model and image
runtimes, the native capability tables, the documents and fields libraries,
the vendored in-tree C (`chibi_fmt`, `chibi_json` over yyjson) and the
libraries over them, and three header-only libraries
(`chibi_execution_contract`, `chibi_tasks`, `chibi_input_action`),
`chibi_builtin_fonts`, whose fonts are generated sources, `chibi_save_runtime`, and
`chibi_execution` over the third-party archive's prebuilt SLJIT and the
header-only `chibi_math` from it, with the libraries that need only those (the
behavior, scene, mesh, tilemap and UI runtimes, the math, vector, arithmetic, core and
Studio capabilities, the prepared native catalog, model baking and the scene
and prototype documents), and `chibi_native_types` and `chibi_native_bindings`,
whose sources `reflect` writes, with the libraries over them (the spatial,
entity, particle and input runtimes, declared
components, the visual catalog, scene authoring and the scenario document),
and, over the archive's prebuilt tree-sitter runtime, CSS and HTML grammars,
Box2D and Box3D, the language and project syntax documents and UI authoring,
and over its prebuilt enkiTS task scheduler the task pool, material, physics,
visibility, graph model, authoring and compilation, Studio catalog and graph,
engine and default native catalog libraries over it, and image preparation, the language,
compiler, authoring, asset authoring, graph catalog, model authoring, shader
preparation, source catalog and presentation and player libraries, and over
the desktop Cute archive the platform, model import, UI host (C++17 over
RmlUi) and renderer (with its Objective-C GPU clock on macOS) and the project
compilation, format and preparation libraries: 594 sources in 100 archives.
`ExportName` names the targets whose CMake `EXPORT_NAME` differs, as
`chibi_ui_host` stages `public/ui` and `chibi_renderer` `public/render`. A name that is no target, such as `chibi_physics` or
`chibi_graph`, lists the headers of a `private/NAME` directory several
libraries include, as CMake stages it; `PrivateDirectory` stages a target's
private headers elsewhere, as the language's sit below
`BUILD/sdk/language/private`. A `Rewrites` entry `HEADER=INPUT` writes a
private header from INPUT with each `Renames` pair replaced
(`build.replace`), as `sdk/images.cmake` gives Cute's codecs private symbols,
only when its text changed, and staging keeps it; a missing input or a failed
write fails the build. For
each it gives sources, the directory its public headers
come from (`src`, `third_party/yyjson` for `chibi_json`, or the archive's Cute
headers for `chibi_math`), public and private
headers, include directories, defines, options, options of single sources
(CMake's `COMPILE_OPTIONS` source property, such as `-ffp-contract=off` for the
transform and particle kernels), include directories of single sources, which
come first (`INCLUDE_DIRECTORIES`, each grammar's parser headers), the C
standard (`gnu23`, or `gnu17` for Box2D and Box3D's `C_STANDARD 17`), whether it
is position-independent (`-fPIC`) and embedded files. A generated source is a
`build/` path, below the build directory where CMake writes it. An
`Embeds` entry `OUTPUT=SYMBOL=INPUT` writes the generated source OUTPUT with
`build.embed_file`, before any compile. A header set's `Configures` entry
`HEADER=INPUT` writes HEADER below its private root as CMake's
`configure_file` does when its text changed, and staging keeps it: a
`.in` INPUT with each `@NAME@` of the target's `Substitutions` replaced
(`bytes:PATH` through `build.byte_list`, `cstring:PATH` as C string lines),
any other INPUT copied; the language tests embed the Chibi sources they
compile this way. A source's own defines (`SourceDefines`, CMake's
`COMPILE_DEFINITIONS` source property) follow the target's, with
`{source_root}`, `{build_type}` and `{sha256:PATH}` filled in. A target whose `Generator` is
`reflect` first makes the graph build `reflect` and stage the math headers,
then run it as root `CMakeLists.txt`'s `generate_runtime_registry` does, to
write `BUILD/generated/native_types.c` and `native_capabilities.c`; `c.Step`
records the run as a link is recorded, over the inputs CMake names, and
reruns it only when one changed. `--reflect PATH` runs another reflect
instead, as a cross build's `CHIBI_REFLECT_EXECUTABLE` names one. Building
a subset with `--target` also stages the headers of every library its
includes name. On Linux and macOS `chibi build` builds them with the graph;
`graph-build` rebuilds `chibi_support` incrementally in a copy of the sources.
Before the dependency-parser and join improvements, building
everything, `test_tools` included, took about 25 to 32 minutes with sanitizers
and no compiler cache at a load average of 5.

A `third-party/` path in the tables is below the unpacked third-party archive
that `cmake/third_party.cmake` loads for the profile: `engine.ThirdParty` reads
`third_party/archives.tsv`'s pin for the host's target and the profile's
variant (the sanitized archive matching the profile's sanitizers, except with
Apple clang) and finds it below `.git/chibi/third_party` in the clone's Git
directory. The graph does not download it; `Build.chibi` or CMake's configure
does, and a run that needs it stops and says so when nothing is pinned for the
host or the archive is not unpacked.
`--third-party DIR` names another. Objects of its sources sit below
`objects/TARGET/third-party/`.
The graph also links executables. `engine.Programs` lists them (the
reflection tool `reflect`, the build host, the compiler host, the `chibi`
launcher and the host, concurrency, policy-worker and benchmark test drivers,
the first two over the header set the graph stages below
`BUILD/testing/host/include`, as root `CMakeLists.txt` does, and on macOS
`chibi-compile`, `test_language` and `test_tools`, whose
`Links` name the desktop archive's libraries as `third-party/` paths and its
frameworks as CMake orders them; Linux's follow its archive). An executable
over C++ (`LinksCxx`) links with the C++ compiler. `c.Link` runs CMake's
executable link (the language and link
flags, the sanitizers, the executable's `LinkOptions`, the objects, `-o`,
then the profile's external libraries and `Links` in CMake's order, which
may name a library twice and holds `-lm` where CMake puts it) and records it
as it records compiles. Compiler-driver execution compresses nonempty argument
runs into quoted GNU response files beside the output when their paths are
smaller than the original arguments. Empty arguments remain directly in argv,
preserving option operands on older Clang versions. Transport never increases
argument count or byte use. The command database and successful record retain
the full ordered argv. Responses are removed after the child returns, and
success is recorded only after cleanup. Failed or interrupted attempts have no
success record; retry removes retained regular base and numbered responses.
NUL and line endings are rejected because the record
stores one argument per line. Other build steps and archivers keep their own
command contracts.
The native regression group uses the host in its selected `BUILD_DIR`;
`CHIBI_BUILD_HOST` can explicitly select another matching host.
An object library (`ObjectLibrary`, as
`chibi_build_host`) compiles but is not archived; an executable's
`LinkedObjects` put its objects before the executable's own. On macOS the
graph also compiles the tooling tests' SDK contract consumers, object
libraries over staged SDK headers and the fixtures it copies to
`BUILD/test/tools/testing/include`, as `test/tools/CMakeLists.txt` does,
and links `test_tools` over them. A target's defines take the same
placeholders as a source's, and `{program:NAME}` names where an executable
is written: below the build directory where CMake writes it
(`ProgramDirectory`: `sdk/` for the SDK's programs and the test drivers'
CMake subdirectories). A `compiler:ARGUMENT` substitution
writes the profile's C compiler and C flags, then ARGUMENT, as C string
initializer lines; `test_tools` exports C with them. An include of `.` is
the source root.
A target's `Resources` (`DESTINATION=SOURCE`; a directory copies every file
below it) are written before any compile, as root `CMakeLists.txt`
configures them: the launcher's `BUILD/share/chibi`, which `chibi version`
and `chibi test` read as installed ones do. Its `engine.json` is the build
identity the graph writes to `BUILD/sdk/chibi_engine_metadata.json` as
`sdk/artifact_formats.cmake` does: the SDK version from
`sdk/VERSION`, the project version from `src/assets/project.h`, the
platform, processors and backends, and each of `ArtifactFormats`' versions
with the SHA-256 of the sources and headers that own it. A target's
`SourceLists` (`OUTPUT=ROOT`) are written then too: every `.chibi` file below
ROOT by absolute path, byte-sorted, as
`test/language/CMakeLists.txt` writes the language corpus `test_language`
reads.
Every run
writes `BUILD/link_commands.json` beside the compile commands. Naming a
program with `--target` also builds the libraries it links and the programs
it runs (`Runs`, CMake's `add_dependencies`), with theirs. `reflect` takes libclang from the profile
(`--libclang-include`, `--libclang-library`), and the graph finds clang's
builtin headers as `tools/reflect/resource.cmake` does, by asking that
libclang's `clang -print-resource-dir`.

A run finds its profile on the machine unless told otherwise
(`graph:engine`'s `Profile.chibi`). `--profile` names the
`sanitizer` (the default), `debug` or `release` profile. The compiler is `CC`,
or Homebrew LLVM's clang, or `cc`, and its kind
comes from its `--version` banner. The C++ compiler, which compiles `.cpp`
sources with the same flags and the target's C++ standard, is `CXX` or the
`clang++`, `g++` or `c++` beside the compiler, or the one on `PATH` for the
compiler's kind; a run that compiles C++ without one stops and says so. On macOS the architecture comes from
`uname -m` and the SDK from `SDKROOT`, the compiler's `-###` report (resolved
through its versioned link, as `Build.chibi` resolves it) or `xcrun`. ccache,
when found, launches compiles and, for a build below the source tree, sets the
debug directory CMake sets. libclang comes from root `CMakeLists.txt`'s
hints. `ar` is the one on PATH and `ranlib` LLVM's beside the compiler. Each
explicit option still overrides what it names. Writing the compile commands
escapes an argument only when the host's `build.before` finds a backslash or
quote in it, because `core:strings.ReplaceAll` scans byte by byte in the
interpreter: with 218 commands it took 7 s, and then 2.4 s.

Windows keeps CMake: the build host has no Windows process support yet.

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
   Require matching semantics, not assumed speedups. On 2026-10-20 the
   compiler's own workload ruled it out for now: SLJIT's generated code was
   6.2% of stage one's samples while it compiled the compiler, and the rest
   was C runtime value copies and per-call resets that an LLVM backend would
   still call.

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

The contracts below describe the private compiler transport. They do not
establish self-hosting completion.

The existing raw parser bridge now retains aligned Tree-sitter named/extra flags
and a syntax root separate from the document's routing root. Its private native
result ABI changes from thirteen to fifteen results; prepared closures must be
regenerated. The actual eight-package snapshot closure is unchanged in scope,
and incompatible native signatures must reject before invocation.

The existing Linux/Darwin `chibi-compiler-host` has an optional private
`--output-artifact FILE` mode. Its other arguments remain `--artifact FILE`,
`--entry HEXID`, `--input FILE`, `--backend bytecode|sljit`, `--budget N` and a
finite caller-supplied `--timeout-ms N`. Binary output and
`--batch` are mutually exclusive. `--output-bytes FILE` writes a `byte[]`
result that is not a compiler, such as stage one's closure of a game's
modules, without loading it as one, and fails when the entry returns no
bytes. Without either switch, JSON stdout and JSON batch behavior are
unchanged. Public `chibi`, `chibi-compile` and `chibi-build`
selection, cache and installed-format contracts do not change.

`compiler_host_invoke_bytes` accepts the existing owned JSON argument array and
requires exactly one primitive `byte[]` return declaration. Address/reference
argument or result provenance rejects before JSON decoding. The caller chooses
an output limit from 1 through `COMPILER_HOST_MAX_ARTIFACT_BYTES` (32 MiB)
octets, and the limit also bounds the result's represented storage. Every
in-process caller except the bootstrap passes `COMPILER_HOST_MAX_OUTPUT_BYTES`
(16 MiB). The package compiler's own closure is about 23 MB, so the bootstrap
compiles it and reads its seed and artifact closures with the artifact bound.
The CLI's file outputs pass it too: `--output-artifact` writes a validated
compiler closure, and `--output-bytes` writes any `byte[]` result without
validating it, such as the asset compiler's closure of about 18 MB.
After successful return, the host validates the runtime array shape and item
types and copies each full `pin_value` item's `.u8` into malloc-owned bytes
before releasing runtime storage. Empty success is a null pointer and zero
length; failure is also null/zero with a false result and diagnostic. Returned
bytes survive input, prepared-artifact and host teardown. The API is format-
agnostic and preserves all 256 octets without text encoding or JSON projection.
The represented-output ceiling, header, declared item capacity up to twice the
limit and packed bytes, follows from that limit. These are transport/storage checks, not a whole-compilation
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

The finite compiler-artifact fixture described in `docs/testing.md` remains a
transport/publication check: stage zero prepares and serializes its tiny echo
artifact. Compiler self-reproduction is established separately by the bootstrap
proof below. Build programs use the compiler-generation path described above;
public game input export and artifact assembly retain their C catalog path until
the remaining declaration and native-binding contracts move.

## Bootstrap artifact

The recovery artifact of the stage-one package compiler is checked in at
`tools/compiler/bootstrap`, as Zig checks in `stage1/zig1.wasm`. It is a seed,
not a snapshot of the current sources: it has to load on the pinned runtime and
compile the sources on disk, and nothing else ties it to them. The compiler
reaches its fixed point from the seed (see below), so compiler changes do not
queue behind a regenerated artifact.
`chibi build bootstrap-artifact` rewrites it (`BOOTSTRAP_ARTIFACT` names another
folder). It runs in two steps:

1. `chibi-compiler-host --bootstrap DIR --compiler tools/compiler --core
   packages/core --recover SEED/compiler.chibip --arguments SEED/compile.json`
   recovers the compiler from the current seed: it compiles the sources on
   disk to stage one, stage one compiles them to stage two, and stage two must
   compile them to itself. It writes that fixed point and Compile's arguments
   (with `debug` false) to the seed folder, `build-release/bootstrap.artifact`,
   which the target removes after.
2. `chibi-compiler-host --bootstrap DIR --compiler tools/compiler --core
   packages/core --seed SEED --revision REV` checks three things: that the
   sources the seed compiled are the ones on disk, with every file of their
   packages; that its target is this host; and that it compiles them to
   itself. It then writes `DIR/compiler.chibip` and, last,
   `DIR/bootstrap.json`, each through a new temporary file and a rename.

`bootstrap.json` records these fields:

- `format`: the manifest format this host reads, 1.
- `version` and `minimum`: copied from `tools/compiler/bootstrap.kdl`.
- `revision`: the tree's `git describe --dirty` revision.
- `entry`: `CompileClosure`'s entry ID.
- `closure`: its file, length and SHA-256.
- `project` and `core`: the virtual roots Compile saw.
- `sources`: each source the seed compiled, in Compile's order, with its length
  and SHA-256. This records where the seed came from. Recovery takes the
  sources to compile from `tools/compiler/asset_compiler/CompilerSources.txt`
  and uses the record only to tell whether they changed.
- `target`: Compile's last six arguments, copied verbatim. These are the external
  inputs:
  - the value layout (`Target`);
  - the prepared ABI;
  - the instruction names;
  - the natives and their ports;
  - each native's identity and signature ABI.

`tools/compiler/bootstrap.kdl` is the sources' bootstrap contract. `version` is
the bootstrap version a compiler built from them has. `minimum` is the oldest
version that can compile them. Update language dependencies in two steps:
1. When the compiler gains a feature, raise `version` and produce an artifact.
2. Raise `minimum` only once the sources use that feature.

`chibi-compiler-host --bootstrap DIR --compiler ROOT --core ROOT` recovers the
compiler from the artifact. Before compiling, it rejects each of the following
with its own message:

- a manifest format it doesn't read;
- a version older than the sources' minimum;
- a closure other than the recorded one;
- a listed source that is missing, or a package folder holding a `.chibi` file
  that `asset_compiler/CompilerSources.txt` does not list, because a package
  compiles only the files it is given;
- a target other than the host's own (`compiler_host_target_json`), compared
  in full: value layout, prepared ABI, instruction names, natives, their
  ports and native identities.

It then compiles the listed sources from disk with the artifact:
- If they are the sources the artifact recorded, the result must be the
  artifact's own bytes.
- If they differ from the record (edited, added, removed or reordered), the
  artifact is only the seed, as zig1 is Zig's. It compiles the sources to stage
  one, and stage one compiles them to stage two. When the two are the same
  bytes, the compiler has reached its fixed point. When they differ, because
  the sources changed how the compiler generates code, stage two compiles them
  to stage three, which must be stage two's bytes. The fixed point is the
  recovered compiler. The seed's own code generation never has to match the
  sources', and the sources never have to match the seed's record.

The in-process `compiler_bootstrap_load` API uses the same validation but loads
matching source hashes directly. Changed sources take the recovery path above
and return the recovered compiler as an owned host. This API writes no
files, including when an output path is present in its options; the CLI check
above still performs the reproduction proof and can write `--recover FILE`.

`chibi build test bootstrap-recovery` exports HEAD's `tools/compiler` and `packages/core` with
`git archive` and runs that check with the Release host. On macOS arm64 the
check takes one compile, about 11 s, two when the sources changed, about
26 s, or three when they also changed the generated code; the whole selector
takes about 18 s.

A clean checkout recovers the compiler with `chibi build bootstrap-recover`. It runs
`chibi-compiler-host --bootstrap tools/compiler/bootstrap --compiler
tools/compiler --core packages/core` and needs neither stage zero, nor a
compiler built from Chibi, nor a network. `--recover FILE` also writes the
compiler it recovers, which `tools/toolchain/StageOne.chibi` runs. Git stores the artifact by content,
and the manifest pins the closure's SHA-256, so a damaged closure is refused
before it loads. Its target is the value layout and prepared ABI rather than an
operating system, so one artifact serves every host with that ABI.

### Three-platform compiler recovery proof

The `Compiler self-build` workflow in `.github/workflows/compiler-recovery.yml`
runs on every pull request into `main`, push to `main` and manual dispatch.
Its macOS 15, Ubuntu 24.04 and Windows 2022 jobs download an existing runtime
binary from the release pinned in `tools/compiler/runtime.json`. The manifest
records each binary's filename, byte length and SHA-256, plus the source
revision, tree and CI run that produced it. Downloaded bytes are checked before
execution. No job installs or invokes a C compiler or CMake, and none depends
on a native build job. A missing or incompatible seed fails the job rather
than silently building a replacement.

The runtime binary is an explicit bootstrap input, alongside the checked-in
Chibi compiler artifact. It executes prepared Chibi code using SLJIT and
exposes native services, including raw Tree-sitter syntax facts. It excludes
the C semantic frontend. The authored compiler performs semantic analysis and
code generation; its output is a prepared `compiler.chibip` artifact executed
by the same runtime. This proof rebuilds the compiler, retaining the native
runtime as an external binary dependency.

The proof copies only the runtime executable, `tools/compiler` and
`packages/core` into a directory outside the checkout. It rejects native source
files there, clears the child environment, gives it an empty `PATH` and
unavailable `CC` and `CXX`. After creating the `stage1` output directory,
it launches the copied executable directly:

```sh
./chibi-compiler-host --bootstrap tools/compiler/bootstrap \
  --compiler tools/compiler --core packages/core --recover "$PWD/stage1/compiler.chibip"
```

On Windows the executable is `chibi-compiler-host.exe`; the workflow passes an
absolute output path with native separators. After this invocation succeeds,
the workflow copies the original bootstrap manifest beside that exact output,
updating only its closure length and SHA-256. The inherited revision and source
hashes continue to identify the original seed's provenance. It then invokes
the runtime with `--bootstrap stage1` to produce stage two. This second
process loads and executes the compiler just built, and the outputs must have
identical hashes. Each invocation also performs the recovery checks above,
including the fixed-point compiles when the sources differ from the record.
The proof therefore checks the self-hosting fixed point of the current
sources, not that they match the checked seed. The scalar regression that
follows runs with that fixed-point compiler.

Each job uploads both compiler stages, their SHA-256 hashes, logs and JSON evidence
with seed/runtime identities, input inventory, revision, platform, cleared
environment and timings. The comparison job requires matching compiler bytes
across all three platforms. Every job is nonadvisory; skipped or failed
platform proofs cannot satisfy the comparison job. Environment and input
isolation do not restrict the runner's filesystem.

`.github/workflows/compiler-runtime-seed.yml` is the separate manual recovery
route for preparing replacement runtime binaries. It uses the source-only SDK
mode (`CHIBI_SDK_COMPILER_HOST_ONLY=ON`), a C compiler and CMake, and retains
the recovery proof and provenance with each binary. Dispatched with `publish`
(the `ci-seed` PR label does this), it also publishes a commit-specific
prerelease and, on a branch other than `main`, commits the new pin to it. The
release holds the three seeds, `runtime.json`, `SHA256SUMS`, each platform's
recovery evidence and native imports, the commit's `git archive`, the pinned
SLJIT and Tree-sitter sources, and the notices. Review the pin commit with the
change that needed it. Routine compiler self-builds continue to consume the
pinned release. The initial runtime seeds
were produced by the predecessor recovery workflow at the recorded revision.

Rewrite the seed only when it can no longer compile the sources on disk or no
longer loads on the pinned runtime, and then in a PR of its own or in the one
change that needs it:
- when `minimum` rises past its version, which is when the sources start to
  use a language feature the seed cannot compile;
- when the host's target changes, which moves the pinned runtime with it;
- when the prepared payload or semantic version changes, even if layout and
  instruction names remain unchanged.

Adding or removing a source file, editing sources, and changing the code the
compiler generates for itself need no new seed. A new file needs a line in
`tools/compiler/asset_compiler/CompilerSources.txt`.

A language feature takes two PRs, as in Zig, where a feature lands before
`zig1.wasm` is rebuilt and only then do `stage2` sources use it:
1. Add the feature to the compiler and raise `version` in
   `tools/compiler/bootstrap.kdl`. The compiler sources themselves must not use
   the feature yet, because the checked seed cannot compile it. Run
   `chibi build bootstrap-artifact` and commit the seed with this change.
2. Use the feature in the compiler sources and raise `minimum` to that
   `version`. The seed from step 1 compiles them.

To tell whether a compiler PR needs a seed refresh, run `chibi build bootstrap-recover`
(or `chibi build test bootstrap-recovery`) on its head merged with `main`. If recovery reports
that the seed cannot compile a source, the sources use something the seed
lacks: move that use to a later PR or refresh the seed first.

To rewrite the seed:
1. Run `chibi build bootstrap-artifact` on a clean tree, so that it records a
   revision without `-dirty`.
2. Commit `tools/compiler/bootstrap` together with the change that needs it.

The final installed workflow must compile Chibi and drive build/link with
`cmake`, `clang` and `gcc` executables
unavailable, including hidden or renamed equivalents. Explicit platform
assemblers/linkers, SDKs, native backends and runtime libraries may remain behind
narrow bindings. Current native tools remain construction/recovery inputs during
migration. Final acceptance also requires the installed clean-checkout proof to
execute stage one and stage two and build/link a checked game.

## Compiler tools from graph outputs

`tools/toolchain/SDK.chibi` owns the first part of graph SDK installation. It
packages the graph-built private build and compiler hosts into a fresh prefix,
with the complete compiler/bootstrap/core resources, Build/Graph/SDK/StageOne programs,
graph packages, the configure signature helper and dependency notices. It
preserves bootstrap manifests and artifacts byte for byte. Public SDK libraries,
headers, CMake exports and `package.json` remain with the existing SDK packager.
StageOne is staged verbatim for the compiler owner's installed CLI integration;
this increment does not establish an installed StageOne execution route.

Build matching Release tools, then name the same unpacked dependency archive
that their `.chibi-record` link records name:

```sh
chibi build --release  --target chibi-build-host --target chibi-compiler-host
build-release/sdk/chibi-build-host --root "$PWD" \
  --source tools/toolchain/SDK.chibi --core packages/core \
  --collection graph=tools/graph --entry CompilerTools --timeout-ms 120000 -- \
  --build "$PWD/build-release" --output "$PWD/dist/compiler-tools" \
  --third-party /absolute/path/to/the/pinned/archive
```

The installer rejects output or staging paths inside copied input directories,
including symlink, dot and case aliases, before creating directories. It also
rejects invalid directory ancestors, missing or stale graph compiler/core resources and an
archive that differs from the executable link records. It reserves the sibling
`OUTPUT.chibi-stage`, copies inputs, checks compiler recovery using the staged
runtime and resources, and writes `share/chibi/compiler-tools.tsv`. That manifest
contains relative file paths and their SHA-256 hashes. Publication atomically
renames the stage and refuses every existing destination. Normal preparation
failures remove only the stage this invocation reserved. A hard kill or fatal
host error can leave that stage for inspection; a later invocation refuses to
claim it. Use a fresh prefix when installing a new generation.

The prefix can move. Build programs use the installed host with explicit source
and collection paths, while the host discovers its compiler resources relative
to its executable. For the installed graph, pass
`--source PREFIX/share/chibi/tools/toolchain/Graph.chibi`,
`--core PREFIX/share/chibi/core`, and
`--collection graph=PREFIX/share/chibi/graph`.

`chibi build test graph-sdk-install` runs `test/graph/SDKInstallTests.chibi`
against the Release graph tools, so build them first:

```sh
chibi build --release --target chibi-build-host --target chibi-compiler-host
chibi build test graph-sdk-install
```

It reads the dependency archive's location from the Release build host's link
record. The cases check what `CompilerTools` refuses before staging, the
staging it owns, and an installed prefix that still recovers its compiler,
runs a program and writes graph commands after it moves, with an empty tool
search path. The graph's `--profile explicit` disables discovery and requires
its compiler, archiver and ranlib to be named; an omitted launcher remains
empty. This checkpoint has no Linux/Windows packaging, full SDK, browser or
Studio acceptance claim.

The standalone SDK program adds `build.reserve_directory` and
`build.publish_directory`. These private bindings reserve one fresh directory
and publish without replacement on macOS and Linux. These bindings do not change
normal `Build.chibi` admission; its protocol gate follows the compiler bootstrap
and build-program requirements above. Use the matching newly built host to run
`SDK.chibi`.
