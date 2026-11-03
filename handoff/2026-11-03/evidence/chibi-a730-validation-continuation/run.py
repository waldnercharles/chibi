import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

work = Path(__file__).parent
frozen = Path('/private/tmp/chibi-a730-composed-proof')
source = frozen / 'source'
receipt_path = work / 'receipt.json'
receipt = json.loads(receipt_path.read_text())
compiler = receipt['compiler']
llvm = receipt['llvm']
env = os.environ.copy()
env.update(CC='/usr/bin/false', CXX='/usr/bin/false', CHIBI_CORE=str(source / 'packages/core'), CHIBI_TEST_WORKERS='1')
env['TMPDIR'] = str(work / 'tmp')
Path(env['TMPDIR']).mkdir(exist_ok=True)

def run(name, command, expected=0):
    start = time.monotonic()
    log = work / (name + '.log')
    with log.open('w') as output:
        result = subprocess.run(command, cwd=source, env=env, stdout=output, stderr=subprocess.STDOUT)
    item = {'name': name, 'command': command, 'exit': result.returncode, 'expected_exit': expected,
            'seconds': round(time.monotonic() - start, 2), 'log': str(log),
            'log_sha256': hashlib.sha256(log.read_bytes()).hexdigest()}
    # Independent phases leave separate receipts; finalization joins them after
    # the already-running managed continuation updates its original receipt.
    (work / (name + '.receipt.json')).write_text(json.dumps(item, indent=2) + '\n')
    print(json.dumps(item), flush=True)
    if result.returncode != expected:
        print(log.read_text()[-10000:], flush=True)
        raise SystemExit(result.returncode)

def build(path, output):
    return [compiler, '--compiler', str(source / 'tools/compiler'), '--core', str(source / 'packages/core'),
            '--source', str(path), '--entry', 'Main', '--output', str(output), '--llvm', llvm,
            '--target', 'aarch64-apple-darwin', '--jobs', '1']

if sys.argv[1] == 'metadata':
    run('metadata-build', build(work / 'Metadata.chibi', work / 'metadata'))
    run('metadata', [str(work / 'metadata')])
elif sys.argv[1] == 'native-probe':
    probe_work = work / 'managed'
    probe_work.mkdir(exist_ok=True)
    run('native-probe-build', build(work / 'overlay/NativeProbe.chibi', probe_work / 'native-probe'))
    run('native-probe', [str(probe_work / 'native-probe'), str(probe_work), llvm,
                        str(source / 'test/native_aot/managed_pointers/Runtime.ll'),
                        str(source / 'test/native_aot/managed_pointers/SourceAudit.ll')])
elif sys.argv[1] == 'managed-tail':
    run('managed-tail', ['sh', str(work / 'managed-tail.sh'), compiler, llvm, str(work / 'managed')])
elif sys.argv[1] == 'container-positive':
    run('container-positive', [str(work / 'managed/container-positive')], expected=1)
elif sys.argv[1] == 'managed-rejections':
    run('managed-rejections', ['sh', str(work / 'managed-rejections.sh'), compiler, llvm, str(work / 'managed')])
elif sys.argv[1] == 'maps':
    run('maps', ['sh', str(source / 'test/native_aot/maps/run.sh'), compiler, llvm, str(work / 'maps')])
elif sys.argv[1] in ['generations-O0', 'generations-O2']:
    level = sys.argv[1][-1]
    run('generations-O' + level, ['sh', str(source / 'test/native_aot/generations/run.sh'), compiler, llvm, level])
else:
    raise SystemExit('unknown continuation phase')
