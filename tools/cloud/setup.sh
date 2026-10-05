#!/bin/bash
# Repository replacement for the October 4 Debian workspace-local setup.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd -P)
prefix=${CHIBI_CLOUD_TOOLS:-/workspace/.cloud-tools}
[[ $prefix == /* ]] || { echo 'CHIBI_CLOUD_TOOLS must be an absolute path.' >&2; exit 1; }
profile=${1:-base}
[[ $profile == base || $profile == modeling ]] || { echo 'Usage: setup.sh [base|modeling]' >&2; exit 2; }
. /etc/os-release
[[ $ID == debian && $VERSION_ID == 13 ]] || { echo 'This profile requires Debian 13 (trixie).' >&2; exit 1; }
[[ $(uname -m) == x86_64 ]] || { echo 'This profile requires x86_64.' >&2; exit 1; }
mkdir -p "$prefix"/{runtime,cache,apt/lists/partial,apt/archives/partial,root}
chmod 700 "$prefix/runtime"
# Official signed Debian archives only; no host package/account settings changed.
cat > "$prefix/sources.list" <<'SOURCES'
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian-security trixie-security main
SOURCES
cat > "$prefix/apt.conf" <<CONFIG
Dir::Etc::parts "-";
Dir::Etc::main "-";
Dir::Etc::sourcelist "$prefix/sources.list";
Dir::Etc::sourceparts "-";
Dir::State::lists "$prefix/apt/lists";
Dir::Cache::archives "$prefix/apt/archives";
APT::Sandbox::User "$(id -un)";
CONFIG
export APT_CONFIG="$prefix/apt.conf"
packages=(clang-19 libclang-19-dev cmake ninja-build ccache libgl1-mesa-dev libvulkan1 mesa-vulkan-drivers libx11-dev libxcursor-dev libxext-dev libxi-dev libxrandr-dev libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev xvfb xauth emscripten)
apt-get update
apt-get --download-only -y -o Debug::NoLocking=1 install "${packages[@]}"
# Explicit roots must also exist locally when the host already has them installed.
(cd "$prefix/apt/archives" && apt-get download "${packages[@]}")
for pkg in "$prefix"/apt/archives/*.deb; do
    case "$pkg" in */nodejs_*.deb|*/nodejs-doc_*.deb) continue;; esac
    dpkg-deb -x "$pkg" "$prefix/root"
done
python3 - "$prefix/root" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
for p in root.rglob('*'):
    if p.is_symlink() and not p.exists():
        host = Path('/') / p.parent.relative_to(root) / p.readlink()
        if host.exists():
            p.unlink()
            p.symlink_to(host)
PY
[[ -x $prefix/python/bin/python ]] || python3 -m venv "$prefix/python"
"$prefix/python/bin/pip" install --no-cache-dir --disable-pip-version-check 'cmake==4.2.3' 'clang-format==22.*'
python3 - "$prefix" "$(command -v node)" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
(p / 'emscripten-config').write_text(f'LLVM_ROOT = {str(p / "root/usr/lib/llvm-19/bin")!r}\nBINARYEN_ROOT = {str(p / "root/usr")!r}\nNODE_JS = {[sys.argv[2]]!r}\nCACHE = {str(p / "emscripten-cache")!r}\n')
PY
if [[ $profile == modeling ]]; then bash "$repo/tools/cloud/modeling_setup.sh"; fi
"$repo/tools/cloud/bin/start_skill"
