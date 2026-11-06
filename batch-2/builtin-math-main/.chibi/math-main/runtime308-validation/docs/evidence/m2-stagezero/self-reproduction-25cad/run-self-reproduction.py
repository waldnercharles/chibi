from pathlib import Path
import hashlib,json,shlex,subprocess,os,time
w=Path.cwd();r=w/'.chibi/stagezero-validation/source';b=r/'build-release';p=w/'.chibi/stagezero-validation/self-reproduction-25cad';fixture=r/'test/language/package_compiler_chibi.c';copy=p/'package_compiler_chibi.capture.c';sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
identity=json.loads((p/'identity.json').read_text());assert sha(fixture)==identity['production_fixture_sha256'];assert sha(copy)==identity['observation_fixture_sha256'];entry=next(x for x in json.loads((b/'compile_commands.json').read_text()) if x['file']==str(fixture));c=shlex.split(entry['command']);c[1:1]=['-I'+str(r/'test/language'),'-H'];c[c.index(str(fixture))]=str(copy);old_obj=c[c.index('-o')+1];obj=str(p/'package_compiler_chibi.capture.o');c[c.index('-o')+1]=obj
for flag in ['-MF','-MT']:
 if flag in c:c[c.index(flag)+1]=str(p/'dependency.d') if flag=='-MF' else obj
l=shlex.split((b/'CMakeFiles/test_language.dir/link.txt').read_text());assert l.count(old_obj)==1;l[l.index(old_obj)]=obj;l[l.index('-o')+1]=str(p/'test_language');l=[str(b/x) if not x.startswith('-') and not Path(x).is_absolute() and x.endswith(('.o','.a')) else x for x in l]
rows=[]
def run(name,cmd,cwd,env=None):
 start=time.monotonic();log=p/(name+'.log')
 with log.open('w') as f:rc=subprocess.call(cmd,cwd=cwd,stdout=f,stderr=subprocess.STDOUT,env=env)
 row={'name':name,'command':cmd,'cwd':str(cwd),'exit':rc,'seconds':round(time.monotonic()-start,3),'log_sha256':sha(log)};rows.append(row);(p/'results.json').write_text(json.dumps(rows,indent=2)+'\n');print(name,rc,row['seconds'],flush=True);return rc
prefix=['python3',str(r/'tools/test_policy.py'),'--timeout','120','native']
if run('compile',prefix+c,b):raise SystemExit(1)
if run('link',prefix+l,b):raise SystemExit(1)
identity.update(driver_sha256=sha(p/'test_language'),embedded_header_sha256=sha(b/'testing/include/package_compiler_chibi_source.h'));(p/'identity.json').write_text(json.dumps(identity,indent=2)+'\n')
env=os.environ.copy();env.pop('CHIBI_BOOTSTRAP_SEED',None);env['CHIBI_RELEASE_LANGUAGE_BUILD_DIR']=str(p);env['CHIBI_LANGUAGE_BUILD_DIR']=str(p)
rc=run('bootstrap',['python3','tools/test_policy.py','--timeout','600','tool','language-package-compiler-chibi-bootstrap'],r,env)
captures={f.name:{'bytes':f.stat().st_size,'sha256':sha(f)} for f in (p/'captures').iterdir() if f.is_file()};(p/'capture-identities.json').write_text(json.dumps(captures,indent=2)+'\n');print(json.dumps(captures,indent=2),flush=True)
raise SystemExit(rc)
