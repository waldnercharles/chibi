#include "assets/project.h"
#include "support/xalloc.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "third_party/yyjson/yyjson.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_REPLIES 16

static const char *program;
static char root[64], log_path[128], recording_path[256];
static yyjson_doc *replies[MAX_REPLIES];
static int reply_count, request_count;
static FILE *requests;
static yyjson_mut_doc *request;
static yyjson_mut_val *input;

static const char *const paths[] = {
    "project.kdl",
    "content/behaviors/Probe.chibi",
    "content/libraries/Helpers.chibi",
    "content/systems/Drift.chibi",
    "content/prototypes/Actor.kdl",
    "content/prototypes/Driver.kdl",
    "content/scenes/Proof.kdl",
};
static char *sources[sizeof paths / sizeof *paths];
enum
{
    MANIFEST,
    PROBE,
    HELPERS,
    DRIFT
};

static void path(char *out, size_t size, const char *relative)
{
    int n = snprintf(out, size, "%s/%s", root, relative);
    assert(n > 0 && n < (int)size);
}

static void write_file(const char *relative, const char *text)
{
    char file[256];
    path(file, sizeof file, relative);
    tool_write(file, text);
}

static yyjson_val *get(yyjson_val *value, const char *key)
{
    return yyjson_obj_get(value, key);
}

static bool string_is(yyjson_val *value, const char *expected)
{
    const char *text = yyjson_get_str(value);
    return text && !strcmp(text, expected);
}

static yyjson_val *result(int i)
{
    assert(i >= 0 && i < reply_count);
    return get(yyjson_doc_get_root(replies[i]), "result");
}

static bool ok(int i)
{
    return yyjson_get_bool(get(yyjson_doc_get_root(replies[i]), "ok"));
}

static void clear_replies()
{
    for (int i = 0; i < reply_count; i++) {
        yyjson_doc_free(replies[i]);
    }
    reply_count = 0;
}

static void begin(const char *operation, const char *relative)
{
    assert(!request);
    if (!requests) {
        char file[128];
        path(file, sizeof file, "requests.jsonl");
        requests = fopen(file, "wb");
        assert(requests);
        request_count = 0;
    }
    request = yyjson_mut_doc_new(NULL);
    input = yyjson_mut_obj(request);
    yyjson_mut_doc_set_root(request, input);
    yyjson_mut_obj_add_int(request, input, "contract", 1);
    yyjson_mut_obj_add_str(request, input, "operation", operation);
    if (relative) {
        yyjson_mut_obj_add_str(request, input, "path", relative);
    }
}

static void send()
{
    char *json = yyjson_mut_write(request, 0, NULL);
    assert(json && request_count < MAX_REPLIES);
    assert(fprintf(requests, "%s\n", json) > 0);
    free(json);
    yyjson_mut_doc_free(request);
    request = NULL;
    request_count++;
}

static int run()
{
    assert(requests && !request);
    assert(!fclose(requests));
    requests = NULL;
    clear_replies();
    const char *args[] = { "/bin/sh",
                           "-c",
                           "exec \"$0\" --project-root . < requests.jsonl",
                           program,
                           NULL };
    int status = tool_run(args, root, log_path);
    char *output = tool_read(log_path);
    assert(output);
    for (const char *line = output; *line;) {
        size_t length = strcspn(line, "\n");
        if (!strncmp(line, "{\"contract\"", 11)) {
            assert(reply_count < MAX_REPLIES);
            replies[reply_count] = yyjson_read(line, length, 0);
            assert(replies[reply_count++]);
        }
        line += length + (line[length] == '\n');
    }
    if (reply_count != request_count) {
        fputs(output, stderr);
    }
    free(output);
    assert(reply_count == request_count);
    return status;
}

static void setup()
{
    recording_path[0] = 0;
    strcpy(root, "/tmp/chibi-controller-XXXXXX");
    assert(mkdtemp(root));
    char *resolved = realpath(root, NULL);
    assert(resolved && strlen(resolved) < sizeof root);
    strcpy(root, resolved);
    free(resolved);
    path(log_path, sizeof log_path, "output.log");
    const char *directories[] = {
        "content",         "content/behaviors",  "content/libraries",
        "content/systems", "content/prototypes", "content/scenes"
    };
    for (size_t i = 0; i < sizeof directories / sizeof *directories; i++) {
        char file[128];
        path(file, sizeof file, directories[i]);
        assert(!mkdir(file, 0700));
    }
    char manifest[256];
    snprintf(manifest, sizeof manifest, "version %d\nname \"Controller proof\"\ncontent \"content\"\nstart_scene \"scenes/Proof.kdl\"\n", CHIBI_PROJECT_VERSION);
    write_file(paths[MANIFEST], manifest);
    write_file(
        paths[PROBE],
        "behavior Probe {\n"
        "    count := 0\n"
        "    on Tick { count += Helpers.Twice(1) }\n"
        "    on Begin { await delay(0.05); count += 100 }\n"
        "}\n"
    );
    write_file(
        paths[HELPERS],
        "library Helpers {\n"
        "    function Twice(value: int) { return value * 2 }\n"
        "    function Taken(value: int) { return value }\n"
        "}\n"
    );
    write_file(paths[DRIFT], "system Drift(position: Transform) {\n    position.x += dt\n}\n");
    write_file(paths[4], "name \"Actor\"\ncomponents {\n    (object)Transform\n    (object)Input\n    (object)Aim\n}\n");
    write_file(paths[5], "name \"Driver\"\ncomponents {\n    EngineSystem asset=\"Drift\"\n}\n");
    write_file(
        paths[6],
        "version 1\nname \"Proof\"\n(array)entities {\n"
        "    - name=\"Actor\" prototype=\"Actor\" { (array)behaviors "
        "\"Probe\"; }\n"
        "    - name=\"Driver\" prototype=\"Driver\"\n}\n"
    );
    const char *args[] = { program, "--project-root", root, "--format", NULL };
    assert(tool_run(args, root, log_path) == 0);
    for (size_t i = 0; i < sizeof paths / sizeof *paths; i++) {
        char file[256];
        path(file, sizeof file, paths[i]);
        sources[i] = tool_read(file);
        assert(sources[i]);
    }
}

static void teardown()
{
    clear_replies();
    if (recording_path[0]) {
        unlink(recording_path);
    }
    for (size_t i = 0; i < sizeof sources / sizeof *sources; i++) {
        free(sources[i]);
        sources[i] = NULL;
    }
    tool_remove_tree(root);
}

static bool unchanged()
{
    for (size_t i = 0; i < sizeof paths / sizeof *paths; i++) {
        char file[256];
        path(file, sizeof file, paths[i]);
        char *text = tool_read(file);
        bool same = text && !strcmp(text, sources[i]);
        free(text);
        if (!same) {
            return false;
        }
    }
    return true;
}

static int offset(int source, const char *text)
{
    const char *found = strstr(sources[source], text);
    assert(found);
    return (int)(found - sources[source]);
}

/* Verify the byte range and its one-based line/column against the actual file. */
static bool range_is(yyjson_val *value, const char *source, int start, int length)
{
    int line = 1, column = 1;
    for (int i = 0; i < start; i++) {
        if (source[i] == '\n') {
            line++;
            column = 1;
            continue;
        }
        column++;
    }
    return yyjson_is_int(get(value, "start")) &&
           yyjson_get_int(get(value, "start")) == start &&
           yyjson_get_int(get(value, "end")) == start + length &&
           yyjson_get_int(get(value, "line")) == line &&
           yyjson_get_int(get(value, "column")) == column;
}

static void analyze(const char *rename)
{
    begin("analyze_code", paths[PROBE]);
    yyjson_mut_obj_add_str(request, input, "source", sources[PROBE]);
    yyjson_mut_obj_add_int(request, input, "offset", offset(PROBE, "Twice"));
    if (rename) {
        yyjson_mut_obj_add_str(request, input, "rename", rename);
    }
}

TEST_CASE(controller_analysis_and_rename_preview)
{
    analyze("Double");
    send();
    analyze("Taken");
    send();
    REQUIRE(run() == 0);
    REQUIRE(ok(0) && ok(1) && yyjson_get_bool(get(result(0), "valid")));
    REQUIRE(yyjson_arr_size(get(result(0), "diagnostics")) == 0);
    yyjson_val *definition = get(result(0), "definition");
    char library_path[256];
    path(library_path, sizeof library_path, paths[HELPERS]);
    REQUIRE(string_is(get(definition, "path"), library_path));
    REQUIRE(range_is(definition, sources[HELPERS], offset(HELPERS, "Twice"), 5));
    yyjson_val *references = get(result(0), "references");
    REQUIRE(yyjson_arr_size(references) == 2);
    bool referenced[2] = { false, false };
    for (size_t i = 0; i < 2; i++) {
        yyjson_val *reference = yyjson_arr_get(references, i);
        int which = string_is(get(reference, "path"), library_path) ? HELPERS : PROBE;
        char file[256];
        path(file, sizeof file, paths[which]);
        REQUIRE(string_is(get(reference, "path"), file));
        REQUIRE(range_is(reference, sources[which], offset(which, "Twice"), 5));
        referenced[which == HELPERS] = true;
    }
    REQUIRE(referenced[0] && referenced[1]);
    REQUIRE(yyjson_get_str(get(result(0), "hover")));
    REQUIRE(yyjson_arr_size(get(result(0), "completions")) > 0);
    yyjson_val *edits = get(result(0), "rename_edits");
    REQUIRE(yyjson_arr_size(edits) == 2);
    bool edited[2] = { false, false };
    for (size_t i = 0; i < 2; i++) {
        yyjson_val *edit = yyjson_arr_get(edits, i);
        int which = string_is(get(edit, "path"), library_path) ? HELPERS : PROBE;
        char file[256];
        path(file, sizeof file, paths[which]);
        REQUIRE(string_is(get(edit, "path"), file));
        REQUIRE(range_is(edit, sources[which], offset(which, "Twice"), 5));
        REQUIRE(string_is(get(edit, "expected"), "Twice"));
        REQUIRE(string_is(get(edit, "replacement"), "Double"));
        edited[which == HELPERS] = true;
    }
    REQUIRE(edited[0] && edited[1]);
    const char *renamed = yyjson_get_str(get(result(0), "renamed_source"));
    REQUIRE(renamed && strstr(renamed, "Helpers.Double(1)") && !strstr(renamed, "Twice"));
    REQUIRE(yyjson_get_str(get(result(1), "rename_error")));
    REQUIRE(!get(result(1), "rename_edits") && !get(result(1), "renamed_source"));
    REQUIRE(unchanged());

    yyjson_val *origin = yyjson_arr_get(get(result(0), "origins"), 0);
    REQUIRE(
        origin && yyjson_get_uint(get(origin, "identity")) &&
        yyjson_get_uint(get(origin, "semantic"))
    );
    int start = (int)yyjson_get_int(get(origin, "start"));
    int length = (int)yyjson_get_int(get(origin, "end")) - start;
    REQUIRE(start >= 0 && length > 0 && start + length <= (int)strlen(sources[PROBE]));
    REQUIRE(range_is(origin, sources[PROBE], start, length));
    analyze(NULL);
    yyjson_mut_val *location = yyjson_mut_obj_add_obj(request, input, "location");
    const char *keys[] = { "graph_id", "node_id", "pin_id" };
    for (int i = 0; i < 3; i++) {
        yyjson_mut_obj_add_uint(request, location, keys[i], yyjson_get_uint(get(origin, keys[i])));
    }
    send();
    REQUIRE(run() == 0 && ok(0));
    REQUIRE(range_is(get(result(0), "location"), sources[PROBE], start, length));
    return true;
}

TEST_CASE(controller_diagnostics_preserve_source_ranges)
{
    const char *broken = "behavior Probe { on Tick { missing_call() } }\n";
    begin("analyze_code", paths[PROBE]);
    yyjson_mut_obj_add_str(request, input, "source", broken);
    send();
    REQUIRE(run() == 0 && ok(0));
    REQUIRE(!yyjson_get_bool(get(result(0), "valid")));
    yyjson_val *diagnostics = get(result(0), "diagnostics");
    REQUIRE(yyjson_arr_size(diagnostics) > 0);
    yyjson_val *diagnostic = yyjson_arr_get(diagnostics, 0);
    REQUIRE(range_is(diagnostic, broken, (int)(strstr(broken, "missing_call") - broken), 12));
    REQUIRE(yyjson_get_int(get(diagnostic, "severity")) == 1);
    REQUIRE(yyjson_get_str(get(diagnostic, "message")));
    REQUIRE(unchanged());
    write_file(paths[PROBE], broken);
    begin("inspect_graph", paths[PROBE]);
    send();
    begin("execution_status", NULL);
    yyjson_mut_obj_add_str(request, input, "kind", "behavior");
    yyjson_mut_obj_add_str(request, input, "name", "Probe");
    send();
    REQUIRE(run() != 0 && !ok(0) && !ok(1));
    REQUIRE(!yyjson_get_bool(get(result(0), "valid")));
    REQUIRE(yyjson_arr_size(get(result(0), "diagnostics")) > 0);
    REQUIRE(range_is(
        yyjson_arr_get(get(result(0), "diagnostics"), 0),
        broken,
        (int)(strstr(broken, "missing_call") - broken),
        12
    ));
    REQUIRE(string_is(get(get(yyjson_doc_get_root(replies[0]), "error"), "code"), "validation_failed"));
    REQUIRE(string_is(get(get(yyjson_doc_get_root(replies[1]), "error"), "code"), "validation_failed"));
    REQUIRE(!yyjson_get_bool(get(get(result(1), "execution"), "ready")));
    REQUIRE(string_is(get(get(result(1), "execution"), "backend"), "unavailable"));
    REQUIRE(yyjson_arr_size(get(result(1), "diagnostics")) > 0);
    return true;
}

static yyjson_val *named(yyjson_val *array, const char *name)
{
    size_t i, count;
    yyjson_val *item;
    yyjson_arr_foreach(array, i, count, item)
    {
        if (string_is(get(item, "name"), name)) {
            return item;
        }
    }
    return NULL;
}

static bool uint_is(yyjson_val *value, uint64_t expected)
{
    return yyjson_is_uint(value) && yyjson_get_uint(value) == expected;
}

TEST_CASE(controller_inspection_and_execution_status)
{
    begin("inspect_graph", paths[PROBE]);
    yyjson_mut_obj_add_str(request, input, "graph_name", "Tick");
    send();
    begin("inspect_system", paths[DRIFT]);
    send();
    const char *kinds[] = { "behavior", "system" };
    const char *names[] = { "Probe", "Drift" };
    for (int i = 0; i < 2; i++) {
        begin("execution_status", NULL);
        yyjson_mut_obj_add_str(request, input, "kind", kinds[i]);
        yyjson_mut_obj_add_str(request, input, "name", names[i]);
        send();
    }
    for (int i = 1; i < 7; i++) {
        begin("validate_asset", paths[i]);
        send();
    }
    REQUIRE(run() == 0);
    for (int i = 0; i < reply_count; i++) {
        REQUIRE(ok(i) && yyjson_get_bool(get(result(i), "valid")));
    }
    yyjson_val *graph = get(result(0), "graph");
    uint64_t graph_id = yyjson_get_uint(get(graph, "id"));
    REQUIRE(graph_id && string_is(get(graph, "name"), "Tick"));
    REQUIRE(yyjson_arr_size(get(result(0), "graphs")) == 3);
    REQUIRE(!get(result(0), "metadata"));
    yyjson_val *nodes = get(graph, "nodes");
    REQUIRE(yyjson_arr_size(nodes) > 0);
    size_t i, count;
    yyjson_val *node;
    int located = 0;
    bool call_found = false;
    yyjson_arr_foreach(nodes, i, count, node)
    {
        REQUIRE(!get(node, "x") && !get(node, "y"));
        if (string_is(get(node, "type"), "Helpers.Twice")) {
            REQUIRE(range_is(node, sources[PROBE], offset(PROBE, "Helpers.Twice(1)"), 16));
            REQUIRE(string_is(yyjson_arr_get(get(node, "in"), 0), "value: int"));
            REQUIRE(string_is(yyjson_arr_get(get(node, "out"), 0), "Result: int"));
            call_found = true;
        }
        if (get(node, "start")) {
            int start = (int)yyjson_get_int(get(node, "start"));
            int length = (int)yyjson_get_int(get(node, "end")) - start;
            REQUIRE(start >= 0 && length > 0 && start + length <= (int)strlen(sources[PROBE]));
            REQUIRE(range_is(node, sources[PROBE], start, length));
            located++;
        }
    }
    REQUIRE(located > 0 && call_found);
    REQUIRE(yyjson_arr_size(get(result(1), "queries")) > 0);
    REQUIRE(yyjson_arr_size(get(get(result(1), "graph"), "nodes")) > 0);
    for (int n = 0; n < 2; n++) {
        yyjson_val *execution = get(result(n + 2), "execution");
        REQUIRE(yyjson_get_bool(get(execution, "ready")));
        bool jit = yyjson_get_bool(get(execution, "jit_available"));
        REQUIRE(string_is(get(execution, "backend"), jit ? "sljit" : "bytecode"));
        REQUIRE(yyjson_get_bool(get(execution, "jit")) == jit);
        REQUIRE(yyjson_get_bool(get(execution, "typed_bytecode")));
        REQUIRE(uint_is(get(execution, "events"), n ? 1 : 2));
        REQUIRE(uint_is(get(execution, "native_events"), jit ? (n ? 1 : 2) : 0));
    }
    begin("inspect_graph", paths[PROBE]);
    yyjson_mut_obj_add_uint(request, input, "graph_id", graph_id);
    yyjson_mut_obj_add_bool(request, input, "include_layout", true);
    yyjson_mut_obj_add_bool(request, input, "include_pins", false);
    send();
    begin("inspect_graph", paths[PROBE]);
    yyjson_mut_obj_add_str(request, input, "graph_name", "NoSuchGraph");
    send();
    REQUIRE(run() != 0 && ok(0) && !ok(1));
    REQUIRE(uint_is(get(get(result(0), "graph"), "id"), graph_id));
    REQUIRE(yyjson_get_str(get(result(0), "metadata")));
    nodes = get(get(result(0), "graph"), "nodes");
    yyjson_arr_foreach(nodes, i, count, node)
    {
        REQUIRE(yyjson_is_num(get(node, "x")) && yyjson_is_num(get(node, "y")));
        REQUIRE(!get(node, "in") && !get(node, "out"));
    }
    REQUIRE(unchanged());
    return true;
}

static void profile(int warmup, int frames, bool details, const char *devices)
{
    begin("profile_scenario", NULL);
    yyjson_mut_obj_add_str(request, input, "kind", "scene");
    yyjson_mut_obj_add_str(request, input, "name", "Proof");
    yyjson_mut_obj_add_int(request, input, "warmup_frames", warmup);
    yyjson_mut_obj_add_int(request, input, "frames", frames);
    yyjson_mut_obj_add_bool(request, input, "behavior_details", details);
    yyjson_mut_val *samples = yyjson_mut_obj_add_arr(request, input, "inputs");
    yyjson_mut_val *sample = yyjson_mut_arr_add_obj(request, samples);
    yyjson_mut_val *aim = yyjson_mut_obj_add_obj(request, sample, "aim");
    yyjson_mut_obj_add_real(request, aim, "x", 1);
    yyjson_mut_obj_add_real(request, aim, "y", 0);
    yyjson_mut_obj_add_bool(request, aim, "firing", true);
    yyjson_mut_obj_add_bool(request, aim, "point", false);
    if (devices) {
        yyjson_mut_obj_add_arr(request, sample, devices);
    }
    send();
}

/* Every reported sample must identify the registered generation and retained
 * source, and account for actual entries/resumes rather than timer polls. */
static bool profile_rows(yyjson_val *value, int measured)
{
    yyjson_val *modules = get(value, "source_modules");
    REQUIRE(yyjson_arr_size(modules) == 2);
    yyjson_val *probe = named(modules, "Probe"), *drift = named(modules, "Drift");
    REQUIRE(probe && drift);
    REQUIRE(yyjson_get_uint(get(probe, "id")) && yyjson_get_uint(get(drift, "id")));
    REQUIRE(yyjson_get_uint(get(probe, "id")) != yyjson_get_uint(get(drift, "id")));
    REQUIRE(uint_is(get(probe, "instances"), 1) && uint_is(get(drift, "instances"), 1));
    yyjson_val *execution = get(value, "execution");
    bool jit = yyjson_get_bool(get(execution, "jit_available"));
    REQUIRE(string_is(get(execution, "backend"), jit ? "sljit" : "bytecode"));
    REQUIRE(string_is(get(probe, "backend"), jit ? "sljit" : "bytecode"));
    REQUIRE(string_is(get(drift, "backend"), jit ? "sljit" : "bytecode"));
    REQUIRE(uint_is(get(execution, "instances"), 2));
    REQUIRE(uint_is(get(execution, "event_entries"), 3));
    REQUIRE(uint_is(get(execution, "native_event_entries"), jit ? 3 : 0));
    yyjson_val *profile = get(value, "source_profile");
    REQUIRE(
        yyjson_get_bool(get(profile, "available")) &&
        yyjson_get_bool(get(profile, "registered"))
    );
    REQUIRE(uint_is(get(profile, "dropped_samples"), 0));
    REQUIRE(uint_is(get(profile, "row_limit"), 256));
    yyjson_val *rows = get(profile, "rows");
    REQUIRE(yyjson_arr_size(rows) == 3);
    uint64_t entries = 0, resumes = 0;
    uint64_t owners[3] = { 0 }, functions[3] = { 0 };
    size_t i, count;
    yyjson_val *row;
    yyjson_arr_foreach(rows, i, count, row)
    {
        int which = string_is(get(row, "name"), "Probe") ? PROBE : DRIFT;
        yyjson_val *module = which == PROBE ? probe : drift;
        REQUIRE(string_is(get(row, "name"), which == PROBE ? "Probe" : "Drift"));
        REQUIRE(uint_is(get(row, "module_id"), yyjson_get_uint(get(module, "id"))));
        REQUIRE(yyjson_get_uint(get(row, "function_id")) && yyjson_get_uint(get(row, "owner")));
        REQUIRE(yyjson_is_bool(get(row, "internal")) && !yyjson_get_bool(get(row, "internal")));
        REQUIRE(string_is(get(row, "source"), which == PROBE ? "Probe" : "Drift"));
        int start = (int)yyjson_get_int(get(row, "start"));
        int length = (int)yyjson_get_int(get(row, "end")) - start;
        REQUIRE(start >= 0 && length > 0 && start + length <= (int)strlen(sources[which]));
        REQUIRE(range_is(row, sources[which], start, length));
        REQUIRE(yyjson_is_uint(get(row, "entries")) && yyjson_is_uint(get(row, "resumes")));
        int sample = which == DRIFT                     ? 2
                   : start >= offset(PROBE, "on Begin") ? 1
                                                        : 0;
        if (!sample) {
            REQUIRE(start >= offset(PROBE, "on Tick"));
        }
        REQUIRE(!owners[sample]);
        owners[sample] = yyjson_get_uint(get(row, "owner"));
        functions[sample] = yyjson_get_uint(get(row, "function_id"));
        REQUIRE(uint_is(get(row, "entries"), sample == 1 ? 0 : (uint64_t)measured));
        REQUIRE(uint_is(get(row, "resumes"), sample == 1 ? 1 : 0));
        entries += yyjson_get_uint(get(row, "entries"));
        resumes += yyjson_get_uint(get(row, "resumes"));
        REQUIRE(yyjson_is_bool(get(row, "cpu_available")));
        if (yyjson_get_bool(get(row, "cpu_available"))) {
            yyjson_val *cpu = get(row, "cpu_seconds");
            REQUIRE(
                yyjson_is_num(cpu) && isfinite(yyjson_get_num(cpu)) &&
                yyjson_get_num(cpu) >= 0
            );
        }
    }
    /* The system runs for Actor, whose Transform satisfies its query. */
    REQUIRE(owners[0] == owners[1] && owners[0] == owners[2]);
    REQUIRE(functions[0] != functions[1]);
    REQUIRE(entries == (uint64_t)measured * 2);
    REQUIRE(resumes == 1);
    return true;
}

TEST_CASE(controller_profile_accounts_for_measured_execution)
{
    /* Explicit aim must survive empty keyboard/controller snapshots, including
     * later frames that retain those snapshots. All three runs end identically. */
    profile(2, 4, true, NULL);
    profile(2, 4, false, "controls");
    profile(0, 6, true, "controllers");
    REQUIRE(run() == 0 && ok(0) && ok(1) && ok(2));
    bool accounted = profile_rows(result(0), 4);
    if (!accounted) {
        char *json = yyjson_val_write(get(result(0), "source_profile"), 0, NULL);
        fprintf(stderr, "Source profile: %s\n", json);
        free(json);
    }
    REQUIRE(accounted);
    REQUIRE(profile_rows(result(2), 6));
    REQUIRE(uint_is(get(get(result(2), "timing"), "sampled_frames"), 6));
    REQUIRE(uint_is(get(get(result(2), "timing"), "simulation_steps"), 6));
    for (int i = 0; i < 2; i++) {
        yyjson_val *value = result(i), *timing = get(value, "timing");
        REQUIRE(uint_is(get(value, "frames"), 4) && uint_is(get(value, "warmup_frames"), 2));
        REQUIRE(uint_is(get(timing, "sampled_frames"), 4));
        REQUIRE(uint_is(get(timing, "simulation_steps"), 4));
        REQUIRE(uint_is(get(get(value, "allocations"), "sampled_frames"), 4));
        REQUIRE(uint_is(get(value, "authored_entities"), 2));
        yyjson_val *worst = get(timing, "worst_frames");
        REQUIRE(yyjson_arr_size(worst) == 4);
        unsigned seen = 0;
        for (size_t j = 0; j < 4; j++) {
            yyjson_val *frame = yyjson_arr_get(worst, j);
            REQUIRE(
                yyjson_is_uint(get(frame, "frame")) &&
                yyjson_get_uint(get(frame, "frame")) < 4
            );
            seen |= 1u << yyjson_get_uint(get(frame, "frame"));
            REQUIRE(uint_is(get(frame, "simulation_steps"), 1));
            REQUIRE(
                yyjson_is_num(get(frame, "total_ms")) &&
                yyjson_get_num(get(frame, "total_ms")) >= 0
            );
        }
        REQUIRE(seen == 15);
    }
    REQUIRE(yyjson_is_str(get(result(0), "world_hash")));
    REQUIRE(yyjson_equals(get(result(0), "world_hash"), get(result(1), "world_hash")));
    REQUIRE(yyjson_equals(get(result(0), "world_hash"), get(result(2), "world_hash")));
    yyjson_val *disabled = get(result(1), "source_profile");
    REQUIRE(
        yyjson_is_bool(get(disabled, "enabled")) &&
        !yyjson_get_bool(get(disabled, "enabled"))
    );
    REQUIRE(
        !yyjson_get_bool(get(disabled, "available")) &&
        yyjson_get_bool(get(disabled, "registered"))
    );
    REQUIRE(yyjson_is_arr(get(disabled, "rows")) && !yyjson_arr_size(get(disabled, "rows")));
    REQUIRE(uint_is(get(disabled, "dropped_samples"), 0));
    REQUIRE(unchanged());
    return true;
}

TEST_CASE(controller_profile_excludes_warmup_only_run)
{
    profile(2, 0, true, NULL);
    REQUIRE(run() == 0 && ok(0));
    REQUIRE(uint_is(get(get(result(0), "timing"), "sampled_frames"), 0));
    yyjson_val *samples = get(result(0), "source_profile");
    REQUIRE(
        yyjson_get_bool(get(samples, "enabled")) &&
        yyjson_get_bool(get(samples, "registered"))
    );
    REQUIRE(yyjson_is_arr(get(samples, "rows")) && yyjson_arr_size(get(samples, "rows")) == 0);
    REQUIRE(uint_is(get(samples, "dropped_samples"), 0));
    return true;
}

/* A complete decoded 1x1 PNG, also used by the manifest icon validation case.
 * Keeping it unreferenced proves inventory identity covers raw files outside
 * the prepared runtime resource closure. */
static void unused_image()
{
    static const unsigned char png[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00,
        0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0x64, 0x60, 0xF8, 0x5F,
        0x0F, 0x00, 0x02, 0x87, 0x01, 0x80, 0xEB, 0x47, 0xBA, 0x92, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
    };
    char file[256];
    path(file, sizeof file, "content/unused.png");
    FILE *stream = fopen(file, "wb");
    assert(stream && fwrite(png, 1, sizeof png, stream) == sizeof png);
    assert(!fclose(stream));
}

TEST_CASE(controller_material_creation_discovery_and_editable_inspection)
{
    begin("describe_asset_schema", NULL);
    yyjson_mut_obj_add_str(request, input, "kind", "material");
    send();
    begin("create_material", NULL);
    yyjson_mut_obj_add_str(request, input, "name", "MetalProof");
    yyjson_mut_obj_add_str(request, input, "preset", "metal");
    send();
    begin("inspect_graph", "content/materials/MetalProof.kdl");
    yyjson_mut_obj_add_bool(request, input, "cpu_sample", true);
    send();
    begin("read_asset", "content/materials/MetalProof.kdl");
    send();
    REQUIRE(run() == 0 && ok(0) && ok(1) && ok(2) && ok(3));
    yyjson_val *schemas = get(result(0), "schemas");
    REQUIRE(yyjson_arr_size(schemas) == 1);
    yyjson_val *schema = yyjson_arr_get(schemas, 0);
    REQUIRE(string_is(get(schema, "kind"), "material"));
    REQUIRE(yyjson_get_str(get(schema, "example")));
    REQUIRE(!yyjson_get_bool(get(schema, "complete")));
    REQUIRE(yyjson_get_bool(get(result(1), "editable")));
    REQUIRE(string_is(get(result(1), "path"), "content/materials/MetalProof.kdl"));
    yyjson_val *report = get(result(2), "shader_report");
    REQUIRE(yyjson_get_bool(get(report, "valid")));
    REQUIRE(!yyjson_get_bool(get(report, "gpu_executed")));
    yyjson_val *sample = get(report, "cpu_sample");
    REQUIRE(yyjson_get_bool(get(sample, "valid")));
    yyjson_val *outputs = get(sample, "outputs");
    REQUIRE(fabs(yyjson_get_num(yyjson_arr_get(get(outputs, "roughness"), 0)) - .25) < 1e-6);
    REQUIRE(fabs(yyjson_get_num(yyjson_arr_get(get(outputs, "metallic"), 0)) - 1) < 1e-6);
    const char *source = yyjson_get_str(get(result(3), "source"));
    REQUIRE(source && strstr(source, "graphs") && strstr(source, "MetalProof"));
    size_t size = strlen(source) + 64;
    char *edited = xmalloc(size);
    snprintf(edited, size, "// Hand-authored material note.\n%s", source);
    write_file("content/materials/MetalProof.kdl", edited);
    free(edited);
    begin("validate_asset", "content/materials/MetalProof.kdl");
    send();
    begin("read_asset", "content/materials/MetalProof.kdl");
    send();
    REQUIRE(run() == 0 && ok(0) && ok(1));
    REQUIRE(yyjson_get_bool(get(result(0), "valid")));
    source = yyjson_get_str(get(result(1), "source"));
    REQUIRE(source && strstr(source, "// Hand-authored material note.") == source);
    REQUIRE(unchanged());
    return true;
}

TEST_CASE(controller_structural_diagnostics_avoid_duplicate_loader_fallout)
{
    const char *broken = "version 1\nname \"Proof\"\nphysics { substeps "
                         "\"two\"; }\n(array)entities\n";
    write_file("content/scenes/Proof.kdl", broken);
    begin("validate_asset", "content/scenes/Proof.kdl");
    send();
    REQUIRE(run() != 0 && !ok(0));
    REQUIRE(!yyjson_get_bool(get(result(0), "valid")));
    yyjson_val *diagnostics = get(result(0), "diagnostics");
    REQUIRE(yyjson_arr_size(diagnostics) == 1);
    yyjson_val *diagnostic = yyjson_arr_get(diagnostics, 0);
    REQUIRE(string_is(get(diagnostic, "code"), "INVALID_TYPE"));
    REQUIRE(string_is(get(diagnostic, "property_path"), "physics.substeps"));
    REQUIRE(string_is(get(diagnostic, "expected"), "integer"));
    REQUIRE(yyjson_get_int(get(diagnostic, "line")) == 3);
    REQUIRE(yyjson_get_int(get(diagnostic, "end")) > yyjson_get_int(get(diagnostic, "start")));
    REQUIRE(!string_is(get(diagnostic, "rule_name"), "LOAD"));
    return true;
}

TEST_CASE(controller_catalog_exposes_full_resource_identity_with_raw_kind_filters)
{
    /* Invalid filters fail even before an inventory contains raw resources. */
    begin("resource_catalog", NULL);
    yyjson_mut_val *invalid_kinds = yyjson_mut_obj_add_arr(request, input, "kinds");
    yyjson_mut_arr_add_int(request, invalid_kinds, 7);
    send();
    begin("resource_catalog", NULL);
    yyjson_mut_obj_add_int(request, input, "query", 7);
    send();
    REQUIRE(run() != 0 && !ok(0) && !ok(1));
    REQUIRE(string_is(get(get(yyjson_doc_get_root(replies[0]), "error"), "code"), "invalid_request"));
    REQUIRE(string_is(get(get(yyjson_doc_get_root(replies[1]), "error"), "code"), "invalid_request"));
    unused_image();
    begin("catalog", NULL);
    send();
    begin("search_assets", NULL);
    yyjson_mut_obj_add_str(request, input, "query", "unused");
    yyjson_mut_val *kinds = yyjson_mut_obj_add_arr(request, input, "kinds");
    yyjson_mut_arr_add_str(request, kinds, "image");
    send();
    REQUIRE(run() == 0 && ok(0) && ok(1));
    yyjson_val *catalog = get(result(0), "resource_catalog");
    yyjson_val *filtered = get(result(1), "resource_catalog");
    REQUIRE(yyjson_get_bool(get(catalog, "complete")));
    REQUIRE(yyjson_get_bool(get(filtered, "complete")));
    const char *identity = yyjson_get_str(get(catalog, "content_hash"));
    REQUIRE(identity && strlen(identity) == 16);
    REQUIRE(string_is(get(filtered, "content_hash"), identity));
    REQUIRE(string_is(get(result(0), "project_content_hash"), identity));
    REQUIRE(yyjson_get_str(get(catalog, "resource_hash")));
    REQUIRE(yyjson_arr_size(get(catalog, "authored_sources")) >= 6);
    yyjson_val *resources = get(filtered, "resources");
    REQUIRE(yyjson_arr_size(resources) == 1);
    yyjson_val *image = yyjson_arr_get(resources, 0);
    REQUIRE(string_is(get(image, "kind"), "image"));
    REQUIRE(string_is(get(image, "key"), "content/unused.png"));
    REQUIRE(yyjson_get_str(get(image, "content_hash")));
    REQUIRE(yyjson_get_int(get(get(image, "metadata"), "width")) == 1);
    REQUIRE(yyjson_arr_size(get(result(1), "assets")) == 0);
    REQUIRE(unchanged());
    return true;
}

TEST_CASE(controller_replay_rejects_changed_unreferenced_raw_resource)
{
    unused_image();
    begin("record_scenario", NULL);
    yyjson_mut_obj_add_str(request, input, "kind", "prototype");
    yyjson_mut_obj_add_str(request, input, "name", "Actor");
    yyjson_mut_obj_add_uint(request, input, "frames", 1);
    send();
    REQUIRE(run() == 0 && ok(0));
    const char *recorded = yyjson_get_str(get(result(0), "recording_path"));
    REQUIRE(recorded && strlen(recorded) < sizeof recording_path);
    snprintf(recording_path, sizeof recording_path, "%s", recorded);
    begin("replay_scenario", NULL);
    yyjson_mut_obj_add_str(request, input, "recording_path", recording_path);
    send();
    REQUIRE(run() == 0 && ok(0));
    char file[256];
    path(file, sizeof file, "content/unused.png");
    FILE *stream = fopen(file, "ab");
    REQUIRE(stream && fputc(0, stream) != EOF && !fclose(stream));
    REQUIRE(unchanged());
    begin("replay_scenario", NULL);
    yyjson_mut_obj_add_str(request, input, "recording_path", recording_path);
    send();
    REQUIRE(run() != 0 && !ok(0));
    yyjson_val *error = get(yyjson_doc_get_root(replies[0]), "error");
    REQUIRE(string_is(get(error, "code"), "validation_failed"));
    const char *message = yyjson_get_str(get(error, "message"));
    REQUIRE(message && strstr(message, "runtime content changed"));
    return true;
}

void test_tool_controller(const char *binary)
{
    assert(binary && binary[0] == '/');
    program = binary;
    pu_setup(setup, teardown);
    RUN_TEST_CASE(controller_analysis_and_rename_preview);
    RUN_TEST_CASE(controller_diagnostics_preserve_source_ranges);
    RUN_TEST_CASE(controller_inspection_and_execution_status);
    RUN_TEST_CASE(controller_profile_accounts_for_measured_execution);
    RUN_TEST_CASE(controller_profile_excludes_warmup_only_run);
    RUN_TEST_CASE(controller_material_creation_discovery_and_editable_inspection);
    RUN_TEST_CASE(controller_structural_diagnostics_avoid_duplicate_loader_fallout);
    RUN_TEST_CASE(controller_catalog_exposes_full_resource_identity_with_raw_kind_filters);
    RUN_TEST_CASE(controller_replay_rejects_changed_unreferenced_raw_resource);
    pu_setup(NULL, NULL);
}
