#!/bin/sh
# Fetches the pinned first build host, the one binary the Chibi graph needs to
# build everything else, as Zig ships zig1. Usage: tools/bootstrap.sh OUTPUT
set -eu
output=${1:?usage: tools/bootstrap.sh OUTPUT}
root=$(cd "$(dirname "$0")/.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) platform=macos-arm64 ;;
    Linux-x86_64) platform=linux-x64 ;;
    *) printf 'bootstrap: no pinned build host for %s %s\n' "$(uname -s)" "$(uname -m)" >&2; exit 2 ;;
esac
pin=$(awk -F '\t' -v platform="$platform" '$1 == platform { print $2 "\t" $3 "\t" $4 }' "$root/tools/bootstrap/hosts.tsv")
if [ -z "$pin" ]; then
    printf 'bootstrap: tools/bootstrap/hosts.tsv pins no host for %s\n' "$platform" >&2
    exit 2
fi
tag=$(printf '%s' "$pin" | cut -f1)
asset=$(printf '%s' "$pin" | cut -f2)
sha256=$(printf '%s' "$pin" | cut -f3)
directory=$(mktemp -d "${TMPDIR:-/tmp}/chibi-bootstrap.XXXXXX")
trap 'rm -rf "$directory"' EXIT
gh release download "$tag" --repo "${CHIBI_REPOSITORY:-waldnercharles/chibi}" --pattern "$asset" --dir "$directory"
if command -v sha256sum >/dev/null 2>&1; then
    actual=$(sha256sum "$directory/$asset" | cut -d' ' -f1)
else
    actual=$(shasum -a 256 "$directory/$asset" | cut -d' ' -f1)
fi
if [ "$actual" != "$sha256" ]; then
    printf 'bootstrap: %s has SHA-256 %s, not the pinned %s\n' "$asset" "$actual" "$sha256" >&2
    exit 1
fi
mkdir -p "$(dirname "$output")"
chmod +x "$directory/$asset"
mv "$directory/$asset" "$output"
# tools/build.sh trusts a host only when it is newer than the last pin change.
touch "$output"
