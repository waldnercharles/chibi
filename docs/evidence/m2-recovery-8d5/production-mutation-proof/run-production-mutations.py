from pathlib import Path
import hashlib,json,re,shlex,subprocess,os,time,difflib,sys
root=Path.cwd();build=root/'build-recovery';out=root/'.chibi/production-mutation-proof';out.mkdir(exist_ok=True)
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
fixture=root/'test/language/package_compiler_chibi.c'
header=build/'testing/include/package_compiler_chibi_source.h';original_header=header.read_text()
entry=next(x for x in json.loads((build/'compile_commands.json').read_text()) if x['file']==str(fixture))
original_compile=shlex.split(entry['command']);original_link=shlex.split((build/'CMakeFiles/test_language.dir/link.txt').read_text())
compile_sources={n:(root/f'tools/compiler/package_compiler/{n}.chibi').read_text() for n in ['Compile','Steps']}
imports=['language-package-compiler-chibi-imports','language-package-compiler-chibi-imports-compiled']
collections=['language-package-compiler-chibi-collections','language-package-compiler-chibi-collections-compiled']
order_before='''        finish := Resume(item.node, 1, found, count);'''
order_after='''        if (associated) {
            reversed: Work[] = [];
            remaining := length(next);
            while (remaining > 0) {
                remaining -= 1;
                reversed.append(next[remaining]);
            }
            next = reversed;
        }
        finish := Resume(item.node, 1, found, count);'''
guard_before='''    if (Floating(inner)) {
        return Evaluated.Failed(Diagnose(forest, value, file,
                                         "floating-to-integer constant conversion is not supported yet"));
    }'''
guard_after=guard_before.replace('if (Floating(inner))','if (false)')
variants=[
('constant-floating-bits-as-integer','Compile',guard_before,guard_after,imports),
('typed-float-zero','Steps','return [Instruction(ops.convert, to, slot, input, 0, 0, node)];','return [ConstantCode(ops, to, uint64(0), slot, node)];',imports),
('constant-context-lost','Compile','inner = converted;','inner = DoubleStorage;',imports),
('constant-sign-lost','Compile','return Folding.Folded(bits ^ sign);','return Folding.Folded(bits);',imports),
('native-port-permutation-lost','Steps','arguments[parameter] = values[first + ordinal];','arguments[ordinal] = values[first + ordinal];',['language-package-compiler-chibi-native-arguments']),
('native-evaluation-reversed','Steps',order_before,order_after,['language-package-compiler-chibi-native-arguments']),
('array-receiver-watch-lost','Steps','// evaluating the item (which can invoke another body).\n                        if (ops.debug) {','// evaluating the item (which can invoke another body).\n                        if (false) {',collections),
]
manifest={'base_commit':'8d5ff52831e12f624a10c178ef9d9aef072881ee','base_fixture_sha256':sha(fixture),'base_header_sha256':sha(header),'base_driver_sha256':sha(build/'test_language'),'method':'One embedded production Chibi source mutation; original C/pico assertions; one original C fixture object recompiled, original link inputs reused except this object and output. No bootstrap regeneration.','variants':[]}
if (out/'manifest.json').exists():
 manifest=json.loads((out/'manifest.json').read_text())
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
def run(cmd,cwd,path,env=None):
 t=time.monotonic()
 with path.open('w') as f:result=subprocess.run(cmd,cwd=cwd,stdout=f,stderr=subprocess.STDOUT,env=env)
 return {'command':cmd,'cwd':str(cwd),'exit':result.returncode,'seconds':round(time.monotonic()-t,3),'log':str(path),'log_sha256':sha(path)}
for name,part,before,after,selectors in variants:
 if len(sys.argv)>1 and name not in sys.argv[1:]:continue
 p=out/name;p.mkdir(exist_ok=False);src=compile_sources[part];assert src.count(before)==1,(name,src.count(before));mut=src.replace(before,after)
 if name=='native-evaluation-reversed':
  old='arguments[parameter] = values[first + ordinal];';assert mut.count(old)==1;mut=mut.replace(old,'arguments[parameter] = values[first + count - 1 - ordinal];')
 (p/f'{part}.original.chibi').write_text(src);(p/f'{part}.mutant.chibi').write_text(mut)
 (p/'production.patch').write_text(''.join(difflib.unified_diff(src.splitlines(True),mut.splitlines(True),fromfile=f'a/tools/compiler/package_compiler/{part}.chibi',tofile=f'b/tools/compiler/package_compiler/{part}.chibi')))
 pattern=rf'(static const char PACKAGE_COMPILER_{part.upper()}_SOURCE\[\] = \{{)(.*?)(\}};)';matches=list(re.finditer(pattern,original_header,re.S));assert len(matches)==1
 old_array=matches[0].group(2);decoded=bytes(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{2})',old_array));assert decoded==src.encode()
 encoded=' '+''.join(f'0x{x:02x},' for x in mut.encode())+' 0 '
 new_header=re.sub(pattern,lambda m:m[1]+encoded+m[3],original_header,flags=re.S);(p/header.name).write_text(new_header)
 c=original_compile.copy();c[1:1]=['-I'+str(p),'-H'];obj=str(p/'package_compiler_chibi.c.o');old_obj=c[c.index('-o')+1];c[c.index('-o')+1]=obj
 for flag in ['-MF','-MT']:
  if flag in c:c[c.index(flag)+1]=str(p/'dependency.d') if flag=='-MF' else obj
 row={'name':name,'component':part,'source_sha256':sha(p/f'{part}.original.chibi'),'mutant_sha256':sha(p/f'{part}.mutant.chibi'),'header_sha256':sha(p/header.name),'selectors':selectors}
 row['compile']=run(['python3',str(root/'tools/test_policy.py'),'--timeout','120','native']+c,build,p/'compile.log');assert row['compile']['exit']==0,row
 assert str(p/header.name) in (p/'compile.log').read_text()
 l=original_link.copy();assert l.count(old_obj)==1,(name,old_obj);l[l.index(old_obj)]=obj;l[l.index('-o')+1]=str(p/'test_language')
 l=[str(build/x) if not x.startswith('-') and not Path(x).is_absolute() and x.endswith(('.o','.a')) else x for x in l]
 row['link']=run(['python3',str(root/'tools/test_policy.py'),'--timeout','120','native']+l,build,p/'link.log');assert row['link']['exit']==0,row
 row['driver_sha256']=sha(p/'test_language');row['tests']=[];env=os.environ.copy();env['CHIBI_LANGUAGE_BUILD_DIR']=str(p);env['CHIBI_RELEASE_LANGUAGE_BUILD_DIR']=str(p)
 for selector in selectors:
  result=run(['python3','tools/test_policy.py','--timeout','300','tool',selector],root,p/(selector+'.log'),env);row['tests'].append(result)
 (p/'results.json').write_text(json.dumps(row,indent=2)+'\n');manifest['variants'].append(row);(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(name,[(x['exit'],x['seconds']) for x in row['tests']],flush=True)
assert sha(fixture)==manifest['base_fixture_sha256'];assert sha(header)==manifest['base_header_sha256'];assert sha(build/'test_language')==manifest['base_driver_sha256']
