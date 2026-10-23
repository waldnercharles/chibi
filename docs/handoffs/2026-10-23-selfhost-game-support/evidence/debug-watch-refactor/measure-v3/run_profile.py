from pathlib import Path
import subprocess,json,hashlib,time,datetime,os,sys,signal
root=Path.cwd();d=root/'.chibi/game-support/combined-d0/debug-watch-refactor/measure-v3'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
counter=root/'.chibi/game-support/combined-d0/storage-profile/counters'; m=json.loads((counter/'manifest.json').read_text());candidate=json.loads((d.parent/'full-candidate/manifest.json').read_text());build=json.loads((counter/'native-build.json').read_text())
assert build['exit_code']==0 and build['source_restored'] and build['host_restored']
for n,h in candidate['files'].items():assert sha(root/n)==h,n
for n,h in m['files'].items():assert sha(root/n)==h['original_sha256'],n
host=Path(build['diagnostic_host']['path']);assert sha(host)==build['diagnostic_host']['sha256']
assert not (d/'profile.json').exists()
argv=[str(host),'--compiler','tools/compiler','--compiler-core','packages/core','--root',str(root),'--source','tools/toolchain/Build.chibi','--core','packages/core','--entry','Build','--timeout-ms','1800000','--','third-party','build']
r={'argv':argv,'candidate_tree':candidate['tree'],'host_sha256':sha(host),'instrumentation_manifest_sha256':sha(counter/'manifest.json'),'bootstrap_sha256':sha(root/'tools/compiler/bootstrap/compiler.chibip'),'environment':{'CHIBI_STORAGE_DIAG':'1'},'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
started=time.monotonic();proc=None;status=1;cancelled=0;launching=True
# Exclusive ownership precedes signal/cleanup handling, so another invocation
# cannot overwrite a running measurement or publish its receipt.
out=(d/'profile.log').open('xb')
handlers={x:signal.getsignal(x) for x in [signal.SIGINT,signal.SIGTERM]}
def cancel(signum,frame):
 global cancelled
 cancelled=signum
 if not launching:raise KeyboardInterrupt
for x in handlers:signal.signal(x,cancel)
try:
 with out:
  if cancelled:raise KeyboardInterrupt
  # Record cancellation during Popen; only raise after owning the child handle.
  proc=subprocess.Popen(argv,cwd=root,env={**os.environ,**r['environment']},stdout=out,stderr=subprocess.STDOUT,start_new_session=True)
  r['pid']=proc.pid
  launching=False
  if cancelled:raise KeyboardInterrupt
  status=proc.wait()
except KeyboardInterrupt:
 status=128+(cancelled or signal.SIGINT)
finally:
 for x in handlers:signal.signal(x,signal.SIG_IGN)
 if proc:
  try:os.killpg(proc.pid,signal.SIGTERM)
  except ProcessLookupError:pass
  try:proc.wait(timeout=5)
  except subprocess.TimeoutExpired:
   try:os.killpg(proc.pid,signal.SIGKILL)
   except ProcessLookupError:pass
   proc.wait()
  try:os.killpg(proc.pid,signal.SIGKILL)
  except ProcessLookupError:pass
 for x,h in handlers.items():signal.signal(x,h)
r.update(exit_code=status,elapsed_seconds=round(time.monotonic()-started,3),completed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),log_sha256=sha(d/'profile.log'))
for n,h in candidate['files'].items():assert sha(root/n)==h,n
for n,h in m['files'].items():assert sha(root/n)==h['original_sha256'],n
r['production_source_verified_unchanged']=True
with (d/'profile.json').open('x') as receipt:receipt.write(json.dumps(r,indent=2)+'\n')
print(json.dumps(r,indent=2),flush=True)
print('\n'.join((d/'profile.log').read_text(errors='replace').splitlines()[-80:]),flush=True)
sys.exit(status)
