import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

root = Path.cwd()
folder = root / '.chibi/integration'
tag = sys.argv[1]
receipt = {
    'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
    'tree': subprocess.check_output(['git', 'rev-parse', 'HEAD^{tree}'], text=True).strip(),
    'tracked_clean_before': not subprocess.check_output(['git', 'status', '--porcelain=v1'], text=True).strip(),
    'commands': [],
}

def run(name, command):
    log = folder / (tag + '-' + name + '.log')
    started = time.monotonic()
    with log.open('w') as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT)
    output = log.read_text(errors='replace')
    row = {
        'name': name, 'command': command, 'exit_code': result.returncode,
        'wall_seconds': time.monotonic() - started,
        'log': str(log.relative_to(root)),
        'log_sha256': hashlib.sha256(log.read_bytes()).hexdigest(),
        'summaries': [line for line in output.splitlines() if 'Summary:' in line or 'MATH_TESTS backend=' in line or 'Test command wall time:' in line],
    }
    receipt['commands'].append(row)
    (folder / (tag + '.json')).write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(row), flush=True)
    if result.returncode:
        print(output[-4000:], flush=True)
    return result.returncode

code = run('build', ['make', 'wasm-language-build', 'WASM_BUILD_DIR=.chibi/integration/wasm', 'JOBS=4'])
if code:
    sys.exit(code)
failed = False
for selector in ('package-math', 'compiler-fixture', 'package-imports', 'package-core', 'docs-markdown', 'packed', 'package-compiler-chibi-imports', 'package-compiler-chibi-imports-compiled'):
    code = run(selector, ['python3', 'tools/test_policy.py', 'native', 'node', str(root / '.chibi/integration/wasm/test_language.js'), '--' + selector])
    failed = bool(code) or failed
receipt['tracked_clean_after'] = not subprocess.check_output(['git', 'status', '--porcelain=v1'], text=True).strip()
receipt['wasm_sha256'] = hashlib.sha256((root / '.chibi/integration/wasm/test_language.wasm').read_bytes()).hexdigest()
receipt['all_requested_checks_passed'] = not failed
(folder / (tag + '.json')).write_text(json.dumps(receipt, indent=2) + '\n')
sys.exit(1 if failed else 0)
