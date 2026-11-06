from pathlib import Path
import json,subprocess,time,hashlib,sys
root=Path.cwd();task=root/'.chibi/main-runner-reload';fixture=task/('studio-real-sdk-'+sys.argv[1] if len(sys.argv)>1 else 'studio-real-sdk');studio=root.parent/'chibi-studio';sdk=task/'public-sdk'
source=(studio/'CMakeLists.txt').read_text();begin=source.index('file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/share/chibi")');end=source.index('file(COPY projects/studio',begin);block=source[begin:end]
fixture.mkdir(exist_ok=True);(fixture/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.25)\nproject(studio_resource_contract NONE)\nset(Chibi_RESOURCE_DIR '+json.dumps(str(sdk/'share/chibi'))+')\n'+block)
(fixture/'Hello.chibi').write_text('package studio_sdk\nimport strings "core:strings"\nMain :: () { assert(strings.HasSuffix("Hello, Studio!", "Studio!"), "copied core"); print_string("Hello, Studio!") }\n')
commands=[('configure',['cmake','-S',str(fixture),'-B',str(fixture/'build')]),('install',['cmake','--install',str(fixture/'build'),'--prefix',str(fixture/'installed')]),('copied-cli',[str(fixture/'build/bin/chibi'),str(fixture/'Hello.chibi')])];records=[]
for name,command in commands:
 log=fixture/(name+'.log');start=time.monotonic()
 with log.open('w') as output:r=subprocess.run(command,stdout=output,stderr=subprocess.STDOUT,cwd=root)
 record={'step':name,'returncode':r.returncode,'wall_seconds':round(time.monotonic()-start,3),'command':command,'log':str(log)};records.append(record)
 if r.returncode:print(log.read_text());sys.exit(r.returncode)
assert (fixture/'copied-cli.log').read_text()=='Hello, Studio!\n'
count=0
for relative in ['shaders','licenses','apps','core']:
 for file in (sdk/'share/chibi'/relative).rglob('*'):
  if not file.is_file():continue
  tail=file.relative_to(sdk/'share/chibi');expected=hashlib.sha256(file.read_bytes()).digest();count+=1
  for destination in [fixture/'build/share/chibi',fixture/'installed/share/chibi']:
   assert hashlib.sha256((destination/tail).read_bytes()).digest()==expected,tail
for destination in [fixture/'build/share/chibi/engine.json',fixture/'installed/share/chibi/engine.json']:
 assert destination.read_bytes()==(sdk/'share/chibi/engine.json').read_bytes()
result={'sdk_source':json.loads((sdk/'share/chibi/package.json').read_text())['source'],'studio_head':subprocess.check_output(['git','-C',str(studio),'rev-parse','HEAD'],text=True).strip(),'studio_cmake_sha256':hashlib.sha256((studio/'CMakeLists.txt').read_bytes()).hexdigest(),'copied_resource_files':count,'engine_metadata_equal':True,'cli_output':'Hello, Studio!\n','steps':records,'scope':'current Studio resource/tool copy and install block against actual SDK; no Studio compile or UI launch'}
(fixture/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
