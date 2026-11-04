"""UNARMED bounded read-boundary models. Not kernel/process results.

The independently admitted caller must import separate pinned core variants.
These models call real core method bodies with finite read/wait boundary values.
They do not open /proc, fork, signal, wait for a process, compile, or load a core.
"""
import os
import select
import signal
import time
import types

# Exact patches, applied to separately reviewed copies, never the production core.
MUTATIONS = {
    'without_live_latch': (
        ("            self.fail('live adoption: ' + repr(row))",
         "            pass  # CONTROL MUTATION: omit the live-adoption latch"),
    ),
    'without_eligibility': (
        ("            self.fail('unregistered or root-level shell adoption: ' + str(pid))",
         "            pass  # CONTROL MUTATION: accept missing shell-subtree ancestry"),
        ("            elif (pid not in self.eligible or pid == self.shell or node['signalled'] or",
         "            elif (node['signalled'] or"),
    ),
}

STATUS_VARIANTS = (
    ('exit0', os.CLD_EXITED, 0, 0, 'ineligible adopted terminal status'),
    ('exit7', os.CLD_EXITED, 7, 7 << 8, 'ineligible adopted terminal status'),
    ('sigterm', os.CLD_KILLED, signal.SIGTERM, signal.SIGTERM, 'ineligible adopted terminal status'),
    ('raw_core_disagrees', os.CLD_KILLED, signal.SIGKILL, signal.SIGKILL | 128, 'ineligible adopted terminal status'),
    ('raw_signal_disagrees', os.CLD_KILLED, signal.SIGKILL, signal.SIGTERM, 'ineligible adopted terminal status'),
    ('wnowait_status_disagrees', os.CLD_KILLED, signal.SIGKILL, signal.SIGKILL, 'pidfd and P_ALL wait events disagree'),
    ('wnowait_pid_disagrees', os.CLD_KILLED, signal.SIGKILL, signal.SIGKILL, 'pidfd and P_ALL wait events disagree'),
    ('valid_sigkill', os.CLD_KILLED, signal.SIGKILL, signal.SIGKILL, None),
)
PIDFD_VARIANTS = ('fdinfo_mismatch', 'fdinfo_retired', 'poll_invalid', 'poll_error')
RETIREMENT_VARIANTS = (
    ('readable', [(99, select.POLLIN)], False),
    ('hung_up', [(99, select.POLLHUP)], False),
    ('readable_hung_up', [(99, select.POLLIN | select.POLLHUP)], False),
    ('poll_error', [(99, select.POLLERR)], True),
    ('poll_invalid', [(99, select.POLLNVAL)], True),
    ('mixed_error', [(99, select.POLLIN | select.POLLERR)], True),
    ('empty', [], True),
    ('zero_mask', [(99, 0)], True),
    ('wrong_descriptor', [(100, select.POLLIN)], True),
    ('boolean_descriptor', [(True, select.POLLIN)], True),
    ('duplicate_events', [(99, select.POLLIN), (99, select.POLLHUP)], True),
    ('non_tuple_event', [[99, select.POLLIN]], True),
    ('non_list_result', ((99, select.POLLIN),), True),
)



def blank(core, eligible=True):
    """Finite identity ledger; model owner11/root22/shell33/victim44."""
    state = core.GraphCancellation.__new__(core.GraphCancellation)
    state.owner, state.root, state.shell = 11, 22, 33
    state.ends = (time.time() + 125, time.monotonic() + 125)
    state.failure = state.cancelled = None
    state.nodes, state.owned = {}, {}
    state.eligible = {33: (22,)}
    if eligible:
        state.eligible[44] = (22, 33)
    state.root_status, state.echild = 0, False
    state.accepted, state.events = set(), []
    state.written, state.journal_broken, state.journal = 0, False, None
    state.discovery_closed = True
    # This model excludes journal I/O rather than pretending it was flushed.
    state.flush = lambda: None
    first = dict(pid=44, start=400, ppid=33, pgid=33, sid=22, state='S')
    node = dict(first=first, procfd=98, pidfd=99, reaped=False, signalled=False)
    state.nodes[44] = node
    state.row = lambda fd: dict(first, ppid=11, state='Z')
    state.fd_pid = lambda item: -1 if item['reaped'] else 44
    state.ready = lambda item: True
    return state


def drain_model(core, variant, *, eligible=True, state=None):
    """Exercise capture_owned + both WNOWAIT reads + raw wait + latch."""
    name, code, status, raw_status, expected = variant
    if state is None:
        state = blank(core, eligible)
    event = types.SimpleNamespace(si_pid=44, si_code=code, si_status=status)
    exact = types.SimpleNamespace(**vars(event))
    if name == 'wnowait_status_disagrees':
        exact.si_status = signal.SIGTERM
    if name == 'wnowait_pid_disagrees':
        exact.si_pid = 45
    consumed = False
    wait_calls = []
    def waitid(kind, identifier, flags):
        wait_calls.append(('waitid', kind, identifier, flags))
        if consumed:
            raise ChildProcessError()
        return exact if kind == os.P_PIDFD else event
    def waitpid(pid, flags):
        nonlocal consumed
        wait_calls.append(('waitpid', pid, flags))
        consumed = True
        return pid, raw_status
    original = core.os
    core.os = types.SimpleNamespace(**vars(os))
    core.os.waitid, core.os.waitpid = waitid, waitpid
    try:
        try:
            state.drain()
        except RuntimeError:
            pass
    finally:
        core.os = original
    if expected is None:
        assert state.failure is None and state.echild and state.accepted == {44}
    else:
        assert state.failure is not None and expected in state.failure
    if name.startswith('wnowait_'):
        assert not consumed, 'disagreement must fail before the exact reap'
    return dict(variant=name, failure=state.failure, echild=state.echild,
                calls=wait_calls, events=state.events, model=True)


def anchored_alias_model(core):
    """Same numeric PID/start tick. The old anchor disappears, new one matches.

    If bind reopens /proc/44 for the second stat it will see the replacement and
    erroneously accept. No PID reuse or kernel scheduling is claimed here.
    """
    state = blank(core)
    state.nodes = {}
    opens, closes, reads = [], [], []
    original = core.os
    core.os = types.SimpleNamespace(**vars(os))
    def open_directory(path, flags):
        opens.append(path)
        return 100 + len(opens)
    core.os.open = open_directory
    core.os.close = closes.append
    core.os.pidfd_open = lambda pid, flags: 200
    identity = dict(pid=44, start=400, ppid=33, pgid=33, sid=22, state='S')
    def row(directory):
        reads.append(directory)
        if directory == 101 and len(reads) > 1:
            raise FileNotFoundError('original opened proc identity retired')
        return dict(identity)
    state.row, state.fd_pid = row, lambda node: 44
    caught = None
    try:
        try:
            state.bind(44)
        except FileNotFoundError as error:
            caught = str(error)
    finally:
        core.os = original
    assert caught and reads == [101, 101] and opens == ['/proc/44']
    assert set(closes) == {101, 200} and not state.nodes
    return dict(model=True, reads=reads, opens=opens, closed=closes, rejected=caught)


def pidfd_model(core, variant):
    state = blank(core)
    node = state.nodes[44]
    state.row = lambda fd: dict(node['first'])
    state.ready = core.GraphCancellation.ready.__get__(state)
    state.fd_pid = lambda item: 45 if variant == 'fdinfo_mismatch' else -1 if variant == 'fdinfo_retired' else 44
    original = core.select
    class Poll:
        def register(self, fd, flags):
            assert fd == 99 and flags == select.POLLIN
        def poll(self, milliseconds):
            assert milliseconds == 0
            if variant == 'poll_error':
                raise OSError('synthetic poll error')
            return [(99, select.POLLNVAL)]
    core.select = types.SimpleNamespace(**vars(select))
    core.select.poll = Poll
    caught = None
    try:
        try:
            state.live(node)
        except (RuntimeError, OSError) as error:
            caught = str(error)
            state.fail(caught)  # Exact caller rule for syscall exceptions.
    finally:
        core.select = original
    assert caught and state.failure
    assert not node['reaped'] and not node['signalled']
    return dict(model=True, variant=variant, failure=state.failure)


def retirement_poll_model(core, name, events, expected_rejection):
    """Declared composite read fault, not a reproduced kernel fd race.

    fdinfo reports retired(-1), then polling reports an error bit. The original
    candidate treated any nonempty poll result as readiness. The fresh narrowly
    revised successor must reject both error masks without changing that source.
    """
    state = blank(core)
    node = state.nodes[44]
    node['reaped'] = True
    state.owned = {44: node}
    state.accepted = {44}
    state.echild = True
    state.fd_pid = lambda item: -1
    state.drain = lambda: None  # This finite model targets only final retirement.
    state.ready = core.GraphCancellation.ready.__get__(state)
    original = core.select
    class Poll:
        def register(self, fd, flags):
            assert fd == 99 and flags == select.POLLIN
        def poll(self, timeout):
            return events
    core.select = types.SimpleNamespace(**vars(select))
    core.select.poll = Poll
    rejected = False
    try:
        try:
            state.finish()
        except (RuntimeError, OSError):
            rejected = True
    finally:
        core.select = original
    result = dict(model=True, composite_fault=expected_rejection, variant=name, poll_events=events, rejected=rejected,
                  provisional=any(row['event'] == 'ownership_success_provisional' for row in state.events))
    assert rejected == expected_rejection, ('final retirement readiness disagreement', result)
    assert rejected or result['provisional'], ('supported retirement did not produce provisional evidence', result)
    return result


def cleanup_poll_failure_model(core):
    state = blank(core)
    node = state.nodes[44]
    state.failure = 'original cleanup failure'
    state.drain = lambda cleanup=False: None
    state.snapshot = lambda cleanup=False: [dict(node['first'], ppid=11)]
    state.capture_owned = lambda pid: node
    state.ready = core.GraphCancellation.ready.__get__(state)
    original_select, original_signal = core.select, core.signal
    class Poll:
        def register(self, fd, flags):
            pass
        def poll(self, timeout):
            return [(99, select.POLLERR)]
    def forbidden_signal(*args):
        raise AssertionError('poll failure model attempted a signal')
    core.select = types.SimpleNamespace(**vars(select))
    core.select.poll = Poll
    core.signal = types.SimpleNamespace(**vars(signal))
    core.signal.pidfd_send_signal = forbidden_signal
    rejected = False
    try:
        try:
            state.cleanup_once()
        except RuntimeError:
            rejected = True
    finally:
        core.select, core.signal = original_select, original_signal
    assert rejected and state.failure == 'original cleanup failure'
    assert not node['signalled'] and not node['reaped']
    assert any(row['event'] == 'pidfd_poll_failure' for row in state.events)
    return dict(model=True, first_failure_preserved=state.failure, events=state.events)


def live_latch_model(core, expected_failure):
    state = blank(core)
    # Restore the real parser/latch; a bounded read model supplies two stat rows.
    state.row = core.GraphCancellation.row.__get__(state)
    def stat(state_letter):
        # fields3..22: state, ppid, pgid, sid, fifteen fields7..21, starttime.
        return ('44 (synthetic) ' + state_letter + ' 11 33 22 ' + '0 ' * 15 + '400\n').encode()
    values = iter((stat('S'), stat('Z')))
    state.read = lambda directory, name: next(values)
    first, second = state.row(98), state.row(98)
    state.row = lambda fd: dict(second)
    valid = STATUS_VARIANTS[-1]
    variant = (*valid[:4], 'live adoption' if expected_failure else None)
    terminal = drain_model(core, variant, state=state)
    return dict(first=first, second=second, failure=state.failure, terminal=terminal, model=True)


def check_counterfactuals(production, without_latch, without_eligibility):
    """Requires actual separately hashed mutant modules, not invented outcomes."""
    good = live_latch_model(production, True)
    mutant = live_latch_model(without_latch, False)
    assert good['failure'] and not mutant['failure'] and mutant['terminal']['echild']
    # Complete the no-eligibility mutant's real drain, including ECHILD. The
    # unchanged core must reject before this same victim can become accepted.
    valid = STATUS_VARIANTS[-1]
    altered = drain_model(without_eligibility, valid, eligible=False)
    assert altered['echild'] and altered['failure'] is None
    # Reuse the actual status machinery with an expected missing-registry error.
    bad = drain_model(production, (*valid[:4], 'unregistered or root-level shell adoption'), eligible=False)
    assert bad['failure']
    return dict(latch={'production': good, 'mutant': mutant},
                eligibility={'production': bad, 'mutant': altered},
                model=True, production_acceptance=False)


def clock_model(core, mode):
    state = blank(core)
    original = core.time
    wall, mono = state.ends
    core.time = types.SimpleNamespace(**vars(time))
    core.time.time = lambda: wall - (14 if mode == 'wall' else 100)
    core.time.monotonic = lambda: mono - (14 if mode == 'mono' else 100)
    try:
        try:
            state.check()
        except RuntimeError:
            pass
    finally:
        core.time = original
    assert state.failure == 'original shared normal cutoff'
    assert state.ends == (wall, mono)
    return dict(model=True, mode=mode, ends=state.ends, failure=state.failure)


def run_models(core, without_latch, without_eligibility):
    # This deliberately cannot be used as an unadmitted backdoor runner.
    from control_binding import require_admission
    require_admission()
    results = [drain_model(core, variant) for variant in STATUS_VARIANTS]
    results.append(anchored_alias_model(core))
    results.extend(pidfd_model(core, variant) for variant in PIDFD_VARIANTS)
    results.extend(retirement_poll_model(core, *variant) for variant in RETIREMENT_VARIANTS)
    results.append(cleanup_poll_failure_model(core))
    results.append(check_counterfactuals(core, without_latch, without_eligibility))
    results.extend(clock_model(core, variant) for variant in ('wall', 'mono'))
    return results


if __name__ == '__main__':
    raise SystemExit('UNARMED: models and counterfactuals require independent source admission')
