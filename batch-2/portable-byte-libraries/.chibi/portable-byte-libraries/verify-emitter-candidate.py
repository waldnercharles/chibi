from pathlib import Path
import hashlib
import json
import os
import subprocess
import time

root = Path(__file__).resolve().parents[2]
base = Path(__file__).resolve().parent


def git(*args):
    return subprocess.check_output(['git', *args], cwd=root, text=True).strip()


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


assert not git('status', '--porcelain=v1')
head = git('rev-parse', 'HEAD')
receipt = {'head': head, 'tree': git('rev-parse', 'HEAD^{tree}'),
           'base': git('rev-parse', 'origin/main'), 'clean_before': True, 'steps': []}
environment = os.environ.copy()
environment['CHIBI_TOOLS_BUILD_DIR'] = str(root / 'build-release/test/tools')


def run(name, argv):
    log = root / f'.chibi/logs/portable-five-4800-{name}.log'
    start = time.monotonic()
    with log.open('w') as output:
        result = subprocess.run(argv, cwd=root, env=environment, stdout=output,
                                stderr=subprocess.STDOUT)
    elapsed = time.monotonic() - start
    receipt['steps'].append({'name': name, 'argv': argv, 'exit_code': result.returncode,
                             'seconds': elapsed, 'log': str(log), 'log_sha256': sha(log)})
    (base / 'emitter-fixed-checks.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(name, result.returncode, f'{elapsed:.3f}s', flush=True)
    assert result.returncode == 0, log


proof = base / 'native-build-regression/driver-proof'
prior = json.loads((proof / 'receipt.json').read_text())
source = proof / 'Corrected.chibi'
assert source.read_bytes().startswith((root / 'tools/native_aot/Driver.chibi').read_bytes())
step = next(x for x in prior['steps'] if x['name'] == 'corrected-emitter')
argv = [str(root / 'build-release/share/chibi/core') if p == str(proof / 'core') else p
        for p in step['argv']]
argv[-1] = str(proof / 'clean-emitter.chibip')
run('private-emitter', argv)
artifact = proof / 'clean-emitter.chibip'
assert artifact.is_file() and artifact.stat().st_size > 0
receipt['emitter_artifact'] = {'bytes': artifact.stat().st_size, 'sha256': sha(artifact)}
receipt['driver_sha256'] = sha(root / 'tools/native_aot/Driver.chibi')
for name, argv in [
    ('published-docs', ['python3', 'tools/test_policy.py', 'tool', 'published-docs']),
    ('chibi-cli', ['python3', 'tools/test_policy.py', 'tool', 'chibi-cli']),
    ('native-aot-driver', ['python3', 'tools/test_policy.py', 'tool', 'native-aot-driver']),
    ('canonicality', ['build-release/bin/chibi_ctl', '--project-root', 'tools/native_aot', '--format', '--check']),
]:
    run(name, argv)
assert git('rev-parse', 'HEAD') == head
assert not git('status', '--porcelain=v1')
receipt['clean_after'] = True
(base / 'emitter-fixed-checks.json').write_text(json.dumps(receipt, indent=2) + '\n')
