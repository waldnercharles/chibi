from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import signal
import stat
import subprocess
import time

root = Path(__file__).resolve().parents[2]
base = root / '.chibi/portable-byte-libraries'
normal = root / '.chibi/s795i'
relocated = root / '.chibi/x795'
hidden = root / '.chibi/h795'
build = root / '.chibi/n795'
receipt_path = base / 'cow-relocation-proof.json'
expected_head = 'dea59917c76d665a3a117690b77a6b0717a798b0'
old_paths = [base / 'relocation-proof.py', base / 'relocation-proof-receipt.json',
             base / 'relocation-clone-receipt.json']
started = time.monotonic()

def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()

def git(*args):
    return subprocess.check_output(['git', *args], cwd=root, text=True).strip()

def inventory(prefix):
    entries = {}
    for item in sorted(prefix.rglob('*')):
        info = item.lstat()
        record = {'mode': stat.S_IMODE(info.st_mode)}
        if stat.S_ISLNK(info.st_mode):
            record.update(type='symlink', target=os.readlink(item))
        elif stat.S_ISDIR(info.st_mode):
            record.update(type='directory')
        elif stat.S_ISREG(info.st_mode):
            record.update(type='file', bytes=info.st_size, sha256=sha(item))
        else:
            raise AssertionError(f'unsupported SDK entry {item}')
        entries[str(item.relative_to(prefix))] = record
    return entries

def inventory_hash(entries):
    return hashlib.sha256(json.dumps(entries, sort_keys=True, separators=(',', ':')).encode()).hexdigest()

assert git('rev-parse', 'HEAD') == expected_head
assert not git('status', '--porcelain=v1')
assert normal.is_dir() and not hidden.exists()
assert not relocated.exists() and not build.exists()
package = json.loads((normal / 'share/chibi/package.json').read_text())
assert package['source']['revision'] == expected_head and not package['source']['dirty']
manifest = json.loads((normal / 'share/chibi/engine.json').read_text())
assert manifest['artifact_formats']['execution']['version'] == 26
sdk_build_log = root / '.chibi/logs/portable-five-dea5-sdk-build.log'
packaged = re.findall(r'Packaged sdk (.+) \(([0-9a-f]{64})\)', sdk_build_log.read_text())
assert packaged and packaged[-1][0] == str(normal)
old_hashes = {str(path): sha(path) for path in old_paths}
# Supply an explicit, recorded environment. Preserve build, sanitizer, guard and
# test-policy settings without copying unrelated credentials to a receipt.
allowed = {'PATH', 'HOME', 'USER', 'LOGNAME', 'SHELL', 'TMPDIR', 'LANG', 'LC_ALL',
           'LC_CTYPE', 'SDKROOT', 'DEVELOPER_DIR', 'MACOSX_DEPLOYMENT_TARGET',
           'CPATH', 'LIBRARY_PATH', 'CMAKE_PREFIX_PATH', 'CC', 'CXX', 'AR',
           'RANLIB', 'LD', 'CFLAGS', 'CPPFLAGS', 'CXXFLAGS', 'LDFLAGS',
           'ASAN_OPTIONS', 'UBSAN_OPTIONS', 'MALLOC_CONF'}
environment = {key: value for key, value in os.environ.items()
               if key in allowed or key.startswith(('CHIBI_', 'CMAKE_', 'NINJA_', 'LC_'))}
assert not any(str(normal) in value or str(hidden) in value for value in environment.values())
receipt = {'head': expected_head, 'tree': git('rev-parse', 'HEAD^{tree}'),
           'clean_before': True, 'source_status_before': git('status', '--porcelain=v1'),
           'normal_prefix': str(normal), 'relocated_prefix': str(relocated),
           'hidden_prefix': str(hidden), 'consumer_build': str(build),
           'sdk_source': package['source'], 'sdk_pin': packaged[-1][1],
           'prepared_format': 26, 'sdk_build_log': str(sdk_build_log),
           'sdk_build_log_sha256': sha(sdk_build_log), 'environment': environment,
           'script_sha256': sha(Path(__file__)), 'old_evidence_sha256': old_hashes,
           'steps': [], 'passed': False, 'original_restored': False}

def save():
    receipt_path.write_text(json.dumps(receipt, indent=2) + '\n')

def interrupted(signum, _frame):
    raise KeyboardInterrupt(f'relocation verification interrupted by signal {signum}')

signal.signal(signal.SIGINT, interrupted)
signal.signal(signal.SIGTERM, interrupted)

def step(name, argv, env=None, cwd=root, phase='test', hidden_required=True):
    if hidden_required:
        assert not normal.exists() and hidden.is_dir()
    log = base / f'cow-relocation-{name}.log'
    active_env = environment if env is None else env
    executable = shutil.which(argv[0], path=active_env.get('PATH'))
    assert executable
    print(f'RUN {name}: {log}', flush=True)
    began = time.monotonic()
    with log.open('w') as output:
        result = subprocess.run(argv, cwd=cwd, env=active_env, stdout=output,
                                stderr=subprocess.STDOUT)
    record = {'name': name, 'argv': argv, 'cwd': str(cwd), 'environment': active_env,
              'executable': executable, 'executable_sha256': sha(Path(executable)),
              'phase': phase, 'seconds': time.monotonic() - began,
              'exit_code': result.returncode, 'log': str(log), 'log_sha256': sha(log),
              'original_unavailable': not normal.exists(), 'hidden_available': hidden.is_dir()}
    receipt['steps'].append(record)
    save()
    print(f'END {name}: code={result.returncode} seconds={record["seconds"]:.3f}', flush=True)
    assert result.returncode == 0, f'{name} failed; see {log}'
    return log.read_text()

try:
    before = inventory(normal)
    step('clone', ['/bin/cp', '-cR', str(normal), str(relocated)],
         phase='preparation', hidden_required=False)
    copied = inventory(relocated)
    assert copied == before, 'SDK clone entries, modes or file contents differ'
    clone_receipt = {'head': expected_head, 'normal_prefix': str(normal),
                     'relocated_prefix': str(relocated), 'inventory_entries': len(before),
                     'inventory_sha256': inventory_hash(before), 'clone_equals_normal': True,
                     'entries': before}
    (base / 'cow-relocation-clone.json').write_text(json.dumps(clone_receipt, indent=2) + '\n')
    receipt['inventory_entries'] = len(before)
    receipt['inventory_sha256'] = inventory_hash(before)
    receipt['clone_equals_normal'] = True
    normal.rename(hidden)
    receipt['original_hidden'] = True
    save()
    print(f'ORIGINAL HIDDEN: {normal}', flush=True)
    configured = step('configure', [
        'cmake', '-S', 'test/language', '-B', str(build), '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm@22/bin/clang',
        '-DCHIBI_INSTALLED_SDK=ON', f'-DChibi_DIR={relocated}/lib/cmake/Chibi',
        '--trace-expand', f'--trace-source={relocated}/lib/cmake/Chibi/ChibiConfig.cmake'
    ], phase='configuration')
    cache_path = build / 'CMakeCache.txt'
    ninja_path = build / 'build.ninja'
    cache = cache_path.read_text()
    ninja = ninja_path.read_text()
    assert f'Chibi_DIR:UNINITIALIZED={relocated}/lib/cmake/Chibi' in cache
    assert 'CHIBI_INSTALLED_SDK:BOOL=ON' in cache
    assert 'CMAKE_BUILD_TYPE:STRING=Release' in cache
    assert '/opt/homebrew/opt/llvm@22/bin/clang' in cache
    assert f'set(Chibi_CORE_SOURCE_DIR {relocated}/share/chibi/core' in configured
    assert str(normal) not in cache + ninja and str(hidden) not in cache + ninja
    assert str(relocated / 'lib/libchibi_text_capabilities.a') in ninja
    assert str(relocated / 'share/chibi/core/bytes/Tests.chibi') in ninja
    receipt['resolved_core_source_dir'] = str(relocated / 'share/chibi/core')
    receipt['cache_and_build_use_relocated_sdk'] = True
    receipt['cmake_cache_sha256'] = sha(cache_path)
    receipt['ninja_sha256'] = sha(ninja_path)
    receipt['cache_build_configuration'] = [line for line in cache.splitlines()
        if re.match(r'(CMAKE_BUILD_TYPE|CMAKE_C_COMPILER|CMAKE_C_FLAGS|CHIBI_INSTALLED_SDK|Chibi_DIR)', line)]
    step('build', ['cmake', '--build', str(build), '--target', 'test_language', '--parallel', '3'], phase='build')
    binary = build / 'test_language'
    receipt['test_language_sha256'] = sha(binary)
    test_env = dict(environment, CHIBI_LANGUAGE_BUILD_DIR=str(build))
    for name, selector in [
        ('portable', 'language-package-portable'),
        ('compiler', 'language-package-portable-compiler'),
        ('asset-host', 'language-asset-compiler-host')
    ]:
        output = step(name, ['python3', 'tools/test_policy.py', 'tool', selector], env=test_env)
        assert re.search(r'Summary: Passed: \d+ Failed: 0', output), output[-4000:]
        receipt['steps'][-1]['summaries'] = [line for line in output.splitlines()
            if line.startswith(('Summary:', 'Test command wall time:'))]
        if name == 'portable':
            assert 'BYTECODE: 23 tests, 0 failed' in output and 'SLJIT: 23 tests, 0 failed' in output
        if name == 'compiler':
            combinations = re.findall(r'Stage-one compiler (\w+) -> target (\w+): 23 authored tests', output)
            assert set(combinations) == {('BYTECODE', 'BYTECODE'), ('BYTECODE', 'SLJIT'),
                                         ('SLJIT', 'BYTECODE'), ('SLJIT', 'SLJIT')}
            assert len(combinations) == 4
            assert 'portable_public_consumer_compiles_with_authored_compiler (OK)' in output
            receipt['authored_test_matrix'] = [list(pair) for pair in combinations]
            receipt['tests_per_matrix_combination'] = 23
            receipt['target_backend_no_fallback_assertions_passed'] = True
        save()
    consumer = build / 'external-consumer'
    consumer.mkdir()
    source_consumer = root / 'test/packages/portable_consumer/Consumer.chibi'
    shutil.copyfile(source_consumer, consumer / 'Consumer.chibi')
    assert sha(consumer / 'Consumer.chibi') == sha(source_consumer)
    source = source_consumer.read_text()
    assert len(re.findall(r'@test\s+PublicImports\s*::', source)) == 1
    apis = {}
    for alias, folder in [('bytes', 'bytes'), ('bits', 'math/bits'), ('fnv', 'hash/fnv'),
                          ('adler', 'hash/adler32'), ('crc', 'hash/crc32')]:
        names = []
        for path in sorted((relocated / 'share/chibi/core' / folder).glob('*.chibi')):
            names.extend(re.findall(r'pub\s+(\w+)\s*::\s*\(', path.read_text()))
        assert names and all(re.search(r'\b' + alias + r'\.' + name + r'\s*\(', source) for name in names)
        apis[folder] = names
    assert sum(map(len, apis.values())) == 34
    receipt['external_consumer_sha256'] = sha(source_consumer)
    receipt['external_consumer_public_apis'] = apis
    receipt['external_consumer_public_api_count'] = 34
    version = step('cli-version', [str(relocated / 'bin/chibi'), 'version'])
    assert f'compiler: {relocated}/bin/chibi-compile' in version
    output = step('cli-test', [str(relocated / 'bin/chibi'), 'test', str(consumer)], cwd=consumer)
    assert 'PASS consumer.PublicImports' in output
    assert re.search(r'\b1 tests?, 0 failed\b', output)
    receipt['external_public_import_tests'] = 1
    docs = build / 'docs-probe'
    (docs / 'docs').mkdir(parents=True)
    (docs / 'README.md').write_text('# SDK relocation proof\n\nPortable bytes remain available.\n')
    (docs / 'docs/site.kdl').write_text('''title "Relocated SDK"
(array)sections {
    - {
        name "Overview"
        (array)pages {
            - {
                title "Home"
                source "README.md"
                output "index.html"
            }
        }
    }
}
''')
    step('cli-doc', [str(relocated / 'bin/chibi'), 'doc', str(docs)], cwd=docs)
    html = docs / 'build/docs/index.html'
    css = docs / 'build/docs/style.css'
    assert html.is_file() and css.is_file()
    assert 'SDK relocation proof' in html.read_text()
    assert '<p>Portable bytes remain available.</p>' in html.read_text()
    receipt['docs_probe'] = {'readme_sha256': sha(docs / 'README.md'),
                             'manifest_sha256': sha(docs / 'docs/site.kdl'),
                             'html_sha256': sha(html), 'css_sha256': sha(css),
                             'rendered_plain_markdown': True}
    receipt['sdk_executable_sha256'] = {str(path.relative_to(relocated)): sha(path)
         for path in sorted((relocated / 'bin').iterdir()) if path.is_file()}
    assert inventory(hidden) == before and inventory(relocated) == before
    receipt['sdk_inventories_unchanged_after_execution'] = True
    assert git('rev-parse', 'HEAD') == expected_head
    assert not git('status', '--porcelain=v1')
    receipt['clean_after'] = True
    receipt['source_status_after'] = git('status', '--porcelain=v1')
    assert all(sha(Path(path)) == value for path, value in old_hashes.items())
    receipt['old_evidence_unchanged'] = True
    receipt['passed'] = True
except BaseException as error:
    receipt['error'] = repr(error)
    raise
finally:
    if hidden.exists():
        assert not normal.exists(), 'the original SDK prefix was recreated during isolation'
        hidden.rename(normal)
    receipt['original_restored'] = normal.is_dir() and not hidden.exists()
    receipt['source_head_after'] = git('rev-parse', 'HEAD')
    receipt['source_status_after'] = git('status', '--porcelain=v1')
    receipt['total_seconds'] = time.monotonic() - started
    save()
    print(f'ORIGINAL RESTORED: {normal}', flush=True)
