#!/bin/sh
# Fetches the native compiler tools/bootstrap/native-seed.tsv pins for PLATFORM
# to OUTPUT and checks its SHA-256. Exits 3 with no pin, 2 for an invalid
# manifest, and 1 for a checksum failure. Download failures keep gh's status.
# Usage: tools/bootstrap/native-seed.sh PLATFORM OUTPUT
set -eu
platform=${1:?usage: tools/bootstrap/native-seed.sh PLATFORM OUTPUT}
output=${2:?usage: tools/bootstrap/native-seed.sh PLATFORM OUTPUT}
root=$(cd "$(dirname "$0")/../.." && pwd -P)
pin=$(awk -F '\t' -v platform="$platform" '
    { sub(/\r$/, "") }
    /^#/ || /^$/ { next }
    {
        if (NF != 4 || $1 !~ /^(linux-x64|macos-arm64|windows-x64)$/ ||
            $2 !~ /^[A-Za-z0-9][A-Za-z0-9._-]*$/ || $3 !~ /^[A-Za-z0-9][A-Za-z0-9._-]*$/ ||
            length($4) != 64 || $4 ~ /[^0-9a-f]/ || seen[$1]++) {
            printf "native-seed: tools/bootstrap/native-seed.tsv:%d has an invalid or duplicate pin\n", NR > "/dev/stderr"
            failed = 1
        }
        if ($1 == platform) pin = $2 "\t" $3 "\t" $4
    }
    END { if (failed) exit 2; if (pin != "") print pin }
' "$root/tools/bootstrap/native-seed.tsv")
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
mkdir -p "$(dirname "$output")"
download=$(mktemp -d "$(dirname "$output")/.native-seed.XXXXXX")
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
chmod +x "$download/$asset"
mv "$download/$asset" "$output"
