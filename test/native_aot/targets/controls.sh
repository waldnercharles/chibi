#!/bin/sh
# Both consumers must reject the same malformed or incomplete real bundle.
# Kept beside the C differential until both native target jobs prove parity.
set -eu
native=$1
reference=$2
native_linker=$3
reference_linker=$4
bundle=$5
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-target-controls.XXXXXX")
trap 'rm -rf "$work"' EXIT
cp -R "$bundle" "$work/bundle"
directory=$work/bundle/macos-arm64
for manifest in modules native-build-programs native-link-programs; do
    cp "$directory/$manifest.txt" "$work/$manifest.txt"
done
reset() {
    for manifest in modules native-build-programs native-link-programs; do
        cp "$work/$manifest.txt" "$directory/$manifest.txt"
    done
}
printf 'module 0-total-0.o\ncall probe_Add -1 3 -9223372036854775808 +9223372036854775807 0\n' >"$directory/modules.txt"
"$native" "$work/bundle" "$native_linker"
"$reference" "$work/bundle" "$reference_linker"
reset
reject() {
    if "$native" "$work/bundle" "$native_linker" >"$work/native.log" 2>&1; then
        echo "native consumer accepted $1" >&2; exit 1
    else
        status=$?
        if [ "$status" -ne 1 ]; then cat "$work/native.log"; exit "$status"; fi
    fi
    if "$reference" "$work/bundle" "$reference_linker" >"$work/reference.log" 2>&1; then
        echo "C consumer accepted $1" >&2; exit 1
    else
        status=$?
        if [ "$status" -ne 1 ]; then cat "$work/reference.log"; exit "$status"; fi
    fi
    echo "Both consumers rejected $1"
    reset
}
rm "$directory/native-build-programs.txt"
reject missing-build-manifest
rm "$directory/native-link-programs.txt"
reject missing-link-manifest
printf 'call chibi_scalar 0 0\n' >"$directory/modules.txt"
reject call-before-module
sed '2s/.*/call chibi_scalar 0 4 1 2 3 4/' "$work/modules.txt" >"$directory/modules.txt"
reject too-many-arguments
sed '2s/.*/call chibi_scalar 0 2 1/' "$work/modules.txt" >"$directory/modules.txt"
reject missing-argument
printf 'module 0-total-0.o\ncall chibi_scalar 0 0' >"$directory/modules.txt"
reject missing-final-newline
sed '2s/chibi_scalar/missing_symbol/' "$work/modules.txt" >"$directory/modules.txt"
reject missing-symbol
sed '2s/ 0 0$/ 1 0/' "$work/modules.txt" >"$directory/modules.txt"
reject wrong-module-result
sed '$d' "$work/native-build-programs.txt" >"$directory/native-build-programs.txt"
reject omitted-program
sed 's/héllo €/other/' "$work/native-link-programs.txt" >"$directory/native-link-programs.txt"
reject wrong-unicode-argument
sed '1s/ 42$/ 41/' "$work/native-build-programs.txt" >"$directory/native-build-programs.txt"
reject wrong-program-exit
for value in 9223372036854775808 -9223372036854775809; do
    printf 'module 0-total-0.o\ncall probe_Add 0 1 %s\n' "$value" >"$directory/modules.txt"
    if "$native" "$work/bundle" "$native_linker" >"$work/native.log" 2>&1; then
        echo "native consumer accepted integer overflow $value" >&2; exit 1
    else
        status=$?
        if [ "$status" -ne 1 ]; then cat "$work/native.log"; exit "$status"; fi
    fi
    grep -q 'call argument is not a signed 64-bit integer' "$work/native.log"
    echo "Native consumer rejected integer overflow $value"
done
