from pathlib import Path
import gzip,hashlib,io,json,shutil,subprocess,tarfile
release=Path('/private/tmp/chibi-native-seed-14f7-release')
original=Path('/private/tmp/chibi-native-seed-14f7-proof')
backup=Path('/private/tmp/chibi-native-seed-14f7-before-pair-proof')
backup.mkdir()
for name in ['chibi-proof.tar.gz','PROVENANCE.json','AUDIT.json','BUILDING.md','verify-package.py','SHA256SUMS']:
 shutil.copyfile(release/name,backup/name)
sha=lambda data:hashlib.sha256(data).hexdigest()
contents={};modes={}
with tarfile.open(release/'chibi-proof.tar.gz') as archive:
 for member in archive:
  assert member.isfile() and member.name not in contents
  contents[member.name]=archive.extractfile(member).read();modes[member.name]=member.mode
provenance=json.loads((release/'PROVENANCE.json').read_text())
receipt={'source_commit':provenance['source_commit'],'method':'cmp on the original consecutive outputs, followed by exact byte comparison of both archived members and the selected asset. No compiler execution or rebuild.','pairs':{}}
for platform,early,late,arch_early,arch_late in [
 ('macos-arm64','macos/stage2','macos/stage3','proof/macos/stage2','proof/macos/stage3'),
 ('linux-x64','linux/fixed5/chibi','linux/fixed6/chibi','proof/linux/fixed5-chibi','proof/linux/fixed6-chibi')]:
 first=(original/early).read_bytes();second=(original/late).read_bytes()
 compared=subprocess.run(['cmp',str(original/early),str(original/late)],capture_output=True)
 assert compared.returncode==0 and first==second,(platform,compared.stderr)
 asset=provenance['platforms'][platform]['asset'];selected=(release/asset).read_bytes()
 assert selected==first==second
 assert sha(selected)==provenance['platforms'][platform]['sha256']
 assert contents[arch_late]==second
 contents[arch_early]=first;modes[arch_early]=modes[arch_late]
 receipt['pairs'][platform]={'original_earlier':early,'original_later':late,'archived_earlier':arch_early,'archived_later':arch_late,'earlier_sha256':sha(first),'later_sha256':sha(second),'cmp_command':['cmp',str(original/early),str(original/late)],'cmp_exit_status':compared.returncode,'byte_equal':True,'selected_asset':asset,'selected_asset_sha256':sha(selected),'selected_asset_equals_both_members':True}
 provenance['platforms'][platform]['archived_accepted_pair']=[arch_early,arch_late]
contents['proof/ACCEPTED_PAIRS.json']=(json.dumps(receipt,indent=2)+'\n').encode();modes['proof/ACCEPTED_PAIRS.json']=0o644
inventory={name:{'sha256':sha(data),'bytes':len(data)} for name,data in sorted(contents.items()) if name!='proof/FILES.json'}
contents['proof/FILES.json']=(json.dumps(inventory,indent=2)+'\n').encode()
with (release/'chibi-proof.tar.gz').open('wb') as output:
 with gzip.GzipFile(filename='',fileobj=output,mode='wb',mtime=0) as compressed:
  with tarfile.open(fileobj=compressed,mode='w') as archive:
   for name,data in sorted(contents.items()):
    member=tarfile.TarInfo(name);member.mode=modes[name];member.size=len(data);member.mtime=0;archive.addfile(member,io.BytesIO(data))
provenance['proof_archive']['sha256']=sha((release/'chibi-proof.tar.gz').read_bytes())
provenance['proof_archive']['accepted_pair_receipt']='proof/ACCEPTED_PAIRS.json'
provenance['proof_archive']['includes']='Original scripts, source.json, logs, both members of each accepted consecutive pair with cmp receipt, initial unequal Linux stage3/stage4 binaries, ELF audit and Linux builder artifact receipt.'
(release/'PROVENANCE.json').write_text(json.dumps(provenance,indent=2)+'\n')
audit=json.loads((release/'AUDIT.json').read_text());audit['archived_consecutive_pair_receipt']='proof/ACCEPTED_PAIRS.json';audit['archived_consecutive_pairs_verified']={name:[entry['archived_earlier'],entry['archived_later']] for name,entry in receipt['pairs'].items()};(release/'AUDIT.json').write_text(json.dumps(audit,indent=2)+'\n')
p=release/'BUILDING.md';text=p.read_text();anchor='The recorded macOS pair is `stage2` and `stage3`.';assert anchor in text;text=text.replace(anchor,'The proof archive includes both members of each accepted consecutive pair.\n`proof/ACCEPTED_PAIRS.json` records their original paths, archived names, hashes,\nactual `cmp` status and equality with the selected executable asset.\n\n'+anchor);p.write_text(text)
print('Archived both earlier accepted binaries and comparison receipt; initial unequal Linux binaries unchanged.')
