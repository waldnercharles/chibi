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
static const char driver_shards
    [] = "if sys.argv[2] == 'shards-manifest':\n"
         "    names = list(policy.GROUPS['language'])\n"
         "    if 'language-scalar-native' not in names: "
         "names.append('language-scalar-native')\n"
         "    names.append('future-unmeasured-selector')\n"
         "    for native in (True, False):\n"
         "        selected = [n for n in names if native or n != "
         "'language-scalar-native']\n"
         "        for count in (1, 2, 3):\n"
         "            shards = policy.language_shards(selected, count)\n"
         "            flat = sum(shards, [])\n"
         "            print(len(shards) == count, len(flat) == len(selected),\n"
         "                  len(set(flat)) == len(flat), set(flat) == "
         "set(selected),\n"
         "                  shards == policy.language_shards(selected, "
         "count),\n"
         "                  all(lane == [n for n in selected if n in lane] for "
         "lane in shards),\n"
         "                  count != 1 or shards == [selected],\n"
         "                  flat.count('future-unmeasured-selector') == 1)\n"
         "    for names, count in ((['a', 'a'], 3), (['a'], 0), (['a'], 4)):\n"
         "        try: policy.language_shards(names, count)\n"
         "        except ValueError: print('rejected')\n"
         "        else: print('accepted')\n"
         "    sys.exit(0)\n"
         "if sys.argv[2] == 'shards-cli':\n"
         "    arguments = json.loads(sys.argv[5])\n"
         "    policy.GROUPS['language'] = ['alpha', 'beta', 'gamma', 'delta']\n"
         "    def record(names, jobs=None, seconds=None):\n"
         "        print('RUN', ' '.join(names), jobs, seconds)\n"
         "        return 17\n"
         "    policy.run_group = record\n"
         "    sys.argv = ['test_policy.py', *arguments]\n"
         "    sys.exit(policy.main())\n";

static const char driver_shard_profile[] =
    "if sys.argv[2] == 'shard-ci-profile':\n"
    "    import runpy, tempfile\n"
    "    source = "
    "Path('.github/workflows/"
    "language-shards-platform.yml').read_text().splitlines()\n"
    "    steps, name = {}, None\n"
    "    for line in source:\n"
    "        if line.startswith('      - name: '):\n"
    "            name = line.split('- name: ', 1)[1]\n"
    "            steps[name] = []\n"
    "        elif name is not None:\n"
    "            steps[name].append(line.strip())\n"
    "    select = steps.get('Select full sanitizer profile', [])\n"
    "    build = steps.get('Build the sanitized drivers once', [])\n"
    "    verify = steps.get('Verify native profile evidence', [])\n"
    "    generation = steps.get('Build Release generation and planning hosts', "
    "[])\n"
    "    helper = Path('.github/native-profile.py')\n"
    "    expected = runpy.run_path(str(helper))['settings']('ON')\n"
    "    with tempfile.TemporaryDirectory(dir=sys.argv[4]) as directory:\n"
    "        output = Path(directory) / 'environment'\n"
    "        env = dict(os.environ, CHIBI_CI_SANITIZERS='ON', "
    "GITHUB_ENV=str(output))\n"
    "        subprocess.run([sys.executable, str(helper)], env=env, "
    "check=True, stdout=sub"
    "process.DEVNULL)\n"
    "        actual = dict(line.split('=', 1) for line in "
    "output.read_text().splitlines())\n"
    "    command = 'make build BUILD_TARGET=\\'test_tools test_language "
    "chibi-compiler-host"
    "\\' ASSET_GENERATOR_HOSTS=build-release/sdk LIBCLANG_ROOT=\"$LLVM\" 2>&1 "
    "| tee \"$RUNNER_"
    "TEMP/native-build.log\"'\n"
    "    generation_command = 'make build-release "
    "BUILD_TARGET=\\'chibi-build-host chibi-co"
    "mpiler-host\\' LIBCLANG_ROOT=\"$LLVM\"'\n"
    "    sequence = ['Select full sanitizer profile', 'Build Release "
    "generation and planni"
    "ng hosts', 'Build the sanitized drivers once', 'Verify native profile "
    "evidence', 'Sta"
    "ge exact-source runtime closure and selector manifest']\n"
    "    print(\"  CHIBI_CI_SANITIZERS: 'ON'\" in source,\n"
    "          'run: python3 .github/native-profile.py' in select,\n"
    "          actual == expected and actual['BUILD_PROFILE'] == 'sanitizer',\n"
    "          command in build and \"VERBOSE: '1'\" in build,\n"
    "          'shell: bash' in build and 'set -o pipefail' in build,\n"
    "          'run: python3 .github/verify-native-profile.py' in verify,\n"
    "          [list(steps).index(name) for name in sequence] == "
    "sorted(list(steps).index("
    "name) for name in sequence) and generation_command in generation and not "
    "any('if [' i"
    "n line for line in generation) and 'echo "
    "\"BUILD_HOST=$PWD/build-release/sdk/chibi-bui"
    "ld-host\" >> \"$GITHUB_ENV\"' in generation,\n"
    "          sum(line.strip().startswith('make build ') for line in source) "
    "== 1 and not"
    " any(line.startswith('if:') for line in select + build + verify))\n"
    "    print('native group fixture remains covered')\n"
    "    sys.exit(0)\n";

static const char *const driver_artifact[] = {
    "if sys.argv[2] == 'artifact-cli':\n    import language_test_artifact as "
    "artifact\n    sys.argv = ['language_test_artifact.py', "
    "*json.loads(sys.argv[5])]\n    artifact.main()\n    sys.exit(0)\nif "
    "sys.argv[2] == 'artifact':\n    import tempfile, shutil\n    import "
    "language_test_artifact as artifact\n    case = sys.argv[5]\n    with "
    "tempfile.TemporaryDirectory(prefix='artifact-', dir=sys.argv[4]) as "
    "temporary:\n        base = Path(temporary).resolve()\n        root = base "
    "/ 'source'\n        root.mkdir()\n        artifact.ROOT = root\n        "
    "def git(*args):\n            subprocess.run(['git', *args], cwd=root, "
    "check=True,\n                           stdout=subprocess.DEVNULL, "
    "stderr=subprocess.DEVNULL)\n        git('init')\n        (root / "
    "'source.chibi').write_text('source\\n')\n        git('add', "
    "'source.chibi')\n        git('-c', 'user.name=Fixture', '-c', "
    "'user.email=fixture@example.invalid',\n            '-c', "
    "'commit.gpgsign=false', 'commit', '-m', 'fixture')\n        resource = "
    "base / 'resource'\n        (resource / 'lib').mkdir(parents=True)\n       "
    " runtime = resource / 'lib/libclang_rt.asan.so'\n        "
    "runtime.write_text('runtime')\n        compiler = base / 'clang'\n        "
    "compiler.write_text('#!/bin/sh\\nif [ \"$1\" = --print-resource-dir ]; "
    "then echo \"' +\n                            str(resource) + '\"; else "
    "echo fixture-clang; fi\\n')\n        compiler.chmod(0o755)\n        "
    "os.environ['CC'] = str(compiler)\n        for name in artifact.FILES:\n   "
    "         file = root / name\n            file.parent.mkdir(parents=True, "
    "exist_ok=True)\n            file.write_text(str(root / 'source.chibi') + "
    "'\\n' if name.endswith('corpus.txt')\n                            else "
    "'fixture executable\\n')\n            file.chmod(0o644 if "
    "name.endswith('corpus.txt') else 0o755)\n        stage = base / 'stage'\n "
    "       artifact.pack(stage)\n        binary = stage / artifact.FILES[0]\n "
    "       metadata_file = stage / 'manifest.json'\n        metadata = "
    "json.loads(metadata_file.read_text())\n        if "
    "case.startswith('archive-'):\n            import tarfile, io\n            "
    "archive = base / 'runtime.tar'\n            destination = base / "
    "'unpacked'\n            if case == 'archive-corrupt': "
    "binary.write_text('bad hash')\n            if case == "
    "'archive-identity':\n                metadata['commit'] = 'wrong'\n       "
    "         metadata_file.write_text(json.dumps(metadata))\n            with "
    "tarfile.open(archive, 'w') as package:\n                for index, name "
    "in enumerate((*artifact.FILES, 'manifest.json')):\n                    "
    "file = stage / name\n                    info = package.gettarinfo(file, "
    "arcname=name)\n                    if index == 0:\n                       "
    " if case == 'archive-traversal': info.name = '../escape'\n                "
    "        elif case == 'archive-absolute': info.name = '/tmp/escape'\n",
    "                        elif case == 'archive-symlink': info.type = "
    "tarfile.SYMTYPE\n                        elif case == 'archive-hardlink': "
    "info.type = tarfile.LNKTYPE\n                        elif case == "
    "'archive-special': info.type = tarfile.FIFOTYPE\n                        "
    "elif case == 'archive-directory': info.type = tarfile.DIRTYPE\n           "
    "             elif case == 'archive-mode': info.mode = 0o4755\n            "
    "            if info.issym() or info.islnk(): info.linkname = str(root / "
    "'source.chibi')\n                    with file.open('rb') as content: "
    "package.addfile(info, content if info.isfile() else None)\n               "
    " if case == 'archive-duplicate':\n                    package.add(stage / "
    "artifact.FILES[0], arcname=artifact.FILES[0], recursive=False)\n          "
    "      if case == 'archive-extra':\n                    package.add(stage "
    "/ artifact.FILES[0], arcname='extra', recursive=False)\n            if "
    "case == 'archive-truncated': "
    "archive.write_bytes(archive.read_bytes()[:300])\n            try: "
    "artifact.unpack(archive, destination)\n            except (OSError, "
    "ValueError, KeyError, TypeError, tarfile.TarError):\n                "
    "print('rejected', not destination.exists(),\n                      (root "
    "/ 'source.chibi').read_text() == 'source\\n')\n                "
    "sys.exit(2)\n            print('unpacked', all((destination / "
    "name).read_bytes() == (stage / name).read_bytes()\n                       "
    "           for name in artifact.FILES))\n            sys.exit(0)\n        "
    "if case == 'roundtrip':\n            artifact.verify(stage)\n            "
    "shutil.rmtree(root / 'build')\n            artifact.restore(stage)\n      "
    "      print(all((root / name).read_bytes() == (stage / "
    "name).read_bytes()\n                      for name in artifact.FILES))\n  "
    "          print(all((root / name).stat().st_mode == (stage / "
    "name).stat().st_mode\n                      for name in "
    "artifact.FILES))\n            sys.exit(0)\n        if case == 'hash': "
    "binary.write_text('damaged')\n        elif case == 'mode': "
    "binary.chmod(0o644)\n        elif case == 'missing': binary.unlink()\n    "
    "    elif case == 'extra': (stage / 'extra').write_text('extra')\n        "
    "elif case == 'extra-directory': (stage / 'extra').mkdir()\n        elif "
    "case == 'symlink':\n            binary.unlink()\n            "
    "binary.symlink_to(root / artifact.FILES[0])\n        elif case == "
    "'special':\n            binary.unlink()\n            os.mkfifo(binary)\n  "
    "      elif case == 'runtime': runtime.write_text('different runtime')\n   "
    "     elif case == 'dirty': (root / 'source.chibi').write_text('dirty')\n  "
    "      elif case == 'corpus':\n            file = stage / "
    "'build/test/language/corpus.txt'\n            file.write_text(str(root / "
    "'../outside.chibi') + '\\n')\n",
    "            metadata['files']['build/test/language/corpus.txt']['sha256'] "
    "= artifact.digest(file)\n            "
    "metadata_file.write_text(json.dumps(metadata))\n        elif case == "
    "'destination-symlink':\n            destination = root / "
    "artifact.FILES[0]\n            destination.unlink()\n            "
    "destination.symlink_to(root / 'source.chibi')\n        elif case in "
    "('source_root', 'commit', 'run', 'language_manifest', 'machine', "
    "'compiler'):\n            metadata[case] = 'different identity'\n         "
    "   metadata_file.write_text(json.dumps(metadata))\n        try:\n         "
    "   artifact.restore(stage)\n        except (OSError, ValueError, "
    "KeyError, TypeError, subprocess.CalledProcessError):\n            "
    "print('rejected')\n            print((root / 'source.chibi').read_text() "
    "== ('dirty' if case == 'dirty' else 'source\\n'))\n            "
    "sys.exit(2)\n        print('accepted')\n        sys.exit(0)\n",
};

static const char driver_routes
    [] = "if sys.argv[2] == 'asset-host-platforms':\n"
         "    print(' '.join(policy.ASSET_COMPILER_HOST_PLATFORMS))\n"
         "    sys.exit(0)\n"
         "if sys.argv[2] == 'wasm-benchmark-routes':\n"
         "    print(' '.join(n for n in policy.GROUPS['wasm']\n"
         "                   if n.endswith('-benchmark')))\n"
         "    for name in sorted(policy.TOOLS):\n"
         "        if name.startswith('wasm-') and "
         "name.endswith('-benchmark'):\n"
         "            print(name, policy.tool_command(name)[-1])\n"
         "    sys.exit(0)\n"
         "if sys.argv[2] == 'inner-pool-route':\n"
         "    name = sys.argv[5]\n"
         "    print(int(name in policy.PARALLEL_RESOURCES),\n"
         "          policy.GROUPS['language'].count(name))\n"
         "    if name == 'language-package-compiler-chibi-scalar':\n"
         "        command = policy.tool_command(name)\n"
         "        print(len(command), Path(command[0]).name, *command[1:])\n"
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
/* Exercise real audited metadata with bounded native rendezvous workers. */
static const char driver_integer_parallel
    [] = "if sys.argv[2] == 'integer-parallel':\n"
         "    root = str(Path(sys.argv[4]) / 'parallel')\n"
         "    Path(root).mkdir(exist_ok=True)\n"
         "    names = [sys.argv[5], 'language-asset-packaging-chibi']\n"
         "    for name, alias in zip(names, ('first', 'second')):\n"
         "        print(name, policy.PARALLEL_RESOURCES.get(name),\n"
         "              policy.GROUPS['language'].count(name), flush=True)\n"
         "        policy.TOOLS[name] = [sys.argv[3], 'parallel', root,\n"
         "                              alias, 'barrier']\n"
         "    print('migration-exclusive', "
         "'language-migration34-generic-repro'\n"
         "          not in policy.PARALLEL_RESOURCES, flush=True)\n"
         "    sys.exit(policy.run_group(names, jobs=2))\n";
/* Standalone developer targets still build their prerequisites and stop on
 * failure. Record build/test boundaries without an engine build. */
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
         "    commands = ['make -j1 test-native test-materials test-projects "
         "test-language-all']\n"
         "    jobs = dict(native=commands, macos=commands)\n"
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
/* Execute actual CI shell blocks with build/driver boundaries replaced. */
static const char driver_ci_ready_setup[] =
    "if sys.argv[2] == 'ci-ready-groups':\n"
    "    root = Path(sys.argv[4]) / 'ready'\n"
    "    root.mkdir(exist_ok=True)\n"
    "    trace = root / 'trace'\n"
    "    request = sys.argv[5]\n"
    "    binaries = ['build/test/tools/test_tools', "
    "'build/test/language/test_language',\n"
    "                'build-release/bin/chibi_ctl', "
    "'build-release/sdk/chibi',\n"
    "                'build-release/sdk/chibi-compile']\n"
    "    for binary in binaries:\n"
    "        path = root / binary\n"
    "        path.parent.mkdir(parents=True, exist_ok=True)\n"
    "        path.write_text('#!/bin/sh\\nexit 99\\n')\n"
    "        path.chmod(0o700)\n"
    "    if request.startswith('missing:'):\n"
    "        (root / binaries[int(request.split(':')[1])]).unlink()\n"
    "    recorder = root / 'record'\n"
    "    recorder.write_text('#!/bin/sh\\nset -e\\n'\n"
    "        'case \"$1\" in\\n'\n"
    "        'tools/test_policy.py) test \"$2\" = group; step=$3; '\n"
    "        'test \"$CHIBI_TOOLS_BUILD_DIR\" = build/test/tools; '\n"
    "        'test \"$CHIBI_LANGUAGE_BUILD_DIR\" = build/test/language; '\n"
    "        'if [ \"$step\" = projects ]; then test \"$CHIBI_CTL\" = "
    "build-release/bin/chibi_ctl; '\n"
    "        'test \"$CHIBI_CLI\" = build-release/sdk/chibi; '\n"
    "        'test \"$CHIBI_COMPILE\" = build-release/sdk/chibi-compile; '\n"
    "        'else test -z \"${CHIBI_CTL-}${CHIBI_CLI-}${CHIBI_COMPILE-}\"; "
    "fi; '\n"
    "        'if [ \"$step\" = language ] || [ \"$step\" = projects ]; then "
    "test \"$CHIBI_LLVM_"
    "BIN\" = \"$CI_LLVM\"; fi; '\n"
    "        'if [ \"$step\" = language ] || [ \"$step\" = projects ]; then "
    "test \"${CHIBI_NATI"
    "VE_BUNDLE-}\" = \"$CI_BUNDLE\"; fi;;\\n'\n"
    "        '*) exit 99;;\\nesac\\n'\n"
    "        'printf \"%s\\\\n\" \"$step\" >> \"$CI_TRACE\"\\n'\n"
    "        '[ \"$step\" != \"$CI_FAIL\" ]\\n')\n"
    "    recorder.chmod(0o700)\n"
    "    for command in ['python3', 'make']:\n"
    "        path = root / command\n"
    "        path.unlink(missing_ok=True)\n"
    "        path.symlink_to(recorder)\n";
static const char driver_ci_ready_run
    [] = "    jobs = {'native': [], 'native-macos': []}\n"
         "    bundles = {}\n"
         "    job, selected, capture = None, False, False\n"
         "    built, verified, released = set(), set(), set()\n"
         "    for line in "
         "Path('.github/workflows/ci.yml').read_text().splitlines():\n"
         "        if line.startswith('  ') and not line.startswith('   '):\n"
         "            job = line.strip().removesuffix(':')\n"
         "        if job not in jobs:\n"
         "            continue\n"
         "        command = line.strip().removeprefix('run: ')\n"
         "        if command.startswith('make build-release BUILD_TARGET='):\n"
         "            released.add(job)\n"
         "        if command.startswith('make build BUILD_TARGET='):\n"
         "            built.add(job)\n"
         "        if command == 'python3 .github/verify-native-profile.py':\n"
         "            if job not in built:\n"
         "                raise RuntimeError('profile verification must follow "
         "the build')\n"
         "            verified.add(job)\n"
         "        if line.startswith('      - name:'):\n"
         "            selected = line.strip() == '- name: Native, material, "
         "project and language selectors'\n"
         "            capture = False\n"
         "            if selected and (job not in released or job not in "
         "verified):\n"
         "                raise RuntimeError('tests require Release helpers "
         "and profile-verified drivers')\n"
         "        bundle = '          CHIBI_NATIVE_BUNDLE: '\n"
         "        if selected and not capture and line.startswith(bundle):\n"
         "            bundles[job] = line.removeprefix(bundle)\n"
         "        if selected and line == '        run: |':\n"
         "            capture = True\n"
         "        elif capture and line.startswith('          '):\n"
         "            jobs[job].append(line[10:])\n"
         "        elif capture:\n"
         "            capture = False\n"
         "    for job, commands in jobs.items():\n"
         "        trace.write_text('')\n"
         "        env = dict(os.environ, PATH=str(root) + os.pathsep + "
         "os.environ['PATH'],\n"
         "                   CI_TRACE=str(trace), CI_FAIL=request,\n"
         "                   CI_LLVM='/usr/lib/llvm-18/bin' if job == 'native' "
         "else '',\n"
         "                   CI_BUNDLE=str(root / job / 'native-bundle') if "
         "job "
         "== 'native' else '')\n"
         "        env.pop('CHIBI_NATIVE_BUNDLE', None)\n"
         "        if job in bundles:\n"
         "            env['CHIBI_NATIVE_BUNDLE'] = bundles[job].replace("
         "'${{ runner.temp }}', str(root / job))\n"
         "        result = subprocess.run(['bash', '-e', '-c', "
         "'\\n'.join(commands)],\n"
         "                                cwd=root, env=env, "
         "stdout=subprocess.DEVNULL,\n"
         "                                stderr=subprocess.DEVNULL)\n"
         "        print('status=' + str(result.returncode))\n"
         "        print(trace.read_text(), end='')\n"
         "    sys.exit(0)\n";

/* Record the initial workflow builds through the real Makefile on both routes. */
static const char driver_ci_build
    [] = "if sys.argv[2] == 'ci-initial-build':\n"
         "    import shlex\n"
         "    root = Path(sys.argv[4])\n"
         "    sanitizer, request = sys.argv[5].split(':', 1)\n"
         "    cmake = request.startswith('cmake:')\n"
         "    request = request.removeprefix('cmake:')\n"
         "    if request == 'alias':\n"
         "        alias = root / 'logical-root'\n"
         "        alias.unlink(missing_ok=True)\n"
         "        alias.symlink_to(root, target_is_directory=True)\n"
         "        root = alias\n"
         "    trace = root / 'ci-build-trace'\n"
         "    recorder = root / 'ci-build-record.py'\n"
         "    recorder.write_text('import json, os, sys\\n'\n"
         "        'with open(os.environ[\"CI_TRACE\"], \"a\") as log: "
         "log.write(json.dumps(sys.argv"
         "[1:]) + \"\\\\n\")\\n'\n"
         "        'sys.exit(1 if sys.argv[1] == os.environ[\"CI_FAIL\"] else "
         "0)\\n')\n"
         "    host = root / 'ci-build-host'\n"
         "    cold = request.startswith('cold:')\n"
         "    failure = request.removeprefix('cold:')\n"
         "    if cold:\n"
         "        host.unlink(missing_ok=True)\n"
         "    else:\n"
         "        host.write_text('#!/bin/sh\\n[ \"$1\" = --protocol-version ] "
         "|| exit 99\\nprintf "
         "\"14\\\\n\"\\n')\n"
         "        host.chmod(0o700)\n"
         "    (root / 'Makefile').write_bytes(Path('Makefile').read_bytes())\n"
         "    env = dict(os.environ, CI_TRACE=str(trace), CI_FAIL=failure, "
         "MAKEFLAGS='', MFLAGS='')\n"
         "    platform_bin = root / 'ci-platform'\n"
         "    platform_bin.mkdir(exist_ok=True)\n"
         "    uname = platform_bin / 'uname'\n"
         "    uname.write_text('#!/bin/sh\\nprintf \\\"%s\\\\n\\\" "
         "\\\"$CI_PLATFORM\\\"\\n')\n"
         "    uname.chmod(0o700)\n"
         "    env['PATH'] = str(platform_bin) + os.pathsep + env['PATH']\n"
         "    env.pop('GRAPH_BUILD', None)\n"
         "    jobs = {'native': [], 'native-macos': []}\n"
         "    configured = set()\n"
         "    pipefail = set()\n"
         "    profile_file = root / 'ci-profile-env'\n"
         "    profile_file.write_text('')\n"
         "    subprocess.run([sys.executable, '.github/native-profile.py'], "
         "check=True,\n"
         "        env=dict(env, CHIBI_CI_SANITIZERS=sanitizer, "
         "GITHUB_ENV=str(profile_file)),\n"
         "        stdout=subprocess.DEVNULL)\n"
         "    profile = dict(line.split('=', 1) for line in "
         "profile_file.read_text().splitlines())\n"
         "    env.update(profile)\n";
static const char driver_ci_build_workflow
    [] = "    release = {}\n"
         "    release_first = {}\n"
         "    job = None\n"
         "    for line in "
         "Path('.github/workflows/ci.yml').read_text().splitlines():\n"
         "        if line.startswith('  ') and not line.startswith('   '):\n"
         "            job = line.strip().removesuffix(':')\n"
         "        command = line.strip().removeprefix('run: ')\n"
         "        if job in jobs and command == 'python3 "
         ".github/native-profile.py':\n"
         "            configured.add(job)\n"
         "        if job in jobs and command == 'set -o pipefail':\n"
         "            pipefail.add(job)\n"
         "        if job in jobs and command.startswith('make '):\n"
         "            words = shlex.split(command)\n"
         "            if words[1] == 'build-release':\n"
         "                release[job] = next((w.split('=', 1)[1] for w in "
         "words if w.startswit"
         "h('BUILD_TARGET=')), '')\n"
         "                release_first[job] = not jobs[job]\n"
         "            if words[1] == 'build':\n"
         "                if "
         "'BUILD_HOST=$PWD/build-release/sdk/chibi-build-host' not in words:\n"
         "                    raise RuntimeError('Graph CI must plan with the "
         "Release host')\n"
         "                if words[-4:] != ['2>&1', '|', 'tee', "
         "'$RUNNER_TEMP/native-build.log']:\n"
         "                    raise RuntimeError('initial build must retain "
         "its evidence pipeline')\n"
         "                jobs[job].append(words[:-4])\n"
         "    if configured != set(jobs) or pipefail != set(jobs):\n"
         "        raise RuntimeError('native CI jobs must select their shared "
         "profile')\n";
static const char driver_ci_build_run[] =
    "    for job, commands in jobs.items():\n"
    "        env['CI_PLATFORM'] = 'Darwin' if job == 'native-macos' else "
    "'Linux'\n"
    "        print('release-first=' + str(release_first.get(job, False)))\n"
    "        print('release-targets=' + release.get(job, ''))\n"
    "        if failure == 'o0':\n"
    "            commands = [c + ['SANITIZER_CMAKE_FLAGS=' +\n"
    "                profile['SANITIZER_CMAKE_FLAGS'].replace('-O1', '-O0'),\n"
    "                'BUILD_DEBUG_OPTIMIZATION=0'] for c in commands]\n"
    "        trace.write_text('')\n"
    "        status = 0\n"
    "        for command in commands:\n"
    "            arguments = [command[0], '-C', str(root),\n"
    "                *command[1:], 'BUILD_HOST=' + str(host),\n"
    "                'QUIET=' + sys.executable + ' ' + str(recorder), "
    "'JOBS=2']\n"
    "            if cmake:\n"
    "                arguments.append('GRAPH_BUILD=')\n"
    "            pipeline = shlex.join(arguments) + ' 2>&1 | tee ' + "
    "shlex.quote(str(root "
    "/ 'build.log'))\n"
    "            result = subprocess.run(['bash', '-o', 'pipefail', '-c', "
    "pipeline],\n"
    "                env=env, stdout=subprocess.DEVNULL, "
    "stderr=subprocess.DEVNULL)\n"
    "            status = result.returncode\n"
    "            if status: break\n"
    "        print(job, 'failed' if status else 'continued')\n"
    "        for line in trace.read_text().splitlines():\n"
    "            arguments = json.loads(line)\n"
    "            step = arguments[0]\n"
    "            print(step)\n"
    "            if step in ('graph', 'native-toolchain', "
    "'configure-native'):\n"
    "                generator = next((a.split('=', 1)[1] for a in arguments "
    "if a.startswi"
    "th('-DCHIBI_ASSET_GENERATOR_HOSTS=')), '')\n"
    "                if '--asset-generator-hosts' in arguments:\n"
    "                    generator = "
    "arguments[arguments.index('--asset-generator-hosts') + 1]\n"
    "                if generator and not Path(generator).is_absolute():\n"
    "                    raise ValueError('generator hosts must be absolute')\n"
    "                print('generators=' + "
    "(str(Path(generator).resolve().relative_to(root"
    ".resolve())) if generator else ''))\n"
    "            if step == 'graph':\n"
    "                print('targets=' + ','.join(arguments[i + 1] for i, arg "
    "in enumerate("
    "arguments) if arg == '--target'))\n"
    "                print('profile=' + arguments[arguments.index('--profile') "
    "+ 1])\n"
    "                print('optimization=' + "
    "arguments[arguments.index('--debug-optimization') + 1])\n"
    "                print('frames=' + "
    "(arguments[arguments.index('--debug-frame-pointers') + 1]\n"
    "                    if '--debug-frame-pointers' in arguments else "
    "'off'))\n"
    "                print('jobs=' + arguments[arguments.index('--jobs') + "
    "1])\n"
    "            if step == 'configure-native':\n"
    "                print('flags=' + ','.join(arg for arg in arguments if "
    "arg.startswith("
    "('-DCMAKE_BUILD_TYPE=', '-DCHIBI_ENABLE_SANITIZERS=', "
    "'-DCHIBI_SANITIZERS=', '-DCMAKE"
    "_C_FLAGS_DEBUG=', '-DCMAKE_CXX_FLAGS_DEBUG=', "
    "'-DCMAKE_OBJC_FLAGS_DEBUG='))))\n"
    "            if step == 'build':\n"
    "                print('targets=' + "
    "','.join(arguments[arguments.index('--target') + 1"
    ":arguments.index('--parallel')]))\n"
    "                print('jobs=' + arguments[arguments.index('--parallel') + "
    "1])\n"
    "            if step == 'native-toolchain':\n"
    "                print('targets=' + ','.join(arguments[i + 1] for i, arg "
    "in enumerate("
    "arguments) if arg == '--target'))\n"
    "                print('flags=' + ','.join(arg for arg in arguments if "
    "arg.startswith("
    "('-DCMAKE_BUILD_TYPE=', '-DCHIBI_ENABLE_SANITIZERS=', "
    "'-DCHIBI_SANITIZERS=', '-DCMAKE"
    "_C_FLAGS_DEBUG=', '-DCMAKE_CXX_FLAGS_DEBUG=', "
    "'-DCMAKE_OBJC_FLAGS_DEBUG='))))\n"
    "                print('jobs=' + arguments[arguments.index('--jobs') + "
    "1])\n"
    "    sys.exit(0)\n";
/* Trigger timeout cleanup after the native descendant publishes its complete
 * PID marker. The fixture clock controls this controller's deadline; production
 * wait/cleanup code and the existing outer startup guards remain active. */
static const char driver_experiment_clock
    [] = "def fixture_experiment_cli(experiment, root, mode):\n"
         "    if mode != 'timeout':\n"
         "        return experiment.cli()\n"
         "    clock = policy.time\n"
         "    epoch = clock.monotonic()\n"
         "    class ReadyTimeoutClock:\n"
         "        def monotonic(self):\n"
         "            try:\n"
         "                record = Path(root, 'started').read_text()\n"
         "                if record.endswith('\\n') and int(record) > 0:\n"
         "                    return epoch + .3\n"
         "            except (OSError, ValueError):\n"
         "                pass\n"
         "            return epoch\n"
         "        def __getattr__(self, name):\n"
         "            return getattr(clock, name)\n"
         "    policy.time = ReadyTimeoutClock()\n"
         "    try:\n"
         "        return experiment.cli()\n"
         "    finally:\n"
         "        policy.time = clock\n";
/* Inject the expensive workload; exercise the experiment's real CLI handlers
 * and policy supervisor over the existing native worker. */
static const char driver_experiment[] =
    "if sys.argv[2].startswith('experiment-'):\n"
    "    import debug_optimization_experiment as experiment\n"
    "    action, worker, root = sys.argv[2:5]\n"
    "    if action == 'experiment-flags':\n"
    "        options = json.loads(sys.argv[5])\n"
    "        experiment.flags(**options)\n"
    "        sys.exit(0)\n"
    "    if action == 'experiment-plan':\n"
    "        options = json.loads(sys.argv[5])\n"
    "        print(json.dumps(experiment.arms(**options)))\n"
    "        sys.exit(0)\n"
    "    if action == 'experiment-mutate':\n"
    "        mode = sys.argv[5]\n"
    "        db = Path(root) / 'build/compile_commands.json'\n"
    "        if mode == 'extra-ndebug':\n"
    "            rows = json.loads(db.read_text())\n"
    "            rows.append(dict(file='/extra.c', command=rows[0]['command'] "
    "+ ' -DNDEBUG'))\n"
    "            db.write_text(json.dumps(rows))\n"
    "        if mode == 'link-sanitizer':\n"
    "            log = Path(root) / 'o1-build.log'\n"
    "            log.write_text(log.read_text() + '/usr/bin/clang-18 -g -O1 "
    "-fno-omit-frame-pointer -o broken a.o\\n')\n"
    "        if mode == 'missing-links':\n"
    "            log = Path(root) / 'o1-build.log'\n"
    "            log.write_text('\\n'.join(line for line in "
    "log.read_text().splitlines() if ' -c ' in line))\n"
    "        sys.exit(0)\n"
    "    if action == 'experiment-configure':\n"
    "        options = json.loads(sys.argv[5])\n"
    "        print(json.dumps(experiment.configure_command(Path(root), "
    "**options)))\n"
    "        sys.exit(0)\n"
    "    if action == 'experiment-build-output':\n"
    "        command = experiment.build_command(Path(root))\n"
    "        print(json.dumps(command), flush=True)\n"
    "        native = command[command.index('--') + 1:] if '--' in "
    "command else []\n"
    "        if sys.argv[5] == 'unsynchronized': native = []\n"
    "        for name in ('a', 'b'):\n"
    "            Path(root, name + '-ready').unlink(missing_ok=True)\n"
    "        result = subprocess.run(['make', '-f', root + '/output.mk',\n"
    "            '-j4', *native], cwd=root, stdout=subprocess.PIPE,\n"
    "            stderr=subprocess.STDOUT, text=True, timeout=5)\n"
    "        print('status', result.returncode)\n"
    "        print('intact', sum(line in ('a' * 12000, 'b' * 12000)\n"
    "            for line in result.stdout.splitlines()))\n"
    "        sys.exit(0)\n"
    "    if action == 'experiment-verify':\n"
    "        options = json.loads(sys.argv[5]) if len(sys.argv) > 5 else {}\n"
    "        experiment.verify(Path(root) / 'build', Path(root), **options)\n"
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
    "        sys.exit(fixture_experiment_cli(experiment, root, sys.argv[5]))\n"
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
/* Keep each concatenated literal below ISO C's 4095-byte minimum limit.
 * Writing these chunks in order preserves the generated Python byte-for-byte. */
static const char *const driver_parallel[] = {
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
    /* This whole-manifest check exercises real scheduler admission, leaf workers,
     * and ordered output. Dedicated tests below cover supervisor containment.
     * Re-importing this fixture in one supervisor per no-descendant mark worker
     * adds startup work unrelated to manifest identity (49 launches today). */
    "    leaf_launches = []\n"
    "    if action == 'parallel-language':\n"
    "        original_popen = policy.subprocess.Popen\n"
    "        def leaf_popen(command, *args, **kwargs):\n"
    "            if len(command) == 4 and command[:3] == [sys.executable, "
    "str(Path(policy.__file__).resolve()), '_supervise']:\n"
    "                commands = json.loads(command[3])\n"
    "                if len(commands) == 1 and len(commands[0]) == 5 and "
    "commands[0][:3] == "
    "[worker, 'parallel', root] and commands[0][3] in names and "
    "commands[0][-1] == 'mark':\n"
    "                    command = commands[0]\n"
    "                    leaf_launches.append(command[3])\n"
    "            return original_popen(command, *args, **kwargs)\n"
    "        policy.subprocess.Popen = leaf_popen\n"
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
    "    if action in ('parallel-low-memory', 'parallel-memory-unknown', ",
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
    "        output = 'Mach Virtual Memory Statistics: (page size of 4096 ",
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
    "        for name, folder in zip(names, ['a', 'b']):\n",
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
    "    result = policy.run_group(names, jobs=jobs)\n"
    "    if action == 'parallel-language':\n"
    "        print('leaf-launches', len(leaf_launches))\n"
    "    sys.exit(result)\n"
};

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

static const char *driver_seed[] = {
    "if sys.argv[2] == 'seed-workflow':\n"
    "    import hashlib, shutil\n"
    "    root = Path(sys.argv[4]) / 'seed'\n"
    "    shutil.rmtree(root, ignore_errors=True)\n"
    "    root.mkdir()\n"
    "    workflow = "
    "Path('.github/workflows/compiler-runtime-seed.yml').read_text()\n"
    "    def block(name):\n"
    "        section = workflow.split('      - name: ' + name + '\\n', "
    "1)[1].split('\\n      - ', 1)[0]\n"
    "        return '\\n'.join(line[10:] for line in section.split('        "
    "run: |\\n', 1)[1].splitlines() if line.startswith('          '))\n"
    "    case = sys.argv[5]\n"
    "    sha, tree = 'a' * 40, 'b' * 40\n"
    "    tag = 'compiler-runtime-prepared22-' + sha\n"
    "    env = dict(os.environ, RUNNER_TEMP=str(root), GITHUB_SHA=sha, "
    "SOURCE_TREE=tree,\n"
    "               GITHUB_REPOSITORY='fixture/repo', GH_REPO='fixture/repo', "
    "TAG=tag,\n"
    "               GITHUB_SERVER_URL='https://github.com', "
    "GITHUB_RUN_ID='123', GITHUB_REF_NAME='candidate')\n"
    "    if case.startswith('release:'):\n"
    "        assets = root / 'published'\n"
    "        assets.mkdir()\n"
    "        manifest = dict(format=1, repository='fixture/repo', tag=tag, "
    "source_revision=sha, source_tree=tree, platforms={})\n"
    "        (root / "
    "'published.json').write_text(json.dumps(dict(isDraft=False, "
    "isPrerelease=True, tagName=tag)))\n"
    "        for platform, suffix in [('linux-x64', ''), ('macos-arm64', ''), "
    "('windows-x64', '.exe')]:\n"
    "            name = 'chibi-compiler-host-' + platform + suffix\n"
    "            data = ('seed-' + platform).encode()\n"
    "            digest = hashlib.sha256(data).hexdigest()\n"
    "            (assets / name).write_bytes(data)\n"
    "            manifest['platforms'][platform] = dict(asset=name, "
    "sha256=digest, bytes=len(data))\n"
    "            runner_os, runner_arch = {'linux-x64': ('Linux', 'X64'), "
    "'macos-arm64': ('macOS', 'ARM64'), 'windows-x64': ('Windows', "
    "'X64')}[platform]\n",
    "            evidence = dict(revision=sha, runtime_sha256=digest, "
    "compiler_bytes=7, compiler_sha256='c' * 64, proof_exit_code=0, "
    "runner_os=runner_os, runner_arch=runner_arch)\n"
    "            (assets / ('seed-evidence-' + platform + "
    "'.json')).write_text(json.dumps(evidence))\n"
    "            (assets / ('native-imports-' + platform + "
    "'.txt')).write_text('imports')\n"
    "        (assets / ('chibi-source-' + sha[:12] + "
    "'.tar.gz')).write_bytes(b'source')\n"
    "        (assets / "
    "'chibi-runtime-notices.tar.gz').write_bytes(b'notices')\n"
    "        (assets / 'runtime.json').write_text(json.dumps(manifest))\n"
    "        def sums():\n"
    "            (assets / "
    "'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest("
    ") + '  ' + p.name + '\\n' for p in sorted(assets.iterdir()) if p.name != "
    "'SHA256SUMS'))\n"
    "        sums()\n"
    "        shutil.copytree(assets, root / 'release')\n"
    "        change = case.split(':', 1)[1]\n"
    "        if change in ('repository', 'tag', 'source_revision', "
    "'source_tree', 'format'):\n"
    "            manifest[change] = 'wrong'\n"
    "            (assets / 'runtime.json').write_text(json.dumps(manifest))\n"
    "            sums()\n"
    "        elif change == 'draft':\n"
    "            (root / "
    "'published.json').write_text(json.dumps(dict(isDraft=True, "
    "isPrerelease=True, tagName=tag)))\n"
    "        elif change == 'asset':\n"
    "            (assets / "
    "'chibi-compiler-host-linux-x64').write_bytes(b'corrupt')\n"
    "        elif change == 'missing':\n"
    "            (assets / 'native-imports-linux-x64.txt').unlink()\n"
    "        elif change == 'extra':\n"
    "            (assets / 'unexpected').write_text('extra')\n"
    "            sums()\n"
    "        elif change == 'duplicate':\n"
    "            with (assets / 'SHA256SUMS').open('a') as f:\n"
    "                f.write((assets / "
    "'SHA256SUMS').read_text().splitlines()[0] + '\\n')\n"
    "        elif change in ('evidence', 'closure'):\n",
    "            path = assets / 'seed-evidence-linux-x64.json'\n"
    "            evidence = json.loads(path.read_text())\n"
    "            evidence['runtime_sha256' if change == 'evidence' else "
    "'compiler_sha256'] = 'd' * 64\n"
    "            path.write_text(json.dumps(evidence))\n"
    "            sums()\n"
    "        elif change == 'platform':\n"
    "            path = assets / 'seed-evidence-macos-arm64.json'\n"
    "            evidence = json.loads(path.read_text())\n"
    "            evidence['runner_os'], evidence['runner_arch'] = 'Linux', "
    "'X64'\n"
    "            path.write_text(json.dumps(evidence))\n"
    "            sums()\n"
    "        elif change == 'all-closures':\n"
    "            for path in assets.glob('seed-evidence-*.json'):\n"
    "                evidence = json.loads(path.read_text())\n"
    "                evidence['compiler_sha256'] = 'd' * 64\n"
    "                path.write_text(json.dumps(evidence))\n"
    "            sums()\n"
    "        elif change == 'source':\n"
    "            (assets / ('chibi-source-' + sha[:12] + "
    "'.tar.gz')).write_bytes(b'other-source')\n"
    "            sums()\n"
    "        elif change == 'pin-size':\n"
    "            manifest['platforms']['linux-x64']['bytes'] += 1\n"
    "            (assets / 'runtime.json').write_text(json.dumps(manifest))\n"
    "            sums()\n"
    "        code = block('Verify published assets before "
    "pinning').split(\"<<'PY_VERIFY'\\n\", 1)[1].split('\\nPY_VERIFY', 1)[0]\n"
    "        sys.exit(subprocess.run([sys.executable, '-c', code], "
    "env=env).returncode)\n"
    "    if case.startswith('publish:'):\n"
    "        (root / 'release').mkdir()\n"
    "        (root / 'release/fixture').write_text('asset')\n"
    "        fake = root / 'bin'\n"
    "        fake.mkdir()\n"
    "        env.update(PATH=str(fake) + os.pathsep + os.environ['PATH'], "
    "FIXTURE_ROOT=str(root), FIXTURE_CASE=case, VERSION='22')\n"
    "        (fake / 'gh').write_text('#!' + sys.executable + '\\n' + "
    "\"\"\"import os, sys\n"
    "from pathlib import Path\n",
    "root = Path(os.environ['FIXTURE_ROOT'])\n"
    "a = sys.argv[1:]\n"
    "case = os.environ['FIXTURE_CASE']\n"
    "with (root / 'gh.log').open('a') as f: f.write(' '.join(a) + '\\\\n')\n"
    "if a[:2] == ['release', 'view']:\n"
    " if case == 'publish:api-error' or (case == 'publish:fresh' and not (root "
    "/ 'created').exists()): sys.exit(1)\n"
    " if 'assets' in a: print('extra' if case == 'publish:extra-draft' else "
    "'fixture')\n"
    " elif 'targetCommitish' in a: print('wrong' if case == "
    "'publish:wrong-target' else os.environ['GITHUB_SHA'])\n"
    " else: print('false' if case == 'publish:published' else 'true')\n"
    "elif a[0] == 'api' and '/commits/' in a[1]:\n"
    " print('wrong' if case == 'publish:wrong-target' else "
    "os.environ['GITHUB_SHA'])\n"
    "elif a[0] == 'api' and '/releases/tags/' in a[1]:\n"
    " print('Not Found (HTTP 404)' if case == 'publish:fresh' else 'Forbidden "
    "(HTTP 403)', file=sys.stderr)\n"
    " sys.exit(1)\n"
    "elif a[:2] == ['release', 'create']: (root / "
    "'created').write_text('draft')\n"
    "elif a[:2] in (['release', 'upload'], ['release', 'edit']): pass\n"
    "else: sys.exit(99)\n"
    "\"\"\")\n"
    "        (fake / 'gh').chmod(0o700)\n"
    "        result = subprocess.run(['bash', '-e', '-o', 'pipefail', '-c', "
    "block('Publish the prerelease')], env=env)\n"
    "        print((root / 'gh.log').read_text())\n"
    "        sys.exit(result.returncode)\n"
    "    # Local-only Git source and remote. gh is a recording fake, never "
    "networked.\n"
    "    bare, repo = root / 'remote', root / 'repo'\n"
    "    subprocess.run(['git', 'init', '--bare', '-q', str(bare)], "
    "check=True)\n"
    "    subprocess.run(['git', 'clone', '-q', str(bare), str(repo)], "
    "check=True)\n"
    "    def git(*args):\n"
    "        return subprocess.check_output(['git', '-C', str(repo), *args], "
    "text=True).strip()\n"
    "    git('config', 'user.name', 'Fixture')\n"
    "    git('config', 'user.email', 'fixture@example.invalid')\n"
    "    (repo / 'tools/compiler').mkdir(parents=True)\n",
    "    (repo / 'tools/compiler/runtime.json').write_text('old\\n')\n"
    "    git('add', '.')\n"
    "    git('commit', '-qm', 'source')\n"
    "    git('branch', '-M', 'candidate')\n"
    "    git('push', '-q', 'origin', 'candidate')\n"
    "    sha = git('rev-parse', 'HEAD')\n"
    "    tag = 'compiler-runtime-prepared22-' + sha\n"
    "    env.update(GITHUB_SHA=sha, TAG=tag, GITHUB_ENV=str(root / 'env'))\n"
    "    (root / 'verified-pin').mkdir()\n"
    "    (root / 'verified-pin/runtime.json').write_text('verified pin\\n')\n"
    "    fake = root / 'bin'\n"
    "    fake.mkdir()\n"
    "    env.update(PATH=str(fake) + os.pathsep + os.environ['PATH'], "
    "FIXTURE_ROOT=str(root), FIXTURE_REMOTE=str(bare), FIXTURE_CASE=case)\n"
    "    (fake / 'gh').write_text('#!' + sys.executable + '\\n' + '''import "
    "json, os, subprocess, sys\n"
    "from pathlib import Path\n"
    "root = Path(os.environ['FIXTURE_ROOT'])\n"
    "a = sys.argv[1:]\n"
    "with (root / 'gh.log').open('a') as f: f.write(json.dumps(a) + '\\\\n')\n"
    "if a[0] == 'api' and '/git/ref/heads/' in a[1]:\n"
    " print(subprocess.check_output(['git', '--git-dir=' + "
    "os.environ['FIXTURE_REMOTE'], 'rev-parse', 'refs/heads/candidate'], "
    "text=True).strip())\n"
    "elif a[0] == 'api' and '/runs?' in a[1]:\n"
    " if 'ci.yml/' in a[1] and os.environ['FIXTURE_CASE'] == 'pin:resume': "
    "print('10')\n"
    "elif a[:3] == ['api', '-X', 'POST']:\n"
    " print('11')\n"
    "elif a[0] == 'api' and '/actions/runs/' in a[1]:\n"
    " print(os.environ['PINNED'])\n"
    "else: sys.exit(99)\n"
    "''')\n"
    "    (fake / 'gh').chmod(0o700)\n"
    "    pin_command = block('Commit the pin onto the seeded commit')\n"
    "    first = subprocess.run(['bash', '-e', '-o', 'pipefail', '-c', "
    "pin_command], cwd=repo, env=env)\n"
    "    if first.returncode: sys.exit(first.returncode)\n"
    "    pinned = git('rev-parse', 'HEAD')\n"
    "    # A restarted job checks out original GITHUB_SHA, as Actions does.\n"
    "    if case == 'pin:unrelated':\n"
    "        (repo / 'other').write_text('change')\n",
    "        git('add', '.')\n"
    "        git('commit', '-qm', 'unrelated')\n"
    "        git('push', '-q', 'origin', 'candidate')\n"
    "    elif case == 'pin:wrong-bytes':\n"
    "        (repo / 'tools/compiler/runtime.json').write_text('different "
    "pin')\n"
    "        git('add', '.')\n"
    "        git('commit', '--amend', '--no-edit', '-q')\n"
    "        git('push', '-q', '--force', 'origin', 'candidate')\n"
    "    elif case == 'pin:mode':\n"
    "        (repo / 'tools/compiler/runtime.json').chmod(0o755)\n"
    "        git('add', '.')\n"
    "        git('commit', '--amend', '--no-edit', '-q')\n"
    "        git('push', '-q', '--force', 'origin', 'candidate')\n"
    "    elif case == 'pin:other-run':\n"
    "        env['GITHUB_RUN_ID'] = '456'\n"
    "    git('checkout', '-q', '--detach', sha)\n"
    "    (root / 'env').write_text('')\n"
    "    result = subprocess.run(['bash', '-e', '-o', 'pipefail', '-c', "
    "pin_command], cwd=repo, env=env)\n"
    "    if result.returncode: sys.exit(result.returncode)\n"
    "    env.update(dict(line.split('=', 1) for line in (root / "
    "'env').read_text().splitlines()))\n"
    "    result = subprocess.run(['bash', '-e', '-o', 'pipefail', '-c', "
    "block('Start CI and the compiler self-build on the pin')], cwd=repo, "
    "env=env)\n"
    "    print('pinned=' + str(env.get('PINNED') == pinned))\n"
    "    calls = [json.loads(line) for line in (root / "
    "'gh.log').read_text().splitlines()]\n"
    "    print('dispatches=' + str(sum(a[:3] == ['api', '-X', 'POST'] for a in "
    "calls)))\n"
    "    sys.exit(result.returncode)\n"
};

static const char *const driver_facade_capture[] = {
    "if sys.argv[2] == 'facade-capture':\n"
    "    import hashlib, shutil\n"
    "    mode = sys.argv[5]\n"
    "    parent = Path(sys.argv[4]) / ('facade-' + mode)\n"
    "    parent.mkdir()\n"
    "    root = parent / 'source tree'\n"
    "    root.mkdir()\n"
    "    output = parent / 'capture'\n"
    "    helper = Path('.github/capture-facade.py').resolve()\n"
    "    sources = ['tools/compiler/bootstrap/bootstrap.json', "
    "'tools/compiler/runtime.json',\n"
    "               'tools/compiler/asset_compiler/CompilerSources.txt',\n"
    "               'tools/graph/asset_compiler/Generate.chibi',\n"
    "               'tools/toolchain/AssetCompiler.chibi', "
    "'tools/graph/engine/Engine.chibi']\n"
    "    for name in sources:\n"
    "        path = root / name\n"
    "        path.parent.mkdir(parents=True, exist_ok=True)\n"
    "        path.write_text('tracked source\\n')\n"
    "    (root / '.gitignore').write_text('build/\\n')\n"
    "    def git(*args):\n"
    "        return subprocess.check_output(['git', *args], cwd=root, "
    "text=True,\n"
    "                                       "
    "stderr=subprocess.DEVNULL).strip()\n"
    "    git('init', '-q')\n"
    "    git('add', '.')\n"
    "    git('-c', 'user.name=Chibi fixture', '-c', "
    "'user.email=fixture@example.invalid',\n"
    "        '-c', 'core.hooksPath=/dev/null', 'commit', '-qm', 'Capture "
    "fixture')\n"
    "    revision = git('rev-parse', 'HEAD')\n"
    "    build = root / 'build'\n"
    "    payload = b'[\"/project\", [], [], "
    "[\"asset_compiler/Compile.chibi\"], [\"source\\\\u0000text\"], false]'\n"
    "    files = {'sdk/generated/assets/input.json': payload,\n"
    "             'sdk/generated/assets/compiler.chibip': "
    "b'\\x00artifact\\xff\\n',\n"
    "             'sdk/generated/assets/native_target.json': "
    "b'[1,2,3,4,5,6]\\n',\n"
    "             'sdk/chibi-compiler-host': b'actual native host\\n',\n"
    "             'sdk/chibi-build-host': b'actual build host\\n',\n"
    "             'compile_commands.json': "
    "b'[{\"arguments\":[\"clang\",\"-O3\",\"-DNDEBUG\"]}]\\n',\n"
    "             'link_commands.json': "
    "b'[{\"arguments\":[\"clang\",\"-o\",\"chibi-compiler-host\"]}]\\n'}\n"
    "    for name, data in files.items():\n"
    "        path = build / name\n"
    "        path.parent.mkdir(parents=True, exist_ok=True)\n"
    "        path.write_bytes(data)\n"
    "    if mode == 'missing':\n"
    "        (build / 'sdk/generated/assets/input.json').unlink()\n"
    "    if mode == 'empty':\n"
    "        (build / 'sdk/generated/assets/input.json').write_bytes(b'')\n"
    "    if mode == 'packaging':\n",
    "        (build / "
    "'sdk/generated/assets/"
    "input.json').write_bytes(b'[[\"library\"],[\"text\"]]')\n"
    "    if mode == 'dirty':\n"
    "        (root / sources[0]).write_text('changed source\\n')\n"
    "    if mode == 'mismatch':\n"
    "        revision = '0' * 40\n"
    "    if mode == 'oversize':\n"
    "        with (build / 'sdk/generated/assets/input.json').open('wb') as "
    "stream:\n"
    "            stream.truncate(64 * 1024 * 1024 + 1)\n"
    "    if mode == 'fifo':\n"
    "        path = build / 'sdk/generated/assets/input.json'\n"
    "        path.unlink()\n"
    "        os.mkfifo(path)\n"
    "    if mode in ('symlink', 'parent-link'):\n"
    "        outside = parent / 'outside'\n"
    "        outside.mkdir()\n"
    "        (outside / "
    "'input.json').write_bytes(b'NEVER_CAPTURE_EXTERNAL_BYTES')\n"
    "        target = build / 'sdk/generated/assets'\n"
    "        if mode == 'symlink':\n"
    "            (target / 'input.json').unlink()\n"
    "            (target / 'input.json').symlink_to(outside / 'input.json')\n"
    "        else:\n"
    "            shutil.rmtree(target)\n"
    "            target.symlink_to(outside, target_is_directory=True)\n"
    "    if mode == 'outside-build':\n"
    "        build = parent / 'outside-build'\n"
    "        build.mkdir()\n"
    "    if mode == 'build-alias':\n"
    "        (root / 'alias').symlink_to(root, target_is_directory=True)\n"
    "        build = root / 'alias/build'\n"
    "    if mode == 'inside-output':\n"
    "        output = root / 'capture'\n"
    "    if mode == 'reuse-output':\n"
    "        output.mkdir()\n"
    "        (output / 'keep').write_bytes(b'preserved')\n"
    "    env = dict(os.environ, GH_TOKEN='NEVER_CAPTURE_AUTH')\n"
    "    result = subprocess.run([sys.executable, str(helper), "
    "'--source-root', str(root),\n"
    "        '--build', str(build), '--output', str(output), "
    "'--expected-revision', revision],\n"
    "        env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, "
    "text=True)\n"
    "    print(result.stdout, end='')\n"
    "    receipt = output / 'capture.json'\n"
    "    if receipt.is_file():\n"
    "        print(receipt.read_text(), end='')\n"
    "    for name in ['sdk/generated/assets/input.json', "
    "'sdk/generated/assets/compiler.chibip',\n"
    "                 'compile_commands.json', 'link_commands.json']:\n"
    "        path = output / 'build' / name\n"
    "        if path.is_file():\n"
    "            print(name + '.sha256=' + "
    "hashlib.sha256(path.read_bytes()).hexdigest())\n"
    "    print('kept-output=' + str((output / 'keep').is_file()))\n"
    "    sys.exit(result.returncode)\n"
};

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

static const char driver_scalar_batch[] =
    "if sys.argv[2] == 'scalar-batch':\n"
    "    import shutil\n"
    "    started = time.monotonic()\n"
    "    def trace(phase):\n"
    "        print(f'scalar-transport phase={phase} elapsed='\n"
    "              f'{time.monotonic() - started:.6f}', file=sys.stderr, "
    "flush=True)\n"
    "    trace('driver-ready')\n"
    "    source = "
    "Path('test/native_aot/scalar/Run.ps1').read_text(encoding='utf-8')\n"
    "    root = Path(sys.argv[4])\n"
    "    pwsh = shutil.which('pwsh')\n"
    "    if not pwsh:\n"
    "        print('PowerShell transport fixture unrun: pwsh unavailable')\n"
    "        sys.exit(0)\n"
    "    helpers = source.split('function Digest', 1)[1].split(\"\\ntry {\\n   "
    " $suffix\", 1)[0]\n"
    "    loop = source.split('    $priorChecked = @{}', 1)[1].split('    if "
    "($priorChecked.Count -ne 4)', 1)[0]\n"
    "    (root / 'loop.ps1').write_text('$priorChecked = @{}\\n' + loop, "
    "encoding='utf-8')\n"
    "    script = root / 'scalar-fixture.ps1'\n"
    "    fixture = "
    "Path('test/native_aot/scalar/"
    "TransportFixture.ps1').read_text(encoding='utf-8')\n"
    "    script.write_text('function Digest' + helpers + '\\n' + fixture, "
    "encoding='utf-8')\n"
    "    trace('pwsh-start')\n"
    "    subprocess.run([pwsh, '-NoProfile', '-File', str(script), str(root)], "
    "check=True)\n"
    "    trace('pwsh-exit')\n"
    "    for i in range(3):\n"
    "        print('member:' + (root / "
    "f'member-{i}.stdout').read_bytes().hex())\n"
    "    members = json.loads((root / "
    "'members.json').read_text(encoding='utf-8-sig'))\n"
    "    original = fixture.split(\"$text = '\", 1)[1].split(\"'\\n\", "
    "1)[0].encode()\n"
    "    # Print evidence-derived byte ranges; C asserts exact member "
    "identity.\n"
    "    for member in members:\n"
    "        print('range:' + "
    "original[member['offset']:member['offset']+member['bytes']].hex())\n"
    "    print('joined:' + (root / 'joined.json').read_bytes().hex())\n"
    "    sys.exit(0)\n";

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

TEST_CASE(scalar_batch_keeps_original_bytes_and_failure_order)
{
    REQUIRE(run("scalar-batch", NULL) == 0);
    char *output = tool_read(logpath);
    REQUIRE(output);
    /* Normalize fixture log line endings only, never the hex-encoded bytes. */
    char *read = output, *write = output;
    while (*read) {
        if (*read != '\r' || read[1] != '\n') {
            *write++ = *read;
        }
        read++;
    }
    *write = 0;

    if (strstr(output, "PowerShell transport fixture unrun")) {
        fprintf(stderr, "%s", output);
        free(output);
        REQUIRE(!getenv("GITHUB_ACTIONS"));
        return true;
    }
    const char *phases[] = { "driver-ready",  "pwsh-start",     "fixture-enter",
                             "split-start",   "split-done",     "join-done",
                             "evidence-done", "singleton-done", "invalid-done",
                             "counters-done", "corpus-start",   "corpus-done",
                             "partial-start", "partial-done",   "pwsh-exit" };
    const char *phase = output;
    for (int i = 0; i < (int)(sizeof phases / sizeof *phases); i++) {
        char expected[96];
        snprintf(expected, sizeof expected, "scalar-transport phase=%s elapsed=", phases[i]);
        phase = strstr(phase, expected);
        REQUIRE(phase);
        phase += strlen(expected);
    }
    REQUIRE(strstr(output, "scalar-transport runtime pwsh="));
    REQUIRE(strstr(output, " dotnet="));
    REQUIRE(strstr(output, " cpus="));
    REQUIRE(
        strstr(output, "digest-empty=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n")
    );
    REQUIRE(
        strstr(output, "digest-binary=07357c2e935267e3b90071a4d9edef497ab0d9e0a8a87a78258a3a173574b10d\n")
    );
    REQUIRE(
        strstr(output, "digest-overwrite=91d6b1c484360131d3d95d4df0d4ad95377f04cdf2de124f6ff9cb7d8bb0d5e7\n")
    );
    REQUIRE(
        strstr(output, "digest-three-byte=4234f1064f32788dc9487507f9eeecd0cdf7df6bd010fd1dad8026226e222813\n")
    );
    REQUIRE(
        strstr(output, "digest-changed=d67e2e944994496c8d8ec76eed0cf9f09679448d584b532bebf941852a37f5ed\n")
    );
    REQUIRE(strstr(output, "digest-missing rejected\ndigest-directory rejected\n"));
    REQUIRE(strstr(output, "singleton=1\nsingleton-identical=True\n"));
    REQUIRE(strstr(output, "full=64\nfull-identical=True\n"));
    REQUIRE(strstr(output, "count-0 rejected\ncount-2 rejected\ncount-65 rejected\n"));
    REQUIRE(
        strstr(output, "empty rejected\noversized rejected\njson rejected\njson rejected\njson rejected\nutf8 rejected\n")
    );
    REQUIRE(
        strstr(output, "Compiler invocation 16-sljit-produce-batch failed: compiler batch item 2: compiler argument count mismatch\n")
    );
    REQUIRE(
        strstr(output, "Authored source/consumer check failed at 1-sljit-bytecode (batch item 1): -5\n")
    );
    REQUIRE(
        strstr(output, "published=0-bytecode-bytecode-raw,0-bytecode-sljit-raw,0-sljit-bytecode-raw,0-sljit-sljit-raw,1-bytecode-bytecode-raw,1-bytecode-sljit-raw\ncrossings=6\nnext-batch=False\n")
    );
    REQUIRE(
        strstr(output, "partial-crossings=260\npartial-raw=260\npartial-last=64-sljit-sljit-raw\npartial-size=1\npartial-order=True\n")
    );
    REQUIRE(!strstr(output, "bad counters accepted"));
    REQUIRE(strstr(output, "bytecode counters accepted\nbytecode counters rejected\n"));
    REQUIRE(strstr(output, "sljit counters accepted\nsljit counters rejected\n"));
    const char *members[] = { ("5b207b2274657874223a22e298835c5c6e5c5c5c225b5d2"
                               "22c2276616c7565223a3138"
                               "3434363734343037333730393535313631357d205d"),
                              "5b747275652c31652d33305d",
                              "5b5d" };
    for (int i = 0; i < 3; i++) {
        char expected[512];
        snprintf(expected, sizeof expected, "member:%s\n", members[i]);
        REQUIRE(strstr(output, expected));
        snprintf(expected, sizeof expected, "range:%s\n", members[i]);
        REQUIRE(strstr(output, expected));
    }
    char joined[1024];
    snprintf(joined, sizeof joined, "joined:5b%s2c%s2c%s5d\n", members[0], members[1], members[2]);
    REQUIRE(strstr(output, joined));
    free(output);
    return true;
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
    /* This case records group membership; parallel scheduler behavior has
     * separate fixtures. Use the real serial CLI route so recorder capacity
     * does not depend on host admission or one supervisor per selector. */
    const char *args[] = { "python3", policy_path, "--jobs", "1",
                           "group",   group,       NULL };
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
    REQUIRE(!strstr(output, "benchmark"));
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
                    "--terrain-playable\n--published-docs\n--chibi-cli\n"
                    "--native-build\n";
    bool exact = output && !strcmp(output, expected);
    free(output);
    REQUIRE(exact);
    REQUIRE(!unlink(commands));
    REQUIRE(run_group("native") == 0);
    output = tool_read(commands);
    bool separate = output && *output && !strstr(output, "benchmark");
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
                                "--chibi-cli\n",
                                "--native-build\n" };
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

/* Native and language dispatch are checked above. Exercise the material
 * group through the same production supervisor rather than a duplicate list. */
TEST_CASE(material_group_does_not_run_benchmarks)
{
    char child[PATH_MAX], commands[PATH_MAX], source[PATH_MAX + 128];
    path(commands, "correctness-commands");
    int length = snprintf(source, sizeof source, "#!/bin/sh\nprintf '%%s\\n' \"$1\" >> '%s'\n", commands);
    REQUIRE(length > 0 && length < (int)sizeof source);
    const char *drivers[] = { "test_tools", "test_language" };
    for (int i = 0; i < 2; i++) {
        path(child, drivers[i]);
        tool_write(child, source);
        REQUIRE(!chmod(child, 0700));
    }
    REQUIRE(run_group("materials") == 0);
    char *output = tool_read(commands);
    REQUIRE(output && *output);
    REQUIRE(!strstr(output, "benchmark"));
    const char *correctness[] = {
        "--material-destruction\n", "--material-fluids\n",
        "--material-mpm\n",         "--material-thermal\n",
        "--material-simulation\n",  "--material-ecs\n",
        "--material-render\n"
    };
    for (int i = 0; i < (int)(sizeof correctness / sizeof *correctness); i++) {
        REQUIRE(strstr(output, correctness[i]));
    }
    free(output);
    REQUIRE(!unlink(commands));
    for (int i = 0; i < 2; i++) {
        path(child, drivers[i]);
        REQUIRE(!unlink(child));
    }
    return true;
}

TEST_CASE(benchmark_routes_remain_explicit)
{
    char child[PATH_MAX], commands[PATH_MAX], source[PATH_MAX + 128];
    path(commands, "benchmark-commands");
    int length = snprintf(source, sizeof source, "#!/bin/sh\nprintf '%%s\\n' \"$1\" >> '%s'\n", commands);
    REQUIRE(length > 0 && length < (int)sizeof source);
    const char *drivers[] = { "test_tools", "test_language" };
    for (int i = 0; i < 2; i++) {
        path(child, drivers[i]);
        tool_write(child, source);
        REQUIRE(!chmod(child, 0700));
    }
    REQUIRE(run_group("benchmarks") == 0);
    char *output = tool_read(commands);
    REQUIRE(output && *output);
    const char *selectors[] = { "--ecs-query-benchmark\n",
                                "--source-reduction-benchmark\n",
                                "--source-execution-benchmark\n",
                                "--native-workload-benchmark\n",
                                "--material-benchmark\n",
                                "--contact-benchmark\n",
                                "--script-benchmark\n",
                                "--scheduler-benchmark\n",
                                "--activation-benchmark\n",
                                "--binding-benchmark\n",
                                "--borrow-benchmark\n",
                                "--incremental-benchmark\n",
                                "--runtime-attribution-benchmark\n",
                                "--query-binding-benchmark\n",
                                "--scalar-read-benchmark\n",
                                "--packed-benchmark\n" };
    for (int i = 0; i < (int)(sizeof selectors / sizeof *selectors); i++) {
        const char *found = strstr(output, selectors[i]);
        REQUIRE(found && !strstr(found + strlen(selectors[i]), selectors[i]));
    }
    free(output);
    REQUIRE(run("wasm-benchmark-routes", NULL) == 0);
    output = tool_read(logpath);
    REQUIRE(
        output && !strcmp(
                      output,
                      "\n"
                      "wasm-activation-benchmark --activation-benchmark\n"
                      "wasm-binding-benchmark --binding-benchmark\n"
                      "wasm-borrow-benchmark --borrow-benchmark\n"
                      "wasm-incremental-benchmark --incremental-benchmark\n"
                      "wasm-packed-benchmark --packed-benchmark\n"
                      "wasm-query-binding-benchmark --query-binding-benchmark\n"
                      "wasm-runtime-attribution-benchmark "
                      "--runtime-attribution-benchmark\n"
                      "wasm-scalar-read-benchmark --scalar-read-benchmark\n"
                      "wasm-script-benchmark --script-benchmark\n"
                  )
    );
    free(output);
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

TEST_CASE(language_shard_producer_selects_verified_full_sanitizer_profile)
{
    REQUIRE(run("shard-ci-profile", NULL) == 0);
    char *output = tool_read(logpath);
    REQUIRE(
        output && !strcmp(
                      output,
                      "True True True True True True True True\n"
                      "native group fixture remains covered\n"
                  )
    );
    free(output);
    return true;
}

TEST_CASE(language_shards_keep_exact_manifest_and_serial_fallback)
{
    REQUIRE(run("shards-manifest", NULL) == 0);
    char *output = tool_read(logpath);
    REQUIRE(output);
    const char *row = output;
    const char *expected = "True True True True True True True True\n";
    for (int i = 0; i < 6; i++) {
        REQUIRE(!strncmp(row, expected, strlen(expected)));
        row += strlen(expected);
    }
    REQUIRE(!strcmp(row, "rejected\nrejected\nrejected\n"));
    free(output);
    return true;
}

TEST_CASE(language_shard_cli_is_serial_and_propagates_failure)
{
    const char *arguments[] = {
        "[\"--timeout\",\"3\",\"--shard\",\"1/3\",\"group\",\"language\"]",
        "[\"--shard\",\"2/3\",\"--jobs\",\"1\",\"group\",\"language\"]",
        "[\"--shard\",\"3/3\",\"group\",\"language\"]",
        "[\"--shard\",\"1/1\",\"group\",\"language\"]",
    };
    const char *expected[] = { "RUN alpha delta 1 3.0\n",
                               "RUN beta 1 None\n",
                               "RUN gamma 1 None\n",
                               "RUN alpha beta gamma delta 1 None\n" };
    for (int i = 0; i < 4; i++) {
        REQUIRE(run("shards-cli", arguments[i]) == 17);
        char *output = tool_read(logpath);
        REQUIRE(output && strstr(output, expected[i]));
        free(output);
    }
    return true;
}

TEST_CASE(language_shard_cli_rejects_invalid_requests_before_work)
{
    const char *arguments[] = {
        "[\"--shard\",\"0/3\",\"group\",\"language\"]",
        "[\"--shard\",\"4/3\",\"group\",\"language\"]",
        "[\"--shard\",\"1/4\",\"group\",\"language\"]",
        "[\"--shard\",\"1/0\",\"group\",\"language\"]",
        "[\"--shard\",\"1\",\"group\",\"language\"]",
        "[\"--shard\",\"a/b\",\"group\",\"language\"]",
        "[\"--shard\",\"1/3\",\"group\",\"native\"]",
        "[\"--shard\",\"1/3\",\"tool\",\"language\"]",
        "[\"--shard\",\"1/3\",\"--jobs\",\"2\",\"group\",\"language\"]",
        "[\"--shard\",\"1/3\",\"shard-manifest\"]",
        "[\"shard-manifest\",\"unexpected\"]",
    };
    for (int i = 0; i < (int)(sizeof arguments / sizeof *arguments); i++) {
        REQUIRE(run("shards-cli", arguments[i]) == 2);
        char *output = tool_read(logpath);
        REQUIRE(output && !strstr(output, "RUN "));
        free(output);
    }
    return true;
}

TEST_CASE(language_artifact_roundtrip_keeps_runtime_files_and_modes)
{
    REQUIRE(run("artifact", "roundtrip") == 0);
    char *output = tool_read(logpath);
    REQUIRE(output && !strcmp(output, "True\nTrue\n"));
    free(output);
    return true;
}

TEST_CASE(language_artifact_rejects_corrupt_and_unsafe_files)
{
    const char *cases[] = { "hash",
                            "mode",
                            "missing",
                            "extra",
                            "extra-directory",
                            "symlink",
                            "special",
                            "corpus",
                            "destination-symlink" };
    for (int i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
        REQUIRE(run("artifact", cases[i]) == 2);
        char *output = tool_read(logpath);
        REQUIRE(output && !strcmp(output, "rejected\nTrue\n"));
        free(output);
    }
    return true;
}

TEST_CASE(language_artifact_requires_exact_source_toolchain_and_manifest)
{
    const char *cases[] = { "source_root",       "commit",  "run",
                            "language_manifest", "machine", "compiler",
                            "runtime",           "dirty" };
    for (int i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
        REQUIRE(run("artifact", cases[i]) == 2);
        char *output = tool_read(logpath);
        REQUIRE(output && !strcmp(output, "rejected\nTrue\n"));
        free(output);
    }
    return true;
}

TEST_CASE(language_artifact_archive_validates_before_writing)
{
    REQUIRE(run("artifact", "archive-valid") == 0);
    char *output = tool_read(logpath);
    REQUIRE(output && !strcmp(output, "unpacked True\n"));
    free(output);
    const char *cases[] = { "archive-duplicate", "archive-extra",
                            "archive-traversal", "archive-absolute",
                            "archive-symlink",   "archive-hardlink",
                            "archive-special",   "archive-directory",
                            "archive-mode",      "archive-corrupt",
                            "archive-identity",  "archive-truncated" };
    for (int i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
        REQUIRE(run("artifact", cases[i]) == 2);
        output = tool_read(logpath);
        REQUIRE(output && !strcmp(output, "rejected True True\n"));
        free(output);
    }
    return true;
}

TEST_CASE(language_artifact_cli_requires_exact_argument_counts)
{
    const char *arguments[] = {
        "[\"unpack\",\"archive.tar\"]",
        "[\"pack\",\"stage\",\"extra\"]",
        "[\"verify\",\"stage\",\"extra\"]",
        "[\"restore\",\"stage\",\"extra\"]",
        "[\"unpack\",\"archive.tar\",\"stage\",\"extra\"]",
    };
    for (int i = 0; i < (int)(sizeof arguments / sizeof *arguments); i++) {
        REQUIRE(run("artifact-cli", arguments[i]) == 2);
        char *output = tool_read(logpath);
        REQUIRE(output && strstr(output, "usage:") && !strstr(output, "verified file inventory"));
        free(output);
    }
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

TEST_CASE(ci_build_timings_use_ready_drivers_and_bounded_selector)
{
    char *workflow = tool_read(CHIBI_SOURCE_ROOT "/.github/workflows/ci.yml");
    REQUIRE(workflow);
    const char
        *step = "      - name: Build timing host regressions (ready drivers)\n"
                "        env:\n"
                "          CHIBI_TOOLS_BUILD_DIR: build/test/tools\n"
                "          CHIBI_BUILD_HOST: "
                "build-release/sdk/chibi-build-host\n"
                "        run: python3 tools/test_policy.py --timeout 180 tool "
                "build-timings\n";
    const char *first = strstr(workflow, step);
    REQUIRE(first);
    const char *second = strstr(first + strlen(step), step);
    REQUIRE(second && !strstr(second + strlen(step), step));
    free(workflow);
    char *policy = tool_read(CHIBI_SOURCE_ROOT "/tools/test_policy.py");
    REQUIRE(policy);
    REQUIRE(strstr(policy, "'build-timings':") && strstr(policy, "'--build-timings'"));
    free(policy);
    return true;
}

TEST_CASE(ci_groups_reuse_ready_drivers_and_keep_coverage)
{
    const char *steps[] = { "native", "materials", "projects", "language" };
    for (int fail = -1; fail < 4; fail++) {
        REQUIRE(run("ci-ready-groups", fail < 0 ? "-" : steps[fail]) == 0);
        char expected[512] = "";
        for (int job = 0; job < 2; job++) {
            strcat(expected, fail < 0 ? "status=0\n" : "status=1\n");
            for (int i = 0; i <= (fail < 0 ? 3 : fail); i++) {
                strcat(expected, steps[i]);
                strcat(expected, "\n");
            }
        }
        char *output = tool_read(logpath);
        bool exact = output && !strcmp(output, expected);
        free(output);
        REQUIRE(exact);
    }
    for (int missing = 0; missing < 5; missing++) {
        char request[32];
        snprintf(request, sizeof request, "missing:%d", missing);
        REQUIRE(run("ci-ready-groups", request) == 0);
        char *output = tool_read(logpath);
        bool exact = output && !strcmp(output, "status=1\nstatus=1\n");
        free(output);
        REQUIRE(exact);
    }
    return true;
}

static bool ci_initial_build_profile(bool instrumented, bool cmake)
{
    const char *targets = "targets=chibi,chibi-compile,chibi-player,chibi_ctl,"
                          "test_tools,test_language";
    const char *profile = instrumented ? "sanitizer" : "debug";
    const char *sanitizer = instrumented ? "ON" : "OFF";
    const char *debug = instrumented ? "-g -O1" : "-g -O1 -fno-omit-frame-pointer";
    char flags[512];
    snprintf(
        flags,
        sizeof flags,
        "flags=-DCMAKE_BUILD_TYPE=Debug,-DCHIBI_ENABLE_SANITIZERS=%s,"
        "-DCHIBI_SANITIZERS=address,undefined,-DCMAKE_C_FLAGS_DEBUG=%s,"
        "-DCMAKE_CXX_FLAGS_DEBUG=%s,-DCMAKE_OBJC_FLAGS_DEBUG=%s\n",
        sanitizer,
        debug,
        debug,
        debug
    );
    const char *failures[] = { "-",           "native-toolchain",
                               "third-party", "graph",
                               "cold:-",      "cold:configure-native",
                               "cold:build",  "cold:third-party",
                               "cold:graph" };
    for (int fail = 0; fail < 9; fail++) {
        char argument[64];
        snprintf(argument, sizeof argument, "%s:%s%s", sanitizer, cmake ? "cmake:" : "", failures[fail]);
        REQUIRE(run("ci-initial-build", argument) == 0);
        bool cold = fail >= 4;
        bool failed = cmake ? fail == 1 || fail == 5 || fail == 6
                            : fail == 2 || fail == 3 || fail >= 5;
        char expected[4096] = "";
        for (int job = 0; job < 2; job++) {
            strcat(expected, "release-first=True\nrelease-targets=chibi_ctl chibi ");
            if (!job) {
                strcat(expected, "chibi-build-host ");
            }
            strcat(expected, "chibi-compiler-host\n");
            strcat(expected, job ? "native-macos " : "native ");
            strcat(expected, failed ? "failed\n" : "continued\n");
            if (cold) {
                strcat(expected, cmake ? "configure-native\ngenerators=build-release/sdk\n" : "configure-native\ngenerators=\n");
                strcat(expected, flags);
                if (fail != 5) {
                    strcat(expected, "build\n");
                    if (cmake) {
                        strcat(expected, targets);
                        strcat(expected, ",chibi-build-host\njobs=2\n");
                    } else {
                        strcat(expected, "targets=chibi-build-host\njobs=2\n");
                    }
                }
            }
            if (cmake && !cold) {
                strcat(expected, "native-toolchain\ngenerators=build-release/sdk\n");
                strcat(expected, targets);
                strcat(expected, "\n");
                strcat(expected, flags);
                strcat(expected, "jobs=2\n");
            } else if (!cmake && fail != 5 && fail != 6) {
                strcat(expected, "third-party\n");
                if (fail != 2 && fail != 7) {
                    strcat(expected, "graph\ngenerators=build-release/sdk\n");
                    strcat(expected, targets);
                    strcat(expected, ",chibi-build-host\nprofile=");
                    strcat(expected, profile);
                    strcat(expected, "\noptimization=1\nframes=");
                    strcat(expected, instrumented ? "off" : "on");
                    strcat(expected, "\njobs=2\n");
                }
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
    REQUIRE(ci_initial_build_profile(false, false));
    REQUIRE(ci_initial_build_profile(true, false));
    return true;
}

TEST_CASE(ci_explicit_cmake_fallback_keeps_recovery_and_failures)
{
    REQUIRE(ci_initial_build_profile(false, true));
    REQUIRE(ci_initial_build_profile(true, true));
    return true;
}

TEST_CASE(ci_generator_hosts_resolve_symlink_root)
{
    const char *profiles[] = { "OFF", "ON" };
    for (int i = 0; i < 2; i++) {
        char argument[32];
        snprintf(argument, sizeof argument, "%s:-", profiles[i]);
        REQUIRE(run("ci-initial-build", argument) == 0);
        char *physical = tool_read(logpath);
        REQUIRE(physical);
        snprintf(argument, sizeof argument, "%s:alias", profiles[i]);
        int status = run("ci-initial-build", argument);
        char *logical = tool_read(logpath);
        bool equal = status == 0 && logical && !strcmp(physical, logical);
        free(physical);
        free(logical);
        REQUIRE(equal);
    }
    return true;
}

TEST_CASE(ci_linux_optimization_preserves_explicit_o0)
{
    const char *profiles[] = { "OFF:o0", "ON:o0", "OFF:cmake:o0", "ON:cmake:o0" };
    for (int i = 0; i < 4; i++) {
        REQUIRE(run("ci-initial-build", profiles[i]) == 0);
        char *output = tool_read(logpath);
        REQUIRE(output);
        bool instrumented = i % 2;
        const char *debug = instrumented ? "-g -O0" : "-g -O0 -fno-omit-frame-pointer";
        char flags[256];
        snprintf(flags, sizeof flags, "-DCMAKE_C_FLAGS_DEBUG=%s,-DCMAKE_CXX_FLAGS_DEBUG=%s,-DCMAKE_OBJC_FLAGS_DEBUG=%s\n", debug, debug, debug);
        bool correct = !strstr(output, "-O1") && !strstr(output, " failed\n");
        if (i >= 2) {
            correct = correct && strstr(output, flags);
        } else {
            correct = correct &&
                      strstr(output, instrumented ? "profile=sanitizer\n" : "profile=debug\n") &&
                      strstr(output, instrumented ? "optimization=0\nframes=off\n" : "optimization=0\nframes=on\n");
        }
        free(output);
        REQUIRE(correct);
    }
    return true;
}

TEST_CASE(ci_main_push_finishes_before_newest_pending_run)
{
    char workflow[PATH_MAX];
    snprintf(workflow, sizeof workflow, "%s/.github/workflows/ci.yml", tool_root());
    char *text = tool_read(workflow);
    REQUIRE(text);
    char *block = strstr(text, "\nconcurrency:\n");
    char *end = block ? strstr(block, "\nenv:\n") : NULL;
    bool scoped = block && end;
    if (end) {
        *end = '\0';
    }
    const char
        *group = "  group: ci-${{ github.workflow }}-${{ github.ref }}-${{ "
                 "github.event_name }}-${{ (github.event_name == 'schedule' || "
                 "inputs.sanitizers == true) && 'sanitizer' || 'fast' }}\n";
    const char
        *cancel = "  cancel-in-progress: ${{ github.event_name != 'push' || "
                  "github.ref != 'refs/heads/main' }}\n";
    bool isolated = scoped && strstr(block, group);
    bool main_finishes = scoped && strstr(block, cancel);
    bool single_pending = scoped && !strstr(block, "queue:");
    free(text);
    REQUIRE(isolated);
    REQUIRE(main_finishes);
    REQUIRE(single_pending);
    return true;
}

TEST_CASE(ci_facade_capture_retains_exact_bytes_and_source_identity)
{
    REQUIRE(run("facade-capture", "clean") == 0);
    char *text = tool_read(logpath);
    REQUIRE(text);
    REQUIRE(strstr(text, "\"source_bound_payloads\": true"));
    REQUIRE(strstr(text, "\"input_role\": \"facade-closure\""));
    REQUIRE(
        strstr(text, "input.json.sha256=7b33b60c9bb94cd26982a12a290baf44fd7bd2ee84904dc8549073b2d4c0b17a")
    );
    REQUIRE(
        strstr(text, "compiler.chibip.sha256=341a68ba9d0e95d90907def00662e751c856cbccd8c6c530ef7493911a2780fb")
    );
    REQUIRE(
        strstr(text, "compile_commands.json.sha256=3b46aae9fba7829e5a1f184a1ff4f78bd66b7cfc3637d008937d2e0e4c68d73a")
    );
    REQUIRE(
        strstr(text, "link_commands.json.sha256=56740dbb425ffd908e0043e1f65305b643cadfe699e0d2f0b1dfec7191a37406")
    );
    REQUIRE(!strstr(text, "NEVER_CAPTURE_AUTH"));
    REQUIRE(strstr(text, "\"child_argv\": {\n    \"status\": \"not captured\""));
    free(text);
    return true;
}

TEST_CASE(ci_facade_capture_distinguishes_missing_empty_and_later_inputs)
{
    REQUIRE(run("facade-capture", "missing") == 0);
    char *text = tool_read(logpath);
    REQUIRE(text && strstr(text, "\"payloads_complete\": false"));
    REQUIRE(strstr(text, "\"input_role\": \"unavailable\""));
    REQUIRE(!strstr(text, "input.json.sha256="));
    free(text);
    REQUIRE(run("facade-capture", "empty") == 0);
    text = tool_read(logpath);
    REQUIRE(text && strstr(text, "\"payloads_complete\": true"));
    REQUIRE(strstr(text, "\"input_role\": \"unparsed\""));
    REQUIRE(
        strstr(text, "input.json.sha256=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
    );
    free(text);
    REQUIRE(run("facade-capture", "packaging") == 0);
    text = tool_read(logpath);
    REQUIRE(text && strstr(text, "may be a later packaging request"));
    REQUIRE(!strstr(text, "\"input_role\": \"facade-closure\""));
    free(text);
    return true;
}

TEST_CASE(ci_facade_capture_does_not_admit_dirty_or_wrong_source)
{
    const char *modes[] = { "dirty", "mismatch" };
    for (int i = 0; i < 2; i++) {
        REQUIRE(run("facade-capture", modes[i]) == 0);
        char *text = tool_read(logpath);
        REQUIRE(text && strstr(text, "\"payloads_complete\": true"));
        REQUIRE(strstr(text, "\"source_bound_payloads\": false"));
        REQUIRE(strstr(text, i ? "\"revision_matches\": false" : "\"clean\": false"));
        free(text);
    }
    return true;
}

TEST_CASE(ci_facade_capture_rejects_unsafe_or_oversized_files)
{
    const char *modes[] = { "symlink", "parent-link", "oversize", "fifo" };
    for (int i = 0; i < 4; i++) {
        REQUIRE(run("facade-capture", modes[i]) == 0);
        char *text = tool_read(logpath);
        REQUIRE(text && strstr(text, "\"payloads_complete\": false"));
        const char *reason = i == 2 ? "capture byte limit exceeded"
                           : i == 3 ? "expected a regular file"
                                    : "symbolic links are not captured";
        REQUIRE(strstr(text, reason));
        REQUIRE(!strstr(text, "input.json.sha256="));
        REQUIRE(!strstr(text, "NEVER_CAPTURE_EXTERNAL_BYTES"));
        REQUIRE(!strstr(text, "NEVER_CAPTURE_AUTH"));
        free(text);
    }
    return true;
}

TEST_CASE(ci_facade_capture_preserves_checkout_and_existing_output)
{
    REQUIRE(run("facade-capture", "outside-build") != 0);
    char *text = tool_read(logpath);
    REQUIRE(text && strstr(text, "build directory must be inside the source checkout"));
    free(text);
    REQUIRE(run("facade-capture", "build-alias") != 0);
    text = tool_read(logpath);
    REQUIRE(text && strstr(text, "build directory must not contain symbolic links"));
    free(text);
    REQUIRE(run("facade-capture", "inside-output") != 0);
    text = tool_read(logpath);
    REQUIRE(text && strstr(text, "capture output must be outside the source checkout"));
    free(text);
    REQUIRE(run("facade-capture", "reuse-output") != 0);
    text = tool_read(logpath);
    REQUIRE(text && strstr(text, "kept-output=True"));
    free(text);
    return true;
}

TEST_CASE(ci_facade_capture_is_diagnostic_and_keeps_the_build_result)
{
    char *text = tool_read(CHIBI_SOURCE_ROOT "/.github/workflows/ci.yml");
    REQUIRE(text);
    const char *job = strstr(text, "\n  native-macos:\n");
    const char *release = job ? strstr(job, "      - name: Build an unsanitized")
                              : NULL;
    const char *capture = release
                            ? strstr(release, "      - name: Capture macOS facade identities")
                            : NULL;
    const char *upload = capture
                           ? strstr(capture, "      - name: Preserve macOS facade identities")
                           : NULL;
    const char *build = upload ? strstr(upload, "      - name: Build products and test drivers")
                               : NULL;
    REQUIRE(job && release && capture && upload && build);
    const char *identifier = strstr(release, "        id: release-build\n");
    REQUIRE(identifier && identifier < capture);
    const char
        *condition = "        if: always() && (steps.release-build.outcome == "
                     "'success' || steps.release-build.outcome == 'failure')\n";
    const char *fields[] = { condition,
                             "        continue-on-error: true\n",
                             ".github/capture-facade.py",
                             "--expected-revision \"$GITHUB_SHA\"" };
    for (int i = 0; i < (int)(sizeof fields / sizeof *fields); i++) {
        const char *field = strstr(capture, fields[i]);
        REQUIRE(field && field < upload);
    }
    const char *continued = strstr(upload, "        continue-on-error: true\n");
    const char *guard = strstr(upload, condition);
    REQUIRE(continued && continued < build && guard && guard < build);
    free(text);
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

/* Inner pools own their CPU budget. Outer admission must remain exclusive,
 * without removing the selector or replacing its real command. */
TEST_CASE(parallel_inner_pools_remain_exclusive_and_covered)
{
    const struct
    {
        const char *name, *expected;
    } cases[] = {
        { "language", "0 1\n" },
        { "language-format", "0 1\n" },
        /* A focused alias: the format selector already runs its three cases. */
        { "language-root-state", "0 0\n" },
        { "language-test-parallel", "0 1\n" },
        { "language-package-tests", "0 1\n" },
        { "language-compiler-fixture", "0 1\n" },
        { "language-package-compiler-chibi-scalar",
          "0 1\n2 test_language --package-compiler-chibi-scalar\n" }
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        REQUIRE(run("inner-pool-route", cases[i].name) == 0);
        char *output = tool_read(logpath);
        bool exclusive = output && !strcmp(output, cases[i].expected);
        free(output);
        REQUIRE(exclusive);
    }
    return true;
}

TEST_CASE(parallel_integer_selectors_overlap_with_audited_asset_work)
{
    const char *names[] = { "language-integer-semantics",
                            "language-integer-shift-counts",
                            "language-integer-shift-counts-authored" };
    for (int i = 0; i < 3; i++) {
        reset_parallel();
        REQUIRE(run("integer-parallel", names[i]) == 0);
        REQUIRE(exists("parallel/first-overlapped") && exists("parallel/second-overlapped"));
        char *output = tool_read(logpath);
        REQUIRE(output && strstr(output, names[i]));
        REQUIRE(strstr(output, "frozenset() 1\n"));
        REQUIRE(strstr(output, "migration-exclusive True\n"));
        free(output);
    }
    reset_parallel();
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
    char expected[64];
    snprintf(expected, sizeof expected, "leaf-launches %d\n", count);
    REQUIRE(strstr(parallel, expected));
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

static void experiment_evidence(bool complete, bool exported, bool database, int level, bool sanitizers)
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
    char build_log[64];
    snprintf(build_log, sizeof build_log, "o%d-build.log", level);
    path(name, build_log);
    FILE *log = fopen(name, "w");
    assert(db && log);
    fprintf(db, "[");
    for (int i = 0; i < 5; i++) {
        char flags[160];
        snprintf(
            flags,
            sizeof flags,
            "-g -O%d %s -fno-omit-frame-pointer -UNDEBUG",
            level,
            sanitizers ? "-fsanitize=address,undefined" : ""
        );
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
    fprintf(
        log,
        "/usr/bin/clang-18 -g -O%d %s -fno-omit-frame-pointer -o test_tools "
        "a.o\n",
        level,
        sanitizers ? "-fsanitize=address,undefined" : ""
    );
    assert(!fclose(db) && !fclose(log));
    path(name, "build/test/tools/testing/include/export_compiler.h");
    FILE *header = fopen(name, "w");
    assert(header);
    fprintf(
        header,
        "\"/usr/bin/"
        "clang-18\",\n\"-g\",\n\"-O%d\",\n%s\"-fno-omit-frame-pointer\",\n%s",
        level,
        sanitizers ? "\"-fsanitize=address,undefined\",\n" : "",
        exported ? "\"-UNDEBUG\",\n" : ""
    );
    assert(!fclose(header));
    path(name, "build/CMakeCache.txt");
    tool_write(name, "CHIBI_THIRD_PARTY_DIR:INTERNAL=/same/archive\n");
}

TEST_CASE(debug_experiment_parallel_build_output)
{
#ifdef __linux__
    char filename[PATH_MAX];
    path(filename, "output.py");
    tool_write(
        filename,
        "import os, pathlib, sys, time\n"
        "name = sys.argv[1]\n"
        "os.write(1, (name * 6000).encode())\n"
        "pathlib.Path(name + '-ready').touch()\n"
        "deadline = time.monotonic() + 3\n"
        "while not all(pathlib.Path(n + '-ready').exists() for n in ('a', "
        "'b')):\n"
        "    if time.monotonic() >= deadline: sys.exit(2)\n"
        "    time.sleep(.001)\n"
        "os.write(1, (name * 6000 + '\\n').encode())\n"
    );
    path(filename, "output.mk");
    tool_write(filename, ".PHONY: all a b\nall: a b\na b:\n\t@python3 output.py $@\n");
    REQUIRE(run("experiment-build-output", "unsynchronized") == 0);
    char *log = tool_read(logpath);
    bool interleaved = strstr(log, "status 0\nintact 0\n");
    free(log);
    REQUIRE(interleaved);
    REQUIRE(run("experiment-build-output", "synchronized") == 0);
    log = tool_read(logpath);
    bool correct = strstr(log, "\"--parallel\", \"4\"") &&
                   strstr(log, "\"--\", \"--output-sync=target\"") &&
                   strstr(log, "status 0\nintact 2\n");
    free(log);
    REQUIRE(correct);
#endif
    return true;
}

TEST_CASE(debug_experiment_manual_workflow)
{
    char name[PATH_MAX];
    snprintf(name, sizeof name, "%s/.github/workflows/debug-optimization-experiment.yml", tool_root());
    char *workflow = tool_read(name);
    REQUIRE(workflow);
    REQUIRE(strstr(workflow, "workflow_dispatch:"));
    REQUIRE(!strstr(workflow, "pull_request:") && !strstr(workflow, "push:"));
    REQUIRE(strstr(workflow, "type: choice"));
    REQUIRE(strstr(workflow, "default: optimization"));
    REQUIRE(
        strstr(workflow, "fromJSON(inputs.comparison == 'instrumentation' && '[\"on\"]' || '[\"on\",\"off\"]')")
    );
    REQUIRE(strstr(workflow, "fail-fast: false"));
    REQUIRE(strstr(workflow, "timeout-minutes: 45"));
    REQUIRE(strstr(workflow, "ref: ${{ inputs.sha }}"));
    REQUIRE(strstr(workflow, "--sanitizers \"$EXPERIMENT_SANITIZERS\""));
    REQUIRE(strstr(workflow, "name: debug-${{ inputs.comparison }}-${{ matrix.sanitizers }}-"));
    REQUIRE(strstr(workflow, "if: always()"));
    REQUIRE(!strstr(workflow, "actions/cache"));
    free(workflow);
    return true;
}

TEST_CASE(debug_experiment_compilation_evidence)
{
    for (int level = 1; level <= 2; level++) {
        for (int sanitizers = 0; sanitizers <= 1; sanitizers++) {
            char options[100];
            snprintf(
                options,
                sizeof options,
                "{\"level\":%d,\"sanitizers\":%s}",
                level,
                sanitizers ? "true" : "false"
            );
            experiment_evidence(true, true, true, level, sanitizers);
            REQUIRE(run("experiment-verify", options) == 0);
            experiment_evidence(false, true, true, level, sanitizers);
            REQUIRE(run("experiment-verify", options) != 0);
            experiment_evidence(true, false, true, level, sanitizers);
            REQUIRE(run("experiment-verify", options) != 0);
            experiment_evidence(true, true, false, level, sanitizers);
            REQUIRE(run("experiment-verify", options) != 0);
            experiment_evidence(true, true, true, level, !sanitizers);
            REQUIRE(run("experiment-verify", options) != 0);
            REQUIRE(run("experiment-configure", options) == 0);
            char *output = tool_read(logpath);
            REQUIRE(output);
            REQUIRE(
                strstr(output, sanitizers ? "-DCHIBI_ENABLE_SANITIZERS=ON" : "-DCHIBI_ENABLE_SANITIZERS=OFF")
            );
            char debug[100];
            snprintf(debug, sizeof debug, "-DCMAKE_C_FLAGS_DEBUG=-g -O%d -fno-omit-frame-pointer", level);
            REQUIRE(strstr(output, debug));
            REQUIRE(strstr(output, "-DCMAKE_BUILD_TYPE=Debug"));
            REQUIRE(strstr(output, "-DCHIBI_ENABLE_CCACHE=OFF"));
            free(output);
        }
    }
    return true;
}

TEST_CASE(debug_experiment_instrumentation_plan)
{
    REQUIRE(
        run("experiment-plan",
            "{\"comparison\":\"instrumentation\",\"sanitizers\":true}") == 0
    );
    char *output = tool_read(logpath);
    REQUIRE(output && strstr(output, "[[\"o1-on\", 1, true], [\"o1-off\", 1, false]]"));
    free(output);
    REQUIRE(run("experiment-plan", "{\"comparison\":\"optimization\",\"sanitizers\":false}") == 0);
    output = tool_read(logpath);
    REQUIRE(output && strstr(output, "[[\"o1\", 1, false], [\"o2\", 2, false]]"));
    free(output);
    const char *mutations[] = { "extra-ndebug", "link-sanitizer", "missing-links" };
    for (int i = 0; i < 3; i++) {
        experiment_evidence(true, true, true, 1, true);
        REQUIRE(run("experiment-mutate", mutations[i]) == 0);
        REQUIRE(run("experiment-verify", "{\"level\":1,\"sanitizers\":true}") != 0);
    }
    experiment_evidence(true, true, true, 1, true);
    char source[PATH_MAX], target[PATH_MAX];
    path(source, "o1-build.log");
    path(target, "o1-on-build.log");
    REQUIRE(rename(source, target) == 0);
    REQUIRE(
        run("experiment-verify",
            "{\"level\":1,\"sanitizers\":true,\"label\":\"o1-on\"}") == 0
    );
    REQUIRE(exists("o1-on-verified-argv.json"));
    REQUIRE(exists("o1-on-CMakeCache.txt"));
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

TEST_CASE(web_publish_uses_private_https_dependencies)
{
    char filename[PATH_MAX];
    snprintf(filename, sizeof filename, "%s/.github/workflows/publish.yml", tool_root());
    char *workflow = tool_read(filename);
    REQUIRE(workflow);
    REQUIRE(strstr(workflow, "      - '.github/install-ubuntu-dependencies.py'\n"));
    REQUIRE(strstr(
        workflow,
        "      - name: Install build dependencies\n"
        "        if: steps.release.outputs.complete != 'true'\n"
        "        run: |\n"
        "          python3 .github/install-ubuntu-dependencies.py "
        "--no-install-recommends \\\n"
        "            ninja-build ccache clang-18 clang-tools-18 "
        "libclang-18-dev libclang-rt-18-dev \\\n"
        "            libgl1-mesa-dev libx11-dev libxcursor-dev libxext-dev "
        "libxi-dev libxrandr-dev \\\n"
        "            libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev "
        "libasound2-dev libpulse-dev\n"
        "          pip install 'cmake==4.2.3'\n"
    ));
    REQUIRE(!strstr(workflow, "apt-get"));
    free(workflow);
    return true;
}

TEST_CASE(runtime_seed_publication_serializes_and_preserves_published_assets)
{
    char filename[PATH_MAX];
    snprintf(filename, sizeof filename, "%s/.github/workflows/compiler-runtime-seed.yml", tool_root());
    char *workflow = tool_read(filename);
    REQUIRE(workflow);
    REQUIRE(
        strstr(workflow, "group: compiler-runtime-seed-${{ github.sha }}\n  cancel-in-progress: false")
    );
    REQUIRE(!strstr(workflow, "compiler-runtime-seed-${{ github.ref }}"));
    const char *verify = strstr(workflow, "- name: Verify published assets before pinning");
    const char *transfer = strstr(workflow, "path: ${{ runner.temp }}/published/runtime.json");
    const char *pin = strstr(workflow, "- name: Commit the pin onto the seeded commit");
    REQUIRE(verify && transfer && pin && verify < transfer && transfer < pin);
    REQUIRE(strstr(workflow, "name: verified-runtime-pin-${{ github.run_id }}\n"));
    REQUIRE(!strstr(workflow, "gh release download \"$TAG\" --pattern runtime.json"));
    free(workflow);
    REQUIRE(run("seed-workflow", "publish:published") == 0);
    char *output = tool_read(logpath);
    REQUIRE(output && !strstr(output, "release upload") && !strstr(output, "release edit"));
    free(output);
    REQUIRE(run("seed-workflow", "publish:draft") == 0);
    output = tool_read(logpath);
    REQUIRE(output && strstr(output, "release upload") && strstr(output, "release edit"));
    free(output);
    REQUIRE(run("seed-workflow", "publish:wrong-target") != 0);
    REQUIRE(run("seed-workflow", "publish:api-error") != 0);
    output = tool_read(logpath);
    REQUIRE(
        output && !strstr(output, "release create") &&
        !strstr(output, "release upload") && !strstr(output, "release edit")
    );
    free(output);
    REQUIRE(run("seed-workflow", "publish:fresh") == 0);
    output = tool_read(logpath);
    REQUIRE(output);
    const char *create = strstr(output, "release create");
    const char *upload = strstr(output, "release upload");
    const char *edit = strstr(output, "release edit");
    REQUIRE(create && upload && edit && create < upload && upload < edit);
    free(output);
    REQUIRE(run("seed-workflow", "publish:extra-draft") != 0);
    return true;
}

TEST_CASE(runtime_seed_reuse_checks_published_provenance)
{
    REQUIRE(run("seed-workflow", "release:valid") == 0);
    const char *invalid[] = { "repository",  "tag",      "source_revision",
                              "source_tree", "format",   "draft",
                              "asset",       "missing",  "extra",
                              "duplicate",   "evidence", "closure",
                              "source",      "pin-size", "all-closures",
                              "platform" };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        char name[64];
        snprintf(name, sizeof name, "release:%s", invalid[i]);
        REQUIRE(run("seed-workflow", name) != 0);
    }
    return true;
}

TEST_CASE(runtime_seed_pin_resumes_only_its_own_commit)
{
    REQUIRE(run("seed-workflow", "pin:resume") == 0);
    char *output = tool_read(logpath);
    REQUIRE(output && strstr(output, "pinned=True") && strstr(output, "dispatches=1"));
    free(output);
    REQUIRE(run("seed-workflow", "pin:unrelated") != 0);
    REQUIRE(run("seed-workflow", "pin:wrong-bytes") != 0);
    REQUIRE(run("seed-workflow", "pin:other-run") != 0);
    REQUIRE(run("seed-workflow", "pin:mode") != 0);
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
    assert(fputs(driver_scalar_batch, script) >= 0);
    assert(fputs(driver_setup, script) >= 0);
    assert(fputs(driver_routes, script) >= 0);
    assert(fputs(driver_shards, script) >= 0);
    assert(fputs(driver_shard_profile, script) >= 0);
    for (size_t i = 0; i < sizeof driver_artifact / sizeof *driver_artifact; i++) {
        assert(fputs(driver_artifact[i], script) >= 0);
    }
    assert(fputs(driver_integer_parallel, script) >= 0);
    assert(fputs(driver_ci, script) >= 0);
    for (size_t i = 0; i < sizeof driver_facade_capture / sizeof *driver_facade_capture;
         i++) {
        assert(fputs(driver_facade_capture[i], script) >= 0);
    }
    assert(fputs(driver_ci_ready_setup, script) >= 0);
    assert(fputs(driver_ci_ready_run, script) >= 0);
    assert(fputs(driver_ci_build, script) >= 0);
    assert(fputs(driver_ci_build_workflow, script) >= 0);
    assert(fputs(driver_ci_build_run, script) >= 0);
    assert(fputs(driver_experiment_clock, script) >= 0);
    assert(fputs(driver_experiment, script) >= 0);
    for (size_t i = 0; i < sizeof driver_parallel / sizeof *driver_parallel; i++) {
        assert(fputs(driver_parallel[i], script) >= 0);
    }
    for (size_t i = 0; i < sizeof driver_seed / sizeof *driver_seed; i++) {
        assert(fputs(driver_seed[i], script) >= 0);
    }
    assert(fputs(driver_source, script) >= 0);
    assert(fclose(script) == 0);
}

void test_tool_test_policy()
{
    set_up();
    RUN_TEST_CASE(web_publish_uses_private_https_dependencies);
    RUN_TEST_CASE(scalar_batch_keeps_original_bytes_and_failure_order);
    RUN_TEST_CASE(runtime_seed_publication_serializes_and_preserves_published_assets);
    RUN_TEST_CASE(runtime_seed_reuse_checks_published_provenance);
    RUN_TEST_CASE(runtime_seed_pin_resumes_only_its_own_commit);
    RUN_TEST_CASE(debug_experiment_flags);
    RUN_TEST_CASE(debug_experiment_manual_workflow);
    RUN_TEST_CASE(debug_experiment_instrumentation_plan);
    RUN_TEST_CASE(debug_experiment_parallel_build_output);
    RUN_TEST_CASE(debug_experiment_compilation_evidence);
    RUN_TEST_CASE(debug_experiment_cancellation_and_failure_clean_up);
    RUN_TEST_CASE(asset_language_cases_are_in_the_actual_ci_group);
    RUN_TEST_CASE(material_cpu_routes_reach_exact_dispatcher);
    RUN_TEST_CASE(project_group_keeps_all_project_commands_out_of_native);
    RUN_TEST_CASE(material_group_does_not_run_benchmarks);
    RUN_TEST_CASE(benchmark_routes_remain_explicit);
    RUN_TEST_CASE(group_selectors_are_unique_and_controller_users_are_projects);
    RUN_TEST_CASE(language_shard_producer_selects_verified_full_sanitizer_profile);
    RUN_TEST_CASE(language_shards_keep_exact_manifest_and_serial_fallback);
    RUN_TEST_CASE(language_shard_cli_is_serial_and_propagates_failure);
    RUN_TEST_CASE(language_shard_cli_rejects_invalid_requests_before_work);
    RUN_TEST_CASE(language_artifact_roundtrip_keeps_runtime_files_and_modes);
    RUN_TEST_CASE(language_artifact_rejects_corrupt_and_unsafe_files);
    RUN_TEST_CASE(language_artifact_requires_exact_source_toolchain_and_manifest);
    RUN_TEST_CASE(language_artifact_archive_validates_before_writing);
    RUN_TEST_CASE(language_artifact_cli_requires_exact_argument_counts);
    RUN_TEST_CASE(ci_groups_share_one_build_and_stop_on_failure);
    RUN_TEST_CASE(ci_groups_reuse_ready_drivers_and_keep_coverage);
    RUN_TEST_CASE(ci_build_timings_use_ready_drivers_and_bounded_selector);
    RUN_TEST_CASE(ci_initial_build_keeps_targets_profiles_and_failures);
    RUN_TEST_CASE(ci_explicit_cmake_fallback_keeps_recovery_and_failures);
    RUN_TEST_CASE(ci_generator_hosts_resolve_symlink_root);
    RUN_TEST_CASE(ci_linux_optimization_preserves_explicit_o0);
    RUN_TEST_CASE(ci_main_push_finishes_before_newest_pending_run);
    RUN_TEST_CASE(ci_facade_capture_retains_exact_bytes_and_source_identity);
    RUN_TEST_CASE(ci_facade_capture_distinguishes_missing_empty_and_later_inputs);
    RUN_TEST_CASE(ci_facade_capture_does_not_admit_dirty_or_wrong_source);
    RUN_TEST_CASE(ci_facade_capture_rejects_unsafe_or_oversized_files);
    RUN_TEST_CASE(ci_facade_capture_preserves_checkout_and_existing_output);
    RUN_TEST_CASE(ci_facade_capture_is_diagnostic_and_keeps_the_build_result);
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
    RUN_TEST_CASE(parallel_inner_pools_remain_exclusive_and_covered);
    RUN_TEST_CASE(parallel_integer_selectors_overlap_with_audited_asset_work);
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
