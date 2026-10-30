# Native product packages

The installed SDK, Studio, and deployed player are separate products. The player
package contains the native `chibi-player`, shaders, licenses, engine identity,
and a manifest pinning its exact runtime SDK. It accepts prepared project
artifacts and their declared resources.

## SDK and application ownership

The engine repository owns the language, compiler, execution, runtime, shared
authoring services and native tools. Studio owns its application loop, editor
UI, starter project and authored shell assets in the separate `chibi-studio`
repository. Games have their own repositories. Engine tests retain focused
fixtures, listed in [examples](examples.md). Product packaging does not discover
or ship `projects/*`.

Project format 10 uses `.chibi` as complete executable source for behaviors,
callable libraries and systems. Declarative assets use KDL. The compiler resolves
source against explicit capability tables and produces verified prepared modules
and asset records. The player loads those records without syntax, compiler,
graph authoring or Studio dependencies. There is no MCP product or setup step.

```sh
make package-sdk SDK_PREFIX=dist/sdk
chibi build package-runtime-sdk RUNTIME_SDK_PREFIX=dist/runtime-sdk
make package-studio STUDIO_SOURCE=/absolute/chibi-studio STUDIO_SDK_PREFIX=/absolute/sdk
```

The compiler-enabled SDK supplies public headers, libraries, installed CMake
targets and `chibi-compile`. The compiler-disabled runtime SDK supplies public
runtime targets and the standalone player. It is an intermediate SDK for native
hosts and player packaging, rather than a game deployment. The player package
contains its executable and declared runtime resources; it excludes SDK headers
and archives. Studio packages its own executable and resources against the exact
SDK manifest captured when it was configured. The controller is a separate
public-SDK consumer built with `make controller-build`.

## Prepare and run a game

Use the game repository directly; no engine-checkout mount is required.

```sh
/installed/sdk/bin/chibi-compile --project-root /absolute/game --output /absolute/output/game.chibip
/installed/player/bin/chibi-player /absolute/output/game.chibip --resource-root /absolute/output
make studio-run STUDIO_SOURCE=/absolute/chibi-studio STUDIO_SDK_PREFIX=/absolute/sdk PROJECT_ROOT=/absolute/game
```

Retain the artifact's resource manifest and declared resources. Studio receives
an explicit project root; browser Studio receives an explicit sample list through
`WEB_PROJECTS`. The extracted browser build and rendered product acceptance are
tracked separately from package integrity and installed-consumer linking.

## Native libraries

A game can ship natively compiled libraries in `natives/` under its resource
root. Each `<Library>.so` (`.dylib` on macOS, `.dll` on Windows) is compiled
from a `<Library> :: library { ... }` source and exports its manifest as
`chibi_natives_<Library>`. The player loads them before the first scene and
binds each manifest to the library's module, so calls to its pure functions run
natively; tasks, unbound functions and Studio stay in the VM. A library that
does not load or bind stops the player with the reason. Web exports ship none.
`chibi export --native` builds them on Linux x86-64 (see the toolchain guide);
see W16 in `docs/native-llvm-ledger.md`.

## Window and icon

An optional `application` block in `project.kdl` sets how the shipped game's
window looks. Every field is optional: the title defaults to the project name,
the size to 1280×720, and the icon is a PNG path relative to the project root.

```kdl
application {
    title "My Game"
    width 1600
    height 900
    fullscreen #false
    icon "icon.png"
}
```

`chibi-compile` stores these settings in the artifact, checks that the icon is
a readable PNG, and lists it with the external resources in the artifact's
resource manifest. The player resolves the icon against its resource root. A
project name longer than 1024 bytes needs a shorter `title`.

## Deployment identity

`share/chibi/package.json` inventories the deployed files and pins the source
runtime SDK. The SDK manifest records configured target, format and compiled
source evidence. The game's `.chibi-engine.json` identifies its copied player
deployment. Use those records for capability requests; the controller's identity
describes its own authoring distribution and is not the game's runtime pin.

```sh
make package-player PLAYER_OUTPUT=dist/game-runtime
```

This builds a native reflection tool and an optimized compiler-free desktop
runtime SDK before deriving the player package. `RUNTIME_SDK_BUILD_DIR` and
`RUNTIME_SDK_PREFIX` select the intermediate build and installed SDK.
`SDK_REFLECT_BUILD_DIR` selects the host tool build.

Use a compiler-enabled installed SDK for `chibi-compile` and the separate Studio
package for authoring. See [runtime authoring](runtime-authoring.md) for preparation,
resource retention, and exact SDK pins. Packaging checks do not establish rendered
game acceptance; runtime execution and GPU checks need their affected-contract
evidence and required hardware.

The workflow below records the retired combined product and its earlier evidence.
Its commands and package layout do not describe the current deployment contract.

## Historical combined package workflow

All sections below preserve the earlier combined-checkout workflow. Its
`projects/arena2d` shipping, mount discovery, `chibi_ctl --engine-info` example,
launch-time deployment and publishing commands are historical and have no role
in the current product interfaces above.

`make package-native` builds an optimized static application, the automation
controller, and Studio resources into `dist/chibi-darwin-arm64` on an Apple
Silicon Mac. The directory follows the build machine's OS and architecture;
`PACKAGE_OUTPUT` selects another location. CMake, a C compiler, and the engine
checkout are build-time dependencies only.

The package contains `bin/chibi` and `bin/chibi_ctl`. Application libraries and
fonts are linked into the executables. Shaders, Studio's authored assets, and
UI documents live in `share/chibi`. OS libraries come from the host. Keep the
whole package together when moving or distributing it.

## Use a separate game workspace

A game repository contains `project.kdl` and its `content` directory. For
example, `project.kdl` can select an empty starting scene:

```kdl
version 21
name "My Game"
content "content"
start_scene "scenes/Main.kdl"
```

Create `content/scenes/Main.kdl`:

```kdl
version 1
name "Main"
(array)entities
```

Check the project, or build and run it, with the installed launcher:

```sh
/path/to/chibi/bin/chibi check /path/to/my-game
/path/to/chibi/bin/chibi run /path/to/my-game
```

Edit `.chibi` executable source and KDL declarative assets directly or through
Studio. Format and validate saved content through `bin/chibi_ctl`, then inspect
or run a bounded scenario. The [asset workflow](runtime-authoring.md) gives
native request examples. A project must use the selected engine's project
format version; selecting a package does not migrate authored assets.

## Request an engine capability

Game work owns scene composition, behavior graphs, shaders, rules, and tuning.
When the catalog cannot express a behavior, produce a small reproducing
project rather than changing engine C inside the game workspace. Include:

- The pinned engine build, obtainable from `bin/chibi_ctl --engine-info`.
- The smallest scene or prototype and scenario that demonstrate the gap.
- Expected behavior and the observed validation error, snapshot, or preview.
- The missing reusable operation and why existing nodes cannot express it.

Engine work implements that capability in the engine repository, validates the
reproduction and another scene with different content, then creates a new
package. Game work selects that package and resumes authoring. Examples and
reproductions can live in the game repository; engine implementation remains
in the engine repository. Neither requires a repository split of the existing
samples before this workflow can be used.

## Local external projects

Games now live in separate repositories. Engine regression inputs remain in
`test/fixtures`; no game project is bundled by default. Select the external
Studio repository and authored game explicitly:

```sh
make studio-run STUDIO_SOURCE=/absolute/path/to/chibi-studio PROJECT_ROOT=/absolute/path/to/game
make web-serve STUDIO_SOURCE=/absolute/path/to/chibi-studio WEB_OUTPUT=/absolute/path/to/studio-web
```

The external game owns its `project.kdl` and `content/`. Native Studio opens that
project directly. The browser workspace owns its imported project files. Use
`chibi_ctl --project-root` for native inspection and validation, or prepare an
artifact with `chibi-compile` and run it with the installed `chibi-player`.

Engine-owned web deployment is retired and its publisher removed; deployment
belongs to the extracted Studio product.
Engine CI compiles owned SDK/CLI targets and runs selected C tooling checks.
Historical browser publication fixtures do not establish current deployment.

## Build and verify

```sh
make package-player
make test-tools
```

`make test-tools` runs C checks for package/install preservation and rejection of
invalid build profiles. The former script-based relocation tests
are retired; the minimal suite does not establish their former coverage.

`share/chibi/engine.json` is the build identity document `chibi version` reads:
the SDK version, the platform and execution backends it was built for, the
project format and each prepared artifact format with its owning sources'
hashes, as configuring generates them, plus the build ID SDK packaging derives
from the compiled inputs and archives. `chibi doctor` compares the platform with
the host and reports the backends. The package manifest
records the payload's file hashes, platform and source revision.
Distribution builds have no resource fallback to the engine checkout.
`CHIBI_RESOURCE_DIR` can explicitly override the resource directory;
`CHIBI_RENDER_SHADER_DIR` can override only the shaders. Development and web
builds retain their existing resource layouts.
