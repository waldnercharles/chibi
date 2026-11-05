#!/bin/sh
set -eu
compiler=${1:?usage: run.sh NATIVE_COMPILER LLVM_BIN}
llvm=${2:?usage: run.sh NATIVE_COMPILER LLVM_BIN}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu ;;
    *) echo 'native POSIX stamp: unsupported test host' >&2; exit 2 ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-posix-stamp.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir "$work/source" "$work/directory"
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
    python3 - "$work/probe-$level" "$@" <<'PY'
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

paths = sys.argv[2:]
expected = "".join(stamp(path) + "\n" for path in paths).encode()
for attempt in range(2):
    actual = subprocess.check_output([sys.argv[1], *paths])
    assert actual == expected, (actual, expected)
assert "".join(stamp(path) + "\n" for path in paths).encode() == expected
assert open(paths[0], "rb").read() == b"abc"
PY
    printf 'PASS full-width POSIX stamps and real stat O%s\n' "$level"
done
