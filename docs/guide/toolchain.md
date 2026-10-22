# Toolchain

Run `chibi` from an installed SDK or use `build/sdk/chibi` in an engine checkout.
The command finds the compiler, player and engine resources beside itself.

## Build and run

PROJECT is a project folder or its `project.chibi`. Without a path, these
commands find the nearest `project.chibi` in or above the working directory.

```sh
chibi check /path/to/game
chibi build /path/to/game
chibi run /path/to/game
```

`check` checks the project without writing files. `build` writes
`.chibi/build/game.chibip`. `run` builds changed content and starts the player.
Pass player options after `--`. Use `chibi-player --help` to list them.

Current projects use version 33 compiled Chibi sources. Earlier document formats
require an offline source conversion; their historical parser and migration chain
are retired. Rebuild prepared artifacts after converting source contracts.

## Project files

A project has a `project.chibi` manifest and a content folder:

```chibi
package asset

import data "game:assets"

pub Build :: () -> byte[] {
    return data.Encode(
        data.Project("My Game", 33, "content", "scenes/Main.chibi", [])
    );
}
```

The start scene path is relative to the content folder. Behaviors, libraries
and systems use executable Chibi declarations. Scenes, prototypes and materials
use ordinary compiled modules with the same `.chibi` suffix.

## Format files

```sh
chibi fmt /path/to/game
chibi fmt --check /path/to/game
```

`fmt` rewrites sources and compiled Chibi assets in their canonical form. It keeps UI
markup unchanged. `--check` reports files that need formatting and writes nothing.

For a folder without `project.chibi`, `fmt` formats package sources instead.
Each source file must start with `package NAME`.

## Test packages

```sh
chibi test /path/to/packages
```

The folder contains package files, rather than game behaviors or libraries.
Tests import local packages with `project:NAME` and SDK packages with `core:NAME`.
See [Testing](testing.md) for a complete example.

## Export a game

Use an installed SDK to assemble a release:

```sh
chibi export /path/to/game --output /path/to/release
```

The output contains the player, compiled game and resources. It also contains
engine shaders, licenses and identity files. Add `--archive` to create a `.tar.gz`
archive beside the output.

**Export replaces an earlier export, including files you added to it.** The
exporter uses its `.chibi-export` marker to identify its output. It refuses
to replace an existing folder without that marker.

### macOS

The output is `/path/to/release.app`, with an `Info.plist` and an ad hoc signature.
A project icon must be a square PNG. Supported sizes are 16, 32, 64, 128, 256,
512 and 1024 pixels.

### Linux

The output folder contains an executable named after the project and a desktop
entry. To install the entry:

1. Create a symlink to the executable in a directory on `PATH`.
2. Install the project icon in your icon theme with the name from the entry's `Icon` field.
3. Copy the desktop entry to `~/.local/share/applications`.

### Windows

The output folder contains an `.exe` and a `data` folder. A project icon must
be a square PNG of at most 256 pixels. The executable embeds the icon.
The SDK links the release C runtime statically, so the export needs no Visual
C++ redistributable.

## Export for browsers

Build the browser player from the engine checkout. This step needs Emscripten.
Browser compilation also needs Node.js.

```sh
make package-web-player
chibi export /path/to/game --web dist/web-player --output /path/to/site
python3 -m http.server -d /path/to/site
```

Open `http://localhost:8000`. The site contains the browser player and the same
game resources as a native export. It fetches those resources before the game
starts. Use a web server or static host. A `file://` URL does not work.

For a temporary local preview:

```sh
chibi web /path/to/game --player /path/to/dist/web-player
```

`web` serves on `http://127.0.0.1:8000/` until you press Ctrl+C. It then removes
the temporary export. Use `--port NUMBER` to select a port. Port 0 selects a
free port.

## Check the SDK

```sh
chibi version
chibi doctor
chibi doctor --web /path/to/dist/web-player
```

`version` prints the SDK version, build identity, compiler and format versions.
`doctor` reports each checked capability as supported, unavailable or broken.
It changes nothing. It fails only when a checked capability is broken.

## Generate documentation

```sh
chibi doc /path/to/repository
chibi doc /path/to/repository --serve
```

`doc` publishes the pages listed in `docs/site.chibi` to `build/docs`. The manifest
can select Markdown, package references and grammar rules. Other files stay private.
`--serve` previews the result locally. It accepts the same `--port` option as `web`.

Each `chibi` code block must parse. A complete package must also pass analysis.
A link to an unpublished file becomes plain text and produces a warning.

## Exit status

Each command accepts `--help`.

- 0 means success.
- 2 means the command could not start, such as an unknown option or missing project.
- Another nonzero status means the command failed.

## Platform limits

Linux and macOS support all commands.

TODO: Windows support for reading package sources in `test` and `doc`.

TODO: Windows support for the local server in `web` and `doc --serve`.
