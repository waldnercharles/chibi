# Debian cloud setup and task entry

This profile tracks CUT-49 and CUT-52 for the c-blueprints configuration published
October 4, 2026, whose toolchain lives in `/workspace/.cloud-tools`. It is separate
from `tools/cloud_setup.sh`, the older Ubuntu/Claude setup. Do not use the Ubuntu
script in the Debian environment. An older saved configuration has the same name;
select by publication date/ID in the product, not by name alone.

## Task entry

Normal Make commands and direct `tools/mcp/launch.sh` entry automatically apply
cloud preflight when the saved prefix directory is present on x86_64 Debian 13:

```sh
make cloud-preflight              # versions and selected compilers
make build JOBS=2                 # no manual source/wrapper needed
make cloud-smoke                  # bounded real compiler checks
```

`tools/cloud/entry.sh` is the common detector and bootstrap. Make invokes it once
before enrollment/locking, then delegates with goals, overrides and jobserver
intact. Recursive Make inherits the verified environment; workspace locking still
uses the existing workflow. Pure `help`/`clean` goals and query/dry-run/touch modes
skip cloud preflight. `test-list` requires it because that target builds
`test_suite`. Missing prerequisites in an existing prefix fail before project work;
an absent prefix, non-Debian host, or `CHIBI_CLOUD=0` leaves the existing entry path
unchanged. Detection does not install anything or attach an MCP server.

Explicit environment/Make compiler, toolchain and flag values are retained,
including empty overrides. CMake retains its usual cached-compiler and explicit
`-D` option precedence. With a `CMAKE_TOOLCHAIN_FILE` environment value,
activation leaves compiler selection to CMake rather than defaulting CC/CXX.
Otherwise only unset CC/CXX default to the provisioned Clang pair. Existing PATH
tools keep precedence; cloud directories supply fallbacks. An incompatible selected
CMake, Ninja, Emscripten, Node or formatter fails with an actionable error instead
of being silently shadowed. `CLANG_FORMAT` selects the formatter when set. A
command-line Make PATH remains exact and must already expose the needed tools.
Use `CHIBI_CLOUD=0` when supplying a separate toolchain that does not meet this
profile's checks.

Arbitrary commands outside Make/MCP still need the explicit agent entry point:

```sh
tools/cloud/bin/start_skill -- <command> [args...]
tools/cloud/bin/start_skill --modeling -- blender --background --factory-startup --version
```

The wrapper checks prerequisites and executes the command with that environment.
With no command it prints readiness and versions. It preserves the working
directory and command exit code. `--modeling` additionally checks the pinned
Blender installation and runs the small CPU-only OBJ fixture before execution.
If PATH selects a different Blender, that executable/data must pass the same
fixture too; the selected binary is not silently replaced.
The explicit wrapper requires cloud setup; Make/MCP's automatic detector is what
provides the no-prefix/opt-out behavior.

Manual recovery for a long-lived **Bash** shell:

```bash
source tools/cloud/activate.sh
start_skill
```

Activation is idempotent and does not write shell profiles. Executable entry
points work from Bash login and non-login shells, including `--noprofile --norc`;
sourcing is supported only in Bash. Start from a clean shell when switching prefixes
or moving from the legacy saved activation script: inherited `C_INCLUDE_PATH`,
`CPLUS_INCLUDE_PATH`, `LIBRARY_PATH`, or `NATIVE_TOOLCHAIN_FLAGS` may still point at
the previous profile. User overrides are not erased. A child process cannot
activate its parent shell; running `start_skill` does not affect another tool call.

**Platform limit:** this is automatic repository-entry preflight, not an
arbitrary-shell or platform startup hook. No saved environment settings were
changed. Publication must snapshot the tested prerequisites; refreshing repository
files does not rerun installation. A genuinely new task from the intended updated
publication must still pass plain Make/MCP entry checks before claiming CUT-49's
fresh published-task acceptance. Publication/configuration identity and adoption
remain unverified by this repository work.

## Provisioning

```sh
bash tools/cloud/setup.sh             # compiler/base profile
bash tools/cloud/setup.sh modeling    # optional Blender profile
```

The scripts support x86_64 Debian 13 trixie, use its official signed package archives,
and extract packages under `/workspace/.cloud-tools`. Set `CHIBI_CLOUD_TOOLS` to
choose another absolute writable prefix without whitespace consistently for setup and activation. The base
host supplies Python/venv, Node, apt/dpkg, and already-installed shared libraries;
this is a workspace profile for that host, not a hermetic OS image. Downloaded
package dependencies are resolved against the host package database. No sudo,
package registration, shell profile edits, project builds, or GPU tests are run.
Whitespace prefixes fail before a command/configure runs because Make expands
`NATIVE_TOOLCHAIN_FLAGS` as an argument string.

CMake is pinned to 4.2.3 in a local Python virtual environment; clang-format major
22 is installed there for the repository commit hook. Clang/LLVM/LLD are major 19; LLVM runtime/tools, Clang resource headers and
sanitizer runtimes, Binaryen, and node-acorn are explicit local prerequisites;
remaining base packages follow the Debian trixie archive (not a byte-for-byte
snapshot). Blender and blender-data are explicitly pinned to `4.3.2+dfsg-2`.
If that Debian version is no longer available, setup fails: review and test a new
pin rather than silently selecting another version. Modeling's local executable,
data files, package marker and runtime version must all pass its check; a host
`/usr/bin/blender` alone is insufficient.

The saved `/workspace/.cloud-tools/setup.sh` and `activate.sh` are not rewritten.
To adopt this repository implementation in a published configuration, its setup
command must explicitly call the checked-out `tools/cloud/setup.sh` with the
required profile. That product-level adoption and publication were not performed.
Each setup uses a fresh disposable apt transaction directory and extracts only
packages downloaded for that invocation. Old `apt/archives` and
`apt/modeling-archives` files are never extraction inputs. Repetition therefore
redownloads selected packages; historical zero-download results below describe
the earlier implementation, not the hardened transaction implementation.
Setup finishes with the bounded compiler smoke and fails if it cannot compile
and execute native C/C++, ASan/UBSan, and optimized Wasm under Node. Native header
search uses CMake's prefix, not global include-path variables that contaminate
Emscripten's sysroot. Debian Acorn is linked into Emscripten's `node_modules` for
the platform Node's standard package lookup.

Concurrent setup processes against the same prefix are unsupported; parallel task
commands may share an already-provisioned prefix.

## Mesh exchange contract

The Blender fixture authors an asymmetric tetrahedron with translation, rotation
and nonuniform scale. Author in Blender's Z-up space and export OBJ with forward
`NEGATIVE_Z`, up `Y`, global scale `1.0`, selected objects only, modifiers applied,
triangulation and normals enabled, UVs/materials disabled for this geometry check.
The resulting raw OBJ coordinates are `(x, z, -y)`: +Y up and -Z forward. Object
transforms are baked into exported vertices. Import with the same axis and scale
settings to recover the Blender world-space geometry.

Blender's fixture uses metric display and unit scale 1. OBJ carries no unit
metadata; one authored unit remains one exported numeric unit. This is an exchange
convention, not a claim that all Chibi scenes use metres. The fixture asserts raw
coordinates independently of reimport, then checks the restored vertices, four
triangles and unit object scale. It does not render or regenerate Suzanne.
Rendering remains a separate capability-aware, budgeted workflow (CUT-51).

## Verification, October 5, 2026

New task environment: `ccarenv_353c5b04662c8191a38780964a5a68df`, Debian 13.6,
checkout base `96322c71`. Its restored saved script fingerprints were:

- setup.sh SHA-256 `cfedd64ea50a8fc72e1ab58ded3125b45f7ed8e0e44999635ad97e2567d75c0e`
- activate.sh SHA-256 `8ddac4de5fc057df7208e3b3de5cf9666246df77de6197552fc983b2cd9bf9ad`

The environment metadata did not expose a saved configuration ID or publication
revision, so attribution to the intended October 4 publication cannot be proved.
In the initial fresh tool shell and fresh Bash login/non-login shells,
`command -v start_skill cmake ninja clang-19 blender` found only `/usr/bin/blender`.
Both host Blender packages were `4.3.2+dfsg-2`.

After implementing this profile, base setup and repeat setup passed. Clean shells
with only HOME and the original platform PATH explicitly retained were checked:

```bash
for mode in -c -lc; do
  env -i HOME="$HOME" PATH="$PATH" bash "$mode" \
    'cd /workspace/cut-setup; tools/cloud/bin/start_skill'
done
timeout 60 tools/cloud/bin/start_skill -- bash tools/cloud/smoke.sh
tools/cloud/bin/start_skill -- python3 test/configure_cache_test.py
bash tools/cloud/setup.sh modeling
bash tools/cloud/modeling_setup.sh --check
```

Both shell modes reported Clang 19.1.7 (Debian 3+b1), CMake 4.2.3, Ninja 1.12.1,
Node v24.19.0 and Python 3.12.14. Additional checks reported Emscripten 3.1.69 and
ccache 4.11.2. The smoke verifies unchanged environment after repeat activation,
actionable failure for a missing prefix, C23 configure/compile/run with Ninja,
and (after hardening) an actual optimized Emscripten compile/link/Node run. These are bounded setup checks; they do not
claim a passing full engine, GPU or sanitizer suite.

The nine repository configure-cache tests passed. Modeling setup downloaded the
explicit pinned packages (59.6 MB), passed its fixture, and reused the cached
packages on repeat (0 B download). `env -i HOME=/tmp PATH=/usr/bin:/bin bash
--noprofile --norc tools/cloud/modeling_setup.sh --check` also passed: local Blender
4.3.2, background bpy and the OBJ round trip without DISPLAY or inherited cloud
activation. This tests the new implementation inside the fresh task described
above; it is not a second newly provisioned task from an updated publication.

## Setup hardening regression

Run `bash tools/cloud/integration_test.sh --download` against the prepared
October 4 profile to reproduce the opt-in real-archive regression. It requires
that baseline's official package archives and extracted shared libraries, network
access and several GB of temporary space. It does not alter the host package
state. The test constructs a private apt status database from the real host plus
those official package records, so LLVM/LLD/Binaryen are considered installed.
Their shared-library dependencies can come from the prepared baseline, while
all compiler executables, Clang resource headers and Wasm tools must be provisioned
into a new empty prefix. It runs real apt, CMake/Make, Clang, emcc, Node and Blender.

Two harmless `.deb` fixture revisions are placed in each old archive directory.
Both contain a sentinel file that must never appear in the installed tree. Initial
and repeated setup must ignore both revisions. A whitespace prefix must reject
an actual `configure_native.sh` invocation before its build directory is created.
This is an isolated dependency-state regression, not a newly published task image.

Hardening verification on October 5 passed both initial/repeated private-status
regression installs and a separate genuinely empty prefix using the actual host
package database with no inherited baseline library path:

```bash
CHIBI_CLOUD_TOOLS=/tmp/cut-real.4XkzNB bash tools/cloud/setup.sh modeling
```

The latter passed Blender 4.3.2 OBJ checks, Make-to-CMake flag propagation, C23,
C++, ASan/UBSan compile/link/run, and `emcc -O2` compile/link followed by Node
execution. Wasm runtime-library generation was limited to two compiler workers;
setup bounds its complete compiler smoke to 180 seconds. Binaryen reported
`wasm-opt version 120`; other recorded tool versions above were unchanged.
The integration test explicitly observed apt saying LLVM/LLD/Binaryen were already
installed, then verified the newly downloaded local tools by compiling and running.
Both stale revisions remained unextracted across repeat setup.

## Automatic repository entry verification

After adding the Make/MCP bootstrap, plain `make --no-print-directory
cloud-preflight` passed in independent `env -i` Bash login and non-login shells
with only HOME and the original platform PATH retained. No activation command was
sourced or wrapped around Make. Plain `make cloud-smoke` passed the real native
C/C++, ASan/UBSan and optimized Wasm/Node checks. Reported versions were unchanged.

`test/cloud_entry_test.py` exercises automatic entry with isolated tool fixtures:
explicit/empty compiler and flag overrides, formatter selection, command-line PATH,
incomplete/no-prefix cases, Fedora detection, silent direct MCP launch, recursive
Make, workspace locking and jobserver handling, help/query/dry-run/touch behavior.
The existing `test/workspace_make_test.py` regression also passed unchanged.
Fedora detection was fixture-tested; no real Fedora host was provisioned here.

Fresh published-task acceptance still requires recording its configuration ID,
publication revision and checkout commit, then running plain `make cloud-preflight`
and `timeout 180 make cloud-smoke` in separate fresh tool shells. Verify the
modeling wrapper too if that profile was published. Record the selected tool paths
and versions, and confirm `make help`/`make -n build` do not require provisioning.
These entry-point checks do not establish automatic activation of unrelated shell
commands.

The final entry suite passed 23 checks; the unchanged workspace Make suite passed
12. Real Make checks also preserved `CC=/usr/bin/cc CXX=` from the environment and
`CC= CXX=/usr/bin/c++` from command-line assignments. An empty temporary prefix
failed before configure with the setup recovery command; after removing that
prefix, `make cloud-preflight` reported activation skipped. Modeling verified both
the provisioned wrapper and the selected `/usr/bin/blender` (both 4.3.2).
