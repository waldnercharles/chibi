from pathlib import Path
import subprocess,os,json,hashlib,time
w=Path.cwd();p=w/'.chibi/compound-proof/green';env=os.environ.copy();env.update(CHIBI_LANGUAGE_BUILD_DIR=str(p),CHIBI_RELEASE_LANGUAGE_BUILD_DIR=str(p));env.pop('CHIBI_BOOTSTRAP_SEED',None);rows=[]
base='language-package-compiler-chibi'
for selector in [base+'-collections-compiled',base+'-types',base+'-types-compiled',base+'-debug-free',base+'-imports',base+'-imports-compiled',base+'-native-arguments','language-integer-shift-counts','language-integer-shift-counts-authored','language-semantic','language-integer-semantics']:
 cmd=['python3','tools/test_policy.py','--timeout','300','tool',selector];log=p/(selector+'.log');start=time.monotonic()
 with log.open('w') as f:rc=subprocess.call(cmd,cwd=w,env=env,stdout=f,stderr=subprocess.STDOUT)
 row={'selector':selector,'command':cmd,'exit':rc,'seconds':round(time.monotonic()-start,3),'log_sha256':hashlib.sha256(log.read_bytes()).hexdigest(),'summary':[s for s in log.read_text().splitlines() if s.startswith(('Summary:','Test command wall time:'))]};rows.append(row);(p/'remaining-results.json').write_text(json.dumps(rows,indent=2)+'\n');print(selector,rc,row['summary'],flush=True)
