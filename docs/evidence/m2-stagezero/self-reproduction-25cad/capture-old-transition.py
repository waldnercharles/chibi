from pathlib import Path
import hashlib,json,subprocess,time,os
w=Path.cwd();r=w/'.chibi/stagezero-validation/source';p=w/'.chibi/stagezero-validation/self-reproduction-25cad';c=p/'captures';host=r/'build-release/sdk/chibi-compiler-host';old=Path('/Users/cwaldner/Documents/Codex/2026-10-24/task-2/chibi-m2-driver-8d5/.chibi/current-source-bootstrap/artifact');sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest();m=json.loads((old/'bootstrap.json').read_text());q=json.loads((c/'request.json').read_text());assert q[0]==m['project'] and q[1:3]==[['core'],[m['core']]] and q[3]==[s['path'] for s in m['sources']] and q[6:]==m['target'] and q[5] is False;assert sha(old/'compiler.chibip')==m['closure']['sha256'];rows=[];env=os.environ.copy();env.pop('CHIBI_BOOTSTRAP_SEED',None)
for name,artifact in [('transition-first',old/'compiler.chibip'),('transition-second',c/'transition-first.chibip')]:
 output=c/(name+'.chibip');assert not output.exists();cmd=['python3','tools/test_policy.py','--timeout','120','native',str(host),'--artifact',str(artifact),'--entry',m['entry'],'--input',str(c/'request.json'),'--backend','sljit','--budget','2000000000','--timeout-ms','40000','--output-artifact',str(output)];log=p/(name+'.log');start=time.monotonic()
 with log.open('w') as f:rc=subprocess.call(cmd,cwd=r,stdout=f,stderr=subprocess.STDOUT,env=env)
 row={'name':name,'command':cmd,'cwd':str(r),'exit':rc,'seconds':round(time.monotonic()-start,3),'input_sha256':sha(artifact),'request_sha256':sha(c/'request.json'),'host_sha256':sha(host),'log_sha256':sha(log)}
 if output.exists():row['output']={'bytes':output.stat().st_size,'sha256':sha(output)}
 rows.append(row);(p/'transition-results.json').write_text(json.dumps(rows,indent=2)+'\n');print(json.dumps(row),flush=True)
 if rc:raise SystemExit(rc)
assert sha(old/'compiler.chibip')==m['closure']['sha256']
