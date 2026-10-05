"""Offline installer control-flow tests; no real SDK download or compilation."""
import ast
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
REVISION = 'a2b92777574c2feda07994cd4f1079a3dfc151f8'
RELEASE = 'releases-666337b525e673e769121856d175f6f52b8ead64-64bit'

# One local executable stands in for git, emsdk and emcc. Unknown invocations
# fail closed. No real git, emsdk, compiler or network client is invoked.
STUB = r'''
import json
import os
from pathlib import Path
import shutil
import sys
root = Path(os.environ['STUB_ROOT'])
name = Path(sys.argv[0]).name
args = sys.argv[1:]
with (root / 'calls.jsonl').open('a') as log:
    log.write(json.dumps({'tool': name, 'args': args,
                          'config': os.environ.get('EM_CONFIG')}) + '\n')
if name == 'git':
    if args[:6] == ['clone', '--depth', '1', '--branch', '6.0.10',
                    'https://github.com/emscripten-core/emsdk.git'] and len(args) == 7:
        sdk = Path(args[6])
        sdk.mkdir(parents=True)
        (sdk / 'revision').write_text(os.environ['STUB_REVISION'])
        shutil.copy2(root / 'stub', sdk / 'emsdk')
    elif len(args) == 4 and args[0] == '-C' and args[2:] == ['rev-parse', 'HEAD']:
        print((Path(args[1]) / 'revision').read_text())
    else:
        sys.exit(91)
elif name == 'emsdk':
    if args != ['install', os.environ['STUB_RELEASE']]:
        sys.exit(92)
    if (root / 'fail-install').exists():
        print('stub installation failure', file=sys.stderr)
        sys.exit(42)
    sdk = Path(sys.argv[0]).parent
    marker = sdk / 'installed'
    if not marker.exists():
        marker.write_text('stub installed once')
        emcc = sdk / 'upstream/emscripten/emcc'
        emcc.parent.mkdir(parents=True)
        shutil.copy2(root / 'stub', emcc)
        print('stub install')
    else:
        print('stub reuse')
elif name == 'emcc':
    if args != ['--version']:
        sys.exit(93)
    if (root / 'fail-version').exists():
        sys.exit(43)
    if not Path(os.environ['EM_CONFIG']).is_file():
        sys.exit(94)
    print('emcc 6.0.10 (offline stub)')
else:
    sys.exit(95)
'''


class EmscriptenSetup(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='cloud-emsdk-test-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        # Exercise Python config quoting, without violating the whitespace ban.
        self.prefix = self.root / "tools'local"
        self.sdk = self.prefix / 'emsdk-6.0.10'
        self.config = self.prefix / 'emscripten-6.0.10-config'
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        stub = self.root / 'stub'
        stub.write_text(f'#!{sys.executable}\n' + STUB)
        stub.chmod(0o755)
        shutil.copy2(stub, self.bin / 'git')
        shutil.copy2(stub, self.bin / 'node')
        (self.bin / 'python3').symlink_to(sys.executable)
        self.bash = shutil.which('bash')
        # Deliberately exclude host PATH and inherited Emscripten variables.
        self.env = {'PATH': str(self.bin), 'STUB_ROOT': str(self.root),
                    'STUB_REVISION': REVISION, 'STUB_RELEASE': RELEASE,
                    'CHIBI_CLOUD_TOOLS': str(self.prefix)}

    def setup(self, **overrides):
        return subprocess.run([self.bash, str(ROOT / 'tools/cloud/emscripten_setup.sh')],
                              cwd=self.root, env=dict(self.env, **overrides),
                              capture_output=True, text=True, timeout=10)

    def calls(self, tool):
        log = self.root / 'calls.jsonl'
        rows = [json.loads(line) for line in log.read_text().splitlines()] if log.exists() else []
        return [row for row in rows if row['tool'] == tool]

    def assert_config(self):
        values = {}
        for statement in ast.parse(self.config.read_text()).body:
            self.assertIsInstance(statement, ast.Assign)
            values[statement.targets[0].id] = ast.literal_eval(statement.value)
        self.assertEqual(values, {
            'LLVM_ROOT': str(self.sdk / 'upstream/bin'),
            'BINARYEN_ROOT': str(self.sdk / 'upstream'),
            'NODE_JS': [str(self.bin / 'node')],
            'CACHE': str(self.prefix / 'emscripten-cache-6.0.10'),
        })
        self.assertEqual(self.calls('emcc')[-1]['config'], str(self.config))

    def test_install_reuse_and_generated_config(self):
        first = self.setup(EM_CONFIG='/unused/inherited-config')
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertIn('stub install', first.stdout)
        self.assert_config()
        original = self.config.read_bytes()
        marker = (self.sdk / 'installed').stat().st_mtime_ns
        second = self.setup()
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertIn('stub reuse', second.stdout)
        self.assertEqual(original, self.config.read_bytes())
        self.assertEqual(marker, (self.sdk / 'installed').stat().st_mtime_ns)
        self.assertEqual(sum(call['args'][0] == 'clone' for call in self.calls('git')), 1)
        self.assertEqual([call['args'] for call in self.calls('emsdk')],
                         [['install', RELEASE], ['install', RELEASE]])

    def test_failed_install_retries_existing_checkout(self):
        failure = self.root / 'fail-install'
        failure.touch()
        result = self.setup()
        self.assertEqual(result.returncode, 42, result.stderr)
        self.assertFalse(self.config.exists())
        self.assertFalse((self.sdk / 'installed').exists())
        self.assertEqual(self.calls('emcc'), [])
        failure.unlink()
        result = self.setup()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_config()
        self.assertEqual(sum(call['args'][0] == 'clone' for call in self.calls('git')), 1)

    def test_failed_reinstall_preserves_existing_config(self):
        self.assertEqual(self.setup().returncode, 0)
        original = self.config.read_bytes()
        (self.root / 'fail-install').touch()
        result = self.setup()
        self.assertEqual(result.returncode, 42, result.stderr)
        self.assertEqual(self.config.read_bytes(), original)
        self.assertEqual(len(self.calls('emcc')), 1)

    def test_wrong_revision_stops_before_install(self):
        result = self.setup(STUB_REVISION='unapproved-revision')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Unexpected emsdk revision', result.stderr)
        self.assertEqual(self.calls('emsdk'), [])
        self.assertFalse(self.config.exists())

    def test_failed_version_probe_is_reported_and_recoverable(self):
        failure = self.root / 'fail-version'
        failure.touch()
        result = self.setup()
        self.assertEqual(result.returncode, 43, result.stderr)
        failure.unlink()
        result = self.setup()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('stub reuse', result.stdout)
        self.assert_config()

    def test_invalid_prefix_fails_before_any_tools(self):
        for prefix in ('relative', str(self.root / 'with space')):
            with self.subTest(prefix=prefix):
                result = self.setup(CHIBI_CLOUD_TOOLS=prefix)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('absolute and contain no whitespace', result.stderr)
        self.assertEqual(self.calls('git'), [])


if __name__ == '__main__':
    unittest.main()
