# Chibi

Chibi is a language and runtime for games. Write game behavior in `.chibi`
files. Define scenes, components and resources in text files. Run the game with
`chibi run`.

- [Language](docs/code-language.md): learn the syntax through code.
- [Examples](docs/examples.md): try common game programming tasks.
- [Toolchain](docs/guide/toolchain.md): build, run and export a project.
- [Testing](docs/guide/testing.md): check package functions with `@test`.

## Install

Native builds need CMake, a C23 compiler and libclang for reflection.
Run these commands from the engine checkout:

```sh
make install
export PATH="$HOME/.local/bin:$PATH"
chibi doctor
```

The default install directory is `~/.local`. Use `make install PREFIX=/absolute/prefix`
to select another directory. The SDK includes the `chibi` command, compiler,
player, headers and libraries.

For a development build, use `make build`. Run the command as `build/sdk/chibi`.

## Run a project

Give `chibi run` a project folder or its `project.kdl`:

```sh
chibi run /path/to/game
```

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
