import pathlib,subprocess,os,time,hashlib,json
w=pathlib.Path(__file__).parent;s=w/'source'; llvm='/private/tmp/chibi-bba9-proof/llvm22'
env=os.environ.copy();env.update(CC='/usr/bin/false',CXX='/usr/bin/false',CHIBI_CORE=str(s/'packages/core'),CHIBI_TEST_WORKERS='1')
receipt=json.loads((w/'source.json').read_text());receipt['environment']={k:env[k] for k in ['CC','CXX','CHIBI_CORE','CHIBI_TEST_WORKERS']};receipt['llvm']=llvm;receipt['steps']=[]
def run(name,args):
 start=time.monotonic()
 with (w/(name+'.log')).open('w') as f:r=subprocess.run(args,cwd=s,env=env,stdout=f,stderr=subprocess.STDOUT)
 item={'name':name,'command':args,'exit':r.returncode,'seconds':round(time.monotonic()-start,2),'log_sha256':hashlib.sha256((w/(name+'.log')).read_bytes()).hexdigest()};receipt['steps'].append(item);(w/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(item),flush=True)
 if r.returncode:raise SystemExit(r.returncode)
builder=receipt['builder']
for stage in ['stage2','stage3']:
 output=w/stage/'chibi';output.parent.mkdir(exist_ok=True)
 run(stage,[builder,'--compiler',str(s/'tools/compiler'),'--core',str(s/'packages/core'),'--output',str(output),'--llvm',llvm,'--target','aarch64-apple-darwin','--jobs','4'])
 receipt[stage+'_sha256']=hashlib.sha256(output.read_bytes()).hexdigest();builder=str(output)
receipt['fixed_point']=(w/'stage2/chibi').read_bytes()==(w/'stage3/chibi').read_bytes();(w/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
if not receipt['fixed_point']:raise SystemExit('fixed point differs')
run('compiler-tests',[builder,'test','--llvm',llvm,str(s/'tools/compiler')])

receipt['compiler_test_summary']=[line for line in (w/'compiler-tests.log').read_text().splitlines() if 'tests, ' in line]
for name,relative in [('task-owners','test/native_aot/task_owners/run.sh'),('managed-pointers','test/native_aot/managed_pointers/run.sh'),('maps','test/native_aot/maps/run.sh')]:
 run(name,['sh',str(s/relative),builder,llvm])
for level in ['0','2']:
 run('generations-O'+level,['sh',str(s/'test/native_aot/generations/run.sh'),builder,llvm,level])
for item in json.loads((w/'source-files.json').read_text()):
 p=s/item['path'];data=p.readlink().as_posix().encode() if item['mode']=='120000' else p.read_bytes();assert hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()==item['blob'],item['path']
 if item['mode']!='120000':assert bool(p.stat().st_mode & 0o111)==(item['mode']=='100755'),item['path']
receipt['archived_source_reverified']=len(json.loads((w/'source-files.json').read_text()));receipt['status']='pass';(w/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps({'status':'pass','fixed_point':receipt['fixed_point'],'compiler_test_summary':receipt['compiler_test_summary']}),flush=True)
