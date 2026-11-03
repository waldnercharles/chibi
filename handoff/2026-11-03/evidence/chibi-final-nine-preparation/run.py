import concurrent.futures,collections,hashlib,json,os,pathlib,re,subprocess,sys,time
w=pathlib.Path(__file__).parent;compiler=str(pathlib.Path(sys.argv[1]).resolve());source=pathlib.Path(sys.argv[2]).resolve();llvm='/private/tmp/chibi-bba9-proof/llvm22';heavy='/Users/cwaldner/Personal/chibi/handoffs/heavy.sh'
prepared=json.loads((w/'prepared.json').read_text());expected=json.loads((w/'candidate-matrix.json').read_text());results=[]
for entry in prepared['families'].values():
 for item in entry['files']:
  data=(source/item['path']).read_bytes();assert hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()==item['blob'],item['path']
core=source/'packages/core';native_core=pathlib.Path(prepared['fresh_native_sdk']['core'])
for item in prepared['fresh_native_sdk']['files']:
 assert (core/item['path']).read_bytes()==(native_core/item['path']).read_bytes(),item['path']
report={'compiler':compiler,'compiler_sha256':hashlib.sha256(pathlib.Path(compiler).read_bytes()).hexdigest(),'source':str(source),'baseline_source':prepared['original_source'],'source_files_verified':prepared['source_files'],'results':results,'status':'running'}
(w/'final-receipt.json').write_text(json.dumps(report,indent=2)+'\n')
def run(name):
 env=os.environ.copy();env.update(CC='/usr/bin/false',CXX='/usr/bin/false',CHIBI_CORE=str(native_core if name=='native' else core),CHIBI_TEST_WORKERS='1')
 command=[compiler,'test','--llvm',llvm,str(source/'test/compiler/samples'/name)]
 cwd=pathlib.Path(prepared['fresh_native_sdk']['cwd']) if name=='native' else source
 started=time.monotonic()
 with (w/(name+'.log')).open('w') as log:p=subprocess.run(['sh',heavy,'root: final nine-family '+name,*command],cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT)
 text=(w/(name+'.log')).read_text();passed=re.findall(r'^PASS (.+)$',text,re.M);failed=re.findall(r'^FAIL (.+)$',text,re.M);counts=collections.Counter(passed);wanted=collections.Counter(expected['families'][name]['expected_names'])
 return {'family':name,'command':command,'cwd':str(cwd),'core':env['CHIBI_CORE'],'status':p.returncode,'seconds':round(time.monotonic()-started,2),'log_sha256':hashlib.sha256((w/(name+'.log')).read_bytes()).hexdigest(),'passed_names':passed,'failed_names':failed,'missing_baseline':list((wanted-counts).elements()),'additional_selected':list((counts-wanted).elements()),'summaries':re.findall(r'^\d+ tests,.*$',text,re.M)}
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
 tasks={pool.submit(run,name):name for name in prepared['families']}
 for future in concurrent.futures.as_completed(tasks):
  result=future.result();results.append(result);(w/'final-receipt.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:result[k] for k in ['family','status','seconds','failed_names','missing_baseline','additional_selected','summaries']}),flush=True)
report['status']='pass' if all(r['status']==0 and not r['failed_names'] and not r['missing_baseline'] for r in results) else 'needs investigation'
report['passed']=sum(len(r['passed_names']) for r in results);report['additional_selected_need_oracle_check']=[{'family':r['family'],'names':r['additional_selected']} for r in results if r['additional_selected']]
for entry in prepared['families'].values():
 for item in entry['files']:
  data=(source/item['path']).read_bytes();assert hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()==item['blob'],item['path']
report['sources_reverified']=True;(w/'final-receipt.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'status':report['status'],'passed':report['passed'],'additional':report['additional_selected_need_oracle_check']}),flush=True)
