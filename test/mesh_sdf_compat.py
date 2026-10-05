"""Check shared encoder against pinned Suzanne code on a tiny synthetic field."""
import hashlib, json, math, struct, subprocess, sys, tempfile, zlib
from pathlib import Path
import numpy as np
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/mesh_sdf'))
from atlas import encode
original = subprocess.check_output(['git', 'show', 'be4d5c0d6d6a38f0747ec5d1c7f66b618c0c9b8a:projects/suzanne_sampled/convert.py'], cwd=ROOT, text=True)
png = original[original.index('def png('):original.index('def bake(')]
body = original[original.index(' field=field.reshape'):original.index('\nsizes=')]
exec(png+'\ndef reference(field,n,half):\n'+body)
with tempfile.TemporaryDirectory() as tmp:
    ROOT = Path(tmp); EVIDENCE = ROOT
    (ROOT/'content/sdf').mkdir(parents=True)
    half = np.array([1., 2., 3.]); n=5
    z,y,x=np.meshgrid(*[np.linspace(-h,h,n) for h in half[::-1]], indexing='ij')
    field=np.sqrt(x*x+y*y+z*z)-.4
    expected=reference(field,n,half)
    expected_bytes=(ROOT/expected['image']).read_bytes()
    result,decoded=encode(field,half,ROOT/'content/sdf')
    assert result==expected
    assert expected_bytes==(ROOT/result['image']).read_bytes()
    assert np.array_equal(decoded,np.load(EVIDENCE/'field-5.npy'))
print('Suzanne shared encoder byte-compatible')
