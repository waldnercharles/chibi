# FAQ

## Where do I start?

[Install the SDK](../../README.md#install), then read the
[language guide](../code-language.md). Use the [examples](../examples.md) to try
common game programming tasks.

## Do I need Studio?

No. Edit project files with any text editor:

```sh
chibi fmt /path/to/game
chibi check /path/to/game
chibi run /path/to/game
```

Studio edits the same files.

## How do I check my changes?

For a game project:

```sh
chibi check /path/to/game
chibi fmt --check /path/to/game
```

For packages:

```sh
chibi test /path/to/packages
```

Each command returns a nonzero status when it finds a problem.

## Can I ship to the web?

Yes. `chibi export --web PLAYER` creates a static site. See
[Export for browsers](toolchain.md#export-for-browsers) for the build commands.

## Why does a test report "Expected package header"?

`chibi test` reads package source. Each file must start with `package NAME`.
Give it your package folder, rather than the content folder of a game.
Game behaviors and libraries use a different source format.

## Can a test loop forever?

Each test stops after 100 million instructions. The command reports the test
as failed. See [Testing](testing.md#test-limits) for other limits.
