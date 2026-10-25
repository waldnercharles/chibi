from pathlib import Path
import subprocess,json,os,hashlib,time
root=Path.cwd();p=root/'.chibi/sanitizer-proof';build=root/'build-sanitizer';sha=lambda q:hashlib.sha256(q.read_bytes()).hexdigest()
entries=json.loads((build/'compile_commands.json').read_text()); selected=[x for x in entries if '/src/execution/' in x['file'] or '/src/host/' in x['file'] or x['file'].endswith('/package_compiler_chibi.c')]
assert selected and all('-fsanitize=address,undefined' in x['command'] for x in selected)
links={str(q.relative_to(build)):q.read_text() for q in [build/'CMakeFiles/test_language.dir/link.txt',build/'sdk/CMakeFiles/chibi-compiler-host.dir/link.txt']};assert all('-fsanitize=address,undefined' in v for v in links.values())
identity={'source':'8d5ff52831e12f624a10c178ef9d9aef072881ee','overlay':'Only current production bootstrap pair, identical to final Release snapshot','driver_sha256':sha(build/'test_language'),'host_sha256':sha(build/'sdk/chibi-compiler-host'),'instrumented_entries':selected,'links':links,'sanitizer_environment':{k:os.environ.get(k) for k in ['ASAN_OPTIONS','UBSAN_OPTIONS','LSAN_OPTIONS','DYLD_INSERT_LIBRARIES']},'cache_sha256':sha(build/'CMakeCache.txt')};(p/'identity.json').write_text(json.dumps(identity,indent=2)+'\n')
rows=json.loads((root/'.chibi/historical-input-proof/test-results.json').read_text())['rows'];names=[x['selector'] for x in rows]+['language-asset-compiler-host'];env=os.environ.copy();env['CHIBI_LANGUAGE_BUILD_DIR']='build-sanitizer';env['CHIBI_RELEASE_LANGUAGE_BUILD_DIR']='build-sanitizer';results=[]
for name in names:
 cmd=['python3','tools/test_policy.py','--timeout','300','tool',name];log=p/(name+'.log');t=time.monotonic()
 with log.open('w') as f:rc=subprocess.call(cmd,stdout=f,stderr=subprocess.STDOUT,env=env)
 text=log.read_text();row={'selector':name,'command':cmd,'exit':rc,'seconds':round(time.monotonic()-t,3),'log_sha256':sha(log),'summary':[x for x in text.splitlines() if x.startswith(('Summary:','Test command wall time:'))],'sanitizer_report':any(s in text for s in ['ERROR: AddressSanitizer','runtime error:','SUMMARY: UndefinedBehaviorSanitizer','LeakSanitizer:'])};results.append(row);(p/'results.json').write_text(json.dumps(results,indent=2)+'\n');print(name,rc,row['summary'],flush=True)
 if row['sanitizer_report']:break
