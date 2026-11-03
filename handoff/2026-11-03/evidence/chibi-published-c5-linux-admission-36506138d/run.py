import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

root = Path('/private/tmp/chibi-published-c5-linux-admission-36506138d')
source = root / 'source'
receipt = json.loads((root / 'receipt.json').read_text())
manifest = json.loads((root / 'source-manifest.json').read_text())

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def save():
    (root / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')

def guard():
    assert all(digest(source / name) == value for name, value in manifest.items())
    assert digest(receipt['seed']) == receipt['seed_sha256']

def run(name, command):
    log = root / (name + '.log')
    started = time.monotonic()
    with log.open('wb') as output:
        result = subprocess.run(command, cwd=source, stdout=output, stderr=subprocess.STDOUT)
    receipt['phases'].append({'name': name, 'command': command, 'status': result.returncode,
                              'wall_seconds': round(time.monotonic() - started, 3),
                              'log': str(log), 'log_sha256': digest(log)})
    if result.returncode:
        receipt['status'] = 'failed: ' + name
    guard()
    receipt['source_and_seed_unchanged'] = True
    save()
    print(json.dumps(receipt['phases'][-1]), flush=True)
    if result.returncode:
        raise SystemExit(result.returncode if result.returncode > 0 else 1)

guard()
receipt['started_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
receipt['environment'] = {'CC': os.environ['CC'], 'CXX': os.environ['CXX']}
receipt['status'] = 'running'
save()
llvm = receipt['llvm']
seed = receipt['seed']
core = str(source / 'packages/core')
project = str(source / 'tools/compiler')
driver = str(source / 'tools/toolchain/Driver.chibi')
graph = 'graph=' + str(source / 'tools/graph')
target = 'x86_64-unknown-linux-gnu'
base = ['--compiler', project, '--core', core, '--llvm', llvm, '--target', target, '--jobs', '2']
run('c5-controller', [seed, *base, '--source', driver, '--entry', 'Main',
                      '--collection', graph, '--output', str(root / 'driver-from-c5')])
(root / 'stage2').mkdir()
run('c5-current-compiler', [seed, *base, '--output', str(root / 'stage2/chibi')])
receipt['stage2_sha256'] = digest(root / 'stage2/chibi')
save()
run('current-version', [str(root / 'stage2/chibi'), 'version'])
run('current-controller', [str(root / 'stage2/chibi'), *base, '--source', driver,
                           '--entry', 'Main', '--collection', graph,
                           '--output', str(root / 'driver-from-current')])
guard()
receipt['status'] = 'pass: published Linux c5 compiles current controller and full compiler; generated compiler runs and compiles current controller'
receipt['completed_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
receipt['scope_limits'] = 'No fixed-point equality, Windows execution or complete CI is claimed by this admission probe.'
save()
