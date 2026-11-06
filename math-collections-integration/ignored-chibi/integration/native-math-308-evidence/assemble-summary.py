from pathlib import Path
import json,hashlib,subprocess,re,statistics,collections,os
E=Path(__file__).resolve().parent;R=E.parents[2];S=E.parent/'native-math-308-snapshot';B=S/'.chibi/integration/native-math-308-language'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
pre=json.loads((E/'preflight.json').read_text());ids=json.loads((E/'build-identity.json').read_text());files=json.loads((E/'tracked-files-sha256.json').read_text())
assert R==Path('/Users/cwaldner/Personal/chibi/.worktrees/math-collections-integration/chibi')
for f in files:
 assert sha(R/f['path'])==f['authoritative_sha256'],f['path']
 assert sha(S/f['path'])==f['snapshot_sha256'],f['path']
assert subprocess.check_output(['git','status','--short','--untracked-files=no'],cwd=R,text=True)==''
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=R,text=True).strip()==pre['head']
assert sha(Path(ids['driver']))==ids['driver_sha256']
names=['configure','build-test-language','package-math','compiler-imports-bytecode','compiler-imports-sljit','package-imports','package-snapshot-chibi','record-publication','native-calls','package-math-benchmark'];commands=[json.loads((E/(n+'.json')).read_text()) for n in names];tests=[]
for c in commands:
 assert c['exit_code']==0 and sha(Path(c['log']))==c['log_sha256']
 if c['name'] in names[2:]:
  text=re.sub(r'\x1b\[[0-9;]*m','',Path(c['log']).read_text());m=re.search(r'Summary: Passed: (\d+) Failed: (\d+) Total: (\d+) Suites: (\d+) Asserts: (\d+)',text);assert m
  tests.append({'name':c['name'],'passed':int(m[1]),'failed':int(m[2]),'cases':int(m[3]),'assertions':int(m[5]),'wall_seconds':c['wall_seconds']})
bench=(E/'package-math-benchmark.log').read_text();trials=[];prepare=[]
for line in bench.splitlines():
 if 'MATH_PREPARE ' in line:
  fields=dict(x.split('=',1) for x in line.split('MATH_PREPARE ',1)[1].split());prepare.append({'backend':fields['backend'],'milliseconds':float(fields['milliseconds']),'source_files':int(fields['source_files']),'invocation_includes':fields['invocation_includes'],'allocation_scope':fields['allocation_scope']})
 if line.startswith('MATH_BENCH '):
  fields=dict(x.split('=',1) for x in line.split()[1:]);row={}
  for k,v in fields.items():row[k]=v if k in ['backend','body'] else float(v) if k in ['ns_per_step','us_per_invocation'] else int(v)
  assert row['loops']==128 and row['invocations']==16 and row['trial'] in [0,1,2];trials.append(row)
assert len(trials)==24 and {x['backend'] for x in trials}=={'BYTECODE','SLJIT'}
groups=collections.defaultdict(list)
for t in trials:groups[t['backend'],t['body']].append(t)
medians=[]
for (backend,body),group in groups.items():
 assert len(group)==3
 r={'backend':backend,'body':body,'median_ns_per_step':statistics.median(t['ns_per_step'] for t in group),'median_us_per_invocation':statistics.median(t['us_per_invocation'] for t in group)}
 for k in ['xalloc_calls','requested_bytes','arena_blocks','arena_used','arena_capacity','activation_bytes','host_bytes','managed_bytes']:
  values=[t[k] for t in group];assert len(set(values))==1;r[k]=values[0]
 medians.append(r)
mathlog=(E/'package-math.log').read_text();authored=[]
for backend,count,ms in re.findall(r'MATH_TESTS backend=(\w+) tests=(\d+) prepare_ms=([\d.]+)',mathlog):authored.append({'backend':backend,'tests':int(count),'prepare_ms':float(ms)})
assert [(x['backend'],x['tests']) for x in authored]==[('BYTECODE',29),('SLJIT',29)]
summary={'purpose':'fresh accepted308 runtime focused native Math validation; only optional asset-host block omitted; no ordinary full/installed SDK acceptance','event_date':'2026-10-28','head':pre['head'],'tree':pre['tree'],'snapshot_revision':pre['snapshot_revision'],'prepared_format':26,'authoritative_tracked_clean_after':True,'only_changed_tracked_paths':pre['only_changed_tracked_paths'],'helper_diff_sha256':pre['helper_diff_sha256'],'tracked_files_compared':len(files),'all_postrun_tracked_source_hashes_match_preflight':True,'no_bootstrap_artifact_substitution_or_regeneration':True,'checked_bootstrap_sha256':pre['checked_bootstrap'],'no_production_or_sibling_source_copies':True,'fresh_build':{'configure_seconds':commands[0]['wall_seconds'],'build_seconds':commands[1]['wall_seconds'],'prebuild_objects':0,'actual_objects':ids['actual_object_count'],'actual_compile_log_lines':ids['actual_compile_log_lines'],'actual_compile_invocations':ids['actual_compile_invocations'],'all_objects_new_mtime':ids['all_objects_new_mtime'],'driver_sha256':ids['driver_sha256'],'selected_objects':ids['selected_objects'],'asset_generation_or_complete_host_executables':False},'configuration':{'compiler':subprocess.check_output(['/opt/homebrew/opt/llvm@22/bin/clang','--version'],text=True).splitlines(),'platform':subprocess.check_output(['uname','-sm'],text=True).strip(),'build_type':'Release','sdk_compiler':True,'sdk_native':False,'sdk_desktop':False,'jobs':4,'selected_flags':'-O3 -DNDEBUG -std=gnu23 -arch arm64 -Wall -Wextra -Werror -UNDEBUG','sanitizers':False,'cmake_cache_sha256':sha(B/'CMakeCache.txt'),'compile_commands_sha256':sha(B/'compile_commands.json')},'authored_tests':authored,'tests':tests,'total_passed_cases_including_benchmark':sum(x['passed'] for x in tests),'total_assertions_including_benchmark':sum(x['assertions'] for x in tests),'backend_scope':{'package_math':'BYTECODE and SLJIT; checks require positive matching backend entries, no bytecode entries on compiled Math calls and no inspection fallback','compiler_imports':'compiler modes BYTECODE and SLJIT; both emitted runtime modes BYTECODE and SLJIT; nine reachability samples plus two Math samples included','emitted_math_calls':'run_math asserts positive SLJIT entries, zero bytecode entries on compiled calls, zero inspection fallback','compiler_fixture_exception':'existing compiler_fixture permits bytecode entries equal to size_bytecode_fallbacks; numeric fallback counts not printed, so blanket zero compiler-size-fallback not claimed','native_calls':'BYTECODE and SLJIT plus deliberately inspected VM path; existing result and native binding checks passed','benchmark':'reports BYTECODE/SLJIT prepared bodies and runs them with matching execution selection; benchmark measure does not print backend entry counters'},'benchmark':{'opt_in':'existing --package-math-benchmark selector; no enabling env flag','environment_overrides':{},'loops':128,'invocations_per_trial':16,'trials_per_backend_body':3,'warmup':'one invocation before the timed trials for each body','preparation':prepare,'scope':'each timed invocation includes reusable storage reset and binding; xalloc wrapper calls/requested bytes and arena blocks measured; not all process allocations','footprints':'arena/activation/host/managed bytes sampled after timed trial, not added together as a total memory claim','trials':trials,'median_by_backend_body':medians,'allocation_counters_are_observations_not_approved_budget_or_expected_baseline':True,'unverified_allocation_cause':True},'original_red_preserved':str(R/'.chibi/integration/native-math-diagnostic/summary.json'),'original_red_classification':'8b normal asset build had33inputs vs27task-artifact, exactreuse3, taskcancelled79.743s; this focused route does not repair it','pending':['Math PR791/SDK publication hold after795','coordinator-ordered final normal Graph build/reviewed matching artifact','ordinary complete/installed SDK and applicable platform gates','compiled-WASM compiler preparation red remains from567 evidence'],'commands':commands,'red_results_this_route':[],'scratch_retained_at_coordinator_request':[str(S),str(B),str(E)],'selected_source_sha256':pre['selected_files_unchanged']}
summary['accepted_runtime']=pre['accepted_runtime']
summary['fresh_build']['compiler_target']=ids['compiler_target']
summary['historical699_matched_math_and_benchmark_sources']=pre['historical699_matched_math_and_benchmark_sources']
summary['pre308_proofs_are_historical_only']=True
summary['fresh_build']['dynamic_target_oracles']='existing compiler fixture builds target arguments from the fresh native layout/catalog; both compiler modes and emitted Math target-entry/no-inspection-fallback oracles passed'
summary['backend_scope']['package_imports']='BYTECODE and SLJIT; existing exclusive-backend positive-entry/no-inspection-fallback assertions passed'
summary['backend_scope']['package_snapshot_chibi']='BYTECODE and SLJIT authored Build via existing compiler_fixture'
summary['backend_scope']['record_publication']='BYTECODE and SLJIT preparation/publication with accepted owned-default lifetime oracle; no authored execution claimed'
historical_path=R/'.chibi/integration/native-math-focused-evidence/summary.json'
historical=json.loads(historical_path.read_text())
assert historical['head']=='6990373667fc211faf828035ab9d4bb051f085bd'
for k in ['loops','invocations_per_trial','trials_per_backend_body']:assert historical['benchmark'][k]==summary['benchmark'][k]
old={(x['backend'],x['body']):x for x in historical['benchmark']['median_by_backend_body']}
comparison=[]
for row in medians:
 prior=old[row['backend'],row['body']]
 comparison.append({'backend':row['backend'],'body':row['body'],'historical699':prior,'fresh_b87':row,'differences':{k:row[k]-prior[k] for k in ['median_ns_per_step','xalloc_calls','requested_bytes','arena_blocks','activation_bytes','host_bytes','managed_bytes']}})
summary['historical_benchmark_comparison']={'prior_head':historical['head'],'prior_summary_sha256':sha(historical_path),'same_math_and_benchmark_code_verified':True,'same128_16_3_settings_verified':True,'comparison':comparison,'scope':'historical699 only; observed whole-runtime difference, no cause assigned to an individual797 change; not an approved budget or expected baseline'}
summary['pending']=['Math PR791/SDK publication hold after795','coordinator-owned full normal Graph/reviewed matching artifact/installed SDK gates','new-runtime full bootstrap/self-build/asset producer not run here','pre308 Wasm/compiler proofs historical only; no fresh new-runtime Wasm claim']
summary['original_red_classification']='pre308 historical8b normal asset route had33vs27inputs and directed cancellation; preserved without reclassifying it as a fresh new-runtime failure'
(E/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
(E/'benchmark-counters.json').write_text(json.dumps(summary['benchmark'],indent=2)+'\n')
print(json.dumps({'summary':str(E/'summary.json'),'sha256':sha(E/'summary.json'),'total_cases':summary['total_passed_cases_including_benchmark'],'assertions':summary['total_assertions_including_benchmark'],'bench_rows':len(trials),'median_rows':medians}))
