from pathlib import Path
import json,os,subprocess,time,sys
root=Path.cwd();task=root/'.chibi/main-runner-reload';build=task/'bootstrap-protocol14';env=os.environ.copy();env['CCACHE_DIR']=str(task/'ccache');env['CCACHE_BASEDIR']=str(root)
source={'head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'merge_head':subprocess.check_output(['git','rev-parse','MERGE_HEAD'],text=True).strip()};records=[]
commands=[('configure',['cmake','-S','sdk','-B',str(build),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang','-DCMAKE_C_COMPILER_LAUNCHER=ccache','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DCHIBI_SDK_BUILD_COMPILER=ON','-DCHIBI_SDK_BUILD_NATIVE=OFF','-DCHIBI_SDK_BUILD_DESKTOP=OFF','-DCHIBI_SDK_COMPILER_HOST_ONLY=OFF']),('build-private-hosts',['cmake','--build',str(build),'--target','chibi-build-host','chibi-compiler-host','--parallel','8'])]
for name,command in commands:
 log=task/('bootstrap-protocol14-'+name+'.log');start=time.monotonic()
 with log.open('w') as output:r=subprocess.run(command,cwd=root,env=env,stdout=output,stderr=subprocess.STDOUT)
 record={'step':name,'returncode':r.returncode,'wall_seconds':round(time.monotonic()-start,3),'log':str(log),'command':command,'source':source};records.append(record);(task/'bootstrap-protocol14-validation.json').write_text(json.dumps(records,indent=2)+'\n');print(json.dumps(record),flush=True)
 if r.returncode:print('\n'.join(log.read_text().splitlines()[-70:]),flush=True);sys.exit(r.returncode)
rows=(build/'chibi_package_targets-Release.txt').read_text().splitlines();targets={}
for row in rows[1:]:
 kind,name,path=row.split('\t')
 if name in ['chibi-build-host','chibi-compiler-host']:
  assert kind=='EXECUTABLE' and Path(path).is_file() and os.access(path,os.X_OK);targets[name]=path
assert len(targets)==2
protocol=subprocess.check_output([targets['chibi-build-host'],'--protocol-version'],text=True).strip();assert protocol=='14'
result={'step':'verify-hosts','targets':targets,'protocol':14,'source':source};records.append(result);(task/'bootstrap-protocol14-validation.json').write_text(json.dumps(records,indent=2)+'\n');print(json.dumps(result),flush=True)
