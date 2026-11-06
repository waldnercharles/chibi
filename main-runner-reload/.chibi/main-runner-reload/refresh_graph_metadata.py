from pathlib import Path
import json,re,shlex,subprocess
root=Path(__file__).resolve().parents[2]
reference=root/'.chibi/main-runner-reload/reference'
cache=(reference/'CMakeCache.txt').read_text()
third=Path(re.search(r'^CHIBI_THIRD_PARTY_DIR:INTERNAL=(.*)$',cache,re.M)[1])
commands=json.loads((reference/'compile_commands.json').read_text())
path=root/'tools/graph/engine/Targets.chibi'
text=path.read_text()
def mapped(value):
    value=Path(value)
    for base,prefix in [(reference/'sdk',''),(reference,'build/'),(third,'third-party/'),(root,'')]:
        if value.is_relative_to(base): return prefix+value.relative_to(base).as_posix()
    raise ValueError(value)
def branch(condition,values):
    lines=[]; line='            '
    for value in values:
        token=json.dumps(value)
        if len(line)+len(token)+2>112:
            lines.append(line.rstrip());line='            '
        line+=token+', '
    if line.strip():lines.append(line.rstrip(', '))
    lines[-1]=lines[-1].rstrip(', ')
    return '    if ('+condition+') {\n        return [\n'+'\n'.join(lines)+'\n        ];\n    }\n'
def replace(function,condition,values):
    global text
    start=text.index('pub '+function+' ::')
    a=text.index('    if ('+condition+')',start)
    first=text.index('{',a);depth=0
    for end in range(first,len(text)):
        if text[end]=='{':depth+=1
        elif text[end]=='}':
            depth-=1
            if not depth:break
    text=text[:a]+branch(condition,values)+text[end+2:]
for target in ['chibi_program','chibi_program_consumer','chibi_application','chibi','chibi-compile','test_tools','test_language']:
    command=next(x for x in commands if '/'+target+'.dir/' in x['command'])
    args=shlex.split(command['command'])
    replace('Includes','target == "'+target+'"',[mapped(x[2:]) for x in args if x.startswith('-I')])
    replace('SystemIncludes','target == "'+target+'"',[mapped(args[i+1]) for i,x in enumerate(args[:-1]) if x=='-isystem'])
for target in ['chibi','chibi-compile','test_tools','test_language']:
    output=('sdk/' if target.startswith('chibi') else ('test/tools/' if target=='test_tools' else 'test/language/'))+target
    command=subprocess.check_output(['ninja','-C',str(reference),'-t','commands','-s',output],text=True)
    (reference.parent/('links-'+target+'.txt')).write_text(command)
    args=shlex.split(command); args=args[args.index('-o')+2:];values=[]
    for value in args:
        if value=='&&':break
        if value.endswith(('.o','.obj')):continue
        if value.endswith('.a'):
            name=Path(value)
            if not name.is_absolute():name=reference/name
            value=name.name.removeprefix('lib').removesuffix('.a') if name.is_relative_to(reference) else mapped(name)
        values.append(value)
    replace('Links','target == "'+target+'" && build.platform() == "Darwin"',values)
path.write_text(text)
print('Updated graph include/system/link declarations from configured CMake metadata')
