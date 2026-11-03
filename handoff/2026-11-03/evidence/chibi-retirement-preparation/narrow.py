from pathlib import Path
import os,hashlib,json,subprocess,time
p=Path('/private/tmp/chibi-retirement-preparation');source=p/'source-one';out=p/'narrow';out.mkdir(exist_ok=True);builder=Path('/private/tmp/chibi-imported-composed-proof/stage3/chibi');llvm='/private/tmp/chibi-bba9-proof/llvm22';env={**os.environ,'CHIBI_CORE':str(source/'packages/core'),'CHIBI_TEST_WORKERS':'1'};results=[]
assert hashlib.sha256(builder.read_bytes()).hexdigest()=='f1f6aa4c9b398753e306ac49d8a2adba59ae22f42f0b89ff5329b2d35ebf35c0'
def run(name,args):
 cmd=['sh','/Users/cwaldner/Personal/chibi/handoffs/heavy.sh','native_cli: retired '+name]+[str(x) for x in args];start=time.monotonic();r=subprocess.run(cmd,cwd=source,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True);record={'name':name,'command':cmd,'status':r.returncode,'seconds':time.monotonic()-start,'stdout':r.stdout,'stderr':r.stderr};results.append(record);(p/'narrow.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(record),flush=True)
 if r.returncode:raise SystemExit(r.returncode)
for fixture,entry in [('CommandTests','Run'),('DriverTests','Entrypoints'),('EngineTests','Run')]:
 output=out/(fixture+'-'+entry)
 run('compile '+fixture,[builder,'--compiler',source,'--core',source/'packages/core','--source',source/f'test/graph/{fixture}.chibi','--entry',entry,'--collection','graph='+str(source/'tools/graph'),'--output',output,'--llvm',llvm,'--target','aarch64-apple-darwin','--level','2','--jobs','2'])
 argv=[output]
 if fixture!='EngineTests':argv += [builder,llvm]
 run('run '+fixture,argv)
run('native-runner',['sh',source/'test/bootstrap/native-runner.sh',builder,llvm])
