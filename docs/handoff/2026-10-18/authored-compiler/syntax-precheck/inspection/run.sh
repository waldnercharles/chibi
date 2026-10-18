#!/usr/bin/env bash
set -euo pipefail
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
cd "$HERE/.."
sha256sum --check --quiet frozen.sha256
sha256sum --check --quiet toolchain.sha256
test -x "$HERE/build/test_language"
cd "$HERE"
sha256sum --check --quiet build/target.sha256
export PYTHONDONTWRITEBYTECODE=1
exec /opt/codex/runtimes/codex-primary-runtime/dependencies/python/bin/python3.12 - "$HERE" <<'PY'
import importlib.util
from pathlib import Path
import sys

root = Path(sys.argv[1]).resolve()
spec = importlib.util.spec_from_file_location("test_policy", root / "tools/test_policy.py")
policy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(policy)
raise SystemExit(policy.run_commands([
    [str(root / "build/test_language"), "--bounded-compiler-source-syntax"],
], seconds=60))
PY
