import hashlib
import json
import os
from pathlib import Path
import selectors
import shutil
import signal
import subprocess
import time

ROOT = Path.cwd()
WORK = ROOT / '.chibi/math-main/paired-cache-308'
COMMON = WORK / 'baseline-source'
CANDIDATE = WORK / 'candidate-source'
TREE = '706782b1239626574a999d1577aec7ea95018478'
CACHE_COMMIT = '38e169fa4ebe66ba4e71f251b81b9bfe6c5b3e2f'
CACHE_BLOB = '7eb2fc5eaecdb1dd0c6a92a11372e2101ef4a516'
F1A = 'f1a6bdffc9d8bec2de2b55f61bad48a2d8593c2a8ab1fc21cca42f46a6497c9c'
RECEIPT = {'runtime': '30837bd5dd057b93b9fa082d456ce9f3b9ec5bb7',
           'math_tree': TREE, 'math_patch_sha256': 'f69daa55d0e6a9431a14f42dc008bcf2ee0f09b0364e7b940a1e0f0f32474efe',
           'cache_commit': CACHE_COMMIT, 'cache_blob': CACHE_BLOB,
           'profile': 'Release Clang 22; fresh matching native objects/headers/hosts per arm',
           'limits': 'Existing generator budget 10000000000; existing layout-work limits; outer/compiler timeouts 600000ms',
           'arms': []}

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def save():
    (WORK / 'receipt.json').write_text(json.dumps(RECEIPT, indent=2) + '\n')

def report(phase, **kwargs):
    print(json.dumps({'phase': phase, **kwargs}), flush=True)

def manifest(folder, paths):
    result = {}
    for name in paths:
        path = folder / name
        data = ('symlink:' + os.readlink(path)).encode() if path.is_symlink() else path.read_bytes()
        result[name] = hashlib.sha256(data).hexdigest()
    return result

def logged(command, path):
    start = time.monotonic()
    with path.open('w') as log:
        run = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    result = {'command': list(map(str, command)), 'status': run.returncode,
              'seconds': round(time.monotonic() - start, 3), 'log': str(path)}
    return result

def stop(process):
    if process.poll() is not None:
        return
    os.killpg(process.pid, signal.SIGTERM)
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()

def ordinary(command, path):
    start = time.monotonic()
    banners = []
    unexpected = False
    timed_out = False
    process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, start_new_session=True)
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    pending = b''
    with path.open('wb') as log:
        try:
            while True:
                if time.monotonic() - start > 600:
                    timed_out = True
                    stop(process)
                for key, _ in selector.select(timeout=1):
                    data = os.read(key.fileobj.fileno(), 65536)
                    if not data:
                        selector.unregister(key.fileobj)
                        continue
                    log.write(data)
                    log.flush()
                    pending += data
                    while b'\n' in pending:
                        line, pending = pending.split(b'\n', 1)
                        text = line.decode(errors='replace')
                        if 'asset compiler:' in text or 'compiler-host bytecode=' in text or 'layout work limit' in text:
                            banners.append(text)
                            report('asset-output', line=text)
                        if ('bootstrap reuse status ' in text and 'bootstrap reuse status 0' not in text) or 'invoke bootstrap fallback' in text:
                            unexpected = True
                            stop(process)
                if process.poll() is not None and not selector.get_map():
                    break
        finally:
            stop(process)
            selector.close()
    return {'command': list(map(str, command)), 'status': process.returncode,
            'seconds': round(time.monotonic() - start, 3), 'log': str(path),
            'phases': banners, 'unexpected_extra_producer_guard': unexpected,
            'outer_600_second_supervisor_expired': timed_out}

try:
    assert not COMMON.exists() and not CANDIDATE.exists(), 'Pair source roots must be fresh'
    COMMON.mkdir()
    archive = subprocess.Popen(['git', 'archive', TREE], stdout=subprocess.PIPE)
    subprocess.run(['tar', '-x', '-C', str(COMMON)], stdin=archive.stdout, check=True)
    archive.stdout.close()
    assert archive.wait() == 0
    tracked = subprocess.check_output(['git', 'ls-tree', '-r', '--name-only', TREE], text=True).splitlines()
    frozen = manifest(COMMON, tracked)
    seed_dir = ROOT / '.chibi/math-main/experimental-bootstrap'
    assert sha(seed_dir / 'compiler.chibip') == F1A
    seed_manifest = json.loads((seed_dir / 'bootstrap.json').read_text())
    seed_request = json.loads((ROOT / '.chibi/math-main/seed/compile.json').read_text())
    assert seed_manifest['target'] == seed_request[6:]
    inputs = (COMMON / 'tools/compiler/asset_compiler/CompilerSources.txt').read_text().splitlines()
    assert inputs == seed_request[3] and len(inputs) == 30
    for name, text in zip(inputs, seed_request[4]):
        path = COMMON / ('packages/' + name[5:] if name.startswith('/sdk/') else 'tools/compiler/' + name)
        assert path.read_text() == text, name
    for name in ['compiler.chibip', 'bootstrap.json']:
        shutil.copy2(seed_dir / name, COMMON / 'tools/compiler/bootstrap' / name)
    common_hashes = manifest(COMMON, tracked)
    changed = [p for p in tracked if frozen[p] != common_hashes[p]]
    assert sorted(changed) == ['tools/compiler/bootstrap/bootstrap.json', 'tools/compiler/bootstrap/compiler.chibip']
    subprocess.run(['cp', '-cR', str(COMMON), str(CANDIDATE)], check=True)
    cache = subprocess.check_output(['git', 'show', CACHE_COMMIT + ':src/execution/prepared_record.c'])
    assert hashlib.sha1(b'blob ' + str(len(cache)).encode() + b'\0' + cache).hexdigest() == CACHE_BLOB
    (CANDIDATE / 'src/execution/prepared_record.c').write_bytes(cache)
    candidate_hashes = manifest(CANDIDATE, tracked)
    assert [p for p in tracked if candidate_hashes[p] != common_hashes[p]] == ['src/execution/prepared_record.c']
    (WORK / 'source-hashes.json').write_text(json.dumps({'frozen706': frozen, 'baseline': common_hashes, 'candidate': candidate_hashes}, indent=2) + '\n')
    RECEIPT['preflight'] = {'source_count': 30, 'ordered_paths_and_texts_match_f1a': True,
                            'common_input_root': str(COMMON), 'artifact_sha256': F1A,
                            'manifest_sha256': sha(seed_dir / 'bootstrap.json'),
                            'source_difference': ['src/execution/prepared_record.c'],
                            'experimental_artifact_overlay_only_in_task_private_sources': changed,
                            'target': seed_manifest['target']}
    save()
    report('preflight-pass', source_count=30, cache_blob=CACHE_BLOB)
    template = json.loads((ROOT / '.chibi/math-main/runtime308-build/configure.json').read_text())['command']
    for name, source, expected_hashes in [('baseline', COMMON, common_hashes), ('cache', CANDIDATE, candidate_hashes)]:
        arm = {'name': name, 'source': str(source), 'build': str(WORK / (name + '-build'))}
        RECEIPT['arms'].append(arm)
        build = Path(arm['build'])
        command = list(template)
        command[command.index('-S') + 1] = str(source / 'test/language')
        command[command.index('-B') + 1] = str(build)
        report(name + '-configure')
        arm['configure'] = logged(command, WORK / (name + '-configure.log'))
        save()
        if arm['configure']['status']:
            raise RuntimeError(name + ' configure failed')
        report(name + '-fresh-native-build')
        arm['build_result'] = logged(['cmake', '--build', str(build), '--target', 'chibi-build-host', 'chibi-compiler-host', '--parallel', '4'], WORK / (name + '-build.log'))
        save()
        if arm['build_result']['status']:
            raise RuntimeError(name + ' native host build failed')
        hosts = {host: build / 'sdk' / host for host in ['chibi-build-host', 'chibi-compiler-host']}
        host_hashes = {host: sha(path) for host, path in hosts.items()}
        target_file = WORK / (name + '-target.json')
        arm['target_check'] = logged([str(hosts['chibi-compiler-host']), '--target-json', str(target_file)], WORK / (name + '-target.log'))
        assert arm['target_check']['status'] == 0
        assert json.loads(target_file.read_text()) == seed_manifest['target']
        assert manifest(source, tracked) == expected_hashes
        assert manifest(COMMON, tracked) == common_hashes
        arm['host_sha256'] = host_hashes
        arm['target_sha256'] = sha(target_file)
        output = WORK / (name + '-assets')
        ordinary_command = [str(hosts['chibi-build-host']), '--compiler', str(COMMON / 'tools/compiler'),
                            '--compiler-core', str(COMMON / 'packages/core'), '--root', str(COMMON),
                            '--source', 'tools/toolchain/AssetCompiler.chibi', '--core', 'packages/core',
                            '--collection', 'asset_generator=tools/graph/asset_compiler', '--entry', 'Build',
                            '--timeout-ms', '600000', '--', str(hosts['chibi-compiler-host']), str(output),
                            '--compiler-timeout-ms=600000']
        report(name + '-ordinary-assets-start', host_sha256=host_hashes, target_sha256=arm['target_sha256'])
        arm['ordinary_assets'] = ordinary(ordinary_command, WORK / (name + '-assets.log'))
        arm['sources_unchanged_after_attempt'] = manifest(source, tracked) == expected_hashes and manifest(COMMON, tracked) == common_hashes
        arm['hosts_unchanged_after_attempt'] = all(sha(path) == host_hashes[host] for host, path in hosts.items())
        assert arm['sources_unchanged_after_attempt'] and arm['hosts_unchanged_after_attempt']
        if arm['ordinary_assets']['status'] == 0:
            arm['source_snapshot_verification'] = logged(['cmake', '-DROOT=' + str(COMMON), '-DRESOURCE=' + str(output), '-P', str(COMMON / 'cmake/verify_source_snapshot.cmake')], WORK / (name + '-inventory.log'))
        arm['output_files'] = {str(p.relative_to(output)): {'bytes': p.stat().st_size, 'sha256': sha(p)} for p in output.rglob('*') if p.is_file()}
        save()
        report(name + '-ordinary-assets-finished', **{k: arm['ordinary_assets'][k] for k in ['status', 'seconds', 'unexpected_extra_producer_guard']})
        if arm['ordinary_assets']['unexpected_extra_producer_guard']:
            raise RuntimeError('Unexpected bootstrap producer required; experiment stopped for coordinator review')
    RECEIPT['complete'] = True
    save()
    report('pair-complete', outcomes=[{'arm': a['name'], 'status': a['ordinary_assets']['status'], 'seconds': a['ordinary_assets']['seconds']} for a in RECEIPT['arms']])
except BaseException as error:
    RECEIPT['orchestration_error'] = repr(error)
    save()
    report('orchestration-stopped', error=repr(error))
    raise
