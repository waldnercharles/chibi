from pathlib import Path
import hashlib,json,re,shlex,subprocess,os,time,difflib,sys
w=Path.cwd();mode=sys.argv[1];assert mode in ['fixed','watch-reverted'];o=w/'.chibi/compound-proof';out=o/mode;out.mkdir(exist_ok=False)
baseline=w.parent/'chibi-m2-current-seed';build=baseline/'.chibi/build-language';fixture=w/'test/language/package_compiler_chibi.c';basefixture=baseline/'test/language/package_compiler_chibi.c';header=build/'testing/include/package_compiler_chibi_source.h';sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();seed='9711a2f36686e52765b7dd6d28c659b100867971';S='072d9ee157aabea7f7c60f6a7e5a92993639f8a5'
assert subprocess.check_output(['git','-C',str(baseline),'rev-parse','HEAD'],text=True).strip()==seed
assert not subprocess.check_output(['git','-C',str(baseline),'status','--porcelain'],text=True).strip()
changes=subprocess.check_output(['git','diff','--name-only',S],text=True).splitlines();assert set(changes)<=set(['test/language/package_compiler_chibi.c','tools/compiler/package_compiler/Steps.chibi','tools/compiler/package_compiler/References.chibi'])
original=header.read_text();updated=original;embedded={}
for name in ['Steps','References']:
 source=w/('tools/compiler/package_compiler/'+name+'.chibi');base_source=baseline/('tools/compiler/package_compiler/'+name+'.chibi')
 pattern=r'(static const char PACKAGE_COMPILER_'+name.upper()+r'_SOURCE\[\] = \{)(.*?)(\};)';m=re.search(pattern,updated,re.S);assert m
 assert bytes(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{2})',m[2]))==base_source.read_bytes()
 data=source.read_bytes()
 if mode=='watch-reverted' and name=='References':
  old=b'if (!reference && !Parameter(forest, binding))';assert data.count(old)==1;data=data.replace(old,b'if (!reference)')
 embedded[name]={'production_sha256':sha(source),'embedded_sha256':hashlib.sha256(data).hexdigest()}
 (out/(name+'.chibi')).write_bytes(data)
 encoded=' '+''.join(f'0x{x:02x},' for x in data)+' 0 '
 updated=re.sub(pattern,lambda m:m[1]+encoded+m[3],updated,flags=re.S)
(out/header.name).write_text(updated)
s=fixture.read_text();obs=s
if mode=='red':
 needle='''        const sample *s = &samples[i];
        if (s->group != group) {''';assert s.count(needle)==1
 obs=s.replace(needle,'''        const sample *s = &samples[i];
        const char *selected = getenv("CHIBI_COMPOUND_SAMPLE");
        if (selected && strcmp(selected, s->name)) {
            continue;
        }
        if (s->group != group) {''')
copy=out/'package_compiler_chibi.c';copy.write_text(obs);(out/'observation.patch').write_text(''.join(difflib.unified_diff(s.splitlines(True),obs.splitlines(True),fromfile='production fixture',tofile='scratch selection only')))
entry=next(x for x in json.loads((build/'compile_commands.json').read_text()) if x['file']==str(basefixture));c=shlex.split(entry['command']);c[1:1]=['-I'+str(out),'-I'+str(w/'test/language'),'-H'];c[c.index(str(basefixture))]=str(copy);oldobj=c[c.index('-o')+1];obj=str(out/'package_compiler_chibi.o');c[c.index('-o')+1]=obj
for flag in ['-MF','-MT']:
 if flag in c:c[c.index(flag)+1]=str(out/'dependency.d') if flag=='-MF' else obj
l=shlex.split((build/'CMakeFiles/test_language.dir/link.txt').read_text());assert l.count(oldobj)==1;l[l.index(oldobj)]=obj;l[l.index('-o')+1]=str(out/'test_language');l=[str(build/x) if not x.startswith('-') and not Path(x).is_absolute() and x.endswith(('.o','.a')) else x for x in l]
record={'mode':mode,'reference_S':S,'native_reference':seed,'base_driver_sha256':sha(build/'test_language'),'production_fixture_sha256':sha(fixture),'scratch_fixture_sha256':sha(copy),'embedded_sources':embedded,'embedded_header_sha256':sha(out/header.name),'native_compile_database_sha256':sha(build/'compile_commands.json'),'method':'Only current C fixture object rebuilt and relinked against exact9711 native objects; embedded current Chibi Steps and References replaced in scratch header. Red-only case selection preserves all production assertions. No seed generation/default build acceptance.','runs':[]}
(o/(mode+'-source.patch')).write_bytes(subprocess.check_output(['git','diff',S,'--','test/language/package_compiler_chibi.c','tools/compiler/package_compiler/Steps.chibi','tools/compiler/package_compiler/References.chibi']))
def run(name,cmd,cwd,env=None):
 log=out/(name+'.log');t=time.monotonic()
 with log.open('w') as f:rc=subprocess.call(cmd,cwd=cwd,stdout=f,stderr=subprocess.STDOUT,env=env)
 row={'name':name,'command':cmd,'cwd':str(cwd),'exit':rc,'seconds':round(time.monotonic()-t,3),'log_sha256':sha(log),'summary':[s for s in log.read_text().splitlines() if s.startswith(('Summary:','Test command wall time:'))]};record['runs'].append(row);(out/'results.json').write_text(json.dumps(record,indent=2)+'\n');print(mode,name,rc,row['summary'],flush=True);return rc
prefix=['python3',str(w/'tools/test_policy.py'),'--timeout','120','native']
if run('compile',prefix+c,build):raise SystemExit(1)
assert str(out/header.name) in (out/'compile.log').read_text()
if run('link',prefix+l,build):raise SystemExit(1)
record['driver_sha256']=sha(out/'test_language');env=os.environ.copy();env.pop('CHIBI_BOOTSTRAP_SEED',None);env.update(CHIBI_LANGUAGE_BUILD_DIR=str(out),CHIBI_RELEASE_LANGUAGE_BUILD_DIR=str(out))
base='language-package-compiler-chibi';coll=base+'-collections'
if mode=='red':
 names=['mixed compound assignment','mixed compound field assignment','mixed compound bitwise assignment','mixed compound reference assignment','mixed compound reference field assignment','mixed compound boxed local assignment','mixed compound boxed parameter assignment']
 for i,name in enumerate(names):
  for b in range(2):
   selector=(base if i<3 else coll)+('-compiled' if b else '');env['CHIBI_COMPOUND_SAMPLE']=name;rc=run(str(i)+'-'+str(b),['python3','tools/test_policy.py','--timeout','300','tool',selector],w,env);log=(out/(str(i)+'-'+str(b)+'.log')).read_text();assert rc==1 and 'package compiler '+name+': accepted' in log,(name,rc,log[-2500:]);record['runs'][-1]['selected_case']=name;(out/'results.json').write_text(json.dumps(record,indent=2)+'\n')
elif mode=='watch-reverted':
 for selector in [coll,coll+'-compiled']:
  rc=run(selector,['python3','tools/test_policy.py','--timeout','300','tool',selector],w,env)
  assert rc==1
else:
 for selector in [base,base+'-compiled',coll,coll+'-compiled',base+'-types',base+'-types-compiled',base+'-debug-free',base+'-imports',base+'-imports-compiled',base+'-native-arguments','language-integer-shift-counts','language-integer-shift-counts-authored','language-semantic','language-integer-semantics']:
  rc=run(selector,['python3','tools/test_policy.py','--timeout','300','tool',selector],w,env)
  if rc:raise SystemExit(rc)
assert sha(build/'test_language')==record['base_driver_sha256'];assert sha(fixture)==record['production_fixture_sha256'];assert sha(header)==hashlib.sha256(original.encode()).hexdigest();(out/'results.json').write_text(json.dumps(record,indent=2)+'\n')
