#!/bin/sh
# The engine build's one entry from a tree with no `chibi` yet: finds a build
# host (or fetches the pinned first one) and runs tools/toolchain/Driver.chibi,
# which is `chibi build` once chibi exists. Usage: tools/build.sh [build] OPTIONS
# With --native-driver and --llvm, the entry itself runs natively too.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd -P)
cd "$root"
[ "${1:-}" = build ] && shift
# The existing native-runner options select the native entry before fetching
# any VM host or bytecode seed. Downstream commands may still need those hosts.
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
if [ -n "$native_given" ]; then
    if [ -z "$native_driver" ] || [ -z "$llvm" ] || [ -n "$option" ]; then
        echo 'tools/build.sh: --native-driver requires a program and --llvm DIR' >&2
        exit 2
    fi
    case "$native_driver" in /*) ;; *) native_driver=$root/$native_driver ;; esac
    case "$llvm" in /*) ;; *) llvm=$root/$llvm ;; esac
    case "$(uname -s)-$(uname -m)" in
        Linux-x86_64) triple=x86_64-unknown-linux-gnu; linker=ld.lld ;;
        Darwin-arm64) triple=aarch64-apple-darwin; linker=ld64.lld ;;
        *) echo 'tools/build.sh: no native build entry for this platform' >&2; exit 2 ;;
    esac
    for program in "$native_driver" "$llvm/opt" "$llvm/llc" "$llvm/$linker"; do
        if [ ! -x "$program" ]; then
            printf 'tools/build.sh: native build entry needs executable %s\n' "$program" >&2
            exit 2
        fi
    done
    # 35 small core sources plus the entry and four tool binaries today. Hash
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
fi
# A host built before the pinned first build host last changed may read
# another native catalog or prepared format than the seed, which re-pins it.
pinned=$(git log -1 --format=%ct -- tools/bootstrap/hosts.tsv 2>/dev/null || true)
usable() {
    [ -x "$1" ] && [ "$("$1" --protocol-version 2>/dev/null)" = 15 ] || return 1
    [ -z "$pinned" ] || [ "$(date -r "$1" +%s)" -ge "$pinned" ] && return 0
    echo "tools/build.sh: not using $1, built before tools/bootstrap/hosts.tsv last changed" >&2
    return 1
}
host=${CHIBI_BUILD_HOST:-}
if [ -z "$host" ]; then
    for candidate in build/sdk/chibi-build-host build-release/sdk/chibi-build-host build-debug/sdk/chibi-build-host; do
        if usable "$candidate"; then
            host=$candidate
            break
        fi
    done
fi
if [ -z "$host" ]; then
    host=build-release/sdk/chibi-build-host
    sh tools/bootstrap.sh "$host"
fi
sh tools/bootstrap/seed.sh
exec "$host" --compiler tools/compiler --compiler-core packages/core --root "$root" \
    --source tools/toolchain/Driver.chibi --core packages/core --entry Main \
    --timeout-ms 86400000 -- build "$@"
