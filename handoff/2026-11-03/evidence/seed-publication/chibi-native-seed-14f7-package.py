from pathlib import Path
import gzip, hashlib, io, json, os, shutil, subprocess, tarfile, tempfile
repo=Path('/Users/cwaldner/Personal/chibi/.worktrees/authored-strings-regressions/chibi')
proof=Path('/private/tmp/chibi-native-seed-14f7-proof')
release=Path('/private/tmp/chibi-native-seed-14f7-release')
commit='14f7a46a047ab92acfe58398b3cdac25ab7d0312'
release.mkdir()
staging=Path(tempfile.mkdtemp(prefix='chibi-native-seed-14f7-package-'))
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def git(*args):return subprocess.check_output(['git','-C',str(repo),*args])
def put(path,value):path.write_text(json.dumps(value,indent=2)+'\n')
source_meta=json.loads((proof/'source.json').read_text())
raw=git('ls-tree','-rz','--full-tree',commit)
tracked={}
for row in raw.split(b'\0'):
 if not row:continue
 head,name=row.split(b'\t',1);mode,kind,oid=head.decode().split();assert kind=='blob',(name,kind)
 tracked[name.decode()]={'mode':mode,'git_blob':oid}
# Independently verify the actual source snapshot used by the proof.
for name,entry in tracked.items():
 path=proof/'source'/name
 data=os.readlink(path).encode() if entry['mode']=='120000' else path.read_bytes()
 assert hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()==entry['git_blob'],name
 entry['sha256']=hashlib.sha256(data).hexdigest();entry['bytes']=len(data)
 # Git executable mode is part of the source tree identity.
 if entry['mode']!='120000':assert bool(path.stat().st_mode&0o111)==(entry['mode']=='100755'),name
raw_archive=git('archive','--format=tar','--prefix=chibi/',commit)
with tarfile.open(fileobj=io.BytesIO(raw_archive)) as archive:
 seen=set()
 for member in archive:
  if member.isdir():continue
  assert member.name.startswith('chibi/')
  name=member.name[len('chibi/'):];entry=tracked[name];seen.add(name)
  if member.issym():data=member.linkname.encode();assert entry['mode']=='120000'
  else:data=archive.extractfile(member).read();assert member.isfile();assert bool(member.mode&0o111)==(entry['mode']=='100755')
  assert hashlib.sha256(data).hexdigest()==entry['sha256'],name
 assert seen==set(tracked)
with (release/'chibi-source.tar.gz').open('wb') as output:
 with gzip.GzipFile(filename='',fileobj=output,mode='wb',mtime=0) as compressed:compressed.write(raw_archive)
put(release/'SOURCE_FILES.json',{'source_commit':commit,'verified_blob_count':len(tracked),'files':tracked})
for name in ['LICENSE.md','COPYING','COPYING.LESSER','COPYING.EXAMPLES']:(release/name).write_bytes(git('show',commit+':'+name))
assets={'linux-x64':proof/'linux/fixed5/chibi','macos-arm64':proof/'macos/stage2'}
for platform,path in assets.items():
 target=release/('chibi-native-'+platform);shutil.copyfile(path,target);target.chmod(0o755);assert sha(target)==sha(path)
# Preserve original proof scripts/logs and both selected consecutive binaries.
proof_stage=staging/'proof';proof_stage.mkdir()
for name in ['source.json','macos.sh','linux.sh','linux-fixed.sh','timed.py']:
 shutil.copy2(proof/name,proof_stage/name)
for platform in ['macos','linux']:
 target=proof_stage/platform;target.mkdir()
 for path in sorted((proof/platform).glob('*.log')):shutil.copy2(path,target/path.name)
for name in ['basename-mismatch.txt','basename-mismatch-audit.json']:
 shutil.copy2(proof/'linux'/name,proof_stage/'linux'/name)
for source,target in [(proof/'macos/stage3','macos/stage3'),(proof/'linux/fixed6/chibi','linux/fixed6-chibi'),(proof/'linux/stage3','linux/initial-stage3'),(proof/'linux/stage4','linux/initial-stage4')]:
 shutil.copyfile(source,proof_stage/target)
linux_builder_receipt=Path('/private/tmp/chibi-command-fixture-linux/receipt.json')
shutil.copy2(linux_builder_receipt,proof_stage/'linux-builder-receipt.json')
proof_files={str(path.relative_to(staging)):{'sha256':sha(path),'bytes':path.stat().st_size} for path in sorted(proof_stage.rglob('*')) if path.is_file()}
put(proof_stage/'FILES.json',proof_files)
# Evidence archives use deterministic ordering/metadata. Original script/log
# bytes are preserved; their original absolute build paths remain explicit.
with (release/'chibi-proof.tar.gz').open('wb') as output:
 with gzip.GzipFile(filename='',fileobj=output,mode='wb',mtime=0) as compressed:
  with tarfile.open(fileobj=compressed,mode='w') as archive:
   for path in sorted(proof_stage.rglob('*')):
    if not path.is_file():continue
    data=path.read_bytes();item=tarfile.TarInfo(str(path.relative_to(staging)));item.size=len(data);item.mode=0o755 if path.name.endswith('.sh') else 0o644;item.mtime=0
    archive.addfile(item,io.BytesIO(data))
for platform in ['linux','macos']:
 assert sha(source_meta[platform+'_builder'])==source_meta[platform+'_builder_sha256']
provenance={
 'status':'prepared local candidate; unpublished; no installed pin changes; no Windows asset',
 'scope':'Intermediate native bootstrap compilers only. This is not final Chibi product or runtime-retirement acceptance.',
 'source_commit':commit,'source_tree':git('rev-parse',commit+'^{tree}').decode().strip(),
 'compiler_tree':git('rev-parse',commit+':tools/compiler').decode().strip(),'core_tree':git('rev-parse',commit+':packages/core').decode().strip(),
 'source_archive':{'path':'chibi-source.tar.gz','sha256':sha(release/'chibi-source.tar.gz'),'archive_and_proof_snapshot_verified_against_git_blobs':len(tracked),'inventory':'SOURCE_FILES.json','method':'git archive --format=tar --prefix=chibi/ COMMIT, gzip mtime0'},
 'proof_archive':{'path':'chibi-proof.tar.gz','sha256':sha(release/'chibi-proof.tar.gz'),'inventory':'proof/FILES.json','includes':'Original scripts, source.json, logs, selected pair comparison binaries, initial Linux mismatch binaries, ELF audit and Linux builder artifact receipt.'},
 'llvm':{'version':'22.1.8','macos_directory':'/private/tmp/chibi-bba9-proof/llvm22','linux_directory':'/usr/lib/llvm-22/bin'},
 'builders':source_meta,
 'build_environment':{'CC':'/usr/bin/false','CXX':'/usr/bin/false','macos':'local macOS arm64; proof/macos.sh','linux':'Ubuntu24.04 linux/amd64 Docker','linux_image':'sha256:c2936c6e9f67a2bb3bc5869b07e36d7f6cbe87b0af413c1b3f33844ae5722128'},
 'platforms':{
  'macos-arm64':{'asset':'chibi-native-macos-arm64','sha256':sha(assets['macos-arm64']),'bytes':assets['macos-arm64'].stat().st_size,'accepted_pair':['macos/stage2','macos/stage3'],'pair_equal':True,'build_seconds':[19.09,21.93],'tests':{'binary':'macos/stage3','count':485,'failed':0,'seconds':24.64,'exit_status':0,'environment':{'CHIBI_CORE':str(proof/'source/packages/core')},'CC_CXX':'No explicit override on the separate test invocation. Build overrides do not establish test environment.','command':['/usr/bin/time','-p','env','CHIBI_CORE='+str(proof/'source/packages/core'),str(proof/'macos/stage3'),'test','--root',str(proof/'source/tools/compiler'),'--llvm','/private/tmp/chibi-bba9-proof/llvm22',str(proof/'source/tools/compiler')]},'imports':['/usr/lib/libSystem.B.dylib'],'builder_bytes_equal_selected_asset':True},
  'linux-x64':{'asset':'chibi-native-linux-x64','sha256':sha(assets['linux-x64']),'bytes':assets['linux-x64'].stat().st_size,'accepted_pair':['linux/fixed5/chibi','linux/fixed6/chibi'],'pair_equal':True,'build_seconds':[53.67,53.80],'tests':{'binary':'linux/fixed6/chibi','count':485,'failed':0,'seconds':71.73,'exit_status':0,'environment':{'CHIBI_CORE':str(proof/'source/packages/core'),'CC':'/usr/bin/false','CXX':'/usr/bin/false'},'script':'proof/linux-fixed.sh'},'imports':['libc.so.6'],'initial_stage3_stage4':{'accepted_as_fixed_point':False,'differing_bytes':58,'reason':'ELF STT_FILE names stage3.N.ll versus stage4.N.ll only; independently parsed symbol/string tables. All other bytes equal.','audit':'proof/linux/basename-mismatch-audit.json','preserved_binaries':['proof/linux/initial-stage3','proof/linux/initial-stage4']}}
 },
 'publication':{'proposed_tag':'native-seed-'+commit,'candidate_manifest':'native-seed.tsv','approved':False,'published':False,'windows':'Excluded. No approved Windows package or pin is proposed.'}
}
put(release/'PROVENANCE.json',provenance)
(release/'native-seed.tsv').write_text('# CANDIDATE ONLY: prepared locally, not published or installed.\n# platform, proposed release tag, asset, SHA-256; exact accepted pairs are in PROVENANCE.json.\n'+''.join(platform+'\t'+'native-seed-'+commit+'\tchibi-native-'+platform+'\t'+sha(assets[platform])+'\n' for platform in ['linux-x64','macos-arm64']))
print(json.dumps({'release':str(release),'source_blobs':len(tracked),'assets':{k:sha(v) for k,v in assets.items()}},indent=2))
