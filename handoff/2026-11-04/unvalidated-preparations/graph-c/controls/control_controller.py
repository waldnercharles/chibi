"""UNARMED one-suite controller. Default invocation launches nothing.

One unchanged outer spans420s, preserving its final5s cleanup. Its one fixed
worker is the per-case subreaper:5s normal,7s inner cleanup,8s external completion.
Case34 is last and deliberately transfers its known fixture to that outer.
"""
import ctypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import signal
import stat
import subprocess
import sys
import time

H = Path('/workspace/shared/chibi-graph-c-runtime-20261104-revision01/controls')
OUTER_SHA256 = 'eb565285001cef43ed88cf73e5797cc83b726afcf6cf192cf5b4ba10f6078698'
CASES = ((1,None),(2,None),(3,None),(4,None),(5,None),(10,None),(11,None),
         (14,None),(15,None),(16,None),(18,None),(19,None),(21,None),(22,None),
         (23,None),(24,None),(25,None),(26,'fsync'),(26,'close'),(29,None),
         (30,None),(30,'mismatch'),(31,None),(31,'open'),(32,None),
         (32,'registration'),(32,'terminal'),(33,None),(35,None),(36,None),
         (37,None),(34,None))


def need(ok, reason):
    if not ok:
        raise RuntimeError(reason)


def read(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            need(key not in result, 'duplicate JSON key')
            result[key] = value
        return result
    return json.loads(Path(path).read_text(), object_pairs_hook=unique)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(path, value, *, replace=False):
    target = path.with_suffix('.pending') if replace else path
    with target.open('x', encoding='utf-8') as stream:
        json.dump(value, stream, sort_keys=True)
        stream.write('\n'); stream.flush(); os.fsync(stream.fileno())
    if replace:
        target.replace(path)
    directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        os.fsync(directory)
    finally:
        os.close(directory)


def fact(path):
    path = Path(path)
    info = path.lstat()
    need(stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode), 'control input is not a file/link')
    return dict(path=str(path), realpath=str(path.resolve()), bytes=info.st_size,
                mode=stat.S_IMODE(info.st_mode), sha256=sha(path), link=os.readlink(path) if path.is_symlink() else None)


def load(name, digest, parent=H):
    path = parent/(name + '.py')
    need(sha(path) == digest, 'fixed source changed: ' + name)
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def admission():
    plan = read(H/'CONTROL-PLAN.json')
    need(plan['enabled'] is True, 'UNARMED: control plan disabled')
    intent = read(H/'suite-intent.json')
    need(plan['fixture_argv_prefix'] == [str(H/'artifacts/fixture'), '--root'] and
         plan['environment'] == {'PATH':'/usr/bin:/bin','LC_ALL':'C','LANG':'C','TZ':'UTC'} and
         plan['working_root'] == str(H/'work'), 'fixed control argv/environment/cwd differs')
    need(plan['cpu_affinity'] == [0,1] and plan['descriptor_exec'] is True and
         os.execve in os.supports_fd, 'fixed control affinity/descriptor-exec differs')
    need(sha(H/'CONTROL-PLAN.json') == intent['plan_sha256'], 'control plan changed')
    need(intent['end_wall'] - intent['start_wall'] == 420 and
         intent['end_mono'] - intent['start_mono'] == 420, 'control original interval changed')
    need(time.time() < intent['end_wall'] - 5 and time.monotonic() < intent['end_mono'] - 5,
         'control original work interval expired')
    need(plan['max_fixture_launches'] <= 48 and plan['case_seconds'] == [5,7,8] and
         plan['suite_seconds'] == 420 and plan['final_outer_seconds'] == 5, 'control budget differs')
    path = Path(intent['acceptance_path'])
    need(path.is_absolute() and not path.resolve().is_relative_to(H) and not path.is_symlink(),
         'independent control acceptance path required')
    need(sha(path) == intent['acceptance_sha256'], 'control acceptance changed')
    accepted = read(path)
    need(accepted['phase'] == 'graph-c-owner-controls' and accepted['status'] == 'accepted-for-execution',
         'control execution admission absent')
    need(accepted['plan_sha256'] == intent['plan_sha256'] and
         accepted['source_sha256'] == plan['source_sha256'] and
         accepted['case_manifest_sha256'] == sha(H/'manifest.json'), 'control exact source binding differs')
    need(accepted['not_before_epoch'] <= intent['start_wall'] and intent['end_wall'] <= accepted['not_after_epoch'],
         'control acceptance window excludes original interval')
    need(accepted['production_source_sha256'] == plan['production_source_sha256'] and
         accepted['budget'] == plan['budget'], 'control production/budget binding differs')
    source_review = plan['source_admission']
    need(source_review is not None and fact(source_review['path']) == source_review and
         accepted['source_admission'] == source_review, 'independent control source review absent')
    reviewed = read(source_review['path'])
    need(reviewed['status'] == 'accepted' and reviewed['phase'] == 'graph-c-control-source' and
         reviewed['source_sha256'] == plan['source_sha256'] and
         reviewed['production_source_sha256'] == plan['production_source_sha256'], 'control source review scope differs')
    need(plan['budget']['case_invocations'] == len(CASES) and
         plan['budget']['fixture_launches'] == sum(case != 21 for case, variant in CASES) and
         plan['budget']['fixture_launches'] <= 48, 'control exact case/launch count differs')
    for name, digest in plan['production_source_sha256'].items():
        need('/' not in name and sha(H.parent/name) == digest, 'production source changed: ' + name)
    for name, digest in plan['source_sha256'].items():
        need('/' not in name and sha(H/name) == digest, 'control source changed: ' + name)
    need(sha(H/'outer_owner.py') == OUTER_SHA256, 'unchanged outer changed')
    need(plan['fixture'] is not None and fact(H/'artifacts/fixture') == plan['fixture'], 'fixture ELF not admitted')
    artifact = plan['artifact_admission']
    need(artifact is not None and fact(artifact['path']) == artifact and accepted['artifact_admission'] == artifact,
         'control native/runtime artifact admission absent')
    closure = read(artifact['path'])
    need(closure['status'] == 'accepted' and closure['phase'] == 'graph-c-control-artifact' and
         closure['fixture'] == plan['fixture'] and
         closure['fixture_source_sha256'] == plan['source_sha256']['fixture.c'], 'fixture artifact scope differs')
    for item in closure['files']:
        need(fact(item['path']) == item, 'control runtime closure changed')
    mapped = {line.split()[-1] for line in Path('/proc/self/maps').read_text().splitlines()
              if len(line.split()) >= 6 and line.split()[-1].startswith('/')}
    need(mapped <= {item['realpath'] for item in closure['files']}, 'actual Python/ctypes maps absent from closure')
    need(sys.flags.isolated and sys.flags.no_site and sys.dont_write_bytecode and
         Path('/proc/self/environ').read_bytes() == b'', 'fixed empty isolated Python required')
    need(sorted(os.sched_getaffinity(0)) == [0,1], 'control affinity differs')
    return plan, intent


def subreaper(enabled):
    libc = ctypes.CDLL(None, use_errno=True)
    libc.prctl.argtypes = [ctypes.c_int, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong]
    libc.prctl.restype = ctypes.c_int
    if libc.prctl(36, int(enabled), 0, 0, 0):
        raise OSError(ctypes.get_errno(), 'control subreaper setting')
    observed = ctypes.c_int()
    if libc.prctl(37, ctypes.addressof(observed), 0, 0, 0) or observed.value != int(enabled):
        raise RuntimeError('control subreaper verification failed')


def echild():
    try:
        os.waitid(os.P_ALL, 0, os.WEXITED | os.WNOHANG | os.WNOWAIT)
    except ChildProcessError:
        return True
    return False


def run_models(plan):
    binding = load('control_binding', plan['source_sha256']['control_binding.py'])
    models = load('model_binding', plan['source_sha256']['model_binding.py'])
    tables = load('terminal_tables', plan['source_sha256']['terminal_tables.py'])
    admission_tables = load('admission_tables', plan['source_sha256']['admission_tables.py'])
    support = load('caller_support', plan['source_sha256']['caller_support.py'])
    core = load('graph_c_attribution', binding.CORE_SHA256, H.parent)
    mutations = [load(name, plan['source_sha256'][name + '.py'])
                 for name in ('core_without_live_latch', 'core_without_eligibility')]
    verifier = load('run', plan['production_source_sha256']['run.py'], H.parent)
    results = models.run_models(core, *mutations)
    results.extend(tables.run_terminal_tables(verifier.verify_inner))
    results.extend(admission_tables.run_admission_tables(sys.modules[__name__], support))
    save(H/'model-results.json', dict(results=results, model_only=True))


def check_case(case_id, variant, receipt, journal, actual, external, verifier, intent):
    need(external['echild'], 'case external ECHILD absent')
    need(external['finished_wall'] < intent['start_wall'] + 8 and
         external['finished_mono'] < intent['start_mono'] + 8, 'case external result late')
    if case_id in (1,18):
        verifier.verify_inner(receipt, journal, actual, external['observed_wall'], external['observed_mono'], intent)
        need(not external['adoptions'] and not external['signals'], 'positive case needed external cleanup')
    else:
        need(actual is not None and actual != 0, 'negative case did not actually fail')
        need((receipt is not None and receipt['cleanup_echild']) or case_id in (30,31),
             'negative case missed owned cleanup')
        need(case_id in (30,31) or not external['adoptions'], 'unexpected external adoption')
    required = {
        2: ('unregistered',), 3: ('unregistered',),
        4: ('unregistered', 'incomplete graph-c ownership'),
        5: ('deadline-shell argv/executable mismatch',),
        10: ('ProcessLookupError', 'FileNotFoundError'), 11: ('FileNotFoundError', 'ProcessLookupError'),
        14: ('unregistered', 'live adoption'),
        15: ('registration lost', 'live adoption', 'ancestry changed'),
        16: ('live adoption',), 19: ('unregistered',),
        21: ('cancelled before fork',), 22: ('control cancellation after root retention',),
        23: ('cancelled:',), 24: ('cancelled:',),
        30: ('synthetic ownership read denial', 'not the exact unreaped direct kernel child'),
        31: ('synthetic pidfd', 'force failure-only cleanup'),
        32: ('journal I/O',), 33: ('control cleanup cascade',),
        35: ('graph-c did not exit 0',), 36: ('graph-c did not exit 0',),
        37: ('incomplete graph-c ownership',),
    }
    if case_id in required:
        evidence = str(receipt) + str(journal)
        need(any(token in evidence for token in required[case_id]), 'required fault outcome not distinguished')
    if case_id == 35:
        need(any(row['event'] == 'terminal_before_reap' and row.get('code') == os.CLD_EXITED and
                 row.get('status') == 7 for row in journal), 'required root exit7 not retained')
    if case_id == 36:
        need(any(row['event'] == 'terminal_before_reap' and row.get('code') == os.CLD_EXITED and
                 row.get('status') == 127 for row in journal), 'descriptor-exec127 not retained')
    if case_id in (18,19):
        real = [row for row in receipt['fixture_evidence'] if row['event'] == 'real20ms']
        need(len(real) == 1 and real[0]['marker_absent'], 'real20ms marker evidence absent')
    if case_id == 16:
        need('live adoption' in str(receipt.get('failure')), 'live latch not exercised')
    if case_id == 2:
        need('unregistered' in str(receipt.get('failure')), 'registration suppression not distinguished')
    if case_id in (25,26,29):
        need((H/'work'/('case-%02d' % case_id + (('-' + variant) if variant else ''))/'injection-event.json').exists(),
             'terminal fault boundary not witnessed')


def worker():
    plan, suite = admission()
    outer = load('outer_owner', OUTER_SHA256)
    expected_outer = read(H/'outer-identity.json')
    need(outer.stable(outer.identity(os.getppid())) == expected_outer, 'worker is not exact outer child')
    save(H/'suite.claim', dict(worker=outer.stable(outer.identity(os.getpid())), intent_sha256=sha(H/'suite-intent.json')))
    signal.signal(signal.SIGCHLD, signal.SIG_DFL)
    signal.pthread_sigmask(signal.SIG_SETMASK, [])
    need(echild(), 'suite worker must begin at ECHILD')
    subreaper(True)
    run_models(plan)
    verifier = load('run', plan['production_source_sha256']['run.py'], H.parent)
    prior = []
    for case_id, variant in CASES:
        need(echild(), 'previous case has unresolved children')
        admission()
        label = 'case-%02d' % case_id + (('-' + variant) if variant else '')
        directory = H/'work'/label
        directory.mkdir(mode=0o700)
        (directory/'work').mkdir(mode=0o700)
        wall, mono = time.time(), time.monotonic()
        intent = dict(case_id=case_id, variant=variant, start_wall=wall, end_wall=wall+125,
                      start_mono=mono, end_mono=mono+125, parent=outer.stable(outer.identity(os.getpid())))
        save(directory/'control-intent.json', intent)
        save(H/'active-case.json', intent, replace=True)
        if case_id == 34:
            save(H/'prior-results.json', dict(cases=prior, all_prior_echild=True))
            need(len(prior) == len(CASES)-1 and echild(), 'final negative cannot start before prior durable completion')
            save(H/'final-adoption-intent.json', dict(case=34, prior_sha256=sha(H/'prior-results.json'),
                 worker=outer.stable(outer.identity(os.getpid())), original_case=intent,
                 expected='only declared case34 descendants adopted by unchanged outer'))
            subreaper(False)
        with (directory/'raw.log').open('xb') as log:
            child = subprocess.Popen(['/usr/bin/python3','-I','-S','-B',str(H/'control_owner.py')],
                                     cwd=directory, env={}, stdout=log, stderr=subprocess.STDOUT, close_fds=True)
            root_fd = os.pidfd_open(child.pid, 0)
            root_identity = outer.identity(child.pid)
            need(root_identity['ppid'] == os.getpid(), 'control owner is not direct child')
            save(directory/'owner-launch.json', dict(identity=root_identity, pidfd_retained=True))
            nodes = {child.pid: dict(identity=root_identity, fd=root_fd, sent=set())}
            events, adoptions, signals = [], [], []
            actual = None
            observed_wall = observed_mono = None
            while not echild():
                now_wall, now_mono = time.time(), time.monotonic()
                if now_wall >= wall+8 or now_mono >= mono+8:
                    raise TimeoutError('original eight-second case expired; no next case')
                for pid in outer.direct_children():
                    if pid not in nodes:
                        fd = os.pidfd_open(pid, 0)
                        identity = outer.identity(pid)
                        need(identity['ppid'] == os.getpid(), 'adopted control identity mismatch')
                        nodes[pid] = dict(identity=identity, fd=fd, sent=set())
                        adoptions.append(identity)
                for pid, node in nodes.items():
                    if node.get('reaped'):
                        continue
                    signum = None
                    if pid != child.pid or now_wall >= wall+7 or now_mono >= mono+7:
                        signum = signal.SIGKILL
                    elif now_wall >= wall+5 or now_mono >= mono+5:
                        signum = signal.SIGTERM
                    if signum is not None and signum not in node['sent']:
                        node['sent'].add(signum)
                        signals.append(dict(identity=node['identity'], signal=int(signum), wall=now_wall, mono=now_mono))
                        try:
                            signal.pidfd_send_signal(node['fd'], signum)
                        except ProcessLookupError:
                            pass
                while True:
                    try:
                        event = os.waitid(os.P_ALL, 0, os.WEXITED|os.WNOHANG|os.WNOWAIT)
                    except ChildProcessError:
                        break
                    if event is None:
                        break
                    if event.si_pid not in nodes:
                        fd = os.pidfd_open(event.si_pid, 0)
                        identity = outer.identity(event.si_pid)
                        need(identity['ppid'] == os.getpid(), 'waitable adoption identity mismatch')
                        nodes[event.si_pid] = dict(identity=identity, fd=fd, sent=set())
                        adoptions.append(identity)
                    node = nodes[event.si_pid]
                    exact = os.waitid(os.P_PIDFD, node['fd'], os.WEXITED|os.WNOHANG|os.WNOWAIT)
                    need(exact == event, 'external WNOWAIT mismatch')
                    pid, status = os.waitpid(event.si_pid, os.WNOHANG)
                    need(pid == event.si_pid, 'external exact reap mismatch')
                    events.append(dict(identity=node['identity'], code=event.si_code,
                                       status=event.si_status, raw_status=status, wall=time.time(), mono=time.monotonic()))
                    node['reaped'] = True
                    if pid == child.pid:
                        actual = os.waitstatus_to_exitcode(status)
                        child.returncode = actual
                        observed_wall, observed_mono = time.time(), time.monotonic()
                time.sleep(0.001)
            for node in nodes.values():
                os.close(node['fd'])
        external = dict(case=case_id, variant=variant, actual_returncode=actual, echild=echild(),
                        adoptions=adoptions, signals=signals, waits=events,
                        observed_wall=observed_wall, observed_mono=observed_mono,
                        finished_wall=time.time(), finished_mono=time.monotonic())
        save(directory/'external-result.json', external)
        if case_id == 34:
            # The outer may already be terminating this worker. Never call it
            # a pass, never launch another case, and never restore subreaping.
            return 0
        receipt = read(directory/'work/inner-result.json')
        journal_path = directory/'work/ancestry.jsonl'
        journal = [json.loads(line) for line in journal_path.read_text().splitlines()] if journal_path.exists() else []
        check_case(case_id, variant, receipt, journal, actual, external, verifier, intent)
        prior.append(dict(case=case_id, variant=variant, receipt=fact(directory/'work/inner-result.json'),
                          external=fact(directory/'external-result.json')))
    raise RuntimeError('final outer-adoption negative did not occur')


def admitted_suite():
    plan, intent = admission()
    outer = load('outer_owner', OUTER_SHA256)
    save(H/'controller.claim', dict(intent_sha256=sha(H/'suite-intent.json')))
    (H/'work').mkdir(mode=0o700)
    save(H/'outer-identity.json', outer.stable(outer.identity(os.getpid())))
    status = outer.supervise(['/usr/bin/python3','-I','-S','-B',str(H/'control_controller.py'),'--worker'],
                             H, H/'outer', intent['end_wall']-5, intent['end_wall'],
                             dict(phase='graph-c-owner-controls', original_interval=intent))
    receipt = read(H/'outer/receipt.json')
    # Preserve actual outer nonzero. Only the predeclared final case may explain
    # it. This receipt is evidence for independent admission, not a passed report.
    final = read(H/'final-adoption-intent.json')
    release = read(H/'work/case-34/case34-release.json')
    owner = read(H/'work/case-34/owner-launch.json')['identity']
    expected = {release['root']['pid'], owner['pid']} | {
        row['pid'] for row in release['fixture'] if row['event'] == 'created'}
    root = receipt['root']['pid']
    adopted = {row['pid'] for row in receipt['observed_children'] if row['pid'] != root}
    need(status != 0 and receipt['reason'] == 'adopted_descendant' and receipt['cleanup_echild'] and
         not receipt['errors'] and not receipt['cancel_signal'] and receipt['deadline_respected'],
         'outer failure is not the declared adoption rejection')
    native = expected - {owner['pid']}
    need(native <= adopted and adopted <= expected, 'outer adoption set differs from declared fixture')
    external_path = H/'work/case-34/external-result.json'
    actual_inner = read(external_path)['actual_returncode'] if external_path.exists() else None
    if actual_inner is None:
        actual_inner = next((row['exit_status'] for row in receipt['wait_statuses'] if row['pid'] == owner['pid']), None)
    need(actual_inner == 23, 'case34 exact inner exit23 not actually observed')
    need(final['prior_sha256'] == sha(H/'prior-results.json'), 'prior results changed')
    need(time.time() < intent['end_wall'] and time.monotonic() < intent['end_mono'], 'suite final receipt late')
    save(H/'suite-observed.json', dict(status='expected-final-outer-rejection', outer_supervisor_result=status, actual_outer_returncode=None,
         outer_receipt=fact(H/'outer/receipt.json'), prior_results=fact(H/'prior-results.json'),
         final_release=fact(H/'work/case-34/case34-release.json'), wall=time.time(), mono=time.monotonic(),
         acceptance=False))
    return status  # Preserve outer nonzero; the external coordinator observes actual exit.


if __name__ == '__main__':
    if sys.argv[1:] == ['--worker']:
        raise SystemExit(worker())
    if sys.argv[1:] == ['--admitted-suite']:
        raise SystemExit(admitted_suite())
    raise SystemExit('UNARMED: no default launch; independent control-source/artifact/budget admission required')
