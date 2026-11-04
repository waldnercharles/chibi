"""Fixed graph-c inner owner. UNARMED plan and missing independent admissions refuse launch."""
import ctypes
import errno
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import select
import signal
import stat
import sys
import time

H = Path('/workspace/shared/chibi-graph-c-runtime-20261104-revision01')
CANCEL = None


def receive_cancel(number, frame):
    global CANCEL
    if CANCEL is None:
        CANCEL = number


def load_fixed(name, digest):
    path = H/(name+'.py')
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise ValueError('fixed source digest differs: '+name)
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    # No payload or mode from argv. These files are absent in the unarmed packet.
    if len(sys.argv) != 1 or sys.argv[0] != str(H/'inner_owner.py'):
        raise ValueError('fixed inner-owner argv required')
    intent = json.loads((H/'runtime-intent.json').read_text())
    plan_bytes = (H/'plan.json').read_bytes()
    if hashlib.sha256(plan_bytes).hexdigest() != intent['plan_sha256']:
        raise ValueError('runtime intent plan differs')
    plan = json.loads(plan_bytes)
    if plan['runtime_enabled'] is not True:
        raise ValueError('UNARMED: independent runtime admission absent')
    g = load_fixed('guard', plan['pins']['guard.py'])
    g.set_clock(intent,15)
    plan['_digest'] = intent['plan_sha256']
    g.admission(plan,intent)
    inner_intent = g.read(H/'inner-intent.json')
    g.need(inner_intent['runtime_intent_sha256'] == g.sha(H/'runtime-intent.json'), 'inner intent differs')
    g.need(g.identity(os.getppid()) == inner_intent['worker_identity']
           and g.identity(inner_intent['worker_identity']['ppid']) == intent['outer_identity'], 'inner parent chain differs')
    worker_argv = [b'/usr/bin/python3',b'-I',b'-S',b'-B',os.fsencode(H/'run.py'),b'--worker']
    g.need(Path('/proc/'+str(os.getppid())+'/cmdline').read_bytes().split(b'\0') == worker_argv+[b''], 'worker argv differs')
    core = load_fixed('graph_c_attribution',g.CORE_SHA256)
    g.need(len(list(Path('/proc/self/task').iterdir())) == 1, 'sole-thread owner required')
    signal.signal(signal.SIGCHLD,signal.SIG_DFL)
    for number in (signal.SIGTERM,signal.SIGINT,signal.SIGHUP):
        signal.signal(number,receive_cancel)
    signal.pthread_sigmask(signal.SIG_SETMASK,[])
    try:
        os.waitid(os.P_ALL,0,os.WEXITED|os.WNOHANG|os.WNOWAIT)
    except ChildProcessError:
        pass
    else:
        raise RuntimeError('inner owner must begin at ECHILD')
    libc = ctypes.CDLL(None,use_errno=True)
    libc.prctl.argtypes = [ctypes.c_int,ctypes.c_ulong,ctypes.c_ulong,ctypes.c_ulong,ctypes.c_ulong]
    libc.prctl.restype = ctypes.c_int
    if libc.prctl(36,1,0,0,0) != 0:
        raise OSError(ctypes.get_errno(),'PR_SET_CHILD_SUBREAPER')
    enabled = ctypes.c_int()
    if libc.prctl(37,ctypes.addressof(enabled),0,0,0) != 0 or enabled.value != 1:
        raise RuntimeError('PR_GET_CHILD_SUBREAPER verification failed')
    g.need(os.execve in os.supports_fd and hasattr(os,'pidfd_open')
           and hasattr(signal,'pidfd_send_signal'),'required descriptor APIs unavailable')
    g.need(os.getcwd() == str(g.S),'fixed source cwd required')
    for entry in Path('/proc/self/fd').iterdir():
        if int(entry.name) <= 2:
            continue
        try:
            os.readlink(entry)
        except FileNotFoundError:
            continue
        raise RuntimeError('unexpected inherited descriptor')
    g.save(H/'inner.claim',dict(runtime_intent_sha256=g.sha(H/'runtime-intent.json'),
                               worker=inner_intent['worker_identity'],inner=g.identity(os.getpid())))
    root = None
    root_fd = None
    root_identity = None
    owner = None
    constructed = False
    released = False
    descriptors = []
    error = None
    echild = False
    denied = False
    terminal = dict(status='failed',phase='graph-c-inner',start_wall=intent['start_wall'],start_mono=intent['start_mono'],
                    end_wall=intent['end_wall'],end_mono=intent['end_mono'],cleanup_echild=False)
    try:
        g.tick()
        executable = os.open(g.ARGV[0],os.O_RDONLY|os.O_NOFOLLOW|os.O_CLOEXEC)
        descriptors.append(executable)
        info = os.fstat(executable)
        g.need(stat.S_ISREG(info.st_mode) and stat.S_IMODE(info.st_mode) == 0o755,'fixed ELF descriptor mode differs')
        with os.fdopen(os.dup(executable),'rb') as stream:
            g.need(hashlib.file_digest(stream,'sha256').hexdigest() == g.GRAPH_SHA256,'fixed ELF descriptor bytes differ')
        os.lseek(executable,0,os.SEEK_SET)
        cwd = os.open(g.S,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW|os.O_CLOEXEC)
        null = os.open('/dev/null',os.O_RDONLY|os.O_CLOEXEC)
        stdout = os.open(H/'work/graph-c.stdout',os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_CLOEXEC,0o600)
        stderr = os.open(H/'work/graph-c.stderr',os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_CLOEXEC,0o600)
        ready_r,ready_w = os.pipe2(os.O_CLOEXEC)
        go_r,go_w = os.pipe2(os.O_CLOEXEC)
        descriptors.extend((cwd,null,stdout,stderr,ready_r,ready_w,go_r,go_w))
        g.save(H/'work/graph-launch.json',dict(argv=g.ARGV,cwd=str(g.S),environment=g.environment(),
                                             executable_sha256=g.GRAPH_SHA256,clock_pair=intent))
        g.tick()
        if CANCEL is not None:
            raise RuntimeError('cancelled before fork')
        root = os.fork()
        if root == 0:
            try:
                for number in (signal.SIGTERM,signal.SIGINT,signal.SIGHUP,signal.SIGPIPE):
                    signal.signal(number,signal.SIG_DFL)
                signal.pthread_sigmask(signal.SIG_SETMASK,[])
                os.close(ready_r); os.close(go_w)
                os.setsid(); os.fchdir(cwd)
                os.dup2(null,0); os.dup2(stdout,1); os.dup2(stderr,2)
                if os.write(ready_w,b'R') != 1 or os.read(go_r,1) != b'G':
                    os._exit(127)
                os.close(ready_w); os.close(go_r)
                os.execve(executable,g.ARGV,g.environment())
            except BaseException:
                os._exit(127)
        # Handlers cannot throw between fork return and retaining this child.
        os.close(ready_w); descriptors.remove(ready_w)
        os.close(go_r); descriptors.remove(go_r)
        root_fd = os.pidfd_open(root,0)
        root_identity = g.identity(root)
        g.need(root_identity['ppid'] == os.getpid(),'fork root is not owned')
        poller = select.poll(); poller.register(ready_r,select.POLLIN|select.POLLHUP)
        while True:
            g.tick()
            if CANCEL is not None:
                raise RuntimeError('cancelled at pre-exec gate')
            if poller.poll(1):
                g.need(os.read(ready_r,1) == b'R','root failed before pre-exec ready')
                break
        root_identity = g.identity(root)
        g.need(root_identity['ppid'] == os.getpid() and root_identity['pgid'] == root_identity['sid'] == root,
               'root gate did not establish exact fresh session')
        # Keep a partially initialized object reachable if __init__ raises.
        owner = core.GraphCancellation.__new__(core.GraphCancellation)
        core.GraphCancellation.__init__(owner,root,intent['start_wall'],intent['start_mono'],intent['end_wall'],intent['end_mono'])
        constructed = True
        owner.cancelled = CANCEL  # Never lose an earlier pending cancellation.
        owner.check(); g.tick()
        g.save(H/'work/root-identity.json',dict(identity=root_identity,inner_identity=g.identity(os.getpid())))
        owner.cancelled = CANCEL
        owner.check()
        g.need(os.write(go_w,b'G') == 1,'root launch gate write failed')
        released = True
        os.close(go_w); descriptors.remove(go_w)
        while True:
            owner.cancelled = CANCEL
            owner.observe(); owner.drain()
            if owner.root_status is not None and owner.echild:
                owner.finish()
                echild = True
                break
            pause = core.OWNED_POLL_SECONDS if owner.discovery_closed else core.DISCOVERY_SECONDS
            time.sleep(max(0,min(pause,intent['end_wall']-15-time.time(),intent['end_mono']-15-time.monotonic())))
        owner.cancelled = CANCEL
        owner.check()
        terminal.update(status='ownership_passed',root=root_identity,root_status=owner.root_status,
                        shell_pid=owner.shell,accepted_adoptions=sorted(owner.accepted),failure=owner.failure)
    except BaseException as failure:
        error = repr(failure)
        denied = isinstance(failure,OSError) and failure.errno in (errno.EPERM,errno.EACCES,errno.ENOSYS)
        terminal.update(error=error,blocked_action=denied)
        if constructed:
            owner.fail(error)
        # Closing the unopened launch gate is not a signal or alternate launch.
        # Its fixed child exits127 on EOF and cannot have created descendants.
        if not released:
            for fd in list(descriptors):
                if 'go_w' in locals() and fd == go_w:
                    os.close(fd); descriptors.remove(fd)
        while time.time() < intent['end_wall']-10 and time.monotonic() < intent['end_mono']-10:
            try:
                if constructed and not denied:
                    owner.cleanup_once()
                    if owner.echild:
                        echild = True
                        break
                elif root is not None and not released:
                    event = os.waitid(os.P_PID,root,os.WEXITED|os.WNOHANG|os.WNOWAIT)
                    if event is not None:
                        terminal['gate_failure_identity'] = g.identity(root)
                        pid,status = os.waitpid(root,os.WNOHANG)
                        if pid != root:
                            raise RuntimeError('pre-exec root reap mismatch')
                        terminal['gate_failure_wait_status'] = status
                        try:
                            os.waitid(os.P_ALL,0,os.WEXITED|os.WNOHANG|os.WNOWAIT)
                        except ChildProcessError:
                            echild = True
                        break
                elif root is None:
                    try:
                        os.waitid(os.P_ALL,0,os.WEXITED|os.WNOHANG|os.WNOWAIT)
                    except ChildProcessError:
                        echild = True
                    break
                else:
                    break  # Denied/uncertain ownership action: no retry or fallback signal.
            except BaseException as cleanup_error:
                terminal['cleanup_error'] = repr(cleanup_error)
                break
            time.sleep(max(0,min(0.005,intent['end_wall']-10-time.time(),intent['end_mono']-10-time.monotonic())))
    finally:
        terminal['cleanup_echild'] = echild
        terminal['cancel_signal'] = CANCEL
        if owner is not None:
            try:
                if hasattr(owner,'journal'):
                    owner.flush()
                if hasattr(owner,'nodes'):
                    owner.close()
            except BaseException as closing_error:
                terminal['close_error'] = repr(closing_error)
                error = error or repr(closing_error)
        for fd in descriptors + ([root_fd] if root_fd is not None else []):
            try:
                if ('stdout' in locals() and fd == stdout) or ('stderr' in locals() and fd == stderr):
                    os.fsync(fd)
                os.close(fd)
            except OSError as closing_error:
                terminal['descriptor_close_error'] = repr(closing_error)
                error = error or repr(closing_error)
        good = error is None and echild and CANCEL is None and terminal['status'] == 'ownership_passed'
        if owner is not None and getattr(owner,'failure',None) is not None:
            good = False
            terminal['failure'] = owner.failure
        terminal['status'] = 'ownership_passed' if good else 'failed'
        terminal['finished_wall'],terminal['finished_mono'] = time.time(),time.monotonic()
        g.set_clock(intent,15 if good else 10)
        g.save(H/'work/inner-result.json',terminal)
        # A success receipt is provisional until this check and actual exit.
        g.tick()
        if CANCEL is not None:
            return 1
        return 0 if good else 1


if __name__ == '__main__':
    raise SystemExit(main())
