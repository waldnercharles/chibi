"""Fixed graph-c runtime controller. The checked-in plan is explicitly unarmed."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import resource
import signal
import subprocess
import sys
import time

H = Path('/workspace/shared/chibi-graph-c-runtime-20261104-revision01')
CANCEL = None


def need(ok, reason):
    if not ok:
        raise ValueError(reason)


def read(path):
    def unique(pairs):
        result = {}
        for key,value in pairs:
            need(key not in result,'duplicate JSON key')
            result[key] = value
        return result
    return json.loads(Path(path).read_text(),object_pairs_hook=unique)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load_fixed(name, expected):
    path = H/(name+'.py')
    need(digest(path) == expected,'fixed module changed: '+name)
    spec = importlib.util.spec_from_file_location(name,path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def receive_cancel(number, frame):
    global CANCEL
    if CANCEL is None:
        CANCEL = number


def verify_inner(receipt, journal, actual_returncode, observed_wall, observed_mono, intent):
    """Pure terminal reconciliation. A provisional journal entry never proves actual exit."""
    need(actual_returncode == 0 and receipt['status'] == 'ownership_passed','inner did not actually exit0')
    need(receipt['cleanup_echild'] is True and receipt['root_status'] == 0
         and receipt['cancel_signal'] is None and receipt['failure'] is None,'inner failure/cleanup contradicts success')
    need(not any(key in receipt for key in ('error','cleanup_error','close_error','descriptor_close_error','blocked_action')),
         'inner receipt contains failure evidence')
    for clock,observed in [('wall',observed_wall),('mono',observed_mono)]:
        need(receipt['start_'+clock] == intent['start_'+clock] and receipt['end_'+clock] == intent['end_'+clock]
             and intent['end_'+clock]-intent['start_'+clock] == 125,'inner reset original clocks')
        need(intent['start_'+clock] <= receipt['finished_'+clock] < intent['end_'+clock]-15
             and receipt['finished_'+clock] <= observed < intent['end_'+clock]-10,'inner actual exit/receipt late')
    allowed = {'root_identity','registered','adopted_identity','terminal_before_reap','reaped',
               'ECHILD','discovery_closed','ownership_success_provisional'}
    need(journal and all(row['event'] in allowed for row in journal),'unknown or failure journal event')
    need(all(intent['start_'+clock] <= row[clock] < intent['end_'+clock]-15
             for row in journal for clock in ('wall','mono')),'late journal event')
    need(all(a['mono'] <= b['mono'] for a,b in zip(journal,journal[1:])),'journal order changed')
    root = receipt['root']['pid']
    inner = receipt['root']['ppid']
    need(receipt['root']['pgid'] == receipt['root']['sid'] == root,'root receipt lost launch-gate group/session')
    registered, adopted, observed, reaped = {}, {}, {}, {}
    root_seen = False
    root_identity = None
    echild = False
    successes = 0
    for row in journal:
        kind = row['event']
        need(not echild or kind in ('ECHILD','ownership_success_provisional'),'new lifecycle event after ECHILD')
        if kind == 'root_identity':
            identity = row['identity']
            need(not root_seen and identity['pid'] == identity['pgid'] == identity['sid'] == root
                 and identity['ppid'] == inner and identity['start'] == receipt['root']['start_ticks'],'root identity mismatch')
            root_seen = True
            root_identity = identity
        elif kind == 'registered':
            identity, ancestry = row['identity'], row['ancestry']
            pid = identity['pid']
            need(root_seen and pid not in registered and ancestry and ancestry[0]['pid'] == root
                 and all(ancestry[0][key] == root_identity[key] for key in ('pid','start','ppid','pgid','sid'))
                 and all(intent['start_'+clock] <= row['witness'][clock] <= row[clock]
                         for clock in ('wall','mono')),'invalid prior registration')
            chain = ancestry+[identity]
            for parent,child in zip(chain,chain[1:]):
                need(child['ppid'] == parent['pid'] and child['sid'] == root,'registered parent/session differs')
            if len(ancestry) == 1:
                need(pid == receipt['shell_pid'] == identity['pgid'],'wrong deadline shell')
            else:
                need(ancestry[1]['pid'] == receipt['shell_pid'] and identity['pgid'] == receipt['shell_pid'],
                     'registration outside deadline subtree')
                for parent in ancestry[1:]:
                    need(parent['pid'] in registered and all(
                         registered[parent['pid']]['identity'][key] == parent[key]
                         for key in ('pid','start','ppid','pgid','sid')),
                         'ancestry lacks prior exact identity')
            registered[pid] = row
        elif kind == 'adopted_identity':
            identity = row['identity']; pid = identity['pid']
            need(pid in registered and pid != receipt['shell_pid'] and pid not in adopted
                 and identity['ppid'] == inner and identity['state'] == 'Z','unregistered/live adoption')
            prior = registered[pid]
            need(prior['mono'] <= row['mono'] and len(prior['ancestry']) >= 2
                 and all(identity[key] == prior['identity'][key] for key in ('pid','start','pgid','sid')),'adoption identity changed')
            adopted[pid] = row
        elif kind == 'terminal_before_reap':
            pid = row['pid']
            need(pid not in observed and (pid == root or pid in adopted),'terminal event lacks owned identity')
            need((pid == root and row['code'] == os.CLD_EXITED and row['status'] == 0)
                 or (pid != root and row['code'] == os.CLD_KILLED and row['status'] == signal.SIGKILL),'wrong terminal status')
            observed[pid] = row
        elif kind == 'reaped':
            pid,status = row['pid'],row['raw_status']
            need(pid in observed and pid not in reaped and observed[pid]['mono'] <= row['mono'],'reap without prior exact wait')
            need((pid == root and status == 0) or (pid != root and os.WIFSIGNALED(status)
                 and os.WTERMSIG(status) == signal.SIGKILL and not os.WCOREDUMP(status)),'raw wait disagrees')
            reaped[pid] = row
        elif kind == 'ECHILD':
            need(root in reaped and set(reaped) == set(observed),'ECHILD precedes complete reaping')
            echild = True
        elif kind == 'ownership_success_provisional':
            need(echild and row['root_status'] == 0 and row['echild'] is True and row['owner_signals'] == 0,
                 'success lacks complete unsignalled cleanup')
            need(set(row['accepted_adoptions']) == set(adopted),'provisional adoption set differs')
            successes += 1
    need(root_seen and echild and successes == 1 and journal[-1]['event'] == 'ownership_success_provisional'
         and adopted and set(reaped) == {root}|set(adopted)
         and set(receipt['accepted_adoptions']) == set(adopted),'incomplete terminal evidence')


def worker():
    intent = read(H/'runtime-intent.json'); plan = read(H/'plan.json')
    need(plan['runtime_enabled'] is True and digest(H/'plan.json') == intent['plan_sha256'],'UNARMED or changed plan')
    g = load_fixed('guard',plan['pins']['guard.py'])
    g.set_clock(intent,15); plan['_digest'] = intent['plan_sha256']
    g.admission(plan,intent)
    g.need(g.identity(os.getppid()) == intent['outer_identity'],'worker is not exact outer child')
    for number in (signal.SIGTERM,signal.SIGINT,signal.SIGHUP):
        signal.signal(number,receive_cancel)
    signal.signal(signal.SIGCHLD,signal.SIG_DFL)
    signal.pthread_sigmask(signal.SIG_SETMASK,[])
    g.fence(plan); g.focused_c_guard(plan); g.artifact_guard(plan['compile_artifact_inventory'])
    g.need(CANCEL is None,'cancelled before inner launch')
    work = H/'work'; work.mkdir(mode=0o700)
    for name in ('home','tmp','cache','compiler-cache','asset-cache'):
        (work/name).mkdir(mode=0o700)
    inner_intent = dict(runtime_intent_sha256=g.sha(H/'runtime-intent.json'),worker_identity=g.identity(os.getpid()))
    g.save(H/'inner-intent.json',inner_intent)
    inner_intent_fact = g.fact(H/'inner-intent.json')
    status = None
    terminal = dict(status='failed',phase='graph-c-worker')
    child = None
    child_fd = None
    try:
        with (work/'inner.raw.log').open('xb') as log:
            g.tick(); g.need(CANCEL is None,'cancelled before inner fork')
            child = subprocess.Popen(['/usr/bin/python3','-I','-S','-B',str(H/'inner_owner.py')],
                                     cwd=g.S,env={},stdout=log,stderr=subprocess.STDOUT,close_fds=True)
            child_fd = os.pidfd_open(child.pid,0)
            inner_identity = g.identity(child.pid)
            g.need(inner_identity['ppid'] == os.getpid(),'inner is not the exact owned child')
            cancel_sent = False
            while child.poll() is None:
                if CANCEL is not None and not cancel_sent:
                    cancel_sent = True
                    signal.pidfd_send_signal(child_fd,signal.SIGTERM)
                if time.time() >= intent['end_wall']-10 or time.monotonic() >= intent['end_mono']-10:
                    raise TimeoutError('inner actual exit not observed by original115-second cutoff')
                time.sleep(max(0,min(0.005,intent['end_wall']-10-time.time(),intent['end_mono']-10-time.monotonic())))
            status = child.returncode
            observed_wall,observed_mono = time.time(),time.monotonic()
        g.set_clock(intent,5)
        g.save(work/'inner-observed.json',dict(identity=inner_identity,actual_returncode=status,
                                              observed_wall=observed_wall,observed_mono=observed_mono))
        evidence_names = ('inner-result.json','inner-observed.json','ancestry.jsonl','graph-launch.json',
                          'root-identity.json','inner.raw.log','graph-c.stdout','graph-c.stderr')
        for name in evidence_names:
            g.need((work/name).is_file() and not (work/name).is_symlink(),'nonregular inner evidence: '+name)
        inner_evidence = [g.fact(work/name) for name in evidence_names]
        g.need(g.fact(H/'inner-intent.json') == inner_intent_fact,'inner intent changed')
        claim = g.read(H/'inner.claim')
        g.need(claim['runtime_intent_sha256'] == inner_intent['runtime_intent_sha256']
               and claim['worker'] == inner_intent['worker_identity'] and claim['inner'] == inner_identity,
               'exclusive inner claim differs from actual owner identity')
        inner_evidence.extend((inner_intent_fact,g.fact(H/'inner.claim'),g.fact(H/'runtime-intent.json')))
        receipt = g.read(work/'inner-result.json')
        root_record = g.read(work/'root-identity.json')
        launch = g.read(work/'graph-launch.json')
        g.need(root_record['identity'] == receipt['root'] and root_record['inner_identity'] == inner_identity,
               'launch-gate root/inner identity differs from actual owner')
        g.need(launch == dict(argv=g.ARGV,cwd=str(g.S),environment=g.environment(),
                             executable_sha256=g.GRAPH_SHA256,clock_pair=intent),'fixed graph launch record differs')
        journal = [json.loads(line) for line in (work/'ancestry.jsonl').read_text().splitlines()]
        verify_inner(receipt,journal,status,observed_wall,observed_mono,intent)
        g.need(CANCEL is None and (work/'inner.raw.log').read_bytes() == b'','inner logging/cancellation failure')
        g.output_guard((work/'graph-c.stdout').read_bytes(),(work/'graph-c.stderr').read_bytes())
        generated = g.fence(plan,True)
        g.focused_c_guard(plan); g.artifact_guard(plan['compile_artifact_inventory']); g.work_guard()
        g.admission(plan,intent); g.need(CANCEL is None,'cancelled during final fences')
        g.need(all(g.fact(row['path']) == row for row in inner_evidence),'inner evidence changed across final fences')
        expected = {'home','tmp','cache','compiler-cache','asset-cache','inner.raw.log','inner-result.json',
                    'inner-observed.json','ancestry.jsonl','graph-launch.json','root-identity.json','graph-c.stdout','graph-c.stderr'}
        g.need({path.name for path in work.iterdir()} == expected,'unexpected runtime work output')
        terminal.update(status='passed',generated=generated,inner_identity=inner_identity,inner_evidence=inner_evidence,
                        stdout=g.fact(work/'graph-c.stdout'),stderr=g.fact(work/'graph-c.stderr'))
    except BaseException as error:
        terminal['error'] = repr(error)
    finally:
        if child_fd is not None:
            os.close(child_fd)
    terminal['cancel_signal'] = CANCEL
    terminal['finished_wall'],terminal['finished_mono'] = time.time(),time.monotonic()
    g.set_clock(intent,5); g.save(work/'worker-result.json',terminal)
    g.tick()
    return 0 if terminal['status'] == 'passed' and CANCEL is None else 1


def execute(acceptance_path, acceptance_sha256):
    start_wall,start_mono = time.time(),time.monotonic()
    plan = read(H/'plan.json')
    need(plan['runtime_enabled'] is True,'UNARMED: caller/control/artifact admission not complete')
    need(acceptance_path.is_absolute() and not acceptance_path.is_symlink()
         and not acceptance_path.resolve().is_relative_to(H),'external independent acceptance required')
    need(digest(acceptance_path) == acceptance_sha256,'independent acceptance digest differs')
    accepted = read(acceptance_path)
    need(accepted['status'] == 'accepted' and accepted['phase'] == 'graph-c-runtime'
         and accepted['plan_sha256'] == digest(H/'plan.json'),'not exact runtime acceptance')
    g = load_fixed('guard',plan['pins']['guard.py'])
    intent = dict(start_wall=start_wall,start_mono=start_mono,end_wall=start_wall+125,end_mono=start_mono+125,
                  acceptance_path=str(acceptance_path),acceptance_sha256=acceptance_sha256,
                  plan_sha256=accepted['plan_sha256'],outer_identity=g.identity(os.getpid()))
    g.set_clock(intent,0); plan['_digest'] = intent['plan_sha256']
    os.sched_setaffinity(0,{0,1}); resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    resource.setrlimit(resource.RLIMIT_FSIZE,(g.FILE_LIMIT,g.FILE_LIMIT)); os.umask(0o022)
    g.admission(plan,intent)
    for name in ('runtime-intent.json','inner-intent.json','inner.claim','work','outer','result.json'):
        g.need(not os.path.lexists(H/name),'runtime attempt already consumed: '+name)
    g.save(H/'runtime-intent.json',intent)  # Exclusive. Never deleted/reset after any outcome.
    plan['_intent_sha256'] = g.sha(H/'runtime-intent.json')
    terminal = dict(status='failed',phase='graph-c-runtime',intent=intent)
    try:
        outer = load_fixed('outer_owner',g.OUTER_SHA256)
        status = outer.supervise(['/usr/bin/python3','-I','-S','-B',str(H/'run.py'),'--worker'],
                                 g.S,H/'outer',intent['end_wall']-5,intent['end_wall'],intent)
        outer_observed_wall,outer_observed_mono = time.time(),time.monotonic()
        receipt = g.read(H/'outer/receipt.json')
        g.need(status == 0 and receipt['status'] == 'pass' and receipt['cleanup_echild'] is True
               and receipt['deadline_respected'] is True and receipt['cancel_signal'] is None
               and not receipt['signals'] and not receipt['errors'],'unchanged outer rejected ownership')
        g.need(outer_observed_wall < intent['end_wall']-5 and outer_observed_mono < intent['end_mono']-5,
               'worker actual exit not established before120 seconds')
        g.need(len(receipt['observed_children']) == 1 and len(receipt['wait_statuses']) == 1
               and receipt['wait_statuses'][0]['raw_wait_status'] == 0,'outer adopted or lost a child')
        result = g.read(H/'work/worker-result.json')
        g.need(result['status'] == 'passed' and result['cancel_signal'] is None
               and result['finished_wall'] < intent['end_wall']-5
               and result['finished_mono'] < intent['end_mono']-5,'worker result failed/late')
        g.need(all(g.fact(row['path']) == row for row in result['inner_evidence']),'inner evidence changed at outer terminal')
        g.need((H/'outer/raw.log').read_bytes() == b'' and g.sha(H/'outer/raw.log') == receipt['raw_log_sha256'],
               'unexpected worker output')
        g.need({path.name for path in (H/'outer').iterdir()} == {'receipt.json','launch.json','raw.log'},'unexpected outer output')
        g.admission(plan,intent); g.tick()
        terminal.update(status='passed',outer_observed_wall=outer_observed_wall,outer_observed_mono=outer_observed_mono)
    except BaseException as error:
        terminal['error'] = repr(error)
    terminal['finished_wall'],terminal['finished_mono'] = time.time(),time.monotonic()
    g.save(H/'result.json',terminal)
    g.tick()
    print(json.dumps(terminal,sort_keys=True),flush=True)
    g.tick()
    return 0 if terminal['status'] == 'passed' else 1


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--worker',action='store_true')
    parser.add_argument('--acceptance',type=Path)
    parser.add_argument('--acceptance-sha256')
    args = parser.parse_args()
    if args.worker:
        need(args.acceptance is None and args.acceptance_sha256 is None,'worker accepts no alternate binding')
        raise SystemExit(worker())
    need(args.acceptance is not None and args.acceptance_sha256 is not None,'UNARMED: exact independent runtime acceptance required')
    raise SystemExit(execute(args.acceptance,args.acceptance_sha256))
