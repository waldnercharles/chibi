from pathlib import Path
import json
import os
import re
import shutil
import signal
import subprocess
import time

root = Path(__file__).resolve().parents[2]
base = root / '.chibi/portable-byte-libraries'
logs = root / '.chibi/logs'
normal = base / 'sdk-installed'
relocated = base / 'sdk-relocated'
hidden = base / 'sdk-installed-hidden'
build = base / 'language-relocated'
receipt_path = base / 'relocation-proof-receipt.json'
receipt = json.loads((base / 'relocation-clone-receipt.json').read_text())
receipt.pop('files')
receipt['steps'] = []
receipt['passed'] = False
receipt['original_restored'] = False
start = time.monotonic()


def save():
    receipt_path.write_text(json.dumps(receipt, indent=2) + '\n')


def interrupted(signum, _frame):
    raise KeyboardInterrupt(f'relocation proof interrupted by signal {signum}')


signal.signal(signal.SIGTERM, interrupted)
signal.signal(signal.SIGINT, interrupted)


def step(name, command, environment=None):
    assert not normal.exists() and hidden.is_dir()
    log = logs / f'byte-verifier-relocated-{name}-21ba.log'
    print(f'RUN {name}: {log}', flush=True)
    began = time.monotonic()
    with log.open('w') as output:
        result = subprocess.run(command, cwd=root, env=environment, stdout=output,
                                stderr=subprocess.STDOUT)
    record = {'name': name, 'command': command, 'log': str(log),
              'seconds': time.monotonic() - began, 'exit_code': result.returncode,
              'original_unavailable': not normal.exists()}
    receipt['steps'].append(record)
    save()
    print(f'END {name}: code={result.returncode} seconds={record["seconds"]:.3f}', flush=True)
    assert result.returncode == 0, f'{name} failed; see {log}'
    return log.read_text()


assert normal.is_dir() and relocated.is_dir() and not hidden.exists()
try:
    normal.rename(hidden)
    assert not normal.exists() and hidden.is_dir()
    receipt['original_hidden'] = True
    save()
    print(f'ORIGINAL HIDDEN: {normal}', flush=True)
    configure = step('configure', [
        'cmake', '-S', 'test/language', '-B', str(build), '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm@22/bin/clang',
        '-DCHIBI_INSTALLED_SDK=ON',
        f'-DChibi_DIR={relocated}/lib/cmake/Chibi',
        '--trace-expand', f'--trace-source={relocated}/lib/cmake/Chibi/ChibiConfig.cmake'
    ])
    cache = (build / 'CMakeCache.txt').read_text()
    ninja = (build / 'build.ninja').read_text()
    assert f'Chibi_DIR:UNINITIALIZED={relocated}/lib/cmake/Chibi' in cache
    assert f'set(Chibi_CORE_SOURCE_DIR {relocated}/share/chibi/core' in configure
    assert str(normal) not in ninja and str(hidden) not in ninja
    assert str(relocated / 'lib/libchibi_text_capabilities.a') in ninja
    assert str(relocated / 'share/chibi/core/bytes/Tests.chibi') in ninja
    receipt['resolved_core_source_dir'] = str(relocated / 'share/chibi/core')
    receipt['cache_and_build_use_relocated_sdk'] = True
    step('build', ['cmake', '--build', str(build), '--target', 'test_language', '--parallel', '3'])
    environment = os.environ.copy()
    environment['CHIBI_LANGUAGE_BUILD_DIR'] = str(build)
    for name, selector in [
        ('portable', 'language-package-portable'),
        ('compiler', 'language-package-portable-compiler'),
        ('asset-host', 'language-asset-compiler-host')
    ]:
        output = step(name, ['python3', 'tools/test_policy.py', 'tool', selector], environment)
        assert re.search(r'Summary: Passed: \d+ Failed: 0', output)
        if name == 'portable':
            assert 'BYTECODE: 23 tests, 0 failed' in output
            assert 'SLJIT: 23 tests, 0 failed' in output
        if name == 'compiler':
            combinations = re.findall(r'Stage-one compiler (\w+) -> target (\w+): 23 authored tests', output)
            assert set(combinations) == {('BYTECODE', 'BYTECODE'), ('BYTECODE', 'SLJIT'),
                                         ('SLJIT', 'BYTECODE'), ('SLJIT', 'SLJIT')}
            assert len(combinations) == 4
            assert 'portable_public_consumer_compiles_with_authored_compiler (OK)' in output
            receipt['authored_test_matrix'] = [list(pair) for pair in combinations]
            receipt['tests_per_matrix_combination'] = 23
            receipt['target_backend_no_fallback_assertions_passed'] = True
    consumer = base / 'relocated-consumer'
    consumer.mkdir()
    shutil.copyfile(root / 'test/packages/portable_consumer/Consumer.chibi', consumer / 'Consumer.chibi')
    version = step('cli-version', [str(relocated / 'bin/chibi'), 'version'])
    assert f'compiler: {relocated}/bin/chibi-compile' in version
    output = step('cli-test', [str(relocated / 'bin/chibi'), 'test', str(consumer)])
    assert 'PASS consumer.PublicImports' in output, output
    assert re.search(r'\b1 tests?, 0 failed\b', output), output
    receipt['external_public_import_tests'] = 1
    receipt['passed'] = True
finally:
    if hidden.exists():
        assert not normal.exists(), 'original SDK prefix was recreated during isolation proof'
        hidden.rename(normal)
    receipt['original_restored'] = normal.is_dir() and not hidden.exists()
    receipt['total_seconds'] = time.monotonic() - start
    save()
    print(f'ORIGINAL RESTORED: {normal}', flush=True)
