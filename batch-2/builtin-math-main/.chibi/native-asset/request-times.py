import json, subprocess,time
p='.chibi/native-asset/'
r=json.load(open(p+'request.json'))
for n in [1,4]:
 q=list(r);q[3]=r[3]*n;q[4]=r[4]*n
 f=p+f'wide{n}.json';json.dump(q,open(f,'w'))
 now=time.monotonic(); result=subprocess.run([p+'request-probe',f]); print(n,result.returncode,time.monotonic()-now,flush=True)
