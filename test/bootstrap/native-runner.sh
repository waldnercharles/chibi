#!/bin/sh
# Real native build programs must execute, cache, and fail without a VM host.
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd -P)
compiler=${1:?usage: native-runner.sh NATIVE_COMPILER LLVM_BIN}
llvm=${2:?usage: native-runner.sh NATIVE_COMPILER LLVM_BIN}
scratch=$(mktemp -d "${TMPDIR:-/tmp}/chibi-native-runner.XXXXXX")
trap 'rm -rf "$scratch"' EXIT
case "$(uname -s)" in
    Darwin) target=aarch64-apple-darwin ;;
    Linux) target=x86_64-unknown-linux-gnu ;;
    *) echo 'native-runner: unsupported test platform' >&2; exit 2 ;;
esac
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/tools/toolchain/Driver.chibi" --entry Main --output "$scratch/driver" \
    --llvm "$llvm" --target "$target"
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
test "$(wc -l < "$scratch/before" | tr -d ' ')" = 2
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
printf 'native runner: execution, reuse, rejection, cleanup and retry passed\n'
