from pathlib import Path
import json,os,subprocess,time,sys
root=Path.cwd();task=root/'.chibi/main-runner-reload';expected=sys.argv[1] if len(sys.argv)>1 else 'fee955dc26861bbe836ca28d15685838b3a56322';short=expected[:8]
assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==expected
assert not subprocess.check_output(['git','status','--porcelain'],text=True).strip()
env=os.environ.copy();env['CCACHE_DIR']=str(task/'ccache');env['CCACHE_BASEDIR']=str(root)
record_path=task/'public-sdk-validation.json';records=json.loads(record_path.read_text()) if record_path.exists() else []
steps=[('configure',['cmake','-S','sdk','-B',str(task/'public-sdk-build')]),('build-all',['cmake','--build',str(task/'public-sdk-build'),'--parallel','8']),('package',[sys.executable,'tools/package_sdk.py','--build',str(task/'public-sdk-build'),'--output',str(task/'public-sdk'),'--kind','sdk'])]
for name,command in steps:
 log=task/('public-sdk-'+name+'-'+short+'.log');start=time.monotonic()
 with log.open('w') as output:result=subprocess.run(command,cwd=root,env=env,stdout=output,stderr=subprocess.STDOUT)
 record={'phase':'clean-'+short,'head':expected,'step':name,'command':command,'returncode':result.returncode,'wall_seconds':round(time.monotonic()-start,3),'log':str(log)};records.append(record);record_path.write_text(json.dumps(records,indent=2)+'\n');print(json.dumps(record),flush=True)
 if result.returncode:
  print('\n'.join(log.read_text().splitlines()[-80:]),flush=True);sys.exit(result.returncode)
assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==expected
assert not subprocess.check_output(['git','status','--porcelain'],text=True).strip()
