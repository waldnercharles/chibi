# Toolchain

`chibi` is the one command a Chibi project needs. It runs from an installed
SDK, `PREFIX/bin/chibi`, or from a checkout's build, `build/sdk/chibi`, and
finds the compiler, the player and the engine files beside itself.

| Command | What it does |
| --- | --- |
| `chibi version` | Prints the SDK version, build, compiler, project format and artifact formats. |
| `chibi doctor [--web PLAYER]` | Reports whether the SDK, compiler, player, platform, native backend and, with `--web`, browser export are supported, unavailable or broken. It changes nothing. |
| `chibi build [PROJECT]` | Compiles the project into `.chibi/build/game.chibip` without starting the player. |
| `chibi check [PROJECT]` | Parses, resolves and validates the project as `build` does, but migrates and writes nothing. |
| `chibi fmt [--check] [PATH...]` | Rewrites a project's sources and KDL assets, or the package sources of a folder without a `project.kdl`, in their canonical form; `--check` only reports what would change. |
| `chibi test [ROOT]` | Runs the `@test` functions of the packages below ROOT. |
| `chibi run [PROJECT] [--] [PLAYER OPTIONS]` | Builds the project when it changed, then plays it. |
| `chibi export [PROJECT] --output DIR` | Assembles a game to ship: a macOS app, a Windows folder with an `.exe`, or a Linux folder with a desktop entry. `--web PLAYER` writes a static site instead. |
| `chibi web [PROJECT] --player PLAYER` | Exports the browser site into a temporary folder and serves it on `http://127.0.0.1:8000/` until Ctrl+C. |
| `chibi doc [ROOT] [--serve]` | Writes the documentation site that `ROOT/docs/site.kdl` lists into `ROOT/build/docs`, and with `--serve` serves it. |

PROJECT is a project directory or its `project.kdl`. Without one, `chibi` uses
the nearest `project.kdl` at or above the working directory. Each command
prints its usage with `--help` and exits 0 when it succeeds. It exits 2 when it
cannot start, such as for an unknown option or a missing or older project, and
another nonzero status when its work fails.

## A project

A project is a folder with a `project.kdl` manifest and a `content` folder:

```kdl
version 25
name "My Game"
content "content"
start_scene "scenes/Main.kdl"
```

Behaviors, libraries and systems are `.chibi` source; scenes, prototypes,
materials and the rest are KDL. `chibi fmt` keeps both canonical, and
`chibi check` is a quick gate for a pre-commit hook or CI.

## Packages

Packages are folders of `.chibi` files that start with `package NAME`. They
import other packages of the same tree as `project:NAME` and the SDK's
collection as `core:NAME`. `chibi test` runs their tests; see
[Testing](testing.md). `chibi fmt FOLDER` formats them.

## Browsers

The browser player is built separately with `make package-web-player`. Pass
its folder to `chibi export --web` for a static site to host anywhere, or to
`chibi web` while developing. `chibi doctor --web PLAYER` checks the folder and
the `node` that runs its compiler.

## Platforms

Linux and macOS support every command. On Windows, `chibi web` does not serve
yet, and `chibi test` and `chibi doc` cannot read package sources yet.
