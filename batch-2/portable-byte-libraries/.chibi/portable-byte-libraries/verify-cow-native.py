from pathlib import Path
import hashlib
import json
import os
import subprocess
import time

root = Path(__file__).resolve().parents[2]
base = Path(__file__).resolve().parent
head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
assert head == 'dea59917c76d665a3a117690b77a6b0717a798b0'
assert not subprocess.check_output(['git', 'status', '--porcelain=v1'], cwd=root, text=True).strip()
build = root / '.chibi/b795'
env = os.environ.copy()
for key, path in {
    'CHIBI_LANGUAGE_BUILD_DIR': build / 'test/language',
    'CHIBI_TOOLS_BUILD_DIR': build / 'test/tools',
    'CHIBI_COMPILER_HOST': build / 'sdk/chibi-compiler-host',
    'CHIBI_RELEASE_COMPILER_HOST': build / 'sdk/chibi-compiler-host',
    'CHIBI_BUILD_HOST': build / 'sdk/chibi-build-host',
    'CHIBI_CLI': build / 'sdk/chibi',
    'CHIBI_COMPILE': build / 'sdk/chibi-compile',
    'CHIBI_CTL': build / 'bin/chibi_ctl',
    'CHIBI_RELEASE_LANGUAGE_BUILD_DIR': build / 'test/language',
}.items():
    env[key] = str(path)
receipt = {'head': head, 'tree': subprocess.check_output(['git', 'rev-parse', 'HEAD^{tree}'], cwd=root, text=True).strip(),
           'clean_before': True, 'environment': {k: v for k, v in env.items() if k.startswith('CHIBI_')}, 'steps': []}
for name in [
    'language-package-portable',
    'language-package-portable-compiler',
    'language-compiler-fixture',
    'language-package-tests',
    'language-package-core',
    'language-package-compiler-chibi-collections',
    'language-package-compiler-chibi-sharing',
    'language-native-calls',
    'language-asset-compiler-host',
    'language-asset-packaging-chibi',
    'compiler-bootstrap',
    'bootstrap-recovery',
]:
    argv = ['python3', 'tools/test_policy.py', 'tool', name]
    log = root / f'.chibi/logs/portable-five-dea5-{name}.log'
    start = time.monotonic()
    print('RUN', name, flush=True)
    with log.open('w') as output:
        result = subprocess.run(argv, cwd=root, env=env, stdout=output, stderr=subprocess.STDOUT)
    elapsed = time.monotonic() - start
    text = log.read_text()
    summary = [s for s in text.splitlines() if s.startswith('Summary:') or s.startswith('Test command wall time:')]
    receipt['steps'].append({'name': name, 'argv': argv, 'exit_code': result.returncode,
                             'seconds': elapsed, 'log': str(log), 'summary': summary,
                             'log_sha256': hashlib.sha256(log.read_bytes()).hexdigest()})
    (base / 'cow-native-proof.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(name, result.returncode, f'{elapsed:.3f}s', summary, flush=True)
    assert result.returncode == 0, log
assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip() == head
assert not subprocess.check_output(['git', 'status', '--porcelain=v1'], cwd=root, text=True).strip()
receipt['clean_after'] = True
(base / 'cow-native-proof.json').write_text(json.dumps(receipt, indent=2) + '\n')
