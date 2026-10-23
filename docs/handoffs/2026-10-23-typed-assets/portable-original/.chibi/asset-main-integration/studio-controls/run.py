#!/usr/bin/env python3
"""Prepared negative controls. Executes only with explicit --execute authorization."""
import argparse, hashlib, json, os, pathlib, shlex, subprocess, sys
ROOT = pathlib.Path(__file__).resolve().parent
ENGINE = ROOT.parents[2]
STUDIO = ENGINE.parent / 'chibi-studio'
BUILD = ENGINE / 'build-studio-release'

def digest(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()

def output(args, cwd=BUILD):
    return subprocess.check_output(args, cwd=cwd, text=True)

def run(args, log, cwd=BUILD, env=None):
    with open(log, 'w') as stream:
        return subprocess.run(args, cwd=cwd, env=env, stdout=stream, stderr=subprocess.STDOUT).returncode

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--control', action='append', choices=[v['id'] for v in json.loads((ROOT/'plan.json').read_text())['controls']])
    args = parser.parse_args()
    plan = json.loads((ROOT/'plan.json').read_text())
    if not args.execute:
        print('Plan only. No compilation or execution. Parent must release the build lane and authorize --execute.')
        return 0
    for path, expected in plan['required_hashes'].items():
        if digest(path) != expected:
            raise RuntimeError('Identity changed: '+path)
    before_index = output(['git', 'diff', '--cached', '--binary'], STUDIO)
    before_status = output(['git', 'status', '--porcelain'], STUDIO)
    commands = json.loads((BUILD/'compile_commands.json').read_text())
    link_lines = output(['ninja', '-t', 'commands', 'bin/studio-acceptance']).splitlines()
    link = shlex.split(link_lines[-1])
    if link[:2] == [':', '&&']: link = link[2:]
    if link[-2:] == ['&&', ':']: link = link[:-2]
    if any(x in ('&&', ';', '|') for x in link):
        raise RuntimeError('Unexpected shell operation in link command')
    archive = BUILD/'libstudio.a'
    archive_args = [i for i, x in enumerate(link) if pathlib.Path(x).name == 'libstudio.a']
    if len(archive_args) != 1:
        raise RuntimeError('Expected exactly one Studio archive argument')
    share = ROOT/'share'/'chibi'
    share.parent.mkdir(exist_ok=True)
    if not share.exists(): share.symlink_to(BUILD/'share'/'chibi', target_is_directory=True)
    if share.resolve() != (BUILD/'share'/'chibi').resolve():
        raise RuntimeError('Resource link differs from accepted baseline')
    (ROOT/'bin').mkdir(exist_ok=True)
    results = []
    for control in plan['controls']:
        if args.control and control['id'] not in args.control: continue
        directory = ROOT/control['id']
        directory.mkdir(exist_ok=True)
        source = STUDIO/control['source']
        text = source.read_text()
        if text.count(control['before']) != 1:
            raise RuntimeError('Control anchor not unique: '+control['id'])
        changed = directory/source.name
        changed.write_text(text.replace(control['before'], control['after'], 1))
        overlay = directory/'overlay.json'
        overlay.write_text(json.dumps({'version':0, 'case-sensitive':True,
            'use-external-names':False, 'roots':[{'type':'file','name':str(source),
            'external-contents':str(changed)}]}, indent=2)+'\n')
        entry = next(x for x in commands if pathlib.Path(x['file']) == source)
        compile_args = shlex.split(entry['command'])
        member = pathlib.Path(compile_args[compile_args.index('-o')+1]).name
        obj = directory/member
        compile_args[compile_args.index('-o')+1] = str(obj)
        for flag in ('-MF','-MT','-MQ'):
            if flag in compile_args: compile_args[compile_args.index(flag)+1] = str(directory/'dependencies.d')
        compile_args[1:1] = ['-ivfsoverlay',str(overlay)]
        compiler = pathlib.Path(compile_args[0]).resolve()
        ar = compiler.parent/'llvm-ar'
        if output([str(ar), 't', str(archive)]).splitlines().count(member) != 1:
            raise RuntimeError('Archive member is not unique: '+member)
        receipt={'control':control['id'],'mutated_source_sha256':digest(changed),
            'compile_argv':compile_args,'compiler_sha256':digest(compiler),
            'baseline_binary_sha256':digest(BUILD/'bin/studio-acceptance')}
        if run(compile_args,directory/'compile.log',pathlib.Path(entry['directory'])):
            raise RuntimeError('Control compilation failed: '+control['id'])
        copied = directory/'libstudio.a'
        subprocess.run(['cp','-c',str(archive),str(copied)],check=True)
        subprocess.run([str(ar),'rcs',str(copied),str(obj)],check=True)
        executable = ROOT/'bin'/('studio-acceptance-'+control['id'])
        command = list(link)
        command[archive_args[0]] = str(copied)
        command[command.index('-o')+1] = str(executable)
        receipt['link_argv']=command
        if run(command,directory/'link.log'):
            raise RuntimeError('Control link failed: '+control['id'])
        env=dict(os.environ,CHIBI_ASSET_CACHE_DIR='')
        policy=[sys.executable,str(ENGINE/'tools/test_policy.py'),'native',str(executable),'--compiled-asset-edit-model']
        status=run(policy,directory/'model.log',ENGINE,env)
        log=(directory/'model.log').read_text()
        caught=status == 1 and '(FAILED:' in log and all(x in log for x in control['expected_failure_tokens'])
        receipt.update(exit_status=status, caught_expected_defect=caught,
            binary_sha256=digest(executable),policy_argv=policy)
        (directory/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
        results.append(receipt)
        if not caught: raise RuntimeError('Control did not fail at expected assertion: '+control['id'])
        executable.unlink()
        copied.unlink()
        obj.unlink()
    if before_index != output(['git','diff','--cached','--binary'],STUDIO) or before_status != output(['git','status','--porcelain'],STUDIO):
        raise RuntimeError('Active index/status changed during controls')
    (ROOT/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print('Expected negative controls caught:',len(results))
    return 0
if __name__ == '__main__':
    sys.exit(main())
