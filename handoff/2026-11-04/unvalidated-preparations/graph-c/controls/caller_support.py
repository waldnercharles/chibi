"""UNARMED fixed support for the source-bound production-owner lifecycle copy.

The declared kernel and lifecycle families share this one fixed caller. No arbitrary commands
or production switches. A fixed controller exists; independent source/artifact/budget admission is absent.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import signal
import time

H = Path('/workspace/shared/chibi-graph-c-runtime-20261104-revision01/controls')
PRODUCTION = H.parent
CORE_SHA256 = 'aeb3759d27e523aeb0257e73790ffce8cca89a1e64feb02290a2442d903914b4'
CASE_ID = None
VARIANT = None
GRAPH_SHA256 = None
ARGV = None
S = None
CLOCK = None
END = None
CANCEL = None
CHANNELS = None
RECORDS = []
PENDING = b''
SENT = False
BINDING = None
CORE = None
FAULT_INSTALLED = False
SCAN_COUNT = 0
MAX_SCAN_GAP = 0.0
MAX_SCAN_SECONDS = 0.0
LAST_SCAN = None


def need(ok, reason):
    if not ok:
        raise ValueError(reason)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def identity(pid):
    fields = Path('/proc/' + str(pid) + '/stat').read_text().rsplit(')', 1)[1].split()
    return dict(pid=pid, ppid=int(fields[1]), pgid=int(fields[2]), sid=int(fields[3]), start_ticks=int(fields[19]))


def parent_guard(expected, actual, argv):
    need(expected == actual, 'control owner parent changed')
    need(argv == b'/usr/bin/python3\0-I\0-S\0-B\0' +
         os.fsencode(H/'control_controller.py') + b'\0--worker\0', 'control suite parent argv differs')


def initialize():
    global BINDING
    # The suite controller is fixed; no executable, environment or command can
    # be supplied by this per-case input. Admission is still unarmed in plan.
    plan = json.loads((H/'CONTROL-PLAN.json').read_text())
    need(plan['enabled'] is True, 'UNARMED: control plan is disabled')
    path = H/'control_controller.py'
    need(sha(path) == plan['source_sha256']['control_controller.py'], 'controller source changed')
    spec = importlib.util.spec_from_file_location('fixed_control_controller', path)
    controller = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(controller)
    controller.admission()
    case = json.loads((H/'active-case.json').read_text())
    parent_guard(case['parent'], identity(os.getppid()),
                 Path('/proc/' + str(os.getppid()) + '/cmdline').read_bytes())
    path = H/'control_binding.py'
    need(sha(path) == plan['source_sha256']['control_binding.py'], 'control binding changed')
    spec = importlib.util.spec_from_file_location('fixed_control_binding', path)
    BINDING = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(BINDING)
    return bind_prechecked_case(case, case['case_id'], case['variant'], plan['fixture']['sha256'], BINDING)


def bind_prechecked_case(intent, case_id, variant, artifact_sha256, binding):
    """Called by initialize only after exact independent control admission.

    This does not grant admission or create an interval. Its inputs must already
    be bound by exact source/artifact/outer/controller admission and unique claim.
    """
    global CASE_ID, VARIANT, GRAPH_SHA256, ARGV, S, CLOCK, BINDING, CORE
    need(case_id in binding.KERNEL_CASES + binding.CALLER_CASES, 'not a declared fixture case')
    variants = {26: (None, 'fsync', 'close'), 30: (None, 'mismatch'),
                31: (None, 'open'), 32: (None, 'registration', 'terminal')}
    need(variant in variants.get(case_id, (None,)), 'wrong finite control variant')
    need(intent['end_wall'] - intent['start_wall'] == 125 and intent['end_mono'] - intent['start_mono'] == 125,
         'unchanged core requires original125-second clock shape')
    CASE_ID, VARIANT, GRAPH_SHA256, BINDING = case_id, variant, artifact_sha256, binding
    ARGV = [str(H/'artifacts/fixture'), '--root', str(case_id)]
    label = 'case-%02d' % case_id + (('-' + variant) if variant is not None else '')
    S = H/'work'/label
    CLOCK = intent
    set_clock(intent, 15)
    CORE = binding.load_bound_core(case_id)
    CORE.RUNTIME = S
    return intent, CORE


def set_clock(intent, reserve):
    global CLOCK, END
    CLOCK = intent
    END = 5 if reserve == 15 else 7


def tick():
    if time.time() >= CLOCK['start_wall'] + END or time.monotonic() >= CLOCK['start_mono'] + END:
        raise TimeoutError('original control normal/cleanup cutoff')


def environment():
    return {'PATH': '/usr/bin:/bin', 'LC_ALL': 'C', 'LANG': 'C', 'TZ': 'UTC'}


def bind_cancel(handler):
    global CANCEL
    CANCEL = handler


def note_fault(boundary):
    with (S/'injection-event.json').open('x', encoding='utf-8') as stream:
        json.dump(dict(case=CASE_ID, variant=VARIANT, boundary=boundary,
                       wall=time.time(), mono=time.monotonic()), stream)
        stream.write('\n'); stream.flush(); os.fsync(stream.fileno())


def save(path, value):
    """Same exclusive JSON/write/flush/fsync/close flow, fixed terminal faults."""
    terminal = Path(path).name == 'inner-result.json'
    if terminal and CHANNELS is not None:
        value['control_case'] = CASE_ID
        value['control_variant'] = VARIANT
        value['fixture_evidence'] = RECORDS
        value['control_scan_count'] = SCAN_COUNT
        value['control_max_scan_gap_seconds'] = MAX_SCAN_GAP
        value['control_max_scan_seconds'] = MAX_SCAN_SECONDS
    stream = open(path, 'x', encoding='utf-8')
    try:
        if terminal and CASE_ID == 25:
            CANCEL(signal.SIGTERM, None)  # At the terminal write boundary.
            note_fault('terminal-write')
        json.dump(value, stream, sort_keys=True)
        stream.write('\n')
        stream.flush()
        if terminal and CASE_ID == 26 and VARIANT == 'fsync':
            CANCEL(signal.SIGTERM, None)  # Inside save immediately before fsync.
            note_fault('terminal-fsync')
        os.fsync(stream.fileno())
        if terminal and CASE_ID == 29:
            # Synthetic terminal I/O only. Same original5s normal endpoint;
            # do not add time or alter the production deadline shell.
            time.sleep(max(0.0, CLOCK['start_mono'] + 5.01 - time.monotonic()))
            note_fault('terminal-deadline')
    finally:
        if terminal and CASE_ID == 26 and VARIANT == 'close':
            CANCEL(signal.SIGTERM, None)  # Inside save immediately before close.
            note_fault('terminal-close')
        stream.close()


def prepare_channels():
    global CHANNELS
    command_r, command_w = os.pipe2(os.O_CLOEXEC)
    evidence_r, evidence_w = os.pipe2(os.O_CLOEXEC)
    CHANNELS = (command_r, command_w, evidence_r, evidence_w)
    return CHANNELS


def install_child_channels():
    command_r, command_w, evidence_r, evidence_w = CHANNELS
    # The main opens these before executable/cwd/logs, so descriptors3/4 do not
    # overwrite its executable descriptor. Validate rather than assume this.
    need(command_r == 3 and command_w == 4, 'control fd reservation differs')
    os.dup2(command_r, 3)
    os.dup2(evidence_w, 4)
    os.set_inheritable(3, True)
    os.set_inheritable(4, True)
    for fd in CHANNELS:
        if fd not in (3, 4):
            os.close(fd)


def close_parent_channels():
    return (CHANNELS[0], CHANNELS[3])


def drive(owner):
    global PENDING, SENT, FAULT_INSTALLED, SCAN_COUNT, MAX_SCAN_GAP, LAST_SCAN
    tick()
    PENDING = BINDING.read_fixture(CHANNELS[2], PENDING, RECORDS)
    ready = any(row['event'] == 'fixture_ready' for row in RECORDS)
    if CASE_ID == 36:
        return False  # Descriptor exec fails before any fixture evidence.
    if CASE_ID not in (18, 19) and not ready:
        time.sleep(min(0.001, max(0.0, CLOCK['start_mono'] + 5 - time.monotonic())))
        return True  # Synthetic-only birth gate, never the actual20ms shell.
    if ready and not FAULT_INSTALLED:
        BINDING.apply_kernel_fault(CORE, owner, CASE_ID, (CHANNELS[1], CHANNELS[2]),
                                   RECORDS, CLOCK['start_mono'], VARIANT)
        FAULT_INSTALLED = True
    if CASE_ID in (10, 11) and ready:
        owner.bind(BINDING.fixture_pids(RECORDS)[1])
        raise RuntimeError('retired parent unexpectedly bound')
    if CASE_ID == 19 and not any(row['event'] == 'real20ms' for row in RECORDS):
        time.sleep(0.001)
        return True
    if ready and CASE_ID == 34:
        save(S/'case34-release.json', dict(owner=identity(os.getpid()), root=owner.nodes[owner.root]['first'],
                                        fixture=RECORDS, wall=time.time(), mono=time.monotonic()))
        os._exit(23)  # Intentional leak: unchanged outer must reject adoption.
    if ready and not SENT:
        pids = BINDING.fixture_pids(RECORDS)
        required_roles = (1, 2) if CASE_ID == 2 else (1, 2, 3)
        registered = all(pids[role] in owner.eligible for role in required_roles if role in pids)
        if registered or CASE_ID in (4, 14, 33, 37):
            if CASE_ID == 24:
                CANCEL(signal.SIGTERM, None)
                owner.cancelled = signal.SIGTERM
                owner.check()
            if CASE_ID == 33:
                owner.reject('control cleanup cascade')
            os.write(CHANNELS[1], b'S' if CASE_ID == 16 else b'K')
            SENT = True
    moment = time.monotonic()
    if LAST_SCAN is not None:
        MAX_SCAN_GAP = max(MAX_SCAN_GAP, moment - LAST_SCAN)
    LAST_SCAN = moment
    SCAN_COUNT += 1
    return False


def note_scan_elapsed(seconds):
    global MAX_SCAN_SECONDS
    MAX_SCAN_SECONDS = max(MAX_SCAN_SECONDS, seconds)


def on_failure():
    if CASE_ID == 16 and SENT:
        try:
            os.write(CHANNELS[1], b'L')
        except OSError as error:
            return repr(error)
    return None


def capture_evidence(terminal):
    global PENDING
    if CHANNELS is not None:
        try:
            PENDING = BINDING.read_fixture(CHANNELS[2], PENDING, RECORDS)
        except OSError as error:
            terminal['fixture_read_error'] = repr(error)


if __name__ == '__main__':
    raise SystemExit('UNARMED: fixed lifecycle support, not a launcher')
