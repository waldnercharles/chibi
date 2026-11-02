#!/bin/bash
# Optional local source transfer. Never uploads; packages only HEAD's reachable history.
set -euo pipefail
node .github/ci/budget.js "$(($(date +%s) + 300))" bash -euo pipefail -s -- "$@" <<'SH'
[ "$#" = 1 ] || { echo 'usage: source-bundle.sh NEW_OUTPUT_DIRECTORY' >&2; exit 2; }
test "$(git rev-parse --is-shallow-repository)" = false
sha=$(git rev-parse HEAD)
tree=$(git rev-parse 'HEAD^{tree}')
source_ref=$(git symbolic-ref --quiet --short HEAD || printf '%s' "$sha")
mkdir "$1"
out=$(cd "$1" && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-source-bundle.XXXXXX")
trap 'rm -rf "$work"' EXIT
# A temporary repository avoids changing checkout refs or copying unrelated tips.
root=$(git rev-parse --show-toplevel)
git init --bare "$work/source.git"
git -C "$work/source.git" fetch --no-tags "$root" "$sha:refs/heads/source-bundle"
git -C "$work/source.git" bundle create "$out/chibi.bundle" refs/heads/source-bundle
test "$(git bundle list-heads "$out/chibi.bundle")" = "$sha refs/heads/source-bundle"
git init --bare "$work/check.git"
git -C "$work/check.git" bundle verify "$out/chibi.bundle"
git clone --bare --branch source-bundle "$out/chibi.bundle" "$work/clone.git"
test "$(git -C "$work/clone.git" rev-parse HEAD)" = "$sha"
test "$(git -C "$work/clone.git" rev-parse 'HEAD^{tree}')" = "$tree"
git -C "$work/clone.git" fsck --full
node - "$out" "$sha" "$tree" "$source_ref" <<'JS'
const fs = require('node:fs');
const {createHash} = require('node:crypto');
const [out, commit, tree, source_ref] = process.argv.slice(2);
const sha256 = createHash('sha256').update(fs.readFileSync(`${out}/chibi.bundle`)).digest('hex');
fs.writeFileSync(`${out}/manifest.json`, JSON.stringify({version: 1, commit, tree, source_ref,
  bundle_ref: 'refs/heads/source-bundle', bundle: 'chibi.bundle', sha256}, null, 2) + '\n');
JS
(cd "$out" && shasum -a 256 chibi.bundle manifest.json > SHA256SUMS)
SH
