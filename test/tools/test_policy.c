#include "support/xalloc.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <glob.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static char directory[PATH_MAX], driver[PATH_MAX], logpath[PATH_MAX];

/* Python only invokes the production supervisor and injects unavailable host
 * capabilities. Native processes supply fixtures; pico_unit owns assertions. */
static const char
    driver_imports[] = "import json, os, signal, subprocess, sys, time\n"
                       "from pathlib import Path\n"
                       "sys.path.insert(0, sys.argv[1])\n"
                       "import test_policy as policy\n";
static const char driver_setup
    [] = "policy.__file__ = os.path.join(os.path.dirname(__file__), "
         "'supervisor.py')\n"
         "signal.signal(signal.SIGTERM, policy.terminate)\n"
         "signal.signal(signal.SIGINT, policy.terminate)\n"
         "mode = os.environ.get('CHIBI_POLICY_FIXTURE', '')\n"
         "def unavailable():\n"
         "    raise OSError('fixture: discovery unavailable')\n"
         "if mode == 'portable':\n"
         "    policy.enable_subreaper = lambda: False\n"
         "    cleanup = policy.cleanup_subtree\n"
         /* A parent's deadline can send SIGTERM as the nested supervisor starts
            its finally block. Inject it at that boundary without a timing race. */
         "    def interrupted_cleanup(child):\n"
         "        open(os.path.join(os.path.dirname(__file__), "
         "'cleanup-entered'), 'w').close()\n"
         "        os.kill(os.getpid(), signal.SIGTERM)\n"
         "        cleanup(child)\n"
         "    policy.cleanup_subtree = interrupted_cleanup\n"
         "if mode in ('missing-proc', 'no-reader'): policy.proc_children = "
         "unavailable\n"
         /* Keep the real ps output and PID, delaying only its startup. */
         "if mode in ('slow-ps', 'hung-ps'):\n"
         "    policy.enable_subreaper = lambda: False\n"
         "    original_popen = subprocess.Popen\n"
         "    def delayed_ps(command, *args, **kwargs):\n"
         "        if command == ['ps', '-A', '-o', 'pid=,ppid=']:\n"
         "            command = [sys.argv[3], 'policy-ps', sys.argv[4],\n"
         "                       '1.2' if mode == 'slow-ps' else '30']\n"
         "        return original_popen(command, *args, **kwargs)\n"
         "    policy.subprocess.Popen = delayed_ps\n"
         /* signal.signal runs pending handlers before it installs a new one. Model
            the caller's SIGTERM still pending as cleanup starts to ignore it. */
         "if mode == 'pending':\n"
         "    class PendingSignal:\n"
         "        pending = True\n"
         "        def __getattr__(self, name): return getattr(signal, name)\n"
         "        def signal(self, number, handler):\n"
         "            if self.pending:\n"
         "                self.pending = False\n"
         "                raise policy.TerminationRequested()\n"
         "            return signal.signal(number, handler)\n"
         "    policy.signal = PendingSignal()\n"
         "if mode == 'no-reader':\n"
         "    policy.enable_subreaper = lambda: True\n"
         "    policy.process_children = unavailable\n";
static const char driver_routes
    [] = "if sys.argv[2] == 'asset-host-platforms':\n"
         "    print(' '.join(policy.ASSET_COMPILER_HOST_PLATFORMS))\n"
         "    sys.exit(0)\n"
         "if sys.argv[2] == 'bootstrap-route':\n"
         "    command = policy.tool_command('compiler-bootstrap')\n"
         "    print(len(command), Path(command[0]).name, command[1],\n"
         "          Path(command[2]).name, Path(command[3]).name,\n"
         "          policy.GROUPS['language'].count('compiler-bootstrap'))\n"
         "    sys.exit(0)\n"
         "if sys.argv[2] == 'cpu-route':\n"
         "    import agent\n"
         "    selector = sys.argv[5]\n"
         "    if selector not in agent.runnable_selectors(agent.CPU, "
         "agent.CPU_SELECTORS):\n"
         "        sys.exit(2)\n"
         "    command = agent.selector_command(selector, agent.CPU_SELECTORS)\n"
         "    print(Path(command[0]).name, command[-1])\n"
         "    sys.exit(0)\n"
         /* Main CPU/Wasm groups own unique selectors; Release chibi_ctl, chibi
          * and chibi-compile users belong to the target that builds them. */
         "if sys.argv[2] == 'misplaced-selectors':\n"
         "    names = [n for key in ('native', 'materials', 'projects', "
         "'language', 'wasm')\n"
         "             for n in policy.GROUPS[key]]\n"
         "    ctl = {n for n, command in policy.TOOLS.items()\n"
         "           if any(Path(a).name in ('chibi_ctl', 'chibi', "
         "'chibi-compile') for a in command[1:])}\n"
         "    print(' '.join(sorted({n for n in names if names.count(n) > 1} "
         "|\n"
         "        {n for n in ctl & set(names) if n not in "
         "policy.GROUPS['projects']})))\n"
         "    print(policy.GROUPS['language'].count('language-row-array'),\n"
         "          policy.GROUPS['wasm'].count('wasm-row-array'),\n"
         "          "
         "policy.GROUPS['language'].count('language-outliner-projection'),\n"
         "          policy.GROUPS['wasm'].count('wasm-outliner-projection'))\n"
         "    sys.exit(0)\n";
/* Execute the workflow's group commands through the real Makefile, recording
 * only its build/test boundaries. No engine build is needed for this check. */
static const char driver_ci
    [] = "if sys.argv[2] == 'ci-groups':\n"
         "    import shlex\n"
         "    root = Path(sys.argv[4])\n"
         "    trace = root / 'ci-trace'\n"
         "    recorder = root / 'ci-record'\n"
         "    recorder.write_text('#!/bin/sh\\n'\n"
         "        'case \"$1\" in tools/test_policy.py) step=$3;; *) step=$1;; "
         "esac\\n'\n"
         "        'printf \"%s\\\\n\" \"$step\" >> \"$CI_TRACE\"\\n'\n"
         "        '[ \"$step\" != \"$CI_FAIL\" ]\\n')\n"
         "    recorder.chmod(0o700)\n"
         "    python = root / 'python3'\n"
         "    if not python.exists(): python.symlink_to(recorder)\n"
         "    env = dict(os.environ, PATH=str(root) + os.pathsep + "
         "os.environ['PATH'],\n"
         "               CI_TRACE=str(trace), CI_FAIL=sys.argv[5], "
         "MAKEFLAGS='', MFLAGS='')\n"
         "    jobs = {'native': [], 'native-macos': []}\n"
         "    job = None\n"
         "    for line in "
         "Path('.github/workflows/ci.yml').read_text().splitlines():\n"
         "        if line.startswith('  ') and not line.startswith('   '):\n"
         "            job = line.strip().removesuffix(':')\n"
         "        command = line.strip().removeprefix('run: ')\n"
         "        if job in jobs and command.startswith('make ') and any(\n"
         "            target in shlex.split(command) for target in\n"
         "            ('test-native', 'test-materials', 'test-projects', "
         "'test-language-all')):\n"
         "            jobs[job].append(command)\n"
         "    for commands in jobs.values():\n"
         "        trace.write_text('')\n"
         "        status = 0\n"
         "        for command in commands:\n"
         "            result = subprocess.run([*shlex.split(command), 'MAKE=' "
         "+ str(recorder)],\n"
         "                env=env, stdout=subprocess.DEVNULL, "
         "stderr=subprocess.DEVNULL)\n"
         "            status = result.returncode\n"
         "            if status: break\n"
         "        print('status=' + str(status))\n"
         "        print(trace.read_text(), end='')\n"
         "    python.unlink()\n"
         "    sys.exit(0)\n";
/* Record the initial workflow builds through the real Makefile on both routes. */
static const char driver_ci_build
    [] = "if sys.argv[2] == 'ci-initial-build':\n"
         "    import shlex\n"
         "    root = Path(sys.argv[4])\n"
         "    trace = root / 'ci-build-trace'\n"
         "    recorder = root / 'ci-build-record.py'\n"
         "    recorder.write_text('import json, os, sys\\n'\n"
         "        'with open(os.environ[\"CI_TRACE\"], \"a\") as log: "
         "log.write(json.dumps(sys.argv[1:]) + \"\\\\n\")\\n'\n"
         "        'sys.exit(1 if sys.argv[1] == os.environ[\"CI_FAIL\"] else "
         "0)\\n')\n"
         "    host = root / 'ci-build-host'\n"
         "    cold = sys.argv[5].startswith('cold:')\n"
         "    failure = sys.argv[5].removeprefix('cold:')\n"
         "    if cold:\n"
         "        host.unlink(missing_ok=True)\n"
         "    else:\n"
         "        host.write_text('#!/bin/sh\\n[ \"$1\" = --protocol-version ] "
         "|| exit 99\\nprintf \"9\\\\n\"\\n')\n"
         "        host.chmod(0o700)\n"
         "    (root / 'Makefile').write_bytes(Path('Makefile').read_bytes())\n"
         "    env = dict(os.environ, CI_TRACE=str(trace), CI_FAIL=failure, "
         "MAKEFLAGS='', MFLAGS='')\n"
         "    jobs = {'native': [], 'native-macos': []}\n"
         "    job = None\n"
         "    for line in "
         "Path('.github/workflows/ci.yml').read_text().splitlines():\n"
         "        if line.startswith('  ') and not line.startswith('   '):\n"
         "            job = line.strip().removesuffix(':')\n"
         "        command = line.strip().removeprefix('run: ')\n"
         "        if job in jobs and command.startswith('make '):\n"
         "            words = shlex.split(command)\n"
         "            if words[1] in ('test-build', 'build'): "
         "jobs[job].append(words)\n"
         "    for job, commands in jobs.items():\n"
         "        trace.write_text('')\n"
         "        status = 0\n"
         "        for command in commands:\n"
         "            result = subprocess.run([command[0], '-C', str(root),\n"
         "                *command[1:], 'BUILD_HOST=' + str(host),\n"
         "                'QUIET=' + sys.executable + ' ' + str(recorder), "
         "'JOBS=2',\n"
         "                'GRAPH_BUILD=' + ('Darwin' if job == 'native-macos' "
         "else '')],\n"
         "                env=env, stdout=subprocess.DEVNULL, "
         "stderr=subprocess.DEVNULL)\n"
         "            status = result.returncode\n"
         "            if status: break\n"
         "        print(job, 'failed' if status else 'continued')\n"
         "        for line in trace.read_text().splitlines():\n"
         "            arguments = json.loads(line)\n"
         "            step = arguments[0]\n"
         "            print(step)\n"
         "            if step == 'graph':\n"
         "                print('targets=' + ','.join(arguments[i + 1] for i, "
         "arg in enumerate(arguments) if arg == '--target'))\n"
         "                print('profile=' + "
         "arguments[arguments.index('--profile') + 1])\n"
         "                print('jobs=' + arguments[arguments.index('--jobs') "
         "+ 1])\n"
         "            if step == 'configure-native':\n"
         "                print('flags=' + ','.join(arg for arg in arguments "
         "if arg.startswith(('-DCMAKE_BUILD_TYPE=', "
         "'-DCHIBI_ENABLE_SANITIZERS=', '-DCHIBI_SANITIZERS='))))\n"
         "            if step == 'build':\n"
         "                print('targets=' + "
         "','.join(arguments[arguments.index('--target') + "
         "1:arguments.index('--parallel')]))\n"
         "                print('jobs=' + "
         "arguments[arguments.index('--parallel') + 1])\n"
         "            if step == 'native-toolchain':\n"
         "                print('targets=' + ','.join(arguments[i + 1] for i, "
         "arg in enumerate(arguments) if arg == '--target'))\n"
         "                print('flags=' + ','.join(arg for arg in arguments "
         "if arg.startswith(('-DCMAKE_BUILD_TYPE=', "
         "'-DCHIBI_ENABLE_SANITIZERS=', '-DCHIBI_SANITIZERS='))))\n"
         "                print('jobs=' + arguments[arguments.index('--jobs') "
         "+ 1])\n"
         "    sys.exit(0)\n";
/* Inject only the expensive workload; exercise the experiment's real CLI
 * handlers and policy supervisor over the existing native worker. */
static const char driver_experiment
    [] = "if sys.argv[2].startswith('experiment-'):\n"
         "    import debug_optimization_experiment as experiment\n"
         "    action, worker, root = sys.argv[2:5]\n"
         "    if action == 'experiment-flags':\n"
         "        options = json.loads(sys.argv[5])\n"
         "        experiment.flags(**options)\n"
         "        sys.exit(0)\n"
         "    if action == 'experiment-verify':\n"
         "        experiment.verify(Path(root) / 'build', Path(root), 1)\n"
         "        sys.exit(0)\n"
         "    if action == 'experiment-inner':\n"
         "        signal.signal(signal.SIGTERM, signal.SIG_DFL)\n"
         "        signal.signal(signal.SIGINT, signal.SIG_DFL)\n"
         "        def workload():\n"
         "            detached = '1' if sys.platform == 'linux' else '0'\n"
         "            orphan = '1' if sys.argv[5] == 'error' else '0'\n"
         "            deadline = time.monotonic() + (.3 if sys.argv[5] == "
         "'timeout' else 4)\n"
         "            experiment.run_phase(Path(root), deadline, sys.argv[5],\n"
         "                [worker, 'descendant', root, detached, orphan], "
         "dict(os.environ))\n"
         "            if orphan == '1':\n"
         "                experiment.run_phase(Path(root), deadline, "
         "'error-failure',\n"
         "                    [worker, 'exit', '7'], dict(os.environ))\n"
         "        experiment.main = workload\n"
         "        sys.exit(experiment.cli())\n"
         "    child = subprocess.Popen([sys.executable, __file__, "
         "sys.argv[1],\n"
         "        'experiment-inner', worker, root, sys.argv[5]])\n"
         "    try:\n"
         "        if sys.argv[5] == 'cancel':\n"
         "            deadline = time.monotonic() + 4\n"
         "            while not os.path.exists(root + '/started'):\n"
         "                if child.poll() is not None or time.monotonic() >= "
         "deadline:\n"
         "                    raise RuntimeError('experiment worker did not "
         "start')\n"
         "                time.sleep(.001)\n"
         "            child.send_signal(signal.SIGTERM)\n"
         "        status = child.wait(timeout=4)\n"
         "        subprocess.run([worker, 'cleanup-probe', root], check=True)\n"
         "        sys.exit(status)\n"
         "    finally:\n"
         "        if child.poll() is None: child.terminate()\n"
         "        child.wait(timeout=4)\n";
static const char driver_parallel[] =
    "if len(sys.argv) > 2 and sys.argv[2].startswith('parallel-'):\n"
    "    action, worker, root = sys.argv[2:5]\n"
    "    root = str(Path(root) / 'parallel')\n"
    "    Path(root).mkdir(exist_ok=True)\n"
    "    import contextlib\n"
    "    original_group = policy.run_group\n"
    "    def reported_group(*args, **kwargs):\n"
    "        try:\n"
    "            with open(Path(root, 'diagnostics.stderr'), 'w') as "
    "diagnostics:\n"
    "                with contextlib.redirect_stderr(diagnostics):\n"
    "                    return original_group(*args, **kwargs)\n"
    "        finally:\n"
    "            rows = [json.loads(line[len('Test policy: '):])\n"
    "                for line in Path(root, "
    "'diagnostics.stderr').read_text().splitlines()\n"
    "                if line.startswith('Test policy: ')]\n"
    "            with open(Path(root, 'metrics.txt'), 'w') as metrics:\n"
    "                for row in rows:\n"
    "                    event = row['event']\n"
    "                    if event == 'admission':\n"
    "                        print(event, row['selected_jobs'], row['mode'], "
    "row['reason'],\n"
    "                            row.get('available_bytes'), "
    "row.get('reserve_bytes'),\n"
    "                            row.get('cpu_slots'), "
    "row.get('memory_probe'), file=metrics)\n"
    "                    if event == 'selector':\n"
    "                        print(event, row['name'], row.get('start'), "
    "row.get('observed_finish'),\n"
    "                            row['status'], row.get('exit_code'), "
    "file=metrics)\n"
    "                    if event == 'group':\n"
    "                        print(event, row.get('peak_live_workers'), "
    "row['wall_seconds'],\n"
    "                            row['result'], row.get('lifecycle_scope'), "
    "file=metrics)\n"
    "    policy.run_group = reported_group\n"
    "    names = ['first', 'second', 'third']\n"
    "    modes = ['mark', 'mark', 'mark']\n"
    "    jobs = 1 if action.endswith('-serial') else 2\n"
    "    if action.startswith('parallel-language'):\n"
    "        names = list(policy.GROUPS['language'])\n"
    "        modes = ['mark'] * len(names)\n"
    "    if action == 'parallel-default':\n"
    "        jobs = None\n"
    "        policy.os.sched_getaffinity = lambda pid: {0, 1}\n"
    "        policy.os.cpu_count = lambda: 2\n"
    "        policy.available_test_memory = lambda: 4 * 1024 ** 3\n"
    "    if action in ('parallel-barrier', 'parallel-default'):\n"
    "        names, modes = names[:2], ['barrier', 'barrier']\n"
    "    if action == 'parallel-conflict':\n"
    "        modes = ['hold', 'conflict', 'mark']\n"
    "    if action == 'parallel-failure':\n"
    "        modes = ['mark', 'fail', 'mark']\n"
    "    if action == 'parallel-unknown':\n"
    "        modes = ['hold', 'conflict', 'mark']\n"
    "    for name, mode in zip(names, modes):\n"
    "        policy.TOOLS[name] = [worker, 'parallel', root, name, mode]\n"
    "        if not action.startswith('parallel-language'):\n"
    "            policy.PARALLEL_RESOURCES[name] = frozenset()\n"
    "    if action == 'parallel-conflict':\n"
    "        for name in names: policy.PARALLEL_RESOURCES[name] = "
    "frozenset(['fixture'])\n"
    "    if action in ('parallel-unknown', 'parallel-failure'):\n"
    "        for name in names[1:]: policy.PARALLEL_RESOURCES.pop(name, None)\n"
    "    if action in ('parallel-low-memory', 'parallel-memory-unknown', "
    "'parallel-all-unknown'):\n"
    "        jobs = None\n"
    "        policy.os.sched_getaffinity = lambda pid: {0, 1}\n"
    "        policy.available_test_memory = lambda: None if action == "
    "'parallel-memory-unknown' else 3 * 1024 ** 3\n"
    "        if action == 'parallel-all-unknown':\n"
    "            for name in names: policy.PARALLEL_RESOURCES.pop(name, None)\n"
    "    if action in ('parallel-conflict', 'parallel-unknown'):\n"
    "        original = policy.subprocess.Popen\n"
    "        spawned = []\n"
    "        class ReleaseOnPoll:\n"
    "            def __init__(self, child): self.child = child\n"
    "            def __getattr__(self, name): return getattr(self.child, "
    "name)\n"
    "            def poll(self):\n"
    "                Path(root, 'resource-release').touch()\n"
    "                return self.child.poll()\n"
    "        def observed_spawn(*args, **kwargs):\n"
    "            if spawned and spawned[0].poll() is None:\n"
    "                Path(root, 'conflict-live').touch()\n"
    "            child = original(*args, **kwargs)\n"
    "            spawned.append(child)\n"
    "            return ReleaseOnPoll(child) if len(spawned) == 1 else child\n"
    "        policy.subprocess.Popen = observed_spawn\n"
    "    if action == 'parallel-memory-reader':\n"
    "        policy.sys.platform = 'linux'\n"
    "        files = {'/proc/meminfo': 'MemAvailable: 8388608 kB',\n"
    "            '/proc/self/cgroup': '0::/user/job',\n"
    "            '/proc/self/mountinfo': '1 0 0:1 / /sys/fs/cgroup rw - "
    "cgroup2 cgroup rw',\n"
    "            '/sys/fs/cgroup/user/job/memory.max': str(6 * 1024 ** 3),\n"
    "            '/sys/fs/cgroup/user/job/memory.current': str(1024 ** 3),\n"
    "            '/sys/fs/cgroup/user/memory.max': str(4 * 1024 ** 3),\n"
    "            '/sys/fs/cgroup/user/memory.current': str(1024 ** 3)}\n"
    "        def read_text(name):\n"
    "            if name not in files: raise FileNotFoundError(name)\n"
    "            return files[name]\n"
    "        print(policy.available_test_memory(read_text))\n"
    "        files['/sys/fs/cgroup/user/job/memory.current'] = 'bad'\n"
    "        print(policy.available_test_memory(read_text))\n"
    "        files['/sys/fs/cgroup/user/job/memory.current'] = str(1024 ** 3)\n"
    "        del files['/sys/fs/cgroup/user/job/memory.max']\n"
    "        print(policy.available_test_memory(read_text))\n"
    "        files['/proc/self/mountinfo'] = '1 0 0:1 /user /sys/fs/cgroup rw "
    "- cgroup2 cgroup rw'\n"
    "        print(policy.available_test_memory(read_text))\n"
    "        files.update({'/proc/self/cgroup': '2:memory:/job',\n"
    "            '/proc/self/mountinfo': '1 0 0:1 / /sys/fs/cgroup/memory rw - "
    "cgroup cgroup rw,memory',\n"
    "            '/sys/fs/cgroup/memory/job/memory.limit_in_bytes': str(2 * "
    "1024 ** 3),\n"
    "            '/sys/fs/cgroup/memory/job/memory.usage_in_bytes': str(1024 "
    "** 3),\n"
    "            '/sys/fs/cgroup/memory/memory.limit_in_bytes': str(8 * 1024 "
    "** 3),\n"
    "            '/sys/fs/cgroup/memory/memory.usage_in_bytes': str(2 * 1024 "
    "** 3)})\n"
    "        print(policy.available_test_memory(read_text))\n"
    "        from types import SimpleNamespace\n"
    "        policy.sys.platform = 'darwin'\n"
    "        output = 'Mach Virtual Memory Statistics: (page size of 4096 "
    "bytes)\\nPages free: 10.\\nPages inactive: 20.\\nPages speculative: "
    "2.\\n'\n"
    "        policy.subprocess.run = lambda *args, **kwargs: "
    "SimpleNamespace(stdout=output)\n"
    "        print(policy.available_test_memory(read_text))\n"
    "        output = 'invalid'\n"
    "        print(policy.available_test_memory(read_text))\n"
    "        sys.exit(0)\n"
    "    if action in ('parallel-cli-serial', 'parallel-env-serial'):\n"
    "        policy.GROUPS['fixture'] = names\n"
    "        policy.os.sched_getaffinity = lambda pid: {0, 1}\n"
    "        policy.available_test_memory = lambda: 4 * 1024 ** 3\n"
    "        os.environ['CHIBI_TEST_JOBS'] = '2' if action == "
    "'parallel-cli-serial' else '1'\n"
    "        sys.argv = [sys.argv[0], *(['--jobs', '1'] if action == "
    "'parallel-cli-serial' else []), 'group', 'fixture']\n"
    "        sys.exit(policy.main())\n"
    "    if action == 'parallel-admission':\n"
    "        policy.os.sched_getaffinity = lambda pid: {0, 1}\n"
    "        policy.os.cpu_count = lambda: 2\n"
    "        for memory in [None, 3 * 1024 ** 3, 4 * 1024 ** 3]:\n"
    "            policy.available_test_memory = lambda: memory\n"
    "            print(policy.automatic_jobs())\n"
    "        policy.os.sched_getaffinity = lambda pid: {0}\n"
    "        policy.os.cpu_count = lambda: 1\n"
    "        print(policy.automatic_jobs())\n"
    "        sys.exit(0)\n"
    "    if action in ('parallel-spawn-int', 'parallel-spawn-term'):\n"
    "        sibling = subprocess.Popen([worker, 'sibling', root])\n"
    "        original = policy.subprocess.Popen\n"
    "        spawned = []\n"
    "        def interrupted_spawn(*args, **kwargs):\n"
    "            child = original(*args, **kwargs)\n"
    "            spawned.append(child)\n"
    "            Path(root, 'started').write_text(str(child.pid))\n"
    "            os.kill(os.getpid(), signal.SIGINT if action.endswith('-int') "
    "else signal.SIGTERM)\n"
    "            return child\n"
    "        policy.subprocess.Popen = interrupted_spawn\n"
    "        try:\n"
    "            result = policy.run_group(names, jobs=jobs)\n"
    "        except (policy.TerminationRequested, KeyboardInterrupt):\n"
    "            result = 130\n"
    "        finally:\n"
    "            policy.subprocess.Popen = original\n"
    "            probe = subprocess.run([worker, 'cleanup-probe', "
    "root]).returncode\n"
    "            print('ownership-probe=' + str(probe))\n"
    "            for child in spawned:\n"
    "                if child.poll() is None:\n"
    "                    os.killpg(child.pid, signal.SIGKILL)\n"
    "                    child.wait()\n"
    "            Path(root, 'sibling-release').touch()\n"
    "            sibling.wait(timeout=6)\n"
    "        sys.exit(result)\n"
    "    if action == 'parallel-active-failure':\n"
    "        Path(root, 'b').mkdir()\n"
    "        policy.TOOLS['first'] = [worker, 'parallel', root, 'first', "
    "'fail-active']\n"
    "        policy.TOOLS['second'] = [worker, 'descendant', str(Path(root, "
    "'b')),\n"
    "            '1' if sys.platform.startswith('linux') else '0', '0']\n"
    "        result = policy.run_group(names, jobs=jobs)\n"
    "        subprocess.run([worker, 'cleanup-probe', str(Path(root, 'b'))], "
    "check=True)\n"
    "        sys.exit(result)\n"
    "    if action in ('parallel-timeout', 'parallel-int', 'parallel-term'):\n"
    "        for name, folder in zip(names, ['a', 'b']):\n"
    "            Path(root, folder).mkdir()\n"
    "            policy.TOOLS[name] = [worker, 'descendant', str(Path(root, "
    "folder)),\n"
    "                                  '1' if sys.platform.startswith('linux') "
    "else '0', '0']\n"
    "        sibling = subprocess.Popen([worker, 'sibling', root])\n"
    "        sender = None\n"
    "        if action != 'parallel-timeout':\n"
    "            number = signal.SIGINT if action == 'parallel-int' else "
    "signal.SIGTERM\n"
    "            sender = subprocess.Popen([worker, 'policy-signal', root, "
    "str(os.getpid()), str(number)])\n"
    "        try:\n"
    "            result = policy.run_group(names, jobs=jobs, seconds=2 if "
    "action == 'parallel-timeout' else None)\n"
    "        except (policy.TerminationRequested, KeyboardInterrupt):\n"
    "            result = 130\n"
    "        finally:\n"
    "            if sender is not None: sender.wait(timeout=6)\n"
    "            for folder in ['a', 'b']:\n"
    "                subprocess.run([worker, 'cleanup-probe', str(Path(root, "
    "folder))], check=True)\n"
    "            Path(root, 'sibling-release').touch()\n"
    "            sibling.wait(timeout=6)\n"
    "        sys.exit(result)\n"
    "    sys.exit(policy.run_group(names, jobs=jobs))\n";

static const char driver_source[] =
    "try:\n"
    "    if sys.argv[2] == '_supervise':\n"
    "        sys.exit(policy.supervise(json.loads(sys.argv[3])))\n"
    "    action, worker, root = sys.argv[2:5]\n"
    "    if action == 'studio-result':\n"
    "        import agent\n"
    "        output = open(root + '/studio-output').read()\n"
    "        print(json.dumps(agent.studio_acceptance_output(sys.argv[5], "
    "output)))\n"
    "        sys.exit(0)\n"
    "    if action == 'studio-resources':\n"
    "        import agent\n"
    "        from pathlib import Path\n"
    "        agent.studio_resource_preflight(Path(root), "
    "json.loads(sys.argv[5]))\n"
    "        sys.exit(0)\n"
    "    if action == 'studio-source':\n"
    "        import agent\n"
    "        records = json.loads(open(root + '/studio-records.json').read())\n"
    "        agent.studio_acceptance_sources(Path(root) / 'studio-source', "
    "records)\n"
    "        sys.exit(0)\n"
    "    if action == 'exit':\n"
    "        result = policy.run_commands([[worker, 'exit', sys.argv[5]]])\n"
    "    elif action == 'aggregate':\n"
    "        result = policy.run_commands([[worker, 'sleep', '1.4'],\n"
    "            [worker, 'sleep', '1.4'], [worker, 'mark', root]], 2)\n"
    "    elif action == 'sequence':\n"
    "        result = policy.run_commands([[worker, 'sleep', '.05'],\n"
    "            [worker, 'mark', root]])\n"
    "    elif action == 'timeout-options':\n"
    "        for duration in (0, -1, float('inf'), float('nan')):\n"
    "            try:\n"
    "                policy.run_commands([[worker, 'mark', root]], duration)\n"
    "            except ValueError:\n"
    "                continue\n"
    "            raise RuntimeError('invalid timeout accepted')\n"
    "        result = policy.run_commands([[worker, 'exit', '0']], 120)\n"
    "    elif action == 'no-reader':\n"
    "        result = policy.supervise([[worker, 'mark', root]])\n"
    "    elif action in ('slow-ps', 'hung-ps'):\n"
    "        try:\n"
    "            result = policy.supervise([[worker, 'mark', root]])\n"
    "        finally:\n"
    /* Verify the probe is gone before the caller performs group cleanup. */
    "            subprocess.run([worker, 'cleanup-probe', root], check=True)\n"
    "    elif action == 'portable':\n"
    "        os.environ['CHIBI_POLICY_FIXTURE'] = 'portable'\n"
    "        os.environ[policy.PROCESS_GROUP] = str(os.getpgrp())\n"
    "        sibling = subprocess.Popen([worker, 'sibling', root])\n"
    "        ready = time.monotonic() + 4\n"
    "        while not os.path.exists(root + '/sibling-ready'):\n"
    "            if sibling.poll() is not None or time.monotonic() >= ready:\n"
    "                raise RuntimeError('sibling readiness failed')\n"
    "            time.sleep(.001)\n"
    "        result = policy.run_commands([[worker, 'descendant', root, '0', "
    "'0']], 2)\n"
    "        open(root + '/sibling-release', 'w').close()\n"
    "        sibling.wait(timeout=4)\n"
    "    elif action == 'nested':\n"
    "        result = policy.run_commands([[sys.executable, __file__, "
    "sys.argv[1],\n"
    "            'inner', worker, root]], 2)\n"
    "    elif action == 'inner':\n"
    "        result = policy.run_commands([[worker, 'descendant', root, '1', "
    "'0']])\n"
    "    else:\n"
    "        if action in ('missing-proc', 'pending'): "
    "os.environ['CHIBI_POLICY_FIXTURE'] = action\n"
    "        detached = '1' if action in ('missing-proc', 'orphan', 'pending') "
    "else '0'\n"
    "        orphan = '1' if action == 'orphan' else '0'\n"
    "        result = policy.run_commands([[worker, 'descendant', root, "
    "detached, orphan]], 2)\n"
    "    if action in ('portable', 'descendant', 'nested', 'missing-proc', "
    "'orphan', 'pending'):\n"
    "        subprocess.run([worker, 'cleanup-probe', root], check=True)\n"
    "    sys.exit(result)\n"
    "except (policy.TerminationRequested, KeyboardInterrupt):\n"
    "    sys.exit(130)\n"
    "except (RuntimeError, ValueError) as error:\n"
    "    print(error, file=sys.stderr)\n"
    "    sys.exit(2)\n";

static void path(char *output, const char *name)
{
    int length = snprintf(output, PATH_MAX, "%s/%s", directory, name);
    assert(length > 0 && length < PATH_MAX);
}

static bool exists(const char *name)
{
    char filename[PATH_MAX];
    path(filename, name);
    return !access(filename, F_OK);
}

static int run(const char *action, const char *argument)
{
    char tools[PATH_MAX];
    int length = snprintf(tools, sizeof tools, "%s/tools", tool_root());
    assert(length > 0 && length < (int)sizeof tools);
    /* _supervise is launched by run_commands with its own fixed argv shape. */
    char supervisor[PATH_MAX];
    path(supervisor, "supervisor.py");
    char source[12000];
    length = snprintf(
        source,
        sizeof source,
        "import sys\nsys.argv.insert(1, "
        "%c%s%c)\nexec(compile(open(%c%s%c).read(), %c%s%c, 'exec'))\n",
        '\'',
        tools,
        '\'',
        '\'',
        driver,
        '\'',
        '\'',
        supervisor,
        '\''
    );
    assert(length > 0 && length < (int)sizeof source);
    tool_write(supervisor, source);
    const char *args[] = {
        "python3", driver,   tools, action, CHIBI_POLICY_WORKER_PATH,
        directory, argument, NULL
    };
    return tool_run(args, tool_root(), logpath);
}

static void reset_markers()
{
    const char *names[] = { "started",         "escaped",
                            "sibling",         "last-command",
                            "cleanup-entered", "sibling-ready",
                            "sibling-release" };
    for (int i = 0; i < (int)(sizeof names / sizeof *names); i++) {
        char filename[PATH_MAX];
        path(filename, names[i]);
        unlink(filename);
    }
}

static int benchmark(const char *const arguments[])
{
    const char *worker = CHIBI_POLICY_WORKER_PATH;
    const char *separator = strrchr(worker, '/');
    assert(separator);
    char binary[PATH_MAX];
    int length = snprintf(binary, sizeof binary, "%.*s/benchmark_trials", (int)(separator - worker), worker);
    assert(length > 0 && length < (int)sizeof binary);
    const char *argv[16] = { binary };
    int count = 1;
    while (*arguments) {
        assert(count < (int)(sizeof argv / sizeof *argv) - 1);
        argv[count++] = *arguments++;
    }
    return tool_run(argv, tool_root(), logpath);
}

TEST_CASE(benchmark_runner_timeout_is_optional)
{
    const char *normal[] = { "--", CHIBI_POLICY_WORKER_PATH, "exit", "0", NULL };
    REQUIRE(benchmark(normal) == 0);
    char *output = tool_read(logpath);
    bool completed = output && strstr(output, "benchmark trials=3 median=");
    free(output);
    REQUIRE(completed);
    const char *extended[] = { "--timeout", "120", "--trials",
                               "5",         "--",  CHIBI_POLICY_WORKER_PATH,
                               "exit",      "0",   NULL };
    REQUIRE(benchmark(extended) == 0);
    output = tool_read(logpath);
    completed = output && strstr(output, "benchmark trials=5 median=");
    free(output);
    REQUIRE(completed);
    const char *failure[] = { "--", CHIBI_POLICY_WORKER_PATH, "exit", "7", NULL };
    REQUIRE(benchmark(failure) == 7);
    const char *invalid[] = { "0", "-1", "2", "inf", "2147483648" };
    for (int i = 0; i < (int)(sizeof invalid / sizeof *invalid); i++) {
        const char *argv[] = { "--timeout", invalid[i],
                               "--",        CHIBI_POLICY_WORKER_PATH,
                               "mark",      directory,
                               NULL };
        REQUIRE(benchmark(argv) == 2);
        REQUIRE(!exists("last-command"));
    }
    return true;
}

TEST_CASE(benchmark_runner_explicit_timeout_cleans_descendants)
{
    reset_markers();
    const char *argv[] = { "--timeout",  "3",
                           "--",         CHIBI_POLICY_WORKER_PATH,
                           "descendant", directory,
                           "0",          "0",
                           NULL };
    REQUIRE(benchmark(argv) == 124);
    REQUIRE(exists("started"));
    const char *probe[] = { CHIBI_POLICY_WORKER_PATH, "cleanup-probe", directory, NULL };
    REQUIRE(tool_run(probe, tool_root(), logpath) == 0);
    REQUIRE(!exists("escaped"));
    reset_markers();
    return true;
}

TEST_CASE(material_cpu_routes_reach_exact_dispatcher)
{
    const char *selectors[] = {
        "--material-destruction", "--material-fluids",     "--material-mpm",
        "--material-thermal",     "--material-simulation", "--material-ecs",
        "--material-render",      "--material-benchmark",  "--contact-benchmark"
    };
    for (int i = 0; i < (int)(sizeof selectors / sizeof *selectors); i++) {
        REQUIRE(run("cpu-route", selectors[i]) == 0);
        char expected[128];
        snprintf(expected, sizeof expected, "test_tools %s\n", selectors[i]);
        char *output = tool_read(logpath);
        bool exact = output && !strcmp(output, expected);
        free(output);
        REQUIRE(exact);
    }
    REQUIRE(run("cpu-route", "--material-unknown") == 2);
    REQUIRE(run("cpu-route", "--material-pixels") == 2);
    return true;
}

/* Real CLI/supervisor dispatch with disposable child drivers: no project work
 * runs, and C checks which selector each production group launches. */
static int run_group(const char *group)
{
    char policy_path[PATH_MAX];
    int length = snprintf(policy_path, sizeof policy_path, "%s/tools/test_policy.py", tool_root());
    assert(length > 0 && length < (int)sizeof policy_path);
    const char *names[] = { "CHIBI_TOOLS_BUILD_DIR", "CHIBI_LANGUAGE_BUILD_DIR" };
    char *saved[2];
    for (int i = 0; i < 2; i++) {
        const char *value = getenv(names[i]);
        saved[i] = value ? xstrdup(value) : NULL;
        assert(!setenv(names[i], directory, 1));
    }
    const char *args[] = { "python3", policy_path, "group", group, NULL };
    int status = tool_run(args, tool_root(), logpath);
    for (int i = 0; i < 2; i++) {
        if (saved[i]) {
            assert(!setenv(names[i], saved[i], 1));
        } else {
            assert(!unsetenv(names[i]));
        }
        free(saved[i]);
    }
    return status;
}

TEST_CASE(asset_language_cases_are_in_the_actual_ci_group)
{
    char child[PATH_MAX], commands[PATH_MAX], source[PATH_MAX + 128];
    path(commands, "asset-language-commands");
    int length = snprintf(source, sizeof source, "#!/bin/sh\nprintf '%%s\\n' \"$1\" >> '%s'\n", commands);
    REQUIRE(length > 0 && length < (int)sizeof source);
    const char *drivers[] = { "test_tools", "test_language" };
    for (int i = 0; i < 2; i++) {
        path(child, drivers[i]);
        tool_write(child, source);
        REQUIRE(!chmod(child, 0700));
    }
    /* The production group/supervisor launches only these disposable recorders;
     * no engine build, asset evaluation or inspection-policy override occurs. */
    REQUIRE(run_group("language") == 0);
    char *output = tool_read(commands);
    REQUIRE(output);
    const char *selectors[] = { "--execution-scope\n",
                                "--asset-packaging-chibi\n",
                                "--asset-compiler-host\n",
                                "--compiler-bootstrap\n" };
    int counts[4] = { 0 };
    for (int i = 0; i < 4; i++) {
        const char *at = output;
        while ((at = strstr(at, selectors[i]))) {
            counts[i]++;
            at += strlen(selectors[i]);
        }
    }
    free(output);
    REQUIRE(counts[0] == 1 && counts[1] == 1);
#if defined(__linux__) || defined(__APPLE__)
    REQUIRE(counts[2] == 1);
#else
    REQUIRE(counts[2] == 0);
#endif
    REQUIRE(counts[3] == 1);
    REQUIRE(run("bootstrap-route", "-") == 0);
    output = tool_read(logpath);
    bool bootstrap = output &&
                     !strcmp(output, "4 test_tools --compiler-bootstrap test_language chibi-compiler-host 1\n");
    free(output);
    REQUIRE(bootstrap);
    REQUIRE(run("asset-host-platforms", "-") == 0);
    output = tool_read(logpath);
    bool platforms = output && !strcmp(output, "linux darwin\n");
    free(output);
    REQUIRE(platforms);
    return true;
}

TEST_CASE(project_group_keeps_all_project_commands_out_of_native)
{
    char child[PATH_MAX], commands[PATH_MAX], source[PATH_MAX + 128];
    path(commands, "group-commands");
    int length = snprintf(source, sizeof source, "#!/bin/sh\nprintf '%%s\\n' \"$1\" >> '%s'\n", commands);
    REQUIRE(length > 0 && length < (int)sizeof source);
    const char *drivers[] = { "test_tools", "test_language" };
    for (int i = 0; i < 2; i++) {
        path(child, drivers[i]);
        tool_write(child, source);
        REQUIRE(!chmod(child, 0700));
    }
    REQUIRE(run_group("projects") == 0);
    char *output = tool_read(commands);
    const char
        *expected = "--mesh-files\n--model-import\n--validate-project\n"
                    "--prototype-expansion\n--controller\n--feature-coverage\n"
                    "--render-fixture\n--material-bake\n--environment-"
                    "authoring\n"
                    "--terrain-checkpoint\n--terrain-storage-commands\n"
                    "--terrain-playable\n--published-docs\n--chibi-cli\n";
    bool exact = output && !strcmp(output, expected);
    free(output);
    REQUIRE(exact);
    REQUIRE(!unlink(commands));
    REQUIRE(run_group("native") == 0);
    output = tool_read(commands);
    bool separate = output && *output;
    const char *selectors[] = { "--mesh-files\n",
                                "--model-import\n",
                                "--validate-project\n",
                                "--prototype-expansion\n",
                                "--controller\n",
                                "--feature-coverage\n",
                                "--render-fixture\n",
                                "--material-bake\n",
                                "--environment-authoring\n",
                                "--terrain-checkpoint\n",
                                "--terrain-storage-commands\n",
                                "--published-docs\n",
                                "--chibi-cli\n" };
    for (int i = 0; separate && i < (int)(sizeof selectors / sizeof *selectors); i++) {
        separate = !strstr(output, selectors[i]);
    }
    free(output);
    REQUIRE(separate);
    REQUIRE(!unlink(commands));
    for (int i = 0; i < 2; i++) {
        path(child, drivers[i]);
        REQUIRE(!unlink(child));
    }
    return true;
}

TEST_CASE(group_selectors_are_unique_and_controller_users_are_projects)
{
    REQUIRE(run("misplaced-selectors", "-") == 0);
    char *output = tool_read(logpath);
    bool exact = output && output[0] == '\n';
    bool row_groups = exact && !strcmp(output + 1, "1 1 1 1\n");
    free(output);
    REQUIRE(exact);
    REQUIRE(row_groups);
    return true;
}

TEST_CASE(ci_groups_share_one_build_and_stop_on_failure)
{
    const char *steps[] = { "build",         "native",   "materials",
                            "build-release", "projects", "language" };
    for (int fail = -1; fail < 6; fail++) {
        REQUIRE(run("ci-groups", fail < 0 ? "-" : steps[fail]) == 0);
        char expected[512] = "";
        for (int job = 0; job < 2; job++) {
            strcat(expected, fail < 0 ? "status=0\n" : "status=2\n");
            for (int i = 0; i <= (fail < 0 ? 5 : fail); i++) {
                strcat(expected, steps[i]);
                strcat(expected, "\n");
            }
        }
        char *output = tool_read(logpath);
        bool exact = output && !strcmp(output, expected);
        free(output);
        REQUIRE(exact);
    }
    return true;
}

TEST_CASE(ci_initial_build_keeps_targets_profiles_and_failures)
{
    const char *targets = "targets=chibi,chibi-compile,chibi-player,chibi_ctl,"
                          "test_tools,test_language";
    const char *flags = "flags=-DCMAKE_BUILD_TYPE=Debug,-DCHIBI_ENABLE_"
                        "SANITIZERS=ON,-DCHIBI_SANITIZERS=address,undefined\n";
    const char *failures[] = { "-",           "native-toolchain",
                               "third-party", "graph",
                               "cold:-",      "cold:configure-native",
                               "cold:build",  "cold:third-party",
                               "cold:graph" };
    for (int fail = 0; fail < 9; fail++) {
        REQUIRE(run("ci-initial-build", failures[fail]) == 0);
        bool cold = fail >= 4;
        char expected[2048] = "";
        strcat(expected, fail == 1 || fail == 5 || fail == 6 ? "native failed\n" : "native continued\n");
        if (cold) {
            strcat(expected, "configure-native\n");
            strcat(expected, flags);
            if (fail != 5) {
                strcat(expected, "build\n");
                strcat(expected, targets);
                strcat(expected, ",chibi-build-host\njobs=2\n");
            }
        } else {
            strcat(expected, "native-toolchain\n");
            strcat(expected, targets);
            strcat(expected, "\n");
            strcat(expected, flags);
            strcat(expected, "jobs=2\n");
        }
        strcat(expected, fail == 2 || fail == 3 || fail >= 5 ? "native-macos failed\n" : "native-macos continued\n");
        if (cold) {
            strcat(expected, "configure-native\n");
            strcat(expected, flags);
            if (fail != 5) {
                strcat(expected, "build\ntargets=chibi-build-host\njobs=2\n");
            }
        }
        if (fail != 5 && fail != 6) {
            strcat(expected, "third-party\n");
            if (fail != 2 && fail != 7) {
                strcat(expected, "graph\n");
                strcat(expected, targets);
                strcat(expected, ",chibi-build-host\nprofile=sanitizer\njobs=2\n");
            }
        }
        char *output = tool_read(logpath);
        bool exact = output && !strcmp(output, expected);
        free(output);
        REQUIRE(exact);
    }
    return true;
}

TEST_CASE(ci_preserves_failed_macos_graph_stage_log)
{
    char workflow[PATH_MAX];
    snprintf(workflow, sizeof workflow, "%s/.github/workflows/ci.yml", tool_root());
    char *text = tool_read(workflow);
    REQUIRE(text);
    const char *job = strstr(text, "\n  native-macos:\n");
    const char *limit = job ? strstr(job, "\n  windows:\n") : NULL;
    const char *step = job ? strstr(job, "      - name: Preserve failed macOS Graph stage log\n")
                           : NULL;
    const char *end = step ? strstr(step + 1, "\n      - ") : NULL;
    bool scoped = step && limit && end && step < limit && end < limit;
    const char *fields[] = { "        if: failure()\n",
                             "        uses: actions/upload-artifact@v4\n",
                             ("          name: macos-graph-failure-${{ "
                              "github.run_id }}-${{ github.run_attempt }}\n"),
                             "          path: .chibi/logs/*-graph.log\n",
                             "          include-hidden-files: true\n",
                             "          if-no-files-found: warn\n",
                             "          retention-days: 7\n" };
    for (int i = 0; scoped && i < (int)(sizeof fields / sizeof *fields); i++) {
        const char *field = strstr(step, fields[i]);
        scoped = field && field < end;
    }
    free(text);
    REQUIRE(scoped);

    char capture[PATH_MAX], hidden[PATH_MAX], logs[PATH_MAX], noise[PATH_MAX],
        quiet[PATH_MAX];
    path(capture, "graph-log-capture");
    path(hidden, "graph-log-capture/.chibi");
    path(logs, "graph-log-capture/.chibi/logs");
    path(noise, "graph-log-capture/.chibi/logs/unrelated-configure.log");
    assert(!mkdir(capture, 0700) && !mkdir(hidden, 0700) && !mkdir(logs, 0700));
    tool_write(noise, "not selected\n");
    snprintf(quiet, sizeof quiet, "%s/tools/quiet.py", tool_root());
    const char *argv[] = { "python3",
                           quiet,
                           "--log-dir",
                           ".chibi/logs",
                           "graph",
                           "--",
                           "python3",
                           "-c",
                           "import sys; print('full-log-first-line'); "
                           "print('ordinary output\\n' * 64, end=''); "
                           "print('full-log-last-line'); sys.exit(7)",
                           NULL };
    REQUIRE(tool_run(argv, capture, logpath) == 7);
    char *console = tool_read(logpath);
    bool bounded = console && strstr(console, "graph: FAILED") &&
                   !strstr(console, "full-log-first-line");
    free(console);
    REQUIRE(bounded);
    char pattern[PATH_MAX];
    path(pattern, "graph-log-capture/.chibi/logs/*-graph.log");
    glob_t selected = { 0 };
    int status = glob(pattern, 0, NULL, &selected);
    bool one = !status && selected.gl_pathc == 1;
    char *full = one ? tool_read(selected.gl_pathv[0]) : NULL;
    bool kept = full && strstr(full, "full-log-first-line\n") &&
                strstr(full, "full-log-last-line\n");
    free(full);
    globfree(&selected);
    REQUIRE(one && kept);
    REQUIRE(!access(noise, F_OK));
    return true;
}

TEST_CASE(studio_acceptance_requires_exact_completion)
{
    char filename[PATH_MAX];
    path(filename, "studio-output");
    const char *graph = "Studio graph visibility accepted: frames=10\n";
    const char
        *edit = "Studio source edit accepted: frames=4 edits=1 undo=1 redo=1\n";
    tool_write(filename, graph);
    REQUIRE(run("studio-result", "--studio-graph-visibility") == 0);
    char *output = tool_read(logpath);
    bool ten_frames = output && strstr(output, "\"frames\": 10");
    free(output);
    REQUIRE(ten_frames);
    REQUIRE(run("studio-result", "--studio-source-edit") == 2);
    tool_write(filename, edit);
    REQUIRE(run("studio-result", "--studio-source-edit") == 0);
    REQUIRE(run("studio-result", "--studio-graph-visibility") == 2);
    tool_write(filename, "Studio graph visibility accepted: frames=9\n");
    REQUIRE(run("studio-result", "--studio-graph-visibility") == 2);
    tool_write(filename, "prefix Studio graph visibility accepted: frames=10\n");
    REQUIRE(run("studio-result", "--studio-graph-visibility") == 2);
    const char *moves[] = { "--studio-scene-move", "--studio-outliner-move" };
    const char *completed[] = { "Studio scene move accepted: frames=2\n",
                                "Studio outliner move accepted: frames=21\n" };
    for (int i = 0; i < 2; i++) {
        tool_write(filename, completed[i]);
        REQUIRE(run("studio-result", moves[i]) == 0);
        REQUIRE(run("studio-result", moves[1 - i]) == 2);
        tool_write(filename, "Studio outliner move accepted: frames=19\n");
        REQUIRE(run("studio-result", moves[i]) == 2);
    }
    char repeated[256];
    snprintf(repeated, sizeof repeated, "%s%s", graph, graph);
    tool_write(filename, repeated);
    REQUIRE(run("studio-result", "--studio-graph-visibility") == 2);
    const char *failures[] = { "Studio UI:", "runtime frame failed", "Could not initialize" };
    for (int i = 0; i < (int)(sizeof failures / sizeof *failures); i++) {
        snprintf(repeated, sizeof repeated, "%s%s\n", graph, failures[i]);
        tool_write(filename, repeated);
        REQUIRE(run("studio-result", "--studio-graph-visibility") == 2);
    }
    const char *selectors[] = { "--studio-graph-pointer",
                                "--studio-asset-pointer",
                                "--studio-debugger-source-history" };
    const char *success[] = {
        "Studio graph pointer accepted: frames=19\n",
        "Studio asset pointer accepted: frames=20\n",
        "Studio debugger source history accepted: frames=47\n"
    };
    for (int i = 0; i < 3; i++) {
        tool_write(filename, success[i]);
        REQUIRE(run("studio-result", selectors[i]) == 0);
        REQUIRE(run("studio-result", selectors[(i + 1) % 3]) == 2);
    }
    return true;
}

TEST_CASE(studio_camera_requires_thirty_clean_frames)
{
    char filename[PATH_MAX];
    path(filename, "studio-output");
    const char *success = "Studio 3D camera accepted: frames=30\n";
    tool_write(filename, success);
    REQUIRE(run("studio-result", "--studio-scene-camera") == 0);
    char *output = tool_read(logpath);
    bool thirty_frames = output && strstr(output, "\"frames\": 30");
    free(output);
    REQUIRE(thirty_frames);
    REQUIRE(run("studio-result", "--studio-graph-pointer") == 2);

    const char *rejected[] = {
        "",
        "Studio 3D camera accepted: frames=26\n",
        "Studio 3D camera accepted: frames=29\n",
        "prefix Studio 3D camera accepted: frames=30\n",
        ("Studio 3D camera accepted: frames=30\nStudio 3D camera accepted: "
         "frames=30\n"),
        "Studio 3D camera accepted: frames=30\nStudio UI: failed\n",
        "Studio 3D camera accepted: frames=30\nruntime frame failed\n",
        "Studio 3D camera accepted: frames=30\nCould not initialize\n",
        "Studio graph pointer accepted: frames=19\n",
    };
    for (int i = 0; i < (int)(sizeof rejected / sizeof *rejected); i++) {
        tool_write(filename, rejected[i]);
        REQUIRE(run("studio-result", "--studio-scene-camera") == 2);
    }
    return true;
}

TEST_CASE(studio_collider_requires_twenty_six_clean_frames)
{
    char filename[PATH_MAX];
    path(filename, "studio-output");
    tool_write(filename, "Studio 3D collider handles accepted: frames=26\n");
    REQUIRE(run("studio-result", "--studio-scene-collider3d") == 0);
    char *output = tool_read(logpath);
    bool complete = output && strstr(output, "\"frames\": 26");
    free(output);
    REQUIRE(complete);
    REQUIRE(run("studio-result", "--studio-scene-gizmo3d") == 2);
    const char *rejected[] = {
        "",
        "Studio 3D collider handles accepted: frames=18\n",
        "Studio 3D collider handles accepted: frames=25\n",
        "prefix Studio 3D collider handles accepted: frames=26\n",
        ("Studio 3D collider handles accepted: frames=26\nStudio 3D collider "
         "handles accepted: frames=26\n"),
        "Studio 3D collider handles accepted: frames=26\nStudio UI: failed\n",
        ("Studio 3D collider handles accepted: frames=26\nruntime frame "
         "failed\n"),
    };
    for (int i = 0; i < (int)(sizeof rejected / sizeof *rejected); i++) {
        tool_write(filename, rejected[i]);
        REQUIRE(run("studio-result", "--studio-scene-collider3d") == 2);
    }
    return true;
}

TEST_CASE(studio_gizmo_requires_forty_two_clean_frames)
{
    char filename[PATH_MAX];
    path(filename, "studio-output");
    tool_write(filename, "Studio 3D gizmo accepted: frames=42\n");
    REQUIRE(run("studio-result", "--studio-scene-gizmo3d") == 0);
    char *output = tool_read(logpath);
    bool complete = output && strstr(output, "\"frames\": 42");
    free(output);
    REQUIRE(complete);
    REQUIRE(run("studio-result", "--studio-scene-camera") == 2);
    const char *rejected[] = {
        "Studio 3D gizmo accepted: frames=38\n",
        "Studio 3D gizmo accepted: frames=41\n",
        "prefix Studio 3D gizmo accepted: frames=42\n",
        ("Studio 3D gizmo accepted: frames=42\nStudio 3D gizmo accepted: "
         "frames=42\n"),
        "Studio 3D gizmo accepted: frames=42\nruntime frame failed\n",
    };
    for (int i = 0; i < (int)(sizeof rejected / sizeof *rejected); i++) {
        tool_write(filename, rejected[i]);
        REQUIRE(run("studio-result", "--studio-scene-gizmo3d") == 2);
    }
    return true;
}

enum
{
    STUDIO_SOURCE_BYTES = 65537,
    STUDIO_SOURCE_OVERSIZE_BYTES = 262145
};
static const char *const studio_source_members[] = {
    "main.c",    "host.c",      "application.c", "debugger.c", "debugger.h",
    "README.md", "project.kdl", "Counter.chibi", "Start.kdl"
};

static void studio_source_path(char *output, const char *member)
{
    char relative[128];
    int length = snprintf(relative, sizeof relative, "studio-source/tests/acceptance/%s", member);
    assert(length > 0 && (size_t)length < sizeof relative);
    path(output, relative);
}

static void write_studio_main(size_t bytes, char first)
{
    char filename[PATH_MAX];
    studio_source_path(filename, "main.c");
    char *text = xmalloc(bytes + 1);
    memset(text, 'a', bytes);
    text[0] = first;
    text[bytes] = 0;
    tool_write(filename, text);
    free(text);
}

/* These fixed hashes cover the C-created bytes, including the over-limit file. */
static void write_studio_records(size_t main_bytes)
{
    assert(main_bytes == STUDIO_SOURCE_BYTES || main_bytes == STUDIO_SOURCE_OVERSIZE_BYTES);
    const char *main_hash = main_bytes == STUDIO_SOURCE_BYTES
                              ? "008ffc88d3c96a9f307524eb361e47c5222a887fc45fa0"
                                "c1fb8d429c5c23b430"
                              : "c592f4a6b099700b5050ce8bc67367f0c8f44810203124"
                                "e86405c3e7b6f1a2ba";
    const char *one_hash = "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b98"
                           "07785afee48bb";
    char filename[PATH_MAX];
    path(filename, "studio-records.json");
    FILE *file = fopen(filename, "wb");
    assert(file);
    assert(fputs("{", file) >= 0);
    for (int i = 0;
         i < (int)(sizeof studio_source_members / sizeof *studio_source_members);
         i++) {
        assert(
            fprintf(
                file,
                "%s\"tests/acceptance/%s\":{\"bytes\":%zu,\"sha256\":\"%s\"}",
                i ? "," : "",
                studio_source_members[i],
                i ? (size_t)1 : main_bytes,
                i ? one_hash : main_hash
            ) > 0
        );
    }
    assert(fputs("}\n", file) >= 0);
    assert(fclose(file) == 0);
}

static bool studio_source_rejected(const char *reason)
{
    REQUIRE(run("studio-source", NULL) == 2);
    char *output = tool_read(logpath);
    bool rejected = output && strstr(output, reason);
    free(output);
    REQUIRE(rejected);
    return true;
}

TEST_CASE(studio_acceptance_source_records_match_files)
{
    const char *folders[] = { "studio-source",
                              "studio-source/tests",
                              "studio-source/tests/acceptance" };
    char filename[PATH_MAX];
    for (int i = 0; i < 3; i++) {
        path(filename, folders[i]);
        REQUIRE(mkdir(filename, 0700) == 0);
    }
    write_studio_main(STUDIO_SOURCE_BYTES, 'a');
    for (int i = 1;
         i < (int)(sizeof studio_source_members / sizeof *studio_source_members);
         i++) {
        studio_source_path(filename, studio_source_members[i]);
        tool_write(filename, "a");
    }
    write_studio_records(STUDIO_SOURCE_BYTES);
    REQUIRE(run("studio-source", NULL) == 0);

    /* A directory alias must not bypass the source-file symlink guard. */
    char acceptance[PATH_MAX], original[PATH_MAX];
    path(acceptance, "studio-source/tests/acceptance");
    path(original, "acceptance-original");
    REQUIRE(rename(acceptance, original) == 0);
    REQUIRE(symlink(original, acceptance) == 0);
    REQUIRE(studio_source_rejected("studio_acceptance_source_size_changed"));
    REQUIRE(unlink(acceptance) == 0);
    REQUIRE(rename(original, acceptance) == 0);

    studio_source_path(filename, "debugger.h");
    REQUIRE(unlink(filename) == 0);
    REQUIRE(studio_source_rejected("studio_acceptance_source_records_changed"));
    tool_write(filename, "a");
    studio_source_path(filename, "unrecorded.c");
    tool_write(filename, "a");
    REQUIRE(studio_source_rejected("studio_acceptance_source_records_changed"));
    REQUIRE(unlink(filename) == 0);

    write_studio_main(STUDIO_SOURCE_BYTES, 'b');
    REQUIRE(studio_source_rejected("studio_acceptance_source_changed"));
    write_studio_main(STUDIO_SOURCE_BYTES, 'a');
    studio_source_path(filename, "debugger.h");
    REQUIRE(unlink(filename) == 0 && symlink("host.c", filename) == 0);
    REQUIRE(studio_source_rejected("studio_acceptance_source_size_changed"));
    REQUIRE(unlink(filename) == 0);
    tool_write(filename, "a");

    write_studio_main(STUDIO_SOURCE_OVERSIZE_BYTES, 'a');
    write_studio_records(STUDIO_SOURCE_OVERSIZE_BYTES);
    REQUIRE(studio_source_rejected("studio_acceptance_source_size_changed"));
    return true;
}

TEST_CASE(studio_resources_require_recorded_generation)
{
    char filename[PATH_MAX];
    const char *folders[] = { "share", "share/chibi", "share/chibi/studio", "share/chibi/ui" };
    for (int i = 0; i < 4; i++) {
        path(filename, folders[i]);
        REQUIRE(mkdir(filename, 0700) == 0);
    }
    path(filename, "share/chibi/studio/fixture.rml");
    tool_write(filename, "a");
    path(filename, "share/chibi/ui/fixture.rml");
    tool_write(filename, "b");
    const char *resources = "{\"studio\":[1,1,"
                            "\"9c4942f88cf0d2006c36fd44be63f5bcba29023280cef8ef"
                            "601bdf7f75b473e8\"],"
                            "\"ui\":[1,1,"
                            "\"d83b434175658e53e4c47cb4c1d8f837746c4b525113c2be"
                            "c7399ec2bbbdba5c\"]}";
    REQUIRE(run("studio-resources", resources) == 0);
    /* An override must verify both generations, never just a chosen subset. */
    const char *partial = "{\"ui\":[1,1,"
                          "\"d83b434175658e53e4c47cb4c1d8f837746c4b525113c2be"
                          "c7399ec2bbbdba5c\"]}";
    REQUIRE(run("studio-resources", partial) == 2);
    REQUIRE(run("studio-resources", "{}") == 2);
    tool_write(filename, "c");
    REQUIRE(run("studio-resources", resources) != 0);
    tool_write(filename, "bb");
    REQUIRE(run("studio-resources", resources) != 0);
    tool_write(filename, "b");
    path(filename, "share/chibi/ui/unreviewed.rml");
    tool_write(filename, "b");
    REQUIRE(run("studio-resources", resources) != 0);
    return true;
}

static void reset_parallel()
{
    char root[PATH_MAX];
    path(root, "parallel");
    if (!access(root, F_OK)) {
        tool_remove_tree(root);
    }
}

static char *parallel_metrics()
{
    char filename[PATH_MAX];
    path(filename, "parallel/metrics.txt");
    return tool_read(filename);
}

static bool parallel_group_metrics(const char *metrics, int expected_peak, int expected_result)
{
    const char *row = strstr(metrics, "group ");
    int peak = -1, result = -1;
    double wall = -1;
    REQUIRE(row && sscanf(row, "group %d %lf %d", &peak, &wall, &result) == 3);
    REQUIRE(peak == expected_peak && result == expected_result && wall >= 0);
    return true;
}

TEST_CASE(parallel_diagnostics_report_observed_overlap_and_resources)
{
    reset_parallel();
    REQUIRE(run("parallel-default", NULL) == 0);
    char *metrics = parallel_metrics();
    REQUIRE(metrics && parallel_group_metrics(metrics, 2, 0));
    const char *first = strstr(metrics, "selector first ");
    const char *second = strstr(metrics, "selector second ");
    double first_start, first_finish, second_start, second_finish;
    char first_status[32], second_status[32];
    int first_exit = -1, second_exit = -1;
    REQUIRE(
        first &&
        sscanf(first, "selector first %lf %lf %31s %d", &first_start, &first_finish, first_status, &first_exit) ==
            4
    );
    REQUIRE(
        second &&
        sscanf(second, "selector second %lf %lf %31s %d", &second_start, &second_finish, second_status, &second_exit) ==
            4
    );
    REQUIRE(first_start <= first_finish && second_start <= second_finish);
    REQUIRE(first_start < second_finish && second_start < first_finish);
    REQUIRE(!strcmp(first_status, "passed") && !strcmp(second_status, "passed"));
    REQUIRE(!first_exit && !second_exit);
    REQUIRE(
        strstr(metrics, "admission 2 automatic cpu-memory-admission 4294967296 2147483648 2 available\n")
    );
    free(metrics);
    char *output = tool_read(logpath);
    REQUIRE(output && !strstr(output, "Test policy: "));
    free(output);
    const char *actions[] = { "parallel-conflict", "parallel-unknown" };
    for (int i = 0; i < 2; i++) {
        reset_parallel();
        REQUIRE(run(actions[i], NULL) == 0);
        metrics = parallel_metrics();
        REQUIRE(metrics && parallel_group_metrics(metrics, 1, 0));
        REQUIRE(!exists("parallel/conflict-live"));
        free(metrics);
    }
    return true;
}

TEST_CASE(parallel_diagnostics_distinguish_failure_cancel_and_not_started)
{
    reset_parallel();
    REQUIRE(run("parallel-active-failure", NULL) == 7);
    char *metrics = parallel_metrics();
    REQUIRE(metrics && parallel_group_metrics(metrics, 2, 7));
    const char *first = strstr(metrics, "selector first ");
    const char *second = strstr(metrics, "selector second ");
    const char *third = strstr(metrics, "selector third ");
    REQUIRE(first && strstr(first, " failed 7\n"));
    REQUIRE(second && strstr(second, " cancelled 130\n"));
    REQUIRE(third && strstr(third, "selector third None None not-started None\n") == third);
    free(metrics);
    return true;
}

TEST_CASE(parallel_diagnostics_explain_serial_escape_and_capacity)
{
    const char *actions[] = { "parallel-cli-serial",
                              "parallel-env-serial",
                              "parallel-low-memory",
                              "parallel-memory-unknown",
                              "parallel-all-unknown" };
    const char *admissions[] = {
        "admission 1 explicit explicit None 2147483648 None not-run\n",
        "admission 1 explicit explicit None 2147483648 None not-run\n",
        "admission 1 automatic cpu-memory-admission 3221225472 2147483648 2 "
        "available\n",
        "admission 1 automatic memory-unavailable None 2147483648 2 unknown\n",
        "admission 1 automatic no-parallel-eligible-selectors None 2147483648 "
        "None not-run\n"
    };
    for (int i = 0; i < 5; i++) {
        reset_parallel();
        REQUIRE(run(actions[i], NULL) == 0);
        char *metrics = parallel_metrics();
        REQUIRE(metrics && strstr(metrics, admissions[i]));
        const char *group = strstr(metrics, "group None ");
        double wall = -1;
        int result = -1;
        char scope[32];
        REQUIRE(group && sscanf(group, "group None %lf %d %31s", &wall, &result, scope) == 3);
        REQUIRE(wall >= 0 && !result && !strcmp(scope, "unobserved-serial"));
        REQUIRE(!strstr(metrics, "selector "));
        free(metrics);
    }
    return true;
}

TEST_CASE(parallel_group_overlaps_and_isolates_scratch)
{
    reset_parallel();
    REQUIRE(run("parallel-default", NULL) == 0);
    REQUIRE(exists("parallel/first-overlapped"));
    REQUIRE(exists("parallel/second-overlapped"));
    char *output = tool_read(logpath);
    REQUIRE(output);
    char *first = strstr(output, "completed-first");
    char *second = strstr(output, "completed-second");
    REQUIRE(first && second && first < second);
    char a[4096], b[4096];
    char *scratch_a = strstr(output, "scratch-first=");
    char *scratch_b = strstr(output, "scratch-second=");
    REQUIRE(scratch_a && scratch_b);
    REQUIRE(sscanf(scratch_a, "scratch-first=%4095[^\n]", a) == 1);
    REQUIRE(sscanf(scratch_b, "scratch-second=%4095[^\n]", b) == 1);
    REQUIRE(strcmp(a, b) && access(a, F_OK) && access(b, F_OK));
    free(output);
    reset_parallel();
    REQUIRE(run("parallel-barrier", NULL) == 0);
    REQUIRE(exists("parallel/first-overlapped") && exists("parallel/second-overlapped"));
    return true;
}

TEST_CASE(parallel_group_serializes_conflicts_and_unknowns)
{
    reset_parallel();
    REQUIRE(run("parallel-conflict", NULL) == 0);
    REQUIRE(exists("parallel/resource-done") && !exists("parallel/conflict-live"));
    reset_parallel();
    REQUIRE(run("parallel-unknown", NULL) == 0);
    REQUIRE(!exists("parallel/conflict-live"));
    REQUIRE(exists("parallel/first") && exists("parallel/second") && exists("parallel/third"));
    return true;
}

TEST_CASE(parallel_group_preserves_manifest_and_failure)
{
    const char *actions[] = { "parallel-serial", "parallel-manifest", "parallel-failure" };
    for (int i = 0; i < 3; i++) {
        reset_parallel();
        REQUIRE(run(actions[i], NULL) == (i == 2 ? 7 : 0));
        REQUIRE(exists("parallel/first") && exists("parallel/second"));
        REQUIRE(exists("parallel/third") == (i != 2));
        char *output = tool_read(logpath);
        REQUIRE(output);
        char *first = strstr(output, "completed-first");
        char *second = strstr(output, "completed-second");
        char *third = strstr(output, "completed-third");
        REQUIRE(first && second && first < second);
        REQUIRE(i == 2 ? !third : third && second < third);
        REQUIRE(!strstr(first + 1, "completed-first"));
        REQUIRE(!strstr(second + 1, "completed-second"));
        REQUIRE(!third || !strstr(third + 1, "completed-third"));
        free(output);
    }
    return true;
}

TEST_CASE(parallel_group_failure_stops_active_peer_and_queued_work)
{
    reset_parallel();
    REQUIRE(run("parallel-active-failure", NULL) == 7);
    REQUIRE(exists("parallel/first") && exists("parallel/b/started"));
    REQUIRE(!exists("parallel/b/escaped") && !exists("parallel/third"));
    char *output = tool_read(logpath);
    REQUIRE(output && strstr(output, "[first] exit 7"));
    REQUIRE(strstr(output, "[third] not run: group stopped"));
    REQUIRE(strstr(output, "[second] cancelled; exit"));
    free(output);
    return true;
}

TEST_CASE(parallel_language_manifest_matches_serial_exactly)
{
    reset_parallel();
    REQUIRE(run("parallel-language-serial", NULL) == 0);
    char *serial = tool_read(logpath);
    REQUIRE(serial);
    reset_parallel();
    REQUIRE(run("parallel-language", NULL) == 0);
    char *parallel = tool_read(logpath);
    REQUIRE(parallel);
    char *a = serial, *b = parallel;
    int count = 0;
    for (;;) {
        a = strstr(a, "completed-");
        b = strstr(b, "completed-");
        REQUIRE((a == NULL) == (b == NULL));
        if (!a) {
            break;
        }
        size_t length = strcspn(a, "\n");
        REQUIRE(length == strcspn(b, "\n") && !memcmp(a, b, length));
        a += length;
        b += length;
        count++;
    }
    /* Real platform-gated language manifest, not a three-worker stand-in. */
    REQUIRE(count >= 44);
    char *metrics = parallel_metrics();
    REQUIRE(metrics);
    int records = 0;
    const char *record = metrics;
    while ((record = strstr(record, "selector "))) {
        records++;
        record++;
    }
    REQUIRE(records == count);
    free(metrics);
    free(serial);
    free(parallel);
    return true;
}

TEST_CASE(parallel_admission_bounds_cpu_and_available_memory)
{
    reset_parallel();
    REQUIRE(run("parallel-admission", NULL) == 0);
    char *output = tool_read(logpath);
    REQUIRE(output && !strcmp(output, "1\n1\n2\n1\n"));
    free(output);
    return true;
}

TEST_CASE(parallel_serial_escape_uses_real_cli_and_environment)
{
    const char *actions[] = { "parallel-cli-serial", "parallel-env-serial" };
    for (int i = 0; i < 2; i++) {
        reset_parallel();
        REQUIRE(run(actions[i], NULL) == 0);
        REQUIRE(exists("parallel/first") && exists("parallel/second") && exists("parallel/third"));
        char *output = tool_read(logpath);
        REQUIRE(output && !strstr(output, "[first]"));
        char *first = strstr(output, "completed-first");
        char *second = strstr(output, "completed-second");
        char *third = strstr(output, "completed-third");
        REQUIRE(first && second && third && first < second && second < third);
        free(output);
    }
    return true;
}

TEST_CASE(parallel_memory_reader_respects_limits_and_unknowns)
{
    reset_parallel();
    REQUIRE(run("parallel-memory-reader", NULL) == 0);
    char *output = tool_read(logpath);
    REQUIRE(output && !strcmp(output, "3221225472\nNone\nNone\nNone\n1073741824\n131072\nNone\n"));
    free(output);
    return true;
}

TEST_CASE(parallel_spawn_cancellation_never_loses_child_ownership)
{
    const char *actions[] = { "parallel-spawn-int", "parallel-spawn-term" };
    for (int i = 0; i < 2; i++) {
        reset_parallel();
        REQUIRE(run(actions[i], NULL) == 130);
        REQUIRE(exists("parallel/started") && exists("parallel/sibling"));
        char *output = tool_read(logpath);
        REQUIRE(output && strstr(output, "ownership-probe=0"));
        free(output);
    }
    return true;
}

TEST_CASE(parallel_group_cancellation_cleans_only_owned_workers)
{
    const char *actions[] = { "parallel-timeout", "parallel-int", "parallel-term" };
    for (int i = 0; i < 3; i++) {
        reset_parallel();
        REQUIRE(run(actions[i], NULL) == (i ? 130 : 124));
        REQUIRE(exists("parallel/a/started") && exists("parallel/b/started"));
        REQUIRE(!exists("parallel/a/escaped") && !exists("parallel/b/escaped"));
        REQUIRE(!exists("parallel/third"));
        char *output = tool_read(logpath);
        REQUIRE(output && strstr(output, "[third] not run: group stopped"));
        free(output);
        REQUIRE(exists("parallel/sibling"));
    }
    return true;
}

TEST_CASE(exit_sequence_and_optional_timeout)
{
    REQUIRE(run("exit", "0") == 0);
    REQUIRE(run("exit", "7") == 7);
    REQUIRE(run("aggregate", NULL) == 124);
    REQUIRE(!exists("last-command"));
    REQUIRE(run("timeout-options", NULL) == 0);
    REQUIRE(!exists("last-command"));
    REQUIRE(run("sequence", NULL) == 0);
    REQUIRE(exists("last-command"));
    reset_markers();
    return true;
}

TEST_CASE(descendant_cleanup)
{
    reset_markers();
    REQUIRE(run("descendant", NULL) == 124);
    REQUIRE(exists("started"));
    REQUIRE(!exists("escaped"));
    return true;
}

TEST_CASE(portable_nested_cleanup_preserves_sibling)
{
    reset_markers();
    REQUIRE(run("portable", NULL) == 124);
    REQUIRE(exists("started"));
    REQUIRE(exists("cleanup-entered"));
    REQUIRE(!exists("escaped"));
    REQUIRE(exists("sibling"));
    return true;
}

TEST_CASE(unavailable_discovery_refuses_before_launch)
{
    reset_markers();
    setenv("CHIBI_POLICY_FIXTURE", "no-reader", 1);
    int status = run("no-reader", NULL);
    unsetenv("CHIBI_POLICY_FIXTURE");
    REQUIRE(status == 2);
    REQUIRE(!exists("last-command"));
    char *output = tool_read(logpath);
    bool refused = output && strstr(output, "no test started");
    free(output);
    REQUIRE(refused);
    return true;
}

TEST_CASE(slow_process_inspection_still_launches_work)
{
    reset_markers();
    setenv("CHIBI_POLICY_FIXTURE", "slow-ps", 1);
    int status = run("slow-ps", NULL);
    unsetenv("CHIBI_POLICY_FIXTURE");
    REQUIRE(status == 0);
    REQUIRE(exists("started"));
    REQUIRE(exists("last-command"));
    REQUIRE(!exists("escaped"));
    reset_markers();
    return true;
}

TEST_CASE(stuck_process_inspection_refuses_work_and_reaps_probe)
{
    reset_markers();
    setenv("CHIBI_POLICY_FIXTURE", "hung-ps", 1);
    int status = run("hung-ps", NULL);
    unsetenv("CHIBI_POLICY_FIXTURE");
    REQUIRE(status == 2);
    REQUIRE(exists("started"));
    REQUIRE(!exists("last-command"));
    char *output = tool_read(logpath);
    bool refused = output && strstr(output, "timed out") &&
                   strstr(output, "no test started");
    free(output);
    REQUIRE(refused);
    reset_markers();
    return true;
}

#ifdef __linux__
TEST_CASE(nested_cancellation_cleans_detached_descendants)
{
    reset_markers();
    REQUIRE(run("nested", NULL) == 124);
    REQUIRE(exists("started"));
    REQUIRE(!exists("escaped"));
    return true;
}

TEST_CASE(orphan_setsid_cleanup)
{
    reset_markers();
    REQUIRE(run("orphan", NULL) == 0);
    REQUIRE(exists("started"));
    REQUIRE(!exists("escaped"));
    return true;
}

TEST_CASE(missing_proc_uses_portable_discovery)
{
    reset_markers();
    REQUIRE(run("missing-proc", NULL) == 124);
    REQUIRE(exists("started"));
    REQUIRE(!exists("escaped"));
    return true;
}

TEST_CASE(pending_termination_still_cleans_up)
{
    reset_markers();
    REQUIRE(run("pending", NULL) == 124);
    REQUIRE(exists("started"));
    REQUIRE(!exists("escaped"));
    return true;
}
#endif

/* Fixed native assertions over the opt-in runner's validation functions. */
TEST_CASE(debug_experiment_flags)
{
    const char *valid = "{\"arguments\":[\"-g\",\"-O1\",\"-fsanitize=address,"
                        "undefined\","
                        "\"-fno-omit-frame-pointer\",\"-UNDEBUG\"],\"level\":1,"
                        "\"assertions\":true}";
    REQUIRE(run("experiment-flags", valid) == 0);
    REQUIRE(
        run("experiment-flags", "{\"arguments\":[\"-g\",\"-O0\",\"-fsanitize=address,undefined\",\"-fno-omit-frame-pointer\",\"-UNDEBUG\"],\"level\":0,\"assertions\":true}") ==
        0
    );
    const char *bad[] = { "-O3", "-DNDEBUG", "-g0", "-fomit-frame-pointer", "-fno-sanitize=undefined" };
    for (int i = 0; i < 5; i++) {
        char arguments[512];
        snprintf(
            arguments,
            sizeof arguments,
            "{\"arguments\":[\"-g\",\"-O1\",\"-fsanitize=address,undefined\","
            "\"-fno-omit-frame-pointer\",\"-UNDEBUG\",\"%s\"],\"level\":1,"
            "\"assertions\":true}",
            bad[i]
        );
        REQUIRE(run("experiment-flags", arguments) != 0);
    }
    return true;
}

static void experiment_evidence(bool complete, bool exported, bool database)
{
    char name[PATH_MAX];
    path(name, "build/test/tools/testing/include");
    const char *mkdir_argv[] = { "mkdir", "-p", name, NULL };
    assert(tool_run(mkdir_argv, tool_root(), logpath) == 0);
    const char *targets[] = { "chibi_engine",
                              "chibi_compiler",
                              "test_tools",
                              "test_language",
                              "chibi_ui_host" };
    const char *sources[] = { "src/runtime/engine.c",
                              "src/language/lower.c",
                              "test/tools/main.c",
                              "test/language/main.c",
                              "src/ui/rml.cpp" };
    path(name, "build/compile_commands.json");
    FILE *db = fopen(name, "w");
    path(name, "o1-build.log");
    FILE *log = fopen(name, "w");
    assert(db && log);
    fprintf(db, "[");
    for (int i = 0; i < 5; i++) {
        const char *flags = "-g -O1 -fsanitize=address,undefined "
                            "-fno-omit-frame-pointer -UNDEBUG";
        if (database || i < 4) {
            fprintf(
                db,
                "%s{\"file\":\"/source/%s\",\"command\":\"/usr/bin/clang-18 %s "
                "-o CMakeFiles/%s.dir/a.o -c /source/%s\"}",
                i ? "," : "",
                sources[i],
                flags,
                targets[i],
                sources[i]
            );
        }
        if (complete || i) {
            fprintf(
                log,
                "cd /build && /usr/bin/clang-18 %s -MD -MT a.o -MF a.d -o "
                "CMakeFiles/%s.dir/a.o -c /source/%s\n",
                flags,
                targets[i],
                sources[i]
            );
        }
    }
    fprintf(db, "]\n");
    assert(!fclose(db) && !fclose(log));
    path(name, "build/test/tools/testing/include/export_compiler.h");
    FILE *header = fopen(name, "w");
    assert(header);
    fprintf(header, "\"/usr/bin/clang-18\",\n\"-g\",\n\"-O1\",\n\"-fsanitize=address,undefined\",\n\"-fno-omit-frame-pointer\",\n%s", exported ? "\"-UNDEBUG\",\n" : "");
    assert(!fclose(header));
    path(name, "build/CMakeCache.txt");
    tool_write(name, "CHIBI_THIRD_PARTY_DIR:INTERNAL=/same/archive\n");
}

TEST_CASE(debug_experiment_compilation_evidence)
{
    experiment_evidence(true, true, true);
    REQUIRE(run("experiment-verify", NULL) == 0);
    experiment_evidence(false, true, true);
    REQUIRE(run("experiment-verify", NULL) != 0);
    experiment_evidence(true, false, true);
    REQUIRE(run("experiment-verify", NULL) != 0);
    experiment_evidence(true, true, false);
    REQUIRE(run("experiment-verify", NULL) != 0);
    return true;
}

TEST_CASE(debug_experiment_cancellation_and_failure_clean_up)
{
    reset_markers();
    REQUIRE(run("experiment-process", "cancel") == 130);
    REQUIRE(exists("started") && !exists("escaped"));
    REQUIRE(exists("cancel.log"));
    char file[PATH_MAX];
    path(file, "timings.jsonl");
    char *timings = tool_read(file);
    REQUIRE(timings && strstr(timings, "\"exit\": 130"));
    free(timings);
    reset_markers();
    REQUIRE(run("experiment-process", "error") == 2);
    REQUIRE(exists("started") && !exists("escaped"));
    REQUIRE(exists("error-failure.log"));
    timings = tool_read(file);
    REQUIRE(timings && strstr(timings, "\"exit\": 7"));
    free(timings);
    reset_markers();
    REQUIRE(run("experiment-process", "timeout") == 2);
    REQUIRE(exists("started") && !exists("escaped"));
    REQUIRE(exists("timeout.log"));
    timings = tool_read(file);
    REQUIRE(timings && strstr(timings, "\"exit\": 124"));
    free(timings);
    reset_markers();
    return true;
}

static void set_up()
{
    strcpy(directory, "/tmp/chibi-policy-XXXXXX");
    assert(mkdtemp(directory));
    path(driver, "driver.py");
    path(logpath, "output.log");
    FILE *script = fopen(driver, "wb");
    assert(script);
    assert(fputs(driver_imports, script) >= 0);
    assert(fputs(driver_setup, script) >= 0);
    assert(fputs(driver_routes, script) >= 0);
    assert(fputs(driver_ci, script) >= 0);
    assert(fputs(driver_ci_build, script) >= 0);
    assert(fputs(driver_experiment, script) >= 0);
    assert(fputs(driver_parallel, script) >= 0);
    assert(fputs(driver_source, script) >= 0);
    assert(fclose(script) == 0);
}

void test_tool_test_policy()
{
    set_up();
    RUN_TEST_CASE(debug_experiment_flags);
    RUN_TEST_CASE(debug_experiment_compilation_evidence);
    RUN_TEST_CASE(debug_experiment_cancellation_and_failure_clean_up);
    RUN_TEST_CASE(asset_language_cases_are_in_the_actual_ci_group);
    RUN_TEST_CASE(material_cpu_routes_reach_exact_dispatcher);
    RUN_TEST_CASE(project_group_keeps_all_project_commands_out_of_native);
    RUN_TEST_CASE(group_selectors_are_unique_and_controller_users_are_projects);
    RUN_TEST_CASE(ci_groups_share_one_build_and_stop_on_failure);
    RUN_TEST_CASE(ci_initial_build_keeps_targets_profiles_and_failures);
    RUN_TEST_CASE(ci_preserves_failed_macos_graph_stage_log);
    RUN_TEST_CASE(studio_acceptance_requires_exact_completion);
    RUN_TEST_CASE(studio_camera_requires_thirty_clean_frames);
    RUN_TEST_CASE(studio_gizmo_requires_forty_two_clean_frames);
    RUN_TEST_CASE(studio_collider_requires_twenty_six_clean_frames);
    RUN_TEST_CASE(studio_resources_require_recorded_generation);
    RUN_TEST_CASE(studio_acceptance_source_records_match_files);
    RUN_TEST_CASE(benchmark_runner_timeout_is_optional);
    RUN_TEST_CASE(benchmark_runner_explicit_timeout_cleans_descendants);
    RUN_TEST_CASE(parallel_diagnostics_report_observed_overlap_and_resources);
    RUN_TEST_CASE(parallel_diagnostics_distinguish_failure_cancel_and_not_started);
    RUN_TEST_CASE(parallel_diagnostics_explain_serial_escape_and_capacity);
    RUN_TEST_CASE(parallel_group_overlaps_and_isolates_scratch);
    RUN_TEST_CASE(parallel_group_serializes_conflicts_and_unknowns);
    RUN_TEST_CASE(parallel_group_preserves_manifest_and_failure);
    RUN_TEST_CASE(parallel_group_failure_stops_active_peer_and_queued_work);
    RUN_TEST_CASE(parallel_language_manifest_matches_serial_exactly);
    RUN_TEST_CASE(parallel_admission_bounds_cpu_and_available_memory);
    RUN_TEST_CASE(parallel_serial_escape_uses_real_cli_and_environment);
    RUN_TEST_CASE(parallel_memory_reader_respects_limits_and_unknowns);
    RUN_TEST_CASE(parallel_spawn_cancellation_never_loses_child_ownership);
    RUN_TEST_CASE(parallel_group_cancellation_cleans_only_owned_workers);
    RUN_TEST_CASE(exit_sequence_and_optional_timeout);
    RUN_TEST_CASE(descendant_cleanup);
    RUN_TEST_CASE(portable_nested_cleanup_preserves_sibling);
    RUN_TEST_CASE(unavailable_discovery_refuses_before_launch);
    RUN_TEST_CASE(slow_process_inspection_still_launches_work);
    RUN_TEST_CASE(stuck_process_inspection_refuses_work_and_reaps_probe);
#ifdef __linux__
    RUN_TEST_CASE(nested_cancellation_cleans_detached_descendants);
    RUN_TEST_CASE(orphan_setsid_cleanup);
    RUN_TEST_CASE(missing_proc_uses_portable_discovery);
    RUN_TEST_CASE(pending_termination_still_cleans_up);
#endif
    tool_remove_tree(directory);
}
