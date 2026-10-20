# Toolchain

`chibi` is the one command that a Chibi project needs. You can run it from an
installed SDK, as `PREFIX/bin/chibi`. You can also run it from the build of a
checkout, as `build/sdk/chibi`. It finds the compiler, the player and the
engine files beside itself.

| Command | What it does |
| --- | --- |
| `chibi version` | Prints the SDK version, the build, the compiler, the project format and the artifact formats. |
| `chibi doctor [--web PLAYER]` | Reports the status of the SDK, the compiler, the player, the platform and the native backend. With `--web`, it also reports the status of browser export. Each status is supported, unavailable or broken. The command changes nothing. |
| `chibi build [PROJECT]` | Compiles the project into `.chibi/build/game.chibip`. It does not start the player. |
| `chibi check [PROJECT]` | Parses, resolves and validates the project in the same way as `build`. It does not migrate or write anything. |
| `chibi fmt [--check] [PATH...]` | Rewrites the sources and KDL assets of a project in their canonical form. For a folder without a `project.kdl`, it formats the package sources. With `--check`, it only reports the changes that it would make. |
| `chibi test [ROOT]` | Runs the `@test` functions of the packages below ROOT. |
| `chibi run [PROJECT] [--] [PLAYER OPTIONS]` | Builds the project if it changed, then plays it. |
| `chibi export [PROJECT] --output DIR` | Assembles a game that you can ship. On macOS, the result is an app. On Windows, it is a folder with an `.exe`. On Linux, it is a folder with a desktop entry. With `--web PLAYER`, the command writes a static site instead. |
| `chibi web [PROJECT] --player PLAYER` | Exports the browser site into a temporary folder. Then it serves the site on `http://127.0.0.1:8000/` until you press Ctrl+C. |
| `chibi doc [ROOT] [--serve]` | Writes the documentation site that `ROOT/docs/site.kdl` lists into `ROOT/build/docs`. With `--serve`, it also serves the site. |

PROJECT is a project directory or its `project.kdl`. If you do not give a
PROJECT, `chibi` uses the nearest `project.kdl` in or above the working
directory.

Each command prints its usage when you give `--help`. The exit status tells you
the result:

- 0: the command succeeded.
- 2: the command could not start. Examples are an unknown option, a missing
  project and an older project.
- A different nonzero status: the work of the command failed.

## A project

A project is a folder with a `project.kdl` manifest and a `content` folder:

```kdl
version 25
name "My Game"
content "content"
start_scene "scenes/Main.kdl"
```

Behaviors, libraries and systems are `.chibi` source. Scenes, prototypes,
materials and the other assets are KDL. `chibi fmt` keeps both kinds canonical.
`chibi check` is a quick gate for a pre-commit hook or for CI.

## Packages

A package is a folder of `.chibi` files that start with `package NAME`. A
package imports other packages of the same tree as `project:NAME`. It imports
the collection of the SDK as `core:NAME`. `chibi test` runs the tests of the
packages. Refer to [Testing](testing.md). `chibi fmt FOLDER` formats the
packages.

## Browsers

`make package-web-player` builds the browser player separately. Use its folder
in these ways:

- Give it to `chibi export --web` to make a static site that you can host
  anywhere.
- Give it to `chibi web` while you develop.

`chibi doctor --web PLAYER` checks the folder and the `node` that runs its
compiler.

## Platforms

Linux and macOS support all commands. On Windows, these limits apply:

- `chibi web` does not serve yet.
- `chibi test` and `chibi doc` cannot read package sources yet.
