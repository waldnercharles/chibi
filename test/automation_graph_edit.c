#include "assets/code_source.h"
#include "assets/project.h"
#include "automation/controller.h"
#include "automation/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/model/behavior_asset.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/instance.h"
#include "suite_internal.h"
#include "support/text_file.h"
#include "third_party/yyjson/yyjson.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The last request's error. */
static char edit_error[256];

static void automatic_layout_on_disk(const char *edited)
{
    char root[] = "/tmp/chibi-graph-layout-XXXXXX";
    assert(mkdtemp(root));
    char content[256], behaviors[256], path[256];
    snprintf(content, sizeof content, "%s/content", root);
    snprintf(behaviors, sizeof behaviors, "%s/content/behaviors", root);
    snprintf(path, sizeof path, "%s/content/behaviors/GraphEditBehavior.code", root);
    assert(!mkdir(content, 0700) && !mkdir(behaviors, 0700));
    FILE *file = fopen(path, "wb");
    assert(file && fputs(edited, file) >= 0 && !fclose(file));
    int formatted = chibi_automation_format_assets(root, false);
    char *disk = text_file_read_disk(path);
    bool canonical = formatted >= 0 && formatted <= 1 && disk &&
                     code_source_is_structured(disk) && !strstr(disk, "@node") &&
                     !strstr(disk, "@authoring") && !strstr(disk, "placed =") &&
                     !strstr(disk, " x =") && !strstr(disk, " y =") &&
                     chibi_automation_format_assets(root, true) == 0;
    test_check(canonical, "MCP automatic graph layout formats to stable disk text without positions");
    behavior_asset *original = behavior_asset_load(edited);
    code_source *reopened = disk ? code_source_parse(disk) : NULL;
    test_check(reopened && reopened->asset, "MCP graph executable reopens without optional layout metadata");
    bool restored = reopened && code_source_metadata_load(reopened, path);
    test_check(restored && original && behavior_asset_equal(original, reopened->asset), "MCP graph layout and complete identities survive separate disk metadata");
    code_source_free(reopened);
    behavior_asset_free(original);
    free(disk);
    assert(!unlink(path));
    snprintf(path, sizeof path, "%s/content/behaviors/GraphEditBehavior.code.meta", root);
    unlink(path);
    assert(!rmdir(behaviors) && !rmdir(content) && !rmdir(root));
}

static int edit_request(
    const char *source,
    const char *kind,
    const char *name,
    const behavior_asset *const *libraries,
    int library_count,
    const char *request_json,
    char **edited,
    yyjson_mut_doc **response_doc
)
{
    yyjson_doc *request_doc = yyjson_read(request_json, strlen(request_json), 0);
    if (!request_doc) {
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *result = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, result);
    edit_error[0] = '\0';
    int status = automation_graph_edit(
        source,
        NULL,
        kind,
        name,
        libraries,
        library_count,
        yyjson_doc_get_root(request_doc),
        doc,
        result,
        edited,
        edit_error,
        sizeof edit_error
    );
    yyjson_doc_free(request_doc);
    if (response_doc) {
        *response_doc = doc;
    } else {
        yyjson_mut_doc_free(doc);
    }
    return status;
}

static node *find_node_type(graph *graph, const char *type)
{
    for (int i = 0; i < acount(graph->nodes); i++) {
        if (!strcmp(graph->nodes[i]->desc->name, type)) {
            return graph->nodes[i];
        }
    }
    return NULL;
}

static int find_pin(const node *n, const char *name, bool output)
{
    const pin_spec *pins = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    for (int i = 0; i < count; i++) {
        if (pins[i].name && !strcmp(pins[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static bool result_has_node_ref(yyjson_mut_doc *doc, const char *name)
{
    yyjson_mut_val *root = yyjson_mut_doc_get_root(doc);
    yyjson_mut_val *nodes = root ? yyjson_mut_obj_get(root, "nodes") : NULL;
    if (!nodes || !yyjson_mut_is_arr(nodes)) {
        return false;
    }
    yyjson_mut_arr_iter iterator;
    yyjson_mut_arr_iter_init(nodes, &iterator);
    yyjson_mut_val *entry;
    while ((entry = yyjson_mut_arr_iter_next(&iterator))) {
        yyjson_mut_val *ref = yyjson_mut_obj_get(entry, "ref");
        if (ref && yyjson_mut_is_str(ref) && !strcmp(yyjson_mut_get_str(ref), name)) {
            return true;
        }
    }
    return false;
}

static node *nth_node_type(graph *graph, const char *type, int nth)
{
    for (int i = 0; i < acount(graph->nodes); i++) {
        if (!strcmp(graph->nodes[i]->desc->name, type) && !nth--) {
            return graph->nodes[i];
        }
    }
    return NULL;
}

/* The edge into a node's named input. */
static const edge *edge_into(const graph *graph, const node *to, const char *pin)
{
    int index = find_pin(to, pin, false);
    for (int i = 0; index >= 0 && i < acount(graph->edges); i++) {
        if (graph->edges[i].to_id == to->id &&
            graph->edges[i].to_pin_id == to->input_pins[index].id) {
            return &graph->edges[i];
        }
    }
    return NULL;
}

static bool body_has_conversion(const behavior_asset *asset, const node *call, const node_desc *conversion)
{
    const graph *body = behavior_asset_find_graph(asset, call->reference_id);
    for (int i = 0; body && i < acount(body->edges); i++) {
        if (body->edges[i].conversion == conversion) {
            return true;
        }
    }
    return false;
}

static int edit_behavior(const char *source, const char *request, char **edited)
{
    return edit_request(source, "behavior", "GraphEditBehavior", NULL, 0, request, edited, NULL);
}

/* Observe executable results independently of generated capture names. */
static bool numeric_result(behavior_asset *asset, node *n, double expected)
{
    if (!asset || !n) {
        return false;
    }
    int output = find_pin(n, "Result", true);
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    if (output < 0 || !instance) {
        behavior_instance_destroy(instance);
        return false;
    }
    arena memory = { 0 };
    eval_context context;
    eval_context_init(&context);
    context.instance = instance;
    pin_value value = { 0 };
    bool result = !graph_eval_output(asset->graph, n, output, &memory, &value, &context) &&
                  ((value.type == PIN_F64 && value.d == expected) ||
                   (value.type == PIN_F32 && value.f == expected));
    eval_context_free(&context);
    arena_free(&memory);
    behavior_instance_destroy(instance);
    return result;
}

/* Find an expression's connected data capture without relying on translated names. */
static const edge *expression_wire(const graph *g, const node *n)
{
    for (int i = 0; n && i < n->input_count; i++) {
        if (n->input_pins[i].kind == PIN_DATA) {
            const edge *wire = edge_into(g, n, n->input_pins[i].name);
            if (wire) {
                return wire;
            }
        }
    }
    return NULL;
}

static bool sequence_result(behavior_asset *asset, int expected)
{
    if (!asset) {
        return false;
    }
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    if (!instance) {
        return false;
    }
    flow_error error = { 0 };
    flow_run *run = behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
    pin_value value = { 0 };
    bool result = run && flow_run_status(run) == FLOW_COMPLETED &&
                  !behavior_instance_get_variable(instance, asset->variables[0].id, &value) &&
                  value.type == PIN_I32 && value.i == expected;
    behavior_instance_destroy(instance);
    return result;
}

/* Socket order determines actual Sequence branch execution, regardless of labels. */
static void sequence_order(void)
{
    const char
        *source = "behavior SequenceOrder { state order:int=0; on Tick() { "
                  "sequence { branch { order=1; } branch { order=2; } } } }";
    behavior_asset *original = behavior_asset_load(source);
    node *sequence = original ? find_node_type(original->graph, "Sequence") : NULL;
    char request[256];
    snprintf(
        request,
        sizeof request,
        "{\"operations\":[{\"op\":\"set_pin\",\"node\":%llu,"
        "\"side\":\"output\",\"pin\":\"Then0\",\"order\":1}]}",
        (unsigned long long)(sequence ? sequence->id : 0)
    );
    char *edited = NULL;
    int status = edit_behavior(source, request, &edited);
    behavior_asset *changed = edited ? behavior_asset_load(edited) : NULL;
    test_check(
        status == CHIBI_AUTOMATION_OK && sequence_result(original, 2) &&
            sequence_result(changed, 1),
        "MCP reordering connected Sequence outputs preserves their branches "
        "and changes execution order"
    );
    behavior_asset_free(changed);
    behavior_asset_free(original);
    free(edited);
}

/* Two expressions with one source are two nodes with two bodies. */
static void duplicate_expressions(const char *source)
{
    char *edited = NULL;
    int status = edit_behavior(
        source,
        "{\"operations\":["
        "{\"op\":\"add_node\",\"ref\":\"first\",\"type\":\"Behavior "
        "Expression\",\"source\":\"x + 1\"},"
        "{\"op\":\"add_node\",\"ref\":\"second\",\"type\":\"Behavior "
        "Expression\",\"source\":\"x + 1\"},"
        "{\"op\":\"add_node\",\"ref\":\"two\",\"type\":\"Real\",\"inputs\":{"
        "\"Value\":2}},"
        "{\"op\":\"connect\",\"from\":\"two\",\"from_pin\":\"Value\",\"to\":"
        "\"second\",\"to_pin\":\"x\"}]}",
        &edited
    );
    behavior_asset *loaded = edited ? behavior_asset_load(edited) : NULL;
    node *first = loaded ? nth_node_type(loaded->graph, "Behavior Expression", 0)
                         : NULL;
    node *second = loaded ? nth_node_type(loaded->graph, "Behavior Expression", 1)
                          : NULL;
    graph *first_body = first ? behavior_asset_find_graph(loaded, first->reference_id)
                              : NULL;
    graph *second_body = second ? behavior_asset_find_graph(loaded, second->reference_id)
                                : NULL;
    test_check(
        status == CHIBI_AUTOMATION_OK && first_body && second_body &&
            first_body != second_body && expression_wire(loaded->graph, second) &&
            !expression_wire(loaded->graph, first) &&
            numeric_result(loaded, first, 1) && numeric_result(loaded, second, 3),
        "MCP graph edits add two Behavior Expressions with the same source"
    );
    code_source *parsed = edited ? code_source_parse(edited) : NULL;
    char *readable = second ? code_source_expression(parsed, loaded, second) : NULL;
    /* Edit the public expression returned by the shared code/graph service. */
    char *last = readable ? strrchr(readable, '1') : NULL;
    if (last) {
        *last = '3';
    }
    yyjson_mut_doc *request_doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *request_root = yyjson_mut_obj(request_doc);
    yyjson_mut_doc_set_root(request_doc, request_root);
    yyjson_mut_val *operations = yyjson_mut_obj_add_arr(request_doc, request_root, "operations");
    yyjson_mut_val *op = yyjson_mut_arr_add_obj(request_doc, operations);
    yyjson_mut_obj_add_str(request_doc, op, "op", "set_expression");
    yyjson_mut_obj_add_uint(request_doc, op, "node", second ? second->id : 0);
    yyjson_mut_obj_add_str(request_doc, op, "source", readable ? readable : "");
    char *request = yyjson_mut_write(request_doc, 0, NULL);
    char *changed = NULL;
    status = edited ? edit_behavior(edited, request, &changed)
                    : CHIBI_AUTOMATION_INVALID_REQUEST;
    behavior_asset *again = changed ? behavior_asset_load(changed) : NULL;
    node *kept = again ? nth_node_type(again->graph, "Behavior Expression", 0) : NULL;
    node *edited_call = again ? nth_node_type(again->graph, "Behavior Expression", 1)
                              : NULL;
    test_check(
        status == CHIBI_AUTOMATION_OK && kept && edited_call &&
            kept->id == first->id && edited_call->id == second->id &&
            expression_wire(again->graph, edited_call) &&
            numeric_result(again, kept, 1) && numeric_result(again, edited_call, 5),
        "editing one of two same-source Behavior Expressions leaves the other "
        "alone"
    );
    free(request);
    yyjson_mut_doc_free(request_doc);
    free(readable);
    code_source_free(parsed);
    behavior_asset_free(again);
    behavior_asset_free(loaded);
    free(changed);
    free(edited);
}

/* A bool wired into a number joins through a conversion, or fails naming both
 * pins; it is never dropped. */
static void bool_wires(const char *source)
{
    char *original = strdup(source);
    assert(original);
    char *edited = NULL;
    int status = edit_behavior(
        source,
        "{\"operations\":["
        "{\"op\":\"add_node\",\"ref\":\"flag\",\"type\":\"Equal\"},"
        "{\"op\":\"add_node\",\"ref\":\"sine\",\"type\":\"Sine\"},"
        "{\"op\":\"connect\",\"from\":\"flag\",\"from_pin\":\"Result\",\"to\":"
        "\"sine\",\"to_pin\":\"Value\"}]}",
        &edited
    );
    behavior_asset *loaded = edited ? behavior_asset_load(edited) : NULL;
    node *sine = loaded ? find_node_type(loaded->graph, "Sine") : NULL;
    const edge *wire = sine ? edge_into(loaded->graph, sine, "Value") : NULL;
    test_check(
        status == CHIBI_AUTOMATION_OK && wire && wire->conversion == &NODE_To_Real,
        "MCP connects a bool output to a number input through a conversion"
    );
    behavior_asset_free(loaded);
    free(edited);

    edited = NULL;
    status = edit_behavior(
        source,
        "{\"operations\":["
        "{\"op\":\"add_node\",\"ref\":\"flag\",\"type\":\"Equal\"},"
        "{\"op\":\"add_node\",\"ref\":\"scaled\",\"type\":\"Behavior "
        "Expression\",\"source\":\"x * 2\"},"
        "{\"op\":\"connect\",\"from\":\"flag\",\"from_pin\":\"Result\",\"to\":"
        "\"scaled\",\"to_pin\":\"x\"}]}",
        &edited
    );
    loaded = edited ? behavior_asset_load(edited) : NULL;
    node *scaled = loaded ? find_node_type(loaded->graph, "Behavior Expression") : NULL;
    node *flag = loaded ? find_node_type(loaded->graph, "Equal") : NULL;
    wire = scaled ? expression_wire(loaded->graph, scaled) : NULL;
    test_check(
        status == CHIBI_AUTOMATION_OK && wire && flag && wire->from_id == flag->id &&
            (wire->conversion == &NODE_To_Real ||
             body_has_conversion(loaded, scaled, &NODE_To_Real)) &&
            numeric_result(loaded, scaled, 2),
        "MCP keeps a bool wired into an expression that reads it as a number"
    );
    behavior_asset_free(loaded);
    free(edited);

    edited = NULL;
    status = edit_behavior(
        source,
        "{\"operations\":["
        "{\"op\":\"add_node\",\"ref\":\"text\",\"type\":\"String\"},"
        "{\"op\":\"add_node\",\"ref\":\"sine\",\"type\":\"Sine\"},"
        "{\"op\":\"connect\",\"from\":\"text\",\"from_pin\":\"Value\",\"to\":"
        "\"sine\",\"to_pin\":\"Value\"}]}",
        &edited
    );
    test_check(
        status != CHIBI_AUTOMATION_OK && !edited &&
            strstr(edit_error, "String.Value (const char *)") &&
            strstr(edit_error, "Sine.Value (double)"),
        "MCP names both pins when no conversion joins them"
    );
    status = edit_behavior(
        source,
        "{\"operations\":["
        "{\"op\":\"add_node\",\"ref\":\"text\",\"type\":\"String\"},"
        "{\"op\":\"add_node\",\"ref\":\"scaled\",\"type\":\"Behavior "
        "Expression\",\"source\":\"x * 2\"},"
        "{\"op\":\"connect\",\"from\":\"text\",\"from_pin\":\"Value\",\"to\":"
        "\"scaled\",\"to_pin\":\"x\"}]}",
        &edited
    );
    test_check(
        status != CHIBI_AUTOMATION_OK && !edited && !strcmp(source, original) &&
            strstr(edit_error, "rejected") &&
            strstr(edit_error, "String.Value (const char *)") &&
            strstr(edit_error, "Behavior Expression.x"),
        "MCP rejects invalid expression operands atomically and names both pins"
    );
    free(original);
}

static void variable_node_inputs(const char *source)
{
    char *edited = NULL;
    int status = edit_behavior(
        source,
        "{\"operations\":["
        "{\"op\":\"add_variable\",\"name\":\"hp\",\"type\":\"int\"},"
        "{\"op\":\"add_variable_node\",\"ref\":\"store\",\"kind\":\"set\","
        "\"variable\":\"hp\","
        "\"inputs\":{\"Value\":5}}]}",
        &edited
    );
    behavior_asset *loaded = edited ? behavior_asset_load(edited) : NULL;
    node *store = loaded ? find_node_type(loaded->graph, "Set Variable") : NULL;
    int value = store ? find_pin(store, "Value", false) : -1;
    test_check(
        status == CHIBI_AUTOMATION_OK && value >= 0 &&
            store->input_pins[value].default_value.i == 5,
        "MCP add_variable_node applies its inputs as add_node does"
    );
    behavior_asset_free(loaded);
    free(edited);
}

/* GameTransform aliases CF_V2, so its conversion fits a CF_V2 pair too. */
static void same_type_wires(const char *source)
{
    char *edited = NULL;
    int status = edit_behavior(
        source,
        "{\"operations\":["
        "{\"op\":\"add_node\",\"ref\":\"heading\",\"type\":\"Direction From "
        "Angle\"},"
        "{\"op\":\"add_node\",\"ref\":\"unit\",\"type\":\"Normalize Vector "
        "2\"},"
        "{\"op\":\"connect\",\"from\":\"heading\",\"from_pin\":0,\"to\":"
        "\"unit\",\"to_pin\":0}]}",
        &edited
    );
    behavior_asset *loaded = edited ? behavior_asset_load(edited) : NULL;
    node *unit = loaded ? find_node_type(loaded->graph, "Normalize Vector 2") : NULL;
    const edge *wire = unit ? edge_into(loaded->graph, unit, "Value") : NULL;
    test_check(
        status == CHIBI_AUTOMATION_OK && wire && !wire->conversion,
        "MCP connects pins of one type directly, with no conversion"
    );
    behavior_asset_free(loaded);
    free(edited);
}

/* A bool Index picks Option1 when true and Option0 when false. */
static void select_by_bool(const char *source)
{
    char *edited = NULL;
    int status = edit_behavior(
        source,
        "{\"operations\":["
        "{\"op\":\"add_node\",\"ref\":\"flag\",\"type\":\"Equal\"},"
        "{\"op\":\"add_node\",\"ref\":\"off\",\"type\":\"Real\",\"inputs\":{"
        "\"Value\":0.5}},"
        "{\"op\":\"add_node\",\"ref\":\"on\",\"type\":\"Real\",\"inputs\":{"
        "\"Value\":1.5}},"
        "{\"op\":\"add_node\",\"ref\":\"pick\",\"type\":\"Select\"},"
        "{\"op\":\"connect\",\"from\":\"off\",\"from_pin\":\"Value\",\"to\":"
        "\"pick\",\"to_pin\":\"Option0\"},"
        "{\"op\":\"connect\",\"from\":\"on\",\"from_pin\":\"Value\",\"to\":"
        "\"pick\",\"to_pin\":\"Option1\"},"
        "{\"op\":\"connect\",\"from\":\"flag\",\"from_pin\":\"Result\",\"to\":"
        "\"pick\",\"to_pin\":\"Index\"}]}",
        &edited
    );
    behavior_asset *loaded = edited ? behavior_asset_load(edited) : NULL;
    node *pick = loaded ? find_node_type(loaded->graph, "Select") : NULL;
    const edge *index = pick ? edge_into(loaded->graph, pick, "Index") : NULL;
    test_check(
        status == CHIBI_AUTOMATION_OK && index && index->conversion == &NODE_To_Integer &&
            edge_into(loaded->graph, pick, "Option0") &&
            edge_into(loaded->graph, pick, "Option1"),
        "MCP builds a Select that a bool picks between two values"
    );
    behavior_asset_free(loaded);
    free(edited);
}

/* The controller's answer to one edit_graph request on the project's Gates behavior. */
static yyjson_doc *edit_project(const char *root, const char *fields)
{
    char request[4096];
    snprintf(
        request,
        sizeof request,
        "{\"contract\":1,\"operation\":\"edit_graph\",\"project_root\":\"%s\","
        "\"kind\":\"behavior\","
        "\"name\":\"Gates\",%s}",
        root,
        fields
    );
    int exit_code = 0;
    char *response = chibi_automation_handle(request, ".", &exit_code);
    yyjson_doc *document = yyjson_read(response, strlen(response), 0);
    free(response);
    return document;
}

static bool edit_published(yyjson_doc *document)
{
    yyjson_val *response = yyjson_doc_get_root(document);
    return yyjson_get_bool(yyjson_obj_get(response, "ok")) &&
           yyjson_get_bool(yyjson_obj_get(yyjson_obj_get(response, "result"), "published"));
}

static void published_duplicate_expressions(void)
{
    char root[] = "/tmp/chibi-graph-edit-XXXXXX";
    assert(mkdtemp(root));
    char path[256];
    snprintf(path, sizeof path, "%s/project.kdl", root);
    FILE *file = fopen(path, "wb");
    assert(file);
    fprintf(
        file,
        "version %d\n"
        "content \"content\"\n",
        CHIBI_PROJECT_VERSION
    );
    assert(!fclose(file));
    snprintf(path, sizeof path, "%s/content", root);
    assert(!mkdir(path, 0755));
    snprintf(path, sizeof path, "%s/content/behaviors", root);
    assert(!mkdir(path, 0755));

    yyjson_doc *first = edit_project(
        root,
        "\"create\":true,\"operations\":[{\"op\":\"add_node\",\"ref\":\"a\","
        "\"type\":"
        "\"Behavior Expression\",\"source\":\"x + 1\"}]"
    );
    char again[512];
    snprintf(
        again,
        sizeof again,
        "\"expected_hash\":\"%s\",\"operations\":[{\"op\":\"add_node\",\"ref\":"
        "\"b\",\"type\":"
        "\"Behavior Expression\",\"source\":\"x + 1\"}]",
        yyjson_get_str(yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(first), "result"), "hash"))
    );
    yyjson_doc *second = edit_project(root, again);
    snprintf(path, sizeof path, "%s/content/behaviors/Gates.code", root);
    char *text = text_file_read_disk(path);
    behavior_asset *published = text ? behavior_asset_load(text) : NULL;
    int expressions = 0;
    for (int i = 0; published && i < acount(published->graph->nodes); i++) {
        node *n = published->graph->nodes[i];
        graph *function = behavior_asset_find_graph(published, n->reference_id);
        if (!strcmp(n->desc->name, "Behavior Expression") && function &&
            function->math_expression && numeric_result(published, n, 1)) {
            expressions++;
        }
    }
    test_check(edit_published(first) && edit_published(second) && expressions == 2, "MCP publishes an asset edited to hold two Behavior Expressions with one source");
    char inspect_request[512];
    snprintf(
        inspect_request,
        sizeof inspect_request,
        "{\"contract\":1,\"operation\":\"inspect_graph\",\"project_root\":\"%"
        "s\","
        "\"kind\":\"behavior\",\"name\":\"Gates\",\"graph_id\":0}",
        root
    );
    int inspect_exit = 0;
    char *inspection = chibi_automation_handle(inspect_request, ".", &inspect_exit);
    yyjson_doc *inspected = yyjson_read(inspection, strlen(inspection), 0);
    yyjson_val *inspected_result = yyjson_obj_get(yyjson_doc_get_root(inspected), "result");
    yyjson_val *inspected_graph = yyjson_obj_get(inspected_result, "graph");
    yyjson_val *nodes = yyjson_obj_get(inspected_graph, "nodes");
    size_t index, maximum;
    yyjson_val *item;
    int readable = 0;
    yyjson_arr_foreach(nodes, index, maximum, item)
    {
        const char *expression = yyjson_get_str(yyjson_obj_get(item, "expression"));
        readable += expression && expression[0] && !strstr(expression, "_code_input_");
    }
    test_check(!inspect_exit && readable == 2, "MCP graph inspection exposes readable expressions for subsequent source edits");
    yyjson_doc_free(inspected);
    free(inspection);
    behavior_asset_free(published);
    free(text);

    char stale[256];
    snprintf(stale, sizeof stale, "\"expected_hash\":\"0123456789abcdef\",\"operations\":[]");
    yyjson_doc *refused = edit_project(root, stale);
    yyjson_val *error = yyjson_obj_get(yyjson_doc_get_root(refused), "error");
    const char *message = yyjson_get_str(yyjson_obj_get(error, "message"));
    test_check(message && message[0], "a refused MCP graph edit carries its reason");
    yyjson_doc_free(refused);
    yyjson_doc_free(second);
    yyjson_doc_free(first);
    char command[512];
    snprintf(command, sizeof command, "rm -rf '%s'", root);
    assert(system(command) == 0);
}

/* A failed link reports its reason as text the response owns, so the
 * response still serializes once the check has returned. */
static void diagnostics_outlive_validation(const char *source)
{
    behavior_asset *unrelated = behavior_asset_new("Unrelated");
    const behavior_asset *libraries[] = { unrelated };
    char *edited = NULL;
    yyjson_mut_doc *response = NULL;
    int status = edit_request(
        source,
        "behavior",
        "GraphEditBehavior",
        libraries,
        1,
        "{\"operations\":["
        "{\"op\":\"add_node\",\"type\":\"Send "
        "Event\",\"reference_name\":\"NoSuchEvent\"}]}",
        &edited,
        &response
    );
    yyjson_mut_val *diagnostics = yyjson_mut_obj_get(yyjson_mut_doc_get_root(response), "diagnostics");
    yyjson_mut_val *first = diagnostics ? yyjson_mut_arr_get_first(diagnostics) : NULL;
    const char *message = first
                            ? yyjson_mut_get_str(yyjson_mut_obj_get(first, "message"))
                            : NULL;
    char *json = yyjson_mut_write(response, 0, NULL);
    test_check(
        status == CHIBI_AUTOMATION_VALIDATION_ERROR && message &&
            strstr(message, "missing event definition") &&
            strstr(edit_error, message) && json,
        "MCP link diagnostics hold their text after the check returns"
    );
    free(json);
    yyjson_mut_doc_free(response);
    free(edited);
    behavior_asset_free(unrelated);
}

static int asset_node_count(const behavior_asset *asset, const char *type)
{
    int count = 0;
    for (int i = 0; asset && i < acount(asset->graphs); i++) {
        graph *g = asset->graphs[i];
        for (int j = 0; j < acount(g->nodes); j++) {
            count += !strcmp(g->nodes[j]->desc->name, type);
        }
    }
    return count;
}

void test_automation_graph_edit(void)
{
    behavior_asset *empty = behavior_asset_new("GraphEditBehavior");
    char *source = behavior_asset_save(empty);
    behavior_asset_free(empty);
    char *edited = NULL;
    yyjson_mut_doc *response = NULL;
    int status = edit_request(
        source,
        "behavior",
        "GraphEditBehavior",
        NULL,
        0,
        "{\"operations\":[{\"op\":\"add_node\",\"ref\":\"value\",\"type\":"
        "\"Integer\",\"inputs\":{\"Value\":7}}]}",
        &edited,
        &response
    );
    behavior_asset *added = edited ? behavior_asset_load(edited) : NULL;
    bool behavior_ok = status == CHIBI_AUTOMATION_OK && added &&
                       asset_node_count(added, "Integer") == 1 &&
                       yyjson_mut_obj_get(yyjson_mut_doc_get_root(response), "nodes");
    test_check(behavior_ok, "MCP graph edits add typed behavior nodes and return refs");
    behavior_asset_free(added);
    yyjson_mut_doc_free(response);
    free(edited);

    /* Studio's code actions as operations: collapse a selection into a
     * function, expand its call back, convert math to an expression, promote
     * a pin to a variable, and name a value without choosing the name. */
    static const char base
        [] = "{\"op\":\"add_node\",\"ref\":\"two\",\"type\":\"Real\","
             "\"inputs\":{\"Value\":2}},"
             "{\"op\":\"add_node\",\"ref\":\"double\",\"type\":\"Multiply\"},"
             "{\"op\":\"connect\",\"from\":\"two\",\"from_pin\":\"Value\","
             "\"to\":\"double\",\"to_pin\":\"A\"},"
             "{\"op\":\"set_input\",\"node\":\"double\",\"pin\":\"B\","
             "\"value\":3},"
             "{\"op\":\"add_node\",\"ref\":\"plus\",\"type\":\"Add\"},"
             "{\"op\":\"connect\",\"from\":\"double\",\"from_pin\":\"Result\","
             "\"to\":\"plus\",\"to_pin\":\"A\"}";
    const char *actions[] = {
        "{\"op\":\"collapse_to_function\",\"nodes\":[\"double\"],\"name\":"
        "\"Double\",\"ref\":\"fn\"}",
        "{\"op\":\"collapse_to_function\",\"nodes\":[\"double\"],\"name\":"
        "\"Double\",\"ref\":\"fn\"},"
        "{\"op\":\"expand_node\",\"node\":\"fn\"}",
        "{\"op\":\"convert_to_expression\",\"nodes\":[\"double\",\"plus\"],"
        "\"ref\":\"expr\"}",
        "{\"op\":\"add_node\",\"ref\":\"gate\",\"type\":\"Branch\"},"
        "{\"op\":\"promote_to_variable\",\"node\":\"gate\",\"pin\":"
        "\"Condition\",\"ref\":\"flag\"}",
        "{\"op\":\"create_local\",\"node\":\"two\",\"pin\":\"Value\"}",
        "{\"op\":\"create_local\",\"node\":\"two\",\"pin\":\"Value\"},"
        "{\"op\":\"expand_local\",\"node\":\"two\",\"pin\":\"Value\"}",
    };
    char *results[6] = { 0 };
    int statuses[6];
    for (int i = 0; i < 6; i++) {
        char request[2048];
        snprintf(request, sizeof request, "{\"operations\":[%s,%s]}", base, actions[i]);
        statuses[i] = edit_request(source, "behavior", "GraphEditBehavior", NULL, 0, request, &results[i], NULL);
    }
    behavior_asset *models[6] = { 0 };
    for (int i = 0; i < 6; i++) {
        models[i] = results[i] ? behavior_asset_load(results[i]) : NULL;
    }
    bool actions_ok = true;
    for (int i = 0; i < 6; i++) {
        actions_ok &= statuses[i] == CHIBI_AUTOMATION_OK && models[i];
    }
    test_check(
        actions_ok && behavior_asset_find_graph_by_name(models[0], "Double") &&
            asset_node_count(models[0], "Call Pure Function") == 1 &&
            asset_node_count(models[1], "Multiply") == 2 &&
            asset_node_count(models[1], "Call Pure Function") == 0 &&
            asset_node_count(models[2], "Behavior Expression") == 1 &&
            behavior_asset_find_variable_by_name(models[3], "Condition") &&
            asset_node_count(models[3], "Get Variable") > 0 &&
            acount(models[4]->graph->locals) > 0 &&
            acount(models[5]->graph->locals) == 0,
        "MCP graph edits collapse, expand, convert, promote and name values "
        "like Studio"
    );
    for (int i = 0; i < 6; i++) {
        behavior_asset_free(models[i]);
    }
    for (int i = 0; i < 6; i++) {
        free(results[i]);
    }

    /* Editing a function's Entry or Return pins edits its signature, so
     * calls to it follow the rename and gain the new input and its default. */
    edited = NULL;
    status = edit_request(
        source,
        "behavior",
        "GraphEditBehavior",
        NULL,
        0,
        "{\"operations\":["
        "{\"op\":\"add_graph\",\"name\":\"Math\",\"kind\":\"function\","
        "\"pure\":true,\"ref\":\"math\"},"
        "{\"op\":\"add_pin\",\"node\":\"math.entry\",\"side\":\"output\","
        "\"name\":\"Value\",\"type\":\"float\"},"
        "{\"op\":\"add_pin\",\"node\":\"math.return\",\"side\":\"input\","
        "\"name\":\"Result\",\"type\":\"float\"},"
        "{\"op\":\"connect\",\"from\":\"math.entry\",\"from_pin\":\"Value\","
        "\"to\":\"math.return\",\"to_pin\":\"Result\"},"
        "{\"op\":\"select_graph\",\"graph\":0},"
        "{\"op\":\"add_node\",\"ref\":\"call\",\"type\":\"Call Pure "
        "Function\",\"reference_name\":\"Math\"},"
        "{\"op\":\"select_graph\",\"graph\":\"math\"},"
        "{\"op\":\"set_pin\",\"node\":\"math.entry\",\"side\":\"output\","
        "\"pin\":\"Value\",\"name\":\"Amount\"},"
        "{\"op\":\"add_pin\",\"node\":\"math.entry\",\"side\":\"output\","
        "\"name\":\"Scale\",\"type\":\"float\",\"default\":2}]}",
        &edited,
        NULL
    );
    behavior_asset *signature = edited ? behavior_asset_load(edited) : NULL;
    node *call = signature ? find_node_type(signature->graph, "Call Pure Function")
                           : NULL;
    int scale = call ? find_pin(call, "Scale", false) : -1;
    test_check(
        status == CHIBI_AUTOMATION_OK && call && find_pin(call, "Amount", false) >= 0 &&
            find_pin(call, "Value", false) < 0 && scale >= 0 &&
            call->input_pins[scale].default_value.f == 2.0f,
        "MCP signature pin edits carry to the function's calls"
    );
    behavior_asset_free(signature);
    free(edited);

    /* set_pin.order moves a signature pin, and each call's pins follow with
     * their wires. Connected Sequence ordering is verified separately at runtime. */
    static const char reorder
        [] = "{\"op\":\"add_graph\",\"name\":\"Math\",\"kind\":\"function\","
             "\"pure\":true,\"ref\":\"math\"},"
             "{\"op\":\"add_pin\",\"node\":\"math.entry\",\"side\":\"output\","
             "\"name\":\"Value\",\"type\":\"float\"},"
             "{\"op\":\"add_pin\",\"node\":\"math.entry\",\"side\":\"output\","
             "\"name\":\"Scale\",\"type\":\"float\"},"
             "{\"op\":\"select_graph\",\"graph\":0},"
             "{\"op\":\"add_node\",\"ref\":\"call\",\"type\":\"Call Pure "
             "Function\",\"reference_name\":\"Math\"},"
             "{\"op\":\"add_node\",\"ref\":\"two\",\"type\":\"Real\"},"
             "{\"op\":\"connect\",\"from\":\"two\",\"from_pin\":\"Value\","
             "\"to\":\"call\",\"to_pin\":\"Value\"},"
             "{\"op\":\"add_node\",\"ref\":\"seq\",\"type\":\"Sequence\"},"
             "{\"op\":\"set_pin\",\"node\":\"seq\",\"side\":\"output\","
             "\"pin\":\"Then0\",\"order\":1},"
             "{\"op\":\"select_graph\",\"graph\":\"math\"},"
             "{\"op\":\"set_pin\",\"node\":\"math.entry\",\"side\":\"output\","
             "\"pin\":\"Scale\",\"order\":%d}";
    char operations[2048], request[2048];
    edited = NULL;
    snprintf(operations, sizeof operations, reorder, 0);
    snprintf(request, sizeof request, "{\"operations\":[%s]}", operations);
    status = edit_request(source, "behavior", "GraphEditBehavior", NULL, 0, request, &edited, NULL);
    behavior_asset *moved = edited ? behavior_asset_load(edited) : NULL;
    call = moved ? find_node_type(moved->graph, "Call Pure Function") : NULL;
    node *two = NULL;
    int two_pin = -1;
    test_check(
        status == CHIBI_AUTOMATION_OK && call && find_pin(call, "Scale", false) == 0 &&
            graph_input_source(moved->graph, call, 1, &two, &two_pin) &&
            !strcmp(two->desc->name, "Real"),
        "MCP set_pin.order reorders a signature and its calls keep their wires"
    );
    behavior_asset_free(moved);
    free(edited);
    edited = NULL;
    snprintf(operations, sizeof operations, reorder, 2);
    snprintf(request, sizeof request, "{\"operations\":[%s]}", operations);
    status = edit_request(source, "behavior", "GraphEditBehavior", NULL, 0, request, &edited, NULL);
    test_check(
        status != CHIBI_AUTOMATION_OK && !edited && strstr(edit_error, "set_pin.order"),
        "MCP refuses to move a signature pin past the pins it declares"
    );
    free(edited);

    /* A setting shapes the other pins, so it may follow them in inputs. */
    edited = NULL;
    status = edit_request(
        source,
        "behavior",
        "GraphEditBehavior",
        NULL,
        0,
        "{\"operations\":[{\"op\":\"add_node\",\"type\":\"Make Struct\","
        "\"inputs\":{\"current\":5,\"maximum\":9,\"Type\":\"GameHealth\"}},"
        "{\"op\":\"add_node\",\"type\":\"Set Component\","
        "\"inputs\":{\"current\":4,\"Component\":\"GameHealth\"}}]}",
        &edited,
        NULL
    );
    behavior_asset *settings = edited ? behavior_asset_load(edited) : NULL;
    node *made = settings ? find_node_type(settings->graph, "Make Struct") : NULL;
    node *set = settings ? find_node_type(settings->graph, "Set Component") : NULL;
    int maximum = made ? find_pin(made, "maximum", false) : -1;
    int current = set ? find_pin(set, "current", false) : -1;
    test_check(
        status == CHIBI_AUTOMATION_OK && maximum >= 0 &&
            made->input_pins[maximum].default_value.i == 9 && current >= 0 &&
            set->input_pins[current].default_value.i == 4,
        "MCP add_node applies a setting before the pins it shapes"
    );
    behavior_asset_free(settings);
    free(edited);

    edited = NULL;
    status = edit_request(
        source,
        "behavior",
        "GraphEditBehavior",
        NULL,
        0,
        "{\"operations\":[{\"op\":\"add_node\",\"type\":\"Make Struct\","
        "\"inputs\":{\"Type\":\"GameHealth\",\"curent\":5}}]}",
        &edited,
        NULL
    );
    test_check(
        status != CHIBI_AUTOMATION_OK &&
            strstr(edit_error, "unknown input pin 'curent'; inputs: current, maximum, Type"),
        "MCP names a node's pins when a pin name is unknown"
    );
    free(edited);

    edited = NULL;
    status = edit_request(
        source,
        "behavior",
        "GraphEditBehavior",
        NULL,
        0,
        "{\"operations\":[{\"op\":\"add_node\",\"ref\":\"compare\",\"type\":"
        "\"Equal\"},"
        "{\"op\":\"set_pin\",\"node\":\"compare\",\"pin\":\"A\",\"type\":"
        "\"uint\"}]}",
        &edited,
        NULL
    );
    test_check(status != CHIBI_AUTOMATION_OK && !edited, "MCP rejects retyping a fixed native pin before a runtime assertion");
    free(edited);

    edited = NULL;
    response = NULL;
    status = edit_request(
        source,
        "behavior",
        "GraphEditBehavior",
        NULL,
        0,
        "{\"operations\":[{\"op\":\"add_node\",\"ref\":\"expression\","
        "\"type\":\"Behavior Expression\",\"source\":\"x >= 0\"},"
        "{\"op\":\"set_expression\",\"node\":\"expression\","
        "\"source\":\"x < 10\"}]}",
        &edited,
        &response
    );
    behavior_asset *expression_result = edited ? behavior_asset_load(edited) : NULL;
    node *expression = expression_result
                         ? find_node_type(expression_result->graph, "Behavior Expression")
                         : NULL;
    graph *expression_graph = expression_result && expression
                                ? behavior_asset_find_graph(
                                      expression_result,
                                      expression->reference_id
                                  )
                                : NULL;
    bool expression_edit = status == CHIBI_AUTOMATION_OK && expression_graph &&
                           expression_graph->expression_mode == GRAPH_EXPRESSION_BRANCH &&
                           find_pin(expression, "Exec", false) >= 0 &&
                           find_pin(expression, "True", true) >= 0 &&
                           find_pin(expression, "False", true) >= 0 &&
                           find_pin(expression, "Result", true) >= 0;
    test_check(expression_edit, "MCP graph edits add and update Behavior Expression nodes");
    behavior_asset_free(expression_result);
    yyjson_mut_doc_free(response);
    free(edited);

    edited = NULL;
    status = edit_request(
        source,
        "behavior",
        "GraphEditBehavior",
        NULL,
        0,
        "{\"operations\":[{\"op\":\"add_node\",\"ref\":\"value\",\"type\":"
        "\"Integer\",\"inputs\":{\"Value\":7}},"
        "{\"op\":\"add_node\",\"ref\":\"first\",\"type\":\"Add\"},"
        "{\"op\":\"add_node\",\"ref\":\"second\",\"type\":\"Add\"},"
        "{\"op\":\"connect\",\"from\":\"value\",\"from_pin\":\"Value\","
        "\"to\":\"first\",\"to_pin\":\"A\"},"
        "{\"op\":\"create_local\",\"node\":\"value\",\"pin\":\"Value\","
        "\"name\":\"Seven\"},"
        "{\"op\":\"connect\",\"from\":\"value\",\"from_pin\":\"Value\","
        "\"to\":\"second\",\"to_pin\":\"A\"}]}",
        &edited,
        NULL
    );
    behavior_asset *named = edited ? behavior_asset_load(edited) : NULL;
    int reads = 0;
    for (int i = 0; named && i < acount(named->graph->nodes); i++) {
        const pin_binding *b = named->graph->nodes[i]->input_count
                                 ? named->graph->nodes[i]->input_pins[0].binding
                                 : NULL;
        reads += b && b->kind == PIN_BINDING_LOCAL;
    }
    test_check(status == CHIBI_AUTOMATION_OK && reads == 2, "MCP connections from a named output read it by name");
    behavior_asset_free(named);
    free(edited);

    edited = NULL;
    response = NULL;
    status = edit_request(
        source,
        "behavior",
        "GraphEditBehavior",
        NULL,
        0,
        "{\"description\":\"Enemy movement graph\","
        "\"owner_type\":\"ecs.entity\","
        "\"graph_description\":\"Runs once per fixed frame\","
        "\"operations\":[]}",
        &edited,
        &response
    );
    behavior_asset *metadata = edited ? behavior_asset_load(edited) : NULL;
    bool metadata_ok = status == CHIBI_AUTOMATION_OK && metadata &&
                       !strcmp(metadata->description, "Enemy movement graph") &&
                       !strcmp(metadata->owner_type_name, "ecs.entity") &&
                       !strcmp(metadata->graph->description, "Runs once per fixed frame");
    test_check(metadata_ok, "MCP graph edits preserve asset and selected graph metadata");
    behavior_asset_free(metadata);
    yyjson_mut_doc_free(response);
    free(edited);

    edited = NULL;
    response = NULL;
    status = edit_request(
        NULL,
        "material",
        "InvalidMaterial",
        NULL,
        0,
        "{\"graph_name\":\"missing\",\"operations\":[]}",
        &edited,
        &response
    );
    bool invalid_new_material = status == CHIBI_AUTOMATION_INVALID_REQUEST &&
                                !edited &&
                                yyjson_mut_obj_get(yyjson_mut_doc_get_root(response), "failed_operation");
    test_check(invalid_new_material, "MCP new asset failures clean up the initialized shader output");
    yyjson_mut_doc_free(response);

    edited = NULL;
    response = NULL;
    status = edit_request(source, "material", "GraphEditBehavior", NULL, 0, "{\"operations\":[]}", &edited, &response);
    bool domain_mismatch = status == CHIBI_AUTOMATION_INVALID_REQUEST && !edited;
    test_check(domain_mismatch, "MCP graph edits reject a source asset from another family");
    yyjson_mut_doc_free(response);

    edited = NULL;
    response = NULL;
    status = edit_request(
        source,
        "behavior",
        "GraphEditBehavior",
        NULL,
        0,
        "{\"operations\":[{\"op\":\"add_node\",\"ref\":\"first\","
        "\"type\":\"Integer\"},{\"op\":\"add_node\",\"ref\":\"second\","
        "\"type\":\"Integer\"},{\"op\":\"connect\",\"from\":\"first\","
        "\"from_pin\":\"Value\",\"to\":\"second\",\"to_pin\":\"Value\"},"
        "{\"op\":\"set_node\",\"node\":\"first\",\"presentation\":\"expanded\"}"
        "]}",
        &edited,
        &response
    );
    behavior_asset *arranged_asset = edited ? behavior_asset_load(edited) : NULL;
    node *arranged_first = NULL;
    node *arranged_second = NULL;
    if (arranged_asset) {
        for (int i = 0; i < acount(arranged_asset->graph->nodes); i++) {
            node *candidate = arranged_asset->graph->nodes[i];
            if (strcmp(candidate->desc->name, "Integer")) {
                continue;
            }
            if (!arranged_first) {
                arranged_first = candidate;
            } else {
                arranged_second = candidate;
                break;
            }
        }
    }
    bool layout_enabled = status == CHIBI_AUTOMATION_OK && arranged_first &&
                          arranged_second &&
                          (arranged_first->x != arranged_second->x ||
                           arranged_first->y != arranged_second->y);
    test_check(layout_enabled, "MCP graph edits arrange authored nodes by default");
    if (layout_enabled) {
        automatic_layout_on_disk(edited);
    }
    behavior_asset_free(arranged_asset);
    yyjson_mut_doc_free(response);
    free(edited);

    edited = NULL;
    response = NULL;
    status = edit_request(
        source,
        "behavior",
        "GraphEditBehavior",
        NULL,
        0,
        "{\"operations\":[{\"op\":\"add_node\",\"ref\":\"note\",\"type\":"
        "\"Integer\"},"
        "{\"op\":\"set_comment\",\"node\":\"note\",\"comment\":\"Starting "
        "health\"},"
        "{\"op\":\"set_comment\",\"node\":\"note\",\"hidden\":true}]}",
        &edited,
        &response
    );
    behavior_asset *commented = edited ? behavior_asset_load(edited) : NULL;
    node *note = commented ? find_node_type(commented->graph, "Integer") : NULL;
    test_check(
        status == CHIBI_AUTOMATION_OK && note && note->comment &&
            !strcmp(note->comment, "Starting health") && note->comment_hidden,
        "MCP set_comment writes a node comment and hides it separately"
    );
    behavior_asset_free(commented);
    yyjson_mut_doc_free(response);
    free(edited);

    edited = NULL;
    response = NULL;
    status = edit_request(
        source,
        "behavior",
        "GraphEditBehavior",
        NULL,
        0,
        "{\"operations\":[{\"op\":\"add_node\",\"ref\":\"note\",\"type\":"
        "\"Integer\"},"
        "{\"op\":\"set_comment\",\"node\":\"note\"}]}",
        &edited,
        &response
    );
    test_check(status != CHIBI_AUTOMATION_OK && !edited, "MCP set_comment needs a comment or hidden");
    yyjson_mut_doc_free(response);
    free(edited);

    behavior_asset *function_asset = behavior_asset_new("FunctionVariables");
    behavior_asset_set_owner_type(function_asset, "ecs.entity");
    behavior_asset_add_variable(function_asset, "items", PIN_F32, NULL, PIN_CONTAINER_ARRAY, NULL, "Test");
    behavior_asset_add_function(function_asset, "Work", false);
    char *function_source = behavior_asset_save(function_asset);
    behavior_asset_free(function_asset);
    edited = NULL;
    response = NULL;
    status = edit_request(
        function_source,
        "behavior",
        "FunctionVariables",
        NULL,
        0,
        "{\"graph_name\":\"Work\",\"operations\":["
        "{\"op\":\"add_variable_node\",\"ref\":\"items_get\",\"kind\":\"get\","
        "\"variable\":1},"
        "{\"op\":\"add_self_node\",\"ref\":\"owner\"}]}",
        &edited,
        &response
    );
    behavior_asset *function_result = edited ? behavior_asset_load(edited) : NULL;
    graph *work = function_result
                    ? behavior_asset_find_graph_by_name(function_result, "Work")
                    : NULL;
    node *get = work ? find_node_type(work, "Get Variable") : NULL;
    node *self = work ? find_node_type(work, "Self") : NULL;
    bool function_nodes = status == CHIBI_AUTOMATION_OK && get && self &&
                          get->reference_id == 1 &&
                          get->output_pins[0].container == PIN_CONTAINER_ARRAY &&
                          self->output_pins[0].type == PIN_HANDLE &&
                          self->output_pins[0].type_name &&
                          !strcmp(self->output_pins[0].type_name, "ecs.entity");
    test_check(function_nodes, "MCP variable and Self nodes target the selected function graph");
    behavior_asset_free(function_result);
    yyjson_mut_doc_free(response);
    free(function_source);
    free(edited);

    char variable_request[4096] = "{\"operations\":[";
    for (int i = 0; i < 20; i++) {
        char operation[192];
        snprintf(
            operation,
            sizeof operation,
            "%s{\"op\":\"add_variable\",\"name\":\"v%d\",\"type\":\"float\"%s}",
            i ? "," : "",
            i,
            i ? "" : ",\"ref\":\"first\""
        );
        strcat(variable_request, operation);
    }
    strcat(variable_request, ",{\"op\":\"set_variable\",\"variable\":\"first\",\"name\":\"renamed\"}]}");
    edited = NULL;
    response = NULL;
    status = edit_request(source, "behavior", "GraphEditBehavior", NULL, 0, variable_request, &edited, &response);
    behavior_asset *variable_result = edited ? behavior_asset_load(edited) : NULL;
    bool variable_refs = status == CHIBI_AUTOMATION_OK && variable_result &&
                         behavior_asset_find_variable_by_name(variable_result, "renamed");
    test_check(variable_refs, "MCP variable refs survive variable-array growth");
    behavior_asset_free(variable_result);
    yyjson_mut_doc_free(response);
    free(edited);

    edited = NULL;
    response = NULL;
    status = edit_request(
        NULL,
        "material",
        "GraphEditMaterial",
        NULL,
        0,
        "{\"operations\":["
        "{\"op\":\"add_node\",\"ref\":\"color\",\"type\":\"Shader Vector3\","
        "\"inputs\":{\"X\":1,\"Y\":0.25,\"Z\":0}},"
        "{\"op\":\"connect\",\"from\":\"color\",\"from_pin\":\"Value\","
        "\"to\":\"output\",\"to_pin\":\"Base Color\",\"kind\":\"data\"},"
        "{\"op\":\"add_node\",\"ref\":\"ramp\",\"type\":\"Shader Color Ramp\"},"
        "{\"op\":\"set_ramp\",\"node\":\"ramp\",\"stops\":["
        "{\"position\":0,\"color\":[0,0,0,1]},"
        "{\"position\":1,\"color\":[1,0.5,0,1]}]}]}",
        &edited,
        &response
    );
    bool shader_ok = status == CHIBI_AUTOMATION_OK && edited &&
                     strstr(edited, "GraphEditMaterial") &&
                     strstr(edited, "Shader Color Ramp");
    test_check(shader_ok, "MCP graph edits initialize, connect, and configure shader nodes");
    yyjson_mut_doc_free(response);
    free(edited);

    edited = NULL;
    response = NULL;
    status = edit_request(
        NULL,
        "material",
        "GraphEditConstantMaterial",
        NULL,
        0,
        "{\"operations\":["
        "{\"op\":\"add_node\",\"ref\":\"constant\",\"type\":\"Shader "
        "Constant\"},"
        "{\"op\":\"set_output\",\"node\":\"constant\",\"pin\":\"Value\","
        "\"type\":\"ShaderVec3\",\"value\":{\"x\":0.1,\"y\":0.2,\"z\":0.3}},"
        "{\"op\":\"connect\",\"from\":\"constant\",\"from_pin\":\"Value\","
        "\"to\":\"output\",\"to_pin\":\"Base Color\",\"kind\":\"data\"}]}",
        &edited,
        &response
    );
    behavior_asset *constant_material = edited ? behavior_asset_load(edited) : NULL;
    node *constant = constant_material
                       ? find_node_type(constant_material->graph, "Shader Constant")
                       : NULL;
    bool constant_ok = status == CHIBI_AUTOMATION_OK && constant &&
                       constant->output_pins[0].type == PIN_STRUCT &&
                       constant->output_pins[0].type_name &&
                       !strcmp(constant->output_pins[0].type_name, "ShaderVec3");
    test_check(constant_ok, "MCP shader edits set typed values on constant outputs");
    behavior_asset_free(constant_material);
    yyjson_mut_doc_free(response);
    free(edited);

    edited = NULL;
    response = NULL;
    status = edit_request(
        NULL,
        "material",
        "GraphEditTypedWildcardMaterial",
        NULL,
        0,
        "{\"operations\":["
        "{\"op\":\"add_node\",\"ref\":\"value\",\"type\":\"Shader Parameter\"},"
        "{\"op\":\"add_node\",\"ref\":\"multiply\",\"type\":\"Shader "
        "Multiply\"},"
        "{\"op\":\"connect\",\"from\":\"value\",\"from_pin\":\"Value\","
        "\"to\":\"multiply\",\"to_pin\":\"A\",\"type\":\"ShaderVec3\",\"kind\":"
        "\"data\"},"
        "{\"op\":\"connect\",\"from\":\"multiply\",\"from_pin\":\"Value\","
        "\"to\":\"output\",\"to_pin\":\"Base Color\",\"kind\":\"data\"}]}",
        &edited,
        &response
    );
    behavior_asset *typed_wildcard = edited ? behavior_asset_load(edited) : NULL;
    node *typed_value = typed_wildcard
                          ? find_node_type(typed_wildcard->graph, "Shader Parameter")
                          : NULL;
    node *typed_multiply = typed_wildcard
                             ? find_node_type(typed_wildcard->graph, "Shader Multiply")
                             : NULL;
    bool typed_wildcard_ok = status == CHIBI_AUTOMATION_OK && typed_value &&
                             typed_multiply &&
                             typed_value->output_pins[0].type == PIN_STRUCT &&
                             typed_value->output_pins[0].type_name &&
                             !strcmp(typed_value->output_pins[0].type_name, "ShaderVec3") &&
                             typed_multiply->input_pins[0].type == PIN_STRUCT &&
                             typed_multiply->input_pins[0].type_name &&
                             !strcmp(typed_multiply->input_pins[0].type_name, "ShaderVec3");
    test_check(typed_wildcard_ok, "MCP shader connections accept explicit wildcard types");
    behavior_asset_free(typed_wildcard);
    yyjson_mut_doc_free(response);
    free(edited);

    edited = NULL;
    response = NULL;
    status = edit_request(
        NULL,
        "shape",
        "GraphEditShape",
        NULL,
        0,
        "{\"operations\":[{\"op\":\"set_bounds\",\"value\":[24,18]}]}",
        &edited,
        &response
    );
    behavior_asset *shape_asset = edited ? behavior_asset_load(edited) : NULL;
    bool shape_ok = status == CHIBI_AUTOMATION_OK && shape_asset &&
                    shape_asset->bounds[0] == 24 && shape_asset->bounds[1] == 18;
    test_check(shape_ok, "MCP shape creation authors valid half extents");
    behavior_asset_free(shape_asset);
    yyjson_mut_doc_free(response);
    free(edited);

    char *library_json = NULL;
    response = NULL;
    status = edit_request(
        NULL,
        "shader-library",
        "ShaderLib",
        NULL,
        0,
        "{\"operations\":[{\"op\":\"add_graph\",\"ref\":\"math\","
        "\"name\":\"Math\",\"kind\":\"function\",\"pure\":true},"
        "{\"op\":\"add_pin\",\"node\":\"math.entry\",\"side\":\"output\","
        "\"name\":\"Value\",\"kind\":\"data\",\"type\":\"float\"},"
        "{\"op\":\"add_pin\",\"node\":\"math.return\",\"side\":\"input\","
        "\"name\":\"Result\",\"kind\":\"data\",\"type\":\"float\"},"
        "{\"op\":\"connect\",\"from\":\"math.entry\",\"from_pin\":\"Value\","
        "\"to\":\"math.return\",\"to_pin\":\"Result\",\"kind\":\"data\"}]}",
        &library_json,
        &response
    );
    bool library_ok = status == CHIBI_AUTOMATION_OK && library_json &&
                      strstr(library_json, "domain \"shader-library\"") &&
                      strstr(library_json, "name \"Math\"") &&
                      result_has_node_ref(response, "math.entry") &&
                      result_has_node_ref(response, "math.return");
    test_check(library_ok, "MCP graph edits create a valid shader function library");
    yyjson_mut_doc_free(response);

    behavior_asset *library = library_json ? behavior_asset_load(library_json) : NULL;
    graph *math = library ? behavior_asset_find_graph_by_name(library, "Math") : NULL;
    char invalid_library_request[1024];
    snprintf(
        invalid_library_request,
        sizeof invalid_library_request,
        "{\"graph_name\":\"Math\",\"operations\":["
        "{\"op\":\"add_node\",\"ref\":\"gradient\","
        "\"type\":\"Shader Gradient\"},"
        "{\"op\":\"connect\",\"from\":\"gradient\","
        "\"from_pin\":\"Value\",\"to\":%llu,\"to_pin\":\"Result\","
        "\"kind\":\"data\"},"
        "{\"op\":\"set_input\",\"node\":\"gradient\","
        "\"pin\":\"Type\",\"value\":99}]}\n",
        (unsigned long long)(math ? math->return_id : 0)
    );
    edited = NULL;
    response = NULL;
    status = library ? edit_request(
                           library_json,
                           "shader-library",
                           "ShaderLib",
                           NULL,
                           0,
                           invalid_library_request,
                           &edited,
                           &response
                       )
                     : CHIBI_AUTOMATION_VALIDATION_ERROR;
    bool invalid_library_setting = status == CHIBI_AUTOMATION_VALIDATION_ERROR &&
                                   !edited;
    test_check(invalid_library_setting, "MCP shader libraries reject invalid shader settings before publication");
    yyjson_mut_doc_free(response);
    free(edited);

    const behavior_asset *libraries[] = { library };
    edited = NULL;
    response = NULL;
    status = library ? edit_request(
                           NULL,
                           "material",
                           "LibraryConsumer",
                           libraries,
                           1,
                           "{\"operations\":[{\"op\":\"add_node\",\"ref\":"
                           "\"math_call\","
                           "\"type\":\"Call Pure Function\",\"reference_id\":1,"
                           "\"reference_name\":\"Math\",\"reference_asset\":"
                           "\"ShaderLib\"}]}",
                           &edited,
                           &response
                       )
                     : CHIBI_AUTOMATION_VALIDATION_ERROR;
    bool library_call_ok = status == CHIBI_AUTOMATION_OK && edited &&
                           strstr(edited, "LibraryConsumer");
    test_check(library_call_ok, "MCP shader consumers validate linked library calls before save");
    yyjson_mut_doc_free(response);
    free(edited);
    behavior_asset_free(library);
    free(library_json);

    edited = NULL;
    response = NULL;
    status = edit_request(
        source,
        "behavior",
        "GraphEditBehavior",
        NULL,
        0,
        "{\"operations\":[{\"op\":\"add_node\",\"ref\":\"value\",\"type\":"
        "\"Integer\"},"
        "{\"op\":\"connect\",\"from\":\"value\",\"from_pin\":\"Value\","
        "\"to\":999999,\"to_pin\":0,\"kind\":\"data\"}]}",
        &edited,
        &response
    );
    bool rollback_ok = status == CHIBI_AUTOMATION_INVALID_REQUEST && !edited &&
                       yyjson_mut_obj_get(yyjson_mut_doc_get_root(response), "failed_operation");
    test_check(rollback_ok, "MCP graph edit batches reject invalid operations atomically");
    yyjson_mut_doc_free(response);

    sequence_order();
    duplicate_expressions(source);
    bool_wires(source);
    variable_node_inputs(source);
    same_type_wires(source);
    select_by_bool(source);
    published_duplicate_expressions();
    diagnostics_outlive_validation(source);
    free(source);
}
