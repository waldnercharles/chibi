from pathlib import Path
import hashlib,json,shutil,subprocess,tempfile
release=Path('/private/tmp/chibi-native-seed-14f7-release');source=Path('/private/tmp/chibi-native-seed-14f7-proof/source')
results=[]
with tempfile.TemporaryDirectory(prefix='chibi-native-seed-14f7-cache-') as scratch:
 root=Path(scratch); tools=root/'tools/bootstrap';tools.mkdir(parents=True)
 for name in ['native-seed.sh','native-seed.ps1']:shutil.copyfile(source/'tools/bootstrap'/name,tools/name)
 shutil.copyfile(release/'native-seed.tsv',tools/'native-seed.tsv')
 for platform in ['linux-x64','macos-arm64']:
  asset=release/('chibi-native-'+platform);cached=root/platform;shutil.copyfile(asset,cached)
  for parser in ['sh','pwsh']:
   if parser=='sh':command=['sh',str(tools/'native-seed.sh'),platform,str(cached)]
   else:command=['docker','run','--rm','--network','none','--user','501:20','-e','HOME=/tmp','-v',str(root)+':/fixture','chibi-seed-pwsh:local','pwsh','-NoLogo','-NoProfile','-File','/fixture/tools/bootstrap/native-seed.ps1','-Platform',platform,'-Output','/fixture/'+platform]
   result=subprocess.run(command,capture_output=True,text=True);assert result.returncode==0,(parser,platform,result.stdout,result.stderr)
   assert cached.read_bytes()==asset.read_bytes();results.append({'parser':parser,'platform':platform,'status':0,'cached_bytes_unchanged':True})
 for parser in ['sh','pwsh']:
  if parser=='sh':command=['sh',str(tools/'native-seed.sh'),'windows-x64',str(root/'windows-not-created')]
  else:command=['docker','run','--rm','--network','none','--user','501:20','-e','HOME=/tmp','-v',str(root)+':/fixture','chibi-seed-pwsh:local','pwsh','-NoLogo','-NoProfile','-File','/fixture/tools/bootstrap/native-seed.ps1','-Platform','windows-x64','-Output','/fixture/windows-not-created']
  result=subprocess.run(command,capture_output=True,text=True);assert result.returncode==3,(parser,result.stdout,result.stderr);assert not (root/'windows-not-created').exists();results.append({'parser':parser,'platform':'windows-x64','status':3,'missing_pin_rejected':True})
 assert not list(root.glob('**/.native-seed*'))
receipt={'candidate_commit':'14f7a46a047ab92acfe58398b3cdac25ab7d0312','scope':'Offline candidate manifest warm-cache parser audit only; does not execute either seed or prove Windows bootstrap runtime. No release download or publication.','checks':results,'proof_pairs_independently_compared':True,'source_snapshot_and_git_archive_blobs_verified':3400,'macos_codesign_verify_strict_status':0,'license_files':'Exact Git blobs from the selected commit.'}
(release/'AUDIT.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2))
