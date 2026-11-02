#!/bin/bash
# Preparation only. Dependencies must already be installed; see docs/ci-maintenance.md.
set -euo pipefail
node .github/ci/budget.js "$(($(date +%s) + 1800))" bash -euo pipefail -s -- "$@" <<'SH'
[ "$#" = 1 ] || { echo 'usage: bootstrap-host.sh NEW_OUTPUT_DIRECTORY' >&2; exit 2; }
case "$(uname -s):$(uname -m)" in
  Linux:x86_64) platform=linux-x64 ;;
  Darwin:arm64) platform=macos-arm64 ;;
  *) echo 'Only Linux x64 and macOS arm64 hosts are supported' >&2; exit 2 ;;
esac
: "${LIBCLANG_ROOT:?Set LIBCLANG_ROOT to the installed LLVM prefix}"
: "${CC:?Set CC to the LLVM C compiler}" "${CXX:?Set CXX to the LLVM C++ compiler}"
test -z "$(git status --porcelain --untracked-files=normal)"
revision=$(git rev-parse HEAD)
tree=$(git rev-parse 'HEAD^{tree}')
mkdir "$1"
out=$(cd "$1" && pwd -P)
export NATIVE_TOOLCHAIN_FLAGS="-DCMAKE_C_COMPILER=$CC -DCMAKE_CXX_COMPILER=$CXX"
sh tools/build.sh build --release --target chibi-build-host --libclang-root "$LIBCLANG_ROOT" 2>&1 | tee "$out/build.log"
test "$(build-release/sdk/chibi-build-host --protocol-version)" = 15
asset="chibi-build-host-$platform"
cp build-release/sdk/chibi-build-host "$out/$asset"
test "$(git rev-parse HEAD)" = "$revision"
test "$(git rev-parse 'HEAD^{tree}')" = "$tree"
test -z "$(git status --porcelain --untracked-files=normal)"
(cd "$out" && shasum -a 256 "$asset" > SHA256SUMS)
digest=$(cut -d' ' -f1 "$out/SHA256SUMS")
printf '%s\tbootstrap-host-%s\t%s\t%s\n' "$platform" "$revision" "$asset" "$digest" | tee "$out/hosts.tsv"
printf 'revision=%s\ntree=%s\nplatform=%s\nprotocol=15\n' "$revision" "$tree" "$platform" > "$out/source.txt"
SH
