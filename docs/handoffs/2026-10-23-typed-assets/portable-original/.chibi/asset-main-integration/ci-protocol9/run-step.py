import sys,os,json,hashlib,subprocess,time,datetime
from pathlib import Path
out=Path(__file__).resolve().parent
repo=out.parents[2]
name,tree=sys.argv[1:3];argv=sys.argv[3:]
assert argv
log=out/(name+'.log');receipt=out/(name+'.json')
assert not log.exists() and not receipt.exists(),'existing evidence must be preserved'
execution=json.loads((out/'execution.json').read_text())
def git(*a):return subprocess.check_output(['git','-C',str(repo),*a],text=True).strip()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def guard():
 assert git('write-tree')==tree,'index tree changed'
 assert not git('diff','--name-only'),'unstaged source changed'
 assert git('rev-parse','HEAD')==execution['head'],'HEAD changed'
 assert git('rev-parse','MERGE_HEAD')==execution['merge_head'],'merge parent changed'
guard();env=os.environ.copy();env['PATH']='/opt/homebrew/opt/python@3.13/libexec/bin:'+env['PATH'];env['MallocNanoZone']='0'
r={'name':name,'tree':tree,'head':execution['head'],'merge_head':execution['merge_head'],'argv':argv,'status':'running','started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'log':str(log),'python':subprocess.check_output(['/opt/homebrew/opt/python@3.13/libexec/bin/python3','--version'],text=True).strip(),'jobs':'4 only via Make argument; no global -j'}
receipt.write_text(json.dumps(r,indent=2)+'\n');start=time.monotonic()
with log.open('wb') as stream:
 child=subprocess.Popen(argv,cwd=repo,env=env,stdout=stream,stderr=subprocess.STDOUT);r['pid']=child.pid;receipt.write_text(json.dumps(r,indent=2)+'\n');code=child.wait()
r.update(exit_code=code,elapsed_seconds=round(time.monotonic()-start,3),status='passed' if code==0 else 'failed',completed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),log_sha256=sha(log))
try:guard();r['source_currency']='unchanged'
except Exception as e:r['source_currency']=str(e);r['status']='invalid_source_changed'
receipt.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2),flush=True)
sys.exit(0 if r['status']=='passed' else 1)
