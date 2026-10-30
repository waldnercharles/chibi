#!/bin/sh
# Fetches the bootstrap seed, the compiled compiler that compiles the compiler
# sources, as Zig ships zig1.wasm: it is a release asset, not committed.
# bootstrap.json pins its SHA-256 and seed.tsv names the release that holds it.
# A seed that already matches bootstrap.json is kept, so one that
# `make bootstrap-artifact` just wrote stays until it is published.
# Usage: tools/bootstrap/seed.sh [DIRECTORY], default tools/compiler/bootstrap
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd -P)
directory=${1:-$root/tools/compiler/bootstrap}
manifest=$directory/bootstrap.json
seed=$directory/compiler.chibip
digest() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}
# The closure's digest is the first sha256 in the manifest, before the sources'.
sha256=$(sed -n 's/.*"sha256": *"\([0-9a-f]\{64\}\)".*/\1/p' "$manifest" | head -n 1)
if [ -z "$sha256" ]; then
    printf 'seed: %s pins no closure SHA-256\n' "$manifest" >&2
    exit 2
fi
if [ -f "$seed" ] && [ "$(digest "$seed")" = "$sha256" ]; then
    exit 0
fi
pin=$(awk -F '\t' -v sha256="$sha256" '$3 == sha256 { print $1 "\t" $2 }' "$root/tools/bootstrap/seed.tsv")
if [ -z "$pin" ]; then
    printf 'seed: tools/bootstrap/seed.tsv names no release for the seed %s that %s pins\n' "$sha256" "$manifest" >&2
    exit 2
fi
tag=$(printf '%s' "$pin" | cut -f1)
asset=$(printf '%s' "$pin" | cut -f2)
download=$(mktemp -d "${TMPDIR:-/tmp}/chibi-seed.XXXXXX")
trap 'rm -rf "$download"' EXIT
gh release download "$tag" --repo "${CHIBI_REPOSITORY:-waldnercharles/chibi}" --pattern "$asset" --dir "$download"
actual=$(digest "$download/$asset")
if [ "$actual" != "$sha256" ]; then
    printf 'seed: %s has SHA-256 %s, not the pinned %s\n' "$asset" "$actual" "$sha256" >&2
    exit 1
fi
mv "$download/$asset" "$seed"
