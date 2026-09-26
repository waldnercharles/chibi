#include "assets/project.h"
#include "automation/controller.h"
#include "graph/model/registry.h"
#include "suite_internal.h"
#include "third_party/yyjson/yyjson.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* An agent's query and the node it means: the type, the component, struct or
 * event it names, and for a Set the field it writes. */
typedef struct
{
    const char *query;
    const char *domain;
    const char *type;
    const char *name;
    const char *field;
} intent;

static const intent INTENTS[] = {
    { "set health current", "behavior", "Set Component", "GameHealth", "current" },
    { "set health", "behavior", "Set Component", "GameHealth", NULL },
    { "get transform x", "behavior", "Get Component", "GameTransform", NULL },
    { "has aim", "behavior", "Has Component", "GameAim", NULL },
    { "add health", "behavior", "Add Component", "GameHealth", NULL },
    { "remove velocity", "behavior", "Remove Component", "GameVelocity", NULL },
    { "make transform", "behavior", "Make Struct", "GameTransform", NULL },
    { "break velocity", "behavior", "Break Struct", "GameVelocity", NULL },
    { "branch", "behavior", "Branch", NULL, NULL },
    { "random range", "behavior", "Random Range", NULL, NULL },
    { "destroy entity", "behavior", "Destroy Entity", NULL, NULL },
    { "spawn prototype", "behavior", "Spawn Prototype", NULL, NULL },
    { "play sound", "behavior", "Play Sound", NULL, NULL },
    { "clamp", "behavior", "Clamp", NULL, NULL },
    { "delay", "behavior", "Delay", NULL, NULL },
    { "for each", "behavior", "ForEach", NULL, NULL },
    { "sequence", "behavior", "Sequence", NULL, NULL },
    { "delta time", "behavior", "Delta Time", NULL, NULL },
    { "base color", "shader", "Shader Surface Output", NULL, NULL },
    { "get velocity", "system", "Get_GameVelocity", NULL, NULL },
    { "on hit", "behavior", "Event", "Contact Begin", NULL },
    { "every frame", "behavior", "Event", "Tick", NULL },
    { "on start", "behavior", "Event", "Begin", NULL },
    { "send damaged", "behavior", "Send Event", "Damaged", NULL },
    { "wait", "behavior", "Delay", NULL, NULL },
    { "if", "behavior", "Branch", NULL, NULL },
    { "kill", "behavior", "Destroy Entity", NULL, NULL },
    { "instantiate", "behavior", "Spawn Prototype", NULL, NULL },
    { "closest", "behavior", "Find Nearest Entity", NULL, NULL },
    { "owner", "behavior", "Self", NULL, NULL },
    { "sqrt", "behavior", "Square Root", NULL, NULL },
    { "ln", "behavior", "Logarithm", NULL, NULL },
    { "vec2", "behavior", "Make Struct", "CF_V2", NULL },
};

static char *search(const char *arguments, size_t *bytes)
{
    char request[512];
    snprintf(request, sizeof request, "{\"contract\":1,\"operation\":\"search_nodes\",%s}", arguments);
    int exit_code = 0;
    char *response = chibi_automation_handle(request, ".", &exit_code);
    *bytes = response ? strlen(response) : 0;
    return response;
}

static bool same(const char *text, const char *expected)
{
    return expected ? text && !strcmp(text, expected) : !text;
}

/* A result names a component or struct in its setting, or an event. */
static bool means(yyjson_val *node, const intent *wanted)
{
    yyjson_val *inputs = yyjson_obj_get(node, "inputs");
    const char *name = yyjson_get_str(yyjson_obj_get(inputs, "Component"));
    name = name ? name : yyjson_get_str(yyjson_obj_get(inputs, "Type"));
    name = name ? name : yyjson_get_str(yyjson_obj_get(node, "event_name"));
    name = name ? name : yyjson_get_str(yyjson_obj_get(node, "reference_name"));
    return same(yyjson_get_str(yyjson_obj_get(node, "type")), wanted->type) &&
           same(name, wanted->name) &&
           same(yyjson_get_str(yyjson_obj_get(node, "field")), wanted->field);
}

/* Each intent's node is among the first three results. */
static void intents_rank()
{
    int found = 0;
    for (size_t i = 0; i < sizeof INTENTS / sizeof *INTENTS; i++) {
        const intent *wanted = &INTENTS[i];
        char arguments[256];
        snprintf(
            arguments,
            sizeof arguments,
            "\"query\":\"%s\",\"domain\":\"%s\",\"limit\":3,\"include_pins\":"
            "false",
            wanted->query,
            wanted->domain
        );
        size_t bytes;
        char *response = search(arguments, &bytes);
        yyjson_doc *doc = yyjson_read(response, bytes, 0);
        yyjson_val *nodes = yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(doc), "result"), "nodes");
        bool hit = false;
        size_t at, max;
        yyjson_val *node;
        yyjson_arr_foreach(nodes, at, max, node)
        {
            hit |= means(node, wanted);
        }
        if (!hit) {
            printf("  \"%s\" missed %s: %s\n", wanted->query, wanted->type, response);
        }
        found += hit;
        yyjson_doc_free(doc);
        free(response);
    }
    test_check(found == (int)(sizeof INTENTS / sizeof *INTENTS), "searchNodes ranks the node an agent's query means in its first three results");
}

/* Behavior search offers component nodes, not the generated nodes they lower
 * to; System graphs name generated nodes directly. */
static void behavior_vocabulary()
{
    size_t bytes;
    char *behavior = search("\"query\":\"velocity\",\"domain\":\"behavior\",\"limit\":50", &bytes);
    char *system = search("\"query\":\"velocity\",\"domain\":\"system\",\"limit\":50", &bytes);
    test_check(
        behavior && !strstr(behavior, "\"Get_GameVelocity\"") &&
            strstr(behavior, "\"Get Component\"") && system &&
            strstr(system, "\"Get_GameVelocity\"") &&
            !strstr(system, "\"Get Component\""),
        "searchNodes offers component nodes to behaviors and generated nodes "
        "to systems"
    );
    free(behavior);
    free(system);
}

/* A result shows how Behavior Expression writes its node. */
static void expression_forms()
{
    size_t bytes;
    char *clamp = search("\"query\":\"clamp\",\"domain\":\"behavior\",\"limit\":1", &bytes);
    char *smooth = search("\"query\":\"smoothstep\",\"domain\":\"shader\",\"limit\":1", &bytes);
    test_check(
        clamp && strstr(clamp, "\"expression\":\"clamp(Value, Min, Max)\"") && smooth &&
            strstr(smooth, "\"expression\":\"smoothstep(Min, Max, Value)\""),
        "searchNodes gives the expression that writes a node, in argument order"
    );
    free(clamp);
    free(smooth);
}

/* A wire's type filters and ranks nodes by the pin that takes it, like a
 * dropped wire in Studio: exact types first, then conversions, then
 * wildcards. */
static void wire_fits()
{
    size_t bytes;
    char *destroy = search("\"query\":\"destroy\",\"wire_from\":\"ecs.entity\",\"include_pins\":false", &bytes);
    char *health = search("\"wire_to\":\"GameHealth\",\"domain\":\"behavior\",\"limit\":1,\"include_pins\":false", &bytes);
    char *after = search("\"query\":\"delay\",\"wire_from\":\"exec\",\"limit\":1,\"include_pins\":false", &bytes);
    char *real = search("\"wire_from\":\"int\",\"wire_to\":\"float\",\"limit\":1,\"include_pins\":false", &bytes);
    char *unknown = search("\"wire_from\":\"GameNothing\"", &bytes);
    test_check(
        destroy && strstr(destroy, "\"type\":\"Destroy Entity\"") &&
            strstr(destroy, "\"wire_input\":\"Entity\"") && health &&
            strstr(health, "\"wire_output\":\"GameHealth\"") && after &&
            strstr(after, "\"type\":\"Delay\"") &&
            strstr(after, "\"wire_input\":\"Exec\"") && real &&
            strstr(real, "\"type\":\"To Real\"") && unknown &&
            strstr(unknown, "\"invalid_request\""),
        "searchNodes ranks nodes by the pin that takes a wire of a given type"
    );
    free(destroy);
    free(health);
    free(after);
    free(real);
    free(unknown);
}

/* Every node the outline offers, in every domain, describes itself. */
static void descriptions_complete()
{
    size_t bytes;
    char *listed = search("\"include_pins\":false", &bytes);
    char *system = search("\"domain\":\"system\"", &bytes);
    int missing = 0;
    const char *responses[] = { listed, system };
    for (int r = 0; r < 2; r++) {
        yyjson_doc *doc = yyjson_read(responses[r], strlen(responses[r]), 0);
        yyjson_val *categories = yyjson_obj_get(
            yyjson_obj_get(yyjson_doc_get_root(doc), "result"),
            "categories"
        );
        size_t at, max, i, count;
        yyjson_val *category, *names, *name;
        yyjson_obj_foreach(categories, at, max, category, names)
        {
            yyjson_arr_foreach(names, i, count, name)
            {
                const node_desc *desc = registry_node_by_name(yyjson_get_str(name));
                if (!desc->description || !desc->description[0]) {
                    printf("  %s has no description\n", desc->name);
                    missing++;
                }
            }
        }
        yyjson_doc_free(doc);
    }
    test_check(!missing, "every node searchNodes lists has a description");
    free(listed);
    free(system);
}

/* inspectGraph lists each node's pins with the types wildcards resolved to,
 * which the serialized nodes omit. */
static void inspected_pins()
{
    char root[] = "/tmp/chibi-inspect-XXXXXX";
    assert(mkdtemp(root));
    char manifest[128], content[128], behaviors[128], asset[160];
    snprintf(manifest, sizeof manifest, "%s/" CHIBI_PROJECT_MANIFEST, root);
    snprintf(content, sizeof content, "%s/content", root);
    snprintf(behaviors, sizeof behaviors, "%s/content/behaviors", root);
    snprintf(asset, sizeof asset, "%s/Pins.yaml", behaviors);
    FILE *file = fopen(manifest, "w");
    assert(file);
    fprintf(file, "version: %d\ncontent: content\n", CHIBI_PROJECT_VERSION);
    fclose(file);
    assert(!mkdir(content, 0755) && !mkdir(behaviors, 0755));
    char request[1024];
    snprintf(
        request,
        sizeof request,
        "{\"contract\":1,\"operation\":\"edit_graph\",\"project_root\":\"%s\","
        "\"kind\":\"behavior\",\"name\":\"Pins\",\"create\":true,"
        "\"operations\":["
        "{\"op\":\"add_node\",\"ref\":\"two\",\"type\":\"Real\",\"inputs\":{"
        "\"Value\":2}},"
        "{\"op\":\"add_node\",\"ref\":\"sum\",\"type\":\"Add\"},"
        "{\"op\":\"connect\",\"from\":\"two\",\"from_pin\":\"Value\",\"to\":"
        "\"sum\",\"to_pin\":\"A\"}]}",
        root
    );
    int exit_code = 0;
    char *edited = chibi_automation_handle(request, ".", &exit_code);
    snprintf(
        request,
        sizeof request,
        "{\"contract\":1,\"operation\":\"inspect_graph\",\"project_root\":\"%"
        "s\","
        "\"kind\":\"behavior\",\"name\":\"Pins\"}",
        root
    );
    char *inspected = chibi_automation_handle(request, ".", &exit_code);
    test_check(
        edited && strstr(edited, "\"ok\":true") && inspected &&
            strstr(inspected, "\"type\":\"Add\"") &&
            strstr(inspected, "\"in\":[\"A: double\",") &&
            strstr(inspected, "\"out\":[\"Result: double\"]"),
        "inspectGraph lists each node's pins with their resolved types"
    );
    free(edited);
    free(inspected);
    unlink(asset);
    rmdir(behaviors);
    rmdir(content);
    unlink(manifest);
    rmdir(root);
}

/* Responses stay small enough to read whole: the vocabulary outline, and a
 * page of results with pins. */
static void response_budgets()
{
    size_t outline, page;
    char *listed = search("\"domain\":\"behavior\"", &outline);
    char *found = search("\"query\":\"health\"", &page);
    test_check(
        listed && strstr(listed, "\"categories\"") &&
            strstr(listed, "\"GameHealth\"") && strstr(listed, "\"Contact Begin\"") &&
            outline < 8192 && found && page < 2560,
        "searchNodes lists the behavior vocabulary in under 8 KB and a page of "
        "pins in under 2.5 KB"
    );
    free(listed);
    free(found);
}

void test_automation_search()
{
    intents_rank();
    behavior_vocabulary();
    expression_forms();
    wire_fits();
    descriptions_complete();
    inspected_pins();
    response_budgets();
}
