#include "ecs/ecs.h"
#include "editor/workspace/editor.h"
#include "graph/compiler/compile.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/value_array.h"
#include "runtime/components.h"
#include "runtime/generated.h"
#include "runtime/systems/system.h"
#include "support/arena.h"

#include <assert.h>
#include <cute_defines.h>
#define CF_MATH_H
#undef CF_MATH_H
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double evaluate(behavior_asset *asset, node *call)
{
    compile_report report;
    compile_report_init(&report);
    assert(behavior_asset_compile(asset, &report) == 0);
    compile_report_free(&report);
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(instance);
    eval_context context;
    eval_context_init(&context);
    context.instance = instance;
    arena memory;
    arena_init(&memory, 4096);
    pin_value output;
    int result = graph_eval_output(asset->graph, call, 0, &memory, &output, &context);
    if (result != 0) {
        printf(
            "expression evaluation failed: %s\n",
            eval_context_status(&context)->message
        );
    }
    assert(result == 0 && (output.type == PIN_F32 || output.type == PIN_F64));
    double value = output.type == PIN_F32 ? output.f : output.d;
    eval_context_free(&context);
    arena_free(&memory);
    behavior_instance_destroy(instance);
    return value;
}

static void expect(const char *source, double expected)
{
    behavior_asset *asset = behavior_asset_new("Math test");
    behavior_expression_error error;
    node *call = behavior_expression_add(asset, asset->graph, source, &error);
    if (!call) {
        printf(
            "expression parse failed: %s at %d: %s\n",
            source,
            error.offset,
            error.message
        );
    }
    assert(call);
    double value = evaluate(asset, call);
    if (fabs(value - expected) >= 1e-10) {
        fprintf(stderr, "Math result: %s = %.17g, expected %.17g\n", source, value, expected);
    }
    assert(fabs(value - expected) < 1e-10);
    behavior_asset_free(asset);
}

static node *literal(graph *g, double value)
{
    node *n = graph_add_node(g, registry_node_by_name("Real"));
    assert(
        n && graph_set_pin_default(g, n, 0, 0, &(pin_value){ .type = PIN_F64, .d = value }) == 0
    );
    return n;
}

static node *math_node(graph *g, const char *name)
{
    node *n = graph_add_node(g, registry_node_by_name(name));
    assert(n);
    return n;
}

static void selection_conversion()
{
    behavior_asset *asset = behavior_asset_new("Selection conversion");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    graph *g = asset->graph;
    node *input = literal(g, 4.0);
    node *subtract = math_node(g, "Subtract");
    node *multiply = math_node(g, "Multiply");
    node *add = math_node(g, "Add");
    node *sink = math_node(g, "Add");
    assert(graph_connect_data(g, input, 0, subtract, 0) == 0);
    assert(graph_set_pin_type(g, subtract, 0, 1, PIN_F32, "float") == 0);
    pin_binding binding = { .kind = PIN_BINDING_COMPONENT_FIELD,
                            .component_key = GAME_COMPONENT_GameTransform.key,
                            .field_name = "x" };
    assert(graph_set_pin_binding(g, subtract, 0, 1, &binding) == 0);
    assert(graph_connect_data(g, subtract, 0, multiply, 0) == 0);
    assert(
        graph_set_pin_default(g, multiply, 0, 1, &(pin_value){ .type = PIN_F32, .f = 0.5f }) == 0
    );
    assert(graph_connect_data(g, multiply, 0, add, 0) == 0);
    assert(graph_set_pin_binding(g, add, 0, 1, &binding) == 0);
    assert(graph_connect_data(g, add, 0, sink, 0) == 0);

    node *selection[] = { subtract, multiply, add };
    uint64_t subtract_id = subtract->id;
    uint64_t multiply_id = multiply->id;
    uint64_t add_id = add->id;
    assert(behavior_expression_can_convert_selection(asset, g, selection, 3) == NULL);
    behavior_expression_error error;
    node *call = behavior_expression_convert_selection(asset, g, selection, 3, &error);
    if (!call) {
        fprintf(stderr, "selection conversion failed: %s\n", error.message);
    }
    assert(call);
    assert(!graph_node_by_id(g, subtract_id));
    assert(!graph_node_by_id(g, multiply_id));
    assert(!graph_node_by_id(g, add_id));
    assert(strstr(behavior_expression_source(asset, call), "game_GameTransform_x"));
    assert(strstr(behavior_expression_source(asset, call), "0.5"));
    int bound = -1;
    int wired = -1;
    for (int i = 0; i < call->input_count; i++) {
        if (call->input_pins[i].binding) {
            bound = i;
        } else if (graph_input_source(g, call, i, &input, &wired)) {
            assert(input->desc == registry_node_by_name("Real"));
        }
    }
    assert(bound >= 0);
    assert(call->input_pins[bound].binding->kind == PIN_BINDING_COMPONENT_FIELD);
    node *source = NULL;
    int source_pin = -1;
    assert(graph_input_source(g, sink, 0, &source, &source_pin));
    assert(source == call && source_pin == 0);
    behavior_asset_free(asset);
}

static void selected_struct_conversion()
{
    behavior_asset *asset = behavior_asset_new("Structured selection conversion");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    graph *g = asset->graph;
    node *camera = math_node(g, "Make_GameTransform");
    node *target = math_node(g, "Make_GameTransform");
    node *alpha = literal(g, 0.5);
    node *subtract = math_node(g, "Subtract");
    node *multiply = math_node(g, "Multiply");
    node *add = math_node(g, "Add");
    node *sink = math_node(g, "Set_GameTransform");
    assert(graph_connect_data(g, camera, 0, subtract, 0) == 0);
    assert(graph_connect_data(g, target, 0, subtract, 1) == 0);
    assert(graph_connect_data(g, subtract, 0, multiply, 0) == 0);
    assert(graph_connect_data(g, alpha, 0, multiply, 1) == 0);
    assert(graph_connect_data(g, camera, 0, add, 0) == 0);
    assert(graph_connect_data(g, multiply, 0, add, 1) == 0);
    assert(graph_connect_data(g, add, 0, sink, 1) == 0);

    node *selection[] = { subtract, multiply, add };
    const char *reason = behavior_expression_can_convert_selection(asset, g, selection, 3);
    if (reason) {
        fprintf(stderr, "structured selection rejected: %s\n", reason);
    }
    assert(!reason);
    behavior_expression_error error;
    node *call = behavior_expression_convert_selection(asset, g, selection, 3, &error);
    if (!call) {
        fprintf(stderr, "structured selection conversion failed: %s\n", error.message);
    }
    assert(call);
    assert(call->output_pins[0].type == PIN_STRUCT);
    assert(!strcmp(call->output_pins[0].type_name, "GameTransform"));
    assert(strstr(behavior_expression_source(asset, call), "GameTransform"));
    node *source = NULL;
    int source_pin = -1;
    assert(graph_input_source(g, sink, 1, &source, &source_pin));
    assert(source == call && source_pin == 0);
    behavior_asset_free(asset);
}

static void terminal_selection_conversion()
{
    behavior_asset *asset = behavior_asset_new("Terminal selection conversion");
    graph *g = asset->graph;
    node *input = literal(g, 4.0);
    node *subtract = math_node(g, "Subtract");
    uint64_t subtract_id = subtract->id;
    assert(graph_connect_data(g, input, 0, subtract, 0) == 0);
    assert(
        graph_set_pin_default(g, subtract, 0, 1, &(pin_value){ .type = PIN_F32, .f = 2 }) == 0
    );

    node *selection[] = { subtract };
    assert(!behavior_expression_can_convert_selection(asset, g, selection, 1));
    behavior_expression_error error;
    node *call = behavior_expression_convert_selection(asset, g, selection, 1, &error);
    if (!call) {
        fprintf(stderr, "terminal selection conversion failed: %s\n", error.message);
    }
    assert(call);
    assert(!graph_node_by_id(g, subtract_id));
    assert(!strcmp(behavior_expression_source(asset, call), "(A-2)"));
    behavior_asset_free(asset);
}

static void expression_input_rename()
{
    behavior_asset *asset = behavior_asset_new("Expression input rename");
    graph *g = asset->graph;
    behavior_expression_error error;
    node *call = behavior_expression_add(asset, g, "a + b", &error);
    assert(call && call->input_count == 2);
    node *source = literal(g, 2);
    assert(graph_connect_data(g, source, 0, call, 0) == 0);
    assert(graph_set_pin_default(g, call, 0, 1, &(pin_value){ .type = PIN_F64, .d = 3 }) == 0);
    pin_binding binding = { .kind = PIN_BINDING_DELTA_TIME };
    assert(graph_set_pin_binding(g, call, 0, 1, &binding) == 0);
    const pin_binding *saved_binding = graph_pin_binding(g, call, 0, 1);
    uint64_t a_id = call->input_pins[0].id;
    uint64_t b_id = call->input_pins[1].id;
    graph *generated = behavior_asset_find_graph(asset, call->reference_id);
    assert(generated);
    node *second = behavior_asset_add_function_call_node(asset, generated);
    assert(second);
    uint64_t second_a_id = second->input_pins[0].id;
    assert(behavior_expression_set(asset, call, "target + b", &error) == 0);
    assert(!strcmp(behavior_expression_source(asset, call), "target + b"));
    assert(call->input_count == 2);
    assert(!strcmp(call->input_pins[0].name, "target") && call->input_pins[0].id == a_id);
    assert(call->input_pins[1].id == b_id && call->input_pins[1].default_value.d == 3);
    assert(graph_pin_binding(g, call, 0, 1) == saved_binding);
    node *connected_source;
    int connected_pin;
    assert(graph_input_source(g, call, 0, &connected_source, &connected_pin));
    assert(connected_source == source && connected_pin == 0);
    assert(!strcmp(second->input_pins[0].name, "target"));
    assert(second->input_pins[0].id == second_a_id);
    assert(acount(g->edges) == 1);
    behavior_asset_free(asset);
}

static CF_V2 evaluate_struct(behavior_asset *asset, node *call, const char *type_name)
{
    compile_report report;
    compile_report_init(&report);
    assert(behavior_asset_compile(asset, &report) == 0);
    compile_report_free(&report);
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(instance);
    eval_context context;
    eval_context_init(&context);
    context.instance = instance;
    arena memory;
    arena_init(&memory, 4096);
    pin_value output;
    assert(graph_eval_output(asset->graph, call, 0, &memory, &output, &context) == 0);
    assert(output.type == PIN_STRUCT && !strcmp(output.type_name, type_name));
    CF_V2 value = *(const CF_V2 *)output.ptr;
    eval_context_free(&context);
    arena_free(&memory);
    behavior_instance_destroy(instance);
    return value;
}

static CF_V2 evaluate_vector(behavior_asset *asset, node *call)
{
    return evaluate_struct(asset, call, "CF_V2");
}

static CF_V3 evaluate_vector3(behavior_asset *asset, node *call)
{
    compile_report report;
    compile_report_init(&report);
    assert(behavior_asset_compile(asset, &report) == 0);
    compile_report_free(&report);
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(instance);
    eval_context context;
    eval_context_init(&context);
    context.instance = instance;
    arena memory;
    arena_init(&memory, 4096);
    pin_value output;
    assert(graph_eval_output(asset->graph, call, 0, &memory, &output, &context) == 0);
    assert(output.type == PIN_STRUCT && !strcmp(output.type_name, "CF_V3"));
    CF_V3 value = *(const CF_V3 *)output.ptr;
    eval_context_free(&context);
    arena_free(&memory);
    behavior_instance_destroy(instance);
    return value;
}

static int named_pin(const node *n, int output, const char *name)
{
    int count = output ? n->output_count : n->input_count;
    const pin_spec *pins = output ? n->output_pins : n->input_pins;
    for (int i = 0; i < count; i++) {
        if (pins[i].name && !strcmp(pins[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static void set_real_default(graph *g, node *n, int pin, double value)
{
    if (n->input_pins[pin].type == PIN_F32) {
        assert(
            graph_set_pin_default(g, n, 0, pin, &(pin_value){ .type = PIN_F32, .f = (float)value }) == 0
        );
    } else {
        assert(
            graph_set_pin_default(g, n, 0, pin, &(pin_value){ .type = PIN_F64, .d = value }) == 0
        );
    }
}

static bool evaluate_bool(behavior_asset *asset, node *call)
{
    compile_report report;
    compile_report_init(&report);
    assert(behavior_asset_compile(asset, &report) == 0);
    compile_report_free(&report);
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(instance);
    eval_context context;
    eval_context_init(&context);
    context.instance = instance;
    arena memory;
    arena_init(&memory, 4096);
    pin_value output;
    assert(
        graph_eval_output(asset->graph, call, named_pin(call, true, "Result"), &memory, &output, &context) == 0
    );
    assert(output.type == PIN_BOOL);
    bool value = output.b;
    eval_context_free(&context);
    arena_free(&memory);
    behavior_instance_destroy(instance);
    return value;
}

static void vector_constructor_expressions()
{
    behavior_asset *asset = behavior_asset_new("Vector constructor expressions");
    behavior_expression_error error;
    node *call = behavior_expression_add(asset, asset->graph, "vec2(x, y)", &error);
    assert(call && call->input_count == 2);
    set_real_default(asset->graph, call, 0, 3);
    set_real_default(asset->graph, call, 1, 4);
    CF_V2 value = evaluate_vector(asset, call);
    assert(value.x == 3 && value.y == 4);
    behavior_asset_free(asset);

    asset = behavior_asset_new("Vector three constructor expression");
    call = behavior_expression_add(asset, asset->graph, "vec3(x, 2, z)", &error);
    assert(call && call->input_count == 2);
    set_real_default(asset->graph, call, 0, 1);
    set_real_default(asset->graph, call, 1, 3);
    CF_V3 value3 = evaluate_vector3(asset, call);
    assert(value3.x == 1 && value3.y == 2 && value3.z == 3);
    behavior_asset_free(asset);
}

static void boolean_expressions()
{
    behavior_asset *asset = behavior_asset_new("Boolean expression");
    asset->graph->dataflow = true;
    behavior_expression_error error;
    node *call = behavior_expression_add(asset, asset->graph, "x >= 0 && x < (50 + 10)", &error);
    assert(
        call && behavior_asset_find_graph(asset, call->reference_id)->expression_mode == GRAPH_EXPRESSION_VALUE
    );
    assert(call->output_count == 1 && call->output_pins[0].type == PIN_BOOL);
    assert(
        graph_set_pin_default(asset->graph, call, 0, 0, &(pin_value){ .type = PIN_F64, .d = 12 }) == 0
    );
    assert(evaluate_bool(asset, call));
    assert(
        graph_set_pin_default(asset->graph, call, 0, 0, &(pin_value){ .type = PIN_F64, .d = 75 }) == 0
    );
    assert(!evaluate_bool(asset, call));
    behavior_asset_free(asset);
}

static void boolean_symbol_and_member_assignment()
{
    behavior_asset *asset = behavior_asset_new("Boolean symbol expression");
    behavior_expression_error error;
    node *call = behavior_expression_add(asset, asset->graph, "flag", &error);
    node *flag = graph_add_node(asset->graph, registry_node_by_name("Bool"));
    assert(call && flag && call->output_count == 1);
    assert(graph_connect_data(asset->graph, flag, 0, call, 0) == 0);
    assert(behavior_expression_refresh(asset, call, &error) == 0);
    graph *generated = behavior_asset_find_graph(asset, call->reference_id);
    assert(generated && generated->expression_mode == GRAPH_EXPRESSION_BRANCH);
    int flag_pin = named_pin(call, false, "flag");
    int result_pin = named_pin(call, true, "Result");
    assert(flag_pin >= 0 && result_pin >= 0);
    assert(call->input_pins[flag_pin].type == PIN_BOOL);
    assert(call->output_pins[result_pin].type == PIN_BOOL);
    assert(named_pin(call, false, "Exec") >= 0);
    assert(named_pin(call, true, "True") >= 0);
    assert(named_pin(call, true, "False") >= 0);
    behavior_asset_free(asset);

    asset = behavior_asset_new("Component member assignment");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    node *event = graph_add_node(asset->graph, &NODE_Event);
    assert(event && graph_set_event_name(asset->graph, event, "Run") == 0);
    call = behavior_expression_add(asset, asset->graph, "aim.direction.x = desired_x", &error);
    assert(call);
    int aim = named_pin(call, false, "aim");
    int desired = named_pin(call, false, "desired_x");
    int exec = named_pin(call, false, "Exec");
    assert(aim >= 0 && desired >= 0 && exec >= 0);
    assert(
        graph_set_pin_binding(
            asset->graph,
            call,
            0,
            aim,
            &(pin_binding){ .kind = PIN_BINDING_COMPONENT,
                            .component_key = GAME_COMPONENT_GameAim.key }
        ) == 0
    );
    assert(
        graph_set_pin_default(asset->graph, call, 0, desired, &(pin_value){ .type = PIN_F64, .d = 2 }) == 0
    );
    assert(graph_connect_exec(asset->graph, event, 0, call, exec, 0) == 0);
    assert(behavior_expression_refresh(asset, call, &error) == 0);
    assert(call->input_pins[aim].type == PIN_STRUCT);
    assert(!strcmp(call->input_pins[aim].type_name, "GameAim"));
    compile_report report;
    compile_report_init(&report);
    assert(behavior_asset_compile(asset, &report) == 0 && report.error_count == 0);
    compile_report_free(&report);
    ecs_world *world = ecs_world_new();
    assert(world && ecs_world_register_component(world, &GAME_COMPONENT_GameAim, NULL) == ECS_OK);
    ecs_entity owner = ecs_world_create_entity(world);
    assert(GAME_GameAim_add(world, owner, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    GAME_GameAim_get(world, owner)->direction = (CF_V2){ 1, 3 };
    pin_value handle = { .type = PIN_HANDLE,
                         .type_name = "ecs.entity",
                         .u64 = ecs_entity_to_u64(owner) };
    behavior_instance *instance = behavior_instance_create(asset, &handle, NULL, NULL);
    assert(instance && behavior_instance_bind_service(instance, "ecs.world", world) == 0);
    eval_component_reader reader;
    ecs_eval_component_reader_init(&reader, world);
    assert(behavior_instance_bind_service(instance, EVAL_COMPONENT_READER_SERVICE, &reader) == 0);
    flow_error flow;
    flow_run *run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ 0 }, &flow);
    assert(run && flow_run_status(run) == FLOW_COMPLETED);
    assert(GAME_GameAim_get_const(world, owner)->direction.x == 2);
    assert(GAME_GameAim_get_const(world, owner)->direction.y == 3);
    behavior_instance_destroy(instance);
    ecs_world_free(world);
    behavior_asset_free(asset);
}

static void assignment_expressions()
{
    static const struct
    {
        const char *operator;
        double expected;
    } cases[] = {
        { "=", 2 }, { "+=", 12 }, { "-=", 8 }, { "*=", 20 }, { "/=", 5 },
    };
    for (int i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
        behavior_asset *asset = behavior_asset_new("Assignment expression");
        pin_value initial = { .type = PIN_F64, .d = 10 };
        behavior_variable *position = behavior_asset_add_variable(
            asset,
            "position",
            PIN_F64,
            "double",
            PIN_CONTAINER_SINGLE,
            &initial,
            NULL
        );
        assert(position);
        node *event = graph_add_node(asset->graph, &NODE_Event);
        assert(event && graph_set_event_name(asset->graph, event, "Run") == 0);
        char source[64];
        snprintf(source, sizeof source, "position %s amount", cases[i].operator);
        behavior_expression_error error;
        node *call = behavior_expression_add(asset, asset->graph, source, &error);
        assert(call);
        int position_pin = named_pin(call, false, "position");
        int amount_pin = named_pin(call, false, "amount");
        int exec_pin = named_pin(call, false, "Exec");
        assert(position_pin >= 0 && amount_pin >= 0 && exec_pin >= 0);
        assert(call->input_pins[position_pin].flags & PIN_FLAG_REFERENCE);
        pin_binding binding = { .kind = PIN_BINDING_VARIABLE,
                                .variable_id = position->id,
                                .source_asset = asset->name };
        assert(graph_set_pin_binding(asset->graph, call, 0, position_pin, &binding) == 0);
        assert(
            graph_set_pin_default(asset->graph, call, 0, amount_pin, &(pin_value){ .type = PIN_F64, .d = 2 }) ==
            0
        );
        assert(graph_connect_exec(asset->graph, event, 0, call, exec_pin, 0) == 0);
        assert(behavior_expression_refresh(asset, call, &error) == 0);
        assert(call->desc == &NODE_BehaviorExpression);
        int result_pin = named_pin(call, true, "Result");
        assert(result_pin >= 0 && call->output_pins[result_pin].type == PIN_F64);

        behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
        assert(instance);
        flow_error flow;
        flow_run *run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ 0 }, &flow);
        assert(run && flow_run_status(run) == FLOW_COMPLETED);
        pin_value value;
        assert(behavior_instance_get_variable(instance, position->id, &value) == 0);
        assert(fabs(value.d - cases[i].expected) < 1e-10);
        assert(flow_run_watch_output(run, call, result_pin, &value) == 0);
        assert(fabs(value.d - cases[i].expected) < 1e-10);
        behavior_instance_destroy(instance);
        behavior_asset_free(asset);
    }
}

static node *configured_variable_getter(graph *g, const behavior_variable *variable)
{
    node *getter = graph_add_node(g, &NODE_GetVariable);
    assert(getter);
    assert(graph_set_node_reference(g, getter, variable->id, variable->name) == 0);
    assert(graph_set_pin_type(g, getter, 1, 0, variable->type, variable->type_name) == 0);
    assert(graph_set_pin_container(g, getter, 1, 0, variable->container) == 0);
    return getter;
}

static void reference_assignment_expressions()
{
    behavior_asset *asset = behavior_asset_new("Reference assignment expression");
    behavior_variable *position = behavior_asset_add_variable(
        asset,
        "position",
        PIN_F64,
        "double",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_F64, .d = 10 },
        NULL
    );
    behavior_variable *velocity = behavior_asset_add_variable(
        asset,
        "velocity",
        PIN_F64,
        "double",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_F64, .d = 20 },
        NULL
    );
    assert(position && velocity);
    node *event = graph_add_node(asset->graph, &NODE_Event);
    assert(event && graph_set_event_name(asset->graph, event, "Run") == 0);
    behavior_expression_error error;
    node *call = behavior_expression_add(asset, asset->graph, "target += amount", &error);
    assert(call);
    int target_pin = named_pin(call, false, "target");
    int amount_pin = named_pin(call, false, "amount");
    int exec_pin = named_pin(call, false, "Exec");
    assert(target_pin >= 0 && amount_pin >= 0 && exec_pin >= 0);
    assert(call->input_pins[target_pin].flags & PIN_FLAG_REFERENCE);

    node *getter = configured_variable_getter(asset->graph, position);
    assert(getter->output_pins[0].flags & PIN_FLAG_REFERENCE);
    pin_binding resolved = { 0 };
    assert(graph_resolve_reference(asset->graph, getter, 0, &resolved) == 0);
    assert(
        resolved.kind == PIN_BINDING_VARIABLE &&
        resolved.variable_id == position->id
    );
    assert(graph_connect_data(asset->graph, getter, 0, call, target_pin) == 0);
    assert(graph_pin_binding(asset->graph, call, 0, target_pin) == NULL);
    assert(
        graph_set_pin_default(asset->graph, call, 0, amount_pin, &(pin_value){ .type = PIN_F64, .d = 2 }) == 0
    );
    assert(graph_connect_exec(asset->graph, event, 0, call, exec_pin, 0) == 0);
    assert(behavior_expression_refresh(asset, call, &error) == 0);
    int result_pin = named_pin(call, true, "Result");
    assert(result_pin >= 0 && call->output_pins[result_pin].type == PIN_F64);
    node *source = NULL;
    int source_pin = -1;
    uint64_t call_id = call->id;
    uint64_t getter_id = getter->id;
    const graph *generated = behavior_asset_find_graph(asset, call->reference_id);
    char *serialized = behavior_asset_save_json(asset);
    /* The expression's body is rebuilt from its source rather than stored. */
    assert(serialized && !strstr(serialized, "\"Function Entry\""));
    behavior_asset *reloaded = behavior_asset_load_json(serialized);
    free(serialized);
    assert(reloaded);
    node *reloaded_call = graph_node_by_id(reloaded->graph, call_id);
    node *reloaded_getter = graph_node_by_id(reloaded->graph, getter_id);
    assert(reloaded_call && reloaded_getter);
    graph *reloaded_generated = behavior_asset_find_graph(
        reloaded,
        reloaded_call->reference_id
    );
    assert(reloaded_generated && reloaded_generated->expression_mode == GRAPH_EXPRESSION_ACTION);
    assert(reloaded_generated->entry_id == generated->entry_id);
    assert(acount(reloaded_generated->nodes) == acount(generated->nodes));
    for (int i = 0; i < acount(generated->nodes); i++) {
        assert(graph_node_by_id(reloaded_generated, generated->nodes[i]->id));
    }
    assert(graph_input_source(
        reloaded->graph,
        reloaded_call,
        named_pin(reloaded_call, false, "target"),
        &source,
        &source_pin
    ));
    assert(source == reloaded_getter && source_pin == 0);
    behavior_asset_free(reloaded);

    node *make = graph_add_node(asset->graph, registry_node_by_name("Make_CF_V2"));
    assert(make);
    assert(graph_connect_data(asset->graph, make, 0, call, target_pin) != 0);
    assert(graph_input_source(asset->graph, call, target_pin, &source, &source_pin));
    assert(source == getter && source_pin == 0);

    assert(graph_remove_edge(asset->graph, getter, 0, call, target_pin) == 0);
    pin_binding binding = { .kind = PIN_BINDING_VARIABLE,
                            .variable_id = velocity->id,
                            .source_asset = asset->name };
    assert(graph_set_pin_binding(asset->graph, call, 0, target_pin, &binding) == 0);
    assert(behavior_expression_refresh(asset, call, &error) == 0);
    assert(graph_pin_binding(asset->graph, call, 0, target_pin));
    assert(
        behavior_asset_find_graph(asset, call->reference_id)->expression_mode == GRAPH_EXPRESSION_ACTION
    );

    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(instance);
    flow_error flow;
    flow_run *run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ 0 }, &flow);
    assert(run && flow_run_status(run) == FLOW_COMPLETED);
    pin_value value;
    assert(behavior_instance_get_variable(instance, velocity->id, &value) == 0);
    assert(fabs(value.d - 22) < 1e-10);
    assert(flow_run_watch_output(run, call, result_pin, &value) == 0);
    assert(fabs(value.d - 22) < 1e-10);
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);

    asset = behavior_asset_new("Component reference assignment expression");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    event = graph_add_node(asset->graph, &NODE_Event);
    assert(event && graph_set_event_name(asset->graph, event, "Run") == 0);
    call = behavior_expression_add(asset, asset->graph, "target.direction = desired", &error);
    assert(call);
    target_pin = named_pin(call, false, "target");
    int desired_pin = named_pin(call, false, "desired");
    exec_pin = named_pin(call, false, "Exec");
    assert(target_pin >= 0 && desired_pin >= 0 && exec_pin >= 0);
    node *component_getter = graph_add_node(asset->graph, registry_node_by_name("Get_GameAim"));
    assert(component_getter && component_getter->output_pins[0].flags & PIN_FLAG_REFERENCE);
    assert(graph_connect_data(asset->graph, component_getter, 0, call, target_pin) == 0);
    node *desired = graph_add_node(asset->graph, registry_node_by_name("Make_CF_V2"));
    assert(desired);
    assert(
        graph_set_pin_default(asset->graph, desired, 0, 0, &(pin_value){ .type = PIN_F32, .f = 5 }) == 0
    );
    assert(
        graph_set_pin_default(asset->graph, desired, 0, 1, &(pin_value){ .type = PIN_F32, .f = 6 }) == 0
    );
    assert(graph_connect_data(asset->graph, desired, 0, call, desired_pin) == 0);
    assert(graph_connect_exec(asset->graph, event, 0, call, exec_pin, 0) == 0);
    assert(behavior_expression_refresh(asset, call, &error) == 0);
    result_pin = named_pin(call, true, "Result");
    assert(result_pin >= 0 && call->output_pins[result_pin].type == PIN_STRUCT);
    assert(!strcmp(call->output_pins[result_pin].type_name, "CF_V2"));

    ecs_world *world = ecs_world_new();
    assert(world && ecs_world_register_component(world, &GAME_COMPONENT_GameAim, NULL) == ECS_OK);
    ecs_entity owner = ecs_world_create_entity(world);
    assert(GAME_GameAim_add(world, owner, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    pin_value handle = { .type = PIN_HANDLE,
                         .type_name = "ecs.entity",
                         .u64 = ecs_entity_to_u64(owner) };
    instance = behavior_instance_create(asset, &handle, NULL, NULL);
    assert(instance && behavior_instance_bind_service(instance, "ecs.world", world) == 0);
    eval_component_reader reader;
    ecs_eval_component_reader_init(&reader, world);
    assert(behavior_instance_bind_service(instance, EVAL_COMPONENT_READER_SERVICE, &reader) == 0);
    run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ 0 }, &flow);
    assert(run && flow_run_status(run) == FLOW_COMPLETED);
    assert(GAME_GameAim_get_const(world, owner)->direction.x == 5);
    assert(GAME_GameAim_get_const(world, owner)->direction.y == 6);
    assert(flow_run_watch_output(run, call, result_pin, &value) == 0);
    assert(value.type == PIN_STRUCT && value.ptr);
    assert(((CF_V2 *)value.ptr)->x == 5 && ((CF_V2 *)value.ptr)->y == 6);
    behavior_instance_destroy(instance);
    ecs_world_free(world);
    behavior_asset_free(asset);

    asset = behavior_asset_new("Component field reference assignment expression");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    event = graph_add_node(asset->graph, &NODE_Event);
    assert(event && graph_set_event_name(asset->graph, event, "Run") == 0);
    call = behavior_expression_add(asset, asset->graph, "target = desired", &error);
    assert(call);
    target_pin = named_pin(call, false, "target");
    desired_pin = named_pin(call, false, "desired");
    exec_pin = named_pin(call, false, "Exec");
    assert(target_pin >= 0 && desired_pin >= 0 && exec_pin >= 0);
    component_getter = graph_add_node(asset->graph, registry_node_by_name("Get_GameAim"));
    assert(component_getter);
    assert(graph_expose_output_fields(asset->graph, component_getter, 0) == 0);
    int direction_pin = named_pin(component_getter, true, "direction");
    assert(
        direction_pin >= 0 && component_getter->output_pins[direction_pin].flags & PIN_FLAG_REFERENCE
    );
    assert(graph_split_pin(asset->graph, component_getter, 1, direction_pin) >= 0);
    int direction_x_pin = named_pin(component_getter, true, "x");
    assert(
        direction_x_pin >= 0 &&
        component_getter->output_pins[direction_x_pin].flags & PIN_FLAG_REFERENCE
    );
    resolved = (pin_binding){ 0 };
    assert(
        graph_resolve_reference(asset->graph, component_getter, direction_x_pin, &resolved) == 0
    );
    assert(
        resolved.kind == PIN_BINDING_COMPONENT_FIELD &&
        !strcmp(resolved.field_name, "direction.x")
    );
    assert(graph_recombine_pin(asset->graph, component_getter, 1, direction_x_pin) >= 0);
    direction_pin = named_pin(component_getter, true, "direction");
    assert(direction_pin >= 0);
    resolved = (pin_binding){ 0 };
    assert(graph_resolve_reference(asset->graph, component_getter, direction_pin, &resolved) == 0);
    assert(
        resolved.kind == PIN_BINDING_COMPONENT_FIELD &&
        !strcmp(resolved.component_key, GAME_COMPONENT_GameAim.key) &&
        !strcmp(resolved.field_name, "direction")
    );
    assert(graph_connect_data(asset->graph, component_getter, direction_pin, call, target_pin) == 0);
    make = graph_add_node(asset->graph, registry_node_by_name("Make_CF_V2"));
    assert(make);
    assert(graph_connect_data(asset->graph, make, 0, call, target_pin) != 0);
    desired = graph_add_node(asset->graph, registry_node_by_name("Make_CF_V2"));
    assert(desired);
    assert(
        graph_set_pin_default(asset->graph, desired, 0, 0, &(pin_value){ .type = PIN_F32, .f = 7 }) == 0
    );
    assert(
        graph_set_pin_default(asset->graph, desired, 0, 1, &(pin_value){ .type = PIN_F32, .f = 8 }) == 0
    );
    assert(graph_connect_data(asset->graph, desired, 0, call, desired_pin) == 0);
    assert(graph_connect_exec(asset->graph, event, 0, call, exec_pin, 0) == 0);
    assert(behavior_expression_refresh(asset, call, &error) == 0);
    result_pin = named_pin(call, true, "Result");
    assert(result_pin >= 0 && call->output_pins[result_pin].type == PIN_STRUCT);
    world = ecs_world_new();
    assert(world && ecs_world_register_component(world, &GAME_COMPONENT_GameAim, NULL) == ECS_OK);
    owner = ecs_world_create_entity(world);
    assert(GAME_GameAim_add(world, owner, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    handle.u64 = ecs_entity_to_u64(owner);
    instance = behavior_instance_create(asset, &handle, NULL, NULL);
    assert(instance && behavior_instance_bind_service(instance, "ecs.world", world) == 0);
    ecs_eval_component_reader_init(&reader, world);
    assert(behavior_instance_bind_service(instance, EVAL_COMPONENT_READER_SERVICE, &reader) == 0);
    run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ 0 }, &flow);
    assert(run && flow_run_status(run) == FLOW_COMPLETED);
    assert(GAME_GameAim_get_const(world, owner)->direction.x == 7);
    assert(GAME_GameAim_get_const(world, owner)->direction.y == 8);
    behavior_instance_destroy(instance);
    ecs_world_free(world);
    behavior_asset_free(asset);
}

static void assignment_root_rename_preserves_binding()
{
    behavior_asset *asset = behavior_asset_new("Assignment root rename");
    pin_value initial = { .type = PIN_F64, .d = 10 };
    behavior_variable *position = behavior_asset_add_variable(
        asset,
        "position",
        PIN_F64,
        "double",
        PIN_CONTAINER_SINGLE,
        &initial,
        NULL
    );
    assert(position);
    node *event = graph_add_node(asset->graph, &NODE_Event);
    assert(event && graph_set_event_name(asset->graph, event, "Run") == 0);
    behavior_expression_error error;
    node *call = behavior_expression_add(asset, asset->graph, "position += amount", &error);
    assert(call);
    int position_pin = named_pin(call, false, "position");
    int amount_pin = named_pin(call, false, "amount");
    int exec_pin = named_pin(call, false, "Exec");
    assert(position_pin >= 0 && amount_pin >= 0 && exec_pin >= 0);
    pin_binding binding = { .kind = PIN_BINDING_VARIABLE,
                            .variable_id = position->id,
                            .source_asset = asset->name };
    assert(graph_set_pin_binding(asset->graph, call, 0, position_pin, &binding) == 0);
    const pin_binding *saved_binding = graph_pin_binding(asset->graph, call, 0, position_pin);
    uint64_t pin_id = call->input_pins[position_pin].id;
    assert(
        graph_set_pin_default(asset->graph, call, 0, amount_pin, &(pin_value){ .type = PIN_F64, .d = 2 }) == 0
    );
    assert(graph_connect_exec(asset->graph, event, 0, call, exec_pin, 0) == 0);
    assert(behavior_expression_set(asset, call, "velocity += amount", &error) == 0);
    int velocity_pin = named_pin(call, false, "velocity");
    amount_pin = named_pin(call, false, "amount");
    assert(velocity_pin >= 0 && amount_pin >= 0);
    assert(call->input_pins[velocity_pin].id == pin_id);
    assert(call->input_pins[velocity_pin].flags & PIN_FLAG_REFERENCE);
    assert(graph_pin_binding(asset->graph, call, 0, velocity_pin) == saved_binding);

    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(instance);
    flow_error flow;
    flow_run *run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ 0 }, &flow);
    assert(run && flow_run_status(run) == FLOW_COMPLETED);
    pin_value value;
    assert(behavior_instance_get_variable(instance, position->id, &value) == 0);
    assert(fabs(value.d - 12) < 1e-10);
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
}

static void indexed_assignment_expression()
{
    behavior_asset *asset = behavior_asset_new("Indexed assignment expression");
    pin_array *initial = pin_array_new(PIN_F64, "double", 2);
    assert(initial);
    assert(pin_array_push(initial, &(pin_value){ .type = PIN_F64, .d = 3 }) == 0);
    assert(pin_array_push(initial, &(pin_value){ .type = PIN_F64, .d = 5 }) == 1);
    behavior_variable *items = behavior_asset_add_variable(
        asset,
        "items",
        PIN_F64,
        "double",
        PIN_CONTAINER_ARRAY,
        &(pin_value){ .type = PIN_F64, .container = PIN_CONTAINER_ARRAY, .ptr = initial },
        NULL
    );
    pin_array_free(initial);
    assert(items);
    node *event = graph_add_node(asset->graph, &NODE_Event);
    assert(event && graph_set_event_name(asset->graph, event, "Run") == 0);
    behavior_expression_error error;
    node *call = behavior_expression_add(asset, asset->graph, "items[index] += amount", &error);
    assert(call);
    int items_pin = named_pin(call, false, "items");
    int index = named_pin(call, false, "index");
    int amount = named_pin(call, false, "amount");
    int exec = named_pin(call, false, "Exec");
    assert(items_pin >= 0 && index >= 0 && amount >= 0 && exec >= 0);
    pin_binding binding = { .kind = PIN_BINDING_VARIABLE,
                            .variable_id = items->id,
                            .source_asset = asset->name };
    assert(graph_set_pin_binding(asset->graph, call, 0, items_pin, &binding) == 0);
    assert(
        graph_set_pin_default(asset->graph, call, 0, index, &(pin_value){ .type = PIN_F64, .d = 1 }) == 0
    );
    assert(
        graph_set_pin_default(asset->graph, call, 0, amount, &(pin_value){ .type = PIN_F64, .d = 2 }) == 0
    );
    assert(graph_connect_exec(asset->graph, event, 0, call, exec, 0) == 0);
    assert(behavior_expression_refresh(asset, call, &error) == 0);
    int result = named_pin(call, true, "Result");
    assert(result >= 0 && call->output_pins[result].type == PIN_F64);
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(instance);
    flow_error flow;
    flow_run *run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ 0 }, &flow);
    assert(run && flow_run_status(run) == FLOW_COMPLETED);
    pin_value value;
    assert(behavior_instance_get_variable(instance, items->id, &value) == 0);
    assert(value.container == PIN_CONTAINER_ARRAY && value.ptr);
    assert(((pin_array *)value.ptr)->items[1].d == 7);
    assert(flow_run_watch_output(run, call, result, &value) == 0 && value.d == 7);
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
}

static void branch_expression_flow()
{
    behavior_asset *asset = behavior_asset_new("Branch expression");
    behavior_variable *hit = behavior_asset_add_variable(
        asset,
        "hit",
        PIN_BOOL,
        "bool",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_BOOL, .b = false },
        NULL
    );
    assert(hit);
    node *event = graph_add_node(asset->graph, &NODE_Event);
    assert(event && graph_set_event_name(asset->graph, event, "Run") == 0);
    behavior_expression_error error;
    node *call = behavior_expression_add(asset, asset->graph, "amount >= 0", &error);
    assert(call);
    graph *generated = behavior_asset_find_graph(asset, call->reference_id);
    assert(generated && generated->expression_mode == GRAPH_EXPRESSION_BRANCH);
    int exec = named_pin(call, false, "Exec");
    int amount = named_pin(call, false, "amount");
    int yes = named_pin(call, true, "True");
    int no = named_pin(call, true, "False");
    int result = named_pin(call, true, "Result");
    assert(exec >= 0 && amount >= 0 && yes >= 0 && no >= 0 && result >= 0);
    assert(call->output_pins[result].type == PIN_BOOL);
    assert(
        graph_set_pin_default(asset->graph, call, 0, amount, &(pin_value){ .type = PIN_F64, .d = 5 }) == 0
    );
    node *set = behavior_asset_add_set_node(asset, hit->id);
    assert(set);
    assert(
        graph_set_pin_default(asset->graph, set, 0, 1, &(pin_value){ .type = PIN_BOOL, .b = true }) == 0
    );
    assert(graph_connect_exec(asset->graph, event, 0, call, exec, 0) == 0);
    assert(graph_connect_exec(asset->graph, call, yes, set, 0, 0) == 0);
    (void)no;
    compile_report report;
    compile_report_init(&report);
    assert(behavior_asset_compile(asset, &report) == 0);
    compile_report_free(&report);
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(instance);
    flow_error flow;
    flow_run *run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ 0 }, &flow);
    assert(run && flow_run_status(run) == FLOW_COMPLETED);
    pin_value value;
    assert(
        behavior_instance_get_variable(instance, hit->id, &value) == 0 && value.b
    );
    assert(flow_run_watch_output(run, call, result, &value) == 0 && value.b);
    behavior_instance_destroy(instance);

    assert(
        graph_set_pin_default(asset->graph, call, 0, amount, &(pin_value){ .type = PIN_F64, .d = -5 }) == 0
    );
    instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(instance);
    run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ 0 }, &flow);
    assert(run && flow_run_status(run) == FLOW_COMPLETED);
    assert(
        behavior_instance_get_variable(instance, hit->id, &value) == 0 && !value.b
    );
    assert(flow_run_watch_output(run, call, result, &value) == 0 && !value.b);
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
}

static void vector_expression()
{
    behavior_asset *asset = behavior_asset_new("Vector expression");
    behavior_expression_error error;
    node *call = behavior_expression_add(asset, asset->graph, "value * scale", &error);
    assert(call && call->input_count == 2);
    node *vector = graph_add_node(asset->graph, registry_node_by_name("Make_CF_V2"));
    assert(vector);
    assert(
        graph_set_pin_default(asset->graph, vector, 0, 0, &(pin_value){ .type = PIN_F32, .f = 3 }) == 0
    );
    assert(
        graph_set_pin_default(asset->graph, vector, 0, 1, &(pin_value){ .type = PIN_F32, .f = 4 }) == 0
    );
    assert(graph_connect_data(asset->graph, vector, 0, call, 0) == 0);
    assert(graph_connect_data(asset->graph, literal(asset->graph, 2), 0, call, 1) == 0);
    assert(behavior_expression_refresh(asset, call, &error) == 0);
    assert(call->output_pins[0].type == PIN_STRUCT);
    assert(!strcmp(call->output_pins[0].type_name, "CF_V2"));
    CF_V2 value = evaluate_vector(asset, call);
    assert(value.x == 6 && value.y == 8);
    behavior_asset_free(asset);
}

static void field_expression()
{
    behavior_asset *asset = behavior_asset_new("Field expression");
    behavior_expression_error error;
    node *call = behavior_expression_add(asset, asset->graph, "value.x + value.y", &error);
    assert(call);
    node *vector = graph_add_node(asset->graph, registry_node_by_name("Make_CF_V2"));
    assert(vector);
    assert(
        graph_set_pin_default(asset->graph, vector, 0, 0, &(pin_value){ .type = PIN_F32, .f = 3 }) == 0
    );
    assert(
        graph_set_pin_default(asset->graph, vector, 0, 1, &(pin_value){ .type = PIN_F32, .f = 4 }) == 0
    );
    assert(graph_connect_data(asset->graph, vector, 0, call, 0) == 0);
    assert(behavior_expression_refresh(asset, call, &error) == 0);
    assert(call->output_pins[0].type == PIN_F32);
    assert(evaluate(asset, call) == 7.0);
    assert(behavior_expression_set(asset, call, "value.x * 2", &error) == 0);
    assert(call->output_pins[0].type == PIN_F32);
    assert(evaluate(asset, call) == 6.0);
    behavior_asset_free(asset);
}

static void mixed_struct_expression()
{
    behavior_asset *asset = behavior_asset_new("Mixed struct expression");
    behavior_expression_error error;
    node *call = behavior_expression_add(asset, asset->graph, "pos + input * movement * dt", &error);
    node *pos = graph_add_node(asset->graph, registry_node_by_name("Make_GameTransform"));
    node *input = graph_add_node(asset->graph, registry_node_by_name("Make_CF_V2"));
    assert(call && pos && input);
    assert(
        graph_set_pin_default(asset->graph, pos, 0, 0, &(pin_value){ .type = PIN_F32, .f = 1 }) == 0
    );
    assert(
        graph_set_pin_default(asset->graph, pos, 0, 1, &(pin_value){ .type = PIN_F32, .f = 2 }) == 0
    );
    assert(
        graph_set_pin_default(asset->graph, input, 0, 0, &(pin_value){ .type = PIN_F32, .f = 3 }) == 0
    );
    assert(
        graph_set_pin_default(asset->graph, input, 0, 1, &(pin_value){ .type = PIN_F32, .f = 4 }) == 0
    );
    assert(graph_connect_data(asset->graph, pos, 0, call, 0) == 0);
    assert(graph_connect_data(asset->graph, input, 0, call, 1) == 0);
    assert(graph_connect_data(asset->graph, literal(asset->graph, 2), 0, call, 2) == 0);
    assert(graph_connect_data(asset->graph, literal(asset->graph, 0.5), 0, call, 3) == 0);
    assert(behavior_expression_refresh(asset, call, &error) == 0);
    assert(call->output_pins[0].type == PIN_STRUCT);
    assert(!strcmp(call->output_pins[0].type_name, "GameTransform"));
    CF_V2 value = evaluate_struct(asset, call, "GameTransform");
    assert(value.x == 4 && value.y == 6);
    behavior_asset_free(asset);
}

static void component_struct_input_expression()
{
    behavior_asset *asset = behavior_asset_new("Component struct expression");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    behavior_expression_error error;
    node *call = behavior_expression_add(asset, asset->graph, "pos + input * movement * dt", &error);
    node *movement = graph_add_node(asset->graph, registry_node_by_name("Get_GameMovement"));
    node *pos = graph_add_node(asset->graph, registry_node_by_name("Make_GameTransform"));
    node *input = graph_add_node(asset->graph, registry_node_by_name("Make_CF_V2"));
    assert(call && movement && pos && input);
    assert(movement->output_count == 1);
    assert(movement->output_pins[0].type == PIN_STRUCT);
    assert(!strcmp(movement->output_pins[0].type_name, "GameMovement"));
    assert(graph_connect_data(asset->graph, pos, 0, call, 0) == 0);
    assert(graph_connect_data(asset->graph, input, 0, call, 1) == 0);
    assert(graph_connect_data(asset->graph, movement, 0, call, 2) == 0);
    assert(graph_connect_data(asset->graph, literal(asset->graph, 0.5), 0, call, 3) == 0);
    /* Reproduce a loaded/regenerated call whose public pin drifted back to
     * its numeric default while the authored edge still points at the struct. */
    call->input_pins[2].type = PIN_F64;
    call->input_pins[2].type_name = "double";
    assert(behavior_expression_refresh(asset, call, &error) == 0);
    assert(call->input_pins[2].type == PIN_STRUCT);
    assert(!strcmp(call->input_pins[2].type_name, "GameMovement"));

    compile_report report;
    compile_report_init(&report);
    if (behavior_asset_compile(asset, &report) != 0) {
        const compile_diagnostic *diagnostic = compile_report_first_error(&report);
        fprintf(
            stderr,
            "Component struct expression failed to compile: %s\n",
            diagnostic ? diagnostic->message : "unknown error"
        );
    }
    assert(report.error_count == 0);
    compile_report_free(&report);
    behavior_asset_free(asset);
}

static void expression_title()
{
    editor *e = editor_new(behavior_asset_new("Expression title"));
    behavior_expression_error error;
    node *call = editor_add_behavior_expression(e, "a + b", 0, 0, &error);
    assert(call);
    graph *generated = behavior_asset_find_graph(editor_behavior_asset(e), call->reference_id);
    assert(generated && !strcmp(generated->name, "a + b"));
    assert(editor_set_behavior_expression(e, call, "a * b", &error) == 0);
    assert(!strcmp(generated->name, "a * b"));
    assert(
        editor_command_apply(
            e,
            (editor_command){ .kind = EDITOR_COMMAND_RENAME_GRAPH,
                              .data.rename_graph = { generated->graph_id, "Custom Title" } }
        ) == 0
    );
    assert(editor_set_behavior_expression(e, call, "a / b", &error) == 0);
    assert(!strcmp(generated->name, "Custom Title"));
    assert(
        editor_command_apply(
            e,
            (editor_command){ .kind = EDITOR_COMMAND_RENAME_GRAPH,
                              .data.rename_graph = { generated->graph_id, "a / b" } }
        ) == 0
    );
    assert(editor_set_behavior_expression(e, call, "a - b", &error) == 0);
    assert(!strcmp(generated->name, "a - b"));
    editor_free(e);
}

static void editor_history()
{
    editor *e = editor_new(behavior_asset_new("Expression history"));
    behavior_expression_error error;
    node *call = editor_add_behavior_expression(e, "a + b", 12, 34, &error);
    assert(call);
    uint64_t id = call->id;
    uint64_t pin_id = call->input_pins[0].id;
    assert(editor_set_behavior_expression(e, call, "a * b", &error) == 0);
    assert(editor_set_behavior_expression(e, call, "a +", &error) == -1);
    editor_undo(e);
    call = graph_node_by_id(editor_behavior_asset(e)->graph, id);
    assert(call && !strcmp(behavior_expression_source(editor_behavior_asset(e), call), "a + b"));
    assert(call->input_pins[0].id == pin_id);
    editor_redo(e);
    call = graph_node_by_id(editor_behavior_asset(e)->graph, id);
    assert(call && !strcmp(behavior_expression_source(editor_behavior_asset(e), call), "a * b"));
    assert(editor_open_graph_call(e, call->id) == 0);
    assert(editor_active_graph(e)->math_expression);
    assert(!editor_spawn(e, registry_node_by_name("Real"), 0, 0));
    assert(!editor_add_behavior_expression(e, "x + y", 0, 0, &error));
    assert(
        editor_command_apply(
            e,
            (editor_command){ .kind = EDITOR_COMMAND_SPAWN_NODE,
                              .data.spawn_node = { registry_node_by_name("Real"), 0, 0 } }
        ) == -1
    );
    assert(editor_graph_back(e) == 0);
    call = graph_node_by_id(editor_behavior_asset(e)->graph, id);
    assert(
        editor_command_apply(
            e,
            (editor_command){ .kind = EDITOR_COMMAND_RENAME_GRAPH,
                              .data.rename_graph = { call->reference_id, "Changed generated graph" } }
        ) == 0
    );
    assert(!strcmp(
        behavior_asset_find_graph(editor_behavior_asset(e), call->reference_id)->name,
        "Changed generated graph"
    ));
    while (acount(editor_behavior_asset(e)->graphs) > 1) {
        editor_undo(e);
    }
    assert(acount(editor_behavior_asset(e)->graphs) == 1);
    editor_free(e);
}

static void editor_delete_expression()
{
    behavior_asset *asset = behavior_asset_new("Delete expression");
    node *call = behavior_expression_add(asset, asset->graph, "negate(a)", NULL);
    assert(call);
    uint64_t call_id = call->id;
    uint64_t graph_id = call->reference_id;
    graph *body = behavior_asset_find_graph(asset, graph_id);
    graph *other = behavior_asset_add_graph(asset, GRAPH_EVENT, "Other");
    uint64_t other_id = other->graph_id;
    node *second = behavior_asset_add_function_call_node_to_graph(asset, other, body);
    assert(second);
    uint64_t second_id = second->id;
    node *value = literal(asset->graph, 4);
    uint64_t value_id = value->id;
    node *sink = graph_add_node(asset->graph, registry_node_by_name("Abs"));
    assert(graph_connect_data(asset->graph, value, 0, call, 0) == 0);
    assert(graph_node_is_inline(asset->graph, value));
    assert(graph_connect_data(asset->graph, call, 0, sink, 0) == 0);
    char *before = behavior_asset_save_json(asset);
    editor *e = editor_new(asset);
    assert(editor_open_graph_call(e, call_id) == 0);
    assert(editor_delete_graph(e, graph_id) == 0);
    const behavior_asset *current = editor_behavior_asset(e);
    assert(editor_active_graph(e) == current->graph);
    assert(editor_graph_history_count(e) == 0);
    assert(!behavior_asset_find_graph(current, graph_id));
    assert(!graph_node_by_id(current->graph, call_id));
    assert(!graph_node_by_id(current->graph, value_id));
    assert(!graph_node_by_id(behavior_asset_find_graph(current, other_id), second_id));
    assert(acount(current->graph->edges) == 0);
    editor_undo(e);
    current = editor_behavior_asset(e);
    char *restored = behavior_asset_save_json(current);
    assert(!strcmp(before, restored));
    free(restored);
    free(before);
    editor_redo(e);
    current = editor_behavior_asset(e);
    assert(!behavior_asset_find_graph(current, graph_id));
    assert(!graph_node_by_id(current->graph, call_id));
    assert(!graph_node_by_id(behavior_asset_find_graph(current, other_id), second_id));
    editor_free(e);
}

static node *movement_field_node(graph *g, const char *name, bool output)
{
    node *n = graph_add_node(g, registry_node_by_name(name));
    while (n->input_count) {
        assert(graph_remove_pin(g, n, 0, n->input_count - 1) == 0);
    }
    while (n->output_count) {
        assert(graph_remove_pin(g, n, 1, n->output_count - 1) == 0);
    }
    pin_spec speed = { .name = "speed", .type = PIN_F32, .type_name = "float" };
    assert(graph_add_pin(g, n, output, &speed) == 0);
    return n;
}

static void test_system_expression()
{
    ecs_world *world = ecs_world_new();
    assert(ecs_world_register_component(world, &GAME_COMPONENT_GameMovement, NULL) == ECS_OK);
    ecs_entity entity = ecs_world_create_entity(world);
    assert(GAME_GameMovement_add(world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    behavior_asset *asset = behavior_asset_new("Expression System");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    graph *g = asset->graph;
    g->dataflow = true;
    node *tick = graph_add_node(g, &NODE_Event);
    assert(graph_set_event_name(g, tick, "Tick") == 0);
    node *get = movement_field_node(g, "Get_GameMovement", true);
    node *set = movement_field_node(g, "Set_GameMovement", false);
    node *expression = behavior_expression_add(asset, g, "speed * 2 + sin(pi / 2)", NULL);
    assert(expression);
    assert(graph_connect_data(g, get, 0, expression, 0) == 0);
    assert(graph_connect_data(g, expression, 0, set, 0) == 0);
    game_system *system = game_system_new(world, asset);
    assert(!compile_report_blocked(game_system_compile(system)));
    assert(game_system_uses_bytecode(system));
    flow_error error;
    GAME_GameMovement_get(world, entity)->speed = 3;
    assert(
        game_system_tick_mode(system, 0, .016, &error, GAME_SYSTEM_EXEC_GENERIC_BYTECODE) == FLOW_OK
    );
    assert(GAME_GameMovement_get(world, entity)->speed == 7);
    assert(
        game_system_tick_mode(system, .016, .016, &error, GAME_SYSTEM_EXEC_GENERIC_BYTECODE) == FLOW_OK
    );
    assert(GAME_GameMovement_get(world, entity)->speed == 15);
    game_system_free(system);

    asset = behavior_asset_new("Repeated Expression System");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    g = asset->graph;
    g->dataflow = true;
    tick = graph_add_node(g, &NODE_Event);
    assert(graph_set_event_name(g, tick, "Tick") == 0);
    get = movement_field_node(g, "Get_GameMovement", true);
    set = movement_field_node(g, "Set_GameMovement", false);
    expression = behavior_expression_add(asset, g, "speed * 2 + 1", NULL);
    node *second = behavior_asset_add_function_call_node(
        asset,
        behavior_asset_find_graph(asset, expression->reference_id)
    );
    assert(second);
    assert(graph_connect_data(g, get, 0, expression, 0) == 0);
    assert(graph_connect_data(g, expression, 0, second, 0) == 0);
    assert(graph_connect_data(g, second, 0, set, 0) == 0);
    system = game_system_new(world, asset);
    assert(game_system_uses_bytecode(system));
    GAME_GameMovement_get(world, entity)->speed = 3;
    assert(
        game_system_tick_mode(system, 0, .016, &error, GAME_SYSTEM_EXEC_GENERIC_BYTECODE) == FLOW_OK
    );
    assert(GAME_GameMovement_get(world, entity)->speed == 15);
    assert(
        game_system_tick_mode(system, .016, .016, &error, GAME_SYSTEM_EXEC_GENERIC_BYTECODE) == FLOW_OK
    );
    assert(GAME_GameMovement_get(world, entity)->speed == 63);
    game_system_free(system);

    asset = behavior_asset_new("Random Expression System");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    g = asset->graph;
    g->dataflow = true;
    tick = graph_add_node(g, &NODE_Event);
    assert(graph_set_event_name(g, tick, "Tick") == 0);
    set = movement_field_node(g, "Set_GameMovement", false);
    expression = behavior_expression_add(asset, g, "rand() + rand()", NULL);
    assert(expression && graph_connect_data(g, expression, 0, set, 0) == 0);
    system = game_system_new(world, asset);
    assert(game_system_uses_bytecode(system));
    srand(912);
    double expected = (double)rand() / ((double)RAND_MAX + 1);
    expected += (double)rand() / ((double)RAND_MAX + 1);
    srand(912);
    assert(
        game_system_tick_mode(system, 0, .016, &error, GAME_SYSTEM_EXEC_GENERIC_BYTECODE) == FLOW_OK
    );
    assert(GAME_GameMovement_get(world, entity)->speed == (float)expected);
    assert(
        game_system_tick_mode(system, .016, .016, &error, GAME_SYSTEM_EXEC_GENERIC_BYTECODE) == FLOW_OK
    );
    assert(GAME_GameMovement_get(world, entity)->speed != (float)expected);
    game_system_free(system);
    ecs_world_free(world);
}

void test_math_expression()
{
    test_system_expression();
    vector_constructor_expressions();
    boolean_expressions();
    boolean_symbol_and_member_assignment();
    assignment_expressions();
    reference_assignment_expressions();
    assignment_root_rename_preserves_binding();
    indexed_assignment_expression();
    branch_expression_flow();
    vector_expression();
    field_expression();
    mixed_struct_expression();
    component_struct_input_expression();
    selection_conversion();
    selected_struct_conversion();
    terminal_selection_conversion();
    expression_input_rename();
    expression_title();
    editor_delete_expression();
    expect("negate(3) + negate(-2)", -1);
    expect("1 + 2 * 3", 7);
    expect("(1 + 2) * 3", 9);
    expect("-2^2", -4);
    expect("2 ** 3 ^ 2", 512);
    expect("2^-2", 0.25);
    expect("10 % 3", 1);
    expect("sin(pi / 2) + sqrt(9) + max(2, 3)", 7);
    expect("clamp(-3, -2, 2) + abs(-4) + min(3, 1)", 3);
    expect("log(e) + log10(100) + floor(2.9) + ceil(2.1) + round(1.6)", 10);
    expect("cos(0) + tan(0) + asin(0) + acos(1) + atan(0) + atan2(0, 1)", 1);
    expect("pow(2, 3) / exp(0)", 8);

    behavior_asset *asset = behavior_asset_new("Expression edits");
    graph *g = asset->graph;
    behavior_expression_error error;
    node *call = behavior_expression_add(asset, g, "a + b + a", &error);
    assert(call && call->input_count == 2 && call->output_count == 1);
    graph *generated = behavior_asset_find_graph(asset, call->reference_id);
    assert(generated && generated->kind == GRAPH_FUNCTION && acount(generated->nodes) > 2);
    assert(graph_connect_data(g, literal(g, 2), 0, call, 0) == 0);
    assert(graph_connect_data(g, literal(g, 3), 0, call, 1) == 0);
    uint64_t a_id = call->input_pins[0].id, b_id = call->input_pins[1].id;
    uint64_t output_id = call->output_pins[0].id, graph_id = generated->graph_id;
    node *second = behavior_asset_add_function_call_node(asset, generated);
    uint64_t second_a_id = second->input_pins[0].id;
    assert(evaluate(asset, call) == 7);
    assert(behavior_expression_set(asset, call, "b + a + c", &error) == 0);
    assert(
        call->input_count == 3 && call->input_pins[0].id == a_id &&
        call->input_pins[1].id == b_id
    );
    assert(call->output_pins[0].id == output_id && call->reference_id == graph_id);
    assert(behavior_asset_find_graph(asset, graph_id) == generated);
    assert(second->input_count == 3 && second->input_pins[0].id == second_a_id);
    assert(acount(g->edges) == 2);
    assert(graph_set_pin_default(g, call, 0, 2, &(pin_value){ .type = PIN_F64, .d = 4 }) == 0);
    assert(evaluate(asset, call) == 9);
    uint64_t call_id = call->id;
    char *snapshot = behavior_asset_save_json(asset);
    const char *invalid[] = { "",          "a +",        "(a + b",  "sin()",
                              "sin(1, 2)", "unknown(2)", "rand(1)", "1 $ 2",
                              "1 2",       "max(1,)",    "1e999" };
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        assert(behavior_expression_set(asset, call, invalid[i], &error) == -1);
        assert(error.message[0] && error.offset >= 0 && error.length > 0);
        char *after = behavior_asset_save_json(asset);
        assert(!strcmp(snapshot, after));
        free(after);
    }
    assert(behavior_expression_set(asset, call, "b + c", &error) == 0);
    assert(call->input_count == 2 && call->input_pins[0].id == b_id);
    assert(acount(g->edges) == 1 && evaluate(asset, call) == 7);
    behavior_asset_free(asset);
    asset = behavior_asset_load_json(snapshot);
    free(snapshot);
    assert(asset);
    call = graph_node_by_id(asset->graph, call_id);
    assert(call && !strcmp(behavior_expression_source(asset, call), "b + a + c"));
    assert(call->input_pins[0].id == a_id && call->input_pins[1].id == b_id);
    assert(evaluate(asset, call) == 9);
    g = asset->graph;
    assert(graph_set_pin_type(g, call, 0, 0, PIN_BOOL, "bool") == 0);
    assert(acount(g->edges) == 1);
    assert(behavior_expression_set(asset, call, "b + a + c + 0", &error) == 0);
    assert(call->input_pins[0].type == PIN_F64 && call->input_pins[0].id == a_id);
    assert(call->input_pins[1].id == b_id && acount(g->edges) == 1);
    assert(call->input_pins[2].default_value.d == 4 && evaluate(asset, call) == 7);
    behavior_asset_free(asset);

    asset = behavior_asset_new("Random expression");
    call = behavior_expression_add(asset, asset->graph, "rand() + rand()", &error);
    assert(call && call->input_count == 0);
    srand(1234);
    double first = (double)rand() / ((double)RAND_MAX + 1);
    double next = (double)rand() / ((double)RAND_MAX + 1);
    srand(1234);
    assert(evaluate(asset, call) == first + next);
    double later = evaluate(asset, call);
    assert(later >= 0 && later < 2 && later != first + next);
    behavior_asset_free(asset);
    editor_history();
    puts("Behavior Expression PASS: precedence, functions, named pins, stable wires, atomic errors, serialization and random sampling");
}
