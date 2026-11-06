#!/usr/bin/env python3
import json, os, subprocess, tempfile
R='/Users/cwaldner/Personal/chibi/.worktrees/builtin-math-main/chibi'; F=R+'/.chibi/math-main/fnptr'
host=F+'/runtime/chibi-compiler-host-macos-arm64'; artifact=F+'/stage1/compiler.chibip'
def entry(symbol):
    h=14695981039346656037
    for b in ('package-symbol:'+symbol).encode(): h=((h^b)*1099511628211)&0xFFFFFFFFFFFFFFFF
    return '0x%016x'%(h or 1)
paths=['native_emitter/Emitter.chibi']; texts=[open(F+'/driver/emitter/Emitter.chibi').read()]
for part in ['scalar_facts','scalar_llvm','llvm_native','native_abi']:
    for root,_,files in sorted(os.walk(R+'/tools/compiler/'+part)):
        if '/tests' in root: continue
        for n in sorted(files):
            if n.endswith('.chibi'):
                paths.append(os.path.relpath(os.path.join(root,n),R+'/tools/compiler')); texts.append(open(os.path.join(root,n)).read())
paths.append('/sdk/core/strings/Strings.chibi'); texts.append(open(R+'/packages/core/strings/Strings.chibi').read())
target=json.loads(subprocess.check_output([host,'--target-json']))
w=tempfile.mkdtemp(dir=F); open(w+'/in.json','w').write(json.dumps(['/project',['core'],['/sdk/core'],paths,texts,False]+target))
d=subprocess.run([host,'--artifact',artifact,'--entry',entry('project:package_compiler:1:Compile:callable'),'--input',w+'/in.json','--backend','sljit','--budget','10000000000','--timeout-ms','600000'],capture_output=True,text=True)
print(d.returncode, d.stdout[:1500], d.stderr[-600:])
