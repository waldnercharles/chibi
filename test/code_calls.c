#include "assets/code_calls.h"

#include "assets/code_source_context.h"
#include "graph/authoring/pin_edit.h"
#include "graph/document/graph_signature.h"
#include "graph/document/serialize.h"
#include "graph/model/local.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/runtime/value_array.h"
#include "support/array.h"

#include <string.h>

void test_check(int ok, const char *message);

static void borrowed_signature_lifetime(bool macro)
{
    behavior_asset *catalog = behavior_asset_new("Borrowed");
    graph *function = macro ? behavior_asset_add_macro(catalog, "Echo")
                            : behavior_asset_add_function(catalog, "Echo", true);
    pin_spec label = { .name = "Label",
                       .type = PIN_STR,
                       .kind = PIN_DATA,
                       .type_name = graph_own_str(function, "string", 6),
                       .default_value = { .type = PIN_STR, .str = "catalog default" } };
    graph_signature_add(catalog, function, false, &label);
    node *entry = graph_node_by_id(function, function->entry_id);
    graph_set_pin_default(function, entry, true, entry->output_count - 1, &label.default_value);
    pin_array *numbers = pin_array_new(PIN_I32, NULL, 2);
    pin_value seven = { .type = PIN_I32, .i = 7 }, nine = { .type = PIN_I32, .i = 9 };
    pin_array_push(numbers, &seven);
    pin_array_push(numbers, &nine);
    pin_spec array = { .name = "Numbers",
                       .type = PIN_I32,
                       .kind = PIN_DATA,
                       .container = PIN_CONTAINER_ARRAY,
                       .default_value = { .type = PIN_I32,
                                          .container = PIN_CONTAINER_ARRAY,
                                          .ptr = numbers } };
    graph_signature_add(catalog, function, false, &array);
    graph_set_pin_default(function, entry, true, entry->output_count - 1, &array.default_value);
    pin_array_free(numbers);
    pin_spec result = { .name = "Entity",
                        .type = PIN_HANDLE,
                        .kind = PIN_DATA,
                        .type_name = graph_own_str(function, "ecs.entity", 10),
                        .default_value = { .type = PIN_HANDLE } };
    graph_signature_add(catalog, function, true, &result);
    behavior_asset *candidate = behavior_asset_new("Independent");
    code_call_spec spec = { .name = macro ? "call_macro" : "call_pure_function",
                            .target = function,
                            .reference_asset = "Borrowed" };
    char error[256];
    node *call = code_call_build(candidate, candidate->graph, &spec, error, sizeof error);
    test_check(call != NULL, "Borrowed signature constructs an independent candidate");
    int label_index = call ? code_call_port_index(call, false, "Label", error, sizeof error)
                           : -1;
    int array_index = call ? code_call_port_index(call, false, "Numbers", error, sizeof error)
                           : -1;
    int result_index = call ? code_call_port_index(call, true, "Entity", error, sizeof error)
                            : -1;
    bool shape = label_index >= 0 && array_index >= 0 && result_index >= 0;
    uint64_t input_id = shape ? call->input_pins[label_index].id : 0;
    uint64_t output_id = shape ? call->output_pins[result_index].id : 0;
    unsigned flags = shape ? call->input_pins[label_index].flags : 0;
    behavior_asset_free(catalog);
    test_check(
        shape && !strcmp(call->input_pins[label_index].default_value.str, "catalog default") &&
            !strcmp(call->input_pins[label_index].type_name, "string") &&
            !strcmp(call->output_pins[result_index].type_name, "ecs.entity") &&
            call->input_pins[label_index].id == input_id &&
            call->output_pins[result_index].id == output_id &&
            call->input_pins[label_index].flags == flags,
        "Candidate signature retains owned nominal types and string defaults "
        "after catalog destruction"
    );
    pin_array *copy = shape ? call->input_pins[array_index].default_value.ptr : NULL;
    test_check(
        copy && copy->count == 2 && copy->items[0].i == 7 && copy->items[1].i == 9,
        "Candidate signature array defaults survive destruction of the "
        "borrowed catalog"
    );
    behavior_asset_free(candidate);
}

static void typed_expression_signature()
{
    behavior_asset *asset = behavior_asset_new("TypedExpression");
    code_call_port input = { .name = "x",
                             .type = PIN_F32,
                             .type_name = "float",
                             .kind = PIN_DATA,
                             .has_default = true,
                             .default_value = { .type = PIN_F32, .f = 2 } };
    code_call_spec call = { .name = "behavior_expression",
                            .expression = "x + 1",
                            .expression_mode = GRAPH_EXPRESSION_VALUE,
                            .inputs = &input,
                            .input_count = 1 };
    char error[256] = { 0 };
    node *n = code_call_build(asset, asset->graph, &call, error, sizeof error);
    graph *body = n ? behavior_asset_find_graph(asset, n->reference_id) : NULL;
    node *entry = body ? graph_node_by_id(body, body->entry_id) : NULL;
    int parameter = entry ? code_call_port_index(entry, true, "x", error, sizeof error)
                          : -1;
    int argument = n ? code_call_port_index(n, false, "x", error, sizeof error) : -1;
    test_check(
        parameter >= 0 && argument >= 0 && entry->output_pins[parameter].type == PIN_F32 &&
            n->input_pins[argument].type == PIN_F32 &&
            body->expression_mode == GRAPH_EXPRESSION_VALUE,
        "Typed native expression configuration refreshes the generated "
        "function ABI after argument authoring"
    );
    behavior_asset_free(asset);
}
static void compact_run_snapshot()
{
    behavior_asset *asset = behavior_asset_new("CompactRuns");
    graph *g = asset->graph;
    node *selection = graph_add_node(g, &NODE_SwitchEnum);
    graph_remove_pin(g, selection, true, 0);
    graph_remove_pin(g, selection, true, 0);
    node *sequence = graph_add_node(g, &NODE_Sequence);
    graph_remove_pin(g, sequence, true, 1);
    graph_connect_exec(g, selection, 0, sequence, 0, 7);
    code_call_port result = { .name = "Array",
                              .type = PIN_I32,
                              .container = PIN_CONTAINER_ARRAY,
                              .kind = PIN_DATA };
    code_call_spec call = { .name = "make_array", .results = &result, .result_count = 1 };
    char error[256] = { 0 };
    node *array = code_call_build(asset, g, &call, error, sizeof error);
    av_doc *doc = av_doc_new();
    behavior_asset *reopened = behavior_asset_from_value(
        behavior_asset_to_value(doc, asset)
    );
    test_check(array && reopened && behavior_asset_equal(asset, reopened), "Native snapshots retain empty data runs, default-only switches and unary sequences with ordered execution edges");
    behavior_asset_free(reopened);
    av_doc_free(doc);
    behavior_asset_free(asset);
}
static void converted_local_component_field(bool fallback)
{
    behavior_asset *asset = behavior_asset_new("LocalComponentField");
    graph *g = asset->graph;
    node *producer = graph_add_node(g, registry_node_by_name("Real"));
    pin_value value = { .type = PIN_F64, .d = 42.9 };
    graph_set_pin_default(g, producer, false, 0, &value);
    uint64_t local = graph_declare_local(g, producer, 0, "Remaining");
    code_call_argument args[] = {
        { .name = "Component",
          .kind = CODE_CALL_LITERAL,
          .literal = { .type = PIN_NAME, .str = "GAME.GameHealth" } },
        { .name = "Value.current",
          .kind = CODE_CALL_BINDING,
          .binding = { .kind = PIN_BINDING_LOCAL, .local_id = local },
          .conversion = registry_node_by_name("To Integer") },
    };
    if (fallback) {
        args[1].kind = CODE_CALL_OUTPUT;
        args[1].source = (code_call_output){ .node = producer, .pin = 0 };
        args[1].has_fallback_binding = true;
        args[1].fallback_binding = args[1].binding;
        args[1].has_fallback_default = true;
        args[1].fallback_default = (pin_value){ .type = PIN_I32, .i = 7 };
    }
    code_call_port field = { .name = "Value.current", .type = PIN_I32, .kind = PIN_DATA };
    code_call_spec spec = { .name = "set_component",
                            .arguments = args,
                            .argument_count = 2,
                            .inputs = &field,
                            .input_count = 1 };
    char error[256];
    node *setter = code_call_build(asset, g, &spec, error, sizeof error);
    int input = setter
                  ? code_call_port_index(setter, false, "current", error, sizeof error)
                  : -1;
    node *source = NULL;
    int output = -1;
    test_check(
        input >= 0 && graph_local_by_id(g, local) && setter->input_pins[input].binding &&
            setter->input_pins[input].binding->local_id == local &&
            graph_input_source(g, setter, input, &source, &output) &&
            source == producer && output == 0 &&
            graph_input_conversion(g, setter, input) == args[1].conversion &&
            producer->output_pins[0].type == PIN_F64 &&
            producer->input_pins[0].default_value.d == 42.9 &&
            !graph_validate_locals(g) &&
            (!fallback || setter->input_pins[input].default_value.i == 7),
        fallback ? "Shaping a component field preserves a converted output's "
                   "fallback local and default"
                 : "Shaping a component field preserves its converted local "
                   "producer and binding"
    );
    if (fallback && input >= 0) {
        test_check(
            graph_expand_local(g, local) == 0 && !setter->input_pins[input].binding &&
                graph_input_source(g, setter, input, &source, &output) &&
                source == producer &&
                graph_input_conversion(g, setter, input) == args[1].conversion &&
                setter->input_pins[input].default_value.i == 7,
            "Expanding the fallback local preserves its numeric driver, "
            "conversion, and default"
        );
    }
    behavior_asset_free(asset);
}

static void typed_literal_boundaries()
{
    const char *invalid[] = {
        "behavior B { state count: int = 2147483648; }",
        "behavior B { action v = set_component(Component: "
        "\"GAME.GameAppearance\", Value.shape: int = 2147483648); }",
        "behavior B { action v = set_component(Component: \"GAME.GameHealth\", "
        "Value.bogus: int = 1); }",
    };
    const char *messages[] = { "outside int storage range",
                               "outside int storage range",
                               "Unknown component field 'bogus'" };
    for (int i = 0; i < 3; i++) {
        behavior_asset *asset = behavior_asset_load(invalid[i]);
        test_check(!asset && strstr(graph_last_error(), messages[i]), "Invalid typed literal or component field reports its actual storage or member error");
        behavior_asset_free(asset);
    }
    behavior_asset *limits = behavior_asset_load("behavior B { state low: int = -2147483648; state high: int = 2147483647; }");
    test_check(
        limits && limits->variables[0].default_value.i == INT32_MIN &&
            limits->variables[1].default_value.i == INT32_MAX,
        "Signed integer literal boundaries retain exact values"
    );
    behavior_asset_free(limits);
    behavior_asset *setting = behavior_asset_load("behavior B { action v = set_component(Component: name = \"GAME.GameHealth\", Value.current: int = 4); }");
    test_check(setting != NULL, "Matching explicit setting type validates without mutating its native setting contract");
    behavior_asset_free(setting);
}

void run_code_call_checks()
{
    typed_literal_boundaries();
    converted_local_component_field(false);
    converted_local_component_field(true);
    compact_run_snapshot();
    typed_expression_signature();
    borrowed_signature_lifetime(false);
    borrowed_signature_lifetime(true);
    char error[256] = { 0 };
    int reversible = 0;
    bool exact = true;
    for (int i = 0; i < registry_node_count(); i++) {
        const node_desc *desc = registry_node(i);
        char name[256];
        if (code_call_name(desc, name, sizeof name)) {
            reversible += code_call_descriptor(name, error, sizeof error) == desc;
        }
        exact &= code_call_descriptor(desc->name, error, sizeof error) == desc;
    }
    test_check(exact, "Exact callable spelling preserves each registered native descriptor");
    test_check(reversible > 0, "Native callable aliases resolve reversibly");
    test_check(
        !code_call_descriptor("normalize_vector_2", error, sizeof error) &&
            !code_call_descriptor("spawn_prototype", error, sizeof error),
        "Obsolete verbose intrinsic source aliases are removed"
    );
    test_check(
        code_call_descriptor("vec2_norm", error, sizeof error) ==
                registry_node_by_name("Normalize Vector 2") &&
            code_call_descriptor("spawn", error, sizeof error) ==
                registry_node_by_name("Spawn Prototype"),
        "Concise intrinsic names resolve to the existing optimized operations"
    );
    test_check(
        !code_call_descriptor("unregistered_callable", error, sizeof error),
        "Unknown callable aliases fail before construction"
    );

    behavior_asset *asset = behavior_asset_new("Calls");
    graph *g = asset->graph;
    code_call_port outputs[] = {
        { .name = "A", .type = PIN_FLOW, .kind = PIN_EXEC },
        { .name = "B", .type = PIN_FLOW, .kind = PIN_EXEC },
        { .name = "C", .type = PIN_FLOW, .kind = PIN_EXEC },
    };
    code_call_spec sequence = { .name = "sequence",
                                .results = outputs,
                                .result_count = 3,
                                .complete_results = true };
    node *n = code_call_build(asset, g, &sequence, error, sizeof error);
    test_check(
        n && n->output_count == 3 && !strcmp(n->output_pins[0].name, "A") &&
            !strcmp(n->output_pins[2].name, "C"),
        "Dynamic execution results retain their named declaration order"
    );
    code_call_output selected;
    test_check(
        n && code_call_resolve_output(g, n, "B", &selected, error, sizeof error) &&
            selected.pin == 1,
        "Shared actions select a named execution continuation"
    );
    test_check(
        n && !code_call_resolve_output(g, n, "", &selected, error, sizeof error),
        "Native calls never infer an unnamed first output"
    );

    pin_value initial = { .type = PIN_I32, .i = 4 };
    behavior_variable *state = behavior_asset_add_variable(
        asset,
        "count",
        PIN_I32,
        NULL,
        PIN_CONTAINER_SINGLE,
        &initial,
        NULL
    );
    code_call_spec getter = { .name = "get_variable", .reference = "count" };
    node *read = code_call_build(asset, g, &getter, error, sizeof error);
    test_check(
        read && state && read->reference_id == state->id &&
            read->output_pins[0].type == PIN_I32,
        "Variable calls resolve typed stable declarations"
    );
    code_call_argument amount = { .name = "value",
                                  .kind = CODE_CALL_LITERAL,
                                  .literal = initial };
    code_call_spec setter = { .name = "set_variable",
                              .reference = "count",
                              .arguments = &amount,
                              .argument_count = 1 };
    node *write = code_call_build(asset, g, &setter, error, sizeof error);
    test_check(write && write->input_pins[1].default_value.i == 4, "Named native arguments configure typed variable writes");

    behavior_asset *library = behavior_asset_new("Library");
    graph *function = behavior_asset_add_function(library, "Result", 1);
    pin_spec result = { .name = "Answer",
                        .type = PIN_I32,
                        .kind = PIN_DATA,
                        .default_value = { .type = PIN_I32 } };
    graph_signature_add(library, function, true, &result);
    node *function_return = graph_node_by_id(function, function->return_id);
    pin_value body_constant = { .type = PIN_I32, .i = 7 };
    graph_set_pin_default(function, function_return, false, 0, &body_constant);
    code_call_spec call = { .name = "call_pure_function",
                            .target = function,
                            .reference_asset = "Library" };
    node *external = code_call_build(asset, g, &call, error, sizeof error);
    test_check(
        external && external->reference_id == function->graph_id &&
            external->reference_asset && !strcmp(external->reference_asset, "Library") &&
            code_call_resolve_output(g, external, "answer", &selected, error, sizeof error),
        "External function calls use the native named result signature"
    );
    test_check(external && external->output_pins[0].default_value.i == 0, "Callable result defaults do not leak a borrowed function's executable Return constant");
    graph *macro = behavior_asset_add_macro(asset, "Branches");
    code_call_spec macro_call = { .name = "call_macro", .target = macro };
    node *expanded = code_call_build(asset, g, &macro_call, error, sizeof error);
    test_check(
        expanded &&
            expanded->input_count == graph_node_by_id(macro, macro->entry_id)->output_count &&
            expanded->output_count == graph_node_by_id(macro, macro->return_id)->input_count,
        "Macro calls preserve named execution tunnel signatures"
    );

    code_call_spec entry_call = { .name = "function_entry" };
    node *entry = graph_node_by_id(function, function->entry_id);
    test_check(
        code_call_build(library, function, &entry_call, error, sizeof error) == entry,
        "Native function entry invocation reuses its declared signature node"
    );

    code_call_argument component_type = {
        .name = "component",
        .kind = CODE_CALL_LITERAL,
        .literal = { .type = PIN_NAME, .str = "GameTransform" }
    };
    code_call_spec component_read = { .name = "get_component",
                                      .arguments = &component_type,
                                      .argument_count = 1 };
    node *component = code_call_build(asset, g, &component_read, error, sizeof error);
    code_call_output x, y;
    test_check(
        component &&
            code_call_resolve_output(g, component, "value.x", &x, error, sizeof error) &&
            code_call_resolve_output(g, component, "value.y", &y, error, sizeof error) &&
            x.pin != y.pin &&
            component->output_pins[x.pin].field_source ==
                component->output_pins[0].id,
        "Named aggregate output projections retain their native source "
        "relationship"
    );
    code_call_argument fields[] = {
        component_type,
        { .name = "value.x",
          .kind = CODE_CALL_LITERAL,
          .literal = { .type = PIN_F32, .f = 3 } },
        { .name = "value.y",
          .kind = CODE_CALL_LITERAL,
          .literal = { .type = PIN_F32, .f = 7 } },
    };
    code_call_port field_schema[] = {
        { .name = "Value.x", .type = PIN_F32, .type_name = "float", .kind = PIN_DATA },
        { .name = "Value.y", .type = PIN_F32, .type_name = "float", .kind = PIN_DATA },
    };
    code_call_spec component_write = { .name = "set_component",
                                       .arguments = fields,
                                       .argument_count = 3,
                                       .inputs = field_schema,
                                       .input_count = 2 };
    node *field_write = code_call_build(asset, g, &component_write, error, sizeof error);
    bool configured_fields = false;
    if (field_write) {
        int matching = 0;
        for (int i = 0; i < field_write->input_count; i++) {
            pin_spec *pin = &field_write->input_pins[i];
            matching += (!strcmp(pin->name, "x") && pin->default_value.f == 3) ||
                        (!strcmp(pin->name, "y") && pin->default_value.f == 7);
        }
        configured_fields = matching == 2;
    }
    test_check(configured_fields, "Named component writes configure multiple fields after removing their aggregate input");

    code_call_port literal_result = { .name = "value",
                                      .type = PIN_I32,
                                      .kind = PIN_DATA,
                                      .has_default = true,
                                      .default_value = { .type = PIN_I32, .i = 19 } };
    code_call_spec literal = { .name = "integer",
                               .results = &literal_result,
                               .result_count = 1 };
    node *constant = code_call_build(asset, g, &literal, error, sizeof error);
    test_check(constant && constant->output_pins[0].default_value.i == 19, "Native literal outputs carry typed authored values");

    const behavior_asset *catalog[] = { library };
    code_source_context context = { .libraries = catalog, .library_count = 1 };
    const char *owner = NULL;
    test_check(
        code_source_context_graph(&context, asset, "Library", "Result", GRAPH_FUNCTION, &owner, error, sizeof error) ==
                function &&
            owner && !strcmp(owner, "Library"),
        "Source context resolves external signatures from a borrowed catalog"
    );
    graph *local_result = behavior_asset_add_function(asset, "Result", 1);
    test_check(
        code_source_context_graph(&context, asset, NULL, "Result", GRAPH_FUNCTION, &owner, error, sizeof error) ==
                local_result &&
            !owner,
        "Local declarations take precedence over unqualified library callables"
    );
    behavior_asset *second_library = behavior_asset_new("OtherLibrary");
    behavior_asset_add_function(second_library, "Result", 1);
    const behavior_asset *ambiguous[] = { library, second_library };
    context.libraries = ambiguous;
    context.library_count = 2;
    test_check(
        !code_source_context_graph(&context, NULL, NULL, "Result", GRAPH_FUNCTION, &owner, error, sizeof error),
        "Unqualified ambiguous external signatures are rejected"
    );
    test_check(
        code_source_context_graph(&context, NULL, "Library", "Result", GRAPH_FUNCTION, &owner, error, sizeof error) ==
            function,
        "Explicit library qualification resolves ambiguous external names"
    );
    behavior_asset *event_definition = behavior_asset_new("Damage");
    event_definition->domain = BEHAVIOR_DOMAIN_EVENT_DEFINITION;
    const behavior_asset *events[] = { event_definition, event_definition };
    context.events = events;
    context.event_count = 1;
    test_check(
        code_source_context_event(&context, "Damage", error, sizeof error) == event_definition,
        "Source context borrows exact native event definitions"
    );
    context.event_count = 2;
    test_check(
        !code_source_context_event(&context, "Damage", error, sizeof error),
        "Ambiguous event catalogs cannot silently select a definition"
    );
    behavior_asset_free(event_definition);
    behavior_asset_free(second_library);

    code_call_port array_result = { .name = "Array",
                                    .type = PIN_I32,
                                    .container = PIN_CONTAINER_ARRAY,
                                    .kind = PIN_DATA };
    code_call_spec empty_array = { .name = "make_array",
                                   .results = &array_result,
                                   .result_count = 1 };
    node *empty = code_call_build(asset, g, &empty_array, error, sizeof error);
    test_check(
        empty && empty->input_count == 0 && empty->output_pins[0].type == PIN_I32 &&
            empty->output_pins[0].container == PIN_CONTAINER_ARRAY,
        "An empty native data run has zero authored items and retains its "
        "result element type"
    );

    code_call_argument repeated[] = { amount, amount };
    setter.arguments = repeated;
    setter.argument_count = 2;
    test_check(!code_call_build(asset, g, &setter, error, sizeof error), "Repeated named arguments are refused");
    behavior_asset_free(library);
    behavior_asset_free(asset);
}
