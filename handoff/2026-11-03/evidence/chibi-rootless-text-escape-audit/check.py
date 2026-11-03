import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

work = Path(__file__).parent
source = Path('/private/tmp/chibi-a730-composed-proof/source')
compiler = '/private/tmp/chibi-a730-composed-proof/stage3/chibi'
rows = []
for name in ['ArrayText', 'InlineText', 'InlineTextCell']:
    start = time.monotonic()
    output = work / (name + '.ll')
    command = [compiler, '--compiler', str(source / 'tools/compiler'), '--core', str(source / 'packages/core'),
        '--source', str(work / name / 'Main.chibi'), '--entry', 'Main', '--emit-llvm',
        '--target', 'aarch64-apple-darwin', '--output', str(output)]
    env = os.environ.copy()
    env.update(CC='/usr/bin/false', CXX='/usr/bin/false')
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
    (work / (name + '.log')).write_bytes(result.stdout)
    item = {'name': name, 'command': command, 'exit': result.returncode,
        'seconds': round(time.monotonic() - start, 2), 'diagnostic': result.stdout.decode(),
        'source_sha256': hashlib.sha256((work / name / 'Main.chibi').read_bytes()).hexdigest(),
        'ir_sha256': hashlib.sha256(output.read_bytes()).hexdigest() if output.exists() else None}
    rows.append(item)
    print(json.dumps(item), flush=True)
(work / 'receipt.json').write_text(json.dumps({'revision': 'a73081162986aea06efd625ae25963010ae9cb95',
    'compiler_sha256': hashlib.sha256(Path(compiler).read_bytes()).hexdigest(), 'cases': rows}, indent=2) + '\n')
