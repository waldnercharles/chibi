"""Tiny synthetic metadata/encoding checks against native sampled-SDF limits."""
from pathlib import Path
import sys
import tempfile
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/mesh_sdf'))
from atlas import encode, validate_metadata


def rejects(args, diagnostic):
    try:
        validate_metadata(*args)
    except ValueError as e:
        assert diagnostic in str(e), str(e)
        return
    raise AssertionError(f'accepted invalid metadata: {args}')


validate_metadata(144, [1e6]*3, 1e6, 16)
for half in (1e-5, 1e6+1, float('inf')):
    rejects((3, [half]*3, 1, 1), 'Half Size')
for value in (0, 1e6+1, float('nan'), 1e-100):
    rejects((3, [1]*3, value, 1), 'Range')
for value in (.99, 16.01, float('inf')):
    rejects((3, [1]*3, 1, value), 'Lipschitz')
for n in (1, 145):
    rejects((n, [1]*3, 1, 1), 'Resolution')
with tempfile.TemporaryDirectory() as tmp:
    destination = Path(tmp)
    field = np.ones((3,3,3)); field[1,1,1] = -100
    try:
        encode(field, np.ones(3), destination)
    except ValueError as e:
        assert 'Lipschitz' in str(e), str(e)
    else:
        raise AssertionError('published excessive-gradient field')
    assert not list(destination.iterdir())
print('Native sampled-SDF metadata bounds PASS')
