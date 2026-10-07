#include "assets/code_print.h"

#include "assets/code_program_reconcile.h"
#include "assets/code_source.h"
#include "assets/code_value.h"
#include "graph/document/events.h"
#include "graph/document/graph_signature.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "graph/model/local.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/component_nodes.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/instance.h"
#include "runtime/generated.h"
#include "support/array.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_check(int ok, const char *message);

static behavior_asset *native_behavior(const char *name)
{
    behavior_asset *asset = behavior_asset_new(name);
    test_check(
        !behavior_asset_set_owner_type(asset, "ecs.entity"),
        "Native behavior fixtures use the language's implicit entity owner"
    );
    return asset;
}

static void dump_models(
    const char *name,
    const behavior_asset *expected,
    const behavior_asset *actual,
    const char *source
)
{
    fprintf(stderr, "%s source:\n%s\n", name, source);
    const behavior_asset *models[] = { expected, actual };
    const char *labels[] = { "expected", "actual" };
    for (int i = 0; i < 2; i++) {
        if (!models[i]) {
            continue;
        }
        av_doc *doc = av_doc_new();
        char *text = code_value_write(behavior_asset_to_value(doc, models[i]));
        char path[192];
        snprintf(path, sizeof path, "build/code-print-%s-%s.txt", name, labels[i]);
        FILE *file = fopen(path, "w");
        if (file) {
            fputs(text, file);
            fclose(file);
        }
        free(text);
        av_doc_free(doc);
    }
}

/* Executable validation runs before optional presentation restoration. The
 * following complete-model assertions then verify the separate metadata path. */
static code_source *reopen_with_metadata(const behavior_asset *asset, const char *text)
{
    code_source *source = code_source_parse(text);
    test_check(
        text && !strstr(text, "@node") && !strstr(text, "@authoring"),
        "Canonical executable source contains no generated annotations"
    );
    behavior_asset *compiled = code_source_compile_behavior(source);
    test_check(compiled != NULL, "Canonical executable independently compiles without optional metadata");
    behavior_asset_free(compiled);
    char *metadata = NULL;
    char *anchored = code_source_format_with_metadata(asset, &metadata);
    test_check(anchored && text && !strcmp(anchored, text), "Metadata emission retains identical clean executable bytes");
    free(anchored);
    test_check(metadata && code_source_metadata_restore(source, metadata), "Optional presentation metadata restores onto independently parsed executable source");
    free(metadata);
    return source;
}

static behavior_asset *reopen(const behavior_asset *asset)
{
    char error[256];
    char *text = code_print_behavior(asset, error, sizeof error);
    test_check(text != NULL, "Readable printer produces executable source");
    if (!text) {
        fprintf(stderr, "code printer: %s\n", error);
        return NULL;
    }
    test_check(!strstr(text, "nodes =") && !strstr(text, "connections =") && !strstr(text, "asset {"), "Readable behavior contains source declarations rather than graph records");
    code_source *source = code_source_parse(text);
    behavior_asset *candidate = code_source_compile_behavior(source);
    if (!candidate) {
        fprintf(stderr, "Printed source:\n%s\n", text);
        for (int i = 0; i < acount(source->diagnostics); i++) {
            fprintf(stderr, "Printer diagnostic: %s\n", source->diagnostics[i].message);
        }
    }
    test_check(candidate != NULL, "Printed source independently compiles through the native frontend");
    code_source_free(source);
    free(text);
    return candidate;
}
static int dispatch_value(behavior_instance *instance, behavior_asset *asset, const char *event)
{
    flow_error error = { 0 };
    behavior_instance_dispatch(instance, event, (flow_inputs){ 0 }, &error);
    test_check(error.code == 0 && !behavior_instance_last_failure(instance), "Printed behavior dispatch succeeds");
    behavior_variable *variable = behavior_asset_find_variable_by_name(asset, "count");
    pin_value value = { 0 };
    test_check(
        variable && behavior_instance_get_variable(instance, variable->id, &value) == 0,
        "Printed behavior state is readable"
    );
    return value.i;
}
static void shared_gate()
{
    behavior_asset *asset = native_behavior("Shared gate");
    pin_value initial = { .type = PIN_I32 };
    behavior_variable *count = behavior_asset_add_variable(
        asset,
        "count",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &initial,
        NULL
    );
    node *tick = graph_add_node(asset->graph, &NODE_Event);
    node *reset = graph_add_node(asset->graph, &NODE_Event);
    graph_set_event_name(asset->graph, tick, "Tick");
    test_check(graph_host_event_apply(asset->graph, tick) == 0, "Native Tick fixture uses its complete reflected host signature");
    graph_set_event_name(asset->graph, reset, "Reset");
    node *gate = graph_add_node(asset->graph, registry_node_by_name("MultiGate"));
    node *first = behavior_asset_add_set_node(asset, count->id);
    node *second = behavior_asset_add_set_node(asset, count->id);
    graph_set_pin_default(asset->graph, first, false, 1, &(pin_value){ .type = PIN_I32, .i = 1 });
    graph_set_pin_default(asset->graph, second, false, 1, &(pin_value){ .type = PIN_I32, .i = 2 });
    graph_connect_exec(asset->graph, tick, 0, gate, 0, 0);
    graph_connect_exec(asset->graph, reset, 0, gate, 1, 0);
    graph_connect_exec(asset->graph, gate, 0, first, 0, 0);
    graph_connect_exec(asset->graph, gate, 1, second, 0, 0);
    behavior_asset *candidate = reopen(asset);
    if (candidate) {
        behavior_instance *instance = behavior_instance_create(candidate, NULL, NULL, NULL);
        test_check(instance != NULL, "Shared native action creates a runtime instance");
        if (instance) {
            test_check(dispatch_value(instance, candidate, "Tick") == 1, "First activation advances one shared gate");
            test_check(dispatch_value(instance, candidate, "Tick") == 2, "Second activation retains gate state");
            dispatch_value(instance, candidate, "Reset");
            test_check(dispatch_value(instance, candidate, "Tick") == 1, "Different event resets the same native instance");
            behavior_instance_destroy(instance);
        }
    }
    behavior_asset_free(candidate);
    behavior_asset_free(asset);
}
static void contingent_outcome()
{
    behavior_asset *asset = native_behavior("Contingent continuation");
    behavior_variable *count = behavior_asset_add_variable(
        asset,
        "count",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_I32 },
        NULL
    );
    uint64_t count_id = count->id;
    behavior_variable *enabled = behavior_asset_add_variable(
        asset,
        "enabled",
        PIN_BOOL,
        "bool",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_BOOL },
        NULL
    );
    graph *macro = behavior_asset_add_macro(asset, "Conditional");
    node *entry = graph_node_by_id(macro, macro->entry_id);
    node *result = graph_node_by_id(macro, macro->return_id);
    graph_set_pin_name(macro, result, false, 0, "Out");
    node *condition = graph_add_node(macro, &NODE_Branch);
    node *read = behavior_asset_add_get_node_to_graph(asset, macro, enabled->id);
    graph_connect_data(macro, read, 0, condition, 1);
    graph_connect_exec(macro, entry, 0, condition, 0, 0);
    graph_connect_exec(macro, condition, 0, result, 0, 0);
    node *call = behavior_asset_add_macro_call_node(asset, macro);
    test_check(
        call->input_count == 1 && call->output_count == 1 &&
            !strcmp(call->output_pins[0].name, "Out"),
        "Contingent fixture has exactly one input and one nonconventional "
        "outcome"
    );
    node *tick = graph_add_node(asset->graph, &NODE_Event);
    graph_set_event_name(asset->graph, tick, "Tick");
    graph_host_event_apply(asset->graph, tick);
    node *open = graph_add_node(asset->graph, &NODE_Event);
    graph_set_event_name(asset->graph, open, "Open");
    node *set = behavior_asset_add_set_node(asset, count_id);
    graph_set_pin_default(asset->graph, set, false, 1, &(pin_value){ .type = PIN_I32, .i = 1 });
    node *enable = behavior_asset_add_set_node(asset, enabled->id);
    graph_set_pin_default(asset->graph, enable, false, 1, &(pin_value){ .type = PIN_BOOL, .b = true });
    graph_connect_exec(asset->graph, tick, 0, call, 0, 0);
    graph_connect_exec(asset->graph, open, 0, enable, 0, 0);
    graph_connect_exec(asset->graph, call, 0, set, 0, 0);
    char *text = code_source_format(asset);
    code_source *source = reopen_with_metadata(asset, text);
    test_check(
        source->asset && behavior_asset_equal(asset, source->asset),
        "Sole nonconventional outcome preserves native contingent continuation"
    );
    if (!source->asset || !behavior_asset_equal(asset, source->asset)) {
        dump_models("contingent-outcome", asset, source->asset, text);
    }
    if (source->asset) {
        behavior_instance *instance = behavior_instance_create(source->asset, NULL, NULL, NULL);
        test_check(instance != NULL, "Contingent outcome creates a runtime instance");
        if (instance) {
            test_check(
                dispatch_value(instance, source->asset, "Tick") == 0,
                "A non-emitting native outcome does not run its successor"
            );
            dispatch_value(instance, source->asset, "Open");
            test_check(dispatch_value(instance, source->asset, "Tick") == 1, "An emitting native outcome runs its successor");
            behavior_instance_destroy(instance);
        }
    }
    free(text);
    code_source_free(source);
    behavior_asset_free(asset);
}
static void branch_chain()
{
    behavior_asset *asset = native_behavior("Branch chain");
    pin_value initial = { .type = PIN_I32 };
    behavior_variable *count = behavior_asset_add_variable(
        asset,
        "count",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &initial,
        NULL
    );
    node *tick = graph_add_node(asset->graph, &NODE_Event);
    graph_set_event_name(asset->graph, tick, "Tick");
    test_check(graph_host_event_apply(asset->graph, tick) == 0, "Native Tick fixture uses its complete reflected host signature");
    node *condition = graph_add_node(asset->graph, &NODE_Branch);
    node *set = behavior_asset_add_set_node(asset, count->id);
    graph_set_pin_default(asset->graph, condition, false, 1, &(pin_value){ .type = PIN_BOOL, .b = true });
    graph_set_pin_default(asset->graph, set, false, 1, &(pin_value){ .type = PIN_I32, .i = 7 });
    graph_connect_exec(asset->graph, tick, 0, condition, 0, 7);
    graph_connect_exec(asset->graph, condition, 0, set, 0, 0);
    char error[256];
    char *text = code_print_behavior(asset, error, sizeof error);
    test_check(
        text && strstr(text, "if (true)") && !strstr(text, "action branch"),
        "Ordinary branch flow prints as a readable if statement"
    );
    free(text);
    behavior_asset *candidate = reopen(asset);
    if (candidate) {
        behavior_instance *instance = behavior_instance_create(candidate, NULL, NULL, NULL);
        if (instance) {
            test_check(dispatch_value(instance, candidate, "Tick") == 7, "Printed branch preserves native effects");
            behavior_instance_destroy(instance);
        }
    }
    behavior_asset_free(candidate);
    behavior_asset_free(asset);
}
static void branch_expression_identity()
{
    for (int value_mode = 0; value_mode < 2; value_mode++) {
        behavior_asset *asset = native_behavior("Predicate topology");
        node *tick = graph_add_node(asset->graph, &NODE_Event);
        graph_set_event_name(asset->graph, tick, "Tick");
        test_check(
            graph_host_event_apply(asset->graph, tick) == 0,
            "Native Tick fixture uses its complete reflected host signature"
        );
        behavior_expression_error error = { 0 };
        node *predicate = behavior_expression_add_mode(
            asset,
            asset->graph,
            "1 < 2",
            value_mode ? GRAPH_EXPRESSION_VALUE : GRAPH_EXPRESSION_BRANCH,
            &error
        );
        test_check(predicate != NULL, "Predicate fixture compiles in its explicit native mode");
        if (!predicate) {
            behavior_asset_free(asset);
            continue;
        }
        node *condition = predicate;
        if (value_mode) {
            condition = graph_add_node(asset->graph, &NODE_Branch);
            graph_connect_data(asset->graph, predicate, 0, condition, 1);
        }
        graph_connect_exec(asset->graph, tick, 0, condition, 0, 0);
        char *text = code_source_format(asset);
        code_source *source = reopen_with_metadata(asset, text);
        test_check(source->asset && behavior_asset_equal(asset, source->asset), "Source roundtrip distinguishes lazy predicate plus Branch from a branch-mode expression");
        if (!source->asset || !behavior_asset_equal(asset, source->asset)) {
            dump_models(
                value_mode ? "predicate-value" : "predicate-branch",
                asset,
                source->asset,
                text
            );
        }
        code_source_free(source);
        free(text);
        behavior_asset_free(asset);
    }
}
static void polymorphic_local()
{
    behavior_asset *asset = native_behavior("Typed global local");
    behavior_variable *total = behavior_asset_add_variable(
        asset,
        "total",
        PIN_F32,
        "float",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_F32 },
        NULL
    );
    const node_desc *desc = registry_node_by_name("Get Global Variable");
    test_check(desc != NULL, "Polymorphic native global reader is registered");
    if (!desc) {
        behavior_asset_free(asset);
        return;
    }
    node *read = graph_add_node(asset->graph, desc);
    graph_set_pin_default(
        asset->graph,
        read,
        false,
        0,
        &(pin_value){ .type = PIN_NAME, .type_name = "name", .str = "Charge" }
    );
    graph_set_pin_type(asset->graph, read, true, 0, PIN_F32, "float");
    uint64_t local = graph_declare_local(asset->graph, read, 0, "charge");
    node *set = behavior_asset_add_set_node(asset, total->id);
    graph_use_local(asset->graph, local, set, 1);
    node *tick = graph_add_node(asset->graph, &NODE_Event);
    graph_set_event_name(asset->graph, tick, "Tick");
    test_check(graph_host_event_apply(asset->graph, tick) == 0, "Native Tick fixture uses its complete reflected host signature");
    graph_connect_exec(asset->graph, tick, 0, set, 0, 0);
    behavior_variable *wide = behavior_asset_add_variable(
        asset,
        "wide",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_I32 },
        NULL
    );
    node *converted = behavior_asset_add_set_node(asset, wide->id);
    test_check(
        graph_connect_data_conversion(asset->graph, read, 0, converted, 1, registry_node_by_name("To Integer")) ==
            0,
        "Converted local fixture connects a valid float-to-integer conversion"
    );
    test_check(
        graph_set_pin_binding(
            asset->graph,
            converted,
            false,
            1,
            &(pin_binding){ .kind = PIN_BINDING_LOCAL, .local_id = local }
        ) == 0,
        "Converted local fixture retains its named producer"
    );
    node *convert_event = graph_add_node(asset->graph, &NODE_Event);
    graph_set_event_name(asset->graph, convert_event, "Convert");
    graph_connect_exec(asset->graph, convert_event, 0, converted, 0, 0);
    char *text = code_source_format(asset);
    code_source *source = reopen_with_metadata(asset, text);
    test_check(
        text && strstr(text, ": float = get_global_variable("),
        "Native wildcard reader declares its concrete computed value type"
    );
    test_check(source->asset && behavior_asset_equal(asset, source->asset), "Typed computed value preserves its named local consumers and explicit conversion");
    test_check(text && strstr(text, "convert<\"To Integer\">(&local.charge)"), "Converted local references retain their authored name inside the conversion");
    if (!source->asset || !behavior_asset_equal(asset, source->asset)) {
        dump_models("typed-global-local", asset, source->asset, text);
    }
    code_source_free(source);
    free(text);
    behavior_asset_free(asset);
}
static void iterator_loop()
{
    behavior_asset *asset = native_behavior("Iterator loop");
    behavior_variable *count = behavior_asset_add_variable(
        asset,
        "count",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_I32 },
        NULL
    );
    node *tick = graph_add_node(asset->graph, &NODE_Event);
    graph_set_event_name(asset->graph, tick, "Tick");
    test_check(graph_host_event_apply(asset->graph, tick) == 0, "Native Tick fixture uses its complete reflected host signature");
    node *loop = graph_add_node(asset->graph, &NODE_ForLoop);
    graph_set_pin_default(asset->graph, loop, false, 1, &(pin_value){ .type = PIN_I32, .i = 1 });
    graph_set_pin_default(asset->graph, loop, false, 2, &(pin_value){ .type = PIN_I32, .i = 3 });
    node *read = behavior_asset_add_get_node(asset, count->id);
    node *add = graph_add_node(asset->graph, &NODE_Add);
    graph_set_pin_type(asset->graph, add, false, 0, PIN_I32, "int");
    graph_set_pin_type(asset->graph, add, false, 1, PIN_I32, "int");
    graph_set_pin_type(asset->graph, add, true, 0, PIN_I32, "int");
    node *set = behavior_asset_add_set_node(asset, count->id);
    graph_connect_data(asset->graph, read, 0, add, 0);
    graph_connect_data(asset->graph, loop, 1, add, 1);
    graph_connect_data(asset->graph, add, 0, set, 1);
    graph_connect_exec(asset->graph, tick, 0, loop, 0, 0);
    graph_connect_exec(asset->graph, loop, 0, set, 0, 0);
    char error[256];
    char *text = code_print_behavior(asset, error, sizeof error);
    test_check(text && strstr(text, "for ("), "Reducible native iterator graphs print a scoped source loop");
    free(text);
    behavior_asset *candidate = reopen(asset);
    behavior_asset *models[] = { asset, candidate };
    for (int i = 0; i < 2; i++) {
        if (!models[i]) {
            continue;
        }
        behavior_instance *instance = behavior_instance_create(models[i], NULL, NULL, NULL);
        test_check(instance != NULL, "Native and printed iterator graphs create runtime instances");
        if (instance) {
            test_check(dispatch_value(instance, models[i], "Tick") == 6, "Source iteration preserves native index values and reads current state each iteration");
            behavior_instance_destroy(instance);
        }
    }
    behavior_asset_free(candidate);
    behavior_asset_free(asset);
}
static void float_rounding()
{
    behavior_asset *asset = native_behavior("Float rounding");
    pin_value initial = { .type = PIN_F32 };
    behavior_variable *total = behavior_asset_add_variable(
        asset,
        "total",
        PIN_F32,
        "float",
        PIN_CONTAINER_SINGLE,
        &initial,
        NULL
    );
    node *tick = graph_add_node(asset->graph, &NODE_Event);
    graph_set_event_name(asset->graph, tick, "Tick");
    test_check(graph_host_event_apply(asset->graph, tick) == 0, "Native Tick fixture uses its complete reflected host signature");
    node *first = graph_add_node(asset->graph, &NODE_Add);
    node *second = graph_add_node(asset->graph, &NODE_Add);
    node *sum[] = { first, second };
    for (int i = 0; i < 2; i++) {
        graph_set_pin_type(asset->graph, sum[i], false, 0, PIN_F32, "float");
        graph_set_pin_type(asset->graph, sum[i], false, 1, PIN_F32, "float");
        graph_set_pin_type(asset->graph, sum[i], true, 0, PIN_F32, "float");
    }
    graph_set_pin_default(asset->graph, first, false, 0, &(pin_value){ .type = PIN_F32, .f = 16777216 });
    graph_set_pin_default(asset->graph, first, false, 1, &(pin_value){ .type = PIN_F32, .f = 1 });
    graph_set_pin_default(asset->graph, second, false, 1, &(pin_value){ .type = PIN_F32, .f = -16777216 });
    graph_connect_data(asset->graph, first, 0, second, 0);
    double inputs[] = { 16777216, 1, -16777216 };
    for (int i = 0; i < 3; i++) {
        node *real = graph_add_node(asset->graph, registry_node_by_name("Real"));
        graph_set_pin_default(
            asset->graph,
            real,
            false,
            0,
            &(pin_value){ .type = PIN_F64, .d = inputs[i] }
        );
        graph_connect_data(asset->graph, real, 0, i < 2 ? first : second, i < 2 ? i : 1);
    }
    first->presentation = NODE_PRESENTATION_EXPANDED;
    for (int i = 0; i < 2; i++) {
        graph_set_pin_type(asset->graph, sum[i], false, 0, PIN_F32, "float");
        graph_set_pin_type(asset->graph, sum[i], false, 1, PIN_F32, "float");
        graph_set_pin_type(asset->graph, sum[i], true, 0, PIN_F32, "float");
    }
    node *set = behavior_asset_add_set_node(asset, total->id);
    graph_connect_data(asset->graph, second, 0, set, 1);
    graph_connect_exec(asset->graph, tick, 0, set, 0, 0);
    for (int i = 0; i < 2; i++) {
        sum[i]->input_pins[0].flags &= ~PIN_FLAG_WILDCARD;
        sum[i]->input_pins[1].flags &= ~PIN_FLAG_WILDCARD;
    }
    char *formatted = code_source_format(asset);
    code_source *roundtrip = reopen_with_metadata(asset, formatted);
    test_check(roundtrip->asset && behavior_asset_equal(asset, roundtrip->asset), "Presented native float arithmetic retains its consumer storage types when sources produce doubles");
    if (!roundtrip->asset || !behavior_asset_equal(asset, roundtrip->asset)) {
        dump_models("float-rounding", asset, roundtrip->asset, formatted);
    }
    code_source_free(roundtrip);
    free(formatted);
    behavior_asset *candidate = reopen(asset);
    if (candidate) {
        int operators = 0;
        bool float_inputs = true;
        for (int i = 0; i < acount(candidate->graph->nodes); i++) {
            const node *n = candidate->graph->nodes[i];
            if (n->desc == &NODE_Add) {
                operators++;
                float_inputs &= n->input_pins[0].type == PIN_F32 &&
                                n->input_pins[1].type == PIN_F32;
            }
        }
        test_check(operators == 2 && float_inputs, "Finalized plain float inputs retain their storage despite wildcard templates and double producers");
        behavior_instance *instance = behavior_instance_create(candidate, NULL, NULL, NULL);
        test_check(instance != NULL, "Printed float arithmetic creates an instance");
        if (instance) {
            flow_error error = { 0 };
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
            behavior_variable *variable = behavior_asset_find_variable_by_name(candidate, "total");
            pin_value value = { 0 };
            behavior_instance_get_variable(instance, variable->id, &value);
            test_check(value.type == PIN_F32 && value.f == 0, "Readable printing preserves per-operation float rounding instead of promoting the whole expression to double");
            behavior_instance_destroy(instance);
        }
    }
    behavior_asset_free(candidate);
    behavior_asset_free(asset);
}
static void double_to_float_boundary()
{
    behavior_asset *asset = native_behavior("Double precision boundary");
    behavior_variable *total = behavior_asset_add_variable(
        asset,
        "total",
        PIN_F32,
        "float",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_F32 },
        NULL
    );
    node *multiply = graph_add_node(asset->graph, &NODE_Multiply);
    node *subtract = graph_add_node(asset->graph, &NODE_Subtract);
    node *operations[] = { multiply, subtract };
    for (int i = 0; i < 2; i++) {
        graph_set_pin_type(asset->graph, operations[i], false, 0, PIN_F64, "double");
        graph_set_pin_type(asset->graph, operations[i], false, 1, PIN_F64, "double");
        graph_set_pin_type(asset->graph, operations[i], true, 0, PIN_F64, "double");
    }
    graph_set_pin_default(asset->graph, multiply, false, 0, &(pin_value){ .type = PIN_F64, .d = 16777217 });
    graph_set_pin_default(asset->graph, multiply, false, 1, &(pin_value){ .type = PIN_F64, .d = 1 });
    graph_set_pin_default(asset->graph, subtract, false, 1, &(pin_value){ .type = PIN_F64, .d = 16777216 });
    graph_connect_data(asset->graph, multiply, 0, subtract, 0);
    node *set = behavior_asset_add_set_node(asset, total->id);
    unsigned flags = subtract->output_pins[0].flags;
    subtract->output_pins[0].flags &= ~PIN_FLAG_WILDCARD;
    graph_connect_data(asset->graph, subtract, 0, set, 1);
    subtract->output_pins[0].flags = flags;
    node *tick = graph_add_node(asset->graph, &NODE_Event);
    graph_set_event_name(asset->graph, tick, "Tick");
    graph_host_event_apply(asset->graph, tick);
    graph_connect_exec(asset->graph, tick, 0, set, 0, 0);
    char *text = code_source_format(asset);
    code_source *source = reopen_with_metadata(asset, text);
    test_check(source->asset && behavior_asset_equal(asset, source->asset), "Native double operators retain their topology and widths before a float consumer");
    if (!source->asset || !behavior_asset_equal(asset, source->asset)) {
        dump_models("double-float-boundary", asset, source->asset, text);
    }
    if (source->asset) {
        int count = 0;
        bool widths = true;
        for (int i = 0; i < acount(source->asset->graph->nodes); i++) {
            const node *n = source->asset->graph->nodes[i];
            if (n->desc != &NODE_Multiply && n->desc != &NODE_Subtract) {
                continue;
            }
            count++;
            widths &= n->input_pins[0].type == PIN_F64 &&
                      n->input_pins[1].type == PIN_F64 &&
                      n->output_pins[0].type == PIN_F64;
        }
        test_check(count == 2 && widths, "Source keeps both original native double operations");
        behavior_instance *instance = behavior_instance_create(source->asset, NULL, NULL, NULL);
        test_check(instance != NULL, "Double-to-float source creates a runtime instance");
        if (instance) {
            flow_error error = { 0 };
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
            behavior_variable *v = behavior_asset_find_variable_by_name(source->asset, "total");
            pin_value value = { 0 };
            behavior_instance_get_variable(instance, v->id, &value);
            test_check(
                error.code == 0 && value.type == PIN_F32 && value.f == 1,
                "Double operations round only at the float consumer boundary"
            );
            behavior_instance_destroy(instance);
        }
    }
    code_source_free(source);
    free(text);
    behavior_asset_free(asset);
}
static void same_descriptor_identity(bool root_scope, int depth_limit)
{
    behavior_asset *asset = native_behavior("Distinct native producers");
    behavior_variable *total = behavior_asset_add_variable(
        asset,
        "total",
        PIN_F64,
        "double",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_F64 },
        NULL
    );
    graph *g = root_scope ? asset->graph
                          : behavior_asset_add_function(asset, "Compute", false);
    if (!root_scope) {
        g->call_depth_limit = depth_limit;
    }
    node *a = graph_add_node(g, &NODE_Multiply);
    node *b = graph_add_node(g, &NODE_Multiply);
    node *add = graph_add_node(g, &NODE_Add);
    node *operations[] = { a, b, add };
    for (int i = 0; i < 3; i++) {
        graph_set_pin_type(g, operations[i], false, 0, PIN_F64, "double");
        graph_set_pin_type(g, operations[i], false, 1, PIN_F64, "double");
        graph_set_pin_type(g, operations[i], true, 0, PIN_F64, "double");
    }
    graph_set_pin_default(g, a, false, 0, &(pin_value){ .type = PIN_F64, .d = 2 });
    graph_set_pin_default(g, a, false, 1, &(pin_value){ .type = PIN_F64, .d = 3 });
    graph_set_pin_default(g, b, false, 0, &(pin_value){ .type = PIN_F64, .d = 4 });
    graph_set_pin_default(g, b, false, 1, &(pin_value){ .type = PIN_F64, .d = 5 });
    graph_connect_data(g, a, 0, add, 0);
    graph_connect_data(g, b, 0, add, 1);
    node *set = behavior_asset_add_set_node_to_graph(asset, g, total->id);
    graph_connect_data(g, add, 0, set, 1);
    if (!root_scope) {
        graph_connect_exec(g, graph_node_by_id(g, g->entry_id), 0, set, 0, 0);
        graph_connect_exec(g, set, 0, graph_node_by_id(g, g->return_id), 0, 0);
    }
    node *tick = graph_add_node(asset->graph, &NODE_Event);
    graph_set_event_name(asset->graph, tick, "Tick");
    graph_host_event_apply(asset->graph, tick);
    node *call = root_scope ? set : behavior_asset_add_function_call_node(asset, g);
    graph_connect_exec(asset->graph, tick, 0, call, 0, 0);
    char *text = code_source_format(asset);
    code_source *source = reopen_with_metadata(asset, text);
    test_check(source->asset && behavior_asset_equal(asset, source->asset), "Repeated native descriptors in authored scopes retain their distinct identities");
    if (!source->asset || !behavior_asset_equal(asset, source->asset)) {
        dump_models(
            root_scope ? "same-descriptor-root" : "same-descriptor",
            asset,
            source->asset,
            text
        );
    }
    if (source->asset) {
        const graph *copy = root_scope
                              ? source->asset->graph
                              : behavior_asset_find_graph(source->asset, g->graph_id);
        if (!root_scope) {
            test_check(copy && copy->call_depth_limit == depth_limit, "Function roundtrip preserves both an absent recursion guard and a nondefault depth limit");
        }
        const node *first = copy ? graph_node_by_id(copy, a->id) : NULL;
        const node *second = copy ? graph_node_by_id(copy, b->id) : NULL;
        test_check(
            first && second && first != second &&
                first->desc == &NODE_Multiply && second->desc == &NODE_Multiply,
            "Each same-descriptor producer restores its original native ID"
        );
        behavior_instance *instance = behavior_instance_create(source->asset, NULL, NULL, NULL);
        test_check(instance != NULL, "Distinct-producer function creates a runtime instance");
        if (instance) {
            flow_error error = { 0 };
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
            behavior_variable *v = behavior_asset_find_variable_by_name(source->asset, "total");
            pin_value value = { 0 };
            behavior_instance_get_variable(instance, v->id, &value);
            test_check(error.code == 0 && value.type == PIN_F64 && value.d == 26, "Both distinct native products feed the one action after roundtrip");
            behavior_instance_destroy(instance);
        }
    }
    code_source_free(source);
    free(text);
    behavior_asset_free(asset);
}
static void computed_result_boundary()
{
    const char *text = "behavior ResultBoundary {\n"
                       "state sample: double = 16777217;\n"
                       "state observed: double = 0;\n"
                       "state contextual: double = 0;\n"
                       "value rounded: float = (cast<double>(&state.sample) - "
                       "16777216);\n"
                       "value inferred: float = (16777217.0 - 16777216.0);\n"
                       "on Tick { &state.observed = rounded; &state.contextual "
                       "= inferred; }\n}\n";
    code_source *original = code_source_parse(text);
    test_check(original->asset != NULL, "Explicit numeric result fixture compiles");
    if (!original->asset) {
        for (int i = 0; i < acount(original->diagnostics); i++) {
            const code_source_diagnostic *diagnostic = &original->diagnostics[i];
            fprintf(
                stderr,
                "Computed result %d..%d: %s\n",
                diagnostic->start,
                diagnostic->end,
                diagnostic->message
            );
        }
        code_source_free(original);
        return;
    }
    char error[256];
    char *printed = code_print_behavior(original->asset, error, sizeof error);
    test_check(
        printed && (strstr(printed, ": float = (") || strstr(printed, "cast<float>(")) &&
            !strstr(printed, "@authoring"),
        "Readable computed value declares its float result boundary without "
        "executable metadata"
    );
    if (!printed) {
        code_source_free(original);
        return;
    }
    code_source *reopened = code_source_parse(printed);
    test_check(reopened->asset != NULL, "Printed numeric result source independently compiles");
    if (!reopened->asset) {
        dump_models("computed-result-boundary", original->asset, NULL, printed);
    }
    behavior_asset *models[] = { original->asset, reopened->asset };
    for (int side = 0; side < 2; side++) {
        behavior_asset *asset = models[side];
        if (!asset) {
            continue;
        }
        bool boundary = false;
        bool double_computation = false;
        bool float_computation = false;
        for (int scope = 0; scope < acount(asset->graphs); scope++) {
            const graph *g = asset->graphs[scope];
            for (int i = 0; i < acount(g->nodes); i++) {
                const node *n = g->nodes[i];
                boundary |= n->desc == &NODE_BehaviorExpression &&
                            node_behavior_for(n) == NODE_BEHAVIOR_PURE &&
                            n->output_count == 1 && n->output_pins[0].type == PIN_F32;
                if (n->desc != &NODE_Subtract) {
                    continue;
                }
                double_computation |= n->input_pins[0].type == PIN_F64 &&
                                      n->input_pins[1].type == PIN_F64 &&
                                      n->output_pins[0].type == PIN_F64;
                float_computation |= n->input_pins[0].type == PIN_F32 &&
                                     n->input_pins[1].type == PIN_F32 &&
                                     n->output_pins[0].type == PIN_F32;
            }
        }
        test_check(boundary && double_computation, "Explicit double computation survives a float computed result boundary");
        test_check(float_computation, "Ordinary typed float expression infers float computation");
        behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
        test_check(instance != NULL, "Numeric result boundary creates a runtime instance");
        if (instance) {
            flow_error failure = { 0 };
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &failure);
            const behavior_variable *observed = behavior_asset_find_variable_by_name(asset, "observed");
            pin_value value = { 0 };
            behavior_instance_get_variable(instance, observed->id, &value);
            test_check(failure.code == 0 && value.type == PIN_F64 && value.d == 1, "Computed float boundary rounds after double subtraction instead of before it");
            const behavior_variable *contextual = behavior_asset_find_variable_by_name(asset, "contextual");
            behavior_instance_get_variable(instance, contextual->id, &value);
            test_check(failure.code == 0 && value.type == PIN_F64 && value.d == 0, "Contextual float computation rounds its operands before subtraction");
            behavior_instance_destroy(instance);
        }
    }
    code_source_free(reopened);
    free(printed);
    code_source_free(original);
}
static void lazy_helper_sharing()
{
    const char
        *text = "behavior Sharing {\n"
                "state count: double = 4; state duration: double = 8;\n"
                "value fade = (&state.count / max(&state.duration, 1));\n"
                "value strength = (fade * fade);\n"
                "on Tick { &state.count = strength; }\n}\n";
    code_source *source = code_source_parse(text);
    test_check(source->asset != NULL, "Shared lazy helper fixture compiles");
    if (!source->asset) {
        code_source_free(source);
        return;
    }
    char *formatted = code_source_format(source->asset);
    code_source *roundtrip = reopen_with_metadata(source->asset, formatted);
    test_check(roundtrip->asset && behavior_asset_equal(source->asset, roundtrip->asset), "Repeated helper operands retain their original lazy producer and evaluation graph");
    if (!roundtrip->asset || !behavior_asset_equal(source->asset, roundtrip->asset)) {
        dump_models("lazy-sharing", source->asset, roundtrip->asset, formatted);
    }
    if (roundtrip->asset) {
        behavior_instance *instance = behavior_instance_create(roundtrip->asset, NULL, NULL, NULL);
        test_check(instance != NULL, "Shared lazy helper roundtrip creates an instance");
        if (instance) {
            flow_error error = { 0 };
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
            behavior_variable
                *count = behavior_asset_find_variable_by_name(roundtrip->asset, "count");
            pin_value value = { 0 };
            behavior_instance_get_variable(instance, count->id, &value);
            test_check(error.code == 0 && value.type == PIN_F64 && value.d == 0.25, "Shared helper reads current state and preserves the squared fade result");
            behavior_instance_destroy(instance);
        }
    }
    free(formatted);
    code_source_free(roundtrip);
    code_source_free(source);
}
static void cast_comparison_loops()
{
    const char *text = "behavior CastLoops {\n"
                       "state spawned: int = 0; state total: int = 3;\n"
                       "state count: int = 0;\n"
                       "on Tick {\n"
                       "    if (cast<double>(&state.spawned) < "
                       "cast<double>(&state.total)) {\n"
                       "        while (cast<double>(&state.spawned) < "
                       "cast<double>(&state.total)) {\n"
                       "            for (index in 0..0) {\n"
                       "                &state.count += 1;\n"
                       "                &state.spawned += 1;\n"
                       "            }\n"
                       "        }\n"
                       "    }\n"
                       "}\n}\n";
    code_source *original = code_source_parse(text);
    test_check(original->asset != NULL, "Cast comparisons with nested control flow compile");
    if (!original->asset) {
        for (int i = 0; i < acount(original->diagnostics); i++) {
            fprintf(
                stderr,
                "Nested cast loop diagnostic: %s\n",
                original->diagnostics[i].message
            );
        }
        code_source_free(original);
        return;
    }
    behavior_asset *reopened = reopen(original->asset);
    behavior_asset *models[] = { original->asset, reopened };
    for (int side = 0; side < 2; side++) {
        behavior_asset *asset = models[side];
        if (!asset) {
            continue;
        }
        int whiles = 0;
        int fors = 0;
        for (int i = 0; i < acount(asset->graph->nodes); i++) {
            const node *n = asset->graph->nodes[i];
            whiles += n->desc == &NODE_WhileLoop;
            fors += n->desc == &NODE_ForLoop;
        }
        test_check(whiles == 1 && fors == 1, "Cast comparison lowering retains both nested native loops");
        behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
        test_check(instance != NULL, "Nested cast loops create a runtime instance");
        if (instance) {
            flow_error failure = { 0 };
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &failure);
            const behavior_variable *count = behavior_asset_find_variable_by_name(asset, "count");
            const behavior_variable *spawned = behavior_asset_find_variable_by_name(asset, "spawned");
            pin_value value = { 0 };
            behavior_instance_get_variable(instance, count->id, &value);
            test_check(failure.code == 0 && value.type == PIN_I32 && value.i == 3, "Nested loop executes all three increments under cast comparisons");
            behavior_instance_get_variable(instance, spawned->id, &value);
            test_check(value.type == PIN_I32 && value.i == 3, "Nested cast loop advances its condition to termination");
            behavior_instance_destroy(instance);
        }
    }
    behavior_asset_free(reopened);
    code_source_free(original);
}
static void shared_expression_callable()
{
    code_source *source = code_source_parse(
        "behavior SharedPhase { state age: double = 3; state first: double = "
        "0; "
        "state second: double = 0; value phase = ((&state.age + 1) / 2); }"
    );
    test_check(source->asset != NULL, "Shared expression fixture compiles");
    if (!source->asset) {
        code_source_free(source);
        return;
    }
    behavior_asset *asset = source->asset;
    node *first = NULL;
    for (int i = 0; i < acount(asset->graph->nodes); i++) {
        if (asset->graph->nodes[i]->desc == &NODE_BehaviorExpression) {
            first = asset->graph->nodes[i];
            break;
        }
    }
    test_check(first && first->input_count == 1, "Native phase expression captures one dynamic input");
    if (!first || first->input_count != 1) {
        code_source_free(source);
        return;
    }
    graph *body = behavior_asset_find_graph(asset, first->reference_id);
    behavior_asset_rename_graph(asset, body->graph_id, "AgePhase");
    graph *observer = behavior_asset_add_function(asset, "Observe", false);
    node *second = graph_add_node(observer, &NODE_BehaviorExpression);
    second->reference_id = body->graph_id;
    second->reference_name = graph_own_str(observer, "AgePhase", 8);
    test_check(
        graph_library_refresh_call(observer, second, body) == NULL,
        "Second native expression caller shares the original generated body"
    );
    graph_set_pin_binding(observer, second, false, 0, first->input_pins[0].binding);
    const behavior_variable *first_state = behavior_asset_find_variable_by_name(asset, "first");
    const behavior_variable *second_state = behavior_asset_find_variable_by_name(asset, "second");
    node *first_set = behavior_asset_add_set_node(asset, first_state->id);
    node *second_set = behavior_asset_add_set_node_to_graph(
        asset,
        observer,
        second_state->id
    );
    graph_connect_data(asset->graph, first, 0, first_set, 1);
    graph_connect_data(observer, second, 0, second_set, 1);
    graph_connect_exec(observer, graph_node_by_id(observer, observer->entry_id), 0, second_set, 0, 0);
    graph_connect_exec(
        observer,
        second_set,
        0,
        graph_node_by_id(observer, observer->return_id),
        0,
        0
    );
    node *observe = behavior_asset_add_function_call_node(asset, observer);
    node *tick = graph_add_node(asset->graph, &NODE_Event);
    graph_set_event_name(asset->graph, tick, "Tick");
    graph_host_event_apply(asset->graph, tick);
    graph_connect_exec(asset->graph, tick, 0, first_set, 0, 0);
    graph_connect_exec(asset->graph, first_set, 0, observe, 0, 0);
    char error[256];
    char *printed = code_print_behavior(asset, error, sizeof error);
    test_check(printed != NULL, "Shared generated expression prints as an ordinary callable");
    if (!printed) {
        code_source_free(source);
        return;
    }
    char *clean = malloc(strlen(printed) + 1);
    char *out = clean;
    for (const char *line = printed; *line;) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line + 1) : strlen(line);
        const char *content = line;
        while (*content == ' ') {
            content++;
        }
        if (strncmp(content, "// @node", 8) && strncmp(content, "// @authoring", 13)) {
            memcpy(out, line, length);
            out += length;
        }
        line += length;
    }
    *out = 0;
    code_source *reopened = code_source_parse(clean);
    test_check(reopened->asset != NULL, "Shared callable source reopens without identity metadata");
    if (!reopened->asset) {
        dump_models("shared-expression", asset, NULL, clean);
    }
    behavior_asset *models[] = { asset, reopened->asset };
    for (int side = 0; side < 2; side++) {
        behavior_asset *model = models[side];
        if (!model) {
            continue;
        }
        graph *shared = behavior_asset_find_graph_by_name(model, "AgePhase");
        int callers = 0, adds = 0, divides = 0;
        for (int scope = 0; scope < acount(model->graphs); scope++) {
            graph *g = model->graphs[scope];
            for (int j = 0; j < acount(g->nodes); j++) {
                node *n = g->nodes[j];
                adds += n->desc == &NODE_Add;
                divides += n->desc == &NODE_Divide;
                if (!shared || n->reference_id != shared->graph_id) {
                    continue;
                }
                callers++;
                test_check(
                    n->input_count == 1 && n->input_pins[0].type == PIN_F64 &&
                        n->output_count == 1 && n->output_pins[0].type == PIN_F64 &&
                        n->input_pins[0].binding &&
                        n->input_pins[0].binding->kind == PIN_BINDING_VARIABLE,
                    "Both shared callable sites preserve double signature and "
                    "dynamic state binding"
                );
            }
        }
        test_check(
            shared && callers == 2 && adds == 1 && divides == 1,
            "Cross-scope callers reuse one body with one add and divide"
        );
        behavior_instance *instance = behavior_instance_create(model, NULL, NULL, NULL);
        test_check(instance != NULL, "Shared generated callable creates a runtime instance");
        if (instance) {
            const behavior_variable *age = behavior_asset_find_variable_by_name(model, "age");
            for (int run = 0; run < 2; run++) {
                behavior_instance_set_variable(
                    instance,
                    age->id,
                    &(pin_value){ .type = PIN_F64, .d = run ? 7 : 3 }
                );
                flow_error failure = { 0 };
                behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &failure);
                const char *names[] = { "first", "second" };
                for (int field = 0; field < 2; field++) {
                    const behavior_variable
                        *v = behavior_asset_find_variable_by_name(model, names[field]);
                    pin_value value = { 0 };
                    behavior_instance_get_variable(instance, v->id, &value);
                    test_check(failure.code == 0 && value.type == PIN_F64 && value.d == (run ? 4 : 2), "Both scopes evaluate the shared body using current dynamic inputs");
                }
            }
            behavior_instance_destroy(instance);
        }
    }
    code_source_free(reopened);
    free(clean);
    free(printed);
    code_source_free(source);
}
static void expression_constant_width()
{
    behavior_asset *asset = native_behavior("Constant precision");
    behavior_variable *sample = behavior_asset_add_variable(
        asset,
        "sample",
        PIN_F32,
        "float",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_F32, .f = 16777216 },
        NULL
    );
    behavior_variable *observed = behavior_asset_add_variable(
        asset,
        "observed",
        PIN_F32,
        "float",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_F32 },
        NULL
    );
    graph *body = behavior_asset_add_function(asset, "AgePrecision", true);
    graph_signature_add(
        asset,
        body,
        false,
        &(pin_spec){ .name = "age", .kind = PIN_DATA, .type = PIN_F32, .type_name = "float" }
    );
    graph_signature_add(
        asset,
        body,
        true,
        &(pin_spec){ .name = "Result", .kind = PIN_DATA, .type = PIN_F32, .type_name = "float" }
    );
    body->math_expression = graph_own_str(body, "age - 16777217", strlen("age - 16777217"));
    body->expression_mode = GRAPH_EXPRESSION_VALUE;
    node *constant = graph_add_node(body, registry_node_by_name("Real"));
    graph_set_pin_default(body, constant, false, 0, &(pin_value){ .type = PIN_F64, .d = 16777217 });
    node *subtract = graph_add_node(body, &NODE_Subtract);
    graph_set_pin_type(body, subtract, false, 0, PIN_F64, "double");
    graph_set_pin_type(body, subtract, false, 1, PIN_F64, "double");
    graph_set_pin_type(body, subtract, true, 0, PIN_F64, "double");
    subtract->input_pins[0].flags &= ~PIN_FLAG_WILDCARD;
    subtract->input_pins[1].flags &= ~PIN_FLAG_WILDCARD;
    subtract->output_pins[0].flags &= ~PIN_FLAG_WILDCARD;
    graph_connect_data(body, graph_node_by_id(body, body->entry_id), 0, subtract, 0);
    graph_connect_data(body, constant, 0, subtract, 1);
    graph_connect_data(body, subtract, 0, graph_node_by_id(body, body->return_id), 0);
    node *call = behavior_asset_add_function_call_node(asset, body);
    call->desc = &NODE_BehaviorExpression;
    graph_set_pin_binding(
        asset->graph,
        call,
        false,
        0,
        &(pin_binding){ .kind = PIN_BINDING_VARIABLE, .variable_id = sample->id }
    );
    node *set = behavior_asset_add_set_node(asset, observed->id);
    graph_connect_data(asset->graph, call, 0, set, 1);
    node *tick = graph_add_node(asset->graph, &NODE_Event);
    graph_set_event_name(asset->graph, tick, "Tick");
    graph_host_event_apply(asset->graph, tick);
    graph_connect_exec(asset->graph, tick, 0, set, 0, 0);
    char error[256];
    char *printed = code_print_behavior(asset, error, sizeof error);
    test_check(
        printed && strstr(printed, "pure function \"AgePrecision\""),
        "Single-caller helper with wider constants prints its typed native body"
    );
    if (!printed) {
        behavior_asset_free(asset);
        return;
    }
    char *clean = malloc(strlen(printed) + 1), *out = clean;
    for (const char *line = printed; *line;) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line + 1) : strlen(line);
        const char *content = line;
        while (*content == ' ') {
            content++;
        }
        if (strncmp(content, "// @node", 8) && strncmp(content, "// @authoring", 13)) {
            memcpy(out, line, length);
            out += length;
        }
        line += length;
    }
    *out = 0;
    code_source *reopened = code_source_parse(clean);
    test_check(reopened->asset != NULL, "Precision helper reopens without identity metadata");
    if (!reopened->asset) {
        dump_models("constant-width", asset, NULL, clean);
    }
    behavior_asset *models[] = { asset, reopened->asset };
    for (int side = 0; side < 2; side++) {
        behavior_asset *model = models[side];
        if (!model) {
            continue;
        }
        graph *g = behavior_asset_find_graph_by_name(model, "AgePrecision");
        bool wide_constant = false, wide_operation = false;
        for (int j = 0; g && j < acount(g->nodes); j++) {
            node *n = g->nodes[j];
            wide_constant |= n->desc == registry_node_by_name("Real") &&
                             n->output_pins[0].type == PIN_F64 &&
                             n->input_pins[0].default_value.d == 16777217;
            wide_operation |= n->desc == &NODE_Subtract &&
                              n->input_pins[0].type == PIN_F64 &&
                              n->input_pins[1].type == PIN_F64 &&
                              n->output_pins[0].type == PIN_F64;
        }
        const node *entry = g ? graph_node_by_id(g, g->entry_id) : NULL;
        const node *result = g ? graph_node_by_id(g, g->return_id) : NULL;
        test_check(
            wide_constant && wide_operation && entry && result &&
                entry->output_pins[0].type == PIN_F32 &&
                result->input_pins[0].type == PIN_F32,
            "Native double constant and subtraction survive the float callable "
            "boundary"
        );
        behavior_instance *instance = behavior_instance_create(model, NULL, NULL, NULL);
        test_check(instance != NULL, "Precision helper creates a runtime instance");
        if (instance) {
            flow_error failure = { 0 };
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &failure);
            const behavior_variable *v = behavior_asset_find_variable_by_name(model, "observed");
            pin_value value = { 0 };
            behavior_instance_get_variable(instance, v->id, &value);
            test_check(failure.code == 0 && value.type == PIN_F32 && value.f == -1, "Wider helper constant remains distinguishable from its rounded float value");
            behavior_instance_destroy(instance);
        }
    }
    code_source_free(reopened);
    free(clean);
    free(printed);
    behavior_asset_free(asset);
}
static void action_result_width()
{
    code_source *original = code_source_parse(
        "behavior ActionResult { state sample: double = 16777217; "
        "state lhs: float = 0; state observed: double = 0; "
        "on Tick { &state.lhs = cast<double>(&state.sample); } }"
    );
    test_check(original->asset != NULL, "Action result precision fixture compiles");
    if (!original->asset) {
        code_source_free(original);
        return;
    }
    behavior_asset *asset = original->asset;
    node *action = NULL;
    int result_pin = -1, then_pin = -1;
    for (int i = 0; i < acount(asset->graph->nodes); i++) {
        node *n = asset->graph->nodes[i];
        if (n->desc != &NODE_BehaviorExpression ||
            node_behavior_for(n) == NODE_BEHAVIOR_PURE) {
            continue;
        }
        action = n;
        for (int j = 0; j < n->output_count; j++) {
            if (n->output_pins[j].kind == PIN_DATA) {
                result_pin = j;
            }
            if (n->output_pins[j].kind == PIN_EXEC) {
                then_pin = j;
            }
        }
        break;
    }
    test_check(action && result_pin >= 0 && then_pin >= 0, "Native assignment exposes execution and a data result");
    if (!action || result_pin < 0 || then_pin < 0) {
        code_source_free(original);
        return;
    }
    test_check(
        graph_set_pin_type(asset->graph, action, true, result_pin, PIN_F64, "double") == 0,
        "Native action result accepts an explicit double schema"
    );
    test_check(
        behavior_expression_refresh_mode(asset, action, GRAPH_EXPRESSION_NONE, NULL) == 0,
        "Native action refresh retains its explicitly saved result width"
    );
    test_check(action->output_pins[result_pin].type == PIN_F64, "Native action keeps its explicitly saved double Result ABI");
    graph *body = behavior_asset_find_graph(asset, action->reference_id);
    behavior_asset_rename_graph(asset, body->graph_id, "AssignPrecision");
    const behavior_variable *observed = behavior_asset_find_variable_by_name(asset, "observed");
    node *set = behavior_asset_add_set_node(asset, observed->id);
    graph_connect_data(asset->graph, action, result_pin, set, 1);
    graph_connect_exec(asset->graph, action, then_pin, set, 0, 0);
    char error[256];
    char *printed = code_print_behavior(asset, error, sizeof error);
    test_check(
        printed && strstr(printed, "function \"AssignPrecision\"") &&
            !strstr(printed, "pure function \"AssignPrecision\""),
        "Consumed action Result prints an ordinary impure function body"
    );
    if (!printed) {
        code_source_free(original);
        return;
    }
    char *clean = malloc(strlen(printed) + 1), *out = clean;
    for (const char *line = printed; *line;) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line + 1) : strlen(line);
        const char *content = line;
        while (*content == ' ') {
            content++;
        }
        if (strncmp(content, "// @node", 8) && strncmp(content, "// @authoring", 13)) {
            memcpy(out, line, length);
            out += length;
        }
        line += length;
    }
    *out = 0;
    code_source *reopened = code_source_parse(clean);
    test_check(reopened->asset != NULL, "Action precision source reopens without identity metadata");
    if (!reopened->asset) {
        dump_models("action-result-width", asset, NULL, clean);
    }
    behavior_asset *models[] = { asset, reopened->asset };
    for (int side = 0; side < 2; side++) {
        behavior_asset *model = models[side];
        if (!model) {
            continue;
        }
        graph *target = behavior_asset_find_graph_by_name(model, "AssignPrecision");
        int callers = 0;
        for (int j = 0; j < acount(model->graph->nodes); j++) {
            node *n = model->graph->nodes[j];
            if (!target || n->reference_id != target->graph_id) {
                continue;
            }
            callers++;
            bool double_result = false, float_argument = false;
            for (int k = 0; k < n->output_count; k++) {
                double_result |= n->output_pins[k].kind == PIN_DATA &&
                                 n->output_pins[k].type == PIN_F64;
            }
            for (int k = 0; k < n->input_count; k++) {
                float_argument |= n->input_pins[k].kind == PIN_DATA &&
                                  n->input_pins[k].type == PIN_F32 &&
                                  n->input_pins[k].binding;
            }
            const behavior_variable *lhs = behavior_asset_find_variable_by_name(model, "lhs");
            bool writes_lhs = false;
            for (int k = 0; target && k < acount(target->nodes); k++) {
                const node *write = target->nodes[k];
                writes_lhs |= write->desc == &NODE_SetVariable &&
                              write->reference_id == lhs->id;
            }
            if (!double_result || !float_argument || !writes_lhs) {
                dump_models("action-result-abi", asset, reopened->asset, clean);
                fprintf(stderr, "Action side%d double%d reference%d\n", side, double_result, float_argument);
            }
            test_check(double_result && float_argument && writes_lhs, "Action callable preserves its double result, bound float argument and concrete writable target");
        }
        test_check(callers == 1, "Assignment and returned value share one action invocation");
        behavior_instance *instance = behavior_instance_create(model, NULL, NULL, NULL);
        test_check(instance != NULL, "Action result precision creates a runtime instance");
        if (instance) {
            flow_error failure = { 0 };
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &failure);
            const behavior_variable *lhs = behavior_asset_find_variable_by_name(model, "lhs");
            const behavior_variable *result = behavior_asset_find_variable_by_name(model, "observed");
            pin_value value = { 0 };
            behavior_instance_get_variable(instance, lhs->id, &value);
            test_check(
                failure.code == 0 && value.type == PIN_F32 && value.f == 16777216,
                "Assignment rounds only its float destination"
            );
            behavior_instance_get_variable(instance, result->id, &value);
            if (value.type != PIN_F64 || value.d != 16777216) {
                fprintf(
                    stderr,
                    "Action side%d resulttype%d result%.17g error%d\n",
                    side,
                    value.type,
                    value.d,
                    failure.code
                );
                dump_models("action-result-runtime", asset, reopened->asset, clean);
            }
            test_check(value.type == PIN_F64 && value.d == 16777216, "Consumed action result widens the stored float through its double result ABI");
            behavior_instance_destroy(instance);
        }
    }
    code_source_free(reopened);
    free(clean);
    free(printed);
    code_source_free(original);
}
static void typed_native_math_call()
{
    code_source *original = code_source_parse(
        "behavior NativeMinimum { state observed: float = 0; "
        "value minimum: float = min(A: plain float = 3.0, B: plain float = "
        "7.0); "
        "on Tick { set_variable<\"observed\">(Value: minimum); } }"
    );
    test_check(original->asset != NULL, "Typed named native math call compiles");
    if (!original->asset) {
        for (int i = 0; i < acount(original->diagnostics); i++) {
            fprintf(
                stderr,
                "Native math call diagnostic: %s\n",
                original->diagnostics[i].message
            );
        }
        code_source_free(original);
        return;
    }
    char error[256];
    char *printed = code_print_behavior(original->asset, error, sizeof error);
    test_check(printed != NULL, "Typed native math call prints readable source");
    if (!printed) {
        code_source_free(original);
        return;
    }
    char *clean = malloc(strlen(printed) + 1), *out = clean;
    for (const char *line = printed; *line;) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line + 1) : strlen(line);
        const char *content = line;
        while (*content == ' ') {
            content++;
        }
        if (strncmp(content, "// @node", 8) && strncmp(content, "// @authoring", 13)) {
            memcpy(out, line, length);
            out += length;
        }
        line += length;
    }
    *out = 0;
    code_source *reopened = code_source_parse(clean);
    test_check(reopened->asset != NULL, "Typed native math call reopens without identity metadata");
    if (!reopened->asset) {
        dump_models("typed-native-min", original->asset, NULL, clean);
    }
    behavior_asset *models[] = { original->asset, reopened->asset };
    for (int side = 0; side < 2; side++) {
        behavior_asset *asset = models[side];
        if (!asset) {
            continue;
        }
        int minima = 0, helpers = 0;
        bool widths = true;
        for (int i = 0; i < acount(asset->graph->nodes); i++) {
            const node *n = asset->graph->nodes[i];
            helpers += n->desc == &NODE_BehaviorExpression;
            if (n->desc != &NODE_Min) {
                continue;
            }
            minima++;
            widths &= n->input_count == 2 && n->output_count == 1 &&
                      n->input_pins[0].type == PIN_F32 &&
                      n->input_pins[1].type == PIN_F32 &&
                      n->output_pins[0].type == PIN_F32 &&
                      !(n->input_pins[0].flags & PIN_FLAG_WILDCARD) &&
                      !(n->input_pins[1].flags & PIN_FLAG_WILDCARD);
        }
        test_check(acount(asset->graphs) == 1 && minima == 1 && helpers == 0 && widths, "Typed native Min remains one float operation without a generated expression wrapper");
        behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
        test_check(instance != NULL, "Typed native Min creates a runtime instance");
        if (instance) {
            flow_error failure = { 0 };
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &failure);
            const behavior_variable *observed = behavior_asset_find_variable_by_name(asset, "observed");
            pin_value value = { 0 };
            behavior_instance_get_variable(instance, observed->id, &value);
            test_check(
                failure.code == 0 && value.type == PIN_F32 && value.f == 3,
                "Native minimum feeds its float result into the observed state"
            );
            behavior_instance_destroy(instance);
        }
    }
    code_source_free(reopened);
    free(clean);
    free(printed);
    code_source_free(original);
}
static void known_double_operand()
{
    code_source *original = code_source_parse(
        "behavior KnownDouble { state sample: double = 0.001; "
        "state observed: double = 1; "
        "value difference: float = (&state.sample - 0.001); "
        "on Tick { set_variable<\"observed\">(Value: difference); } }"
    );
    test_check(original->asset != NULL, "Known double operand precision fixture compiles");
    if (!original->asset) {
        code_source_free(original);
        return;
    }
    char error[256];
    char *printed = code_print_behavior(original->asset, error, sizeof error);
    test_check(printed != NULL, "Known double operand prints readable source");
    if (!printed) {
        code_source_free(original);
        return;
    }
    char *clean = malloc(strlen(printed) + 1), *out = clean;
    for (const char *line = printed; *line;) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line + 1) : strlen(line);
        const char *content = line;
        while (*content == ' ') {
            content++;
        }
        if (strncmp(content, "// @node", 8) && strncmp(content, "// @authoring", 13)) {
            memcpy(out, line, length);
            out += length;
        }
        line += length;
    }
    *out = 0;
    code_source *reopened = code_source_parse(clean);
    test_check(reopened->asset != NULL, "Known double operand source reopens without metadata");
    if (!reopened->asset) {
        dump_models("known-double", original->asset, NULL, clean);
    }
    behavior_asset *models[] = { original->asset, reopened->asset };
    for (int side = 0; side < 2; side++) {
        behavior_asset *asset = models[side];
        if (!asset) {
            continue;
        }
        int subtractions = 0;
        bool widths = true, float_result = false;
        for (int scope = 0; scope < acount(asset->graphs); scope++) {
            graph *g = asset->graphs[scope];
            bool contains_subtract = false;
            for (int j = 0; j < acount(g->nodes); j++) {
                node *n = g->nodes[j];
                if (n->desc == &NODE_BehaviorExpression ||
                    n->desc == &NODE_CallPureFunction) {
                    for (int output = 0; output < n->output_count; output++) {
                        float_result |= n->output_pins[output].kind == PIN_DATA &&
                                        n->output_pins[output].type == PIN_F32;
                    }
                }
                if (n->desc != &NODE_Subtract) {
                    continue;
                }
                subtractions++;
                contains_subtract = true;
                widths &= n->input_pins[0].type == PIN_F64 &&
                          n->input_pins[1].type == PIN_F64 &&
                          n->output_pins[0].type == PIN_F64;
            }
            if (!contains_subtract) {
                continue;
            }
            const node *entry = graph_node_by_id(g, g->entry_id);
            for (int j = 0; entry && j < entry->output_count; j++) {
                if (entry->output_pins[j].kind == PIN_DATA) {
                    widths &= entry->output_pins[j].type == PIN_F64;
                }
            }
        }
        if (subtractions != 1 || !widths || !float_result) {
            fprintf(stderr, "Double side%d operations%d widths%d boundary%d\n", side, subtractions, widths, float_result);
            dump_models("known-double", original->asset, reopened->asset, clean);
        }
        test_check(subtractions == 1 && widths && float_result, "Known double capture keeps subtraction and its contextual literal in double storage");
        behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
        test_check(instance != NULL, "Known double operand creates a runtime instance");
        if (instance) {
            flow_error failure = { 0 };
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &failure);
            const behavior_variable *observed = behavior_asset_find_variable_by_name(asset, "observed");
            pin_value value = { 0 };
            behavior_instance_get_variable(instance, observed->id, &value);
            test_check(failure.code == 0 && value.type == PIN_F64 && value.d == 0, "Equal double sample and literal cancel exactly before the float result boundary");
            behavior_instance_destroy(instance);
        }
    }
    code_source_free(reopened);
    free(clean);
    free(printed);
    code_source_free(original);
}
static void component_alias_capture()
{
    behavior_asset *asset = native_behavior("Alias capture");
    behavior_asset_set_owner_type(asset, "ecs.entity");
    behavior_variable *observed = behavior_asset_add_variable(
        asset,
        "observed",
        PIN_F32,
        "float",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_F32 },
        NULL
    );
    graph *body = behavior_asset_add_function(asset, "VectorX", true);
    graph_signature_add(
        asset,
        body,
        false,
        &(pin_spec){ .name = "position", .kind = PIN_DATA, .type = PIN_STRUCT, .type_name = "CF_V2" }
    );
    graph_signature_add(
        asset,
        body,
        true,
        &(pin_spec){ .name = "Result", .kind = PIN_DATA, .type = PIN_F32, .type_name = "float" }
    );
    body->math_expression = graph_own_str(body, "position.x", 10);
    body->expression_mode = GRAPH_EXPRESSION_VALUE;
    node *entry = graph_node_by_id(body, body->entry_id);
    test_check(graph_expose_output_fields(body, entry, 0) == 0, "Vector capture exposes its native x projection");
    int x = -1;
    for (int j = 0; j < entry->output_count; j++) {
        if (entry->output_pins[j].field_source &&
            !strcmp(entry->output_pins[j].field_name, "x")) {
            x = j;
        }
    }
    test_check(x >= 0, "Vector capture has a reflected x field");
    if (x < 0) {
        behavior_asset_free(asset);
        return;
    }
    graph_connect_data(body, entry, x, graph_node_by_id(body, body->return_id), 0);
    node *call = behavior_asset_add_function_call_node(asset, body);
    call->desc = &NODE_BehaviorExpression;
    test_check(
        graph_set_pin_binding(
            asset->graph,
            call,
            false,
            0,
            &(pin_binding){ .kind = PIN_BINDING_COMPONENT, .component_key = "GAME.GameTransform" }
        ) == 0,
        "Physical vector alias accepts its component binding"
    );
    node *set = behavior_asset_add_set_node(asset, observed->id);
    graph_connect_data(asset->graph, call, 0, set, 1);
    node *tick = graph_add_node(asset->graph, &NODE_Event);
    graph_set_event_name(asset->graph, tick, "Tick");
    graph_host_event_apply(asset->graph, tick);
    graph_connect_exec(asset->graph, tick, 0, set, 0, 0);
    char error[256];
    char *printed = code_print_behavior(asset, error, sizeof error);
    test_check(
        printed && strstr(printed, "pure function \"VectorX\""),
        "Cross-nominal capture prints its original typed helper body"
    );
    if (!printed) {
        behavior_asset_free(asset);
        return;
    }
    char *clean = malloc(strlen(printed) + 1), *out = clean;
    for (const char *line = printed; *line;) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line + 1) : strlen(line);
        const char *content = line;
        while (*content == ' ') {
            content++;
        }
        if (strncmp(content, "// @node", 8) && strncmp(content, "// @authoring", 13)) {
            memcpy(out, line, length);
            out += length;
        }
        line += length;
    }
    *out = 0;
    code_source *reopened = code_source_parse(clean);
    test_check(reopened->asset != NULL, "Component alias capture reopens without metadata");
    if (!reopened->asset) {
        dump_models("component-alias", asset, NULL, clean);
    }
    ecs_world *world = ecs_world_new();
    ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL);
    ecs_entity entity = ecs_world_create_entity(world);
    GAME_GameTransform_add(world, entity, 0);
    ecs_world_apply_commands(world);
    *GAME_GameTransform_get(world, entity) = (GameTransform){ 2.5f, 8 };
    eval_component_reader reader;
    ecs_eval_component_reader_init(&reader, world);
    behavior_asset *models[] = { asset, reopened->asset };
    for (int side = 0; side < 2; side++) {
        behavior_asset *model = models[side];
        if (!model) {
            continue;
        }
        graph *g = behavior_asset_find_graph_by_name(model, "VectorX");
        const node *e = g ? graph_node_by_id(g, g->entry_id) : NULL;
        int callers = 0;
        for (int j = 0; j < acount(model->graph->nodes); j++) {
            node *n = model->graph->nodes[j];
            if (!g || n->reference_id != g->graph_id) {
                continue;
            }
            callers++;
            const pin_binding *binding = n->input_pins[0].binding;
            test_check(
                n->input_pins[0].type == PIN_STRUCT &&
                    !strcmp(n->input_pins[0].type_name, "CF_V2") && binding &&
                    binding->kind == PIN_BINDING_COMPONENT &&
                    !strcmp(binding->component_key, "GAME.GameTransform"),
                "Caller retains CF_V2 storage and the GameTransform component "
                "binding"
            );
        }
        test_check(
            callers == 1 && e && e->output_pins[0].type == PIN_STRUCT &&
                !strcmp(e->output_pins[0].type_name, "CF_V2"),
            "One helper retains its native vector Entry type"
        );
        pin_value owner = { .type = PIN_HANDLE,
                            .type_name = "ecs.entity",
                            .u64 = ecs_entity_to_u64(entity) };
        behavior_instance *instance = behavior_instance_create(model, &owner, NULL, NULL);
        test_check(instance != NULL, "Component alias helper creates a runtime instance");
        if (instance) {
            test_check(
                behavior_instance_bind_service(instance, "ecs.world", world) == 0 &&
                    behavior_instance_bind_service(instance, EVAL_COMPONENT_READER_SERVICE, &reader) ==
                        0,
                "Alias fixture binds both native world and component reader "
                "services"
            );
            flow_error failure = { 0 };
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &failure);
            const behavior_variable *v = behavior_asset_find_variable_by_name(model, "observed");
            pin_value value = { 0 };
            behavior_instance_get_variable(instance, v->id, &value);
            test_check(
                failure.code == 0 && value.type == PIN_F32 && value.f == 2.5f,
                "Vector alias reads the current component x value"
            );
            behavior_instance_destroy(instance);
        }
    }
    ecs_world_free(world);
    code_source_free(reopened);
    free(clean);
    free(printed);
    behavior_asset_free(asset);
}
static void vector_cross_result_boundary()
{
    code_source *source = code_source_parse(
        "behavior CrossBoundary { state a: struct<\"CF_V2\"> = "
        "{x:10000.25,y:10000.5}; "
        "state b: struct<\"CF_V2\"> = {x:10000.75,y:10001}; state observed: "
        "double = 0; }"
    );
    test_check(source->asset != NULL, "Vector cross precision fixture compiles");
    if (!source->asset) {
        code_source_free(source);
        return;
    }
    behavior_asset *asset = source->asset;
    graph *cross = behavior_asset_add_function(asset, "CrossPrecision", true);
    graph_signature_add(
        asset,
        cross,
        false,
        &(pin_spec){ .name = "a", .kind = PIN_DATA, .type = PIN_STRUCT, .type_name = "CF_V2" }
    );
    graph_signature_add(
        asset,
        cross,
        false,
        &(pin_spec){ .name = "b", .kind = PIN_DATA, .type = PIN_STRUCT, .type_name = "CF_V2" }
    );
    graph_signature_add(
        asset,
        cross,
        true,
        &(pin_spec){ .name = "Result", .kind = PIN_DATA, .type = PIN_F32, .type_name = "float" }
    );
    cross->math_expression = graph_own_str(cross, "a.x * b.y - a.y * b.x", strlen("a.x * b.y - a.y * b.x"));
    cross->expression_mode = GRAPH_EXPRESSION_VALUE;
    node *entry = graph_node_by_id(cross, cross->entry_id);
    graph_expose_output_fields(cross, entry, 0);
    graph_expose_output_fields(cross, entry, 1);
    int fields[2][2] = { { -1, -1 }, { -1, -1 } };
    for (int j = 0; j < entry->output_count; j++) {
        const pin_spec *p = &entry->output_pins[j];
        if (!p->field_source) {
            continue;
        }
        for (int root = 0; root < 2; root++) {
            if (p->field_source != entry->output_pins[root].id) {
                continue;
            }
            if (!strcmp(p->field_name, "x")) {
                fields[root][0] = j;
            }
            if (!strcmp(p->field_name, "y")) {
                fields[root][1] = j;
            }
        }
    }
    test_check(
        fields[0][0] >= 0 && fields[0][1] >= 0 && fields[1][0] >= 0 && fields[1][1] >= 0,
        "Vector cross exposes both typed vector field pairs"
    );
    if (fields[0][0] < 0 || fields[0][1] < 0 || fields[1][0] < 0 || fields[1][1] < 0) {
        code_source_free(source);
        return;
    }
    node *first = graph_add_node(cross, &NODE_Multiply);
    node *second = graph_add_node(cross, &NODE_Multiply);
    node *subtract = graph_add_node(cross, &NODE_Subtract);
    node *ops[] = { first, second, subtract };
    for (int j = 0; j < 3; j++) {
        for (int input = 0; input < 2; input++) {
            graph_set_pin_type(cross, ops[j], false, input, PIN_F64, "double");
            ops[j]->input_pins[input].flags &= ~PIN_FLAG_WILDCARD;
        }
        graph_set_pin_type(cross, ops[j], true, 0, PIN_F64, "double");
        ops[j]->output_pins[0].flags &= ~PIN_FLAG_WILDCARD;
    }
    graph_connect_data(cross, entry, fields[0][0], first, 0);
    graph_connect_data(cross, entry, fields[1][1], first, 1);
    graph_connect_data(cross, entry, fields[0][1], second, 0);
    graph_connect_data(cross, entry, fields[1][0], second, 1);
    graph_connect_data(cross, first, 0, subtract, 0);
    graph_connect_data(cross, second, 0, subtract, 1);
    graph_connect_data(cross, subtract, 0, graph_node_by_id(cross, cross->return_id), 0);
    graph *observer = behavior_asset_add_function(asset, "ObserveCross", true);
    graph_signature_add(
        asset,
        observer,
        true,
        &(pin_spec){ .name = "Result", .kind = PIN_DATA, .type = PIN_F64, .type_name = "double" }
    );
    node *hidden = behavior_asset_add_function_call_node_to_graph(asset, observer, cross);
    hidden->desc = &NODE_BehaviorExpression;
    const char *names[] = { "a", "b" };
    for (int j = 0; j < 2; j++) {
        const behavior_variable *v = behavior_asset_find_variable_by_name(asset, names[j]);
        graph_set_pin_binding(
            observer,
            hidden,
            false,
            j,
            &(pin_binding){ .kind = PIN_BINDING_VARIABLE, .variable_id = v->id }
        );
    }
    graph_connect_data(observer, hidden, 0, graph_node_by_id(observer, observer->return_id), 0);
    node *call = behavior_asset_add_function_call_node(asset, observer);
    const behavior_variable *observed = behavior_asset_find_variable_by_name(asset, "observed");
    node *set = behavior_asset_add_set_node(asset, observed->id);
    graph_connect_data(asset->graph, call, 0, set, 1);
    node *tick = graph_add_node(asset->graph, &NODE_Event);
    graph_set_event_name(asset->graph, tick, "Tick");
    graph_host_event_apply(asset->graph, tick);
    graph_connect_exec(asset->graph, tick, 0, set, 0, 0);
    char error[256];
    char *printed = code_print_behavior(asset, error, sizeof error);
    test_check(
        printed && strstr(printed, "pure function \"CrossPrecision\""),
        "Wider arithmetic with float Return prints its original helper body"
    );
    if (!printed) {
        code_source_free(source);
        return;
    }
    char *clean = malloc(strlen(printed) + 1), *out = clean;
    for (const char *line = printed; *line;) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line + 1) : strlen(line);
        const char *content = line;
        while (*content == ' ') {
            content++;
        }
        if (strncmp(content, "// @node", 8) && strncmp(content, "// @authoring", 13)) {
            memcpy(out, line, length);
            out += length;
        }
        line += length;
    }
    *out = 0;
    code_source *reopened = code_source_parse(clean);
    test_check(reopened->asset != NULL, "Vector cross boundary reopens without metadata");
    if (!reopened->asset) {
        dump_models("vector-cross", asset, NULL, clean);
    }
    float ax = 10000.25f, ay = 10000.5f, bx = 10000.75f, by = 10001.0f;
    double expected = (float)((double)ax * by - (double)ay * bx);
    float early_a = ax * by, early_b = ay * bx;
    test_check(expected != (double)(early_a - early_b), "Cross precision fixture distinguishes double products from prematurely rounded float products");
    behavior_asset *models[] = { asset, reopened->asset };
    for (int side = 0; side < 2; side++) {
        behavior_asset *model = models[side];
        if (!model) {
            continue;
        }
        graph *g = behavior_asset_find_graph_by_name(model, "CrossPrecision");
        graph *outer = behavior_asset_find_graph_by_name(model, "ObserveCross");
        int multiplies = 0, subtractions = 0;
        bool widths = true;
        for (int j = 0; g && j < acount(g->nodes); j++) {
            node *n = g->nodes[j];
            multiplies += n->desc == &NODE_Multiply;
            subtractions += n->desc == &NODE_Subtract;
            if (n->desc == &NODE_Multiply || n->desc == &NODE_Subtract) {
                widths &= n->input_pins[0].type == PIN_F64 &&
                          n->input_pins[1].type == PIN_F64 &&
                          n->output_pins[0].type == PIN_F64;
            }
        }
        const node *e = g ? graph_node_by_id(g, g->entry_id) : NULL;
        const node *ret = g ? graph_node_by_id(g, g->return_id) : NULL;
        const node *outer_ret = outer ? graph_node_by_id(outer, outer->return_id)
                                      : NULL;
        bool float_call = false;
        for (int j = 0; outer && j < acount(outer->nodes); j++) {
            node *n = outer->nodes[j];
            if (g && n->reference_id == g->graph_id) {
                float_call |= n->input_count == 2 &&
                              !strcmp(n->input_pins[0].type_name, "CF_V2") &&
                              !strcmp(n->input_pins[1].type_name, "CF_V2") &&
                              n->output_pins[0].type == PIN_F32;
            }
        }
        test_check(
            multiplies == 2 && subtractions == 1 && widths && e &&
                !strcmp(e->output_pins[0].type_name, "CF_V2") &&
                !strcmp(e->output_pins[1].type_name, "CF_V2") && ret &&
                ret->input_pins[0].type == PIN_F32 && outer_ret &&
                outer_ret->input_pins[0].type == PIN_F64 && float_call,
            "Typed body and Float helper boundary remain distinct from the "
            "public Double return"
        );
        behavior_instance *instance = behavior_instance_create(model, NULL, NULL, NULL);
        test_check(instance != NULL, "Vector cross boundary creates a runtime instance");
        if (instance) {
            flow_error failure = { 0 };
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &failure);
            const behavior_variable *v = behavior_asset_find_variable_by_name(model, "observed");
            pin_value value = { 0 };
            behavior_instance_get_variable(instance, v->id, &value);
            test_check(failure.code == 0 && value.type == PIN_F64 && value.d == expected, "Double vector products round only at the original Float helper return");
            behavior_instance_destroy(instance);
        }
    }
    code_source_free(reopened);
    free(clean);
    free(printed);
    code_source_free(source);
}
static void vector_atan2_captures()
{
    code_source *source = code_source_parse(
        "behavior VectorAngle { state center: struct<\"CF_V2\"> = "
        "{x:2.25,y:-1.5}; "
        "state position: struct<\"CF_V2\"> = {x:-3.75,y:4.5}; state observed: "
        "double = 0; }"
    );
    test_check(source->asset != NULL, "Vector angle fixture compiles");
    if (!source->asset) {
        code_source_free(source);
        return;
    }
    behavior_asset *asset = source->asset;
    graph *body = behavior_asset_add_function(asset, "VectorAngleBody", true);
    const char *names[] = { "center", "position" };
    for (int j = 0; j < 2; j++) {
        graph_signature_add(
            asset,
            body,
            false,
            &(pin_spec){ .name = names[j], .kind = PIN_DATA, .type = PIN_STRUCT, .type_name = "CF_V2" }
        );
    }
    graph_signature_add(
        asset,
        body,
        true,
        &(pin_spec){ .name = "Result", .kind = PIN_DATA, .type = PIN_F64, .type_name = "double" }
    );
    const char *formula = "atan2(position.y - center.y, position.x - center.x)";
    body->math_expression = graph_own_str(body, formula, strlen(formula));
    body->expression_mode = GRAPH_EXPRESSION_VALUE;
    node *entry = graph_node_by_id(body, body->entry_id);
    graph_expose_output_fields(body, entry, 0);
    graph_expose_output_fields(body, entry, 1);
    int fields[2][2] = { { -1, -1 }, { -1, -1 } };
    for (int j = 0; j < entry->output_count; j++) {
        const pin_spec *p = &entry->output_pins[j];
        if (!p->field_source) {
            continue;
        }
        for (int parent = 0; parent < 2; parent++) {
            if (p->field_source != entry->output_pins[parent].id) {
                continue;
            }
            if (!strcmp(p->field_name, "x")) {
                fields[parent][0] = j;
            }
            if (!strcmp(p->field_name, "y")) {
                fields[parent][1] = j;
            }
        }
    }
    test_check(
        fields[0][0] >= 0 && fields[0][1] >= 0 && fields[1][0] >= 0 && fields[1][1] >= 0,
        "Vector angle exposes projections from two aggregate parents"
    );
    if (fields[0][0] < 0 || fields[0][1] < 0 || fields[1][0] < 0 || fields[1][1] < 0) {
        code_source_free(source);
        return;
    }
    const node_desc *atan_desc = registry_node_by_name("Atan2");
    node *angle = graph_add_node(body, atan_desc);
    for (int axis = 0; axis < 2; axis++) {
        node *difference = graph_add_node(body, &NODE_Subtract);
        for (int input = 0; input < 2; input++) {
            graph_set_pin_type(body, difference, false, input, PIN_F64, "double");
            difference->input_pins[input].flags &= ~PIN_FLAG_WILDCARD;
        }
        graph_set_pin_type(body, difference, true, 0, PIN_F64, "double");
        difference->output_pins[0].flags &= ~PIN_FLAG_WILDCARD;
        graph_connect_data(body, entry, fields[1][axis], difference, 0);
        graph_connect_data(body, entry, fields[0][axis], difference, 1);
        graph_connect_data(body, difference, 0, angle, axis ? 0 : 1);
    }
    graph_connect_data(body, angle, 0, graph_node_by_id(body, body->return_id), 0);
    node *call = behavior_asset_add_function_call_node(asset, body);
    call->desc = &NODE_BehaviorExpression;
    for (int j = 0; j < 2; j++) {
        const behavior_variable *v = behavior_asset_find_variable_by_name(asset, names[j]);
        graph_set_pin_binding(
            asset->graph,
            call,
            false,
            j,
            &(pin_binding){ .kind = PIN_BINDING_VARIABLE, .variable_id = v->id }
        );
    }
    const behavior_variable *observed = behavior_asset_find_variable_by_name(asset, "observed");
    node *set = behavior_asset_add_set_node(asset, observed->id);
    graph_connect_data(asset->graph, call, 0, set, 1);
    node *tick = graph_add_node(asset->graph, &NODE_Event);
    graph_set_event_name(asset->graph, tick, "Tick");
    graph_host_event_apply(asset->graph, tick);
    graph_connect_exec(asset->graph, tick, 0, set, 0, 0);
    char error[256];
    char *printed = code_print_behavior(asset, error, sizeof error);
    test_check(printed != NULL, "Aggregate angle capture prints readable source");
    if (!printed) {
        code_source_free(source);
        return;
    }
    char *clean = malloc(strlen(printed) + 1), *out = clean;
    for (const char *line = printed; *line;) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line + 1) : strlen(line);
        const char *content = line;
        while (*content == ' ') {
            content++;
        }
        if (strncmp(content, "// @node", 8) && strncmp(content, "// @authoring", 13)) {
            memcpy(out, line, length);
            out += length;
        }
        line += length;
    }
    *out = 0;
    code_source *reopened = code_source_parse(clean);
    test_check(reopened->asset != NULL, "Aggregate angle source reopens without metadata");
    if (!reopened->asset) {
        dump_models("vector-atan2", asset, NULL, clean);
    }
    behavior_asset *models[] = { asset, reopened->asset };
    for (int side = 0; side < 2; side++) {
        behavior_asset *model = models[side];
        if (!model) {
            continue;
        }
        graph *g = behavior_asset_find_graph_by_name(model, "VectorAngleBody");
        const node *e = g ? graph_node_by_id(g, g->entry_id) : NULL;
        int captures = 0, subtracts = 0, angles = 0, callers = 0;
        bool widths = true;
        for (int j = 0; e && j < e->output_count; j++) {
            const pin_spec *p = &e->output_pins[j];
            if (p->kind != PIN_DATA || p->field_source) {
                continue;
            }
            captures++;
            widths &= p->type == PIN_STRUCT && !strcmp(p->type_name, "CF_V2");
        }
        for (int j = 0; g && j < acount(g->nodes); j++) {
            node *n = g->nodes[j];
            subtracts += n->desc == &NODE_Subtract;
            angles += n->desc == atan_desc;
            if (n->desc == &NODE_Subtract || n->desc == atan_desc) {
                widths &= n->input_pins[0].type == PIN_F64 &&
                          n->input_pins[1].type == PIN_F64 &&
                          n->output_pins[0].type == PIN_F64;
            }
        }
        for (int j = 0; j < acount(model->graph->nodes); j++) {
            node *n = model->graph->nodes[j];
            if (!g || n->reference_id != g->graph_id) {
                continue;
            }
            callers++;
            widths &= n->input_count == 2 && n->input_pins[0].type == PIN_STRUCT &&
                      n->input_pins[1].type == PIN_STRUCT &&
                      n->input_pins[0].binding && n->input_pins[1].binding &&
                      n->output_pins[0].type == PIN_F64;
        }
        test_check(captures == 2 && callers == 1 && subtracts == 2 && angles == 1 && widths, "Angle helper retains two vector parents and its typed subtraction/atan2 body");
        behavior_instance *instance = behavior_instance_create(model, NULL, NULL, NULL);
        test_check(instance != NULL, "Vector angle helper creates a runtime instance");
        if (instance) {
            flow_error failure = { 0 };
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &failure);
            const behavior_variable *v = behavior_asset_find_variable_by_name(model, "observed");
            pin_value value = { 0 };
            behavior_instance_get_variable(instance, v->id, &value);
            test_check(
                failure.code == 0 && value.type == PIN_F64 &&
                    fabs(value.d - atan2(6.0, -6.0)) < 1e-12,
                "Angle calculation reads both original vectors in the correct "
                "atan2 operand order"
            );
            behavior_instance_destroy(instance);
        }
    }
    code_source_free(reopened);
    free(clean);
    free(printed);
    code_source_free(source);
}
static void expression_grouping()
{
    const char *expressions[] = {
        "((&state.a + &state.b) * &state.c)",
        "(&state.a + &state.b) * &state.c",
    };
    for (int variant = 0; variant < 2; variant++) {
        char text[512];
        snprintf(
            text,
            sizeof text,
            "behavior Grouping { state a: double = 2; state b: double = 3; "
            "state c: double = 4; state observed: double = 0; "
            "on Tick { set_variable<\"observed\">(Value: double = %s); } }",
            expressions[variant]
        );
        code_source *original = code_source_parse(text);
        test_check(original->asset != NULL, "Grouped expression fixture compiles");
        if (!original->asset) {
            code_source_free(original);
            continue;
        }
        char *first = code_source_format(original->asset);
        code_source *second = reopen_with_metadata(original->asset, first);
        test_check(second->asset != NULL, "First canonical grouped expression reopens");
        if (!second->asset) {
            free(first);
            code_source_free(second);
            code_source_free(original);
            continue;
        }
        char *again = code_source_format(second->asset);
        code_source *third = reopen_with_metadata(second->asset, again);
        test_check(third->asset != NULL, "Second canonical grouped expression reopens");
        char *last = third->asset ? code_source_format(third->asset) : NULL;
        test_check(last && !strcmp(first, again) && !strcmp(again, last), "Repeated canonical formatting does not accumulate redundant expression groups");
        test_check(
            behavior_asset_equal(original->asset, second->asset) &&
                third->asset && behavior_asset_equal(second->asset, third->asset),
            "Canonical grouped expression retains native operations and "
            "identities"
        );
        behavior_asset *models[] = { original->asset, second->asset, third->asset };
        for (int side = 0; side < 3; side++) {
            behavior_asset *asset = models[side];
            if (!asset) {
                continue;
            }
            behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
            test_check(instance != NULL, "Grouped expression creates a runtime instance");
            if (instance) {
                flow_error failure = { 0 };
                behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &failure);
                const behavior_variable *observed = behavior_asset_find_variable_by_name(asset, "observed");
                pin_value value = { 0 };
                behavior_instance_get_variable(instance, observed->id, &value);
                test_check(failure.code == 0 && value.type == PIN_F64 && value.d == 20, "Canonical grouping preserves addition before multiplication");
                behavior_instance_destroy(instance);
            }
        }
        free(last);
        code_source_free(third);
        free(again);
        code_source_free(second);
        free(first);
        code_source_free(original);
    }
}
static void configured_struct_literal()
{
    code_source *source = code_source_parse("behavior ConfiguredLiteral { on Tick { self.Transform = {x:196,y:260}; } }");
    test_check(source->asset != NULL, "Configured component struct literal parses with its declared nominal type");
    if (!source->asset) {
        for (int i = 0; i < acount(source->diagnostics); i++) {
            fprintf(
                stderr,
                "Configured initial literal: %s\n",
                source->diagnostics[i].message
            );
        }
        code_source_free(source);
        return;
    }
    char error[256];
    char *text = code_print_behavior(source->asset, error, sizeof error);
    test_check(
        text && (strstr(text, "Value: struct<\"GameTransform\">") ||
                 strstr(text, "self.Transform =")),
        "Printer retains configured component type in its source reference"
    );
    code_source *copy = text ? code_source_parse(text) : NULL;
    test_check(copy && copy->asset, "Configured struct literal reopens without authoring metadata");
    if (copy && !copy->asset) {
        fprintf(stderr, "Configured literal source:\n%s\n", text);
        for (int i = 0; i < acount(copy->diagnostics); i++) {
            fprintf(stderr, "Configured literal: %s\n", copy->diagnostics[i].message);
        }
    }
    if (copy && copy->asset) {
        node *setter = NULL;
        for (int i = 0; i < acount(copy->asset->graph->nodes); i++) {
            if (copy->asset->graph->nodes[i]->desc == &NODE_SetComponent) {
                setter = copy->asset->graph->nodes[i];
            }
        }
        const pin_spec *value = NULL;
        for (int i = 0; setter && i < setter->input_count; i++) {
            if (!strcmp(setter->input_pins[i].name, "Value")) {
                value = &setter->input_pins[i];
            }
        }
        const CF_V2 *point = value ? value->default_value.ptr : NULL;
        test_check(
            value && value->type == PIN_STRUCT && value->type_name &&
                !strcmp(value->type_name, "GameTransform") && point &&
                point->x == 196 && point->y == 260,
            "Metadata-free component literal retains both nominal type and "
            "authored coordinates"
        );
    }
    code_source_free(copy);
    free(text);
    code_source_free(source);
}
static void shaped_struct_template()
{
    code_source *initial = code_source_parse("behavior ShapedStruct { state health: struct<\"GameHealth\"> = {current:0,maximum:0}; }");
    test_check(initial->asset != NULL, "Shaped struct fixture compiles");
    if (!initial->asset) {
        code_source_free(initial);
        return;
    }
    behavior_asset *asset = initial->asset;
    node *make = graph_add_node(asset->graph, &NODE_MakeStruct);
    test_check(
        component_node_retarget(asset->graph, make, "GameHealth") == 0,
        "Make Struct configures its reflected GameHealth field template"
    );
    test_check(
        make->input_count == 3 && !strcmp(make->input_pins[0].name, "current") &&
            !strcmp(make->input_pins[1].name, "maximum") &&
            !strcmp(make->input_pins[2].name, "Type"),
        "Shaped constructor places Type after its data fields"
    );
    graph_set_pin_default(asset->graph, make, false, 0, &(pin_value){ .type = PIN_I32, .i = 5 });
    graph_set_pin_default(asset->graph, make, false, 1, &(pin_value){ .type = PIN_I32, .i = 9 });
    const behavior_variable *health = behavior_asset_find_variable_by_name(asset, "health");
    node *set = behavior_asset_add_set_node(asset, health->id);
    graph_connect_data(asset->graph, make, 0, set, 1);
    node *tick = graph_add_node(asset->graph, &NODE_Event);
    graph_set_event_name(asset->graph, tick, "Tick");
    graph_host_event_apply(asset->graph, tick);
    graph_connect_exec(asset->graph, tick, 0, set, 0, 0);
    char error[256];
    char *readable = code_print_behavior(asset, error, sizeof error);
    test_check(
        readable && strstr(readable, "current:") &&
            strstr(readable, "maximum:") && strstr(readable, "Type:"),
        "Readable constructor distinguishes reflected fields from its Type "
        "setting"
    );
    code_source *printed = readable ? code_source_parse(readable) : NULL;
    test_check(printed && printed->asset, "Shaped constructor independently reopens printed source");
    char *canonical = code_source_format(asset);
    code_source *formatted = reopen_with_metadata(asset, canonical);
    test_check(formatted->asset && behavior_asset_equal(asset, formatted->asset), "Canonical constructor roundtrip preserves reflected fields, settings and identities");
    if (!formatted->asset || !behavior_asset_equal(asset, formatted->asset)) {
        dump_models("shaped-struct", asset, formatted->asset, canonical);
    }
    behavior_asset *models[] = { asset,
                                 printed ? printed->asset : NULL,
                                 formatted->asset };
    for (int side = 0; side < 3; side++) {
        behavior_asset *model = models[side];
        if (!model) {
            continue;
        }
        node *constructor = NULL;
        for (int j = 0; j < acount(model->graph->nodes); j++) {
            if (model->graph->nodes[j]->desc == &NODE_MakeStruct) {
                constructor = model->graph->nodes[j];
                break;
            }
        }
        test_check(
            constructor && constructor->input_count == 3 &&
                !strcmp(constructor->input_pins[0].name, "current") &&
                constructor->input_pins[0].default_value.i == 5 &&
                !strcmp(constructor->input_pins[1].name, "maximum") &&
                constructor->input_pins[1].default_value.i == 9 &&
                !strcmp(constructor->input_pins[2].name, "Type") &&
                (constructor->input_pins[2].flags & PIN_FLAG_SETTING) &&
                !strcmp(constructor->output_pins[0].type_name, "GameHealth"),
            "Constructor retains both typed data defaults and its trailing "
            "reflected Type setting"
        );
        behavior_asset *runtime = behavior_asset_link_libraries(model, NULL, 0, error, sizeof error);
        test_check(runtime != NULL, "Shaped constructor lowers its reflected node before execution");
        behavior_instance *instance = runtime
                                        ? behavior_instance_create(runtime, NULL, NULL, NULL)
                                        : NULL;
        test_check(instance != NULL, "Shaped struct constructor creates a runtime instance");
        if (instance) {
            flow_error failure = { 0 };
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &failure);
            const behavior_variable *v = behavior_asset_find_variable_by_name(runtime, "health");
            pin_value value = { 0 };
            behavior_instance_get_variable(instance, v->id, &value);
            const GameHealth *result = value.ptr;
            if (failure.code || value.type != PIN_STRUCT || !result ||
                result->current != 5 || result->maximum != 9) {
                const behavior_instance_failure *detail = behavior_instance_last_failure(instance);
                fprintf(
                    stderr,
                    "Shaped struct side%d error%d type%d ptr%p current%d "
                    "maximum%d detail%s\n",
                    side,
                    failure.code,
                    value.type,
                    (void *)result,
                    result ? result->current : -1,
                    result ? result->maximum : -1,
                    detail && detail->message ? detail->message : ""
                );
            }
            test_check(
                failure.code == 0 && value.type == PIN_STRUCT && result &&
                    result->current == 5 && result->maximum == 9,
                "Shaped constructor builds the authored GameHealth field values"
            );
            behavior_instance_destroy(instance);
        }
        behavior_asset_free(runtime);
    }
    code_source_free(formatted);
    free(canonical);
    code_source_free(printed);
    free(readable);
    code_source_free(initial);
}
static void named_expression_capture()
{
    for (int driven = 0; driven < 2; driven++) {
        behavior_asset *asset = native_behavior("Named capture");
        behavior_variable *observed = behavior_asset_add_variable(
            asset,
            "observed",
            PIN_F64,
            "double",
            PIN_CONTAINER_SINGLE,
            &(pin_value){ .type = PIN_F64 },
            NULL
        );
        behavior_expression_error why = { 0 };
        node *expression = behavior_expression_add_mode(
            asset,
            asset->graph,
            "a + 0.001",
            GRAPH_EXPRESSION_VALUE,
            &why
        );
        test_check(
            expression && expression->input_count == 1 &&
                !strcmp(expression->input_pins[0].name, "a"),
            "Native expression exposes its authored named capture"
        );
        if (!expression || expression->input_count != 1) {
            behavior_asset_free(asset);
            continue;
        }
        graph_set_pin_default(asset->graph, expression, false, 0, &(pin_value){ .type = PIN_F64, .d = 3 });
        if (driven) {
            node *integer = graph_add_node(asset->graph, registry_node_by_name("Integer"));
            graph_set_pin_default(
                asset->graph,
                integer,
                false,
                0,
                &(pin_value){ .type = PIN_I32, .i = 16777217 }
            );
            graph_connect_data(asset->graph, integer, 0, expression, 0);
        }
        test_check(behavior_expression_refresh(asset, expression, &why) == 0, "Named expression capture compiles with its default or actual producer");
        node *set = behavior_asset_add_set_node(asset, observed->id);
        graph_connect_data(asset->graph, expression, 0, set, 1);
        node *tick = graph_add_node(asset->graph, &NODE_Event);
        graph_set_event_name(asset->graph, tick, "Tick");
        graph_host_event_apply(asset->graph, tick);
        graph_connect_exec(asset->graph, tick, 0, set, 0, 0);
        char error[256];
        char *printed = code_print_behavior(asset, error, sizeof error);
        test_check(printed != NULL, "Named capture prints executable source");
        if (!printed) {
            behavior_asset_free(asset);
            continue;
        }
        char *clean = malloc(strlen(printed) + 1), *out = clean;
        for (const char *line = printed; *line;) {
            const char *end = strchr(line, '\n');
            size_t length = end ? (size_t)(end - line + 1) : strlen(line);
            const char *content = line;
            while (*content == ' ') {
                content++;
            }
            if (strncmp(content, "// @node", 8) &&
                strncmp(content, "// @authoring", 13)) {
                memcpy(out, line, length);
                out += length;
            }
            line += length;
        }
        *out = 0;
        code_source *reopened = code_source_parse(clean);
        test_check(reopened->asset != NULL, "Named capture independently reopens without metadata");
        if (!reopened->asset) {
            dump_models("named-expression", asset, NULL, clean);
        }
        char *canonical = code_source_format(asset);
        code_source *identity = reopen_with_metadata(asset, canonical);
        test_check(identity->asset && behavior_asset_equal(asset, identity->asset), "Named expression capture retains native source identity through canonical metadata");
        if (!identity->asset || !behavior_asset_equal(asset, identity->asset)) {
            dump_models("named-expression-identity", asset, identity->asset, canonical);
        }
        behavior_asset *models[] = { asset, reopened->asset, identity->asset };
        for (int side = 0; side < 3; side++) {
            behavior_asset *model = models[side];
            if (!model) {
                continue;
            }
            node *capture = NULL;
            for (int j = 0; j < acount(model->graph->nodes); j++) {
                if (model->graph->nodes[j]->desc == &NODE_BehaviorExpression) {
                    capture = model->graph->nodes[j];
                    break;
                }
            }
            test_check(
                capture && capture->input_count == 1 &&
                    !strcmp(capture->input_pins[0].name, "a"),
                "Reopened expression keeps its authored capture name"
            );
            if (!driven && capture) {
                test_check(
                    capture->input_pins[0].default_value.type == PIN_F64 &&
                        capture->input_pins[0].default_value.d == 3,
                    "Unconnected named capture retains its authored default"
                );
            }
            if (driven && capture) {
                node *producer = NULL;
                int output = -1;
                test_check(
                    graph_input_source(model->graph, capture, 0, &producer, &output) &&
                        producer->desc == registry_node_by_name("Integer") &&
                        producer->input_pins[0].default_value.i == 16777217,
                    "Driven named capture retains its Integer producer"
                );
            }
            behavior_instance *instance = behavior_instance_create(model, NULL, NULL, NULL);
            test_check(instance != NULL, "Named capture creates a runtime instance");
            if (instance) {
                flow_error failure = { 0 };
                behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &failure);
                const behavior_variable *v = behavior_asset_find_variable_by_name(model, "observed");
                pin_value value = { 0 };
                behavior_instance_get_variable(instance, v->id, &value);
                test_check(
                    failure.code == 0 && value.type == PIN_F64 &&
                        value.d == (driven ? 16777217.001 : 3.001),
                    "Named capture evaluates its authored default or driven "
                    "input"
                );
                behavior_instance_destroy(instance);
            }
        }
        code_source_free(identity);
        free(canonical);
        code_source_free(reopened);
        free(clean);
        free(printed);
        behavior_asset_free(asset);
    }
}
static void defaulted_action_capture()
{
    code_source *initial = code_source_parse(
        "behavior DefaultedAction { state kept: int = 11; state removed: int = "
        "7; "
        "on Tick { &state.kept += &state.removed; } }"
    );
    test_check(initial->asset != NULL, "Defaulted action capture fixture compiles");
    if (!initial->asset) {
        code_source_free(initial);
        return;
    }
    behavior_asset *asset = initial->asset;
    node *action = NULL;
    int lhs = -1, rhs = -1;
    for (int i = 0; i < acount(asset->graph->nodes); i++) {
        node *n = asset->graph->nodes[i];
        if (n->desc != &NODE_BehaviorExpression ||
            node_behavior_for(n) == NODE_BEHAVIOR_PURE) {
            continue;
        }
        action = n;
        for (int j = 0; j < n->input_count; j++) {
            if (n->input_pins[j].kind != PIN_DATA) {
                continue;
            }
            if (n->input_pins[j].flags & PIN_FLAG_REFERENCE) {
                lhs = j;
            } else {
                rhs = j;
            }
        }
        break;
    }
    test_check(action && lhs >= 0 && rhs >= 0, "Native action has writable lhs and rhs data captures");
    if (!action || lhs < 0 || rhs < 0) {
        code_source_free(initial);
        return;
    }
    behavior_expression_error why = { 0 };
    test_check(
        behavior_expression_rename_input(asset, action, action->input_pins[lhs].name, "lhs", &why) ==
            0,
        "Native action capture lhs receives an authored name"
    );
    test_check(
        behavior_expression_rename_input(asset, action, action->input_pins[rhs].name, "rhs", &why) ==
            0,
        "Native action capture rhs receives an authored name"
    );
    test_check(
        graph_set_pin_binding(asset->graph, action, false, rhs, NULL) == 0 &&
            graph_set_pin_default(asset->graph, action, false, rhs, &(pin_value){ .type = PIN_I32, .i = 0 }) ==
                0,
        "Removing rhs binding leaves its native integer default zero"
    );
    char error[256];
    char *printed = code_print_behavior(asset, error, sizeof error);
    test_check(printed != NULL, "Defaulted action captures print executable source");
    if (!printed) {
        code_source_free(initial);
        return;
    }
    char *clean = malloc(strlen(printed) + 1), *out = clean;
    for (const char *line = printed; *line;) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line + 1) : strlen(line);
        const char *content = line;
        while (*content == ' ') {
            content++;
        }
        if (strncmp(content, "// @node", 8) && strncmp(content, "// @authoring", 13)) {
            memcpy(out, line, length);
            out += length;
        }
        line += length;
    }
    *out = 0;
    code_source *reopened = code_source_parse(clean);
    test_check(reopened->asset != NULL, "Defaulted action capture reopens without metadata");
    if (!reopened->asset) {
        dump_models("defaulted-action", asset, NULL, clean);
    }
    char *canonical = code_source_format(asset);
    code_source *identity = reopen_with_metadata(asset, canonical);
    test_check(
        identity->asset && behavior_asset_equal(asset, identity->asset),
        "Canonical named action retains the complete native capture contract"
    );
    if (!identity->asset || !behavior_asset_equal(asset, identity->asset)) {
        dump_models("defaulted-action-identity", asset, identity->asset, canonical);
    }
    behavior_asset *models[] = { asset, reopened->asset, identity->asset };
    for (int side = 0; side < 3; side++) {
        behavior_asset *model = models[side];
        if (!model) {
            continue;
        }
        node *call = NULL;
        int data = 0;
        bool reference = false, defaulted = false;
        for (int j = 0; j < acount(model->graph->nodes); j++) {
            if (model->graph->nodes[j]->desc == &NODE_BehaviorExpression) {
                call = model->graph->nodes[j];
                break;
            }
        }
        for (int j = 0; call && j < call->input_count; j++) {
            const pin_spec *p = &call->input_pins[j];
            if (p->kind != PIN_DATA) {
                continue;
            }
            data++;
            if (!strcmp(p->name, "lhs")) {
                reference = p->type == PIN_I32 && (p->flags & PIN_FLAG_REFERENCE) &&
                            p->binding && p->binding->kind == PIN_BINDING_VARIABLE;
            }
            if (!strcmp(p->name, "rhs")) {
                defaulted = p->type == PIN_I32 && !p->binding &&
                            p->default_value.type == PIN_I32 &&
                            p->default_value.i == 0;
            }
        }
        test_check(data == 2 && reference && defaulted, "Named action preserves both captures, writable reference and unbound zero default");
        behavior_instance *instance = behavior_instance_create(model, NULL, NULL, NULL);
        test_check(instance != NULL, "Defaulted action creates a runtime instance");
        if (instance) {
            flow_error failure = { 0 };
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &failure);
            const behavior_variable *kept = behavior_asset_find_variable_by_name(model, "kept");
            pin_value value = { 0 };
            behavior_instance_get_variable(instance, kept->id, &value);
            test_check(failure.code == 0 && value.type == PIN_I32 && value.i == 11, "Unbound action rhs adds zero instead of the removed source value seven");
            behavior_instance_destroy(instance);
        }
    }
    code_source_free(identity);
    free(canonical);
    code_source_free(reopened);
    free(clean);
    free(printed);
    code_source_free(initial);
}
static void explicit_component_owner_roundtrip()
{
    code_source *source = code_source_parse("behavior ExplicitOwner { on Begin { self.ParticleEmitter.Active = true; } }");
    test_check(source->asset != NULL, "Explicit owner topology fixture parses");
    if (!source->asset) {
        code_source_free(source);
        return;
    }
    behavior_asset *asset = source->asset;
    node *setter = NULL;
    for (int i = 0; i < acount(asset->graph->nodes); i++) {
        if (asset->graph->nodes[i]->desc == &NODE_SetComponent) {
            setter = asset->graph->nodes[i];
        }
    }
    node *owner = graph_add_node(asset->graph, &NODE_Self);
    graph_set_pin_type(asset->graph, owner, true, 0, PIN_HANDLE, "ecs.entity");
    int entity = -1;
    for (int i = 0; setter && i < setter->input_count; i++) {
        if (!strcmp(setter->input_pins[i].name, "Entity")) {
            entity = i;
        }
    }
    test_check(entity >= 0, "Explicit owner fixture finds the component entity input");
    test_check(!graph_connect_data(asset->graph, owner, 0, setter, entity), "Explicit owner fixture connects a typed entity edge");
    char *text = code_source_format(asset);
    test_check(
        text && strstr(text, " = self;") &&
            strstr(text, ".ParticleEmitter.Active = true;"),
        "An authored Self edge uses one readable owner alias"
    );
    code_source *copy = reopen_with_metadata(asset, text);
    test_check(copy->asset && behavior_asset_equal(asset, copy->asset), "Readable owner aliases preserve existing Self and component edge identities");
    if (!copy->asset || !behavior_asset_equal(asset, copy->asset)) {
        dump_models("explicit-component-owner", asset, copy->asset, text);
    }
    code_source_free(copy);
    free(text);
    code_source_free(source);
}

static void event_cross_callback()
{
    const char *sources[] = {
        "behavior CrossCallback { state elapsed: float = 0; }",
        "system CrossCallback { state elapsed: float = 0; }",
    };
    for (int i = 0; i < 2; i++) {
        code_source *initial = code_source_parse(sources[i]);
        test_check(initial->asset != NULL, "Cross-callback event fixture parses");
        if (!initial->asset) {
            code_source_free(initial);
            continue;
        }
        behavior_asset *asset = initial->asset;
        graph *macro = behavior_asset_add_macro(asset, "Continuation");
        node *entry = graph_node_by_id(macro, macro->entry_id);
        node *ret = graph_node_by_id(macro, macro->return_id);
        graph_set_pin_name(macro, ret, false, 0, "Out");
        graph_connect_exec(macro, entry, 0, ret, 0, 0);
        node *call = behavior_asset_add_macro_call_node(asset, macro);
        node *tick = NULL;
        for (int j = 0; j < acount(asset->graph->nodes); j++) {
            node *candidate = asset->graph->nodes[j];
            if (candidate->desc == &NODE_Event && candidate->reference_name &&
                !strcmp(candidate->reference_name, "Tick")) {
                tick = candidate;
                break;
            }
        }
        if (!tick) {
            tick = graph_add_node(asset->graph, &NODE_Event);
        }
        graph_set_event_name(asset->graph, tick, "Tick");
        graph_host_event_apply(asset->graph, tick);
        node *set = behavior_asset_add_set_node(
            asset,
            behavior_asset_find_variable_by_name(asset, "elapsed")->id
        );
        int delta = -1;
        for (int j = 0; j < tick->output_count; j++) {
            if (!strcmp(tick->output_pins[j].name, "DeltaSeconds")) {
                delta = j;
            }
        }
        test_check(delta >= 0, "Cross-callback fixture resolves Tick payload");
        graph_connect_exec(asset->graph, tick, 0, call, 0, 0);
        graph_connect_exec(asset->graph, call, 0, set, 0, 0);
        graph_connect_data(asset->graph, tick, delta, set, 1);
        char *text = code_source_format(asset);
        test_check(text && strstr(text, "after ") && strstr(text, ".dt"), "A separate continuation references its event payload through an explicit alias");
        code_source *copy = reopen_with_metadata(asset, text);
        test_check(copy->asset && behavior_asset_equal(asset, copy->asset), "Event payload edges and identity survive cross-callback source formatting");
        if (!copy->asset || !behavior_asset_equal(asset, copy->asset)) {
            dump_models("cross-callback-event", asset, copy->asset, text);
        }
        code_source_free(copy);
        free(text);
        code_source_free(initial);
    }
}

static void event_identity_format()
{
    code_source *source = code_source_parse("system EventIdentity { on Tick as tick() {} }");
    test_check(source->asset != NULL, "System event identity fixture parses");
    if (!source->asset) {
        code_source_free(source);
        return;
    }
    char error[256];
    char *printed = code_print_behavior(source->asset, error, sizeof error);
    test_check(printed && !strstr(printed, "@node"), "Native printer emits clean event declarations without machine identities");
    free(printed);
    char *formatted = code_source_format(source->asset);
    code_source *copy = reopen_with_metadata(source->asset, formatted);
    char *persisted = copy->asset ? code_source_persist(copy) : NULL;
    test_check(persisted && !strcmp(formatted, persisted), "Native event formatting and shared persistence produce identical clean source");
    test_check(
        copy->asset && behavior_asset_equal(source->asset, copy->asset),
        "Event identity formatting retains native graph state"
    );
    free(persisted);
    free(formatted);
    code_source_free(copy);
    code_source_free(source);
}
static void incomplete_callable_source()
{
    const int kinds[] = { GRAPH_FUNCTION, GRAPH_MACRO };
    for (int i = 0; i < 2; i++) {
        behavior_asset *asset = native_behavior("Incomplete");
        graph *g = behavior_asset_add_graph(asset, kinds[i], "Draft");
        char error[256];
        char *text = code_print_behavior(asset, error, sizeof error);
        test_check(!text && strstr(error, "Draft") && strstr(error, "Entry"), "Printer diagnoses incomplete callable instead of dereferencing a missing signature");
        free(text);
        node *entry = graph_add_node(g, &NODE_FunctionEntry);
        g->entry_id = entry->id;
        text = code_print_behavior(asset, error, sizeof error);
        test_check(!text && strstr(error, "Return"), "Printer diagnoses callable with Entry but no Return");
        free(text);
        node *result = graph_add_node(g, &NODE_Return);
        g->return_id = result->id;
        text = code_print_behavior(asset, error, sizeof error);
        test_check(text && !error[0], "Completing callable signature restores source serialization");
        free(text);
        behavior_asset_free(asset);
    }
}
static void input_fallback_sources()
{
    const char *clauses[] = {
        "binding: &state.latent, default: 7",
        "default: 7",
        "binding: &state.latent",
    };
    for (int variant = 0; variant < 3; variant++) {
        char text[512];
        snprintf(
            text,
            sizeof text,
            "behavior FallbackSources { state latent: int = 120; state "
            "observed: int = 0; "
            "value routed: int = reroute_data(In: int = "
            "fallback(integer(Value: 33), %s)); "
            "on Tick { set_variable<\"observed\">(Value: routed); } }",
            clauses[variant]
        );
        code_source *source = code_source_parse(text);
        test_check(source->asset != NULL, "Fallback source clauses compile with omitted binding or default variants");
        if (!source->asset) {
            code_source_free(source);
            continue;
        }
        char *canonical = code_source_format(source->asset);
        code_source *copy = reopen_with_metadata(source->asset, canonical);
        test_check(copy->asset && behavior_asset_equal(source->asset, copy->asset), "Fallback source clauses retain complete native identity through canonical source");
        if (!copy->asset || !behavior_asset_equal(source->asset, copy->asset)) {
            dump_models("input-fallback", source->asset, copy->asset, canonical);
        }
        behavior_asset *models[] = { source->asset, copy->asset };
        for (int side = 0; side < 2; side++) {
            behavior_asset *asset = models[side];
            if (!asset) {
                continue;
            }
            node *routed = NULL;
            for (int j = 0; j < acount(asset->graph->nodes); j++) {
                if (asset->graph->nodes[j]->desc == &NODE_RerouteData) {
                    routed = asset->graph->nodes[j];
                    break;
                }
            }
            node *driver = NULL;
            int output = -1;
            bool wired = routed &&
                         graph_input_source(asset->graph, routed, 0, &driver, &output);
            const pin_binding *binding = routed ? routed->input_pins[0].binding : NULL;
            test_check(
                wired && driver->desc == registry_node_by_name("Integer") &&
                    driver->input_pins[0].default_value.i == 33 &&
                    routed->input_pins[0].type == PIN_I32,
                "Fallback argument retains its integer driver and input type"
            );
            test_check(
                variant == 1
                    ? !binding
                    : binding && binding->kind == PIN_BINDING_VARIABLE &&
                          binding->variable_id ==
                              behavior_asset_find_variable_by_name(asset, "latent")
                                  ->id,
                "Fallback argument retains or omits its dormant state binding"
            );
            if (variant != 2) {
                test_check(
                    routed && routed->input_pins[0].default_value.type == PIN_I32 &&
                        routed->input_pins[0].default_value.i == 7,
                    "Fallback argument retains its dormant integer default"
                );
            }
            for (int stage = 0; stage < (variant ? 1 : 3); stage++) {
                if (stage == 1) {
                    test_check(graph_remove_edge(asset->graph, driver, output, routed, 0) == 0, "Removing the fallback driver exposes its dormant binding");
                }
                if (stage == 2) {
                    test_check(graph_set_pin_binding(asset->graph, routed, false, 0, NULL) == 0, "Removing the fallback binding exposes its dormant default");
                }
                behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
                test_check(instance != NULL, "Fallback source creates a runtime instance");
                if (instance) {
                    flow_error failure = { 0 };
                    behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &failure);
                    const behavior_variable
                        *observed = behavior_asset_find_variable_by_name(asset, "observed");
                    pin_value value = { 0 };
                    behavior_instance_get_variable(instance, observed->id, &value);
                    test_check(
                        failure.code == 0 && value.type == PIN_I32 &&
                            value.i == (stage == 0   ? 33
                                        : stage == 1 ? 120
                                                     : 7),
                        "Fallback evaluation follows driver, then binding, "
                        "then default precedence"
                    );
                    behavior_instance_destroy(instance);
                }
            }
        }
        code_source_free(copy);
        free(canonical);
        code_source_free(source);
    }
    const char *invalid[] = {
        "fallback(integer(Value: 33), binding: &state.latent, binding: "
        "&state.latent)",
        "fallback(integer(Value: 33), default: 7, default: 9)",
        "fallback(integer(Value: 33))",
        "fallback(binding: &state.latent, default: 7)",
        "fallback(integer(Value: 33), binding: &state.text)",
        "fallback(integer(Value: 33), default: \"seven\")",
    };
    for (int i = 0; i < 6; i++) {
        char text[640];
        snprintf(
            text,
            sizeof text,
            "behavior InvalidFallback { state latent: int = 120; state text: "
            "string = \"bad\"; "
            "value routed: int = reroute_data(In: int = %s); }",
            invalid[i]
        );
        code_source *source = code_source_parse(text);
        test_check(!source->asset && acount(source->diagnostics) > 0, "Public frontend rejects duplicate, incomplete and incompatible fallback clauses");
        code_source_free(source);
    }
}
static void cast_fallback_precedence()
{
    const char *values[] = { "cast<float>(fallback(real(Value:16777217.0), "
                             "binding:&state.latent, default:16777219.0))",
                             "fallback(cast<float>(real(Value:16777217.0)), "
                             "binding:&state.latent, default:16777219.0)" };
    for (int order = 0; order < 2; order++) {
        char text[640];
        snprintf(
            text,
            sizeof text,
            "behavior CastFallback { state latent:double=16777217; state "
            "observed:double=0; "
            "on Tick { set_variable<\"observed\">(Value:double=%s); } }",
            values[order]
        );
        code_source *source = code_source_parse(text);
        test_check(source->asset != NULL, "Both cast/fallback evaluation orders compile");
        if (!source->asset) {
            code_source_free(source);
            continue;
        }
        char *canonical = code_source_format(source->asset);
        int start, end;
        if (canonical && code_source_metadata_range(canonical, &start, &end)) {
            canonical[start] = 0;
        }
        code_source *reopened = canonical ? code_source_parse(canonical) : NULL;
        test_check(reopened && reopened->asset, "Cast/fallback precedence reconstructs without authoring metadata");
        for (int version = 0; version < 2; version++) {
            code_source *current = version ? reopened : source;
            if (!current || !current->asset) {
                continue;
            }
            behavior_asset *asset = code_source_build_behavior(current);
            const behavior_variable *observed = behavior_asset_find_variable_by_name(asset, "observed");
            node *setter = NULL;
            int input = -1;
            for (int i = 0; i < acount(asset->graph->nodes); i++) {
                node *n = asset->graph->nodes[i];
                if (!strcmp(n->desc->name, "Set Variable") &&
                    n->reference_id == observed->id) {
                    setter = n;
                    for (int pin = 0; pin < n->input_count; pin++) {
                        if (n->input_pins[pin].kind == PIN_DATA) {
                            input = pin;
                        }
                    }
                }
            }
            node *boundary = NULL;
            int boundary_pin = -1;
            bool wired = setter && input >= 0 &&
                         graph_input_source(asset->graph, setter, input, &boundary, &boundary_pin);
            node *fallback_node = order ? setter : boundary;
            int fallback_pin = order ? input : 0;
            node *primary = NULL;
            int primary_pin = -1;
            bool shaped = wired && fallback_node &&
                          graph_input_source(asset->graph, fallback_node, fallback_pin, &primary, &primary_pin) &&
                          fallback_node->input_pins[fallback_pin].binding &&
                          fallback_node->input_pins[fallback_pin].type ==
                              (order ? PIN_F64 : PIN_F32);
            test_check(shaped, "Fallback clauses stay on the input inside or outside the requested float boundary");
            if (shaped) {
                for (int stage = 0; stage < 3; stage++) {
                    if (stage == 1) {
                        test_check(!graph_remove_edge(asset->graph, primary, primary_pin, fallback_node, fallback_pin), "Removing the primary driver exposes the fallback at its authored boundary");
                    }
                    if (stage == 2) {
                        test_check(!graph_set_pin_binding(asset->graph, fallback_node, false, fallback_pin, NULL), "Removing the fallback binding exposes its default at the same boundary");
                    }
                    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
                    flow_error error = { 0 };
                    pin_value result = { 0 };
                    if (instance) {
                        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
                        behavior_instance_get_variable(instance, observed->id, &result);
                    }
                    double expected = stage == 0 ? 16777216.0
                                    : order
                                        ? (stage == 1 ? 16777217.0 : 16777219.0)
                                        : (stage == 1 ? 16777216.0 : 16777220.0);
                    test_check(
                        instance && !error.code && result.type == PIN_F64 &&
                            result.d == expected,
                        "Cast/fallback ordering retains float rounding through "
                        "driver, binding and default reads"
                    );
                    behavior_instance_destroy(instance);
                }
            }
            behavior_asset_free(asset);
        }
        code_source_free(reopened);
        free(canonical);
        code_source_free(source);
    }
}

static void source_trivia()
{
    const char *text = "// Authored explanation\nbehavior \"Counter\" {\n"
                       "    state count: int = 1; // Default note\n"
                       "    on Tick as tick { &state.count += 1; }\n}\n";
    code_source *source = code_source_parse(text);
    test_check(source->asset && source->program, "Readable program is available for source reconciliation");
    if (!source->asset) {
        code_source_free(source);
        return;
    }
    behavior_asset *edited = code_source_build_behavior(source);
    edited->graph->nodes[0]->x = 900;
    edited->graph->nodes[0]->placed = true;
    char error[256];
    char *same = code_program_reconcile(source, edited, error, sizeof error);
    test_check(same && !strcmp(same, text), "Layout-only reconciliation retains exact authored bytes");
    free(same);
    behavior_variable *count = behavior_asset_find_variable_by_name(edited, "count");
    behavior_asset_set_variable_default(edited, count->id, &(pin_value){ .type = PIN_I32, .i = 3 });
    char *changed = code_program_reconcile(source, edited, error, sizeof error);
    test_check(
        changed && strstr(changed, "count: int = 3; // Default note") &&
            strstr(changed, "// Authored explanation"),
        "State-default reconciliation changes the literal and preserves its "
        "comments"
    );
    free(changed);
    behavior_asset_free(edited);
    code_source_free(source);
}
static void local_callable_width()
{
    code_source *source = code_source_parse(
        "behavior LocalWidth { state narrow:float=0; state wide:double=0; "
        "value precision=Precision(); local retained=precision; "
        "on Tick { set_variable<\"narrow\">(Value:&local.retained); "
        "set_variable<\"wide\">(Value:&local.retained); } "
        "pure function Precision<T:double>() -> (Result:T=0) { "
        "result Result:double=16777217.0; } }"
    );
    test_check(source->asset != NULL, "Local callable precision fixture compiles");
    if (!source->asset) {
        code_source_free(source);
        return;
    }
    char error[256];
    char *text = code_print_behavior(source->asset, error, sizeof error);
    code_source *reopened = text ? code_source_parse(text) : NULL;
    test_check(reopened && reopened->asset, "Local callable precision source reopens");
    behavior_asset *models[] = { source->asset, reopened ? reopened->asset : NULL };
    for (int side = 0; side < 2; side++) {
        behavior_asset *asset = models[side];
        if (!asset) {
            continue;
        }
        graph *target = behavior_asset_find_graph_by_name(asset, "Precision");
        node *ret = target ? graph_node_by_id(target, target->return_id) : NULL;
        bool wide_return = false, wide_call = false;
        for (int i = 0; ret && i < ret->input_count; i++) {
            wide_return |= ret->input_pins[i].kind == PIN_DATA &&
                           ret->input_pins[i].type == PIN_F64;
        }
        for (int i = 0; i < acount(asset->graph->nodes); i++) {
            node *call = asset->graph->nodes[i];
            if (!target || call->reference_id != target->graph_id) {
                continue;
            }
            for (int j = 0; j < call->output_count; j++) {
                wide_call |= call->output_pins[j].kind == PIN_DATA &&
                             call->output_pins[j].type == PIN_F64;
            }
        }
        test_check(wide_return && wide_call, "Float local consumer preserves the callable double result ABI");
        behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
        test_check(instance != NULL, "Local callable precision creates a runtime instance");
        if (!instance) {
            continue;
        }
        flow_error failure = { 0 };
        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &failure);
        pin_value narrow = { 0 }, wide = { 0 };
        behavior_instance_get_variable(
            instance,
            behavior_asset_find_variable_by_name(asset, "narrow")->id,
            &narrow
        );
        behavior_instance_get_variable(
            instance,
            behavior_asset_find_variable_by_name(asset, "wide")->id,
            &wide
        );
        test_check(
            failure.code == 0 && narrow.type == PIN_F32 && narrow.f == 16777216 &&
                wide.type == PIN_F64 && wide.d == 16777217,
            "Local consumers convert independently without narrowing their "
            "shared producer"
        );
        behavior_instance_destroy(instance);
    }
    code_source_free(reopened);
    free(text);
    code_source_free(source);
}

void test_code_print()
{
    branch_chain();
    branch_expression_identity();
    polymorphic_local();
    shared_gate();
    contingent_outcome();
    iterator_loop();
    float_rounding();
    double_to_float_boundary();
    same_descriptor_identity(false, 0);
    same_descriptor_identity(false, 7);
    same_descriptor_identity(true, 0);
    computed_result_boundary();
    cast_comparison_loops();
    lazy_helper_sharing();
    shared_expression_callable();
    expression_constant_width();
    action_result_width();
    typed_native_math_call();
    known_double_operand();
    component_alias_capture();
    vector_cross_result_boundary();
    vector_atan2_captures();
    expression_grouping();
    configured_struct_literal();
    shaped_struct_template();
    named_expression_capture();
    defaulted_action_capture();
    event_identity_format();
    event_cross_callback();
    explicit_component_owner_roundtrip();
    incomplete_callable_source();
    input_fallback_sources();
    cast_fallback_precedence();
    source_trivia();
    local_callable_width();
}
