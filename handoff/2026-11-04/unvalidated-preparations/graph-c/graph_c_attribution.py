"""UNARMED source candidate. No launcher, command option, or executable entry point.

This is only the new graph-c ownership state machine. OWNER-CONTRACT.md is a
mandatory integration boundary, not an implemented or accepted controller.
"""
import hashlib
import json
import os
from pathlib import Path
import select
import signal
import time


PACKAGE = Path('/workspace/shared/chibi-graph-c-owner-candidate-20261104-revision01')
# Proposed successor only: it does not exist and is not created by this source.
RUNTIME = Path('/workspace/shared/chibi-graph-c-runtime-20261104-revision01')
SOURCE = RUNTIME / 'source'
GRAPH = Path('/workspace/shared/chibi-graph-c-20261104-revision01/artifacts/graph-c')
OUTER_SHA256 = 'eb565285001cef43ed88cf73e5797cc83b726afcf6cf192cf5b4ba10f6078698'
SHELL_SHA256 = 'a6f559e00b69a4aa4d8cb607be18d9386c5aee55c509e2c075549dcf00e00fc7'
SHELL_ARGV = (b'/bin/sh\0-c\0(sleep 0.2; printf late > "$1") & wait\0deadline\0'
              + os.fsencode(SOURCE / '.chibi/graph-c/late-child') + b'\0')
MAX_VISIBLE = 256
MAX_IDENTITIES = 64
MAX_ELIGIBLE = 16
MAX_DEPTH = 4
DISCOVERY_SECONDS = 0.001
OWNED_POLL_SECONDS = 0.020


def stable(row):
    return tuple(row[key] for key in ('pid', 'start', 'ppid', 'pgid', 'sid'))


class GraphCancellation:
    """Sole-reaper state, constructed only after the admitted root launch gate.

    root_pid is the one already-owned graph-c child, waiting at a pre-exec pipe
    gate AFTER setsid, not a caller-selected program. This class cannot launch
    anything. The integration must verify that exact child and release its gate.
    No process is stopped, signalled, or waited on to improve registration.
    """

    def __init__(self, root_pid, start_wall, start_mono, end_wall, end_mono):
        self.owner = os.getpid()
        self.root = root_pid
        self.ends = (end_wall, end_mono)
        self.failure = None
        self.cancelled = None
        self.nodes = {}
        self.eligible = {}
        self.owned = {}
        self.shell = None
        self.discovery_closed = False
        self.root_status = None
        self.accepted = set()
        self.echild = False
        self.events = []
        self.written = 0
        self.journal_broken = False
        self.journal = None
        self.check()
        if (end_wall - start_wall != 125 or end_mono - start_mono != 125
                or not start_wall <= time.time() < end_wall
                or not start_mono <= time.monotonic() < end_mono):
            self.reject('original 125-second clock pair differs')
        # Hash only once, before opening the graph-c execution gate.
        with open('/bin/sh', 'rb') as stream:
            if hashlib.file_digest(stream, 'sha256').hexdigest() != SHELL_SHA256:
                self.reject('pinned shell changed')
            info = os.fstat(stream.fileno())
        self.shell_file = (info.st_dev, info.st_ino, info.st_size,
                           info.st_mtime_ns, info.st_ctime_ns)
        self.journal = open(RUNTIME / 'work/ancestry.jsonl', 'x', encoding='utf-8')
        root = self.bind(root_pid)
        if root['first']['ppid'] != self.owner or root['first']['pgid'] != root_pid or root['first']['sid'] != root_pid:
            self.reject('root is not the owned fresh-session launch-gate child')
        self.owned[root_pid] = root
        self.record('root_identity', identity=root['first'])

    def fail(self, reason):
        # Set this before any fallible I/O. Nothing clears it.
        if self.failure is None:
            self.failure = str(reason)
            self.record('failure', reason=self.failure)

    def reject(self, reason):
        self.fail(reason)
        raise RuntimeError(self.failure)

    def check(self, cleanup=False):
        reserve = 10 if cleanup else 15
        if time.time() >= self.ends[0] - reserve or time.monotonic() >= self.ends[1] - reserve:
            self.reject('original shared cleanup cutoff' if cleanup else 'original shared normal cutoff')
        if not cleanup:
            if self.cancelled is not None:
                self.reject('cancelled: ' + str(self.cancelled))
            if self.failure is not None:
                raise RuntimeError(self.failure)

    def record(self, kind, **values):
        self.events.append(dict(event=kind, wall=time.time(), mono=time.monotonic(), **values))

    def flush(self):
        if self.journal_broken or self.journal is None:
            self.fail('journal unavailable')
            return
        try:
            for row in self.events[self.written:]:
                self.journal.write(json.dumps(row, sort_keys=True) + '\n')
            self.journal.flush()
            os.fsync(self.journal.fileno())
            self.written = len(self.events)
        except OSError as error:
            self.journal_broken = True
            self.fail('journal I/O: ' + repr(error))

    def read(self, directory, name, limit=8192):
        fd = os.open(name, os.O_RDONLY | os.O_CLOEXEC, dir_fd=directory)
        try:
            value = os.read(fd, limit + 1)
            if len(value) > limit:
                self.reject('oversized proc input: ' + name)
            return value
        finally:
            os.close(fd)

    def row(self, directory):
        raw = self.read(directory, 'stat')
        left, tail = raw.rsplit(b')', 1)
        fields = tail.split()
        row = dict(pid=int(left.split(b'(', 1)[0]), state=fields[0].decode('ascii'),
                   ppid=int(fields[1]), pgid=int(fields[2]), sid=int(fields[3]), start=int(fields[19]))
        # Earliest observation, before further proc/pidfd reads can see a zombie.
        if row['ppid'] == self.owner and row['pid'] != self.root and row['state'] != 'Z':
            self.fail('live adoption: ' + repr(row))
        return row

    def fd_pid(self, node):
        lines = Path('/proc/self/fdinfo/' + str(node['pidfd'])).read_text().splitlines()
        values = [int(line.split()[1]) for line in lines if line.startswith('Pid:')]
        if len(values) != 1:
            self.reject('missing exact pidfd identity')
        return values[0]

    def ready(self, node):
        poller = select.poll()
        poller.register(node['pidfd'], select.POLLIN)
        events = poller.poll(0)
        if not isinstance(events, list) or len(events) > 1:
            self.record('pidfd_poll_failure', result=repr(events))
            self.reject('unsupported pidfd poll result')
        if not events:
            return False
        event = events[0]
        if (not isinstance(event, tuple) or len(event) != 2
                or type(event[0]) is not int or event[0] != node['pidfd']
                or type(event[1]) is not int
                or event[1] not in (select.POLLIN, select.POLLHUP, select.POLLIN | select.POLLHUP)):
            self.record('pidfd_poll_failure', result=repr(event))
            self.reject('invalid or unsupported pidfd poll event: ' + repr(event))
        return True

    def bind(self, pid):
        if pid in self.nodes:
            return self.nodes[pid]
        if len(self.nodes) >= MAX_IDENTITIES:
            self.reject('identity capacity exceeded')
        directory = os.open('/proc/' + str(pid), os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
        pidfd = None
        try:
            first = self.row(directory)
            pidfd = os.pidfd_open(pid, 0)
            second = self.row(directory)  # Same opened proc directory, never reopened by PID.
            node = dict(first=first, procfd=directory, pidfd=pidfd, reaped=False, signalled=False)
            if stable(first) != stable(second) or first['pid'] != pid or self.fd_pid(node) != pid:
                self.reject('proc-directory/pidfd identity gap')
            self.nodes[pid] = node
            return node
        except BaseException:
            os.close(directory)
            if pidfd is not None:
                os.close(pidfd)
            raise

    def live(self, node):
        self.check()
        row = self.row(node['procfd'])
        if (stable(row) != stable(node['first']) or row['state'] == 'Z'
                or self.fd_pid(node) != row['pid'] or self.ready(node)):
            self.reject('registration lost its original live identity or parent')
        return row

    def register(self, pid, ancestry):
        if len(ancestry) > MAX_DEPTH or len(self.eligible) >= MAX_ELIGIBLE:
            self.reject('deadline-subtree registration bound exceeded')
        node = self.bind(pid)
        chain = [self.nodes[ancestor] for ancestor in ancestry] + [node]
        first = [self.live(item) for item in chain]
        if first[-1]['ppid'] != ancestry[-1]:
            self.reject('candidate no longer belongs to the registered parent')
        group = pid if len(ancestry) == 1 else self.shell
        if any(row['sid'] != self.root for row in first) or first[-1]['pgid'] != group:
            self.reject('deadline-subtree session/group differs')
        witness = dict(wall=time.time(), mono=time.monotonic())
        # At witness time every pinned process was alive: each was read before
        # witness and is proved still the same live identity afterward. Parents
        # cannot die/reparent an edge and then revive on these retained handles.
        second = [self.live(item) for item in chain]
        if [stable(row) for row in first] != [stable(row) for row in second]:
            self.reject('ancestry changed across registration witness')
        self.eligible[pid] = tuple(ancestry)
        self.record('registered', identity=first[-1], ancestry=first[:-1], witness=witness)

    def snapshot(self, cleanup=False):
        rows = []
        # This executor has no task/TID/children interface. Global membership is
        # only discovery; it never establishes eligibility or signal authority.
        with os.scandir('/proc') as entries:
            for entry in entries:
                self.check(cleanup)
                if not entry.name.isdigit():
                    continue
                if len(rows) >= MAX_VISIBLE:
                    self.reject('visible process scan bound exceeded')
                try:
                    directory = os.open(entry.path, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
                    try:
                        rows.append(self.row(directory))
                    finally:
                        os.close(directory)
                except (FileNotFoundError, ProcessLookupError):
                    continue  # No identity is attributed from a disappeared discovery row.
        return rows

    def observe(self):
        rows = self.snapshot()
        for row in rows:
            if row['ppid'] == self.owner:
                self.capture_owned(row['pid'])
        self.check()
        if self.discovery_closed:
            return
        if self.shell is not None and self.ready(self.nodes[self.shell]):
            self.discovery_closed = True
            self.record('discovery_closed', shell=self.shell)
            return
        if self.shell is None:
            for row in rows:
                if row['ppid'] != self.root:
                    continue
                directory = os.open('/proc/' + str(row['pid']), os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
                try:
                    if self.read(directory, 'cmdline') != SHELL_ARGV:
                        continue
                    # Bind again through live registration, then recheck argv/exe
                    # on the retained directory, not this discovery directory.
                    self.register(row['pid'], (self.root,))
                    node = self.nodes[row['pid']]
                    info = os.stat('exe', dir_fd=node['procfd'])
                    if (self.read(node['procfd'], 'cmdline') != SHELL_ARGV or
                            (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns) != self.shell_file):
                        self.reject('deadline-shell argv/executable mismatch')
                    self.live(node)
                    self.shell = row['pid']
                    break
                finally:
                    os.close(directory)
        if self.shell is None:
            return
        # At most MAX_DEPTH passes over <=MAX_VISIBLE rows, not recursive /proc rescans.
        for depth in range(MAX_DEPTH):
            for row in rows:
                pid, parent = row['pid'], row['ppid']
                if pid in self.eligible or parent not in self.eligible:
                    continue
                ancestry = self.eligible[parent] + (parent,)
                self.register(pid, ancestry)

    def capture_owned(self, pid):
        node = self.bind(pid)
        row = self.row(node['procfd'])
        first = node['first']
        if (row['ppid'] != self.owner or self.fd_pid(node) != pid or node['reaped']
                or any(row[key] != first[key] for key in ('pid', 'start', 'pgid', 'sid'))):
            self.reject('not the exact unreaped direct kernel child')
        if pid not in self.owned:
            self.owned[pid] = node
            self.record('adopted_identity', identity=row)
        if pid != self.root and (pid == self.shell or pid not in self.eligible):
            self.fail('unregistered or root-level shell adoption: ' + str(pid))
        return node

    def drain(self, cleanup=False):
        while True:
            self.check(cleanup)
            try:
                event = os.waitid(os.P_ALL, 0, os.WEXITED | os.WNOHANG | os.WNOWAIT)
            except ChildProcessError:
                self.echild = True
                self.record('ECHILD')
                return
            if event is None:
                return
            node = self.capture_owned(event.si_pid)
            exact = os.waitid(os.P_PIDFD, node['pidfd'], os.WEXITED | os.WNOHANG | os.WNOWAIT)
            if exact != event:
                self.reject('pidfd and P_ALL wait events disagree')
            self.record('terminal_before_reap', pid=event.si_pid, code=event.si_code, status=event.si_status)
            self.flush()
            self.check(cleanup)
            pid, status = os.waitpid(event.si_pid, os.WNOHANG)
            if pid != event.si_pid:
                self.reject('exact WNOWAIT child missing at reap')
            node['reaped'] = True
            self.record('reaped', pid=pid, raw_status=status)
            if pid == self.root:
                self.root_status = status
                if event.si_code != os.CLD_EXITED or event.si_status != 0 or status != 0:
                    self.fail('graph-c did not exit 0')
            elif (pid not in self.eligible or pid == self.shell or node['signalled'] or
                    event.si_code != os.CLD_KILLED or event.si_status != signal.SIGKILL or
                    not os.WIFSIGNALED(status) or os.WTERMSIG(status) != signal.SIGKILL or os.WCOREDUMP(status)):
                self.fail('ineligible adopted terminal status: ' + str(pid))
            else:
                self.accepted.add(pid)

    def cleanup_once(self):
        if self.failure is None:
            self.reject('cleanup requested on success path')
        self.drain(cleanup=True)
        for row in self.snapshot(cleanup=True):
            if row['ppid'] != self.owner:
                continue
            node = self.capture_owned(row['pid'])
            if node['signalled'] or self.ready(node):
                continue
            self.record('cleanup_signal_intent', identity=self.row(node['procfd']))
            self.flush()
            self.check(cleanup=True)
            # Ownership was proved after adoption; sole reaping reserves the PID.
            node['signalled'] = True
            try:
                signal.pidfd_send_signal(node['pidfd'], signal.SIGKILL)
            except ProcessLookupError:
                self.record('cleanup_signal_ESRCH', pid=row['pid'])
            # Any other exception escapes. Caller must stop that action, not retry.

    def finish(self):
        self.check()
        self.drain()
        if (not self.echild or self.root_status != 0 or self.shell is None or not self.accepted
                or any(not node['reaped'] or node['signalled'] for node in self.owned.values())):
            self.reject('incomplete graph-c ownership evidence')
        for node in self.nodes.values():
            if self.fd_pid(node) != -1 or not self.ready(node):
                self.reject('registered identity not fully retired')
        self.record('ownership_success_provisional', root_status=0, echild=True,
                    accepted_adoptions=sorted(self.accepted), owner_signals=0)
        self.flush()
        self.check()  # Includes cancellation/deadline/journal failure during terminal I/O.

    def close(self):
        # Integration may call this only after ECHILD or a recorded unresolved
        # failure. Closing descriptors is never evidence of successful cleanup.
        for node in self.nodes.values():
            os.close(node['pidfd'])
            os.close(node['procfd'])
        if self.journal is not None:
            self.journal.close()


if __name__ == '__main__':
    raise SystemExit('UNARMED: source candidate only; independent controls, artifact admission and owner integration are absent')
