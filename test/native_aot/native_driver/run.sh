#!/bin/sh
# Exercises the actual native compiler driver and LLVM, without a C compiler.
# Usage: run.sh NATIVE_DRIVER LLVM_BIN
set -eu
driver=${1:?usage: run.sh NATIVE_DRIVER LLVM_BIN}
llvm=${2:?usage: run.sh NATIVE_DRIVER LLVM_BIN}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-driver-options.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir "$work/source with spaces"
source=$work/source\ with\ spaces
core=$root/packages/core

fail() {
    printf 'native driver options: %s\n' "$*" >&2
    exit 1
}
build() {
    input=$1
    output=$2
    shift 2
    env -i PATH= "$driver" --compiler "$root/tools/compiler" --core "$core" \
        --source "$input" --output "$output" --llvm "$llvm" --jobs 2 "$@"
}
runs() {
    expected=$1
    shift
    status=0
    env -i PATH= "$@" || status=$?
    [ "$status" = "$expected" ] || fail "program exited $status, expected $expected"
}
rejects() {
    expected=$1
    message=$2
    input=$3
    shift 3
    printf 'unchanged\n' >"$work/rejected"
    status=0
    build "$input" "$work/rejected" "$@" >"$work/rejection.log" 2>&1 || status=$?
    [ "$status" = "$expected" ] || { cat "$work/rejection.log" >&2; fail "rejection exited $status, expected $expected"; }
    grep -F "$message" "$work/rejection.log" >/dev/null || { cat "$work/rejection.log" >&2; fail "missing diagnostic: $message"; }
    [ "$(cat "$work/rejected")" = unchanged ] || fail 'a rejection changed the output'
    [ -z "$(find "$work" -name 'rejected.*' -print)" ] || fail 'a rejection wrote intermediate output'
}

cat >"$source/Scalar.chibi" <<'SOURCE'
package scalar
Main :: () -> int { return Twice(21); }
Twice :: (value: int) -> int { return value * 2; }
SOURCE
cat >"$source/Allocates.chibi" <<'SOURCE'
package allocates
Main :: () -> int {
    values: int[] = [40];
    values.append(2);
    return values[0] + values[1];
}
SOURCE
cat >"$source/Libc.chibi" <<'SOURCE'
package libc_probe
foreign import libc "system:c"
foreign libc { abs :: (value: i32) -> i32 }
Main :: () -> int { return int(abs(i32(-42))); }
SOURCE

for level in 0 2; do
    build "$source/Scalar.chibi" "$work/dynamic-$level" --level "$level"
    runs 42 "$work/dynamic-$level"
    "$llvm/llvm-readobj" --program-headers "$work/dynamic-$level" >"$work/headers"
    grep 'PT_INTERP' "$work/headers" >/dev/null || fail 'Linux default is not dynamic'
    build "$source/Scalar.chibi" "$work/static-$level" --level "$level" --static
    runs 42 "$work/static-$level"
    "$llvm/llvm-readobj" --program-headers "$work/static-$level" >"$work/headers"
    if grep 'PT_INTERP' "$work/headers" >/dev/null; then fail '--static linked dynamically'; fi
    build "$source/Libc.chibi" "$work/libc-$level" --level "$level"
    runs 42 "$work/libc-$level"
    build "$source/Allocates.chibi" "$work/allocates-$level" --level "$level"
    runs 42 "$work/allocates-$level"
done
printf 'native driver options: O0/O2 dynamic, static, libc and allocation programs passed\n'

build "$source/Scalar.chibi" "$work/macos" --target aarch64-apple-darwin
"$llvm/llvm-readobj" --file-headers "$work/macos" >"$work/headers"
grep 'Format: Mach-O arm64' "$work/headers" >/dev/null || fail 'macOS target has the wrong format'
build "$source/Scalar.chibi" "$work/windows.exe" --target x86_64-pc-windows-msvc
"$llvm/llvm-readobj" --file-headers "$work/windows.exe" >"$work/headers"
grep 'Format: COFF-x86-64' "$work/headers" >/dev/null || fail 'Windows target has the wrong format'
printf 'native driver options: macOS and Windows cross-link formats passed (not executed)\n'

build "$source/Scalar.chibi" "$work/lines" --level 0 --lines
runs 42 "$work/lines"
"$llvm/llvm-dwarfdump" --verify "$work/lines"
"$llvm/llvm-dwarfdump" --debug-line "$work/lines" >"$work/lines.txt"
grep 'Scalar.chibi' "$work/lines.txt" >/dev/null || fail 'line table has no source file'
printf 'native driver options: line tables passed\n'

cat >"$source/Probe.ll" <<'SOURCE'
target triple = "x86_64-unknown-linux-gnu"
define i32 @probe_add(i32 %a, i32 %b) {
  %sum = add i32 %a, %b
  ret i32 %sum
}
SOURCE
"$llvm/llc" -filetype=obj -relocation-model=pic "$source/Probe.ll" -o "$source/Probe.o"
"$llvm/llvm-ar" rcs "$source/libprobe.a" "$source/Probe.o"
cat >"$source/Archive.chibi" <<'SOURCE'
package archive_probe
foreign import probe "libprobe.a"
foreign probe { probe_add :: (a: i32, b: i32) -> i32 }
Main :: () -> int { return int(probe_add(20, 22)); }
SOURCE
build "$source/Archive.chibi" "$work/archive" --static
runs 42 "$work/archive"

sdk=$work/sdk\ with\ spaces
mkdir -p "$sdk/share/chibi" "$sdk/lib/chibi-vendor"
ln -s "$core" "$sdk/share/chibi/core"
cp "$source/libprobe.a" "$sdk/lib/chibi-vendor/libchibi_vendor_probe.a"
sed 's/"libprobe.a"/"vendor:probe"/' "$source/Archive.chibi" >"$source/Vendor.chibi"
core=$sdk/share/chibi/core
build "$source/Vendor.chibi" "$work/vendor" --static
runs 42 "$work/vendor"
core=$root/packages/core
printf 'native driver options: relative and relocated vendor archives passed\n'

cat >"$source/Library.chibi" <<'SOURCE'
Probe :: library {
    Main :: () -> int { return 42; }
}
SOURCE
cat >"$source/Load.chibi" <<'SOURCE'
package load
foreign import libc "system:c"
foreign libc {
    dlopen :: (path: cstring, flags: i32) -> rawptr
    dlsym :: (handle: rawptr, name: cstring) -> rawptr
    dlclose :: (handle: rawptr) -> i32
}
Main :: () -> int {
    handle := dlopen("./probe.so", 2);
    if (handle == 0) { return 90; }
    address := dlsym(handle, "chibi_host_call_Main");
    if (address == 0) { return 91; }
    call: fn() -> i32 = address;
    result := call();
    if (dlclose(handle) != 0) { return 92; }
    return int(result);
}
SOURCE
build "$source/Load.chibi" "$work/load"
for level in 0 2; do
    build "$source/Library.chibi" "$work/probe.so" --level "$level" --shared
    (cd "$work" && runs 42 "$work/load")
done
printf 'native driver options: O0/O2 shared libraries loaded and called\n'

rejects 2 'usage:' "$source/Scalar.chibi" --level 3
rejects 2 'Linux x86-64 only' "$source/Scalar.chibi" --lines --target aarch64-apple-darwin
rejects 2 'no line tables or static link' "$source/Library.chibi" --shared --lines
rejects 2 'no line tables or static link' "$source/Library.chibi" --shared --static
rejects 2 'omit --static' "$source/Allocates.chibi" --static
rejects 2 'and only it' "$source/Scalar.chibi" --shared
rejects 1 'one source and no --entry' "$source/Library.chibi" --shared --entry Main
sed 's/"libprobe.a"/"missing.a"/' "$source/Archive.chibi" >"$source/Missing.chibi"
rejects 2 'is not a file' "$source/Missing.chibi"
printf 'native driver options: invalid inputs preserve outputs\n'
printf 'native driver options: all checks passed\n'
