# Graph SDK packaging

## Compiler tools, 2026-10-22

Branch `codex/graph-sdk-packaging-20261022` started from main
`1bc97b7305ee91102d3ee035e4852aa5341df31c` and integrated
`fa1b7b2bb297eacbbed6aec6f03c4260355db97c`, then the eligible runtime repair
`5c28191b3ea53afa232c34e37b14fd9b01024aef`, by fast-forward in
`.worktrees/self-hosted-build-programs/chibi`. The main checkout's unrelated
changes remain untouched.

The first increment installs the graph-built compiler and build hosts into a
fresh relocatable prefix. It carries the complete compiler/bootstrap and core
resources, authored Build/Graph/SDK/StageOne programs and graph packages, the configure
signature helper, and notices for the exact linked dependency archive. Compiler
sources, the checked bootstrap and ABI20 remain unchanged.

Reserved files are `tools/toolchain/SDK.chibi`, private build-host directory
publication bindings, `test/tools/build_sdk.c`, their dispatcher/build wiring,
the graph explicit profile, and this document. Shared Build/Graph changes
require coordination. The typed
asset owner retains `sdk/CMakeLists.txt`, asset resources and existing graph
target changes. The CI owner retains workflows, dependency pins and publication.

The acceptance gates are:

1. Purpose-made C cases fail when the staged-host and exact-provenance checks
   are removed.
2. Both private hosts come from a Release graph build. Staged compiler/core
   inventories match the current source, and the dependency notices belong to
   the same archive the executable link records name.
3. Preparation uses an exclusive sibling directory. Failed preparation never
   publishes a prefix, and publication refuses every existing destination.
   Output and staging must lie outside every copied directory input, including
   path aliases; reject overlap and invalid ancestors before creating anything.
4. Move the prefix to a path with spaces. Recover the compiler and compile/run
   an independent build program from an unrelated root with an empty tool PATH.
5. Review the complete candidate independently, run the affected packaging and
   host checks, and run all five repository Make groups before publication.

The graph explicit profile skips toolchain discovery, so the installed graph
can emit commands with an empty PATH and unavailable native compilers. Normal
profiles retain their current behavior.

### Final macOS validation

All execution checks passed on reviewed tree
`77493ea3dbf675f3640b607b00cd16ed924ee9c1` over main `5c28191b3e`.
Whole-repository formatting then found one diagnostic `fprintf` line wrap in
`build_process.c`. Clang-format 22 changed only that call's whitespace. The
refreshed tree `84ca3290f833dafb4ad1bc3605700c4698c30791` passed the affected
host rebuild, complete host selector and whole-repository format check. Product
and test sources equal that refreshed tree; only this document and `PLAN.md`
add the final evidence.

| Check | Result and elapsed wall time |
| --- | --- |
| Release graph private hosts; test drivers; CMake reference | Passed; 8.068s, 13.309s, 4.099s build/configuration. |
| `build-sdk` | 8 cases / 205 assertions; 56.941s. |
| `build-process` | 44 / 7,005; 53.558s; after formatting, rebuilt in 7.317s and passed again in 67.895s. |
| `build-toolchain` | 11 / 165; 236.013s. |
| `build-graph` | 3 / 1,004; 8.715s; 971 compile commands, 140 libraries and 13 links match CMake. |
| Guard-removal controls | Each removal failed its intended C case, with 7 passing / 1 failing: containment 28.594s, staged host 30.482s, exact archive path 40.447s. Source bytes restored. |
| Five required Make groups | All passed; 188.969s including their builds. Group execution: native 33.769s, materials 33.835s, projects 51.704s, language 22.521s, tools 26.632s. Tooling aggregate: 130 cases / 27,374 assertions. |
| Whole-repository format check | Passed after the whitespace correction; 28.312s. |

Independent reviews cleared the product contracts, containment correction,
resource inventory and merged runtime interaction. StageOne is copied verbatim;
its installed execution route remains unverified.

Retained local evidence is in `.chibi/sdk-packaging`: `final-source.json`,
`tested-binaries.json`, `final-binaries.json`, `final-results.json`,
`format-refresh-results.json`, `format-only.patch`, the three named
`without-*-check.log` controls, and logs
`1792717868616195000-74983-final-combination.log` and
`1792718617562931000-2141-format-refresh.log`. The first log deliberately retains
the original formatting failure; the refresh log records its successful repair.
Generated scratch packages and the old host copy were removed. Build products,
logs, receipts and orchestration scripts remain ignored.

Refreshed binary SHA-256 identities:

- `build-release/sdk/chibi-build-host`: `bc277b60687984404971389590e9c7a6923f6651414a0d20acb3f3de3986c065`
- `build-release/sdk/chibi-compiler-host`: `7277a1adc03af2abca8f2a987bdb88d2446274681d44a3488f46f3f09590b620`
- `build/test/tools/test_tools`: `58291030db72c1941e7abf2fbfb8653bee6ac5e03f4dcfa350e49d89c9e74bc3`

### Evidence before the main integration

These results belong to the working patch over `1bc97b7305`, before the
`fa1b7b2bb2` compiler resources were integrated. They are diagnostic evidence,
not acceptance of the final combination. Logs are under the ignored
`.chibi/sdk-packaging` directory.

| Check | Result |
| --- | --- |
| Release graph private hosts | Built. |
| `build-sdk` | Passed 7 cases / 103 assertions, 136.219s. Includes relocation, exact payload and notices, independent installed compilation and explicit graph commands. |
| `build-graph` | Passed 3 cases / 1,004 assertions, 36.616s; 971 compile commands, 140 libraries and 13 links match CMake. |
| Focused host file cases | Passed 3 cases / 52 assertions, 19.6s, including publication on VM and SLJIT. |
| Required native and materials groups | Passed, 108.830s and 116.368s. |
| Required projects group | Failed its Terrain Lab restart child's existing 10s deadline; focused retry passed unchanged in 25.946s. |
| Required language and tools groups | Not reached after the projects failure. |
| Full `build-process` and `build-toolchain` retries | Failed short fixture deadlines; functional compiler preparation and the fake SDK environment require separate diagnosis. |
| `make check-format` | Passed, 102.6s. |

The final combination supersedes these earlier results as recorded above. The
earlier failures remain diagnostic history and do not establish acceptance.

The fixture repairs separate successful self-hosted compilation from the process
behavior under test. Functional primitive invocations allow 120s for compiler
preparation and execution. Concurrent-child and unwaited-child checks start
their elapsed measurements at the existing prepared callback, retaining their
1.2s and 2s operation assertions. Explicit deadline, cancellation, instruction
and storage-limit witnesses keep their limits. The toolchain fixture resolves
Git before installing its fake SDK environment: Apple's Git launcher hangs with
the fake `SDKROOT`, while the resolved developer-tool executable succeeds. Small
Git setup/probe checks passed; the full toolchain retry remains pending.

For Linux, build the Release graph hosts with the matching pinned dependency
archive, then run the C cases from a separate CMake-built harness. Linux graph
test-driver compilation and linking are not supported by this checkpoint:

```sh
make build-release GRAPH_BUILD=1 BUILD_TARGET='chibi-build-host chibi-compiler-host'
make build GRAPH_BUILD= BUILD_DIR=build-sdk-tests BUILD_TARGET=test_tools BUILD_HOST="$PWD/build-release/sdk/chibi-build-host"
CHIBI_TOOLS_BUILD_DIR=build-sdk-tests/test/tools \
CHIBI_BUILD_HOST="$PWD/build-release/sdk/chibi-build-host" \
CHIBI_SDK_GRAPH_BUILD_DIR="$PWD/build-release" \
  python3 tools/test_policy.py tool build-sdk
```

`GRAPH_BUILD=1` selects the graph for the shipped Release hosts and their link
records. The empty `GRAPH_BUILD=` selects CMake only for the C test harness in
`build-sdk-tests`; it must not replace the Release graph outputs in
`build-release`. The Linux CI job records SHA-256 for both shipped hosts and each
adjacent `.chibi-record` before the harness build and checks them immediately
afterwards. The generated harness directory is explicitly ignored; source
cleanliness checks remain enabled.

`build-sdk` derives the unpacked archive from the Release graph executable link
record. `CHIBI_SDK_THIRD_PARTY_DIR` may name that same archive explicitly. The
corrected recipe still requires a successful Linux packaging run.

Linux/Windows execution, full SDK headers/libraries and generated CMake exports,
engine/package identity metadata, browser and Studio consumers remain later
increments. Their existing CMake paths remain until their platform checks pass.
