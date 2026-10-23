from pathlib import Path
import datetime
import hashlib
import json
import os
import signal
import subprocess
import sys
import time

root = Path.cwd()
folder = root / '.chibi/game-support/combined-d0/debug-watch-refactor/ordinary-host'
candidate_path = folder.parent / 'full-candidate/manifest.json'
counter_path = root / '.chibi/game-support/combined-d0/storage-profile/counters/manifest.json'
profile_path = folder.parent / 'measure-v3/profile.json'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

candidate = json.loads(candidate_path.read_text())
counter = json.loads(counter_path.read_text())
profile = json.loads(profile_path.read_text())
assert profile['candidate_tree'] == candidate['tree']
assert profile['exit_code'] == 0 and profile['production_source_verified_unchanged']
assert profile['log_sha256'] == sha(profile_path.with_name('profile.log'))
assert not (folder / 'build.log').exists() and not (folder / 'build.json').exists()

def verify_sources():
    tree = subprocess.check_output(['git', 'write-tree'], cwd=root, text=True).strip()
    assert tree == candidate['tree'], 'index tree differs from frozen candidate'
    subprocess.run(['git', 'diff', '--quiet'], cwd=root, check=True)
    for name, digest in candidate['files'].items():
        assert sha(root / name) == digest, name
    for name, evidence in counter['files'].items():
        assert sha(root / name) == evidence['original_sha256'], name

verify_sources()
host = root / 'build-cmake/sdk/chibi-build-host'
argv = ['cmake', '--build', 'build-cmake', '--target', 'chibi-build-host', '--parallel', '4']
receipt = {
    'candidate_tree': candidate['tree'],
    'candidate_manifest_sha256': sha(candidate_path),
    'successful_load_receipt_sha256': sha(profile_path),
    'counter_manifest_sha256': sha(counter_path),
    'argv': argv,
    'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
}
environment = dict(os.environ)
environment.pop('CHIBI_STORAGE_DIAG', None)
# The exclusive log is ownership of this one run. A competing invocation must
# fail here, before entering any cleanup or receipt-writing path.
output = (folder / 'build.log').open('xb')
handlers = {s: signal.getsignal(s) for s in (signal.SIGINT, signal.SIGTERM)}
cancelled = 0
launching = True
process = None
errors = []
status = 1
started = time.monotonic()

def cancel(signum, frame):
    global cancelled
    cancelled = signum
    if not launching:
        raise KeyboardInterrupt

for signum in handlers:
    signal.signal(signum, cancel)
try:
    with output:
        if cancelled:
            raise KeyboardInterrupt
        # Defer raising until Popen has returned the child handle for cleanup.
        process = subprocess.Popen(argv, cwd=root, env=environment, stdout=output,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        receipt['pid'] = process.pid
        launching = False
        if cancelled:
            raise KeyboardInterrupt
        status = process.wait()
except KeyboardInterrupt:
    status = 128 + (cancelled or signal.SIGINT)
    errors.append('cancelled')
except Exception as error:
    errors.append(repr(error))
finally:
    for signum in handlers:
        signal.signal(signum, signal.SIG_IGN)
    if process:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            try:
                os.killpg(process.pid, 0)
            except ProcessLookupError:
                break
            time.sleep(.05)
        else:
            errors.append('owned build process group remains after cleanup')
    try:
        verify_sources()
        receipt['production_sources_verified_unchanged'] = True
    except Exception as error:
        receipt['production_sources_verified_unchanged'] = False
        errors.append('source verification: ' + repr(error))
    if not status:
        try:
            assert b'STORAGE_DIAG_2AC' not in host.read_bytes(), 'diagnostic marker in ordinary host'
            receipt['ordinary_host'] = {'path': str(host), 'bytes': host.stat().st_size, 'sha256': sha(host)}
        except Exception as error:
            errors.append('ordinary host verification: ' + repr(error))
    if errors:
        status = status or 3
    receipt.update(exit_code=status, elapsed_seconds=round(time.monotonic() - started, 3),
                   completed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                   errors=errors, log_sha256=sha(folder / 'build.log') if (folder / 'build.log').exists() else None)
    with (folder / 'build.json').open('x') as receipt_file:
        receipt_file.write(json.dumps(receipt, indent=2) + '\n')
    for signum, handler in handlers.items():
        signal.signal(signum, handler)
print(json.dumps(receipt, indent=2), flush=True)
sys.exit(status)
