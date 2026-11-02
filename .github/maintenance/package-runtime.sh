#!/bin/bash
# Compare both platforms and assemble local release material, never publish or pin it.
set -euo pipefail
exec node .github/ci/budget.js "$(($(date +%s) + 300))" node .github/maintenance/package-runtime.js "$@"
