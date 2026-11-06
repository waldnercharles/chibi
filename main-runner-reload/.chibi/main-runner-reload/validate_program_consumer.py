from pathlib import Path
import json,os,subprocess,time,sys,shlex
root=Path.cwd();task=root/'.chibi/main-runner-reload';build=task/'program-installed-consumer';prefix=task/'public-sdk';phase=sys.argv[1] if len(sys.argv)>1 else 'verified';path=task/'public-sdk-consumer-validation.json';records=json.loads(path.read_text()) if path.exists() else []
sdk_source=json.loads((prefix/'share/chibi/package.json').read_text())['source'];short=sdk_source['revision'][:8]
env=os.environ.copy();env['CHIBI_TOOLS_BUILD_DIR']=str(build)
commands=[('configure',['cmake','-S','test/tools','-B',str(build),'-G','Ninja','-DCHIBI_TEST_PROGRAM_ONLY=ON','-DCMAKE_PREFIX_PATH='+str(prefix),'-DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']),('build',['cmake','--build',str(build),'--parallel','8']),('program-sdk',[sys.executable,'tools/test_policy.py','tool','program-sdk'])]
for name,command in commands:
 log=task/('installed-program-consumer-'+name+'-'+short+'-'+phase+'.log');start=time.monotonic()
 with log.open('w') as output:r=subprocess.run(command,cwd=root,env=env,stdout=output,stderr=subprocess.STDOUT)
 rec={'phase':phase,'sdk_source':sdk_source,'step':name,'returncode':r.returncode,'wall_seconds':round(time.monotonic()-start,3),'log':str(log),'command':command};records.append(rec);(task/'public-sdk-consumer-validation.json').write_text(json.dumps(records,indent=2)+'\n');print(json.dumps(rec),flush=True)
 if r.returncode:print(log.read_text(),flush=True);sys.exit(r.returncode)
commands=json.loads((build/'compile_commands.json').read_text());includes=[]
for entry in commands:
 args=entry.get('arguments') or shlex.split(entry['command'])

 for i,a in enumerate(args):
  if a.startswith('-I'):includes.append(a[2:])
  elif a in ('-isystem','-iquote'):includes.append(args[i+1])
assert len(commands)==2
assert all(Path(value).is_relative_to(prefix/'include') or Path(value).is_relative_to(build/'testing/include') for value in includes)
records.append({'phase':phase,'step':'exported-header-isolation','sources':2,'include_flags':sorted(set(includes)),'passed':True})
(task/'public-sdk-consumer-validation.json').write_text(json.dumps(records,indent=2)+'\n')
