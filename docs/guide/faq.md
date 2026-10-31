# FAQ

## Where do I start?

[Install the SDK](../../README.md#install), then read the
[language guide](../code-language.md). Use the [examples](../examples.md) to try
common programming tasks.

## Do I need Studio?

No. Edit `.chibi` files with any text editor and run them:

```sh
chibi run /path/to/program
```

Game projects are archived for now; see
[Game projects](toolchain.md#game-projects).

## How do I check my changes?

```sh
chibi test /path/to/packages
```

The command returns a nonzero status when a test fails.

## Can I ship to the web?

Not now. `chibi export` and `chibi web` are archived with the player; see
[Export a game](toolchain.md#export-a-game).

## Why does a test report "Expected package header"?

`chibi test` reads package source. Each file must start with `package NAME`.
Give it your package folder, rather than the content folder of a game.
Game behaviors and libraries use a different source format.

## Can a test loop forever?

Each test stops after 100 million instructions. The command reports the test
as failed. See [Testing](testing.md#test-limits) for other limits.
