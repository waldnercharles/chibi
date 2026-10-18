# Scratch-only admission and evidence classifier checks. No source build or target runtime.
import ast,copy,hashlib,json,os,pathlib,re,subprocess,tempfile,yaml
here=pathlib.Path(__file__).resolve().parent
helper=here/'proposal/reader-state-validation.sh'
workflow=yaml.safe_load((here/'proposal/toolchain-m5-unit-analysis.yml').read_text())
results=[]
selectors={'language-package-snapshot-chibi':16,'compiler-artifact':15,'language-signature-reader-chibi':4,'language-snapshot-reader-chibi':3}
SH='70ff5bf54a1125a110be6a26a7e226a4b5d542ba'; ST='1ca14bfda693998e9769b552b84b91bbfc4cc6cb'; CH='1'*40; CT='2'*40; BASE='bee5b54e159cce44ee52ca9439be587d9a95f5d2'
with tempfile.TemporaryDirectory(prefix='transport-controls-',dir=here) as tmp:
 t=pathlib.Path(tmp); bin=t/'bin'; bin.mkdir(); (t/'ws').mkdir(); (t/'run').mkdir()
 git=bin/'git'; git.write_text('''#!/bin/bash
if [[ "$1" == --version ]]; then [[ "${FAIL_GIT:-}" != all ]]; exit $?; fi
[[ "$1" == -C ]] || exit 90
where=${2##*/}; shift 2
[[ "${FAIL_GIT:-}" != "$where" && "${FAIL_GIT:-}" != all ]] || exit 1
case "$where:$1:$2" in
 validation:rev-parse:HEAD) echo "${MOCK_CONTROL_HEAD}";;
 validation:rev-parse:HEAD^\\{tree\\}) echo "${MOCK_CONTROL_TREE}";;
 chibi:rev-parse:HEAD) echo "${MOCK_SOURCE_HEAD}";;
 chibi:rev-parse:HEAD^\\{tree\\}) echo "${MOCK_SOURCE_TREE}";;
 validation:status:--porcelain) printf '%s' "${DIRTY_CONTROL:-}";;
 chibi:status:--porcelain) printf '%s' "${DIRTY_SOURCE:-}";;
 *) exit 90;;
esac
'''); git.chmod(0o755)
 # No executable target exists. These stubs must never be reached.
 for tool in ['python3','cmake','sudo','pip']:
  p=bin/tool; p.write_text('#!/bin/bash\necho FORBIDDEN_TARGET_EXECUTION >&2\nexit 99\n'); p.chmod(0o755)
 env=dict(os.environ,PATH=str(bin)+':'+os.environ['PATH'],RUNNER_TEMP=str(t/'run'),GITHUB_WORKSPACE=str(t/'ws'),READER_STATE_REVIEWED='true',GITHUB_RUN_ATTEMPT='1',CANDIDATE_SHA=SH,CANDIDATE_TREE=ST,CONTROL_SHA=CH,GITHUB_REPOSITORY='waldnercharles/chibi-studio',PR_BASE_SHA=BASE,EXPECTED_BASE_SHA=BASE,PR_BASE_BRANCH='main',PR_HEAD_BRANCH='codex/host-m5-unit-validation-20261018',SOURCE_BRANCH='refs/heads/codex/m5-snapshot-reader-validation-20261018',MOCK_CONTROL_HEAD=CH,MOCK_CONTROL_TREE=CT,MOCK_SOURCE_HEAD=SH,MOCK_SOURCE_TREE=ST)
 work=t/'run/chibi-m5-reader-state-private'; public=t/'run/chibi-m5-reader-state-status'
 def run(name,phase='prepare',mut=None,want=0):
  e=dict(env); e.update(mut or {})
  p=subprocess.run(['bash',str(helper),phase],env=e,text=True,capture_output=True,timeout=5)
  assert (p.returncode==0)==(want==0),(name,p.returncode,p.stdout,p.stderr)
  assert re.fullmatch(r'phase=[a-z0-9-]+ exit=[0-9]+(?: elapsed_seconds=[0-9]+)?\n',p.stdout),(name,p.stdout,p.stderr)
  assert not p.stderr,(name,p.stderr)
  assert all('FORBIDDEN_TARGET_EXECUTION' not in f.read_text(errors='replace') for f in work.glob('*.log'))
  results.append(name)
  return p
 run('missing runner temp fails privately',mut={'RUNNER_TEMP':''},want=1)
 run('missing workspace fails privately',mut={'GITHUB_WORKSPACE':''},want=1)
 run('valid prepare')
 for name,key,value in [
  ('review false','READER_STATE_REVIEWED','false'),('rerun denied','GITHUB_RUN_ATTEMPT','2'),('bad source sha','CANDIDATE_SHA','bad'),('bad source tree','CANDIDATE_TREE','bad'),('bad control sha','CONTROL_SHA','bad'),('wrong repository','GITHUB_REPOSITORY','other/repo'),('base moved','PR_BASE_SHA','3'*40),('bad base pin','EXPECTED_BASE_SHA','bad'),('wrong base branch','PR_BASE_BRANCH','dev'),('wrong control branch','PR_HEAD_BRANCH','dev'),('wrong source branch','SOURCE_BRANCH','refs/heads/dev'),('control head moved','MOCK_CONTROL_HEAD','3'*40),('source head moved','MOCK_SOURCE_HEAD','3'*40),('source tree changed','MOCK_SOURCE_TREE','3'*40),('bad control tree','MOCK_CONTROL_TREE','bad'),('dirty control','DIRTY_CONTROL',' M dirty'),('dirty source','DIRTY_SOURCE','?? dirty'),('git fails control','FAIL_GIT','validation'),('git fails source','FAIL_GIT','chibi')]:
  run(name,mut={key:value},want=1)
 for key in ['LD_PRELOAD','LD_LIBRARY_PATH','LD_AUDIT','CFLAGS','CXXFLAGS','CPPFLAGS','LDFLAGS','LIBRARY_PATH','CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','OBJC_INCLUDE_PATH','GCC_EXEC_PREFIX','COMPILER_PATH','CMAKE_TOOLCHAIN_FILE','CMAKE_PREFIX_PATH']:
  # Loader warnings occur before Bash can redirect; test the admission function inside the shell.
  if key.startswith('LD_'): continue
  run('reject inherited '+key,mut={key:'PRIVATE_OVERRIDE_SENTINEL'},want=1)
 run('valid prepare restored')
 for key in ['LD_PRELOAD','LD_LIBRARY_PATH','LD_AUDIT']:
  e=dict(env)
  cmd='export '+key+'=PRIVATE_OVERRIDE_SENTINEL; source '+str(helper)+' prepare'
  result=subprocess.run(['bash','-c',cmd],env=e,text=True,capture_output=True,timeout=5)
  assert result.returncode!=0 and 'PRIVATE_OVERRIDE_SENTINEL' not in result.stdout and not result.stderr
  results.append('reject inherited '+key)
 run('unknown phase',phase='untrusted-string',want=2)
 run('unallowlisted selector',phase='execute',mut={'SELECTOR':'evil'},want=2)
 first=next(iter(selectors))
 run('audit refuses missing build evidence',phase='audit',want=1)
 run('execute refuses missing audit evidence',phase='execute',mut={'SELECTOR':first},want=1)
 run('execute refuses moved head',phase='execute',mut={'SELECTOR':first,'MOCK_SOURCE_HEAD':'3'*40},want=1)
 (work/('execution-started-'+first)).mkdir()
 (work/'audit.exit').write_text('0\n')
 run('execute refuses repeat',phase='execute',mut={'SELECTOR':first},want=1)
 (work/('execution-started-'+first)).rmdir()
 run('execute refuses missing binary identity',phase='execute',mut={'SELECTOR':first},want=1)
 phases=['prepare','dependencies','configure','build','audit']+['execute-'+s for s in selectors]
 sentinel='PRIVATE_SOURCE_SENTINEL_DO_NOT_EXPORT'
 report='Test command wall time: 0.123s; ceiling 60s; includes in-test fixture setup; external configuration/build excluded.\n'
 def seed():
  for phase in phases:
   (work/(phase+'.exit')).write_text('0\n'); (work/(phase+'.seconds')).write_text('1\n')
  for s,n in selectors.items():
   (work/('execute-'+s+'.log')).write_text(sentinel+'\n'+f'Summary: Passed: {n} Failed: 0 Total: {n} Suites: 1 Asserts: 11\n'+report)
   (work/('identity-'+s+'.verified')).write_text(SH+'\n')
  (work/'control-tree-sha').write_text(CT+'\n')
  dummy=work/'not-a-binary'; dummy.write_text('unexecuted sample')
  (work/'binary.sha256').write_text(hashlib.sha256(dummy.read_bytes()).hexdigest()+'  '+str(dummy)+'\n')
 def collect(name):
  run(name,phase='collect'); data=json.loads((public/'status.json').read_text()); assert sentinel not in json.dumps(data)
  assert set(data)=={'schema','identity','phases','results','raw_logs','test_ceiling_seconds'}
  assert set(data['identity'])=={'source_sha','source_tree','control_sha','control_tree'}
  assert all(set(x)=={'selector','outcome','execution_identity_verified','expected_pico_summary_verified','production_supervisor_60s_report_verified'} for x in data['results'])
  return data
 seed(); data=collect('valid classifier all four routes'); assert all(x['outcome']=='passed' for x in data['results']); run('valid verdict',phase='verdict')
 for selected in selectors:
  seed(); (work/('execute-'+selected+'.log')).write_text(report)
  d=collect('missing summary rejects '+selected)
  assert next(x for x in d['results'] if x['selector']==selected)['outcome']=='unexpected_test_result'
  run('missing summary verdict '+selected,phase='verdict',want=1)
 seed()
 goodlog=(work/('execute-'+first+'.log')).read_text()
 for name,log,rc,outcome in [
  ('missing summary',report,0,'unexpected_test_result'),('duplicate summary',goodlog+goodlog,0,'unexpected_test_result'),('wrong case count',goodlog.replace('Passed: 16','Passed: 17'),0,'unexpected_test_result'),('zero asserts',goodlog.replace('Asserts: 11','Asserts: 0'),0,'unexpected_test_result'),('missing supervisor',goodlog.replace(report,''),0,'unexpected_test_result'),('duplicate supervisor',goodlog+report,0,'unexpected_test_result'),('wrong ceiling',goodlog.replace('ceiling 60s','ceiling 61s'),0,'unexpected_test_result'),('reported overtime',goodlog.replace('0.123s','60.001s'),0,'unexpected_test_result'),('failure marker',goodlog+'FAIL: private diagnostic\n',0,'unexpected_test_result'),('ASAN marker',goodlog+'AddressSanitizer failure\n',0,'unexpected_test_result'),('UBSAN marker',goodlog+'runtime error: invalid\n',0,'unexpected_test_result'),('timeout',goodlog,124,'supervisor_timeout'),('test failure',goodlog,1,'unexpected_test_failure'),('runtime failure',goodlog,137,'runtime_or_supervisor_failure')]:
  seed(); (work/('execute-'+first+'.log')).write_text(log); (work/('execute-'+first+'.exit')).write_text(str(rc)+'\n'); d=collect(name); assert d['results'][0]['outcome']==outcome,(name,d); run(name+' blocks verdict',phase='verdict',want=1)
 seed(); (work/('identity-'+first+'.verified')).unlink(); d=collect('missing execution identity'); assert d['results'][0]['outcome']=='identity_unverified'; run('missing identity blocks verdict',phase='verdict',want=1)
 seed(); (work/('execute-'+first+'.exit')).unlink(); d=collect('missing execution result'); assert d['results'][0]['outcome']=='not_started'; run('missing result blocks verdict',phase='verdict',want=1)
 for name,change in [
  ('missing phase',lambda d:d['phases'].pop()),('duplicate selector',lambda d:d['results'].append(d['results'][0])),('changed source identity',lambda d:d['identity'].__setitem__('source_sha','4'*40)),('failed build',lambda d:d['phases'][3].__setitem__('exit',1)),('failed audit',lambda d:d['phases'][4].__setitem__('exit',1))]:
  seed(); d=collect(name+' seed'); change(d); (public/'status.json').write_text(json.dumps(d)); run(name+' rejected',phase='verdict',want=1)
 seed(); (work/'audit.exit').write_text('1\n'); run('execute refuses failed audit',phase='execute',mut={'SELECTOR':first},want=1)
 seed(); (work/'build.exit').write_text('1\n'); run('audit refuses failed build',phase='audit',want=1)
 seed(); collect('binary tamper seed'); (work/'not-a-binary').write_text('changed'); run('binary tamper rejected',phase='verdict',want=1)
 seed(); collect('collect failure seed'); (work/'collect.exit').write_text('1\n'); run('failed collection rejected',phase='verdict',want=1)
 # Test exact trusted YAML scripts without any actual checkout.
 steps=workflow['jobs']['reader-state']['steps']
 def shell_guard(name,script,mut=None,want=0):
  e=dict(env,RUNNER_DEBUG_ENABLED='');e.update(mut or {})
  p=subprocess.run(['bash','-e','-o','pipefail','-c',script],env=e,text=True,capture_output=True,timeout=5)
  assert (p.returncode==0)==(want==0),(name,p.returncode,p.stdout,p.stderr)
  assert not p.stderr; assert re.fullmatch('phase=[a-z-]+ exit=[01]\n',p.stdout),(name,p.stdout)
  results.append(name)
 shell_guard('trusted admission valid',steps[0]['run'])
 for name,key,value in [('trusted review false','READER_STATE_REVIEWED','false'),('trusted rerun denied','GITHUB_RUN_ATTEMPT','2'),('trusted debug','RUNNER_DEBUG_ENABLED','1'),('trusted invalid head','CANDIDATE_SHA','bad'),('trusted invalid tree','CANDIDATE_TREE','bad'),('trusted missing git','FAIL_GIT','all'),('trusted wrong repo','GITHUB_REPOSITORY','foreign/repo'),('trusted wrong branch','SOURCE_BRANCH','refs/heads/main'),('trusted base drift','PR_BASE_SHA','9'*40),('trusted bad base','EXPECTED_BASE_SHA','bad'),('trusted wrong control branch','PR_HEAD_BRANCH','main'),('trusted wrong base branch','PR_BASE_BRANCH','develop'),('trusted invalid control','CONTROL_SHA','bad')]:
  shell_guard(name,steps[0]['run'],{key:value},1)
 (t/'ws/chibi').mkdir(); shell_guard('trusted nonfresh source',steps[0]['run'],want=1)
 shell_guard('trusted source identity valid',steps[3]['run'])
 for name,key,value in [('trusted source moved','MOCK_SOURCE_HEAD','4'*40),('trusted tree moved','MOCK_SOURCE_TREE','4'*40),('trusted dirty source','DIRTY_SOURCE',' M private'),('trusted source git failure','FAIL_GIT','chibi')]:
  shell_guard(name,steps[3]['run'],{key:value},1)
 assert not any('PRIVATE_SOURCE_SENTINEL' in f.read_text(errors='replace') for f in public.glob('*'))
print(json.dumps({'checks':len(results),'passed':results,'target_build_or_runtime_executed':False},indent=2))
(here/'control-check-results.json').write_text(json.dumps({'checks':len(results),'passed':results,'target_build_or_runtime_executed':False},indent=2)+'\n')
