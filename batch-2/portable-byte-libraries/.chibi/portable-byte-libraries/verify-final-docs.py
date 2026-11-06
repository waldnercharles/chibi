from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import time

root = Path(__file__).resolve().parents[2]
base = Path(__file__).resolve().parent


def git(*args):
    return subprocess.check_output(['git', *args], cwd=root, text=True).strip()


assert not git('status', '--porcelain=v1')
head = git('rev-parse', 'HEAD')
receipt = {'head': head, 'tree': git('rev-parse', 'HEAD^{tree}'),
           'base': git('rev-parse', 'origin/main'), 'clean_before': True, 'steps': []}
environment = os.environ.copy()
environment['CHIBI_TOOLS_BUILD_DIR'] = str(root / 'build-release/test/tools')
for name, argv in [
    ('published-docs', ['python3', 'tools/test_policy.py', 'tool', 'published-docs']),
    ('chibi-cli', ['python3', 'tools/test_policy.py', 'tool', 'chibi-cli']),
    ('format', ['make', 'check-format']),
]:
    log = root / f'.chibi/logs/portable-five-d5ac-{name}.log'
    start = time.monotonic()
    with log.open('w') as output:
        result = subprocess.run(argv, cwd=root, env=environment, stdout=output,
                                stderr=subprocess.STDOUT)
    elapsed = time.monotonic() - start
    receipt['steps'].append({'name': name, 'argv': argv, 'exit_code': result.returncode,
                             'seconds': elapsed, 'log': str(log),
                             'log_sha256': hashlib.sha256(log.read_bytes()).hexdigest()})
    (base / 'docs-fixed-checks.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(name, result.returncode, f'{elapsed:.3f}s', flush=True)
    if result.returncode:
        sys.exit(result.returncode)
assert git('rev-parse', 'HEAD') == head
assert not git('status', '--porcelain=v1')
receipt['clean_after'] = True
(base / 'docs-fixed-checks.json').write_text(json.dumps(receipt, indent=2) + '\n')
