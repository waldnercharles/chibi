# Chibi

Chibi is a language and runtime for games. Start with a `.chibi` program and
run it with `chibi`. Game projects add scenes, components and resources.

- [Language](docs/code-language.md): learn the syntax through code.
- [Examples](docs/examples.md): try common game programming tasks.
- [Toolchain](docs/guide/toolchain.md): run a program, or build and export a game.
- [Testing](docs/guide/testing.md): check package functions with `@test`.

## Install

Native builds need CMake, a C23 compiler and libclang for reflection.
Run these commands from the engine checkout:

```sh
chibi build install
export PATH="$HOME/.local/bin:$PATH"
chibi doctor
```

The default install directory is `~/.local`. Use `chibi build install --prefix /absolute/prefix`
to select another directory. The SDK includes the `chibi` command, compiler,
player, headers and libraries.

For a development build, use `tools/build.sh build` (once a `chibi` exists, `chibi build` inside the checkout does the same). Run the command as `build/sdk/chibi`. From a cold tree it first builds the Release hosts in `build-release` (about three minutes) so the asset compiler is generated quickly, then the Debug build; the whole run takes about ten minutes on an Apple M-series machine.

## Hello World

Save this as `Hello.chibi`:

```chibi
package hello

Main :: () {
    print_string("Hello, world!")
}
```

Run it:

```sh
chibi Hello.chibi
```

It prints `Hello, world!` and exits. `chibi run Hello.chibi` does the same thing.
The program runner calls `Main` once. It takes no arguments and can return
nothing, as above, or an `int` exit status from 0 to 255.

Every program source needs a `package` header. Running a file compiles that
file and the core library. To include sibling source files and local package
imports, run the package directory with `chibi .`.

To keep a program running while you edit it, add a `Frame :: ()` function and
use `chibi --watch Hello.chibi`. The runner calls `Main` once and `Frame` every
frame; each valid save patches the running code, keeps `retain` state, and does
not call `Main` again. Invalid edits report diagnostics and keep the last valid
code running. See [program reload](docs/guide/toolchain.md#reload-a-program)
for a complete example.

The CLI is itself an ordinary Chibi program in
[apps/chibi/Main.chibi](apps/chibi/Main.chibi). The native launcher locates the
SDK and starts its `Main`; command parsing and reload policy live in Chibi.

## Run a project

Give `chibi run` a project folder or its `project.kdl`:

```sh
chibi run /path/to/game
```

`chibi /path/to/game` also runs a game folder containing `project.kdl`.

Inside a project folder, omit the path:

```sh
chibi check
chibi run
chibi export --output /path/to/release
```

`run` builds changed content before it starts the player. `export` creates a
folder or app with the player and the game's resources. See
[Export a game](docs/guide/toolchain.md#export-a-game) for platform details.

## Edit with Studio

Studio edits the same project files. Install it from its separate checkout:

```sh
make -C /path/to/chibi-studio install
chibi-dev /path/to/game
```

You can also open [Studio in your browser](https://waldnercharles.github.io/chibi-studio/).
The browser version uses the latest engine release.

## License

The standalone compiler and tools are GPLv3; the embeddable runtime, player,
and reusable libraries are LGPLv3. Both are version 3 only. Independent games
and applications may use their own license, including proprietary terms,
when the LGPL distribution conditions are met. Distributed changes to Chibi
remain covered by its applicable copyleft license. Third-party components
keep their existing licenses. Three explicitly listed [copy-paste examples](LICENSE.md#mit-copy-paste-examples)
are available under MIT. See the [file scope](LICENSE.md) and
[distribution guide](docs/distribution-licensing.md).
