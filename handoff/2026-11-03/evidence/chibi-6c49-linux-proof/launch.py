from pathlib import Path
import hashlib, json, os, subprocess, sys, time
root=Path('/private/tmp/chibi-6c49-linux-proof')
receipt=json.loads((root/'receipt.json').read_text())
image=receipt['image']
actual=json.loads(subprocess.check_output(['docker','image','inspect',image]))[0]
assert actual['Id']==image and actual['Architecture']=='amd64' and actual['Os']=='linux'
args=['docker','run','--rm','--platform','linux/amd64','--network','none','--name','chibi-6c49-linux-proof','--mount','type=bind,src='+str(root)+',dst='+str(root),'--mount','type=bind,src='+receipt['builder']+',dst='+receipt['builder']+',readonly','--workdir',str(root/'source')]
for key,value in receipt['environment'].items(): args.extend(['--env',key+'='+value])
args.extend([image,'python3',str(root/'run.py')])
receipt['docker_command']=args
receipt['docker_image_platform']={'id':actual['Id'],'architecture':actual['Architecture'],'os':actual['Os']}
receipt['launch_runner_sha256']=hashlib.sha256((root/'launch.py').read_bytes()).hexdigest()
(root/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
started=time.monotonic()
result_code=1
try:
    with (root/'console.log').open('wb') as log:
        child=subprocess.Popen(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        for line in child.stdout:
            log.write(line); log.flush(); sys.stdout.buffer.write(line); sys.stdout.buffer.flush()
        result_code=child.wait()
finally:
    receipt=json.loads((root/'receipt.json').read_text())
    errors=[]
    for item in json.loads((root/'source-files.json').read_text()):
        try:
            path=root/'source'/item['path']
            data=os.fsencode(os.readlink(path)) if item['mode']=='120000' else path.read_bytes()
            assert hashlib.sha256(data).hexdigest()==item['sha256']
            assert hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()==item['git_blob']
            if item['mode']!='120000': assert bool(path.stat().st_mode&0o111)==(item['mode']=='100755')
        except BaseException as error: errors.append(item['path']+': '+repr(error))
    receipt['host_final_source_reverified']=receipt['source_files'] if not errors else False
    receipt['host_final_verification_errors']=errors
    receipt['docker_exit_status']=result_code
    receipt['docker_elapsed_s']=round(time.monotonic()-started,3)
    receipt['console_sha256']=hashlib.sha256((root/'console.log').read_bytes()).hexdigest()
    (root/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    if errors: result_code=2
sys.exit(result_code)
