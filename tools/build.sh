#!/bin/sh
# The engine build entry before chibi is installed. A verified native seed
# compiles and caches tools/toolchain/Driver.chibi. No VM host is used.
# Usage: tools/build.sh [build] OPTIONS [--native-driver PATH] [--llvm DIR]
set -eu
root=$(cd "$(dirname "$0")/.." && pwd -P)
cd "$root"
[ "${1:-}" = build ] && shift
# An explicit driver overrides the pinned seed. LLVM comes from --llvm,
# CHIBI_LLVM_BIN, or the directory containing opt on PATH.
native_driver=
llvm=
option=
native_given=
for argument do
    case "$option" in
        --native-driver) native_driver=$argument; option= ;;
        --llvm) llvm=$argument; option= ;;
        *)
            case "$argument" in
                --native-driver) [ -n "$native_given" ] || option=$argument; native_given=yes ;;
                --llvm) [ -n "$llvm" ] || option=$argument ;;
            esac ;;
    esac
done
if [ -n "$option" ] || { [ -n "$native_given" ] && [ -z "$native_driver" ]; }; then
    echo 'tools/build.sh: --native-driver and --llvm require a value' >&2
    exit 2
fi
if [ -z "$llvm" ]; then
    llvm=${CHIBI_LLVM_BIN:-}
    if [ -z "$llvm" ]; then
        opt=$(command -v opt || true)
        [ -z "$opt" ] || llvm=$(dirname "$opt")
    fi
    if [ -z "$llvm" ]; then
        echo 'tools/build.sh: pass --llvm DIR or set CHIBI_LLVM_BIN to the LLVM tool directory' >&2
        exit 2
    fi
    set -- "$@" --llvm "$llvm"
fi
case "$llvm" in /*) ;; *) llvm=$root/$llvm ;; esac
case "$(uname -s)-$(uname -m)" in
    Linux-x86_64) platform=linux-x64; triple=x86_64-unknown-linux-gnu; linker=ld.lld ;;
    Darwin-arm64) platform=macos-arm64; triple=aarch64-apple-darwin; linker=ld64.lld ;;
    *) echo 'tools/build.sh: no pinned native build entry for this platform' >&2; exit 2 ;;
esac
for tool in "$llvm/opt" "$llvm/llc" "$llvm/$linker"; do
    if [ ! -x "$tool" ]; then
        printf 'tools/build.sh: native build entry needs executable %s\n' "$tool" >&2
        exit 2
    fi
done
if [ -z "$native_given" ]; then
    native_driver=$root/.chibi/native-seed/$platform/chibi
    sh tools/bootstrap/native-seed.sh "$platform" "$native_driver"
    set -- "$@" --native-driver "$native_driver"
fi
case "$native_driver" in /*) ;; *) native_driver=$root/$native_driver ;; esac
if [ ! -x "$native_driver" ]; then
    printf 'tools/build.sh: native build entry needs executable %s\n' "$native_driver" >&2
    exit 2
fi
# The core sources, entry and four tool binaries decide its bytes. Hash
# contents, not mtimes, so restored checkouts and in-place edits invalidate.
# Full hashes also name the cache; a partial compile is never published.
if command -v sha256sum >/dev/null 2>&1; then
    digest=sha256sum
else
    digest='shasum -a 256'
fi
mkdir -p .chibi/build-entry
work=$(mktemp -d "$root/.chibi/build-entry/work.XXXXXX")
trap 'rm -rf "$work"' EXIT
printf '%s\n' "$triple" "$native_driver" "$llvm" > "$work/inputs"
$digest tools/toolchain/Driver.chibi "$native_driver" "$llvm/opt" "$llvm/llc" "$llvm/$linker" >> "$work/inputs"
# The fixed digest command above includes shasum's two arguments.
# shellcheck disable=SC2086
find packages/core -type f -name '*.chibi' -exec $digest {} + > "$work/core"
LC_ALL=C sort "$work/core" >> "$work/inputs"
hashed=$($digest "$work/inputs")
key=${hashed%% *}
case "$key" in
    ''|*[!0-9a-f]*) echo 'tools/build.sh: cannot hash the native build entry inputs' >&2; exit 1 ;;
esac
if [ "${#key}" -ne 64 ]; then
    echo 'tools/build.sh: cannot hash the native build entry inputs' >&2
    exit 1
fi
cached=$root/.chibi/build-entry/$key
if [ ! -x "$cached/driver" ]; then
    "$native_driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/tools/toolchain/Driver.chibi" --entry Main --output "$work/driver" \
        --llvm "$llvm" --target "$triple"
    test -x "$work/driver"
    mkdir -p "$cached"
    mv "$work/driver" "$cached/driver"
fi
rm -rf "$work"
trap - EXIT
exec "$cached/driver" build "$@"
