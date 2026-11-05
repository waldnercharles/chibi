#!/usr/bin/env python3
"""Prepare opaque inputs and full oracle tuples; never execute native code.

Run only after reference.py review/tests are separately accepted. Optional
--frame paths append actual externally prepared immutable closure/full-domain
frames. Provenance/closure validation remains the caller's separate gate.
"""
import argparse
import hashlib
import json
import mmap
import os
import stat
import tempfile
from pathlib import Path
import struct
import sys

# Adjacent copied oracle, not C or the compiler under test.
sys.dont_write_bytecode = True
from reference import census

HEADER = struct.Struct('<4I')
RECORD = struct.Struct('<3I')
MAGIC = 827212611

def record(tag=1, package=1, payload=b''):
    return RECORD.pack(tag, package, len(payload)) + payload

def frame(records=(), packages=1, originals=1, generated=0):
    return HEADER.pack(MAGIC, packages, originals, generated)+b''.join(records)

def small_cases():
    # Authored payload domain, including CR/LF semantics and all byte values.
    for index, payload in enumerate((b'', b'source', b'\n', b'\n\n\n',
            b'a\r\n\r\nb\r\n', b'\t \v\f\r\n \n', b'\v\f', b'a\rb',
            b'\r', b'\0\n\0', bytes(range(256)),
            '\u0085\u2028\u2029\u00a0'.encode(), b'// comment\n', b'a\n', b'a\n\t\r',
            b'x'*131079, b' '*131072+b'x\n', b'\n'*65537)):
        yield f'payload-{index}', frame((record(payload=payload),))
    yield 'multi-package', frame((record(1,1,b'a\n'), record(1,1), record(1,2,b'b\n\n')),2,3)
    yield 'replacement', frame((record(1,1,b'a\n'), record(2,1,b'\nold'),
        record(3,1,b'new\n\n'),record(1,2,b'  \n')),2,3,1)
    yield 'empty-pair', frame((record(2),record(3)),generated=1)
    # Every header byte prefix, each header field and every record-header field.
    header = HEADER.pack(MAGIC,1,1,0)
    for count in range(16): yield f'header-prefix-{count}', header[:count]
    for field, values in enumerate(((0,MAGIC+1,0xffffffff),
            (0,4097,0xffffffff),(0,16385,0xffffffff),(2,0xffffffff))):
        for value in values:
            fields = [MAGIC,1,1,0]; fields[field] = value
            yield f'header-field-{field}-{value}', HEADER.pack(*fields)
    yield 'header-only', header
    for count in range(1,12): yield f'record-prefix-{count}', header+record()[:count]
    for field, values in enumerate(((0,4,0xffffffff),(0,2,0xffffffff),(1,0xffffffff))):
        for value in values:
            fields=[1,1,0]; fields[field]=value
            yield f'record-field-{field}-{value}', header+RECORD.pack(*fields)
    complete=frame((record(payload=b'payload'),))
    for missing in range(1,8): yield f'payload-truncated-{missing}', complete[:-missing]
    for label, data in (
        ('unpaired-generated',frame((record(3),),generated=1)),
        ('generated-count',frame((record(2),record(3)))),
        ('generated-size',frame((record(2),record(3,1,b'x'*257)),generated=1)),
        ('extra-original',frame((record(),record()))),
        ('duplicate-replaced',frame((record(2),record(3),record(2)),originals=2,generated=1)),
        ('missing-pair',frame((record(2),),generated=1)),
        ('package-count',frame((record(),),packages=2)),
        ('original-count',frame((record(),),originals=2)),
        ('generated-count-final',frame((record(),),generated=1)),
        ('package-gap',frame((record(1,1),record(1,3)),packages=3,originals=2)),
        ('package-reorder',frame((record(1,1),record(1,2),record(1,1)),packages=2,originals=3)),
        ('pair-interrupted',frame((record(2),record(1)),originals=2,generated=1)),
        ('pair-wrong-package',frame((record(2),record(3,2)),packages=2,generated=1)),
        ('second-generated',frame((record(2),record(3),record(3)),generated=1)),
        ('precedence-short-magic',b'BAD!'),
        ('precedence-magic-count',HEADER.pack(0,0,0,2)),
        ('precedence-count-record',frame((b'x',),packages=0)),
        ('precedence-tag-size',frame((RECORD.pack(0,0,0xffffffff),))),
        ('precedence-size-pair',frame((RECORD.pack(3,0,0xffffffff),))),
        ('precedence-pair-generated-size',frame((record(3,0,b'x'*257),))),
        ('precedence-count-generated-size',frame((record(2),record(3,1,b'x'*257)))),
        ('precedence-original-package',frame((record(),record(1,0)))),
        ('precedence-original-replaced',frame((record(2),record(3),record(2)),generated=1)),
        ('precedence-pair-final-counts',frame((record(2),),packages=2,originals=2)),
        ('precedence-final-package-counts',frame((record(),),packages=2,originals=2,generated=1)),
        ('precedence-final-original-counts',frame((record(),),originals=2,generated=1))):
        yield label,data
    for count in range(1,13): yield f'trailer-{count}', frame((record(),))+b'\0'*count

def prepare(output, extra):
    output = output.absolute()
    if (output.resolve() != output or output.exists() or output.is_symlink() or
            not output.parent.is_dir()):
        raise ValueError(f'fresh canonical output in existing owned parent required: {output}')
    # Validate every external input before creating even a staging directory.
    # The caller must keep these files and their ancestors immutable until done.
    small = list(small_cases())
    if len(small) + len(extra) > 100000:
        raise ValueError('suite exceeds adapter case bound')
    frames = []
    for path in extra:
        path = path.absolute()
        if path.resolve(strict=True) != path or not stat.S_ISREG(path.lstat().st_mode):
            raise ValueError(f'requires regular non-symlink frame: {path}')
        with path.open('rb') as stream:
            info = os.fstat(stream.fileno())
            if info.st_size > 536870912:
                raise ValueError(f'frame exceeds adapter extent bound: {path}')
        frames.append((path, info))
    oracle_digest = hashlib.sha256(Path(__file__).with_name('reference.py').read_bytes()).hexdigest()
    # Caller exclusively controls output.parent; the final absence check is not
    # a lock. Portable rename can replace an empty directory if this is violated.
    staging = Path(tempfile.mkdtemp(prefix=f'.{output.name}.incomplete-', dir=output.parent))
    try:
        manifest=[]
        # Stream cases: do not keep large payload lists or reconstruct expected fields in C.
        with (staging/'cases.bin').open('xb') as stream:
            stream.write(b'CCTEST1\n'+b'\0'*8)
            def emit(label, data, source=None):
                if len(data) > 536870912 or len(manifest) >= 100000:
                    raise ValueError("suite exceeds adapter resource bounds")
                expected=census(data)
                if len(expected)!=16: raise ValueError('oracle must provide all sixteen fields')
                stream.write(struct.pack('<Q16q',len(data),*expected))
                digest=hashlib.sha256()
                for at in range(0,len(data),262144):
                    chunk=data[at:at+262144]; stream.write(chunk); digest.update(chunk)
                manifest.append({'name':label,'bytes':len(data),'sha256':digest.hexdigest(),
                                 'expected':expected, 'source':source})
            for label,data in small: emit(label,data)
            for path, before in frames:
                with path.open('rb') as input:
                    after = os.fstat(input.fileno())
                    if (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns) != (
                            before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns, before.st_ctime_ns):
                        raise ValueError(f'frame changed after preflight: {path}')
                    if after.st_size:
                        with mmap.mmap(input.fileno(),0,access=mmap.ACCESS_READ) as data:
                            emit(path.name,data,str(path))
                    else: emit(path.name,b'',str(path))
            stream.seek(8); stream.write(struct.pack('<Q',len(manifest)))
        # Rejection transport controls, deliberately not fed to Census. A gated
        # adapter-only process must return 3 for each; they are never success cases.
        bad=staging/'invalid-transport'; bad.mkdir()
        examples={'empty':b'', 'magic':b'BADTEST\n'+struct.pack('<Q',1),
            'zero-count':b'CCTEST1\n'+struct.pack('<Q',0),
            'excess-count':b'CCTEST1\n'+struct.pack('<Q',100001),
            'missing-size':b'CCTEST1\n'+struct.pack('<Q',1),
            'excess-size':b'CCTEST1\n'+struct.pack('<QQ',1,536870913),
            'missing-tuple':b'CCTEST1\n'+struct.pack('<QQ',1,0)}
        valid=frame((record(),))
        transport=b'CCTEST1\n'+struct.pack('<QQ16q',1,len(valid),*census(valid))+valid
        examples['truncated-payload']=transport[:-1]
        examples['trailing-bytes']=transport+b'x'
        for name,data in examples.items(): (bad/name).write_bytes(data)
        # Publish no manifest until the suite and every transport control are closed.
        with (staging/'cases.json').open('x') as stream:
            stream.write(json.dumps({'cases':manifest, 'oracle_sha256':oracle_digest,
                'native_status':'not_run','full_domain_status':'not_claimed'},indent=2)+'\n')
        if output.exists() or output.is_symlink():
            raise ValueError(f'output appeared before publication: {output}')
        staging.rename(output)
    except BaseException:
        print(f'Preparation did not confirm publication; inspect incomplete staging {staging} '
              f'and final {output}; neither is an acceptance receipt.', file=sys.stderr)
        raise

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output',type=Path)
    parser.add_argument('--frame',action='append',default=[],type=Path)
    args=parser.parse_args()
    try: prepare(args.output,args.frame)
    except (OSError,ValueError) as error: parser.exit(1,f'Census cases: {error}\n')
