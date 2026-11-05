#!/bin/sh
set -eu
compiler=${1:?usage: run.sh NATIVE_COMPILER LLVM_BIN}
llvm=${2:?usage: run.sh NATIVE_COMPILER LLVM_BIN}
cc=${CC:-cc}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin; strip=-Wl,-dead_strip ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu; strip=-Wl,--gc-sections ;;
    *) echo 'native POSIX stamp: unsupported test host' >&2; exit 2 ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-posix-stamp.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
# The C host uses root "/"; keep the same paths with a relative TMPDIR too.
work=$(cd "$work" && pwd -P)
mkdir "$work/source" "$work/directory"
# Link the real host stamp and allocator, discarding unrelated host operations.
"$cc" -std=gnu2x -O2 -D_DARWIN_C_SOURCE -Wall -Wextra -Wpedantic -Werror \
    -ffunction-sections -fdata-sections -I"$root/src" \
    "$root/test/native_aot/posix_stamp/host.c" \
    "$root/src/host/build_io.c" "$root/src/support/xalloc.c" \
    "$strip" -o "$work/host"
# Exercise the exact private decoding/formatting, not a test-side replacement.
cat "$root/packages/core/sys/posix/Posix.chibi" > "$work/source/Values.chibi"
sed '1d' "$root/test/native_aot/posix_stamp/Values.chibi" >> "$work/source/Values.chibi"
printf 'abc' > "$work/file"
: > "$work/empty"
chmod 640 "$work/file"
ln -s file "$work/link"
ln -s missing "$work/broken"
set -- "$work/file" "$work/empty" "$work/directory" "$work/link" "$work/missing" "$work/broken" "$work/file/child"
for level in 0 2; do
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$work/source/Values.chibi" --entry Main --output "$work/values-$level" \
        --llvm "$llvm" --target "$target" --level "$level" --jobs 1
    "$work/values-$level"
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/posix_stamp/Probe.chibi" --entry Main --output "$work/probe-$level" \
        --llvm "$llvm" --target "$target" --level "$level" --jobs 1
    python3 - "$work/probe-$level" "$work/host" "$@" <<'PY'
import os
import subprocess
import sys

def stamp(path):
    try:
        info = os.stat(path)
    except (FileNotFoundError, NotADirectoryError):
        return ""
    modified, nanos = divmod(info.st_mtime_ns, 1_000_000_000)
    changed, changed_nanos = divmod(info.st_ctime_ns, 1_000_000_000)
    # Match build_io_stamp's uintmax_t conversion, including signed Darwin dev_t.
    device = info.st_dev % (1 << 64)
    return f"{device}:{info.st_ino}:{info.st_size}:{info.st_mode:o}:{modified}.{nanos:09d}:{changed}.{changed_nanos:09d}"

paths = sys.argv[3:]
stamps = [stamp(path) for path in paths]
assert all(stamps[:4]) and stamps[4:] == ["", "", ""]
expected = "".join(value + "\n" for value in stamps).encode()
for probe in sys.argv[1:3]:
    for attempt in range(2):
        actual = subprocess.check_output([probe, *paths])
        assert actual == expected, (probe, actual, expected)
assert "".join(stamp(path) + "\n" for path in paths).encode() == expected
assert open(paths[0], "rb").read() == b"abc"
PY
    printf 'PASS full-width POSIX stamps and C/native/Python stat O%s\n' "$level"
done
