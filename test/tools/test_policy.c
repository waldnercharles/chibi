#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
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
static const char driver_source[] =
    "import json, os, signal, subprocess, sys, time\n"
    "sys.path.insert(0, sys.argv[1])\n"
    "import test_policy as policy\n"
    "policy.__file__ = os.path.join(os.path.dirname(__file__), "
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
    "    policy.process_children = unavailable\n"
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
    "    if action == 'exit':\n"
    "        result = policy.run_commands([[worker, 'exit', sys.argv[5]]], 6)\n"
    "    elif action == 'aggregate':\n"
    "        result = policy.run_commands([[worker, 'sleep', '1.4'],\n"
    "            [worker, 'sleep', '1.4'], [worker, 'mark', root]], 2)\n"
    "    elif action == 'expired':\n"
    "        os.environ[policy.DEADLINE] = str(time.monotonic() - 1)\n"
    "        result = policy.run_commands([[worker, 'mark', root]], 6)\n"
    "    elif action == 'no-reader':\n"
    "        result = policy.supervise([[worker, 'mark', root]])\n"
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
    "        deadline = time.monotonic() + 2\n"
    "        open(root + '/deadline', 'w').write(str(deadline))\n"
    "        os.environ[policy.DEADLINE] = str(deadline)\n"
    "        result = policy.run_commands([[sys.executable, __file__, "
    "sys.argv[1],\n"
    "            'inner', worker, root]], 60)\n"
    "    elif action == 'inner':\n"
    "        result = policy.run_commands([[worker, 'descendant', root, '1', "
    "'0']], 60)\n"
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
    "except RuntimeError as error:\n"
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
    const char *names[] = { "started",       "escaped",
                            "sibling",       "last-command",
                            "deadline",      "cleanup-entered",
                            "sibling-ready", "sibling-release" };
    for (int i = 0; i < (int)(sizeof names / sizeof *names); i++) {
        char filename[PATH_MAX];
        path(filename, names[i]);
        unlink(filename);
    }
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
    return true;
}

TEST_CASE(studio_move_resources_require_recorded_generation)
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

TEST_CASE(exit_and_aggregate_deadlines)
{
    REQUIRE(run("exit", "0") == 0);
    REQUIRE(run("exit", "7") == 7);
    REQUIRE(run("aggregate", NULL) == 124);
    REQUIRE(!exists("last-command"));
    REQUIRE(run("expired", NULL) == 124);
    REQUIRE(!exists("last-command"));
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

#ifdef __linux__
TEST_CASE(nested_setsid_inherits_deadline)
{
    reset_markers();
    REQUIRE(run("nested", NULL) == 124);
    REQUIRE(exists("started"));
    char filename[PATH_MAX];
    path(filename, "deadline");
    char *expected = tool_read(filename);
    path(filename, "started");
    char *record = tool_read(filename);
    int pid;
    double inherited;
    bool same_deadline = expected && record &&
                         sscanf(record, "%d %lf", &pid, &inherited) == 2 &&
                         strtod(expected, NULL) == inherited;
    free(expected);
    free(record);
    REQUIRE(same_deadline);
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

void test_tool_test_policy()
{
    strcpy(directory, "/tmp/chibi-policy-XXXXXX");
    assert(mkdtemp(directory));
    path(driver, "driver.py");
    path(logpath, "output.log");
    tool_write(driver, driver_source);
    RUN_TEST_CASE(studio_acceptance_requires_exact_completion);
    RUN_TEST_CASE(studio_move_resources_require_recorded_generation);
    RUN_TEST_CASE(exit_and_aggregate_deadlines);
    RUN_TEST_CASE(descendant_cleanup);
    RUN_TEST_CASE(portable_nested_cleanup_preserves_sibling);
    RUN_TEST_CASE(unavailable_discovery_refuses_before_launch);
#ifdef __linux__
    RUN_TEST_CASE(nested_setsid_inherits_deadline);
    RUN_TEST_CASE(orphan_setsid_cleanup);
    RUN_TEST_CASE(missing_proc_uses_portable_discovery);
    RUN_TEST_CASE(pending_termination_still_cleans_up);
#endif
    tool_remove_tree(directory);
}
