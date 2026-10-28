#!/usr/bin/env python3
"""Run the SDK-free C website and formatter-boundary dispatcher selectors."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

from formatter.build import ROOT, SOURCES, TREE_SITTER, git, require


def check(runtime, sanitize):
    require(sys.platform in ('linux', 'darwin'), 'The website fixture runner supports Linux/macOS')
    runtime = runtime.resolve()
    require(git(runtime, 'rev-parse', 'HEAD') == TREE_SITTER and
            not git(runtime, 'status', '--porcelain'), 'Use the exact clean Tree-sitter source')
    revision = git(ROOT, 'rev-parse', 'HEAD')
    sources = [ROOT / name for name in (
        'test/tools/main.c', 'test/tools/fixture.c', 'test/tools/case_timings.c',
        'test/tools/docs_website.c', 'test/tools/formatter_web.c',
        'src/host/process.c', 'src/support/sha256.c', *SOURCES)]
    sources.append(runtime / 'lib/src/lib.c')
    cc = shlex.split(os.environ.get('CC', 'cc'))
    with tempfile.TemporaryDirectory(prefix='chibi-docs-tests-') as temporary:
        folder = Path(temporary)
        binary = folder / 'test_tools'
        command = [*cc, '-std=gnu11', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-multichar',
                   '-ffunction-sections', '-fdata-sections', '-D_DEFAULT_SOURCE',
                   '-DCHIBI_TEST_DOCS_ONLY=1', '-DCHIBI_TEST_FORMATTER_WEB=1',
                   f'-DCHIBI_SOURCE_ROOT="{ROOT}"', f'-DCHIBI_FORMAT_REVISION="{revision}"',
                   '-I' + str(ROOT), '-I' + str(ROOT / 'src'), '-I' + str(ROOT / 'src/language/src'),
                   '-I' + str(runtime / 'lib/include'), '-I' + str(runtime / 'lib/src')]
        if sanitize:
            command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        command += [*map(str, sources), '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections',
                    '-o', str(binary)]
        print(json.dumps({'revision': revision, 'worktree_dirty': bool(git(ROOT, 'status', '--porcelain')),
                          'sanitize': sanitize, 'command': command}), flush=True)
        subprocess.run(command, cwd=ROOT, check=True)
        environment = dict(os.environ, TMPDIR=str(folder))
        for selector in ('--docs-website', '--formatter-web'):
            # Reuse the shared supervisor's timeout and descendant cleanup;
            # killing only the dispatcher can leave its helper children alive.
            subprocess.run([sys.executable, str(ROOT / 'tools/test_policy.py'),
                            '--timeout', '60', 'native', str(binary), selector],
                           cwd=ROOT, env=environment, check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--sanitize', action='store_true')
    arguments = parser.parse_args()
    check(arguments.runtime, arguments.sanitize)
