import hashlib,json,os,pathlib,subprocess,sys,time
root=pathlib.Path(__file__).resolve().parents[2]
task=root/".chibi/integration"
name=sys.argv[1]; argv=sys.argv[2:]
env={k:v for k,v in os.environ.items() if not k.startswith("CHIBI_")}
for k in ["CHIBI_LANGUAGE_BUILD_DIR","CHIBI_RELEASE_LANGUAGE_BUILD_DIR"]:env[k]=str(task/"language")
if name=="bootstrap-fixedpoint":env["CHIBI_BOOTSTRAP_SEED"]=str(task/"seed-not-for-publication")
start=time.monotonic(); log=task/(name+".log"); receipt=task/(name+".json")
assert not log.exists() and not receipt.exists(),name
print("Running",json.dumps(argv),flush=True)
with log.open("wb") as out: code=subprocess.call(argv,cwd=root,env=env,stdout=out,stderr=subprocess.STDOUT)
data={"name":name,"argv":argv,"cwd":str(root),"environment":{k:v for k,v in env.items() if k.startswith("CHIBI_")},"exit_code":code,"wall_seconds":time.monotonic()-start,"log":str(log),"log_sha256":hashlib.sha256(log.read_bytes()).hexdigest(),"head":subprocess.check_output(["git","rev-parse","HEAD"],cwd=root,text=True).strip(),"tracked_status":subprocess.check_output(["git","status","--porcelain=v1","--untracked-files=no"],cwd=root,text=True)}
receipt.write_text(json.dumps(data,indent=2)+"\n")
print(json.dumps(data),flush=True)
print(log.read_text(errors="replace")[-5000:],flush=True)
raise SystemExit(code)
