import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import traceback

root = Path('/private/tmp/chibi-6c49-linux-proof')
source = root / 'source'
receipt = json.loads((root / 'receipt.json').read_text())
environment = dict(os.environ, **receipt['environment'])
llvm = receipt['llvm']
started = time.monotonic()


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save():
    temporary = root / 'receipt.tmp'
    temporary.write_text(json.dumps(receipt, indent=2) + '\n')
    temporary.replace(root / 'receipt.json')


def run(name, arguments, extra=None):
    log = root / (name + '.log')
    before = time.monotonic()
    with log.open('wb') as output:
        result = subprocess.run(arguments, cwd=source,
            env=dict(environment, **(extra or {})), stdout=output, stderr=subprocess.STDOUT)
    phase = dict(name=name, command=arguments, cwd=str(source), status=result.returncode,
        elapsed_s=round(time.monotonic() - before, 3), log=str(log), log_sha256=digest(log))
    receipt['phases'].append(phase)
    save()
    print(json.dumps(phase), flush=True)
    if result.returncode:
        raise RuntimeError(name + ' exited ' + str(result.returncode))
    return phase


def compare(left_number, right_number):
    left = root / ('stage' + str(left_number)) / 'chibi'
    right = root / ('stage' + str(right_number)) / 'chibi'
    command = ['cmp', str(left), str(right)]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode not in (0, 1):
        raise RuntimeError('cmp could not compare the stage files: ' + result.stderr)
    comparison = dict(pair=[left_number, right_number], command=command,
        byte_identical=result.returncode == 0, status=result.returncode,
        output=result.stdout + result.stderr, left_sha256=digest(left), right_sha256=digest(right))
    receipt['comparisons'].append(comparison)
    save()
    print(json.dumps(comparison), flush=True)
    return comparison


def compile_stage(builder, number):
    directory = root / ('stage' + str(number))
    directory.mkdir(exist_ok=True)
    output = directory / 'chibi'
    if output.exists():
        raise RuntimeError('refusing to overwrite stage output: ' + str(output))
    phase = run('stage' + str(number), [builder, '--compiler', str(source / 'tools/compiler'),
        '--core', str(source / 'packages/core'), '--output', str(output), '--llvm', llvm,
        '--target', 'x86_64-unknown-linux-gnu', '--jobs', '2'])
    phase.update(binary=str(output), binary_sha256=digest(output), binary_bytes=output.stat().st_size)
    save()
    return str(output)


def verify_inputs():
    problems = []
    try:
        assert digest(root / 'source-files.json') == receipt['source_file_inventory_sha256'], 'source manifest changed'
        manifest = json.loads((root / 'source-files.json').read_text())
        assert len(manifest) == receipt['source_files'], 'source manifest count changed'
        for item in manifest:
            path = source / item['path']
            if item['mode'] == '120000':
                assert path.is_symlink(), 'not a symlink: ' + item['path']
                data = os.fsencode(os.readlink(path))
            else:
                assert not path.is_symlink(), 'unexpected symlink: ' + item['path']
                data = path.read_bytes()
                assert bool(path.stat().st_mode & 0o111) == (item['mode'] == '100755'), 'mode: ' + item['path']
            assert len(data) == item['bytes'], 'size: ' + item['path']
            assert hashlib.sha256(data).hexdigest() == item['sha256'], 'sha256: ' + item['path']
            actual_blob = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
            assert actual_blob == item['git_blob'], 'git blob: ' + item['path']
        receipt['archived_source_reverified'] = len(manifest)
    except BaseException as error:
        problems.append('source verification: ' + repr(error))
    try:
        receipt['archive_hash_unchanged'] = digest(root / 'source.tar') == receipt['archive_sha256']
        assert receipt['archive_hash_unchanged'], 'source archive changed'
        receipt['builder_hash_unchanged'] = digest(receipt['builder']) == receipt['builder_sha256']
        assert receipt['builder_hash_unchanged'], 'original builder changed'
        receipt['runner_hash_unchanged'] = digest(root / 'run.py') == receipt['runner_sha256']
        assert receipt['runner_hash_unchanged'], 'proof controller changed'
    except BaseException as error:
        problems.append('artifact verification: ' + repr(error))
    receipt['final_verification_problems'] = problems
    return problems


exit_code = 1
try:
    if receipt['status'] != 'frozen' or receipt['phases']:
        raise RuntimeError('proof controller is single-use; prior work is retained')
    receipt['status'] = 'running'
    receipt['started_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    receipt['runner_sha256'] = digest(root / 'run.py')
    save()
    run('llvm-version', [llvm + '/opt', '--version'])
    run('platform', ['/bin/uname', '-a'])
    builder = compile_stage(receipt['builder'], 2)
    builder = compile_stage(builder, 3)
    comparison = compare(2, 3)
    if not comparison['byte_identical']:
        receipt['initial_pair_differed'] = True
        save()
        builder = compile_stage(builder, 4)
        comparison = compare(3, 4)
    receipt['fixed_point'] = comparison
    save()
    if not comparison['byte_identical']:
        raise RuntimeError('neither allowed consecutive stage pair is byte-identical; no additional stages attempted')
    print('PASS exact consecutive stage equality: ' + comparison['right_sha256'], flush=True)
    run('compiler-tests', [builder, 'test', '--root', str(source / 'tools/compiler'),
        '--llvm', llvm, str(source / 'tools/compiler')], {'CHIBI_CORE': str(source / 'packages/core')})
    test_output = (root / 'compiler-tests.log').read_text()
    inventory = re.findall(r'^PASS (.+)$', test_output, re.MULTILINE)
    summaries = re.findall(r'^(\d+) tests, (\d+) failed\s*$', test_output, re.MULTILINE)
    (root / 'compiler-inventory.json').write_text(json.dumps(inventory, indent=2) + '\n')
    receipt['compiler_test_pass_lines'] = len(inventory)
    receipt['compiler_unique_test_names'] = len(set(inventory))
    receipt['compiler_test_inventory_sha256'] = digest(root / 'compiler-inventory.json')
    receipt['compiler_test_summary'] = [line for line in test_output.splitlines() if re.search(r'\d+ tests|\d+ passed|\d+ failed', line)]
    save()
    assert summaries and int(summaries[-1][0]) == len(inventory) > 0 and int(summaries[-1][1]) == 0, 'compiler summary/pass inventory mismatch'
    assert len(set(inventory)) == len(inventory), 'duplicate compiler test PASS names'
    focused = [
        ('task_owners/TaskObservers', 'Main'),
        ('managed_pointers/source/PointerViews', 'Main'),
        ('managed_pointers/source/PointerViews', 'Tasks'),
        ('remove_item/Maps', 'Main'),
    ]
    for level in (0, 2):
        for relative, entry in focused:
            name = relative.rsplit('/', 1)[1] + '-' + entry + '-O' + str(level)
            output = root / 'programs' / name
            phase = run('compile-' + name, [builder, '--compiler', str(source / 'tools/compiler'),
                '--core', str(source / 'packages/core'), '--source', str(source / 'test/native_aot' / (relative + '.chibi')),
                '--entry', entry, '--output', str(output), '--llvm', llvm,
                '--target', 'x86_64-unknown-linux-gnu', '--level', str(level), '--jobs', '2'])
            phase.update(binary=str(output), binary_sha256=digest(output), binary_bytes=output.stat().st_size)
            save()
            run('execute-' + name, [str(output)])
    receipt['status'] = 'pass'
    exit_code = 0
except BaseException as error:
    receipt['status'] = 'failed'
    receipt['error'] = repr(error)
    receipt['traceback'] = traceback.format_exc()
    print(receipt['traceback'], file=sys.stderr, flush=True)
finally:
    verification = verify_inputs()
    if verification:
        receipt['status'] = 'failed verification'
        exit_code = 2
    receipt['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    receipt['controller_elapsed_s'] = round(time.monotonic() - started, 3)
    receipt['total_recorded_phase_seconds'] = round(sum(item['elapsed_s'] for item in receipt['phases']), 3)
    receipt['focused_compile_and_execute_seconds'] = round(sum(item['elapsed_s'] for item in receipt['phases'] if item['name'].startswith(('compile-', 'execute-'))), 3)
    save()
    print(json.dumps({key: receipt.get(key) for key in ('status', 'error', 'archived_source_reverified', 'archive_hash_unchanged', 'builder_hash_unchanged', 'runner_hash_unchanged', 'final_verification_problems', 'controller_elapsed_s')}), flush=True)
sys.exit(exit_code)
