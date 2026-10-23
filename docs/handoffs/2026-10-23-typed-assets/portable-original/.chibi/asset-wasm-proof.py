from pathlib import Path
import json,struct,sys,os,time
root=Path.cwd();sys.path.insert(0,str(root/'tools'));import test_policy
request=json.loads((root/'.chibi/asset-specimen-input.json').read_text())
request[6:]=json.loads((root/'.chibi/asset-wasm-target.json').read_text())
request_path=root/'.chibi/asset-wasm-input.json';request_path.write_text(json.dumps(request))
packet=root/'.chibi/asset-wasm-packet.bin';artifact=root/'.chibi/asset-wasm-specimen.chibip';result=root/'.chibi/asset-wasm-specimen.json'
empty=root/'.chibi/asset-wasm-empty.json';empty.write_text('[]')
h=14695981039346656037
for b in b'package-symbol:project:asset_compiler:1:CompileBytes:callable':h=((h^b)*1099511628211)&((1<<64)-1)
command=['node',str(root/'build-web-sdk/chibi-compiler-host.js')]
start=time.monotonic();os.environ['CHIBI_TEST_DEADLINE']=str(start+60)
with (root/'.chibi/asset-wasm-proof.log').open('w') as log:
 status=test_policy.run_commands([command+['--artifact',str(root/'tools/compiler/asset_compiler/bootstrap/wasm32/asset_compiler.chibip'),'--entry',hex(h),'--input',str(request_path),'--backend','bytecode','--budget','100000000','--timeout-ms','60000','--output-bytes',str(packet)]],output=log)
 if status:raise SystemExit(status)
 raw=packet.read_bytes()
 if len(raw)<29 or not raw[0]:raise SystemExit('CompileBytes rejected: '+repr(raw[29:]))
 entry=struct.unpack_from('<Q',raw,1)[0];artifact.write_bytes(raw[29:])
 status=test_policy.run_commands([command+['--artifact',str(artifact),'--entry',hex(entry),'--input',str(empty),'--backend','bytecode','--budget','1000000','--timeout-ms','60000','--output-bytes',str(result)]],output=log)
 if status:raise SystemExit(status)
actual=json.loads(result.read_text());expected=json.loads((root/'.chibi/asset-specimen.json').read_text())
if actual!=expected:raise SystemExit('Wasm output differs from the native specimen')
print('Actual Wasm Compiler + Build passed',round(time.monotonic()-start,3),'seconds,',artifact.stat().st_size,'prepared bytes')
print(result.read_text())
