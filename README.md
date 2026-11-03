# Chibi

Chibi is a language and runtime for games. Start with a `.chibi` program and
run it with `chibi`. Game projects add scenes, components and resources.

- [Language](docs/code-language.md): learn the syntax through code.
- [Examples](docs/examples.md): try common game programming tasks.
- [Toolchain](docs/guide/toolchain.md): run a program, test packages and build native programs.
- [Testing](docs/guide/testing.md): check package functions with `@test`.

## Install

The native compiler and CLI need LLVM/LLD 22 or newer. Set
`CHIBI_LLVM_BIN` to the directory containing `opt`, `llc`, `llvm-link`,
`llvm-ar` and the platform linker. Windows uses `llvm-lib` for archives. Run
these commands from the engine checkout:

```sh
sh tools/build.sh install
export PATH="$HOME/.local/bin:$PATH"
chibi doctor
```

The default install directory is `~/.local`. Use
`chibi build install --prefix /absolute/prefix` to select another directory.
The SDK includes the native `chibi` compiler and CLI, authored core and graph
packages, the native asset compiler, its compile-only C API archive and header,
and their resources. Installed tools find
these resources beside their prefix, so the prefix can move.

For a development build, use `sh tools/build.sh build`. Once `chibi` is installed,
`chibi build` inside the checkout does the same. The default products are the
native CLI/compiler, asset compiler and compile-only archive; `--target chibi` builds the CLI/compiler
alone. Run it as `build/sdk/chibi`. Explicit foreign C graph fixtures and C ABI
consumers use a C compiler; native products require LLVM and the verified seed.

The compiler and toolchain self-host natively. Programs and reload generations
use the native runtime; the optional browser compiler uses the authored Wasm
route. The compile-only prepared artifact encoder remains available. The
[native ledger](docs/native-llvm-ledger.md) records language admission and runtime
verification. Compiler reproduction alone does not establish runtime parity.

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

To keep a program running while you edit it, use `chibi --watch Hello.chibi`.
A watched program can provide `Main`, `Frame`, or both. Without either entry,
it waits for another save. The runner calls `Main` once if the first runnable
generation provides it, and calls `Frame` every frame when present. Each valid save patches
the running code, keeps `retain` state, and does not call `Main` again. Invalid
edits report diagnostics and keep the last valid code running. See
[program reload](docs/guide/toolchain.md#reload-a-program) for a complete example.

The CLI is compiled from
[tools/compiler/native_driver](tools/compiler/native_driver). It finds core and
graph packages beside the installed SDK, compiles programs natively, and runs
them. Command parsing and reload policy live in Chibi.

## Studio

Studio is maintained in its [own repository](https://github.com/waldnercharles/chibi-studio).
Its build, installation and browser instructions belong there. The compiler-tools
SDK described here does not package the former game player or runtime SDK.

## License

The standalone compiler and tools are GPLv3; the embeddable runtime, player,
and reusable libraries are LGPLv3. Both are version 3 only. Independent games
and applications may use their own license, including proprietary terms,
when the LGPL distribution conditions are met. Distributed changes to Chibi
remain covered by its applicable copyleft license. Third-party components
keep their existing licenses. Three explicitly listed [copy-paste examples](LICENSE.md#mit-copy-paste-examples)
are available under MIT. See the [file scope](LICENSE.md) and
[distribution guide](docs/distribution-licensing.md).
