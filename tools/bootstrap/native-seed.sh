#!/bin/sh
# Fetches the native compiler tools/bootstrap/native-seed.tsv pins for PLATFORM
# (linux-x64 or macos-arm64) to OUTPUT and checks its SHA-256. Exits 3 when the
# platform has no pin.
# Usage: tools/bootstrap/native-seed.sh PLATFORM OUTPUT
set -eu
platform=${1:?usage: tools/bootstrap/native-seed.sh PLATFORM OUTPUT}
output=${2:?usage: tools/bootstrap/native-seed.sh PLATFORM OUTPUT}
root=$(cd "$(dirname "$0")/../.." && pwd -P)
pin=$(awk -F '\t' -v platform="$platform" '$1 == platform { print $2 "\t" $3 "\t" $4 }' "$root/tools/bootstrap/native-seed.tsv")
if [ -z "$pin" ]; then
    printf 'native-seed: tools/bootstrap/native-seed.tsv pins no compiler for %s\n' "$platform" >&2
    exit 3
fi
tag=$(printf '%s' "$pin" | cut -f1)
asset=$(printf '%s' "$pin" | cut -f2)
sha256=$(printf '%s' "$pin" | cut -f3)
# A warm entry verifies the existing bytes against the current pin. A changed
# pin or corrupted file is fetched again; an unchecked executable is never run.
if [ -f "$output" ]; then
    if command -v sha256sum >/dev/null 2>&1; then
        actual=$(sha256sum "$output" | cut -d' ' -f1)
    else
        actual=$(shasum -a 256 "$output" | cut -d' ' -f1)
    fi
    if [ "$actual" = "$sha256" ]; then
        chmod +x "$output"
        exit 0
    fi
fi
download=$(mktemp -d "${TMPDIR:-/tmp}/chibi-native-seed.XXXXXX")
trap 'rm -rf "$download"' EXIT
gh release download "$tag" --repo "${CHIBI_REPOSITORY:-waldnercharles/chibi}" --pattern "$asset" --dir "$download"
if command -v sha256sum >/dev/null 2>&1; then
    actual=$(sha256sum "$download/$asset" | cut -d' ' -f1)
else
    actual=$(shasum -a 256 "$download/$asset" | cut -d' ' -f1)
fi
if [ "$actual" != "$sha256" ]; then
    printf 'native-seed: %s has SHA-256 %s, not the pinned %s\n' "$asset" "$actual" "$sha256" >&2
    exit 1
fi
mkdir -p "$(dirname "$output")"
chmod +x "$download/$asset"
mv "$download/$asset" "$output"
