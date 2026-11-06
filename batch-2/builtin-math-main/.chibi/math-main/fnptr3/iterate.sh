#!/bin/sh
# Recovers the compiler from the sources, installs it in the scratch clone and builds the scratch project.
R=/Users/cwaldner/Personal/chibi/.worktrees/builtin-math-main/chibi
F=$R/.chibi/math-main/fnptr3
cd $R
rm -rf $F/compiler && cp -c -R tools/compiler $F/compiler || exit 1
mkdir -p $F/stage1
$F/runtime/chibi-compiler-host-macos-arm64 --bootstrap $R/tools/compiler/bootstrap --compiler $R/tools/compiler --core $R/packages/core --recover $F/stage1/compiler.chibip || exit 1
cp $F/stage1/compiler.chibip $F/compiler/bootstrap/compiler.chibip
python3 - <<'P'
import json,hashlib,os
F='.chibi/math-main/fnptr3/compiler'
m=json.load(open(F+'/bootstrap/bootstrap.json'))
a=open(F+'/bootstrap/compiler.chibip','rb').read()
m['closure']['bytes']=len(a); m['closure']['sha256']=hashlib.sha256(a).hexdigest()
for s in m['sources']:
    p=os.path.join(F,s['path'])
    if s['path'].startswith('/sdk/core/'): p=os.path.join('packages/core',s['path'][len('/sdk/core/'):])
    b=open(p,'rb').read()
    s['bytes']=len(b); s['sha256']=hashlib.sha256(b).hexdigest()
json.dump(m,open(F+'/bootstrap/bootstrap.json','w'),indent=2)
P
exec $F/run.sh ${1:-0}
