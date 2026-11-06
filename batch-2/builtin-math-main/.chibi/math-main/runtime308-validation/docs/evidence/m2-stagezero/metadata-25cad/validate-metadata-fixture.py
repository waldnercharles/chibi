from pathlib import Path
import hashlib,json,re,shlex,subprocess,os,time,difflib
w=Path.cwd();root=w/'.chibi/stagezero-validation/source';build=root/'build-release';out=w/'.chibi/stagezero-validation/metadata-fixture';out.mkdir(exist_ok=False)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
fixture=root/'test/language/package_compiler_chibi.c';header=build/'testing/include/package_compiler_chibi_source.h';original_header=header.read_text();part='Steps';src=(root/'tools/compiler/package_compiler/Steps.chibi').read_text();before='''            if (NumberLiteral(forest, item.node)) {
                code.extend(LiteralWatch(forest, ops, at.slot, storage, item.node));
            }
''';assert src.count(before)==1
entry=next(x for x in json.loads((build/'compile_commands.json').read_text()) if x['file']==str(fixture));original_compile=shlex.split(entry['command']);original_link=shlex.split((build/'CMakeFiles/test_language.dir/link.txt').read_text())
manifest={'source':'25cad452ae87beb899d1f2b43bb30a10c09af498','method':'Exact current C fixture and embedded Chibi sources; unchanged native source objects from d459 build. Old asset artifact remains linked but is unused by these selected compiler tests. This is diagnostic fixture execution, NOT default asset/fixedpoint acceptance.','base_driver_sha256':sha(build/'test_language'),'fixture_sha256':sha(fixture),'header_sha256':sha(header),'variants':[]}
def run(cmd,cwd,path,env=None):
 t=time.monotonic()
 with path.open('w') as f:rc=subprocess.call(cmd,cwd=cwd,stdout=f,stderr=subprocess.STDOUT,env=env)
 return {'command':cmd,'cwd':str(cwd),'exit':rc,'seconds':round(time.monotonic()-t,3),'log':str(path),'log_sha256':sha(path),'summary':[s for s in path.read_text().splitlines() if s.startswith(('Summary:','Test command wall time:'))]}
for name,mut in [('reviewed',src),('lost-complement-watch',src.replace(before,''))]:
 p=out/name;p.mkdir();pattern=r'(static const char PACKAGE_COMPILER_STEPS_SOURCE\[\] = \{)(.*?)(\};)';m=re.search(pattern,original_header,re.S);assert m;assert bytes(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{2})',m[2]))==src.encode()
 encoded=' '+''.join(f'0x{x:02x},' for x in mut.encode())+' 0 '
 h=re.sub(pattern,lambda m:m[1]+encoded+m[3],original_header,flags=re.S);(p/header.name).write_text(h)
 (p/'production.patch').write_text(''.join(difflib.unified_diff(src.splitlines(True),mut.splitlines(True),fromfile='a/tools/compiler/package_compiler/Steps.chibi',tofile='b/tools/compiler/package_compiler/Steps.chibi')))
 c=original_compile.copy();c[1:1]=['-I'+str(p),'-H'];obj=str(p/'package_compiler_chibi.c.o');old_obj=c[c.index('-o')+1];c[c.index('-o')+1]=obj
 for flag in ['-MF','-MT']:
  if flag in c:c[c.index(flag)+1]=str(p/'dependency.d') if flag=='-MF' else obj
 row={'name':name,'source_sha256':hashlib.sha256(mut.encode()).hexdigest(),'header_sha256':sha(p/header.name)};prefix=['python3',str(root/'tools/test_policy.py'),'--timeout','120','native'];row['compile']=run(prefix+c,build,p/'compile.log');assert row['compile']['exit']==0;assert str(p/header.name) in (p/'compile.log').read_text()
 l=original_link.copy();assert l.count(old_obj)==1;l[l.index(old_obj)]=obj;l[l.index('-o')+1]=str(p/'test_language');l=[str(build/x) if not x.startswith('-') and not Path(x).is_absolute() and x.endswith(('.o','.a')) else x for x in l];row['link']=run(prefix+l,build,p/'link.log');assert row['link']['exit']==0;row['driver_sha256']=sha(p/'test_language');row['tests']=[];env=os.environ.copy();env.update(CHIBI_LANGUAGE_BUILD_DIR=str(p),CHIBI_RELEASE_LANGUAGE_BUILD_DIR=str(p))
 selectors=['language-package-compiler-chibi','language-package-compiler-chibi-types','language-package-compiler-chibi-compiled','language-package-compiler-chibi-types-compiled','language-package-compiler-chibi-debug-free'] if name=='reviewed' else ['language-package-compiler-chibi']
 manifest['variants'].append(row)
 for selector in selectors:
  result=run(['python3','tools/test_policy.py','--timeout','300','tool',selector],root,p/(selector+'.log'),env);row['tests'].append(result);(out/'results.json').write_text(json.dumps(manifest,indent=2)+'\n');print(name,selector,result['exit'],result['summary'],flush=True)
 if name=='lost-complement-watch':
  text=(p/'language-package-compiler-chibi.log').read_text();assert row['tests'][0]['exit']==1 and 'folded complement retains its watch: differs' in text and 'stage zero watch' in text,text[-1500:]
assert sha(fixture)==manifest['fixture_sha256'];assert sha(header)==manifest['header_sha256'];assert sha(build/'test_language')==manifest['base_driver_sha256']
