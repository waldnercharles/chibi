#!/bin/sh
# Fetches the pinned compiler host, the prebuilt chibi-compiler-host that runs the
# compiler's prepared closure, so a machine with no C compiler can start the
# native self-build. tools/compiler/runtime.json pins its release, size and
# SHA-256 per platform. Usage: tools/bootstrap/runtime.sh OUTPUT
set -eu
output=${1:?usage: tools/bootstrap/runtime.sh OUTPUT}
root=$(cd "$(dirname "$0")/../.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) platform=macos-arm64 ;;
    Linux-x86_64) platform=linux-x64 ;;
    *) printf 'runtime: no pinned compiler host for %s %s\n' "$(uname -s)" "$(uname -m)" >&2; exit 2 ;;
esac
manifest=$root/tools/compiler/runtime.json
# The platform's block is the lines from its key to the next closing brace.
field() {
    awk -v platform="\"$platform\"" -v key="\"$1\"" '
        $1 == platform ":" { inside = 1; next }
        inside && /}/ { exit }
        inside && $1 == key ":" { value = $2; gsub(/[",]/, "", value); print value; exit }' "$manifest"
}
repository=$(sed -n 's/.*"repository": *"\([^"]*\)".*/\1/p' "$manifest" | head -n 1)
tag=$(field tag)
asset=$(field asset)
sha256=$(field sha256)
bytes=$(field bytes)
if [ -z "$repository" ] || [ -z "$tag" ] || [ -z "$asset" ] || [ -z "$sha256" ] || [ -z "$bytes" ]; then
    printf 'runtime: %s pins no complete compiler host for %s\n' "$manifest" "$platform" >&2
    exit 2
fi
directory=$(mktemp -d "${TMPDIR:-/tmp}/chibi-runtime.XXXXXX")
trap 'rm -rf "$directory"' EXIT
gh release download "$tag" --repo "${CHIBI_REPOSITORY:-$repository}" --pattern "$asset" --dir "$directory"
if command -v sha256sum >/dev/null 2>&1; then
    actual=$(sha256sum "$directory/$asset" | cut -d' ' -f1)
else
    actual=$(shasum -a 256 "$directory/$asset" | cut -d' ' -f1)
fi
size=$(wc -c < "$directory/$asset" | tr -d ' ')
if [ "$actual" != "$sha256" ] || [ "$size" != "$bytes" ]; then
    printf 'runtime: %s is %s bytes with SHA-256 %s, not the pinned %s bytes and %s\n' "$asset" "$size" "$actual" "$bytes" "$sha256" >&2
    exit 1
fi
mkdir -p "$(dirname "$output")"
chmod +x "$directory/$asset"
mv "$directory/$asset" "$output"
