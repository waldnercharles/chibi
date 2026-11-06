import json, pathlib, hashlib, re, subprocess
P=pathlib.Path('/Users/cwaldner/Personal/chibi/.worktrees/reachability-collections-integration/chibi')
M=pathlib.Path('/Users/cwaldner/Personal/chibi/.worktrees/math-collections-integration/chibi')
def read(root, rel): return json.loads((root/rel).read_text())
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
pure=read(P,'.chibi/integration/artifact-proof/summary-60b28e57.json')
original=read(P,'.chibi/integration/artifact-proof/summary.json')
c3=read(P,'.chibi/integration/artifact-proof/summary-c3fbf7f4.json')
three=read(P,'.chibi/integration/artifact-proof/summary-3ca0b4a0.json')
writer=read(P,'.chibi/integration/artifact-proof/writer-proof.json')
inputs=read(P,'.chibi/integration/artifact-proof/inputs.json')
native=read(M,'.chibi/integration/native-math-diagnostic/summary.json')
wasm_names=['wasm-receipts.json','wasm-after-initializers.json','wasm-complete-initializers.json','wasm-row-array-packed.json','wasm-8b-fixture-controls.json']
receipts=[]
def cite(root,rel):
 f=root/rel; receipts.append({'path':str(f),'sha256':sha(f)})
 return str(f)
commands=[]
def command(c, phase, head=None, tree=None, label=None):
 row={k:c[k] for k in ['name','argv','command','cwd','environment','exit_code','wall_seconds','log','log_sha256','head','tracked_status','snapshot_revision','summaries'] if k in c}
 row.update(phase=phase)
 if head:row['head']=head
 if tree:row['tree']=tree
 if label:row['classification']=label
 commands.append(row)
for rel,phase in [('configure-language.json','configure'),('build-language.json','build'),('diagnose-recover.json','recovery'),('diagnose-writer.json','production writer')]:
 cite(P,'.chibi/integration/'+rel);command(read(P,'.chibi/integration/'+rel),phase)
for name in ['local_configure','local_build']:command(original[name],'configure' if 'configure' in name else 'build')
for name in ['configuration','build']:command(c3[name],'configure' if name=='configuration' else 'build')
# Pull original command receipts directly, avoiding mutable in-progress summaries.
for f in sorted((P/'.chibi/integration/artifact-proof/local-dirty-3ca-evidence').glob('*.json')):
 d=json.loads(f.read_text())
 if 'argv' in d:command(d,'configure' if f.name.startswith('configure') else 'build' if f.name.startswith('build') else 'test')
for c in pure['build_and_rechecks']:command(c,'build' if c['name'].startswith('build') else 'test')
unit=read(P,'.chibi/integration/compiler-bootstrap-current.json');command(unit,'controlled unit test')
for c in read(P,'.chibi/integration/tools-build-receipts.json'):command(c,'tools configure' if 'configure' in c['name'] else 'tools build')
command(read(P,'.chibi/integration/tools-driver-build-only.json'),'tools build')
for c in [native['configure'],native['build']]:command(c,'configure' if c is native['configure'] else 'build',head=native['head'],tree=native['tree'])
for name in wasm_names:
 d=read(M,'.chibi/integration/'+name);cite(M,'.chibi/integration/'+name)
 for c in d.get('commands',[d] if 'command' in d else []):
  command(c,'Wasm build' if c.get('name') in ['build','wasm-build'] else 'Wasm test',head=d['head'],tree=d.get('tree'))
for root,rel in [(P,'.chibi/integration/source-receipt.json'),(P,'.chibi/integration/artifact-proof/inputs.json'),(P,'.chibi/integration/artifact-proof/writer-proof.json'),(P,'.chibi/integration/artifact-proof/summary.json'),(P,'.chibi/integration/artifact-proof/summary-c3fbf7f4.json'),(P,'.chibi/integration/artifact-proof/summary-3ca0b4a0.json'),(P,'.chibi/integration/artifact-proof/summary-60b28e57.json'),(P,'.chibi/integration/artifact-proof/bootstrap-export-setup-failure.json'),(P,'.chibi/integration/artifact-proof/bootstrap-selector-export.json'),(P,'.chibi/integration/compiler-bootstrap-current.json'),(P,'.chibi/integration/tools-build-receipts.json'),(P,'.chibi/integration/tools-driver-build-only.json'),(P,'.chibi/integration/artifact-proof/local-snapshot-60b28e57.json'),(P,'.chibi/integration/artifact-proof/local-snapshot-60b28e57-files-sha256.json'),(M,'.chibi/integration/native-math-diagnostic/summary.json'),(M,'.chibi/integration/native-math-diagnostic/preflight.json'),(M,'.chibi/integration/native-math-diagnostic/snapshot.json'),(M,'.chibi/integration/native-math-diagnostic/snapshot-files-sha256.json'),(M,'.chibi/integration/native-math-diagnostic/captured-bootstrap-request.json'),(M,'.chibi/integration/native-math-diagnostic/fresh-native-target.json')]:cite(root,rel)
# Verify the immutable saved log hashes as part of evidence assembly; no producers run.
verified=[]
for c in commands:
 if 'log' not in c or 'log_sha256' not in c:continue
 f=pathlib.Path(c['log'])
 if not f.is_absolute():
  root=M if c['phase'].startswith('Wasm') else P
  f=root/f
 assert f.is_file(),str(f)
 actual=sha(f);assert actual==c['log_sha256'],str(f)
 verified.append(str(f))
heads={}
for label,h in [('artifact_origin','2a76e61a8bc21ee908f1306d607c8ddd93432b48'),('pure_bootstrap','3ca0b4a0170bf4d56576619396a43440b53077dd'),('pure_fixture_rechecks','60b28e5736d170e3635ad7e94b8ccddf4f334ed3'),('math_wasm_selected','5672369ffb48b8504020caa8f8d760d88f0d935b'),('math_fixture_rechecks_and_native_diagnosis','8b14194143d47ac0ec089e2bb3f034e541d1955e')]:
 heads[label]={'head':h,'tree':subprocess.check_output(['git','rev-parse',h+'^{tree}'],cwd=P,text=True).strip()}
record={
 'status':'DRAFT ONLY; scratch evidence assembly, not publication or final acceptance',
 'event_date':'2026-10-28','prepared_format':26,'bootstrap_version':7,'bootstrap_minimum':6,
 'heads':heads,
 'checked_artifact':{'bytes':20780474,'sha256':inputs['checked_bootstrap_closure_sha256'],'manifest_sha256':inputs['checked_bootstrap_manifest_sha256'],'revision':'f2a71eae9611009c8a9a237b87f867739c7a5f82','unchanged_in_authoritative_sources':True},
 'task_artifact':{'origin_head':heads['artifact_origin']['head'],'revision':writer['revision'],'closure':writer['closure'],'manifest_sha256':writer['manifest_sha256'],'target_sha256':writer['target_sha256'],'captured_request_raw_sha256':inputs['captured_request_sha256'],'original_captured_request_copied_byte_for_byte':True,'source_count':27,'debug':False,'project_root':'/project','core_collection_root':'/sdk/core','full_target_equality_verified':True,'recovery_and_writer_unmodified':True,'publication':False},
 'pure_snapshot':{'revision':pure['snapshot_actual_revision'],'tracked_files_compared':3460,'only_modified_tracked_files':pure['only_dirty_outputs'],'all_other_tracked_hashes_exact':pure['all3460trackedhashes_unchanged_after_build_and_tests'],'compiler_27_texts_and_full_target_unchanged':pure['compiler_27texts_and_target_unchanged'],'final_driver_sha256':pure['final_driver_sha256'],'cleanup':pure['cleanup_deferred']},
 'math_snapshot':{'revision':native['actual_snapshot_revision'],'tracked_files_compared':3467,'all_other_tracked_hashes_exact':native['all3467trackedhashes_still_match_prebuild_receipt'],'only_modified_tracked_files':pure['only_dirty_outputs'],'recorded27path_order_texts_roots_debug_fulltarget_match':native['recorded27path_order_texts_roots_debug_fulltarget_match'],'fresh_native_target_matches_artifact':native['fresh_native_target_matches_pure_artifact'],'host_binary_sha256':native['host_binary_sha256'],'actual_source_count':33,'additional_paths':[r['path'] for r in native['captured33source_rows'] if not r['recorded']],'driver_linked':False,'runtime_tests_run':[],'benchmark_counters':None,'unrun_selectors':native['unrun_existing_selectors'],'build_cancelled_after_confirmed_reuse_miss':True},
 'backend_scope':{'native_pure_import_compiler_modes':['BYTECODE','SLJIT'],'native_pure_sample_execution':['BYTECODE','SLJIT'],'native_pure_bootstrap_stages':['SLJIT','SLJIT'],'native_pure_contains_math_samples':False,'wasm_math_authored_execution':['BYTECODE','WASM'],'math_authored_tests_per_backend':29,'wasm_bytecode_compiler_math_sample_execution':['BYTECODE','WASM'],'wasm_compiled_compiler_import_lane':'FAILED preparation; no successful proof','native_math':'UNRUN'},
 'preserved_reds':[
  {'check':'pure pristine build2a','wall_seconds':264.78620495810173,'cause':'normal bootstrap mismatch/fallback reaches unchanged240000ms guard'},
  {'check':'pure dirty build2a','wall_seconds':200.99245383404195,'cause':'four strict pin_string.owner initializer warnings; asset generation reuse passed'},
  {'check':'pure dirty buildc3','wall_seconds':14.16139766597189,'cause':'further strict pin_string.owner initializer warnings'},
  {'check':'tools build target list2a','wall_seconds':30.448737707920372,'cause':'test_tools built; minimal SDK configuration has no chibi-compile target'},
  {'check':'pure imports3ca','wall_seconds':2.158035208005458,'result':'17pass/1fail/1473asserts','cause':'fixture caller lacked managed heap;18/1530green60b'},
  {'check':'pure snapshots3ca','wall_seconds':0.37620366690680385,'result':'2pass/14fail/48769asserts','cause':'fixture rejected canonicalNULL empty text;16/286093green60b'},
  {'check':'pure bootstrap first invocation3ca','wall_seconds':570.3164802079555,'cause':'agent omitted optional export directory; seeded assertion; later checks unrun in first attempt','correction':'create task-owned directory; same selector/limits580.830587833887s1/31262pass'},
  {'check':'Wasm buildea596','wall_seconds':52.06272145919502,'cause':'four strict pin_string.owner initializer warnings'},
  {'check':'Wasm build519c','wall_seconds':29.42077491688542,'cause':'further strict pin_string.owner initializer warnings'},
  {'check':'Wasm manual imports567','wall_seconds':0.4608474588021636,'result':'4pass/14fail/925asserts','cause':'non-Wasm-admitted selector hardcodesSLJIT'},
  {'check':'Wasm package-core567','wall_seconds':47.10379516589455,'result':'5pass/1fail/12770asserts','cause':'stage-one compiledWASM compiler emission limit'},
  {'check':'Wasm --packed567','wall_seconds':0.6647264170460403,'cause':'unsupported flag; no suite executed'},
  {'check':'Wasm compiled compiler imports567','wall_seconds':22.858516624895856,'result':'0pass/2fail/15asserts','cause':'compiledWASM compiler emission limit during preparation'},
  {'check':'native Math build8b','wall_seconds':79.74326829100028,'cause':'strict exact reuse3 for33vs27inventory; task cancellationexit-15, not timeout/runtime failure'}
 ],
 'boundary':{'accepted_796':'3e2b28dc19fb21961ae06fe99aeb5a8dede35bf0','accepted_protocol':13,'accepted_prepared_format':26,'accepted_796_is_ancestor_of_evidence_heads':False,'separate_common18_fixture_packet':['d0d5d87af3ce5faa2984dcf181c60b0ecb0637c3','f2a3af817456720f4a50c174f5e7450dbb1e6740'],'packet_base':'3e2b28dc19fb21961ae06fe99aeb5a8dede35bf0','not_validated_by_frozen_runs':True},
 'pending':['#795 library/Generate/SDK ownership and first feature landing','#694 EmitBody extraction owned separately','Math SDK publication hold','coordinator-ordered final normal Graph build and reviewed matching artifact gates','native Math runtime/native-boundary/benchmark selectors','native project_compile published-docs CLI driver control','full installed SDK and applicable platform gates; no Windows or release claim'],
 'excluded':['pre-694 prepared25 timing/proof transfer','wholeWasmsuitegreen','nativeMathprooftransferfrompurecontrols'],
 'commands':commands,'receipts':receipts,'verified_saved_log_hashes':len(set(verified))
}
out=M/'.chibi/integration/integration-results.json'
out.write_text(json.dumps(record,indent=2)+'\n')
print(json.dumps({'json':str(out),'sha256':sha(out),'commands':len(commands),'receipt_count':len(receipts),'verified_distinct_logs':len(set(verified))}))
