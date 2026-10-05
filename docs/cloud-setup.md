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
Otherwise only unset CC/CXX default to the provisioned Clang pair. The cloud
profile owns the default CMake, Ninja, ccache and Emscripten frontends: repository
wrappers dispatch to the provisioned executables even when ambient PATH contains
older distro tools. Compiler-name lookup stays on the ambient PATH, so an explicit
`CC=clang` is not changed to another compiler. No intent is inferred from arbitrary
inherited PATH ordering.

Set `CMAKE` to an executable path or name to choose another CMake; names resolve
on the original, pre-activation PATH. The variable is preserved, and an explicit
empty/unavailable/incompatible choice fails rather than selecting another binary.
`CLANG_FORMAT` is handled by `tools/clang-format.sh --resolve`, the same resolver
used by formatting/commits: explicit choice, `clang-format-22`, known versioned
paths, then bare `clang-format`. Explicit incompatible formatters fail clearly;
without an override, bare clang-format 14 alongside clang-format-22 is supported.

A command-line Make PATH remains exact: it bypasses PATH insertion and its tools
are validated as selected. If CMAKE differs from that PATH's cmake executable,
include the repository cmake wrapper on the exact PATH to apply the override.
Use `CHIBI_CLOUD=0` to retain ordinary host behavior with an entirely separate
profile. Node and Python remain host prerequisites; their ambient selections are
not inferred to be user overrides or silently replaced.

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
and extract packages under `/workspace/.cloud-tools`. Emscripten uses the official
emsdk distribution pinned to 6.0.10, matching web CI. Set `CHIBI_CLOUD_TOOLS` to
choose another absolute writable prefix without whitespace consistently for setup and activation. The base
host supplies Python/venv, Node, apt/dpkg, and already-installed shared libraries;
this is a workspace profile for that host, not a hermetic OS image. Downloaded
package dependencies are resolved against the host package database. No sudo,
package registration, shell profile edits, project builds, or GPU tests are run.
Whitespace prefixes fail before a command/configure runs because Make expands
`NATIVE_TOOLCHAIN_FLAGS` as an argument string.

CMake is pinned to 4.2.3 in a local Python virtual environment; clang-format major
22 is installed there for the repository commit hook. Clang/LLVM/LLD are major 19; LLVM runtime/tools, Clang resource headers and
sanitizer runtimes are explicit local prerequisites;
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
Emscripten's sysroot. Official emsdk 6.0.10 supplies its matching LLVM/Binaryen
and SDL3 port; platform Node remains the JavaScript runtime. Its versioned config
and cache avoid reusing Debian Emscripten 3.1.69's incompatible sysroot.

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

## Default-tool and formatter correction

The initial entry bootstrap retained all ambient tools and could reject a distro
CMake 3.x despite a ready cloud CMake 4.2.3. Repository tool wrappers now provide
the explicit cloud-profile defaults described above. The formatter preflight uses
the repository resolver rather than checking only bare `clang-format`.

Focused regression coverage includes old ambient CMake selecting the provisioned
compatible executable for actual Make recipes; explicit CMAKE path/name/empty
choices; mixed bare clang-format 14 and clang-format-22; incompatible explicit
formatters (including failing version probes); and all previous recursion, jobserver, locking, exact PATH, opt-out,
query/dry-run and quiet MCP checks. Real plain-Make compiler/Wasm smoke also passed
with CMake 3.25.1 at the front of the ambient PATH. Explicit `CMAKE=cmake` against
that same PATH failed with the >=4.2 requirement. This does not change saved
configuration adoption or published fresh-task verification status.

The corrected entry suite passed 30 checks; the unchanged workspace Make suite
passed 12. Shell syntax checks and `git diff --check` passed.


## SDL3 web toolchain correction and acceptance history

The earlier Emscripten 3.1.69 tiny C/Wasm smoke did **not** establish application
web readiness: that Debian release lacks the SDL3 port required by Cute's
`-sUSE_SDL=3`. Web CI already pins Emscripten 6.0.10. Setup now uses the official
emsdk tag (commit `a2b92777574c2feda07994cd4f1079a3dfc151f8`) and compiler release
`666337b525e673e769121856d175f6f52b8ead64`. The 6.0.10 SDL3 port pins SDL 3.4.2.
See the [official installation instructions](https://emscripten.org/docs/getting_started/downloads.html).
The SDK is workspace-local, and installation does not run `emsdk activate` or
edit any shell/user/saved environment settings. Re-running the installer reuses
an installed SDK. Preflight rejects 3.1.69 and a missing SDL3 port.

On October 5, the official compiler archive at
`https://storage.googleapis.com/webassembly/emscripten-releases-builds/linux/666337b525e673e769121856d175f6f52b8ead64/wasm-binaries.tar.xz`
was blocked by the execution network proxy (`403 Forbidden`), including the
network escalation attempt. At that point, 6.0.10 installation, repeated-install
idempotence, tiny Wasm smoke, and actual `make web` were **unverified**. The old
3.1.69 files were preserved, and corrected preflight failed until SDK installation.
This is historical evidence; the authorized network update and subsequent session
verification below supersede the download blocker.

To update an existing native prefix without repeating its apt provisioning
(official archive access is required):

```sh
bash tools/cloud/emscripten_setup.sh
bash tools/cloud/emscripten_setup.sh  # verify the installed SDK is reused
make cloud-preflight
timeout 180 make cloud-smoke
EMCC_CORES=2 make web JOBS=2 \
  WEB_BUILD_DIR=build-web-emsdk-6.0.10 \
  WEB_THREADS_BUILD_DIR=build-web-threads-emsdk-6.0.10
```

For application acceptance, use the language owner's native-path isolation fix
`01ebe9ac` (PR #174) together with this cloud change; it is intentionally not
copied into this branch. Both named build directories above must be new when
switching SDKs. `make web` builds serial and threaded bundles; changing only
`WEB_BUILD_DIR` still reuses the default `build-web-threads` cache. If either named
directory already exists from an earlier SDK, choose another unused name. Record
both the tiny smoke and actual web result separately. Passing fixture tests or inspecting `sdl3.py` cannot replace
those two real checks. Full `make test`, saved-config adoption, and genuinely fresh
published-task acceptance remain outstanding final gates.

The corrected fixture suite passed 32 entry tests, including rejection of the
old Emscripten version and a missing SDL3 port. Shell syntax and diff checks passed.
No broad regression suite was repeated for this blocked SDK migration.

Offline installer control flow can be checked independently of provisioning:

```sh
CHIBI_CLOUD=0 make test-tool-cloud-emscripten-setup
```

Six tests passed using only local git/emsdk/emcc stubs on a restricted PATH. They
exercise the real `emscripten_setup.sh` entry point: clone/install/reuse, generated
config paths and quoting, failed installation retry, preservation of an existing
config on reinstall failure, unexpected revision rejection, failed version-probe
recovery, and invalid-prefix rejection. The stub models successful SDK reuse; it
does not prove the official installer is idempotent or that the real SDK compiles.
These offline results alone do not establish real installation, Wasm compilation,
application web readiness, or fresh published-task acceptance.


## Current integration and startup adoption plan

Startup adoption is tracked here under CUT-49/CUT-52 and PR #170. Preparation has
started; saved setup adoption and the fresh-publication gate are not complete.
The parent reports that the authorized `storage.googleapis.com` addition was
published and verified. In fresh verifier task `01a10cc3`, official Emscripten
6.0.10 installed successfully, warm reuse passed, and serial/threaded SDL3 tiny
Wasm checks passed. The required workspace clang-format 22.1.8 was installed, and
fresh-shell `cloud-preflight` and `cloud-smoke` passed. The parent also reports
that PR #174's complete web build passed after a separate reviewed fixture fix.
These are attributed session results, not tests repeated in this checkout and
not evidence that a saved startup recipe provisions PR #170 automatically.

Integration preparation merged main `7b35b66da8fbaf94012e18ed78cfd26e5ee09f2a`
into PR #170's `c6ab2651bc2fc6a734633de534f32f91cbb45371` normally, without
conflicts. Current main's AGENTS full verification requirements are retained:
after review, on suitable hardware or CI with full execution authorized, run
`CHIBI_EXECUTION_PROFILE=hardware CHIBI_ALLOW_FULL_GPU=1 make test test-render`.
Record the exact candidate SHA and actual coverage; neither source review nor
prior session/compiler checks substitute for these final gates. This preparation
does not run them, publish settings, push the candidate, or merge the PR.

Candidate-pinned preview sequence for the root coordinator (not executed):

1. Review the integrated candidate and documentation, then record its immutable
   40-character commit SHA as `CANDIDATE`. Once authorized, push that reviewed
   commit to the existing PR branch; verify the remote head matches. Do not pin a
   mutable branch name in the setup recipe. Run the required full gates against
   that same candidate on the authorized executor/CI before merge.
2. Inspect the existing configuration's supported preview/draft-publication flow
   and record its configuration ID, current publication revision, setup command,
   selected profile and rollback revision. Root obtains publication approval and
   performs the browser action. Use that existing configuration; do not create a
   duplicate environment. If the product cannot publish/test a non-production
   candidate revision while retaining rollback, stop and resolve that product
   limitation instead of silently relaxing the premerge gate.
3. In the approved preview setup recipe, fetch the exact reviewed commit into a
   separate detached worktree before invoking its setup. This avoids changing
   the production task checkout or pointing current main at scripts it does not
   contain. For the known workspace layout, the concrete recipe is below; root
   must replace the placeholder with the reviewed SHA before publication.

   ```sh
   set -eu
   repo=/workspace/c-blueprints
   candidate=REPLACE_WITH_REVIEWED_40_CHARACTER_SHA
   preview=/workspace/c-blueprints-cloud-setup-preview
   git -C "$repo" fetch origin "$candidate"
   test "$(git -C "$repo" rev-parse FETCH_HEAD)" = "$candidate"
   if test -e "$preview/.git"; then
     test "$(git -C "$preview" rev-parse HEAD)" = "$candidate"
     test -z "$(git -C "$preview" status --porcelain)"
   else
     git -C "$repo" worktree add --detach "$preview" "$candidate"
   fi
   CHIBI_CLOUD_TOOLS=/workspace/.cloud-tools bash "$preview/tools/cloud/setup.sh" modeling
   ```

   The modeling profile covers CUT-52 as well as CUT-49. The recipe fails on an
   existing mismatched/dirty preview checkout instead of resetting it. An older
   preview must be deliberately retired or a reviewed new preview path chosen.
4. Record the resulting preview publication revision and setup logs. Start a
   genuinely new task from that revision with the reviewed PR checkout, and
   verify `git rev-parse HEAD` equals `CANDIDATE`. If task checkout selection
   cannot preserve that SHA, stop; running against old main cannot test the new
   Make/MCP entry points. Do not manually install packages or source activation
   in this acceptance task. In separate fresh tool shells, run plain
   `make cloud-preflight`, `timeout 180 make cloud-smoke`, and the modeling entry
   `tools/cloud/bin/start_skill --modeling -- blender --background --factory-startup --version`.
   Record exit status, selected paths and versions, configuration ID, publication
   revision, checkout SHA and task ID. Check `make help`, `make -n build`, and a
   bounded direct MCP initialization for quiet protocol output. This acceptance
   tests the published setup and normal repository entry, not arbitrary-shell
   automatic activation. Capture failures without repairing the task in place;
   fix the candidate/recipe and repeat a new publication/task when needed.
5. Root reviews all required checks and the recorded fresh-publication acceptance
   before merging PR #170. Any candidate source change after validation needs its
   affected checks and publication acceptance repeated. Only after successful
   merge and separate approved production adoption should the saved production
   recipe point at the merged scripts/revision. Keep rollback information and
   verify a new production task; do not infer production adoption from preview
   success. CI protections and required reviews remain in force.
