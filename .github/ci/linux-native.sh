#!/bin/bash
set -euo pipefail
# No C product/test-driver build. The existing fetchers verify the pinned bytes.
sh tools/bootstrap.sh build-release/sdk/chibi-build-host
sh tools/bootstrap/runtime.sh build-release/sdk/chibi-compiler-host
sh tools/bootstrap/seed.sh
test "$(build-release/sdk/chibi-build-host --protocol-version)" = 15
# Native acceptance must start from the pinned native compiler, not fall back to
# compiling stage one through the legacy VM path.
grep -q "^linux-x64	" tools/bootstrap/native-seed.tsv

# Graph command parity still reads third-party headers and link metadata. Use the
# existing archive hash/race/prune policy without compiling any engine products.
build-release/sdk/chibi-build-host --compiler tools/compiler --compiler-core packages/core --root "$PWD" \
  --source tools/toolchain/Build.chibi --core packages/core --entry Build --timeout-ms 1800000 \
  -- third-party "$RUNNER_TEMP/native-prerequisites"
