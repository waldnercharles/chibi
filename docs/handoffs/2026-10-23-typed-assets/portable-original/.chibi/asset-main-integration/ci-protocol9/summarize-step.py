import sys,json,re,ast
from pathlib import Path
out=Path(__file__).resolve().parent
p=out/(sys.argv[1]+'.json');r=json.loads(p.read_text());assert r['status']!='running'
s=Path(r['log']).read_text();rows=re.findall(r'Summary: Passed: (\d+) Failed: (\d+) Total: (\d+) Suites: \d+ Asserts: (\d+)',s)
r['results']={'summary_rows':len(rows),'passed_cases':sum(int(x[0]) for x in rows),'failed_cases':sum(int(x[1]) for x in rows),'assertions':sum(int(x[3]) for x in rows)}
t=re.search(r'Test command wall time: ([0-9.]+)s',s)
if t:r['results']['test_command_wall_seconds']=float(t.group(1))
r['build_timings']=[{'operation':a,'seconds':float(b)} for a,b in re.findall(r'^([\w-]+): ok \(([0-9.]+)s;',s,re.M)]
p.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({'name':r['name'],'results':r['results'],'build_timings':r['build_timings'],'elapsed_seconds':r['elapsed_seconds'],'log_sha256':r['log_sha256']},indent=2))
