from pathlib import Path
import json,sys,re,struct
root=Path.cwd();sys.path.insert(0,str(root/'tools'));import test_policy
metadata=json.loads((root/'tools/compiler/bootstrap/bootstrap.json').read_text())
schema='--schemas' in sys.argv
minimal='--minimal' in sys.argv
use_bytes='--bytes' in sys.argv
source=r'''package asset
import data "game:assets"
Health :: (level: int) -> int { total: int = 3; i: int = 0; while i < level { total += 2; i += 1; } return total; }
pub Build :: () -> byte[] {
    return data.Encode(data.Object([
        data.Field.Named("name", data.Value.Text("Enemy")),
        data.Field.Named("health", data.Value.Int(int64(Health(3)))),
        data.Field.Named("nested", data.List([data.Object([data.Field.Named("ready", data.Value.Bool(true))]), data.Value.Text("é\n\"\\")])),
        data.Field.Named("wide", data.Value.UInt(0xFFFFFFFFFFFFFFFF)),
        data.Field.Named("low", data.Value.Int(-9223372036854775808)),
        data.Field.Named("real", data.Value.Real(1.2345678901234567))
    ]));
}
'''
if '--overrides' in sys.argv:
 source=r'''package asset
import data "game:assets"
pub Parameters :: () -> data.Value {
    return data.Object([data.Field.Named("level",data.Value.Int(3)),data.Field.Named("enabled",data.Value.Bool(true))]);
}
pub Overrides :: () -> data.Value {
    return data.Object([data.Field.Named("Sample", data.Object([data.Field.Named("position",data.Object([data.Field.Named("x",data.Value.Real(1.0))]))])),data.Field.Named("Health",data.Object([data.Field.Named("current",data.Value.Int(90))]))]);
}
MakeEnemy :: (parameters: data.Value) -> data.Value {
    level: int = int(data.AsInt(data.Get(parameters,"level")));
    current: int = 3;
    while level > 0 { current += 2; level -= 1; }
    if !data.AsBool(data.Get(parameters,"enabled")) { current = 0; }
    return data.Object([
        data.Field.Named("name",data.Value.Text("Enemy")),
        data.Field.Named("parameters",parameters),
        data.Field.Named("components",data.Object([
            data.Field.Named("Sample",data.Object([
                data.Field.Named("position",data.Object([data.Field.Named("x",data.Value.Real(10.0)),data.Field.Named("y",data.Value.Real(20.0))])),
                data.Field.Named("label",data.Value.Text("Guard"))])),
            data.Field.Named("Health",data.Object([data.Field.Named("current",data.Value.Int(int64(current))),data.Field.Named("maximum",data.Value.Int(200))]))
        ]))
    ]);
}
pub Build :: () -> byte[] {
    return data.Encode(data.ApplyPrototype(MakeEnemy(Parameters()),data.Object([
        data.Field.Named("components",Overrides()),data.Field.Named("overrides",Overrides())
    ])));
}
'''
if '--reject' in sys.argv:
 source='package asset\npub Build :: () -> byte[] { negative: uint64 = -1; return []; }'
paths=['/project/asset/Asset.chibi','/sdk/game/assets/Assets.chibi','/sdk/core/strings/Strings.chibi']
sources=[source,(root/'packages/game/assets/Assets.chibi').read_text(),(root/'packages/core/strings/Strings.chibi').read_text()]
if schema:
 for disk in sorted((root/'packages').rglob('*.chibi')):
  name='/sdk/'+disk.relative_to(root/'packages').as_posix()
  if name not in paths: paths.append(name);sources.append(disk.read_text())
if minimal:
 paths=['/project/asset/Asset.chibi'];sources=['package asset\nValue :: union { Text(value:string) | Array(values:Value[]) }\npub Build :: () -> byte[] { v := Value.Array([Value.Text("x")]); switch v { case .Text(s): return [2]; case .Array(values): return [byte(length(values))]; } return []; }']
args=['/project',['core','game'],['/sdk/core','/sdk/game'],paths,sources,False,*metadata['target']]
stem='asset-minimal' if minimal else 'asset-negative' if '--reject' in sys.argv else 'asset-overrides' if '--overrides' in sys.argv else 'asset-specimen'
request=root/('.chibi/'+stem+'-input.json');request.write_text(json.dumps(args))
def ident(name):
 h=14695981039346656037
 for b in ('package-symbol:'+name).encode(): h=((h^b)*1099511628211)&((1<<64)-1)
 return hex(h)
host=root/'build/sdk/chibi-compiler-host'
cmd=[str(host),'--artifact',str(root/'.chibi/asset-compiler-generated/asset_compiler.chibip'),'--entry',ident('project:asset_compiler:1:CompileBytes:callable' if use_bytes else 'project:asset_compiler:1:Compile:callable'),'--input',str(request),'--backend',('bytecode' if '--bytecode' in sys.argv else 'sljit'),'--budget','100000000','--timeout-ms','60000']
log=root/('.chibi/'+stem+'-compile.log')
packet=root/('.chibi/'+stem+'-packet.bin')
if use_bytes: cmd.extend(['--output-bytes',str(packet)])
status=0
if '--resume' not in sys.argv:
 with log.open('w') as out: status=test_policy.run_commands([cmd],output=out)
if status: print(log.read_text());raise SystemExit(status)
if use_bytes:
 raw=packet.read_bytes()
 if len(raw)<29: raise SystemExit('Truncated compile envelope')
 entry=struct.unpack_from('<Q',raw,1)[0]
 positions=list(struct.unpack_from('<5i',raw,9))
 values=[bool(raw[0]),entry,list(raw[29:]) if raw[0] else [],*positions,'' if raw[0] else raw[29:].decode()]
else:
 clean=re.sub(r'compiler-host[^\n]*\n','',log.read_text())
 start=clean.find('[{')
 rows=[json.JSONDecoder().raw_decode(clean[start:])[0]] if start>=0 else []
 if not rows: print(log.read_text());raise SystemExit('No compile result')
 values=rows[0][0]['values']
if not values[0]:
 print(json.dumps(values))
 if '--reject' in sys.argv and 'outside the range' in values[-1] and values[3]==0: raise SystemExit(0)
 raise SystemExit('Asset compile rejected')
if '--reject' in sys.argv: raise SystemExit('Expected negative uint64 rejection')
artifact=root/('.chibi/'+stem+'.chibip');artifact.write_bytes(bytes(values[2]))
empty=root/'.chibi/asset-empty.json';empty.write_text('[]')
result=root/('.chibi/'+stem+'.json')
cmd=[str(host),'--artifact',str(artifact),'--entry',hex(values[1]),'--input',str(empty),'--backend','bytecode','--budget','1000000','--timeout-ms','60000','--output-bytes',str(result)]
status=test_policy.run_commands([cmd])
if status: raise SystemExit(status)
print('artifact_bytes',artifact.stat().st_size,'schemas',schema)
print(result.read_text())
