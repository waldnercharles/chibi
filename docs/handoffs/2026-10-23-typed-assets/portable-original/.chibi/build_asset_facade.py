from pathlib import Path
import json,subprocess,sys
root=Path.cwd()
metadata=json.loads((root/'tools/compiler/bootstrap/bootstrap.json').read_text())
bootstrap='--bootstrap' in sys.argv
emit='--emit' in sys.argv
paths=[] if bootstrap else ['asset_compiler/Compile.chibi']+[p.relative_to(root/'tools/compiler').as_posix() for folder in ['asset_source','asset_packaging'] for p in sorted((root/'tools/compiler'/folder).glob('*.chibi'))]
for item in metadata['sources']:
    if item['path'] not in paths: paths.append(item['path'])
for item in sorted((root/'tools/compiler/package_compiler').glob('*.chibi')):
    rel=item.relative_to(root/'tools/compiler').as_posix()
    if rel not in paths: paths.append(rel)
sources=[]
for path in paths:
    disk=root/'packages/core'/path[len('/sdk/core/'):] if path.startswith('/sdk/core/') else root/'packages/game'/path[len('/sdk/game/'):] if path.startswith('/sdk/game/') else root/'tools/compiler'/path
    sources.append(disk.read_text())
if not bootstrap:
 paths.append('/sdk/game/assets/Assets.chibi');sources.append((root/'packages/game/assets/Assets.chibi').read_text())
args=['/project',(['core'] if bootstrap else ['core','game']),(['/sdk/core'] if bootstrap else ['/sdk/core','/sdk/game']),paths,sources,False,*metadata['target']]
request=root/('.chibi/asset-bootstrap-input.json' if bootstrap else '.chibi/asset-facade-input.json');request.write_text(json.dumps(args))
sys.path.insert(0,str(root/'tools'))
import test_policy
cmd=[str(root/'build/sdk/chibi-compiler-host'),'--artifact',str(root/('tools/compiler/bootstrap/compiler.chibip' if bootstrap else '.chibi/asset-bootstrap.chibip')),'--entry','0x'+format(__import__('functools').reduce(lambda h,b:((h^b)*1099511628211)&((1<<64)-1),b'package-symbol:project:package_compiler:1:Compile:callable',14695981039346656037),'016x'),'--input',str(request),'--backend','sljit','--budget','10000000000','--timeout-ms','60000']
if bootstrap or emit:
    cmd[cmd.index('--entry')+1]=metadata['entry']
    cmd.extend(['--output-bytes',str(root/('.chibi/asset-bootstrap.chibip' if bootstrap else '.chibi/asset_compiler.chibip'))])
raise SystemExit(test_policy.run_commands([cmd]))
