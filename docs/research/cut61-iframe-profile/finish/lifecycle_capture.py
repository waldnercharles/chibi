#!/usr/bin/env python3
"""Validate the CUT-61 exact-case selector without starting graphics."""
import json
import os
from pathlib import Path
import subprocess
import sys

suite = Path(sys.argv[1] if len(sys.argv) == 2 else 'build/test_suite').resolve()
selector = '--studio-panel-lifecycle'


def check(args, expected, text, *, profile='hardware', allow='1'):
    env = dict(os.environ, CHIBI_EXECUTION_PROFILE=profile,
               CHIBI_ALLOW_FULL_GPU=allow, CHIBI_IFRAME_PROFILE='1')
    # Only the explicit validation mode or rejected invocations run here.
    assert not (args == [selector] and profile == 'hardware' and allow == '1')
    p = subprocess.run([str(suite), *args], env=env, capture_output=True,
                       text=True, timeout=10)
    output = p.stdout + p.stderr
    assert p.returncode == expected, (args, p.returncode, output)
    assert text in output, (args, output)
    assert 'RmlUi info:' not in output and 'STUDIO_FRAME ' not in output, output
    print('PASS:', ' '.join(args), f'({profile}, allow={allow}, exit={p.returncode})')
    return output


for profile, allow in [('iteration', '0'), ('hardware', '1')]:
    value = json.loads(check([selector, 'validate'], 0, 'validation_only',
                             profile=profile, allow=allow))
    assert value == dict(selector=selector, case='area_panels_take_turns(false)',
                         expected_frames=2016, graphics_started=False,
                         validation_only=True)
for args in [[selector, 'typo'], [selector, 'validate', 'extra'],
             [selector, '--studio-panels'], [selector, 'validate', '--studio-panels'],
             [selector, 'false']]:
    check(args, 2, 'usage:')
check([selector], 2, 'requires_hardware_or_classification', profile='iteration', allow='0')
check([selector], 2, 'requires_hardware_or_classification', profile='hardware', allow='0')
check([selector + '-typo'], 2, 'unknown test_suite selector:')
check(['--list-tests'], 0, selector, profile='iteration', allow='0')
print('11 selector checks passed; no native lifecycle case executed.')
