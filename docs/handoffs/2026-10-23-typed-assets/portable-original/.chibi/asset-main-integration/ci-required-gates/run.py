import sys, os, json, hashlib, subprocess, time, datetime
from pathlib import Path
out=Path(__file__).resolve().parent
plan=json.loads((out/'plan.json').read_text())
repo=Path(plan['repository'])
def git(*a):return subprocess.check_output(['git','-C',str(repo),*a],text=True).strip()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def guard():
    assert git('write-tree')==plan['tree'], 'candidate index changed'
    assert git('diff','--name-only')=='', 'unstaged source changed'
    assert git('rev-parse','HEAD')==plan['head'], 'HEAD changed'
    assert git('rev-parse','MERGE_HEAD')==plan['merge_head'], 'merge parent changed'
    for p,h in plan['source_files_sha256'].items():assert sha(repo/p)==h, p+' changed'
    assert sha(Path(plan['build_host']['path']))==plan['build_host']['sha256'], 'seed build host changed'
group=sys.argv[1]
assert group in plan['groups']
assert (out/'lane-release.json').is_file(), 'coordinator execution release absent'
log=out/(group+'.log');receipt=out/(group+'.json')
assert not log.exists() and not receipt.exists(), 'original check evidence must be preserved'
guard()
argv=['make',group,'JOBS=4','BUILD_HOST='+plan['build_host']['path']]
record={'group':group,'argv':argv,'source_tree':plan['tree'],'head':plan['head'],'merge_head':plan['merge_head'],'status':'running','started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'log':str(log),'environment':plan['check_environment']}
receipt.write_text(json.dumps(record,indent=2)+'\n')
env=os.environ.copy();env.update(plan['check_environment'])
start=time.monotonic()
with log.open('wb') as stream:
    child=subprocess.Popen(argv,cwd=repo,env=env,stdout=stream,stderr=subprocess.STDOUT)
    record['pid']=child.pid;receipt.write_text(json.dumps(record,indent=2)+'\n')
    code=child.wait()
record.update(exit_code=code,elapsed_seconds=round(time.monotonic()-start,3),completed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),log_sha256=sha(log),status='passed' if code==0 else 'failed')
try:
    guard();record['source_currency']='unchanged'
except Exception as e:
    record['source_currency']=str(e);record['status']='invalid_source_changed'
receipt.write_text(json.dumps(record,indent=2)+'\n')
print(json.dumps(record,indent=2),flush=True)
sys.exit(0 if record['status']=='passed' else 1)
