#!/bin/sh
# Publishes the seed `tools/build.sh build bootstrap-artifact` wrote as a release asset and pins
# it in tools/bootstrap/seed.tsv; commit that file with bootstrap.json.
# Usage: tools/bootstrap/publish-seed.sh
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd -P)
seed=$root/tools/compiler/bootstrap/compiler.chibip
if command -v sha256sum >/dev/null 2>&1; then
    sha256=$(sha256sum "$seed" | cut -d' ' -f1)
else
    sha256=$(shasum -a 256 "$seed" | cut -d' ' -f1)
fi
manifest=$(sed -n 's/.*"sha256": *"\([0-9a-f]\{64\}\)".*/\1/p' "$root/tools/compiler/bootstrap/bootstrap.json" | head -n 1)
if [ "$sha256" != "$manifest" ]; then
    printf 'publish-seed: compiler.chibip has SHA-256 %s, but bootstrap.json pins %s\n' "$sha256" "$manifest" >&2
    exit 1
fi
repository=${CHIBI_REPOSITORY:-waldnercharles/chibi}
tag=bootstrap-seed-$(printf '%s' "$sha256" | cut -c1-16)
if ! gh release view "$tag" --repo "$repository" >/dev/null 2>&1; then
    gh release create "$tag" --repo "$repository" --prerelease --title "Bootstrap seed $tag" \
        --notes "The compiled compiler that compiles tools/compiler. SHA-256 $sha256. Pinned by tools/bootstrap/seed.tsv." \
        "$seed"
fi
{
    printf '# The release holding the bootstrap seed that tools/compiler/bootstrap/bootstrap.json pins:\n'
    printf '# release tag, asset, SHA-256. tools/bootstrap/seed.sh fetches it; publish-seed.sh writes it.\n'
    printf '%s\tcompiler.chibip\t%s\n' "$tag" "$sha256"
} > "$root/tools/bootstrap/seed.tsv"
printf '%s\tcompiler.chibip\t%s\n' "$tag" "$sha256"
