# Chibi SDK

The SDK builds and installs the engine, executable runtime, compiler, and shared
authoring packages. Studio is a separate repository that consumes these public
targets. The deployed player accepts prepared projects; the compiler and
controller prepare/inspect authored content before deployment.

`sdk/VERSION` owns the SDK version. The Chibi graph's build identity and retained
CMake SDK/config-version generation read that file. Changing it invalidates
CMake configuration and updates the graph identity on the next build.

The Make convenience profiles are complete native products:

```sh
make package-sdk SDK_PREFIX=/absolute/path/to/chibi-sdk
make package-runtime-sdk RUNTIME_SDK_PREFIX=/absolute/path/to/runtime-sdk
make package-player PLAYER_OUTPUT=/absolute/path/to/player
```

`package-sdk` enables compiler, native capabilities and desktop rendering; it
includes `chibi-compile`, `chibi-player`, and the authoring interfaces Studio needs.
`package-runtime-sdk` enables native/desktop runtime with compiler disabled; its
only product executable is `chibi-player`. `package-player` derives a deployment
containing that executable, resources, licenses and exact SDK pin. It carries no
SDK headers/libraries or authoring tools. Native profiles construct the
`tools/reflect` generator with the Chibi graph on Linux/macOS; matching libclang
headers belong to that host tool. Private host recovery stays inside
`SDK_REFLECT_BUILD_DIR`, alongside its `reflect` executable. Windows retains the
restricted standalone CMake construction until its authored replacement passes.
`SDK_CMAKE_FLAGS` selects Debug by default or an explicit Release
profile. `package-player` requests Release.

For a library-only compiler or headless runtime, configure explicitly:

```sh
cmake -S sdk -B build-headless-sdk -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCHIBI_SDK_BUILD_COMPILER=ON \
  -DCHIBI_SDK_BUILD_NATIVE=OFF -DCHIBI_SDK_BUILD_DESKTOP=OFF
cmake --build build-headless-sdk --parallel
python3 tools/package_sdk.py --build build-headless-sdk --kind sdk \
  --output /absolute/path/to/headless-sdk
```

Set compiler OFF and package kind `runtime` for the corresponding parser-free
headless installation. Optional native capabilities, rendering and UI are separate
packages; a behavior runtime consumer does not link them automatically. Building
or packaging does not establish complete gameplay, GUI or GPU acceptance.

Consumers use the installed CMake targets:

```cmake
find_package(Chibi REQUIRED CONFIG)
target_link_libraries(my_target PRIVATE Chibi::types)
```

Set `CMAKE_PREFIX_PATH` to the installation. `Chibi::support` provides arenas and
allocation; `Chibi::ecs` provides entity storage, queries, structural commands and
execution views without graph dependencies; `Chibi::execution_contract` provides
instruction/host/status contracts.
`Chibi::asset_values` owns document values and their storage. `Chibi::value_document`
converts reflected values to owned documents and reads documents into caller-owned
arenas. Failed reads leave the output value unchanged. Both are optional dependencies for
consumers; execution and ECS do not depend on either target.
`Chibi::capabilities` owns declaration identities, types, signatures and effects.
It is available without the language parser. `Chibi::native_capabilities` joins
those declarations to host, access, command and query descriptors and validates
their declaration/layout ABI. The compiler's native binding adapter resolves this
table by stable identity. Prepared linkage checks the recorded declaration ABI.
`Chibi::core_capabilities`, `Chibi::array_capabilities`,
`Chibi::field_capabilities` and `Chibi::state_capabilities` supply shared native
declarations and their execution implementations. Their signatures are also used
by graph adapters, so argument names, defaults and types have one owner.
`Chibi::comparison_capabilities`, `Chibi::conversion_capabilities`, and
`Chibi::text_capabilities` own numeric comparisons, conversions and bounded text
formatting. Owned results use the caller's arena; authored format modifiers are
validated before calling printf. `Chibi::types` supplies shared value display.
`Chibi::graph_model` is a compiler-enabled authoring package for graph storage,
topology and value ownership. It owns an opaque compiled artifact through an
explicit destructor and has no compiler or graph executor dependency. Descriptor
editing belongs to the separate graph authoring package.
`Chibi::studio_capabilities` is an optional compiler-enabled package for Studio's
typed service calls. Studio supplies the service implementation; the package
contains no editor UI or graph objects.
`Chibi::asset_text` parses and writes KDL documents during asset preparation and
authoring. Its parser implementation and headers are private. This target is
included only in the compiler-enabled SDK.
`Chibi::language_syntax` owns incremental parse snapshots. `Chibi::language`
resolves source against a supplied declaration catalog. `Chibi::compiler` lowers
that resolved source to the executable representation. `Chibi::execution` owns
the shared instruction implementation and backend code. `Chibi::behavior_runtime`
owns module registration, entity attachments, event dispatch and update boundaries.
It uses the same implementation as the game runtime and links only execution and
its ECS/type dependencies. It does not link the compiler, parser, Studio or graphics.
`Chibi::scene_runtime` owns prepared scene and prototype generations, component
registration, entity creation and deferred behavior attachment. `Chibi::scheduling` consumes
copied access/order declarations and a borrowed worker executor; NULL runs serially.
`Chibi::fields` validates and evaluates scalar field artifacts and loads their
bounded prepared records. `Chibi::field_catalog` owns named field generations and
revision-bound baking sessions with typed parameters. Text parameter parsing
belongs to asset preparation. `Chibi::tasks` supplies the shared worker-executor
contract. These packages contain no graph or Studio dependencies.
`Chibi::authoring` also owns canonical navigation, rename plans, highlights,
completion and hover results shared by source consumers.
`Chibi::component_presentation` reads reflected field labels, ranges and editor
grouping from KDL without depending on prototypes, graph objects or Studio.
`Chibi::project_document` owns project manifest creation, loading and scene-path
resolution. Both are optional compiler-enabled packages.
`Chibi::reference_index` owns project dependency/reference facts and reachability
queries. Native graph and UI adapters add facts through its public interface.
`Chibi::prototype_document` reads and writes prototype component values. Native
component creation defaults are supplied explicitly by an authoring adapter.
`Chibi::scene_document` owns scene composition documents and serializes component
overrides using an explicit borrowed component catalog.
`Chibi::text_source` owns text edits, search and file reads with unsaved overlays.
`Chibi::ui_document` owns RML documents, widget expansion, event ranges and RCSS
expansion with source maps. `Chibi::ui_authoring` provides UI source parsing,
formatting, edits, completion, references and hover. Its `Chibi::ui_syntax`
dependency supplies the pinned HTML/CSS grammars and shares the syntax runtime.
These document and UI packages are compiler-enabled; rendering consumes their
owned results through a separate host.
`Chibi::authoring` prepares an owned source generation and resolves imported
library generations without attaching or running anything. Live-world publication
is a separate adapter. Configure the SDK with `-DCHIBI_SDK_BUILD_COMPILER=OFF`
to omit language, parser, compiler and authoring packages entirely. The runtime
package can then load prepared modules with explicit capability and type linkage.

Set `CHIBI_SDK_BUILD_NATIVE=ON` to include the engine's built-in component layouts
and the entity, audio and vector packages. Standalone native builds require
`CHIBI_REFLECT_EXECUTABLE`, a native host reflection generator. When its libclang
needs an explicit builtin-header directory, supply
`CHIBI_REFLECT_RESOURCE_INCLUDE_DIR`. Cute math headers come from the
third-party archive's Cute sources, without its renderer.
`Chibi::entity_runtime` owns entity commands, retained prototype generations and
the source execution adapter. These optional packages do not require Studio or
the language parser. The complete player and the remaining native capabilities
are still being extracted.

The final application supplies `type_catalog_pack_count` and
`type_catalog_pack_at` with its immutable ordered descriptor packs. Descriptor
storage must outlive all values and programs that resolve against it.

The SDK compiles using each target's own exported headers, its declared dependency
headers and its private headers, even when built within the product. Installed
headers live under `include/chibi/<target>`; the CMake target supplies that search
path, so consumers keep includes such as `types/value.h`. No shared header root
exposes unrelated modules. Targets do not receive the product's source directory.
The same type contract cases in `test/tools/types.c` and `test/tools/ecs.c` also compile against only
SDK headers and pico_unit. To build that consumer against an installed package:

```sh
cmake -S test/tools -B build-verify/sdk-consumer \
  -DCHIBI_INSTALLED_SDK=ON -DCMAKE_PREFIX_PATH=/absolute/path/to/chibi-sdk
cmake --build build-verify/sdk-consumer
CHIBI_TOOLS_BUILD_DIR=build-verify/sdk-consumer \
  python3 tools/test_policy.py tool native-tools
```

The last command uses the existing production test supervisor. Report
configuration and compilation time separately from test execution.

`test/language` also supports `CHIBI_INSTALLED_SDK=ON`. Its `--headless` case
prepares source with the compiler and hands the module to a separately compiled
consumer linked only to `Chibi::behavior_runtime`. The consumer retires the
producer reference and verifies attachment, events, bindings and entity cleanup.
Run it through `python3 tools/test_policy.py tool language-headless`, setting
`CHIBI_LANGUAGE_BUILD_DIR` to the configured consumer directory.

`make package-sdk` builds and packages the complete SDK into `SDK_PREFIX`.
`make package-runtime-sdk` packages the compiler-free runtime into
`RUNTIME_SDK_PREFIX` (default `dist/runtime-sdk`) using `RUNTIME_SDK_BUILD_DIR`
(default `build-runtime-sdk`). Use
`SDK_CMAKE_FLAGS="-DCMAKE_BUILD_TYPE=Release -DCHIBI_ENABLE_SANITIZERS=OFF"`
for a release configuration. The iteration default is Debug with sanitizers off.

The compiler SDK installs its packaging tools under `share/chibi/tools`. Installed
tools leave the SDK file inventory unchanged.

For browser Studio, `make package-web-sdk` configures the same packages with
Emscripten and SLJIT disabled. Browser behavior execution defaults to the Wasm
JIT, which emits and compiles script functions during program publication. A
compile error rejects the new program. BYTECODE remains an explicit diagnostic
backend. This SDK is library-only even when compiler,
native capabilities and desktop renderer are enabled. Its manifest records the
Emscripten platform and exact web worker configuration. Serial/threaded profiles
have separate default install prefixes. External Studio consumes the installed
`Chibi_WEB_THREADS`/`Chibi_WEB_WORKERS` ABI and produces the browser application;
there is no root combined application or native CLI in the web SDK. Browser
application acceptance is separate from the CPU Wasm checks described in
`docs/testing.md`.

Packaging installs into a fresh staging directory and verifies the exported
CMake target closure before replacing a previous managed package. Runtime
packages export support, types, document values, capability declarations/bindings,
ECS, execution, events, behavior/scene
runtime, scheduling, scalar fields and named field catalogs, with SLJIT
when enabled. They contain no language, parser, compiler or authoring headers,
archives or exported dependencies. SDK packages include those producer modules.
Both kinds include dependency license notices and accept `--archive` for a tar
archive outside the installed directory.

`share/chibi/package.json` records the explicit package kind, package version,
content build ID, SHA-256 checksum and size of each installed file, source
revision and snapshot identity, compiled input checksums, compiler configuration,
target architecture, public and shared runtime header ABI identities, and project/prepared format
requirements. Compiler dependency files must exist, and an input newer than its
object blocks packaging until rebuilt. Archives must be newer than every owned
object; object hashes and normalized compile-command hashes record target flags.
The source snapshot describes the tree
when packaged; the compiled input identity describes the object dependencies.
The manifest excludes itself from its content checksum table.

`runtime_headers_sha256` compares the shared header interface across SDK and
runtime kinds. It does not guarantee native host capability ABI compatibility;
those declarations and optional capabilities still require explicit linkage.

A game's toolchain pin should retain the manifest's `kind` and complete
`build_id`. Its build consumes `find_package(Chibi CONFIG REQUIRED)` through the
installed prefix. Release/runtime consumers link `Chibi::behavior_runtime` and
load prepared byte records through `execution_prepared_read` or
`package_load`. Compiler and authoring consumers link the SDK targets while
preparing those records. Native capability and type catalogs supply stable IDs
and declaration/layout ABIs; the loader rejects incompatible linkage.

Prepared files contain canonical executable definitions and owned values.
The loader regenerates backend code and retains imported immutable modules.
Opaque host handles and unreflected struct payloads cannot be persisted; such
values need an explicit portable capability contract. Runtime type descriptor
packs remain part of the final application and must outlive their values.

## Linux desktop dependency validation

When changing Linux dependency discovery, s2n-tls in the third-party archive, or
desktop SDK ownership/relocation, build the tooling dispatcher and run this
focused check on Linux before publishing:

```sh
make tools-build
CHIBI_TOOLS_BUILD_DIR=build-tools \
  python3 tools/test_policy.py tool desktop-dependencies
```

Set `CHIBI_TOOLS_BUILD_DIR` if the dispatcher was built outside `build-tools`.
This is an explicit standalone gate; default cross-platform tooling does not run
it automatically. Its six pico cases run eighteen commands on Linux, each with
the existing 10-second child timeout.
External tooling configuration/build is separate; all in-test CMake configuration,
compiler detection and tiny fixture compilation remain inside those child timeouts.

The fixture checks newly discovered crypto target visibility and relocated static
archive/header ownership, unchanged caller-provided targets, rejection of
mismatched OpenSSL metadata, and exact FindOpenGL role remapping with rejection
of an unowned same-basename path.

Synthetic archives and deterministic discovery modules establish CMake metadata
and relocation behavior. None of the cases builds a dependency or executes a
fixture program. Also build and package the actual intended SDK configuration and
verify a relocated installed consumer when those contracts change. Record its real
source revision, provider selection, and configured desktop backend set. These focused checks do not by
themselves establish Fedora-host, cryptographic, application, or GPU acceptance.

## Ninja package ownership validation

When changing SDK package provenance, configured target outputs, or installed
archive ownership, also run this explicit Linux standalone check:

```sh
CHIBI_TOOLS_BUILD_DIR=build-tools \
  python3 tools/test_policy.py tool package-ownership
```

The one pico case has three sequential blocks and up to 15 child commands,
each with the existing 10-second timeout. It builds
six tiny C translation units and one precompiled header with CMake/Ninja, installs
the fixture, and checks production packaging metadata. It never executes the
produced programs. The positive check covers renamed/relocated archives, folded
OBJECT and STATIC targets, an installed auxiliary archive, an auxiliary executable
and the exact product executable. A host C compiler produces a `.res`-named output
to check its real link ownership and declared manifest dependency hash. Eleven
fixed rejection checks cover missing and stale objects, validation-only object or
resource edges, order-only resource or PCH edges, a stale declared resource payload,
an unrecorded linked resource output, duplicate configured target records, and
missing or corrupt installed owners. This resource coverage is native-independent;
native Windows resource compilation and SDK execution are untested by this check.

Packaged SDK executables are linked with their selected `INSTALL_RPATH` so CMake
installation preserves their bytes. Only the SDK product targets receive this
setting; auxiliary and third-party targets retain their own build behavior. The
packager continues to require exact installed-versus-built executable hashes. The
ownership fixture selects distinct build and portable installation paths and
checks that both the chosen install path and executable identity are preserved.

For single-configuration Ninja builds, SDK packaging records configured target
types and output paths separately from compiling targets. Actual explicit/implicit
Ninja input edges prove object ownership; scheduling edges do not. PCH metadata
must lead to a current linked object. Uninstalled folded archives retain their
provenance and must have all their objects covered by verified installed owners.
Auxiliary executables retain build evidence without becoming package products.
Other generators retain their existing packaging behavior; this check does not
establish new multi-configuration or non-Ninja support.

Keep the real SDK package and relocated-consumer validation described above in
addition to this small fixture. Record the exact compiler, configuration and
source revision rather than transferring validation from another build.
