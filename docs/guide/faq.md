# FAQ

## What is Chibi?

Chibi is a C23 scripting language, compiler and runtime for games. It has a
separate editor, Studio. A game is a folder of `.chibi` source and KDL assets.
The `chibi` command compiles, runs, tests and exports the game.

## Where do I start?

1. Build the SDK.
2. Use `chibi run` to run one of the projects in [Examples](../examples.md).

The [language guide](../code-language.md) describes the source.
[Toolchain](toolchain.md) describes each command.

## Do I need Studio?

No. Each asset is a text file that you can edit directly. `chibi fmt` keeps the
files canonical. Studio edits the same files.

## How do I check a project before I commit it?

- `chibi check` validates the project and writes nothing.
- `chibi fmt --check` reports the assets that are not canonical.
- `chibi test` runs the package tests.

Each command exits with a nonzero status when it finds a problem.

## Can I ship to the web?

Yes. `chibi export --web PLAYER` writes a static site for any host. While you
develop, `chibi web` serves a site.

## Why does `chibi test` say "Expected package header"?

The ROOT of `chibi test` must contain only package files. Give it the folder of
your packages. Do not give it a game project. The behaviors and libraries of a
game project are not packages.

## Does a slow test hang forever?

No. Each test stops after 100 million instructions. `chibi test` reports it as
failed.
