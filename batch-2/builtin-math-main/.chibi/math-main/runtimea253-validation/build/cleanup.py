import collections,datetime,hashlib,json,os,pathlib,subprocess
b=pathlib.Path(__file__).resolve().parent
root=b.parent
repo=root.parents[2]
assert root.name=='runtimea253-validation' and root.parent.name=='math-main'
assert not root.is_symlink() and not b.is_symlink()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
load=lambda name:json.loads((b/name).read_text())
inventory=load('cleanup-inventory.json')
assert inventory['root']==str(root)
proof_hashes=inventory['protected_proof_hashes']
for name,h in proof_hashes.items():assert sha(b/name)==h,name
ps=subprocess.check_output(['ps','-axo','pid=,ppid=,command='],text=True)
active=[line for line in ps.splitlines() if str(root) in line and int(line.split(None,1)[0])!=os.getpid()]
assert not active,active
lsof=subprocess.run(['/usr/sbin/lsof','-nP','+D',str(root)],capture_output=True,text=True)
assert lsof.returncode==1 and not lsof.stdout.strip() and not lsof.stderr.strip(),lsof.stdout+lsof.stderr
before_tree=subprocess.check_output(['git','write-tree'],cwd=repo,text=True).strip()
before_status=subprocess.check_output(['git','status','--porcelain=v1','-uno'],cwd=repo,text=True)
removed=[]
for entry in inventory['remove']:
 p=root/entry['path']
 assert p.is_relative_to(root) and p.parent.resolve().is_relative_to(root)
 data=os.fsencode(os.readlink(p)) if p.is_symlink() else p.read_bytes()
 assert hashlib.sha256(data).hexdigest()==entry['sha256'],str(p)
 assert p.lstat().st_size==entry['bytes'],str(p)
 p.unlink()
 removed.append(entry)
for p in sorted((p for p in root.rglob('*') if p.is_dir() and not p.is_symlink()),key=lambda p:len(p.parts),reverse=True):
 if p==b:continue
 try:p.rmdir()
 except OSError:pass
for name,h in inventory['preserve'].items():
 p=root/name
 data=os.fsencode(os.readlink(p)) if p.is_symlink() else p.read_bytes()
 assert hashlib.sha256(data).hexdigest()==h,name
old=load('old-proof-baseline.json');oldroot=pathlib.Path(old['root'])
old_hashes={str(p.relative_to(oldroot)):sha(p) for p in oldroot.rglob('*') if p.is_file()}
assert old_hashes==old['files']
assert subprocess.check_output(['git','write-tree'],cwd=repo,text=True).strip()==before_tree
assert subprocess.check_output(['git','status','--porcelain=v1','-uno'],cwd=repo,text=True)==before_status
assert not (b/'test_language').exists() and not list(b.rglob('*.o')) and not list(b.rglob('*.a'))
assert all(p==b for p in root.iterdir()),[str(p) for p in root.iterdir()]
remaining=[p for p in root.rglob('*') if p.is_file() or p.is_symlink()]
receipt={'cleanup_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'root':str(root),'reason':'Parent independently audited the passing validation receipt, actual binary hash and final doc-only admission; AGENTS requires scratch native outputs and project copies removed after success.','validation_receipt_remains_historical':True,'protected_proof_hashes_unchanged':proof_hashes,'cleanup_inventory_sha256':sha(b/'cleanup-inventory.json'),'replay_instructions_sha256':sha(b/'REPLAY.md'),'source_recovery_sha256':sha(b/'source-recovery.json'),'deleted_file_count':len(removed),'deleted_logical_bytes':sum(x['bytes'] for x in removed),'deleted_allocated_bytes':sum(x['allocated_bytes'] for x in removed),'deleted_categories':dict(collections.Counter(x['category'] for x in removed)),'retained_file_count_before_this_receipt':len(remaining),'retained_logical_bytes_before_this_receipt':sum(p.lstat().st_size for p in remaining),'generated_input_bytes_retained':True,'all_preserved_file_hashes_unchanged':True,'old_runtime74a_proof_file_count':len(old_hashes),'all_old_runtime74a_proof_hashes_unchanged':True,'no_process_using_outputs':True,'ps_matching_processes':active,'lsof_exit_code':lsof.returncode,'authoritative_staged_tree_unchanged':before_tree,'authoritative_status_unchanged':True,'tests_repeated':False,'source_recovery':load('source-recovery.json')}
(b/'cleanup-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps({k:v for k,v in receipt.items() if k not in ['source_recovery']},indent=2))
print('cleanup_receipt_sha256',sha(b/'cleanup-receipt.json'))
