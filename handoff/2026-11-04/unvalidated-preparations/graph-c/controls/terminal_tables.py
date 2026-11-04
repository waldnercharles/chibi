"""Finite case38 terminal inputs for the real run.verify_inner callable.

No owner import or execution occurs here. The separately admitted source caller
supplies the pinned verifier. Admission/outer-owner predicates are not imitated.
"""
import copy
import os
import signal


def baseline():
    root = dict(pid=22, ppid=11, pgid=22, sid=22, start=200, state='S')
    shell = dict(pid=33, ppid=22, pgid=33, sid=22, start=300, state='S')
    victim = dict(pid=44, ppid=33, pgid=33, sid=22, start=400, state='S')
    intent = dict(start_wall=1000.0, end_wall=1125.0, start_mono=100.0, end_mono=225.0)
    receipt = dict(status='ownership_passed', cleanup_echild=True, root_status=0,
                   cancel_signal=None, failure=None, root=dict(pid=22, ppid=11, pgid=22, sid=22, start_ticks=200),
                   shell_pid=33, accepted_adoptions=[44], finished_wall=1010.0,
                   finished_mono=110.0, **intent)
    journal = []
    def add(event, **values):
        moment = 100.0 + len(journal) / 100.0
        journal.append(dict(event=event, mono=moment, wall=moment + 900.0, **values))
    add('root_identity', identity=root)
    add('registered', identity=shell, ancestry=[root], witness=dict(mono=100.005, wall=1000.005))
    add('registered', identity=victim, ancestry=[root, shell], witness=dict(mono=100.015, wall=1000.015))
    add('adopted_identity', identity=dict(victim, ppid=11, state='Z'))
    add('terminal_before_reap', pid=44, code=os.CLD_KILLED, status=signal.SIGKILL)
    add('reaped', pid=44, raw_status=signal.SIGKILL)
    add('terminal_before_reap', pid=22, code=os.CLD_EXITED, status=0)
    add('reaped', pid=22, raw_status=0)
    add('ECHILD')
    add('ownership_success_provisional', root_status=0, echild=True, owner_signals=0, accepted_adoptions=[44])
    return receipt, journal, 0, 1011.0, 111.0, intent


def terminal_cases():
    """Each alternative changes one named fact; no process-launch multiplier."""
    rows = [('valid', baseline(), False)]
    for name in ('receipt_missing', 'actual_exit_nonzero', 'actual_exit_missing',
                 'echild_missing', 'failure_event', 'live_adoption', 'raw_disagrees',
                 'wall_late', 'mono_late', 'wall_receipt_late', 'mono_receipt_late',
                 'clock_reset', 'owner_signal', 'missing_registry',
                 'journal_wall_late', 'journal_mono_late', 'post_success_adoption',
                 'root_group', 'root_session', 'ancestry_root_start', 'witness_wall_before_start'):
        values = list(copy.deepcopy(baseline()))
        receipt, journal, status, wall, mono, intent = values
        if name == 'receipt_missing': values[0] = None
        elif name == 'actual_exit_nonzero': values[2] = 1
        elif name == 'actual_exit_missing': values[2] = None
        elif name == 'echild_missing':
            receipt['cleanup_echild'] = False
            journal[:] = [r for r in journal if r['event'] != 'ECHILD']
        elif name == 'failure_event': journal[3]['event'] = 'failure'
        elif name == 'live_adoption': journal[3]['identity']['state'] = 'S'
        elif name == 'raw_disagrees': journal[5]['raw_status'] = signal.SIGTERM
        elif name == 'wall_late': values[3] = 1115.0
        elif name == 'mono_late': values[4] = 215.0
        elif name == 'wall_receipt_late': receipt['finished_wall'] = 1110.0
        elif name == 'mono_receipt_late': receipt['finished_mono'] = 210.0
        elif name == 'clock_reset': receipt['start_mono'] = 101.0
        elif name == 'ancestry_root_start':
            journal[2]['ancestry'] = copy.deepcopy(journal[2]['ancestry'])
            journal[2]['ancestry'][0]['start'] += 1
        elif name == 'witness_wall_before_start': journal[2]['witness']['wall'] = 999.0
        elif name == 'root_group': receipt['root']['pgid'] = 33
        elif name == 'root_session': receipt['root']['sid'] = 33
        elif name == 'owner_signal': journal[-1]['owner_signals'] = 1
        elif name == 'missing_registry': del journal[2]
        elif name == 'journal_wall_late': journal[4]['wall'] = 1110.0
        elif name == 'journal_mono_late': journal[4]['mono'] = 210.0
        elif name == 'post_success_adoption':
            # A second fully consistent registered/adopted victim appears only
            # after provisional success. No subsequent ECHILD/success exists.
            second = dict(pid=55, ppid=33, pgid=33, sid=22, start=500, state='S')
            ancestry = copy.deepcopy(journal[2]['ancestry'])
            journal.extend([
                dict(event='registered', mono=100.10, wall=1000.10, identity=second,
                     ancestry=ancestry, witness=dict(mono=100.095, wall=1000.095)),
                dict(event='adopted_identity', mono=100.11, wall=1000.11,
                     identity=dict(second, ppid=11, state='Z')),
                dict(event='terminal_before_reap', mono=100.12, wall=1000.12,
                     pid=55, code=os.CLD_KILLED, status=signal.SIGKILL),
                dict(event='reaped', mono=100.13, wall=1000.13, pid=55, raw_status=signal.SIGKILL),
            ])
            receipt['accepted_adoptions'].append(55)
        rows.append((name, tuple(values), True))
    return rows


def run_terminal_tables(verify_inner):
    from control_binding import require_admission
    require_admission()
    results = []
    for name, values, reject in terminal_cases():
        error = None
        try:
            verify_inner(*values)
        except (ValueError, KeyError, TypeError) as failure:
            error = str(failure)
        assert (error is not None) == reject, (name, reject, error)
        results.append(dict(variant=name, rejected=error is not None, reason=error, model=True))
    return results


if __name__ == '__main__':
    raise SystemExit('UNARMED: terminal verifier models require independent admission')
