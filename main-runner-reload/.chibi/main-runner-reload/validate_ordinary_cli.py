from pathlib import Path
import json,os,re,subprocess,sys,time
root=Path(__file__).resolve().parents[2]
task=root/'.chibi/main-runner-reload'
native=task/'native'; driver=native/'test/tools/test_tools'
cli=Path(os.environ.get('CHIBI_VALIDATION_CLI',native/'sdk/chibi'))
host=native/'sdk/chibi-build-host'
selectors={
    'chibi-cli':['--chibi-cli',str(cli)],
    'chibi-cli-process':['--chibi-cli-process',str(cli)],
    'chibi-cli-build':['--chibi-cli-build',str(cli)],
    'chibi-cli-doctor':['--chibi-cli-doctor',str(cli)],
    'chibi-cli-export':['--chibi-cli-export',str(cli)],
    'chibi-cli-installed':['--chibi-cli-installed',str(cli)],
    'native-build':['--native-build',str(cli)],
    'program-sdk':['--program-sdk'],
    'package-ownership':['--package-ownership'],
    'build-graph-resources':['--build-graph-resources',str(host),str(task/'reference')],
    'build-sdk':['--build-sdk',str(host),str(native),''],
}
selector=sys.argv[1]; phase=sys.argv[2] if len(sys.argv)>2 else 'first'
log=task/(selector+'-ordinary-cli-'+phase+'.log')
env=os.environ.copy();env['CHIBI_GRAPH_REFLECT']=str(native/'reflect')
command=[str(driver),*selectors[selector]]
if selector in ('native-build', 'chibi-cli-process', 'chibi-cli', 'chibi-cli-installed', 'program-sdk', 'chibi-cli-build', 'chibi-cli-doctor'):
    env['CHIBI_TOOLS_BUILD_DIR']=str(native/'test/tools')
    env['CHIBI_CLI']=str(cli)
    env['CHIBI_INSTALLED_CLI']=str(cli)
    env['CHIBI_LLVM_BIN']='/opt/homebrew/opt/llvm/bin'
    command=[sys.executable,str(root/'tools/test_policy.py'),'tool',selector]
if selector == 'chibi-cli-export':
    env['CHIBI_EXECUTION_PROFILE']='gpu-check'
    command=[sys.executable,str(root/'tools/test_policy.py'),'native',str(driver),*selectors[selector]]
start=time.monotonic()
with log.open('w') as output:
    result=subprocess.run(command,cwd=root,stdout=output,stderr=subprocess.STDOUT,env=env)
record={'selector':selector,'phase':phase,'returncode':result.returncode,'wall_seconds':round(time.monotonic()-start,3),'log':str(log),'command':command}
text=log.read_text()
if 'unexecuted on this platform' in text: record['coverage']='unexecuted on this platform'
match=re.search(r'Summary: Passed: (\d+) Failed: (\d+) Total: (\d+) Suites: (\d+) Asserts: (\d+)',text)
if match:record.update(dict(zip(['passed','failed','cases','suites','assertions'],map(int,match.groups()))))
match=re.search(r'Test command wall time: ([0-9.]+)s',text)
if match:record['runner_wall_seconds']=float(match[1])
results=task/'ordinary-cli-validation.json'
records=json.loads(results.read_text()) if results.exists() else []
records.append(record);results.write_text(json.dumps(records,indent=2)+'\n')
print(json.dumps(record))
if result.returncode:
    print('\n'.join(text.splitlines()[-90:]))
sys.exit(result.returncode)
