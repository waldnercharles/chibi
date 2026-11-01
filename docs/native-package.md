# Native product packages

The installed SDK is the engine's one product package. The player, its package,
Studio packaging and the game-project compile path are archived until a Chibi
renderer replaces them (`archive/README.md`); the `package-sdk`,
`package-studio`, `package-player`, `package-native`, `studio-run` and
`web-serve` Make targets went with them and with the Makefile (e4aa7b4e2).

## SDK and application ownership

The engine repository owns the language, compiler, execution, runtime, shared
authoring services and native tools. Studio owns its application loop, editor
UI, starter project and authored shell assets in the separate `chibi-studio`
repository. Games have their own repositories. Engine tests retain focused
fixtures, listed in [examples](examples.md). Product packaging does not discover
or ship `projects/*`.

```sh
chibi build install --prefix ~/.local
```

`install` builds the Release `chibi`, `chibi-compile`, `chibi-build-host` and
`chibi-compiler-host` and installs `bin/chibi`, `bin/chibi-compile`, the
private hosts in `libexec/chibi` and the compiler, core and build resources in
`share/chibi` (`tools/toolchain/SDK.chibi`). `chibi build test
graph-sdk-install` checks the installation.

## Deployment identity

`share/chibi/engine.json` is the build identity document `chibi version` reads:
the SDK version, the platform and execution backends it was built for, the
project format and each prepared artifact format with its owning sources'
hashes, plus the build ID derived from the compiled inputs. `chibi doctor`
compares the platform with the host and reports the backends. Distribution
builds have no resource fallback to the engine checkout.
