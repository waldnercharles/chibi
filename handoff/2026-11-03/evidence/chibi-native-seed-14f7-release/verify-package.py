#!/usr/bin/env python3
"""Offline audit of the prepared seed assets; never executes either compiler."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tarfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--repo', type=Path)
arguments = parser.parse_args()
root = Path(__file__).resolve().parent
sha = lambda data: hashlib.sha256(data).hexdigest()
provenance = json.loads((root / 'PROVENANCE.json').read_text())
source_files = json.loads((root / 'SOURCE_FILES.json').read_text())
for key in ['source_archive', 'proof_archive']:
    assert sha((root / provenance[key]['path']).read_bytes()) == provenance[key]['sha256']
assert provenance['publication']['proposed_tag'] == 'native-seed-' + provenance['source_commit']
expected_files = {}
for line in (root / 'SHA256SUMS').read_text().splitlines():
    digest, name = line.split('  ', 1)
    assert '/' not in name and name not in expected_files
    expected_files[name] = digest
assert set(expected_files) == {path.name for path in root.iterdir() if path.is_file() and path.name != 'SHA256SUMS'}
for name, digest in expected_files.items():
    assert sha((root / name).read_bytes()) == digest, name

assert provenance['source_commit'] == source_files['source_commit']
if arguments.repo:
    listing = subprocess.check_output(['git', '-C', str(arguments.repo), 'ls-tree', '-rz', '--full-tree', provenance['source_commit']])
    git_files = {}
    for entry in listing.split(b'\0'):
        if not entry:
            continue
        header, name = entry.split(b'\t', 1)
        mode, kind, oid = header.decode().split()
        assert kind == 'blob'
        git_files[name.decode()] = (mode, oid)
    assert git_files == {name: (value['mode'], value['git_blob']) for name, value in source_files['files'].items()}
    tree = subprocess.check_output(['git', '-C', str(arguments.repo), 'rev-parse', provenance['source_commit'] + '^{tree}']).decode().strip()
    assert tree == provenance['source_tree']

with tarfile.open(root / 'chibi-source.tar.gz') as archive:
    seen = set()
    for member in archive:
        if member.isdir():
            continue
        assert member.name.startswith('chibi/')
        name = member.name[len('chibi/'):]
        assert name not in seen
        seen.add(name)
        expected = source_files['files'][name]
        if member.issym():
            assert expected['mode'] == '120000'
            data = member.linkname.encode()
        else:
            assert member.isfile()
            assert bool(member.mode & 0o111) == (expected['mode'] == '100755')
            data = archive.extractfile(member).read()
        assert sha(data) == expected['sha256'] and len(data) == expected['bytes'], name
        assert hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest() == expected['git_blob'], name
    assert seen == set(source_files['files'])
    assert len(seen) == source_files['verified_blob_count'] == provenance['source_archive']['archive_and_proof_snapshot_verified_against_git_blobs']

with tarfile.open(root / 'chibi-proof.tar.gz') as archive:
    proof = {}
    for member in archive:
        assert member.isfile() and member.name.startswith('proof/') and member.name not in proof
        proof[member.name] = archive.extractfile(member).read()
    assert json.loads(proof['proof/source.json'])['source_commit'] == provenance['source_commit']
    inventory = json.loads(proof['proof/FILES.json'])
    assert set(inventory) == set(proof) - {'proof/FILES.json'}
    for name, expected in inventory.items():
        assert sha(proof[name]) == expected['sha256'] and len(proof[name]) == expected['bytes'], name

pair_receipt = json.loads(proof[provenance['proof_archive']['accepted_pair_receipt']])
assert pair_receipt['source_commit'] == provenance['source_commit']
assert set(pair_receipt['pairs']) == {'macos-arm64', 'linux-x64'}
for platform, earlier, later in [
    ('macos-arm64', 'proof/macos/stage2', 'proof/macos/stage3'),
    ('linux-x64', 'proof/linux/fixed5-chibi', 'proof/linux/fixed6-chibi')
]:
    expected = provenance['platforms'][platform]
    compared = pair_receipt['pairs'][platform]
    assert expected['archived_accepted_pair'] == [earlier, later]
    assert [compared['original_earlier'], compared['original_later']] == expected['accepted_pair']
    assert [compared['archived_earlier'], compared['archived_later']] == [earlier, later]
    assert compared['cmp_exit_status'] == 0 and compared['byte_equal']
    first, second = proof[earlier], proof[later]
    assert first == second, (platform, 'consecutive fixed-point pair differs')
    assert sha(first) == compared['earlier_sha256']
    assert sha(second) == compared['later_sha256']
    binary = (root / expected['asset']).read_bytes()
    assert sha(binary) == expected['sha256'] and len(binary) == expected['bytes']
    assert binary == first and binary == second, (platform, 'selected asset differs from pair')
    assert compared['selected_asset'] == expected['asset']
    assert compared['selected_asset_sha256'] == sha(binary)
    assert compared['selected_asset_equals_both_members']
    log = proof['proof/' + ('macos' if platform == 'macos-arm64' else 'linux') + '/compiler-tests.log'].decode()
    assert log.count('485 tests, 0 failed') == 1
    assert sum(line.startswith('PASS ') for line in log.splitlines()) == 485
    assert not any(line.startswith('FAIL ') for line in log.splitlines())

# Independently locate STT_FILE entries in each initial ELF, then require every
# differing byte to be exactly the stage3 versus stage4 filename spelling.
def file_symbols(data):
    assert data[:6] == b'\x7fELF\x02\x01'
    offset = struct.unpack_from('<Q', data, 40)[0]
    size, count = struct.unpack_from('<HH', data, 58)
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, offset + index * size) for index in range(count)]
    found = []
    for section in sections:
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        for at in range(section[4], section[4] + section[5], section[9]):
            name, info, _, _, _, _ = struct.unpack_from('<IBBHQQ', data, at)
            if info & 15 == 4:
                start = strings[4] + name
                end = data.index(0, start)
                found.append((start, end, data[start:end]))
    return found
left = proof['proof/linux/initial-stage3']
right = proof['proof/linux/initial-stage4']
assert len(left) == len(right) and left != right
differences = {index for index, pair in enumerate(zip(left, right)) if pair[0] != pair[1]}
allowed = set()
first, second = file_symbols(left), file_symbols(right)
assert len(first) == len(second)
changed = 0
for a, b in zip(first, second):
    assert a[:2] == b[:2]
    if a[2] != b[2]:
        assert a[2].replace(b'stage3.', b'stage4.', 1) == b[2]
        allowed.update(range(a[0], a[1]))
        changed += 1
assert len(differences) == changed == 58 and differences <= allowed

rows = [line.split('\t') for line in (root / 'native-seed.tsv').read_text().splitlines() if line and not line.startswith('#')]
assert len(rows) == 2 and {row[0] for row in rows} == {'linux-x64', 'macos-arm64'}
for platform, tag, asset, digest in rows:
    assert tag == provenance['publication']['proposed_tag']
    assert asset == provenance['platforms'][platform]['asset']
    assert digest == provenance['platforms'][platform]['sha256']
assert not provenance['publication']['approved'] and not provenance['publication']['published']
print(f"PASS {len(expected_files)} assets, {len(seen)} source blobs, two complete consecutive fixed-point pairs, 485 tests per platform, and all 58 ELF basename differences")
