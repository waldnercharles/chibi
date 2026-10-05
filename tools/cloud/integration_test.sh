#!/bin/bash
# Opt-in real Debian archive test. Downloads/extracts into a disposable prefix.
# Requires the October 4 prepared profile archive and extracted libraries.
# No host packages change; set CHIBI_CLOUD_TOOLS to that baseline prefix.
set -euo pipefail
[[ ${1-} == --download ]] || { echo 'Usage: integration_test.sh --download (requires network and several GB of temporary space)' >&2; exit 2; }
repo=$(cd "$(dirname "$0")/../.." && pwd -P)
metadata=${CHIBI_CLOUD_TOOLS:-/workspace/.cloud-tools}/apt.conf
[[ -f $metadata ]] || { echo 'Run tools/cloud/setup.sh first for Debian metadata' >&2; exit 1; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
export TEST_APT_STATUS="$tmp/status"
# Simulate the prepared host using official package records from the baseline
# archive and its extracted shared libraries. Apt and all compilers remain real;
# only apt's installed-package database is private. No host state is changed.
base=${CHIBI_CLOUD_TOOLS:-/workspace/.cloud-tools}
python3 - "$tmp/status" "$base/apt/archives" <<'PYDATA'
from pathlib import Path
import subprocess
import sys
records = {}
for record in Path('/var/lib/dpkg/status').read_text().strip().split('\n\n'):
    name = next(line[9:] for line in record.splitlines() if line.startswith('Package: '))
    records[name] = record
for package in sorted(Path(sys.argv[2]).glob('*.deb')):
    record = subprocess.check_output(['dpkg-deb', '-f', str(package)], text=True).strip()
    name = next(line[9:] for line in record.splitlines() if line.startswith('Package: '))
    records[name] = record + '\nStatus: install ok installed'
for name in ('llvm-19', 'lld-19'):
    assert name in records, f'Baseline archive missing {name}; use the prepared October 4 profile'
Path(sys.argv[1]).write_text('\n\n'.join(records.values()) + '\n')
PYDATA
export LD_LIBRARY_PATH="$base/root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
mkdir -p "$tmp/bin" "$tmp/package/DEBIAN"
chmod 755 "$tmp/package/DEBIAN"
cat > "$tmp/bin/apt-get" <<'WRAPPER'
#!/bin/bash
exec /usr/bin/apt-get -o "Dir::State::status=$TEST_APT_STATUS" "$@"
WRAPPER
chmod +x "$tmp/bin/apt-get"
export CHIBI_CLOUD_TOOLS="$tmp/tools"
# Real, harmless .deb fixtures with mixed revisions in BOTH obsolete caches.
# These are never installation inputs: extracting either would create the marker.
for version in 1 9; do
    cat > "$tmp/package/DEBIAN/control" <<CONTROL
Package: cloud-stale-fixture
Version: $version
Architecture: all
Maintainer: Test <test@example.invalid>
Description: forbidden cached extraction sentinel
CONTROL
    echo "$version" > "$tmp/package/stale-cache-marker"
    for cache in archives modeling-archives; do
        mkdir -p "$CHIBI_CLOUD_TOOLS/apt/$cache"
        dpkg-deb --build "$tmp/package" "$CHIBI_CLOUD_TOOLS/apt/$cache/cloud-stale-fixture_${version}_all.deb" >/dev/null
    done
done
PATH="$tmp/bin:$PATH" bash "$repo/tools/cloud/setup.sh" modeling
[[ ! -e $CHIBI_CLOUD_TOOLS/root/stale-cache-marker ]]
# The compiler smoke uses the native LLVM and official SDK LLVM/Binaryen and executes emitted JS/Wasm.
timeout 180 "$repo/tools/cloud/bin/start_skill" -- bash "$repo/tools/cloud/smoke.sh"
# Repetition with mixed stale revisions still present must not replay the caches.
PATH="$tmp/bin:$PATH" bash "$repo/tools/cloud/setup.sh" modeling
[[ ! -e $CHIBI_CLOUD_TOOLS/root/stale-cache-marker ]]
# Reject whitespace before configure can run with split Make flags.
if CHIBI_CLOUD_TOOLS="$tmp/unsupported path" "$repo/tools/cloud/bin/start_skill" -- \
    sh "$repo/tools/configure_native.sh" "$tmp/rejected-build" > "$tmp/path.log" 2>&1; then
    echo 'Whitespace prefix unexpectedly passed' >&2; exit 1
fi
grep -q 'cannot contain whitespace' "$tmp/path.log"
[[ ! -e $tmp/rejected-build ]]
printf 'PASS real archive transactions: installed-host LLVM, stale revisions, repeat setup, C/C++/sanitizers/Wasm, whitespace guard\n'
