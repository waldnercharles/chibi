import pathlib,subprocess,os,time,hashlib,json
w=pathlib.Path(__file__).parent;s=w/'source';llvm='/private/tmp/chibi-bba9-proof/llvm22'
env=os.environ.copy();env.update(CC='/usr/bin/false',CXX='/usr/bin/false',CHIBI_CORE=str(s/'packages/core'),CHIBI_TEST_WORKERS='1')
r=json.loads((w/'source.json').read_text());r['steps']=[];r['environment']={k:env[k] for k in ['CC','CXX','CHIBI_CORE','CHIBI_TEST_WORKERS']};r['llvm']=llvm;r['status']='running'
def save(): (w/'receipt.json').write_text(json.dumps(r,indent=2)+'\n')
def run(name,args):
 t=time.monotonic()
 with (w/(name+'.log')).open('w') as log:p=subprocess.run(args,cwd=s,env=env,stdout=log,stderr=subprocess.STDOUT)
 step={'name':name,'command':args,'exit':p.returncode,'seconds':round(time.monotonic()-t,2),'log_sha256':hashlib.sha256((w/(name+'.log')).read_bytes()).hexdigest()};r['steps'].append(step);save();print(json.dumps(step),flush=True)
 if p.returncode: raise RuntimeError(name+' exit '+str(p.returncode))
try:
 candidate=w/'candidate/chibi';candidate.parent.mkdir()
 run('candidate',[r['builder'],'--compiler',str(s/'tools/compiler'),'--core',str(s/'packages/core'),'--output',str(candidate),'--llvm',llvm,'--target','aarch64-apple-darwin','--jobs','4'])
 r['candidate_sha256']=hashlib.sha256(candidate.read_bytes()).hexdigest()
 run('compiler-tests',[str(candidate),'test','--llvm',llvm,str(s/'tools/compiler')])
 r['compiler_test_summary']=[line for line in (w/'compiler-tests.log').read_text().splitlines() if 'tests, ' in line]
 for name,path in [('task-owners','test/native_aot/task_owners/run.sh'),('map-search','test/native_aot/remove_item/run.sh')]:
  run(name,['sh',str(s/path),str(candidate),llvm])
 r['status']='pass'
except BaseException as error:
 r['status']='failed';r['failure']=str(error);raise
finally:
 files=json.loads((w/'source-files.json').read_text())
 for item in files:
  p=s/item['path'];data=p.readlink().as_posix().encode() if item['mode']=='120000' else p.read_bytes()
  assert hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()==item['blob'],item['path']
  if item['mode']!='120000': assert bool(p.stat().st_mode&0o111)==(item['mode']=='100755'),item['path']
 r['archived_source_reverified']=len(files);save()
