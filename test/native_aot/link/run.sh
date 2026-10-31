#!/bin/sh
# Runs Smoke.chibi under the build host given as $1. The build host reads
# collections, not project packages, so the two packages are staged as
# collections with their imports of each other renamed to match.
set -eu
host=$1
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-native-link.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir "$work/native_link" "$work/native_link_host"
cp "$root/tools/compiler/native_link/Link.chibi" "$work/native_link/"
sed 's#"project:native_link"#"native_link:."#' "$root/tools/compiler/native_link_host/Host.chibi" >"$work/native_link_host/Host.chibi"
"$host" --root "$root" --source "$root/test/native_aot/link/Smoke.chibi" \
    --collection native_link="$work/native_link" --collection native_link_host="$work/native_link_host" \
    --core "$root/packages/core" --compiler "$root/tools/compiler" --compiler-core "$root/packages/core" \
    --entry Run --timeout-ms 600000
