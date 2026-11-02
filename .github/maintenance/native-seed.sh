#!/bin/bash
# Preparation only; native-self-host owns the unchanged stage 2/3/4 fixed-point bound.
set -euo pipefail
node .github/ci/budget.js "$(($(date +%s) + 10800))" bash -euo pipefail -s -- "$@" <<'SH'
[ "$#" = 1 ] || { echo 'usage: native-seed.sh NEW_OUTPUT_DIRECTORY' >&2; exit 2; }
case "$(uname -s):$(uname -m)" in
  Linux:x86_64) platform=linux-x64 ;;
  Darwin:arm64) platform=macos-arm64 ;;
  *) echo 'Only Linux x64 and macOS arm64 seeds are supported' >&2; exit 2 ;;
esac
: "${LLVM_BIN:?Set LLVM_BIN to the LLVM tool directory}" "${LIBCLANG_ROOT:?Set LIBCLANG_ROOT}"
test -z "$(git status --porcelain --untracked-files=normal)"
revision=$(git rev-parse HEAD)
tree=$(git rev-parse 'HEAD^{tree}')
mkdir "$1"
out=$(cd "$1" && pwd -P)
stage_one=()
if awk -F '\t' -v platform="$platform" '$1 == platform { found=1 } END { exit !found }' tools/bootstrap/native-seed.tsv; then
  sh tools/bootstrap/native-seed.sh "$platform" "$out/previous/chibi"
  stage_one=(--stage-one "$out/previous/chibi")
fi
sh tools/build.sh build native-self-host --llvm "$LLVM_BIN" --libclang-root "$LIBCLANG_ROOT" --jobs 4 --skip-vm "${stage_one[@]}" 2>&1 | tee "$out/native-seed.log"
fixed=$(sed -n 's/^native-self-host: the fixed point is //p' "$out/native-seed.log")
test -n "$fixed" && test -x "$fixed"
asset="chibi-native-$platform"
cp "$fixed" "$out/$asset"
test "$(git rev-parse HEAD)" = "$revision"
test "$(git rev-parse 'HEAD^{tree}')" = "$tree"
test -z "$(git status --porcelain --untracked-files=normal)"
(cd "$out" && shasum -a 256 "$asset" > SHA256SUMS)
digest=$(cut -d' ' -f1 "$out/SHA256SUMS")
printf '%s\tnative-seed-%s\t%s\t%s\n' "$platform" "$revision" "$asset" "$digest" | tee "$out/native-seed.tsv"
printf 'revision=%s\ntree=%s\nplatform=%s\nfixed_point=%s\n' "$revision" "$tree" "$platform" "$fixed" > "$out/source.txt"
SH
