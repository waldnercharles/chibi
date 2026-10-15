from pathlib import Path
import csv,json,hashlib,collections,sys
root=Path(__file__).parent
# This is a frozen baseline generator, not a progress-tracking database writer.
# Refuse before any output write when a maintainer has recorded implementation work.
coverage_path=root/'studio-path-coverage.csv'
if coverage_path.exists():
 with coverage_path.open(newline='') as existing:
  reader=csv.DictReader(existing)
  required={'tracking_status','implementation_pr','exact_head_evidence'}
  if not required.issubset(reader.fieldnames or []):
   raise SystemExit('Refusing regeneration: existing coverage tracking columns are missing.')
  for row in reader:
   if (row['tracking_status'] != 'baseline_only_not_implemented'
       or row['implementation_pr'].strip() or row['exact_head_evidence'].strip()):
    raise SystemExit('Refusing regeneration: existing coverage contains recorded progress for '
                     + row.get('path','<unknown path>')
                     + '. Preserve that progress; use a status-preserving update workflow.')
packages=list(csv.DictReader((root/'packages.tsv').open(),delimiter='\t'))
# The Studio cutover depends on features actually consumed, not unrelated storage/lambda refinements.
for p in packages:
 if p['id']=='R397': p['dependencies']='RSCHEMA'
 if p['id']=='R397': p['scope']='PR #397 merged as 7885af0cd5a7e1c8600a6bd9e3db0fd8d88c1cf4 after fresh exact-head review/tests on 6524d3fc090e27943e4bc3942e6b528ba311ae6c. Checked query-load foundation complete; no full Studio/browser readiness claim.'
 if p['id']=='R398': p['dependencies']='R397,RSCHEMA'
 if p['id']=='R398': p['scope']='PR #398 merged as b1ada743d978a25ff8b0d5d677b491291eb4ddf5 after fresh exact-head review/tests on cdf9adc540f1c1c0c9c8468820a1bf5b706629e4. Explicit checked scalar getter complete; ordinary member reads retain debug-snapshot fallback.'
 if p['id']=='K08': p['dependencies']=','.join(d for d in p['dependencies'].split(',') if d not in {'K04','K07'})
 if p['id']=='P38': p['dependencies']=','.join(d for d in p['dependencies'].split(',') if d not in {'L11','L21','L22'})
 if p['id']=='B06':
  p['classification']='completed_audit'
  p['dependencies']=''
  p['scope']='Read-only native-reload source/build audit completed at engine 02f84532 (unchanged relevant paths in 1de4810d) and Studio 78cb4d4c. Native application replacement is already removed; see native-reload-audit.md. Retain retired-option cleanup, generated-C dlopen test and authored publication/checkpoint mechanisms.'
  p['acceptance']='Exact source/build/host references and surviving dynamic-loader uses inspected; no native deletion needed at these snapshots. Reopen only on evidence of a live path, preserving .chibi state/callback/generation safety and workspace recovery. No runtime test claimed.'
 if p['id']=='R399': p['scope']='PR #399 merged as 1de4810d49b3acf4ead7d910d0abd2b042293e18, verified by coordinating worker. Its safety fix is complete; broader Format policy/integration remain separate packages.'
 if p['id']=='RCAP': p['scope']='PR #400 merged as ddc71942aecab39e7b4dd46ad4f8e1ed58f6d1ec, verified by coordinating worker after fresh independent validation. Capture-retention fix is complete; do not reimplement.'
 if p['id']=='RSCHEMA': p['scope']='PR #401 merged as 23cad1c6c58c9e1b3c486ae6ddee1b6291e81deb after exact-head review/tests verified by the coordinator. Schema-safety foundation is complete; dependent query/read/write foundations subsequently landed with their own fresh exact-head integration evidence. Manual payload/semantic version obligations remain.'
 if p['id']=='RWRITE': p['scope']='PR #408 merged as 3c65bb60b6f3443aef6ccf4ab15acbe09b04b191 after independent exact-head review/tests on 982e736e4f2d5922c2308f0b6017dcfd30dbd869. Checked scalar publication and its ownership/order/reacquisition safeguards are complete; no raw-store substitution or broad managed-write claim.'
 if p['id']=='RFMT': p['scope']='Shared formatter SDK foundation PR #402 merged as 64183a1c4ba5b367bf35604ddae45fa616fd3bd0 after independent checks of exact head 3d7a0fed73e0c5630bc0fb71ada4248f8d82d74b, verified by the coordinator. Output is LF only; configurable line endings are not implemented. This foundation does not complete F00 comparison or full F01/F02 caller/dialect integration.'
 if p['id']=='RFMT': p['acceptance']='CST/comments/determinism and supported width/indent configuration verified for the merged SDK slice; output remains LF only. Future line-ending configuration remains F01 scope, and corpus/layout-engine choice and remaining caller integration stay separate.'
 if p['id'] in {'L01','L21'}: p['scope']=p['scope'].replace('Approved ', 'After approval, implement ', 1)
 if p['id']=='L22': p['scope']=p['scope'].replace('Selected explicit layout family;', 'Once selected, implement the explicit layout family;', 1)
 if p['id']=='L10': p['decisions']='Inferred shared mutable bindings selected; optional fn, arrow bodies and inferred results requested; exact grammar/canonicalization and lifetime admission pending; proper cycle reclamation required in a separate PR, mechanism unselected'
 if p['id']=='L10': p['acceptance']='Optional-fn block and arrow forms preserve capture/result semantics; inference keeps explicit recursive fallback and bodyless callable types distinct. Grammar boundaries and checked canonical keyword removal preserve comments, binding/effect identity and source maps. Outer and sibling closures share one binding across stack/owned placement and copies; ordinary value copies remain unchanged. No dangling escape or unsupported suspension; owner/service effects and capture costs inspectable. Proper cycle reclamation remains required in a separate PR; temporary rejection cannot close it. Zero allocation claims need exact hot-path evidence.'
 if p['id']=='L10': p['scope']='Owner selected inferred shared mutable captures and requested optional fn, arrow forms and inferred results. Resolve exact grammar/canonicalization and lifetime admission before parser/runtime work; recommend fn-free semantic canonicalization, distinct callable types and no implicit block-to-arrow rewriting. Checked synchronous authored-to-authored callbacks first with complete owner/service effects. Proper automatic capture-cycle reclamation remains a separate required PR; interim rejection is temporary.'
 if p['id'] in {'F03','F04','F05'}: p['decisions']='Exact state/self/meaning-preserving Format approval in decisions.md; full library/shared/callable self table and named-fn rejection/default policy not selected'
 if p['id'] in {'L21','L22'}: p['decisions']='D9 array-like spelling and possible storage inference under investigation; ownership unresolved'
ids={p['id'] for p in packages}; assert len(ids)==len(packages)
for p in packages:
 assert None not in p,(p['id'],'malformed TSV')
 for dep in filter(None,p['dependencies'].split(',')): assert dep in ids,(p['id'],dep)
with (root/'packages.tsv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=list(packages[0]),delimiter='\t',lineterminator='\n');w.writeheader();w.writerows(packages)
# Explicit source ownership by exact basename; shared packages are additional responsibilities, not duplicate authority.
groups={
 'P00':'studio_registry studio_node_calls studio_sources',
 'P01':'studio_outliner scene_hierarchy',
 'P02':'scene_history',
 'P05':'kit kit_controls kit_drag kit_list kit_motion kit_number kit_popup kit_slot kit_text kit_toast',
 'P06':'studio_member_properties studio_members',
 'P07':'studio_output',
 'P08':'studio_keymap studio_keys studio_prefs studio_settings',
 'P09':'studio_commands studio_panels studio_shell',
 'P10':'kit_command studio_palette studio_palette_view choices labels',
 'P11':'frame_log studio_performance',
 'P12':'commands document mutations scene_document scene_documents scene_retention editor',
 'P13':'scene_disk studio_disk scene_tabs tree_watch',
 'P14':'asset_code ui_buffer ui_code preview',
 'P15':'scene_checkpoint',
 'P16':'asset_index studio_assets studio_gallery',
 'P17':'asset_refactor asset_refactor_source studio_drop',
 'P18':'component_properties studio_properties studio_properties_view',
 'P19':'kit_code studio_code highlight',
 'P20':'navigation studio_calls studio_definition studio_find studio_refactor',
 'P21':'studio_debug scene_debug',
 'P22':'flow input_bindings pins search selection studio_graph_comment studio_graph_edit studio_graph_literals studio_graph_properties studio_graph_values',
 'P23':'kit_wires kit_wire_shape studio_graph_input studio_graph_menu studio_graph_nav studio_graph_pins studio_graph_view studio_graph_find',
 'P24':'eval_view flow_visual presentation studio_graph studio_graph_details studio_graph_draw studio_graph_header studio_graph_layout studio_graph_live',
 'P25':'ui_design studio_canvas studio_designer studio_designer_details studio_designer_edit studio_designer_hierarchy studio_designer_library studio_designer_values',
 'P26':'kit_inspect studio_inspect',
 'P27':'scene_prototype_actions scene_prototypes studio_prototype_actions scene_actors scene_asset_drop scene_authoring',
 'P28':'studio_pick studio_scene studio_scene_camera editor_camera input scene_overlays scene_view scene_view3d scene_viewport viewport',
 'P29':'scene_collider3d scene_gizmo scene_gizmo3d scene_gizmo3d_targets scene_gizmo3d_view',
 'P30':'game_presentation gameplay_input scenario_playback scene_runtime scene_world_preview',
 'P31':'application studio_host studio_ui main',
 'P32':'studio_model model_preview',
 'P33':'studio_thumbnail_cache studio_thumbnail_graph studio_thumbnail_kinds studio_thumbnails',
 'P34':'studio_projects studio_export',
 'P35':'studio_collab actions actions_gate actions_graph actions_scene collab protocol workspace',
 'P37':'web_trace zep_bridge stacktrace state_file',
}
source_owners={}
for owner,names in groups.items():
 for n in names.split():
  assert n not in source_owners,n
  source_owners[n]=owner
header_extra={
 'include/chibi/application.h':'P31','src/diagnostics/tracy.h':'P37',
 'src/editor/collab/actions_internal.h':'P35','src/editor/rmlui/kit_internal.h':'P05',
 'src/editor/studio/studio.h':'P00','src/editor/studio/studio_designer_internal.h':'P25',
 'src/editor/studio/studio_graph_internal.h':'P22','src/editor/studio/studio_internal.h':'P00',
 'src/editor/studio/studio_members_internal.h':'P06','src/editor/studio/studio_nodes.h':'P00',
 'src/editor/studio/studio_properties_internal.h':'P18','src/editor/ui/kit_icons.h':'P10',
 'src/editor/workspace/asset_refactor_internal.h':'P17','src/editor/workspace/checkpoint.h':'P15',
 'src/editor/workspace/editor_internal.h':'P12','src/editor/workspace/movement_input.h':'P28',
 'src/editor/workspace/scene_gizmo3d_internal.h':'P29','src/editor/workspace/scene_internal.h':'P27',
 'src/host/vendor/debugbreak.h':'P37',
}
additional={
 'studio_registry':'K00;P31;P38;B06','studio_node_calls':'P01;P03;P27;P38',
 'studio_sources':'P03;P06;P07;P08;P09;P10;P11;P16;P18;P19;P21;P24;P25;P26;P28;P32;P33;P35;P38',
 'scene_history':'P12;P27','scene_hierarchy':'P27','studio_outliner':'P02;P03;P04',
 'kit':'K02;P03','studio_ui':'K02;P00','studio_host':'K06;P08;P28',
 'scene_authoring':'P01;P12','application':'P15;P34;P35;B06','state_file':'K03',
 'zep_bridge':'K06;P19','web_trace':'B04;P11','frame_log':'B04',
 'studio_commands':'P00;P01;P27','studio_prefs':'P31',
 'navigation':'P12','preview':'P30','scene_checkpoint':'P35','workspace':'P15',
}
rows=list(csv.DictReader((root/'source/studio-file-inventory.csv').open()))
assert len(rows)==867 and len({r['path'] for r in rows})==867
out=[]
for r in rows:
 p=r['path'];cat=r['category'];stem=Path(p).stem
 feature='';note='Baseline path mapping; refresh against actual implementation tree.'
 if cat=='PRODUCT_NATIVE':
  assert stem in source_owners,p
  primary=source_owners[stem];feature=additional.get(stem,'')
  note='Function-level body audit is required before implementation; original evidence/disposition is provisional. One owner per actual function.'
 elif cat=='PRODUCT_HEADER':
  primary=header_extra.get(p,source_owners.get(stem))
  assert primary,p
  feature=additional.get(stem,'');note='Retire with consumers or replace by narrow authored/public SDK contract; do not recreate a global script header.'
 elif cat=='BUILD_TOOLING':
  primary='P38';feature='K09;B00;B01;B06';note='Preserve packaging/toolchain role and pins; update exact native allowlist, public SDK boundary, authored assets and provenance. Do not run quarantined coverage.'
 elif cat=='PROVENANCE': primary='P38';feature='B01;K09';note='Update extraction/SDK ownership records; prevent source-sync tools from restoring removed C policy.'
 elif cat=='BROWSER_HOST': primary='P36';feature='K03;K06;K07;P31;P34;P35';note='Body-audit browser-only mechanism versus application policy; retain narrow browser ABI only.'
 elif cat in {'TEST_ORACLE','ACCEPTANCE_TEST'}:
  primary='B02';feature='B03'
  # Relevant feature affinity supplements explicit admission ownership. Historical tests are never marked passing.
  keywords=[('outliner','P04'),('gizmo','P29'),('collider','P29'),('camera','P28'),('scene_view','P28'),('movement','L24'),('collab','P35'),('checkpoint','P15'),('scene_disk','P13'),('scene_history','P02'),('refactor','P17'),('designer','P25'),('inspector','P26'),('debug','P21'),('graph','P22'),('flow','P22'),('code','P19'),('members','P06'),('output','P07'),('palette','P10'),('settings','P08'),('thumbnail','P33'),('model','P32'),('asset','P16'),('shell','P09'),('chrome','P09'),('panel_idle','B04'),('bench','B04'),('documents','P12'),('scene','P27'),('runtime','K05'),('kit','P05'),('labels','P10')]
  affinity=next((owner for k,owner in keywords if k in p),'')
  if affinity:feature+=';'+affinity
  note='Coverage source/fixture only; no current admission, runnable target or passing result inferred. Preserve assertions/provenance, adapt only through admitted C/pico_unit selectors.'
 elif cat=='AUTHORED_STUDIO':
  primary='B01';feature='F02;P38'
  if p.endswith('.chibi'):
   feature='F01;'+('P03;P04' if 'Outliner' in p else 'P34' if 'NewProject' in p else 'P05')
  note='Preserve frozen RML/RCSS/assets/metadata and source identity; feature ownership refines from actual mount/consumer. Ten existing behaviors are partial migration, not complete app policy.'
 else:
  primary='P38';feature='B01';note='Preserve supporting resource/documentation role; not counted as migrated native product behavior.'
 for x in [primary,*filter(None,feature.split(';'))]:assert x in ids,(p,x)
 r.update(primary_package=primary,related_packages=feature,tracking_status='baseline_only_not_implemented',implementation_pr='',exact_head_evidence='',coverage_note=note)
 out.append(r)
with (root/'studio-path-coverage.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=list(out[0]),lineterminator='\n');w.writeheader();w.writerows(out)
# Topological coverage; all feature assertions and gate checks are documentary checks, not product validation.
edges={p['id']:list(filter(None,p['dependencies'].split(','))) for p in packages};seen=set();active=set();order=[]
def visit(i):
 assert i not in active,('cycle',i)
 if i in seen:return
 active.add(i)
 for d in edges[i]:visit(d)
 active.remove(i);seen.add(i);order.append(i)
for p in packages:visit(p['id'])
refs={}
for line in (root/'source/plan.md').read_text().splitlines():
 if line.startswith('[') and ']: ' in line:
  key,url=line.split(']: ',1);refs[key[1:]]=url
lines=['# Dependency-ordered PR checklist','', 'Only coordinator-verified merged R397/R398/R399/RCAP/RWRITE/RSCHEMA/RFMT foundations and the bounded read-only B06 audit are checked. F00/F01/F02 remain open. Other existing-owned work is reserved to its coordinator; current PR/head/evidence status must be rechecked. Dependencies include preparation and readiness gates, not just code imports. Refer to README.md for universal G0–G7 gates, decisions and policy.','']
byid={p['id']:p for p in packages}
for i in order:
 p=byid[i]
 lines += [f"## [{'x' if i in {'R397','R398','R399','RCAP','RWRITE','RSCHEMA','RFMT','B06'} else ' '}] {i}: {p['name']}", '',f"Stage: {p['stage']} · Repository: {p['repository']} · Scope: {p['classification']}",f"Dependencies: {p['dependencies'] or 'none (preparation can start)'}",f"Decision gate: {p['decisions'] or 'none beyond existing contracts and applicable admission'}",'',p['scope'],'',f"Acceptance: {p['acceptance']}",'',f"Plan evidence: {p['plan_refs']}", ('PR #397 / merged integration head 7885af0cd5a7e1c8600a6bd9e3db0fd8d88c1cf4 / exact tested head 6524d3fc090e27943e4bc3942e6b528ba311ae6c; consult PR evidence.' if i == 'R397' else 'PR #398 / merged integration head b1ada743d978a25ff8b0d5d677b491291eb4ddf5 / exact tested head cdf9adc540f1c1c0c9c8468820a1bf5b706629e4; consult PR evidence.' if i == 'R398' else 'PR #408 / merged integration head 3c65bb60b6f3443aef6ccf4ab15acbe09b04b191 / exact tested head 982e736e4f2d5922c2308f0b6017dcfd30dbd869; consult PR evidence.' if i == 'RWRITE' else 'PR #399 / merged integration head 1de4810d49b3acf4ead7d910d0abd2b042293e18 / merge verified by coordinator; consult PR evidence for exact validation.' if i == 'R399' else 'PR #400 / merged integration head ddc71942aecab39e7b4dd46ad4f8e1ed58f6d1ec / merge and fresh independent validation verified by coordinator; consult PR evidence.' if i == 'RCAP' else 'PR #401 / merged integration head 23cad1c6c58c9e1b3c486ae6ddee1b6291e81deb / exact-head independent review/tests verified by coordinator; consult PR evidence.' if i == 'RSCHEMA' else 'PR #402 / merged integration head 64183a1c4ba5b367bf35604ddae45fa616fd3bd0 / independently tested head 3d7a0fed73e0c5630bc0fb71ada4248f8d82d74b / LF-only foundation; F00/F01/F02 remain open.' if i == 'RFMT' else 'Read-only audit complete; no removal PR needed. Exact snapshot, source links and limits in native-reload-audit.md; no product runtime test claimed.' if i == 'B06' else 'PR / exact head / review / tests / remaining blocker: **unrecorded**'),'']
lines+=['## Immutable source reference index','']
for k,u in sorted(refs.items()):lines.append(f'- [{k}]({u})')
(root/'roadmap.md').write_text('\n'.join(lines)+'\n')
cat=collections.Counter(r['category'] for r in rows)
prod=[r for r in rows if r['category']=='PRODUCT_NATIVE'];heads=[r for r in rows if r['category']=='PRODUCT_HEADER']
native_tests=[r for r in rows if r['category'] in {'TEST_ORACLE','ACCEPTANCE_TEST'} and Path(r['path']).suffix in {'.c','.cpp','.h'}]
assert len(prod)==155 and len(heads)==62 and len(native_tests)==99
assert collections.Counter(r['disposition'] for r in prod)=={'MIGRATE':122,'SPLIT':29,'NATIVE_EXCEPTION':4}
validation={
 'kind':'documentation and dependency validation; no product builds/tests/benchmarks',
 'baseline_engine':'02f8453211dd4cc57965ba52c645f08a92e53c5e','baseline_studio':'78cb4d4c47242db9f7af2b9573653d9756050e99',
 'package_count':len(packages),'package_classes':dict(collections.Counter(p['classification'] for p in packages)),
 'unique_package_ids':True,'all_dependencies_resolve':True,'acyclic_dependencies':True,'topological_order':order,
 'path_count':len(rows),'unique_paths':True,'source_categories':dict(cat),
 'product_sources':len(prod),'product_headers':len(heads),'native_test_sources_and_headers':len(native_tests),
 'product_dispositions':dict(collections.Counter(r['disposition'] for r in prod)),
 'all_product_sources_and_headers_explicitly_owned':True,'all_867_paths_have_tracking_owner':True,
 'original_inventory_sha256':'76d710e88e0a2d2ecb35a9bf895b3b8223a1fbf96b9029e51f185f2106089d87',
 'inventory_normalization':'CRLF to LF only; CSV row/field content retained',
 'input_sha256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((root/'source').iterdir())},
 'external_mutations':'none; no PR publication or merges',
}
(root/'validation.json').write_text(json.dumps(validation,indent=2)+'\n')
print(json.dumps({k:v for k,v in validation.items() if k!='topological_order'},indent=2))
