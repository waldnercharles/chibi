#!/bin/sh
# Real native build programs must execute, cache, and fail without a VM host.
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd -P)
compiler=${1:?usage: native-runner.sh NATIVE_COMPILER LLVM_BIN}
llvm=${2:?usage: native-runner.sh NATIVE_COMPILER LLVM_BIN}
scratch=$(mktemp -d "${TMPDIR:-/tmp}/chibi-native-runner.XXXXXX")
scratch=$(cd "$scratch" && pwd -P)
trap 'rm -rf "$scratch"' EXIT
case "$(uname -s)" in
    Darwin) target=aarch64-apple-darwin ;;
    Linux) target=x86_64-unknown-linux-gnu ;;
    *) echo 'native-runner: unsupported test platform' >&2; exit 2 ;;
esac
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/tools/toolchain/Driver.chibi" --collection "graph=$root/tools/graph" --entry Main --output "$scratch/driver" \
    --llvm "$llvm" --target "$target"
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/bootstrap/fixtures/InstallPaths.chibi" --collection "graph=$root/tools/graph" \
    --entry Main --output "$scratch/sdk-paths" --llvm "$llvm" --target "$target"
# POSIX realpath must not reinterpret a Windows drive as this relative folder.
mkdir "$scratch/C:"
(cd "$scratch" && ./sdk-paths)
checkout=$scratch/'checkout with spaces'
mkdir -p "$checkout/tools/toolchain" "$checkout/tools/graph" "$checkout/packages"
ln -s "$root/tools/compiler" "$checkout/tools/compiler"
ln -s "$root/packages/core" "$checkout/packages/core"
cat > "$checkout/tools/toolchain/Build.chibi" <<'BUILD'
package build
Build :: () -> int { return 0 }
BUILD
cat > "$checkout/tools/toolchain/Graph.chibi" <<'GRAPH'
package graph
import os "core:os"
Libraries :: () -> int {
    if (os.WriteText("native-ran", "ok")) { return 1 }
    return 0
}
GRAPH
cat > "$scratch/host" <<'HOST'
#!/bin/sh
echo 'VM host must never run' >&2
exit 91
HOST
chmod +x "$scratch/host"
cd "$checkout"
run() {
    "$scratch/driver" build --release --target fixture --host "$scratch/host" \
        --native-driver "$compiler" --llvm "$llvm"
}
run
test "$(cat native-ran)" = ok
find .chibi/build-runner -type f -perm -100 -exec shasum -a 256 {} + | sort > "$scratch/before"
# This fixture target needs only Graph; native products no longer prepare
# third-party archives unless their registered resources request them.
test "$(wc -l < "$scratch/before" | tr -d ' ')" = 1
rm native-ran
run
test "$(cat native-ran)" = ok
find .chibi/build-runner -type f -perm -100 -exec shasum -a 256 {} + | sort > "$scratch/after"
cmp "$scratch/before" "$scratch/after"

# A source error must stop the build and leave neither a partial runner nor
# a VM-produced success. Repairing it must work on the next invocation.
cp tools/toolchain/Graph.chibi "$scratch/graph"
printf '\nthis is invalid chibi\n' >> tools/toolchain/Graph.chibi
rm native-ran
if run > "$scratch/rejection" 2>&1; then exit 1; fi
grep -q 'does not compile natively' "$scratch/rejection"
test ! -e native-ran
test -z "$(find .chibi/build-runner -maxdepth 1 -name 'work.*' -print)"
cp "$scratch/graph" tools/toolchain/Graph.chibi
run
test "$(cat native-ran)" = ok
test ! -e build-release/sdk/chibi-build-host
# A compiler that sees an edit during its invocation must not publish under
# the pre-edit identity, even when it successfully writes an executable.
export RUNNER_FIXTURE_PROGRAM
RUNNER_FIXTURE_PROGRAM=$(find "$PWD/.chibi/build-runner" -type f -name Graph-Libraries -print | head -n 1)
cat > "$scratch/mutating-compiler" <<'MUTATE'
#!/bin/sh
set -eu
while [ "$#" -gt 0 ]; do
    if [ "$1" = --output ]; then output=$2; shift; fi
    shift
done
cp "$RUNNER_FIXTURE_PROGRAM" "$output"
printf '\n' >> tools/toolchain/Graph.chibi
MUTATE
chmod +x "$scratch/mutating-compiler"
rm native-ran
if "$scratch/driver" build --release --target fixture --native-driver "$scratch/mutating-compiler" \
    --llvm "$llvm" > "$scratch/mutated" 2>&1; then
    echo 'runner accepted inputs changed during compilation' >&2
    exit 1
fi
grep -q 'input changed during compilation' "$scratch/mutated"
test ! -e native-ran
test -z "$(find .chibi/build-runner -maxdepth 1 -name 'work.*' -print)"
cp "$scratch/graph" tools/toolchain/Graph.chibi
run
test "$(cat native-ran)" = ok
printf 'native runner: execution, reuse, rejection, cleanup and in-flight edits passed\n'

# Explicit recovery and VM comparison prepare their bytecode seed. Ordinary
# native builds and test selectors never request it.
mkdir -p tools/bootstrap
cat > tools/bootstrap/seed.sh <<'SEED'
#!/bin/sh
printf 'seed %s\n' "${1:-default}" >> "$SEED_EVENTS"
exit "${LEGACY_SEED_STATUS:-0}"
SEED
cat > "$scratch/native-tool" <<'TOOL'
#!/bin/sh
printf 'native\n' >> "$SEED_EVENTS"
TOOL
cat > "$scratch/legacy-host" <<'HOST'
#!/bin/sh
printf 'host\n' >> "$SEED_EVENTS"
HOST
chmod +x "$scratch/legacy-host"
printf 'tool\tnative\tsh\t%s\n' "$scratch/native-tool" > tools/tests.tsv
export SEED_EVENTS="$scratch/seed-events" LEGACY_SEED_STATUS=23
: > "$SEED_EVENTS"
run
test ! -s "$SEED_EVENTS"
"$scratch/driver" build test native
printf 'native\n' > "$scratch/expected-events"
cmp "$scratch/expected-events" "$SEED_EVENTS"

# Bootstrap recovery prepares its selected artifact before invoking the host.
mkdir -p "$scratch/releases/sdk"
cp "$scratch/legacy-host" "$scratch/releases/sdk/chibi-compiler-host"
: > "$SEED_EVENTS"
export LEGACY_SEED_STATUS=23
if "$scratch/driver" build bootstrap-recover --artifact 'artifact with spaces' \
    --release-build "$scratch/releases" --native-driver "$compiler" --llvm "$llvm"; then exit 1; else test "$?" = 23; fi
printf 'seed artifact with spaces\n' > "$scratch/expected-events"
cmp "$scratch/expected-events" "$SEED_EVENTS"

# The compiled driver must finish its stages and Windows output, then prepare
# and execute the command contract without a bytecode seed or either VM host.
mkdir -p test/graph
cp "$root/test/graph/CommandTests.chibi" test/graph/CommandTests.chibi
cat > "$scratch/command-contract" <<'CONTRACT'
#!/bin/sh
set -eu
test "$PWD" = "$RUNNER_FIXTURE_ROOT"
test "$#" = 2
test "$1" = "$RUNNER_FIXTURE_FIXED"
test "$2" = "$RUNNER_FIXTURE_LLVM"
printf 'contract-run\n' >> "$SEED_EVENTS"
exit "${RUNNER_CONTRACT_STATUS:-0}"
CONTRACT
chmod +x "$scratch/command-contract"
cat > "$scratch/stage" <<'STAGE'
#!/bin/sh
set -eu
project= core= output= llvm= target= source= entry= collection=
while [ "$#" -gt 0 ]; do
    test "$#" -ge 2
    case "$1" in
        --compiler) project=$2 ;;
        --core) core=$2 ;;
        --output) output=$2 ;;
        --llvm) llvm=$2 ;;
        --target) target=$2 ;;
        --source) source=$2 ;;
        --entry) entry=$2 ;;
        --collection) collection=$2 ;;
        *) echo "unexpected fake-stage option: $1" >&2; exit 92 ;;
    esac
    shift 2
done
test "$PWD" = "$RUNNER_FIXTURE_ROOT"
test "$core" = "$RUNNER_FIXTURE_ROOT/packages/core"
test -n "$output"
if [ -n "$source" ]; then
    case "$source" in
        "$RUNNER_FIXTURE_ROOT/test/graph/CommandTests.chibi") ;;
        *) echo "unexpected fake-stage source: $source" >&2; exit 92 ;;
    esac
    test "$0" = "$RUNNER_FIXTURE_FIXED"
    test "$project" = "$RUNNER_FIXTURE_ROOT"
    test "$entry" = Run
    test "$collection" = "graph=$RUNNER_FIXTURE_ROOT/tools/graph"
    test "$llvm" = "$RUNNER_FIXTURE_LLVM_REAL"
    test "$target" = "$RUNNER_FIXTURE_TARGET"
    case "$output" in */CommandTests-Run) ;; *) exit 92 ;; esac
    printf 'contract-compile\n' >> "$SEED_EVENTS"
    cp "$RUNNER_FIXTURE_CONTRACT" "$output"
    chmod +x "$output"
    exit 0
fi
test "$project" = "$RUNNER_FIXTURE_ROOT/tools/compiler"
test "$llvm" = "$RUNNER_FIXTURE_LLVM"
test -z "$entry$collection"
case "$output" in
    "$RUNNER_FIXTURE_WORK/stage2/chibi")
        test "$0" = "$RUNNER_FIXTURE_STAGE"
        test "$target" = "$RUNNER_FIXTURE_TARGET"
        event=stage-two ;;
    "$RUNNER_FIXTURE_WORK/stage3/chibi")
        test "$0" = "$RUNNER_FIXTURE_FIXED"
        test "$target" = "$RUNNER_FIXTURE_TARGET"
        event=stage-three ;;
    "$RUNNER_FIXTURE_WINDOWS/chibi.exe")
        test "$0" = "$RUNNER_FIXTURE_FIXED"
        test "$target" = x86_64-pc-windows-msvc
        event=stage-two-windows ;;
    *) echo "unexpected fake-stage output: $output" >&2; exit 92 ;;
esac
if [ -n "${RUNNER_FIXTURE_PROGRESS:-}" ]; then
    grep -Fqx "$event: compiling with $0" "$RUNNER_FIXTURE_PROGRESS"
    if grep -Eq "^$event: [0-9]+ s, exit " "$RUNNER_FIXTURE_PROGRESS"; then exit 96; fi
fi
printf '%s\n' "$event" >> "$SEED_EVENTS"
if [ "${RUNNER_STAGE_STATUS:-0}" != 0 ]; then exit "$RUNNER_STAGE_STATUS"; fi
cp "$0" "$output"
chmod +x "$output"
STAGE
chmod +x "$scratch/stage"
export RUNNER_FIXTURE_ROOT="$PWD" RUNNER_FIXTURE_STAGE="$scratch/stage"
export RUNNER_FIXTURE_CONTRACT="$scratch/command-contract" RUNNER_FIXTURE_TARGET="$target"
export RUNNER_FIXTURE_LLVM="$llvm" RUNNER_FIXTURE_LLVM_REAL="$(cd "$llvm" && pwd -P)"
export RUNNER_FIXTURE_WORK="$scratch/stages" RUNNER_FIXTURE_WINDOWS="$scratch/windows"
export RUNNER_FIXTURE_FIXED="$RUNNER_FIXTURE_WORK/stage2/chibi" RUNNER_CONTRACT_STATUS=0

# Stage progress must be visible while the compiler is still running, even
# when that compiler fails. Preserve the exact status and stop later stages.
export RUNNER_FIXTURE_PROGRESS="$scratch/stage-progress" RUNNER_STAGE_STATUS=19
: > "$SEED_EVENTS"
if "$scratch/driver" build native-self-host --llvm "$llvm" --stage-one "$scratch/stage" \
    --work "$RUNNER_FIXTURE_WORK" > "$RUNNER_FIXTURE_PROGRESS" 2>&1; then exit 1; else test "$?" = 19; fi
printf 'stage-two\n' > "$scratch/expected-events"
cmp "$scratch/expected-events" "$SEED_EVENTS"
grep -Eq '^stage-two: [0-9]+ s, exit 19$' "$RUNNER_FIXTURE_PROGRESS"
unset RUNNER_FIXTURE_PROGRESS RUNNER_STAGE_STATUS

# The stage-two-only boundary still returns before the mandatory command
# contract, optional Windows stage, seed or comparison hosts.
: > "$SEED_EVENTS"
"$scratch/driver" build native-self-host --llvm "$llvm" --stage-one "$scratch/stage" \
    --work "$scratch/stages" --stage-two-only --compare-vm --windows "$RUNNER_FIXTURE_WINDOWS" \
    --build-host "$scratch/legacy-host" --compiler-host "$scratch/legacy-host"
printf 'stage-two\n' > "$scratch/expected-events"
cmp "$scratch/expected-events" "$SEED_EVENTS"

export RUNNER_FIXTURE_WORK="$scratch/stages-default" RUNNER_FIXTURE_WINDOWS="$scratch/windows-default"
export RUNNER_FIXTURE_FIXED="$RUNNER_FIXTURE_WORK/stage2/chibi"
: > "$SEED_EVENTS"
"$scratch/driver" build native-self-host --llvm "$llvm" --stage-one "$scratch/stage" \
    --work "$RUNNER_FIXTURE_WORK" --windows "$RUNNER_FIXTURE_WINDOWS" \
    --build-host "$scratch/legacy-host" --compiler-host "$scratch/legacy-host"
printf 'stage-two\nstage-three\nstage-two-windows\ncontract-compile\ncontract-run\n' > "$scratch/expected-events"
cmp "$scratch/expected-events" "$SEED_EVENTS"

# A failed mandatory contract retains its status and cannot fall back to
# a VM seed/host. Separate stage paths keep its preparation independently cold.
export RUNNER_FIXTURE_WORK="$scratch/stages-failed" RUNNER_FIXTURE_WINDOWS="$scratch/windows-failed"
export RUNNER_FIXTURE_FIXED="$RUNNER_FIXTURE_WORK/stage2/chibi" RUNNER_CONTRACT_STATUS=7
: > "$SEED_EVENTS"
if "$scratch/driver" build native-self-host --llvm "$llvm" --stage-one "$scratch/stage" \
    --work "$RUNNER_FIXTURE_WORK" --windows "$RUNNER_FIXTURE_WINDOWS" \
    --build-host "$scratch/legacy-host" --compiler-host "$scratch/legacy-host"; then exit 1; else test "$?" = 7; fi
cmp "$scratch/expected-events" "$SEED_EVENTS"

# The optional comparator still prepares its seed before either comparison
# host; seed status23 stops the route before contract/comparator execution.
export RUNNER_FIXTURE_WORK="$scratch/stages" RUNNER_FIXTURE_FIXED="$scratch/stages/stage2/chibi"
export RUNNER_CONTRACT_STATUS=0
: > "$SEED_EVENTS"
if "$scratch/driver" build native-self-host --llvm "$llvm" --stage-one "$scratch/stage" \
    --work "$scratch/stages" --compare-vm --build-host "$scratch/legacy-host" \
    --compiler-host "$scratch/legacy-host"; then exit 1; else test "$?" = 23; fi
printf 'stage-two\nstage-three\nseed tools/compiler/bootstrap\n' > "$scratch/expected-events"
cmp "$scratch/expected-events" "$SEED_EVENTS"

# Host-only SDK builds package compiler resources even when no asset generator
# runs. Prepare their seed before copying so later test preparation cannot
# make the source and staged compiler trees differ.
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/bootstrap/fixtures/CompilerResources.chibi" --entry Main \
    --collection "graph=$root/tools/graph" --output "$scratch/compiler-resources" --llvm "$llvm" --target "$target"
resources=$scratch/'resource source'
mkdir -p "$resources/tools/bootstrap" "$resources/tools/compiler/bootstrap" \
    "$resources/packages/core"
ln -s "$root/src" "$resources/src"
ln -s "$root/sdk" "$resources/sdk"
cat > "$resources/tools/bootstrap/seed.sh" <<'SEED'
#!/bin/sh
printf 'seed default\n' >> "$SEED_EVENTS"
if [ "${LEGACY_SEED_STATUS:-0}" != 0 ]; then exit "$LEGACY_SEED_STATUS"; fi
printf 'checked seed\n' > "$SEED_RESOURCE_ROOT/tools/compiler/bootstrap/compiler.chibip"
SEED
export SEED_RESOURCE_ROOT=$resources
: > "$SEED_EVENTS"
if "$scratch/compiler-resources" "$resources" "$scratch/resource-output"; then exit 1; else test "$?" = 1; fi
printf 'seed default\n' > "$scratch/expected-events"
cmp "$scratch/expected-events" "$SEED_EVENTS"
test ! -e "$scratch/resource-output/share/chibi/compiler"

# The controlled compiler tools are absent, so the host build still fails.
# Its resource phase must nevertheless copy the seed it just prepared.
export LEGACY_SEED_STATUS=0
: > "$SEED_EVENTS"
if "$scratch/compiler-resources" "$resources" "$scratch/resource-output"; then exit 1; else test "$?" = 1; fi
cmp "$scratch/expected-events" "$SEED_EVENTS"
cmp "$resources/tools/compiler/bootstrap/compiler.chibip" \
    "$scratch/resource-output/share/chibi/compiler/bootstrap/compiler.chibip"
printf 'legacy seed: native routes skip it; host resources prepare it before copying\n'
