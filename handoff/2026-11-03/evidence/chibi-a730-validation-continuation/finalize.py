import hashlib
import json
from pathlib import Path

work = Path(__file__).parent
frozen = Path('/private/tmp/chibi-a730-composed-proof')
source = frozen / 'source'
receipt_path = work / 'receipt.json'
receipt = json.loads(receipt_path.read_text())

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

assert sha(frozen / 'receipt.json') == receipt['original_receipt_sha256']
assert sha(frozen / 'source-files.json') == receipt['original_manifest_sha256']
assert sha(frozen / 'managed-pointers.log') == receipt['original_managed_log_sha256']
assert sha(frozen / 'source.tar') == receipt['source']['archive_sha256']
assert sha(Path(receipt['compiler'])) == receipt['compiler_sha256']
items = json.loads((frozen / 'source-files.json').read_text())
for item in items:
    path = source / item['path']
    data = path.readlink().as_posix().encode() if item['mode'] == '120000' else path.read_bytes()
    assert hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest() == item['blob'], item['path']
    if item['mode'] != '120000':
        assert bool(path.stat().st_mode & 0o111) == (item['mode'] == '100755'), item['path']

steps = {item['name']: item for item in receipt['steps']}
for path in work.glob('*.receipt.json'):
    item = json.loads(path.read_text())
    steps[item['name']] = item
for item in steps.values():
    assert sha(Path(item['log'])) == item['log_sha256'], item['name']
for name in ['native-probe-build', 'native-probe', 'managed-rejections', 'maps', 'generations-O0', 'generations-O2']:
    assert steps[name]['exit'] == 0, name
assert steps['container-positive']['exit'] == 1
assert steps['container-positive']['expected_exit'] == 1
assert steps['metadata']['exit'] == 1
assert steps['managed-tail']['exit'] == 1
assert 'PASS: PArray cell metadata accepted\nnative scalar slot cannot hold a managed value or pointer' in (work / 'metadata.log').read_text()
assert (work / 'managed-tail.log').read_text().endswith('native managed pointers: unsupported container was accepted\n')
assert 'native managed pointer source, collector and boundary proofs passed' in (work / 'managed-rejections.log').read_text()

overlay = work / 'overlay/NativeProbe.chibi'
assert sha(overlay) == receipt['overlay']['corrected_sha256']
assert sha(work / 'overlay/NativeProbe-f255.chibi.txt') == receipt['overlay']['sha256']
receipt['steps'] = list(steps.values())
receipt['environment'] = {'CC': '/usr/bin/false', 'CXX': '/usr/bin/false',
    'CHIBI_CORE': str(source / 'packages/core'), 'CHIBI_TEST_WORKERS': '1', 'TMPDIR': str(work / 'tmp')}
receipt['archived_source_postverified'] = len(items)
receipt['original_logs_and_receipts_unchanged'] = True
receipt['managed_validation'] = {
    'status': 'pass by explicit continuation with fixture migrations',
    'original_passed_prefix': 'Original managed-pointers.log: run.sh lines1-118 completed before stale NativeProbe metadata rejection; 246.91s original run retained, not repeated.',
    'boundary_probe': 'Corrected bebb7a008 NativeProbe overlay, 54 native runtime runs passed; exact a730 production imports/compiler.',
    'continued_suffix': 'Original run.sh lines125-162, split at obsolete negative container.chibi; views, two-image schemas, source string ownership/unload, eight fallible package tests and earlier negatives passed before that stop.',
    'container_migration': json.loads((work / 'container-migration.json').read_text()),
    'remaining_suffix': 'Only the nine not-yet-run negative fixtures and growth controls, unchanged rejection predicates and sentinel checks.',
    'preserved_failed_attempts': ['metadata', 'managed-tail'],
    'not_a_one_shot_green_run': True,
}
receipt['status'] = 'pass with explicit fixture-only migrations'
receipt_path.write_text(json.dumps(receipt, indent=2) + '\n')
print(json.dumps({'status': receipt['status'], 'archived_source_postverified': len(items),
    'compiler_sha256': receipt['compiler_sha256'], 'receipt_sha256': sha(receipt_path),
    'steps': [{'name': name, 'exit': item['exit'], 'seconds': item['seconds']} for name,item in steps.items()]}, indent=2))
