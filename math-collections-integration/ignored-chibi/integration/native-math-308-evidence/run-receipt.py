import os,sys,pathlib,subprocess,time,json,hashlib
label=sys.argv[1];args=sys.argv[2:];root=pathlib.Path(__file__).resolve().parent;snapshot=root.parent/'native-math-308-snapshot';log=root/(label+'.log')
started=time.time();now=time.monotonic()
with log.open('wb') as out:
 p=subprocess.run(args,cwd=snapshot,stdout=out,stderr=subprocess.STDOUT)
elapsed=time.monotonic()-now
body=log.read_bytes();text=body.decode(errors='replace')
r={'name':label,'command':args,'cwd':str(snapshot),'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=snapshot,text=True).strip(),'snapshot_revision':subprocess.check_output(['git','describe','--always','--dirty'],cwd=snapshot,text=True).strip(),'started_unix':started,'environment':{k:v for k,v in os.environ.items() if k.startswith('CHIBI_') or k in ['CFLAGS','CPPFLAGS','LDFLAGS']},'wall_seconds':elapsed,'exit_code':p.returncode,'log':str(log),'log_sha256':hashlib.sha256(body).hexdigest(),'summaries':[s for s in text.splitlines() if 'Summary:' in s or 'Test command wall time:' in s]}
(root/(label+'.json')).write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r),flush=True);sys.exit(p.returncode)
