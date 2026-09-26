/* findReferences, callHierarchy and inspectGraph's use counts over a small
 * project: an event definition, a behavior that sends and handles it, calls
 * its functions and reads its variable through a local, and a ui document
 * that sends the event too. */
#include "assets/call_hierarchy.h"
#include "assets/project.h"
#include "automation/controller.h"
#include "automation/navigation.h"
#include "suite_internal.h"
#include "third_party/yyjson/yyjson.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct
{
    char root[64];
    yyjson_doc *doc; /* the last response */
} project;

/* Sends an operation's fields to the controller for the project; the
 * response stays readable until the next request. */
static yyjson_val *ask(project *p, const char *operation, const char *format, ...)
{
    char fields[2048], request[2304];
    va_list args;
    va_start(args, format);
    vsnprintf(fields, sizeof fields, format, args);
    va_end(args);
    snprintf(
        request,
        sizeof request,
        "{\"contract\":1,\"operation\":\"%s\",\"project_root\":\"%s\",%s}",
        operation,
        p->root,
        fields
    );
    int exit_code = 0;
    char *response = chibi_automation_handle(request, ".", &exit_code);
    yyjson_doc_free(p->doc);
    p->doc = yyjson_read(response, strlen(response), 0);
    free(response);
    return yyjson_doc_get_root(p->doc);
}

static yyjson_val *result_of(yyjson_val *response)
{
    return yyjson_obj_get(response, "result");
}

static bool ok(yyjson_val *response)
{
    return yyjson_get_bool(yyjson_obj_get(response, "ok"));
}

static const char *error_code(yyjson_val *response)
{
    return yyjson_get_str(yyjson_obj_get(yyjson_obj_get(response, "error"), "code"));
}

static int count(yyjson_val *object, const char *role)
{
    return yyjson_get_int(yyjson_obj_get(yyjson_obj_get(object, "counts"), role));
}

static const char *text(yyjson_val *object, const char *key)
{
    const char *value = yyjson_get_str(yyjson_obj_get(object, key));
    return value ? value : "";
}

static bool named(yyjson_val *object, const char *key, const char *name)
{
    return !strcmp(text(object, key), name);
}

/* The first array item whose key names `name`. */
static yyjson_val *item(yyjson_val *array, const char *key, const char *name)
{
    size_t at, max;
    yyjson_val *value;
    yyjson_arr_foreach(array, at, max, value)
    {
        if (named(value, key, name)) {
            return value;
        }
    }
    return NULL;
}

static void write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    assert(file);
    fputs(text, file);
    fclose(file);
}

static project project_new()
{
    project p = { .root = "/tmp/chibi-navigation-XXXXXX" };
    assert(mkdtemp(p.root));
    char path[160];
    snprintf(path, sizeof path, "%s/" CHIBI_PROJECT_MANIFEST, p.root);
    char manifest[64];
    snprintf(manifest, sizeof manifest, "version: %d\ncontent: content\n", CHIBI_PROJECT_VERSION);
    write_file(path, manifest);
    static const char *const FOLDERS[] = { "content",
                                           "content/events",
                                           "content/behaviors",
                                           "content/ui" };
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof path, "%s/%s", p.root, FOLDERS[i]);
        assert(!mkdir(path, 0755));
    }
    snprintf(path, sizeof path, "%s/content/ui/Panel.yaml", p.root);
    write_file(path, "rml: '<div id=\"strike\" data-event=\"Hit\">Strike</div>'\n");
    assert(ok(
        ask(&p, "edit_graph", "\"kind\":\"event\",\"name\":\"Hit\",\"create\":true,\"operations\":[{\"op\":\"add_variable\",\"name\":\"Amount\",\"type\":\"int\"}]")
    ));
    yyjson_val *target = ask(
        &p,
        "edit_graph",
        "\"kind\":\"behavior\",\"name\":\"Target\",\"create\":true,"
        "\"operations\":["
        "{\"op\":\"add_variable\",\"name\":\"hp\",\"type\":\"int\"},"
        "{\"op\":\"add_graph\",\"name\":\"Take\",\"kind\":\"function\",\"ref\":"
        "\"take\"},"
        "{\"op\":\"add_pin\",\"node\":\"take.entry\",\"side\":\"output\","
        "\"name\":\"Amount\",\"type\":\"int\"},"
        "{\"op\":\"add_variable_node\",\"ref\":\"store\",\"kind\":\"set\","
        "\"variable\":\"hp\"},"
        "{\"op\":\"connect\",\"from\":\"take.entry\",\"from_pin\":0,\"to\":"
        "\"store\",\"to_pin\":0,\"kind\":\"exec\"},"
        "{\"op\":\"connect\",\"from\":\"take.entry\",\"from_pin\":\"Amount\","
        "\"to\":\"store\",\"to_pin\":1},"
        "{\"op\":\"add_graph\",\"name\":\"Loop\",\"kind\":\"function\",\"ref\":"
        "\"loop\"},"
        "{\"op\":\"add_node\",\"ref\":\"again\",\"type\":\"Call "
        "Function\",\"reference_name\":\"Loop\"},"
        "{\"op\":\"connect\",\"from\":\"loop.entry\",\"from_pin\":0,\"to\":"
        "\"again\",\"to_pin\":0,\"kind\":\"exec\"},"
        "{\"op\":\"select_graph\",\"graph\":0},"
        "{\"op\":\"add_node\",\"ref\":\"hit\",\"type\":\"Event\",\"event_"
        "name\":\"Hit\"},"
        "{\"op\":\"add_node\",\"ref\":\"call\",\"type\":\"Call "
        "Function\",\"reference_name\":\"Take\"},"
        "{\"op\":\"connect\",\"from\":\"hit\",\"from_pin\":0,\"to\":\"call\","
        "\"to_pin\":0,\"kind\":\"exec\"},"
        "{\"op\":\"add_variable_node\",\"ref\":\"read\",\"kind\":\"get\","
        "\"variable\":\"hp\"},"
        "{\"op\":\"connect\",\"from\":\"read\",\"from_pin\":0,\"to\":\"call\","
        "\"to_pin\":\"Amount\"},"
        "{\"op\":\"create_local\",\"node\":\"read\",\"pin\":0,\"name\":"
        "\"life\"},"
        "{\"op\":\"add_node\",\"ref\":\"tick\",\"type\":\"Event\",\"event_"
        "name\":\"Tick\"},"
        "{\"op\":\"add_node\",\"ref\":\"send\",\"type\":\"Send "
        "Event\",\"reference_name\":\"Hit\"},"
        "{\"op\":\"connect\",\"from\":\"tick\",\"from_pin\":0,\"to\":\"send\","
        "\"to_pin\":0,\"kind\":\"exec\"}]"
    );
    if (!ok(target)) {
        printf("  %s\n", yyjson_write(p.doc, 0, NULL));
    }
    assert(ok(target));
    return p;
}

static void project_free(project *p)
{
    yyjson_doc_free(p->doc);
    char command[128];
    snprintf(command, sizeof command, "rm -rf '%s'", p->root);
    assert(system(command) == 0);
}

#define TARGET "\"asset\":{\"kind\":\"behavior\",\"name\":\"Target\"}"

/* An event's definition, senders by graph node and ui document, and
 * handlers, with counts; a variable's reads and writes; a parameter's
 * arguments and reads; a local's readers by pin. */
static void references(project *p)
{
    yyjson_val *r = result_of(ask(p, "find_references", "\"event\":\"Hit\""));
    yyjson_val *uses = yyjson_obj_get(r, "uses");
    yyjson_val *panel = NULL, *send = NULL;
    size_t at, max;
    yyjson_val *use;
    yyjson_arr_foreach(uses, at, max, use)
    {
        yyjson_val *asset = yyjson_obj_get(use, "asset");
        panel = named(asset, "kind", "ui") ? use : panel;
        send = named(use, "role", "send") && named(asset, "name", "Target") ? use : send;
    }
    test_check(
        named(yyjson_obj_get(yyjson_obj_get(r, "definition"), "asset"), "path", "content/events/Hit.yaml") &&
            count(r, "send") == 2 && count(r, "handle") == 1 && panel &&
            !yyjson_obj_get(panel, "node") && send && yyjson_obj_get(send, "node") &&
            yyjson_get_uint(yyjson_obj_get(send, "graph_id")) == 0,
        "findReferences lists an event's definition, graph and ui senders, and "
        "handlers"
    );

    r = result_of(ask(p, "find_references", TARGET ",\"variable\":\"hp\""));
    test_check(
        count(r, "read") == 1 && count(r, "write") == 1 &&
            named(yyjson_obj_get(yyjson_obj_get(r, "definition"), "asset"), "name", "Target") &&
            named(yyjson_obj_get(r, "symbol"), "name", "hp"),
        "findReferences counts a variable's reads and writes"
    );

    r = result_of(
        ask(p, "find_references", TARGET ",\"graph\":\"Take\",\"parameter\":\"Amount\"")
    );
    yyjson_val *argument = item(yyjson_obj_get(r, "uses"), "role", "argument");
    test_check(
        count(r, "argument") == 1 && count(r, "read") == 1 &&
            named(argument, "pin", "Amount") &&
            named(yyjson_obj_get(r, "definition"), "pin", "Amount") &&
            named(yyjson_obj_get(r, "definition"), "graph", "Take"),
        "findReferences finds a parameter's call pins and reads by name"
    );

    r = result_of(ask(p, "find_references", TARGET ",\"local\":\"life\""));
    test_check(
        count(r, "read") == 1 &&
            named(yyjson_arr_get(yyjson_obj_get(r, "uses"), 0), "pin", "Amount"),
        "findReferences finds a local's readers"
    );

    /* Roles filter the page; counts still cover every role. */
    r = result_of(
        ask(p, "find_references", "\"event\":\"Hit\",\"roles\":[\"send\"],\"limit\":1,\"offset\":1")
    );
    test_check(
        yyjson_get_int(yyjson_obj_get(r, "total")) == 2 &&
            yyjson_get_int(yyjson_obj_get(r, "count")) == 1 &&
            !yyjson_get_bool(yyjson_obj_get(r, "has_more")) && count(r, "handle") == 1 &&
            named(yyjson_arr_get(yyjson_obj_get(r, "uses"), 0), "role", "send"),
        "findReferences pages uses filtered by role"
    );

    r = result_of(ask(p, "find_references", "\"component\":\"GameHealth\",\"field\":\"current\""));
    test_check(
        named(yyjson_obj_get(r, "definition"), "component", "GameHealth") &&
            named(yyjson_obj_get(r, "definition"), "type", "int"),
        "findReferences defines a component field by its reflected type"
    );
}

/* Names that select nothing fail with what is missing. */
static void selection_errors(project *p)
{
    static const struct
    {
        const char *fields, *code;
    } CASES[] = {
        { "\"asset\":{\"kind\":\"behavior\",\"name\":\"Missing\"}", "asset_not_found" },
        { TARGET ",\"variable\":\"mp\"", "asset_not_found" },
        { TARGET ",\"parameter\":\"Amount\"", "invalid_request" },
        { TARGET ",\"graph\":\"Nope\"", "asset_not_found" },
        { "\"component\":\"GameNothing\"", "asset_not_found" },
        { "\"component\":\"GameHealth\",\"field\":\"nothing\"", "asset_not_found" },
        { "\"event\":\"Hit\",\"component\":\"GameHealth\"", "invalid_request" },
        { "\"event\":\"Hit\",\"roles\":[\"define\"]", "invalid_request" },
        { "\"event\":\"Hit\",\"limit\":0", "invalid_request" },
    };
    int failed = 0;
    for (size_t i = 0; i < sizeof CASES / sizeof *CASES; i++) {
        const char *code = error_code(ask(p, "find_references", "%s", CASES[i].fields));
        if (!code || strcmp(code, CASES[i].code)) {
            printf("  %s: %s\n", CASES[i].fields, code ? code : "ok");
            failed++;
        }
    }
    test_check(!failed, "findReferences rejects selections that name nothing");
}

/* Callers of a function reach through the handler whose run calls it to its
 * event's senders, a ui document among them; a recursive call is a cycle. */
static void hierarchy(project *p)
{
    yyjson_val *r = result_of(
        ask(p, "call_hierarchy", TARGET ",\"graph\":\"Take\",\"direction\":\"incoming\",\"depth\":3")
    );
    yyjson_val *root = yyjson_obj_get(r, "root");
    yyjson_val *handler = item(yyjson_obj_get(root, "callers"), "kind", "handler");
    yyjson_val *event = item(yyjson_obj_get(handler, "callers"), "kind", "event");
    yyjson_val *senders = yyjson_obj_get(event, "callers");
    yyjson_val *place = item(senders, "kind", "place");
    test_check(
        named(root, "name", "Take") && !yyjson_obj_get(root, "callees") &&
            named(handler, "event", "Hit") &&
            yyjson_arr_size(yyjson_obj_get(handler, "sites")) == 1 &&
            named(event, "name", "Hit") &&
            named(item(senders, "kind", "handler"), "event", "Tick") &&
            named(yyjson_obj_get(place, "asset"), "name", "Panel"),
        "callHierarchy traces a function's callers through handlers to ui "
        "senders"
    );

    r = result_of(ask(p, "call_hierarchy", "\"event\":\"Tick\",\"direction\":\"outgoing\""));
    yyjson_val *tick = item(yyjson_obj_get(yyjson_obj_get(r, "root"), "callees"), "kind", "handler");
    yyjson_val *hit = item(yyjson_obj_get(tick, "callees"), "name", "Hit");
    test_check(
        yyjson_get_bool(yyjson_obj_get(yyjson_obj_get(r, "root"), "host")) &&
            hit && yyjson_get_bool(yyjson_obj_get(hit, "more")),
        "callHierarchy follows a host event's handler to the events it sends"
    );

    r = result_of(ask(p, "call_hierarchy", TARGET ",\"graph\":\"Loop\""));
    yyjson_val *again = yyjson_arr_get(yyjson_obj_get(yyjson_obj_get(r, "root"), "callees"), 0);
    test_check(
        named(again, "name", "Loop") &&
            yyjson_get_bool(yyjson_obj_get(again, "cycle")) &&
            !yyjson_obj_get(again, "callees"),
        "callHierarchy marks a recursive call as a cycle"
    );
    test_check(
        !strcmp(error_code(ask(p, "call_hierarchy", TARGET ",\"variable\":\"hp\"")), "invalid_request") &&
            !strcmp(error_code(ask(p, "call_hierarchy", "\"event\":\"Hit\",\"depth\":9")), "invalid_request"),
        "callHierarchy takes a function or event within its depth"
    );
}

/* inspectGraph counts each function's calls and each variable's reads and
 * writes across the project when asked to; counting reads every asset. */
static void use_counts(project *p)
{
    yyjson_val *r = result_of(
        ask(p, "inspect_graph", "\"kind\":\"behavior\",\"name\":\"Target\",\"include_pins\":false")
    );
    bool uncounted = !yyjson_obj_get(r, "uses") &&
                     !yyjson_obj_get(item(yyjson_obj_get(r, "graphs"), "name", "Take"), "uses");
    r = result_of(
        ask(p, "inspect_graph", "\"kind\":\"behavior\",\"name\":\"Target\",\"include_pins\":false,\"include_uses\":true")
    );
    yyjson_val *take = yyjson_obj_get(item(yyjson_obj_get(r, "graphs"), "name", "Take"), "uses");
    yyjson_val *hp = yyjson_obj_get(item(yyjson_obj_get(r, "variables"), "name", "hp"), "uses");
    bool members = yyjson_get_int(yyjson_obj_get(take, "call")) == 1 &&
                   yyjson_get_int(yyjson_obj_get(hp, "read")) == 1 &&
                   yyjson_get_int(yyjson_obj_get(hp, "write")) == 1;
    r = result_of(
        ask(p, "inspect_graph", "\"kind\":\"event\",\"name\":\"Hit\",\"include_pins\":false,\"include_uses\":true")
    );
    yyjson_val *event = yyjson_obj_get(r, "uses");
    test_check(
        uncounted && members && yyjson_get_int(yyjson_obj_get(event, "send")) == 2 &&
            yyjson_get_int(yyjson_obj_get(event, "handle")) == 1,
        "inspectGraph counts uses of an asset's functions, variables and event "
        "with include_uses"
    );
}

/* A function more callers call than a tree holds lists as many as fit, and
 * its root says more were left out. */
static void wide_hierarchy()
{
    enum
    {
        CALLERS = CALL_HIERARCHY_ITEMS + 76,
    };
    behavior_asset *asset = behavior_asset_new("Wide");
    graph *target = behavior_asset_add_function(asset, "Target", false);
    for (int i = 0; i < CALLERS; i++) {
        char name[32];
        snprintf(name, sizeof name, "Caller %d", i);
        graph *caller = behavior_asset_add_function(asset, name, false);
        assert(behavior_asset_add_function_call_node_to_graph(asset, caller, target));
    }
    reference_index *index = reference_index_new();
    int wide = reference_index_add_asset(index, REFERENCE_ASSET_BEHAVIOR, "Wide", "content/behaviors/Wide.yaml", NULL);
    reference_index_add_graphs(index, wide, asset);
    navigation_asset names[] = { { "behavior", "Wide", "content/behaviors/Wide.yaml" } };
    assert(reference_index_asset_count(index) == 1);
    navigation nav = { index, names };
    const char
        *request = "{\"asset\":{\"kind\":\"behavior\",\"name\":\"Wide\"},"
                   "\"graph\":\"Target\",\"direction\":\"incoming\"}";
    yyjson_doc *doc = yyjson_read(request, strlen(request), 0);
    yyjson_mut_doc *out = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *result = yyjson_mut_obj(out);
    char error[256];
    int status = automation_call_hierarchy(&nav, yyjson_doc_get_root(doc), out, result, error, sizeof error);
    yyjson_mut_val *root = yyjson_mut_obj_get(result, "root");
    test_check(
        status == CHIBI_AUTOMATION_OK &&
            yyjson_mut_get_bool(yyjson_mut_obj_get(root, "more")) &&
            yyjson_mut_arr_size(yyjson_mut_obj_get(root, "callers")) ==
                CALL_HIERARCHY_ITEMS - 1,
        "callHierarchy lists the callers that fit and marks the root with more"
    );
    yyjson_mut_doc_free(out);
    yyjson_doc_free(doc);
    reference_index_free(index);
    behavior_asset_free(asset);
}

void test_automation_navigation()
{
    project p = project_new();
    references(&p);
    selection_errors(&p);
    hierarchy(&p);
    use_counts(&p);
    project_free(&p);
    wide_hierarchy();
}
