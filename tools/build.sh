#!/bin/sh
# The engine build's one entry from a tree with no `chibi` yet: finds a build
# host (or fetches the pinned first one) and runs tools/toolchain/Driver.chibi,
# which is `chibi build` once chibi exists. Usage: tools/build.sh [build] OPTIONS
set -eu
root=$(cd "$(dirname "$0")/.." && pwd -P)
cd "$root"
[ "${1:-}" = build ] && shift
usable() {
    [ -x "$1" ] && [ "$("$1" --protocol-version 2>/dev/null)" = 14 ]
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
exec "$host" --compiler tools/compiler --compiler-core packages/core --root "$root" \
    --source tools/toolchain/Driver.chibi --core packages/core --entry Main \
    --timeout-ms 86400000 -- build "$@"
