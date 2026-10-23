import json,subprocess,os,time
root=os.getcwd()
scratch='/Users/cwaldner/Personal/chibi/.scratch/shared-sequences-20261022/source-build'
entry=next(x for x in json.load(open('build-release/link_commands.json')) if x['output'].endswith('/sdk/chibi-build-host'))
args=[];skip=False
for arg in entry['arguments']:
    if skip:skip=False;continue
    if arg=='-o':skip=True;continue
    if arg.endswith('.o'):continue
    args.append(arg)
compile=next(x for x in json.load(open('build-release/compile_commands.json')) if x['file'].endswith('/compiler_json.c'))
includes=[]
for i,arg in enumerate(compile['arguments']):
    if arg=='-isystem':includes+=['-isystem',compile['arguments'][i+1]]
args[1:1]=['-std=gnu23','-I'+root+'/src','-I'+root]+includes+[scratch+'/prepare-compiler.c',root+'/build-release/objects/chibi_compiler_host/src/toolchain/compiler_json.c.o',root+'/build-release/objects/chibi_compiler_host/src/toolchain/compiler_syntax.c.o']
args+=['-Wl,-dead_strip','-o',scratch+'/prepare-compiler']
start=time.monotonic()
p=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
open(scratch+'/prepare-compiler-link.log','w').write(p.stdout)
if p.returncode:print(p.stdout);raise SystemExit(p.returncode)
p=subprocess.run([scratch+'/prepare-compiler',os.path.realpath('tools/compiler'),os.path.realpath('packages/core')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
log=p.stdout+'\nelapsed_seconds='+str(round(time.monotonic()-start,3))+'\n'
open(scratch+'/prepare-compiler.log','w').write(log)
print(log)
if p.returncode == 0: os.remove(scratch+'/prepare-compiler')
raise SystemExit(p.returncode)
