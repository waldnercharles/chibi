#!/usr/bin/env python3
"""usage: collect.py SOURCE_FILE [PATH]  -> CollectScalar rows summary with the recovered compiler"""
import json, subprocess, sys, tempfile
R='/Users/cwaldner/Personal/chibi/.worktrees/builtin-math-main/chibi'; F=R+'/.chibi/math-main/fnptr'
host=F+'/runtime/chibi-compiler-host-macos-arm64'; artifact=F+'/stage1/compiler.chibip'
text=open(sys.argv[1]).read(); path=sys.argv[2] if len(sys.argv)>2 else 'Scalar.chibi'
target=json.loads(subprocess.check_output([host,'--target-json']))
req=['/project',path,text,target[0]]+target[2:6]
w=tempfile.mkdtemp(dir=F); open(w+'/in.json','w').write(json.dumps(req))
r=subprocess.run([host,'--artifact',artifact,'--entry','0xebbc43e46769862a','--input',w+'/in.json','--backend','sljit','--budget','10000000000','--timeout-ms','120000'],capture_output=True,text=True)
rows=json.loads(r.stdout)[0] if r.stdout.strip() else []
print(len(rows),'rows;',[ (x.get('kind'), x.get('text','')[:60]) for x in rows[1:]][:4])
