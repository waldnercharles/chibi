# FAQ

## What is Chibi?

A C23 scripting language, compiler and runtime for games, with a separate
editor, Studio. Games are folders of `.chibi` source and KDL assets that the
`chibi` command compiles, runs, tests and exports.

## Where do I start?

Build the SDK, then run one of the projects listed under
[Examples](../examples.md) with `chibi run`. The [language guide](../code-language.md)
describes the source, and [Toolchain](toolchain.md) every command.

## Do I need Studio?

No. Every asset is a text file you can edit directly, and `chibi fmt` keeps
them canonical. Studio edits the same files.

## How do I check a project before committing?

`chibi check` validates it without writing anything, `chibi fmt --check`
reports assets that are not canonical, and `chibi test` runs package tests.
Each exits nonzero on a problem.

## Can I ship to the web?

Yes. `chibi export --web PLAYER` writes a static site for any host, and
`chibi web` serves one while you develop.

## Why does `chibi test` say "Expected package header"?

Its ROOT must hold only package files. Point it at the folder of your
packages, not at a game project, whose behaviors and libraries are not
packages.

## Does a slow test hang forever?

No. Each test stops after 100 million instructions and is reported as
failed.
