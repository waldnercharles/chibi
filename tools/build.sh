#!/bin/sh
# The engine build entry before chibi is installed. A verified native seed
# compiles and caches tools/toolchain/Driver.chibi. No VM host is used.
# Usage: tools/build.sh [build] OPTIONS [--native-driver PATH] [--llvm DIR]
set -eu
root=$(cd "$(dirname "$0")/.." && pwd -P)
cd "$root"
[ "${1:-}" = build ] && shift
# An explicit driver overrides the pinned seed. LLVM comes from --llvm,
# CHIBI_LLVM_BIN, or the directory containing opt on PATH. First value wins;
# every occurrence, including ignored duplicates, needs a nonempty value before --.
native_driver=
llvm=
option=
native_given=
llvm_given=
for argument do
    [ "$argument" != -- ] || break
    if [ -n "$option" ] && [ -z "$argument" ]; then break; fi
    case "$option" in
        --native-driver)
            [ -n "$native_given" ] || native_driver=$argument
            native_given=yes; option= ;;
        --llvm)
            [ -n "$llvm_given" ] || llvm=$argument
            llvm_given=yes; option= ;;
        *)
            case "$argument" in
                --native-driver|--llvm) option=$argument ;;
            esac ;;
    esac
done
if [ -n "$option" ]; then
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
fi
# Keep the inferred spelling for the graph driver; bootstrap paths are absolute.
llvm_argument=$llvm
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
fi
case "$native_driver" in /*) ;; *) native_driver=$root/$native_driver ;; esac
if [ ! -x "$native_driver" ]; then
    printf 'tools/build.sh: native build entry needs executable %s\n' "$native_driver" >&2
    exit 2
fi
# The core and graph sources, entry and four tool binaries decide its bytes. Hash
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
printf '%s\n' 'chibi-native-build-entry 3 LLVM22 graph' "$triple" "$native_driver" "$llvm" > "$work/inputs"
$digest tools/toolchain/Driver.chibi "$native_driver" "$llvm/opt" "$llvm/llc" "$llvm/$linker" >> "$work/inputs"
# The fixed digest command above includes shasum's two arguments.
# shellcheck disable=SC2086
find packages/core tools/graph -type f -name '*.chibi' -exec $digest {} + > "$work/sources"
LC_ALL=C sort "$work/sources" >> "$work/inputs"
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
    # This schema's cache entries were compiled only after all required tools
    # established LLVM/LLD 22 or newer. Hashed tool bytes make warm hits trusted.
    for tool in opt llc "$linker"; do
        if ! reported=$("$llvm/$tool" --version 2>&1); then
            printf 'tools/build.sh: LLVM 22 or newer is required; cannot query %s: %s\n' "$llvm/$tool" "$reported" >&2
            exit 2
        fi
        label='LLVM version '
        [ "$tool" = "$linker" ] && label='LLD '
        major=$(printf '%s\n' "$reported" | sed -n "s/.*$label\([0-9][0-9]*\).*/\1/p")
        case "$major" in
            ''|*[!0-9]*) major=0 ;;
        esac
        if [ "${#major}" -gt 4 ] || [ "$major" -lt 22 ]; then
            printf 'tools/build.sh: LLVM 22 or newer is required; cannot use %s: %s\n' "$llvm/$tool" "$reported" >&2
            exit 2
        fi
    done
    printf 'tools/build.sh: compiling native build entry with %s\n' "$native_driver" >&2
    "$native_driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/tools/toolchain/Driver.chibi" --entry Main --output "$work/driver" \
        --collection "graph=$root/tools/graph" \
        --llvm "$llvm" --target "$triple"
    test -x "$work/driver"
    mkdir -p "$cached"
    mv "$work/driver" "$cached/driver"
    printf 'tools/build.sh: native build entry ready\n' >&2
fi
rm -rf "$work"
trap - EXIT
# Main dispatches on the first argument after build. Put inferred options after
# that verb, before any `--`, without re-parsing or rebuilding the original tail.
verb=
case "${1:-}" in
    ''|--*) ;;
    *) verb=$1; shift ;;
esac
[ -n "$native_given" ] || set -- --native-driver "$native_driver" "$@"
[ -n "$llvm_given" ] || set -- --llvm "$llvm_argument" "$@"
[ -z "$verb" ] || set -- "$verb" "$@"
exec "$cached/driver" build "$@"
