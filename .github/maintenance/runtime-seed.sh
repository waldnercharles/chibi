#!/bin/bash
# Keep the whole build/recovery/import phase, including seed download, within 45 minutes.
set -euo pipefail
exec node .github/ci/budget.js "$(($(date +%s) + 2700))" pwsh -NoProfile -File .github/maintenance/runtime-seed.ps1 "$@"
