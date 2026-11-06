import hashlib,json,shutil,subprocess,time
from pathlib import Path
repo=Path('/Users/cwaldner/Personal/chibi/.worktrees/runtime-sdk-construction/chibi')
out=repo/'.chibi/runtime-sdk/json'
host=Path('/Users/cwaldner/Personal/chibi/.worktrees/cmake-retirement/chibi/build-sdk-reflect/host/chibi-build-host')
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def record(path):return {'sha256':sha(path),'bytes':path.stat().st_size}
source_names=['tools/graph/sdk/JSON.chibi','test/tools/RuntimeSDKJSON.chibi']
sources={name:record(repo/name) for name in source_names}
shutil.copyfile(repo/source_names[0],out/'graph/sdk/JSON.chibi')
shutil.copyfile(repo/source_names[1],out/'RuntimeSDKJSON.chibi')
compiler={str(path.relative_to(repo)):record(path) for base in ['tools/compiler','packages/core'] for path in sorted((repo/base).rglob('*')) if path.is_file()}
(out/'compiler-inputs.json').write_text(json.dumps(compiler,indent=2,sort_keys=True)+'\n')
host_before=record(host)
assert host_before['sha256']=='de63381c9f0cbca82c932db0d9117aa48448d0ff50cd600c3b08efea64f20335'
assert sha(repo/'tools/compiler/bootstrap/compiler.chibip')=='27d8f3d0568e442e1a70b167c04ba97c671ea0d0b1e85e3a7493d88fc59bb912'
protocol=subprocess.check_output([str(host),'--protocol-version'],text=True).strip()
assert protocol=='14',protocol
argv=[str(host),'--compiler',str(repo/'tools/compiler'),'--compiler-core',str(repo/'packages/core'),'--root',str(repo),'--source',str(out/'RuntimeSDKJSON.chibi'),'--core',str(repo/'packages/core'),'--collection','graph='+str(out/'graph'),'--entry','Build','--timeout-ms','300000','--',str(out)]
start=time.monotonic()
with (out/'run.log').open('wb') as log:result=subprocess.run(argv,cwd=repo,stdout=log,stderr=subprocess.STDOUT)
elapsed=time.monotonic()-start
assert result.returncode==0,(result.returncode,(out/'run.log').read_text())
vectors=json.loads((out/'vectors.json').read_text())
for name in vectors['cases']:assert (out/'actual'/name).read_bytes()==(out/'expected'/name).read_bytes(),name
assert {name:record(repo/name) for name in source_names}==sources
assert record(host)==host_before
assert {str(path.relative_to(repo)):record(path) for base in ['tools/compiler','packages/core'] for path in sorted((repo/base).rglob('*')) if path.is_file()}==compiler
receipt={'status':'pass','scope':'Authored SDK JSON codec only, compiled and executed by existing frozen build host. No native/SDK/compiler/asset production or package claim.','source_base':subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),'source_modules':sources,'host':{'path':str(host),'protocol':14,**host_before,'unchanged_after':True},'compiler':{'artifact':record(repo/'tools/compiler/bootstrap/compiler.chibip'),'bootstrap_manifest':record(repo/'tools/compiler/bootstrap/bootstrap.json'),'runtime_pin':record(repo/'tools/compiler/runtime.json'),'source_inputs':record(out/'compiler-inputs.json'),'unchanged_after':True},'run':{'argv':argv,'cwd':str(repo),'exit_code':result.returncode,'wall_seconds':elapsed,'log':record(out/'run.log')},'tests':{'authored_test_groups':5,'groups':['CanonicalManifest','CanonicalUnicode','CanonicalRejectsMalformed','CanonicalDepth','CanonicalFields'],'entry':'Build explicitly calls all five @test functions','differential_vectors':vectors['count'],'valid_vectors':vectors['valid'],'malformed_vectors':vectors['invalid'],'random_seed':vectors['seed'],'vector_manifest':record(out/'vectors.json'),'actual_outputs_sha256':hashlib.sha256(json.dumps({name:sha(out/'actual'/name) for name in sorted(vectors['cases'])},sort_keys=True,separators=(',',':')).encode()).hexdigest()},'contract':{'json_domain':'signed64 integer/string/bool/null/array/object','canonical':'Python json.dumps(sort_keys=True,separators=(comma,colon),ensure_ascii=True); -0 becomes 0','limits':{'text_bytes':16777216,'value_nesting':128},'rejects':['fraction/exponent numbers','signed64 integer overflow','duplicate decoded keys','invalid UTF8 including overlong encodings/surrogates/out-of-range/truncated sequences','unpaired JSON surrogate escapes','non-JSON whitespace and syntax','trailing bytes']},'remaining_scope':['Root owns SDK installer/evidence integration and dispatcher wiring.','No package consumer or native SDK construction validation is claimed.']}
(out/'validation.json').write_text(json.dumps(receipt,indent=2,sort_keys=True)+'\n')
print(json.dumps({'status':'pass','wall_seconds':elapsed,'vectors':vectors['count'],'receipt':str(out/'validation.json'),'receipt_sha256':sha(out/'validation.json')}))
