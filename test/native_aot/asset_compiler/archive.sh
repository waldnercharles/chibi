#!/bin/sh
# Exercise the real archive graph from a source-only private checkout.
set -eu
driver=${1:?usage: archive.sh NATIVE_DRIVER LLVM_BIN}
llvm=${2:?usage: archive.sh NATIVE_DRIVER LLVM_BIN}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-asset-archive.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
target=x86_64-unknown-linux-gnu
if [ "$(uname -s)" = Darwin ]; then target=aarch64-apple-darwin; fi
"$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --collection "graph=$root/tools/graph" --source "$root/test/native_aot/asset_compiler/Archive.chibi" \
    --entry Main --target "$target" --llvm "$llvm" --level 0 --output "$work/graph"
python3 - "$root" "$work" "$driver" "$llvm" <<'PY'
from pathlib import Path
import hashlib
import shutil
import subprocess
import sys

root, work, driver, llvm = map(Path, sys.argv[1:])
source = work / 'source'
for folder in ['tools/compiler', 'tools/graph', 'tools/toolchain', 'packages/core']:
    for path in (root / folder).rglob('*'):
        relative = path.relative_to(root)
        if any(part.startswith('build') or part in {'.git', '.chibi'} for part in relative.parts):
            continue
        if path.is_file() and path.suffix in {'.chibi', '.ll'}:
            destination = source / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, destination)
header = source / 'src/authoring/asset_compiler_host.h'
header.parent.mkdir(parents=True)
shutil.copyfile(root / 'src/authoring/asset_compiler_host.h', header)
products = work / 'products'
(products / 'sdk').mkdir(parents=True)
shutil.copyfile(driver, products / 'sdk/chibi')
(products / 'sdk/chibi').chmod(0o755)
command = [str(work / 'graph'), str(llvm), str(source), str(products)]

def run(label, built):
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (work / f'{label}.log').write_text(result.stdout)
    if result.returncode != (1 if built < 0 else 0) or f'built:{built}\n' not in result.stdout:
        raise RuntimeError(f'{label}: {result.stdout}')

run('cold', 1)
run('warm', 0)
template = source / 'tools/graph/asset_native_runtime/Windows.ll'
original = template.read_bytes()
template.write_bytes(original + b'\n; archive input regression\n')
run('edited', 1)
run('edited-warm', 0)
archive = products / 'lib/libchibi_asset_compiler_host.a'
receipt = archive.with_suffix(archive.suffix + '.json')
before = [hashlib.sha256(path.read_bytes()).digest() for path in [archive, receipt]]
template.unlink()
run('missing', -1)
after = [hashlib.sha256(path.read_bytes()).digest() for path in [archive, receipt]]
if before != after:
    raise RuntimeError('a missing template changed the previous archive or receipt')
template.write_bytes(original)
run('restored', 1)
run('restored-warm', 0)
print('PASS native archive cold/warm, Windows template edit/deletion, prior output and recovery')
PY
