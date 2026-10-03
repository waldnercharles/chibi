# Native engine packages

`make package-native` builds an optimized static application, the automation
controller, Studio resources, and the MCP adapter into `dist/chibi-darwin-arm64`
on an Apple Silicon Mac. The directory name follows the build machine's OS and
architecture. Set `PACKAGE_OUTPUT` to choose its location. A game author needs
the package and Node.js 22 or newer for MCP; CMake, a C compiler, and the engine
checkout are build-time dependencies only.

The package contains two native executables, `bin/chibi` and `bin/chibi_ctl`,
plus `bin/chibi-mcp`. Application libraries and fonts are linked into the
executables. Shaders, Studio's authored assets, UI documents, and JavaScript
MCP modules live in `share/chibi`. OS libraries still come from the host.
Keep the whole package together when moving or distributing it.

## Use a separate game workspace

A game repository contains `project.kdl` and its `content` directory. For
example, `project.kdl` can select an empty starting scene:

```kdl
version 4
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

Then register the installed engine with that workspace:

```sh
/path/to/chibi/bin/chibi-mcp --setup /path/to/my-game
/path/to/chibi/bin/chibi --project-root /path/to/my-game
```

Setup preserves other Codex settings and MCP servers. It writes the Chibi
registration to `.codex/config.toml` and the engine identity to
`.chibi-engine.json`. Commit the pin with the game; keep machine-specific Codex
configuration local. The packaged MCP refuses a different engine identity
until it is explicitly selected with:

```sh
/path/to/new-chibi/bin/chibi-mcp --setup /path/to/my-game --update-engine
```

An engine pin does not migrate assets between project format versions. The
game's `project.kdl` must use the version accepted by the selected engine.

Use MCP discovery and search before authoring. Change assets with the matching
`build*` tool, validate, then preview or run a deterministic scenario. The
installed [MCP guide](mcp.md) describes the tools and expected workflow. The
native editor opens the same assets. Run the game with:

```sh
/path/to/chibi/bin/chibi --play --project-root /path/to/my-game
```

The MCP process serves the compiled engine's catalog and invokes the packaged
controller. It needs no npm install or engine build and never rebuilds a
missing controller. It loads game assets from the selected workspace. Its
JavaScript adapter is shipped separately from the native executable.

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

The tracked `projects/arena2d` sample keeps clean engine checkouts and CI
self-contained. To author an external game in the engine's Studio or local
browser editor, optionally mount it at the ignored `projects/local-game` path:

```sh
ln -s /absolute/path/to/game projects/local-game
make run PROJECT=projects/local-game
make web-serve PROJECT=projects/local-game
```

The external game owns its `project.kdl` and `content/`. Studio's project list
and the local browser editor follow the mount; editor saves update the game's
files. Select it explicitly for source MCP with `CHIBI_ROOT`, or use the
copied runtime and MCP launcher directly from the game workspace.

Ignored mounts are local authoring conveniences and are excluded from clean
CI and static web publishing. If a project link is tracked in a future engine
commit, `make publish-web` requires its target Git checkout and archives only
its committed `HEAD`; CI must also provide that project. `BUILD_SOURCES.json`
records the selected engine and external project commits.

## Deliver runtime updates to local games

Register game workspaces in the editor checkout's ignored
`.chibi/game-runtime.json`:

```json
{
  "games": ["/path/to/neon-swarm"]
}
```

Launch commands such as `make run`, `make play`, and `make dev` build a
distribution package and deliver it to registered games before starting.
`make update-game-runtime` refreshes registered games explicitly, and
`make package-native` delivers its package as well. Set `UPDATE_GAME_RUNTIME=0`
to skip launch-time delivery. Ordinary build and test commands do not create
a second packaging profile or update external game runtimes.

Delivery verifies the complete package and each game's project version, then
stages a full copy inside each game's `.chibi` directory. It replaces
`.chibi/runtime` with regular files, points MCP at the copied launcher, and
refreshes `.chibi-engine.json`. A failed copy leaves the active runtime alone;
a failed activation restores the previous runtimes and metadata. Runtime
delivery creates no symlinks or hardlinks to external packages. New game and
MCP launches use the update; restart existing sessions after delivery.

For registered games, `make publish-web` also builds a native package from the
same isolated `origin/main` checkout. It delivers that package after the live
web bundle passes verification. Game workspaces need only their local binary
interface and MCP catalog.

## Build and verify

```sh
make package-native
make test-package
python3 tools/package_native.py --build build-package \
  --output dist/chibi-darwin-arm64 --archive dist/chibi-darwin-arm64.tar.gz
```

`make test-package` moves a copy into a path containing spaces, starts it from
an unrelated directory, checks native dependencies and resource discovery,
and uses real MCP asset edits, validation, deterministic runs, and runtime
snapshots in an external game workspace. Build commands are blocked during
these checks. `make test` includes the packaging failure and MCP setup tests.

`share/chibi/engine.json` records SHA-256 hashes of the payload, a build ID
derived from those hashes, the project version, host platform, and source
revision. The ID includes resources and the MCP adapter as well as binaries.
Distribution builds have no resource fallback to the engine checkout.
`CHIBI_RESOURCE_DIR` can explicitly override the resource directory;
`CHIBI_RENDER_SHADER_DIR` can override only the shaders. Development and web
builds retain their existing resource layouts.
