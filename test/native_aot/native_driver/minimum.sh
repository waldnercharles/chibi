#!/bin/sh
# Unsupported LLVM tools fail before writing output or intermediates.
set -eu
driver=${1:?usage: minimum.sh DRIVER LLVM_BIN}
llvm=${2:?usage: minimum.sh DRIVER LLVM_BIN}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-llvm-minimum.XXXXXX")
trap 'rm -rf "$work"' EXIT
case $(uname -s)-$(uname -m) in
    Darwin-arm64) triple=aarch64-apple-darwin; linker=ld64.lld ;;
    Linux-x86_64) triple=x86_64-unknown-linux-gnu; linker=ld.lld ;;
    *) echo 'LLVM minimum: unsupported test host' >&2; exit 2 ;;
esac
mkdir "$work/tools" "$work/source"
cat > "$work/source/Main.chibi" <<'SOURCE'
package main
Main :: () -> int { return 42; }
SOURCE
for tool in opt llc "$linker"; do ln -s "$llvm/$tool" "$work/tools/$tool"; done
fail() { printf 'LLVM minimum: %s\n' "$*" >&2; exit 1; }
build() {
    "$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$work/source/Main.chibi" --output "$work/program" --llvm "$work/tools" \
        --target "$triple" --level 0 --jobs 2
}
reject() {
    printf 'unchanged\n' > "$work/program"
    status=0
    build > "$work/rejected.log" 2>&1 || status=$?
    [ "$status" = 2 ] || { cat "$work/rejected.log" >&2; fail 'unsupported tool was accepted'; }
    grep -F 'LLVM 22 or newer is required' "$work/rejected.log" >/dev/null || fail 'missing version diagnostic'
    [ "$(cat "$work/program")" = unchanged ] || fail 'tool rejection changed output'
    [ -z "$(find "$work" -name 'program.*' -print)" ] || fail 'tool rejection wrote intermediates'
}
for tool in opt llc "$linker"; do
    rm "$work/tools/$tool"
    case "$tool" in opt|llc) label='LLVM version ' ;; *) label='LLD ' ;; esac
    printf '#!/bin/sh\necho "%s21.1.8"\n' "$label" > "$work/tools/$tool"
    chmod +x "$work/tools/$tool"
    reject
    rm "$work/tools/$tool"
    ln -s "$llvm/$tool" "$work/tools/$tool"
done
rm "$work/tools/opt"
printf '#!/bin/sh\necho "unrecognized optimizer"\n' > "$work/tools/opt"
chmod +x "$work/tools/opt"
reject
printf '#!/bin/sh\nexit 7\n' > "$work/tools/opt"
reject
status=0
CHIBI_CORE="$root/packages/core" CHIBI_LLVM_BIN="$work/tools" "$driver" doctor > "$work/doctor-broken.log" 2>&1 || status=$?
[ "$status" = 1 ] || fail 'doctor accepted an unusable LLVM installation'
grep -F 'llvm tools: broken' "$work/doctor-broken.log" >/dev/null || fail 'doctor omitted broken LLVM status'
rm "$work/tools/opt"
ln -s "$llvm/opt" "$work/tools/opt"
build > "$work/supported.log" 2>&1 || { cat "$work/supported.log" >&2; fail 'supported tools rejected'; }
status=0
"$work/program" || status=$?
[ "$status" = 42 ] || fail 'supported program failed'
CHIBI_CORE="$root/packages/core" CHIBI_LLVM_BIN="$work/tools" "$driver" doctor > "$work/doctor-supported.log" 2>&1 || fail 'doctor rejected supported LLVM'
grep -F 'llvm tools: supported' "$work/doctor-supported.log" >/dev/null || fail 'doctor omitted supported LLVM status'
printf 'LLVM minimum: old, unknown and failed tool versions rejected; supported native program passed\n'
