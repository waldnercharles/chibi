#!/bin/bash
set -euo pipefail
root=/private/tmp/chibi-published-c5-linux-admission-36506138d
seed=/private/tmp/chibi-native-seed-c5c0a1ac2-release/chibi-native-linux-x64
image=sha256:c2936c6e9f67a2bb3bc5869b07e36d7f6cbe87b0af413c1b3f33844ae5722128
test "$(docker image inspect --format '{{.Id}}' "$image")" = "$image"
test "$(shasum -a 256 "$seed" | cut -d' ' -f1)" = 1d1d289a2429fe2ee2a75a71a959bf21590a02ff3ebf7cd8af1037a11bf1268e
cleanup() {
  if [ -f "$root/container.cid" ]; then
    owned=$(cat "$root/container.cid")
    if [[ "$owned" =~ ^[a-f0-9]{64}$ ]]; then docker rm --force "$owned" > /dev/null 2>&1 || true; fi
  fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
cd /Users/cwaldner/Personal/chibi/.coordination/compile-ci-costs-20261102/chibi
node .github/ci/budget.js "$(($(date +%s) + 5400))" \
  docker run --rm --network none --platform linux/amd64 --cpus 2 \
    --cidfile "$root/container.cid" \
    --mount "type=bind,source=$root,target=$root" \
    --mount "type=bind,source=$seed,target=$seed,readonly" \
    --workdir "$root/source" --env CC=/usr/bin/false --env CXX=/usr/bin/false \
    "$image" python3 "$root/run.py"
