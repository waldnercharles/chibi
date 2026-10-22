#include "support/xalloc.h"
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
    [] = "if sys.argv[2] == 'cpu-route':\n"
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

void test_tool_test_policy()
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
    assert(fputs(driver_source, script) >= 0);
    assert(fclose(script) == 0);
    RUN_TEST_CASE(material_cpu_routes_reach_exact_dispatcher);
    RUN_TEST_CASE(project_group_keeps_all_project_commands_out_of_native);
    RUN_TEST_CASE(group_selectors_are_unique_and_controller_users_are_projects);
    RUN_TEST_CASE(studio_acceptance_requires_exact_completion);
    RUN_TEST_CASE(studio_camera_requires_thirty_clean_frames);
    RUN_TEST_CASE(studio_gizmo_requires_forty_two_clean_frames);
    RUN_TEST_CASE(studio_collider_requires_twenty_six_clean_frames);
    RUN_TEST_CASE(studio_resources_require_recorded_generation);
    RUN_TEST_CASE(studio_acceptance_source_records_match_files);
    RUN_TEST_CASE(exit_sequence_and_optional_timeout);
    RUN_TEST_CASE(descendant_cleanup);
    RUN_TEST_CASE(portable_nested_cleanup_preserves_sibling);
    RUN_TEST_CASE(unavailable_discovery_refuses_before_launch);
#ifdef __linux__
    RUN_TEST_CASE(nested_cancellation_cleans_detached_descendants);
    RUN_TEST_CASE(orphan_setsid_cleanup);
    RUN_TEST_CASE(missing_proc_uses_portable_discovery);
    RUN_TEST_CASE(pending_termination_still_cleans_up);
#endif
    tool_remove_tree(directory);
}
