#!/bin/bash
# Match .github/workflows/deploy-web.yml using the official emsdk distribution.
set -euo pipefail
prefix=${CHIBI_CLOUD_TOOLS:-/workspace/.cloud-tools}
[[ $prefix == /* && ! $prefix =~ [[:space:]] ]] || { echo 'CHIBI_CLOUD_TOOLS must be absolute and contain no whitespace.' >&2; exit 1; }
sdk="$prefix/emsdk-6.0.10"
# Official 6.0.10 tag and its release target, pinned rather than tracking latest.
revision=a2b92777574c2feda07994cd4f1079a3dfc151f8
release=666337b525e673e769121856d175f6f52b8ead64
if [[ ! -e $sdk ]]; then
    git clone --depth 1 --branch 6.0.10 https://github.com/emscripten-core/emsdk.git "$sdk"
fi
[[ $(git -C "$sdk" rev-parse HEAD) == "$revision" ]] || { echo "Unexpected emsdk revision in $sdk; expected $revision" >&2; exit 1; }
# Install the official compiler bundle; retain the prepared host's Node runtime.
# Unlike 'activate', this needs no shell or user configuration changes.
"$sdk/emsdk" install "releases-$release-64bit"
python3 - "$prefix" "$(command -v node)" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
sdk = p / 'emsdk-6.0.10/upstream'
(p / 'emscripten-6.0.10-config').write_text(
    f'LLVM_ROOT = {str(sdk / "bin")!r}\n'
    f'BINARYEN_ROOT = {str(sdk)!r}\n'
    f'NODE_JS = {[sys.argv[2]]!r}\n'
    f'CACHE = {str(p / "emscripten-cache-6.0.10")!r}\n')
PY
EM_CONFIG="$prefix/emscripten-6.0.10-config" "$sdk/upstream/emscripten/emcc" --version
