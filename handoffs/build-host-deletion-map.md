# What still needs the C build host (records, 2026-11-01)

The coordinator rebuilt this file on 2026-11-02 from records' final report
(session local_65cdd2c1-7142-4d58-9437-d99f0f3df8e6), because the file records named was not on disk.

Steps 1 and 2 of docs/native-build-runner.md are merged in #1299:
- A real engine build runs through the native graph: `build --release --target chibi-compiler-host --target
  chibi-build-host` compiles 96 C sources into hosts that work. It takes 8 s under the native Driver, and 12 s more under
  the VM Driver to compile the runners. A rerun plans in 0.07 s.
- With `chibi build --native-driver PATH --llvm DIR`, Build, Graph, SDK and the other programs are compiled natively once
  and cached in `.chibi/build-runner/KEY/`. KEY is made from the file stamps of tools/toolchain, tools/graph and
  packages/core, plus the driver, `llc` and the target. A program that fails to compile natively falls back to the
  build host. These are options rather than environment variables, because the C host allows only a fixed list of
  environment names.

Step 3, deleting `chibi-build-host`, is blocked. The host is still:
1. the first host that `tools/build.sh` fetches (hosts.tsv). This can go once the native seed ships the native driver
   and `build.sh` starts the native Driver from it: #1326, then records.
2. a product the graph builds and the SDK installs;
3. how `Engine.chibi` runs the asset-compiler generator. That needs the VM compiler, so it goes with gate 4 and gate 5;
4. what `chibi build --native`, `chibi run --native` and `chibi test` use (gate 4);
5. passed as `--build-host` by many CI steps.

Items 2, 4 and 5 depend on deleting the VM. `src/host/process.c` is also `chibi run`'s child-process engine. Only
macOS arm64 was tested; the runner path has not run on Linux CI yet.
