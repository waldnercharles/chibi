# Native modules on macOS and Windows

`language-scalar-native` (`--package-compiler-chibi-native`) runs every
native scalar module on Linux x86-64. When `CHIBI_NATIVE_BUNDLE` names a
directory, it also emits each module it runs for the other targets
`native_abi` admits and builds them into that directory:

- each module's IR comes from `scalar_llvm` (or `llvm_native` for the
  arithmetic module) for `MacOSArm64()` or `WindowsX64()`, with the same
  probe adapters as the Linux run;
- `opt` and `llc` build it at O0 and O2 with the native driver's exact
  arguments, and the optimized module must declare the target's triple and
  LLVM's canonical data layout for that explicit destination target;
- the object must have no undefined symbol, except `_fltused` and
  `__chkstk` on Windows, which the platform runtime defines: Windows code
  that uses floating point references `_fltused`, and a frame larger than a
  page calls `__chkstk`. The bundle's `windows-x64/runtime.o` defines both,
  `__chkstk` as compiler-rt's x86-64 probe;
- `<target>/modules.txt` lists each object, then each call with the value
  both VMs and the native Linux runs gave: `call SYMBOL EXPECTED COUNT
  ARGUMENTS...`.

`native-build` (`test/native_aot/native_build/BuildTests.chibi`) adds programs that
`chibi build --native --target` links on Linux, at O0 and O2, and one with a
256 KiB frame, which Windows code probes through the program's `__chkstk`.
`<target>/native-build-programs.txt` lists each as `program NAME STATUS`,
optionally followed by tab-separated UTF-8 arguments, and the program is
`<target>/programs/NAME`. Each case writes a private manifest fragment; the
parent merges those fragments in name order after every case succeeds.
The parent resets its prior list before starting workers. No worker reads,
rewrites or deletes another producer's manifest.

`native-link` (`test/native_aot/link/Smoke.chibi`) adds the argument program, linked by
`native_link` with no SDK: listed once without arguments and once with `a b` and `héllo €`.
Its own `<target>/native-link-programs.txt` keeps both argument cases, so
`native-link` and `native-build` may publish concurrently. Both consumers require
both lists and run their combined eight Windows cases or five macOS cases.
The two rows for `arguments` are intentional and are both run.
The consumer checks each producer's execution count and both argument forms,
so an omitted row or empty list fails the target test.
It checks `chibi_argument_count`, `chibi_argument` and `chibi_environment`. On Windows
the C reference starts it with `_wspawnv`; the authored runner uses
`core:os.Run`. Both pass Unicode arguments to `GetCommandLineW` as UTF-16.

The Linux cases that supervise a hung or overflowing native call, and the
lazy-arm and lookup-table checks that rewrite or hand-write IR, stay on
Linux.

`Runner.chibi` links and runs a bundle on its own target. It uses
`native_link_host.Link` with LLVM22's `ld64.lld` on macOS or `lld-link` on
Windows, loads each resulting image, and calls every listed symbol with the
original ABI. `chibi_scalar` takes no arguments and returns `i32`; each
`probe_` adapter takes a pointer to signed 64-bit arguments and returns `i64`.
Calls and arguments occupy flat arrays; symbols are interned and resolved
once per module. The Windows DLL plan exports those exact symbols. Loaded
images are closed before removal.

The runner then executes every listed program and checks its exit status.
On macOS it first sets mode 0755 because artifact transfer discards file
modes. Missing manifests, omitted program rows, changed Unicode argument
cases and malformed calls fail the test.

CI's Linux `native` job cross-builds the two runner executables with its
current native compiler and includes them in `native-bundle`. The
`native-targets` jobs execute their own target runner without compiling a
compiler there. While the replacement is being verified, both jobs also
build and execute the existing `run.c` reference against that same artifact.
The C reference and its MSVC setup remain until actual macOS and Windows
execution agree. The macOS job also runs `controls.sh`: both consumers must
reject incomplete manifests, invalid calls, missing symbols, changed
results and argument cases. Signed 64-bit boundaries and the three-argument
limit pass through the real adapter ABI; the native parser additionally
rejects integer overflow.

To cross-build the consumers with a current native compiler and LLVM22:

```sh
sh test/native_aot/targets/build.sh /absolute/path/to/chibi /absolute/path/to/llvm/bin /tmp/bundle
# On macOS, after copying the complete bundle:
chmod +x /tmp/bundle/runner-macos-arm64
/tmp/bundle/runner-macos-arm64 /tmp/bundle /absolute/path/to/llvm/bin/ld64.lld
```

On Windows run `runner-windows-x64.exe BUNDLE LLVM_BIN/lld-link.exe`.
Each complete bundle also needs both producer-owned program manifests and
their programs, the module objects, and Windows' `runtime.o`.
