# Proposed Linux acceptance for PR460's Cute guard

Status: command proposal for review, not executed on Linux. No dispatch is authorized
by this document. Root owns the current build/GPU slot and publication decision.

## Prerequisites

- Use an Ubuntu 24.04 x86_64 runner with Clang/LLVM 18, real glibc headers,
  ASan+UBSan, Xvfb, and Mesa lavapipe. This proves software rendering behavior,
  not hardware performance or the clay laboratory's visual acceptance.
- Freeze a reviewed commit containing the guard, `--draw-uniforms` C/pico case,
  CMake/Graph/selector wiring, and the eligible CI662 vendor-system-library fix.
  The current local repair was reviewed at tree
  `619e6552f79cb64abf23aa60e11e72bf60606e51`, based on main `1bc97b730`.
  It is uncommitted and is not itself a dispatchable source ref.
- That candidate must pin an eligible `linux-x64/address-undefined` archive in
  `third_party/archives.tsv`. CI662's system OpenGL versus static vendor export
  defect is a current prerequisite owned by CI/Graph, not a missing-pin waiver.
- Preserve the published baseline archive unchanged. Replace only Cute in a
  private copy. The private directory's digest-shaped name is a lookup key;
  after replacement it does not identify the published archive's contents.
- Do not use `memcmp_nonnull.h`, `contract-instrumentation.cmake`, or any forced
  declaration on Linux. Its real glibc declaration supplies the contract.

The CI receipt uses the existing `test/tools/test_tools` dispatcher through the
repository's native process supervisor, with its explicit GPU profile:

```sh
CHIBI_EXECUTION_PROFILE=gpu-check python3 tools/test_policy.py native -- \
  xvfb-run -a "$PR460_BUILD/test/tools/test_tools" --draw-uniforms
```

This is the same supervised route used below and retains full native output.
Use one invocation per phase. The case draws two
circles in separate layers of one hidden 32x32 app, then checks both colors and
the clear background. Keep the exact same C source, readback wait, selector,
renderer environment, sanitizer options, and compiler flags for red and green.

## Runner and immutable inputs

Use bash. Extend the existing native job's package installation with Xvfb,
lavapipe discovery, and LLVM's symbol tools. Use the repository's hashed build
tool requirements, because Ubuntu's stock CMake is insufficient for pinned Cute.

```sh
set -euo pipefail
: "${PR460_SOURCE_SHA:?Set the exact reviewed integration commit}"
test "$(git rev-parse HEAD)" = "$PR460_SOURCE_SHA"
test -z "$(git status --porcelain)"
test "$(uname -m)" = x86_64

sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  clang-18 clang-tools-18 llvm-18 libclang-18-dev libclang-rt-18-dev \
  ninja-build ccache git gh python3-venv ripgrep libssl-dev \
  libgl1-mesa-dev libvulkan1 mesa-vulkan-drivers vulkan-tools \
  libx11-dev libxcursor-dev libxext-dev libxi-dev libxrandr-dev \
  libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev \
  libasound2-dev libpulse-dev xvfb xauth

PR460_TMP="${RUNNER_TEMP:?}/pr460-linux"
test ! -e "$PR460_TMP"
mkdir -p "$PR460_TMP"
python3 -m venv "$PR460_TMP/build-tools"
"$PR460_TMP/build-tools/bin/pip" install --only-binary=:all: \
  --require-hashes -r .github/build-tools.txt
export PATH="$PR460_TMP/build-tools/bin:$PATH"
export CC=clang-18 CXX=clang++-18

IFS=$'\t' read -r PR460_REPO PR460_RELEASE PR460_PIN < <(python3 - <<'PY'
from pathlib import Path
rows = Path('third_party/archives.tsv').read_text().splitlines()
repo = next(x.removeprefix('# repository ') for x in rows if x.startswith('# repository '))
pins = [x.split('\t') for x in rows if x.startswith('linux-x64\taddress-undefined\t')]
assert len(pins) == 1, 'Need the reviewed Linux sanitizer pin'
print(repo, pins[0][2], pins[0][3], sep='\t')
PY
)
gh release download "$PR460_RELEASE" --repo "$PR460_REPO" \
  --pattern third-party-linux-x64-address-undefined.tar.gz --dir "$PR460_TMP"
printf '%s  %s\n' "$PR460_PIN" \
  "$PR460_TMP/third-party-linux-x64-address-undefined.tar.gz" | sha256sum -c -
mkdir -p "$PR460_TMP/pristine" "$PR460_TMP/private-cache/$PR460_PIN"
tar -xzf "$PR460_TMP/third-party-linux-x64-address-undefined.tar.gz" \
  -C "$PR460_TMP/pristine"
cp -a --reflink=auto "$PR460_TMP/pristine/." "$PR460_TMP/private-cache/$PR460_PIN/"
PR460_PREFIX="$PR460_TMP/private-cache/$PR460_PIN"
PR460_BUILD="$PR460_TMP/engine"
```

## Build the real unguarded control

The normal third-party configuration checks out every dependency at the frozen
source's pin and applies the ordered patches. Reverse only the candidate guard
in this task's fetched Cute checkout. RelWithDebInfo retains source-level stacks;
both phases keep the same optimization, macros, and sanitizer configuration.

```sh
cmake -S third_party -B "$PR460_TMP/deps" -G Ninja \
  -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCHIBI_SANITIZERS=address,undefined \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
PR460_CUTE="$PR460_TMP/deps/_deps/chibi_cute_framework-src"
PR460_GUARD="$PWD/third_party/cute_framework/draw-zero-uniforms.patch"
PR460_OBJECT="$PR460_TMP/deps/cute_framework/CMakeFiles/cute.dir/src/cute_draw.cpp.o"
PR460_LIBRARY="$PR460_TMP/deps/cute_framework/libcute.a"
git -C "$PR460_CUTE" rev-parse HEAD
cp --reflink=auto "$PR460_CUTE/src/cute_draw.cpp" "$PR460_TMP/cute-green.cpp"
git -C "$PR460_CUTE" apply --reverse --check "$PR460_GUARD"
git -C "$PR460_CUTE" apply --reverse "$PR460_GUARD"
cp --reflink=auto "$PR460_CUTE/src/cute_draw.cpp" "$PR460_TMP/cute-red.cpp"
cmake --build "$PR460_TMP/deps" --target cute --parallel 2

llvm-nm-18 --undefined-only "$PR460_OBJECT" > "$PR460_TMP/red-object-symbols.txt"
llvm-nm-18 --print-file-name --undefined-only "$PR460_LIBRARY" > "$PR460_TMP/red-library-symbols.txt"
rg '__ubsan_handle_nonnull_arg' "$PR460_TMP/red-object-symbols.txt"
rg 'cute_draw.cpp.o.*__ubsan_handle_nonnull_arg' "$PR460_TMP/red-library-symbols.txt"
cp --reflink=auto "$PR460_LIBRARY" "$PR460_TMP/red-libcute.a"
cp --reflink=auto "$PR460_LIBRARY" "$PR460_PREFIX/lib/chibi-vendor/libchibi_vendor_cute.a"

sh tools/configure_native.sh "$PR460_BUILD" -G Ninja \
  -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
  -DCMAKE_BUILD_TYPE=Debug -DCHIBI_ENABLE_SANITIZERS=ON \
  -DCHIBI_SANITIZERS=address,undefined -DCHIBI_ENABLE_WARNINGS=ON \
  -DCHIBI_ENABLE_CCACHE=ON -DLIBCLANG_ROOT=/usr/lib/llvm-18 \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCHIBI_THIRD_PARTY_CACHE="$PR460_TMP/private-cache"
cmake --build "$PR460_BUILD" --target test_tools --parallel 2
```

`sdk/vendor_dependencies.cmake` installs static archives by copying and renaming
them, so the private Cute replacement above follows that existing layout. Confirm
the actual `test_tools` link command names this private archive, not a shared
cache or release tree. Retain the binary and Cute archive hashes before running.

## Identical rendering command for red and green

Explicit X11 selects Xvfb instead of the case's default offscreen path. Explicit
Vulkan and the sole lavapipe ICD prevent a backend change between phases. Keep
the discovered ICD path and Vulkan inventory in the receipt. Do not classify the
run as hardware acceleration. If Xvfb/Vulkan/lavapipe cannot initialize, stop and
report that setup failure; do not switch backends or weaken checks.

```sh
PR460_ICD=$(rg --files /usr/share/vulkan/icd.d | rg '/lvp_icd[^/]*\.json$')
test -n "$PR460_ICD"
test "$(printf '%s\n' "$PR460_ICD" | wc -l)" -eq 1
export XDG_RUNTIME_DIR="$PR460_TMP/runtime"
mkdir -m 700 "$XDG_RUNTIME_DIR"
export SDL_VIDEO_DRIVER=x11 SDL_GPU_DRIVER=vulkan SDL_AUDIODRIVER=dummy
export VK_ICD_FILENAMES="$PR460_ICD" SDL_LOGGING=gpu=info
export ASAN_OPTIONS=abort_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1:external_symbolizer_path=/usr/bin/llvm-symbolizer-18
xvfb-run -a vulkaninfo --summary > "$PR460_TMP/vulkan-inventory.txt" 2>&1

pr460_draw() {
  CHIBI_EXECUTION_PROFILE=gpu-check python3 tools/test_policy.py native -- \
    xvfb-run -a "$PR460_BUILD/test/tools/test_tools" --draw-uniforms
}
set +e
pr460_draw > "$PR460_TMP/red-draw.log" 2>&1
PR460_RED_SUPERVISOR_STATUS=$?
set -e
printf '%s\n' "$PR460_RED_SUPERVISOR_STATUS" > "$PR460_TMP/red-supervisor-exit.txt"
test "$PR460_RED_SUPERVISOR_STATUS" -ne 0
```

The `native` route is the existing `run_commands` process supervisor used by the
agent wrapper. It preserves full stdout/stderr and normally propagates native exit while
cleaning its process group and, on Linux, detached/orphaned descendants. Xvfb is
inside that supervised command. No new timeout is introduced; the C readback wait
and sanitizer settings remain unchanged. The direct route is intentional because
`agent.py` discards successful native output and truncates failure output.

Red is accepted only when the failure is the real unguarded `memcmp` in
`s_process_command`, with UBSan's null-argument diagnostic and source/stack
correlation to that comparison. A nonzero supervisor exit from setup, linking,
shader compilation, a readback assertion, or another sanitizer report does not
establish this regression. Preserve the complete native diagnostic and record
the native outcome separately from the supervisor outcome.

Each phase's receipt must distinguish `native_result`, `native_exit`,
`supervisor_exit`, `cleanup_result`, and `remaining_owned_processes`. A cleanup
exception can replace the child's exit with the supervisor's error exit. When
that happens, retain the primary sanitizer source/stack evidence, record the
cleanup diagnostic separately, and leave the native exit unknown unless it was
independently captured. Do not infer it from the supervisor exit. For example,
the local Mac diagnostic reached the intended `memcmp` at `cute_draw.cpp:5875`
and then reported cleanup `EPERM` with supervisor exit 2. That establishes the
scoped native sanitizer reproduction, not a clean supervised run. Root's separate
process inspection found no owned processes remaining. Linux is still unexecuted.
A cleanup failure remains a CI failure requiring an explained, reviewed
resolution; do not mask it because the red sanitizer failure was expected.

```sh
git -C "$PR460_CUTE" apply --check "$PR460_GUARD"
git -C "$PR460_CUTE" apply "$PR460_GUARD"
cmp "$PR460_CUTE/src/cute_draw.cpp" "$PR460_TMP/cute-green.cpp"
cmake --build "$PR460_TMP/deps" --target cute --parallel 2
cp --reflink=auto "$PR460_LIBRARY" "$PR460_TMP/green-libcute.a"
cp --reflink=auto "$PR460_LIBRARY" "$PR460_PREFIX/lib/chibi-vendor/libchibi_vendor_cute.a"
cmake --build "$PR460_BUILD" --target test_tools --parallel 2
set +e
pr460_draw > "$PR460_TMP/green-draw.log" 2>&1
PR460_GREEN_SUPERVISOR_STATUS=$?
set -e
printf '%s\n' "$PR460_GREEN_SUPERVISOR_STATUS" > "$PR460_TMP/green-supervisor-exit.txt"
test "$PR460_GREEN_SUPERVISOR_STATUS" -eq 0
```

Green requires native and supervisor success, one passing C/pico case and five
assertions, with no sanitizer or cleanup failure. The C test verifies actual
pixels. This pass does not establish
clay-fixture appearance, Studio, a second renderer, temporal behavior, or hardware
performance. Retain native backend/device output when it is emitted, along with
requested-backend and discovery evidence. If native output does not identify the
actual device, leave it unknown rather than supplying the inventory's device name
as though the renderer reported it. The agent wrapper's success marker alone
does not supply observed backend/device evidence.

## Receipt and publication boundary

Retain the reviewed source commit/tree and clean status; all dependency pins and
ordered patch hashes; baseline tarball tag/hash; glibc, LLVM, CMake and SDK/system
header versions; normal glibc `memcmp` declaration; full actual Cute and C-test
compile commands; object symbols; red/green source diff, object/archive/binary
hashes; identical C-test object hash; actual link commands; backend environment,
Xvfb/Vulkan inventory; commands, separate native/supervisor results and cleanup
evidence, wall times and full logs. Verify all
other private vendor archives remain byte-identical between phases. Preserve
assertions, sanitizer settings and process cleanup. Stop for a mismatched red
failure, missing instrumentation, or an unreviewed source/configuration change.

This is the supported CMake Linux path and existing GPU dispatcher, with a private
dependency override. The recipe has not been executed on Linux. CI owner must
review it against the final vendor fix and exact candidate before adding/running
the lane. Passing this lane does not clear the remaining typed-assets/compiler
integration, ordinary macOS guarded draw, or clay visual acceptance holds, and
does not authorize a release, pin update, source merge, or PR merge.
