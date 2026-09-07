#include "core/math_expression.h"

#include "core/arena.h"
#include "core/compile.h"
#include "core/eval.h"
#include "core/instance.h"
#include "core/registry.h"
#include "core/serialize.h"
#include "editor/editor.h"
#include "game/components.h"
#include "game/generated.h"
#include "game/system.h"

#include <assert.h>
#include <cute_defines.h>
#define CF_MATH_H
#include <imgui/dcimgui.h>
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
    math_expression_error error;
    node *call = math_expression_add(asset, asset->graph, source, &error);
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

static void vector_expression()
{
    behavior_asset *asset = behavior_asset_new("Vector expression");
    math_expression_error error;
    node *call = math_expression_add(asset, asset->graph, "value * scale", &error);
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
    assert(math_expression_refresh(asset, call, &error) == 0);
    assert(call->output_pins[0].type == PIN_STRUCT);
    assert(!strcmp(call->output_pins[0].type_name, "CF_V2"));
    CF_V2 value = evaluate_vector(asset, call);
    assert(value.x == 6 && value.y == 8);
    behavior_asset_free(asset);
}

static void field_expression()
{
    behavior_asset *asset = behavior_asset_new("Field expression");
    math_expression_error error;
    node *call = math_expression_add(asset, asset->graph, "value.x + value.y", &error);
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
    assert(math_expression_refresh(asset, call, &error) == 0);
    assert(call->output_pins[0].type == PIN_F32);
    assert(evaluate(asset, call) == 7.0);
    assert(math_expression_set(asset, call, "value.x * 2", &error) == 0);
    assert(call->output_pins[0].type == PIN_F32);
    assert(evaluate(asset, call) == 6.0);
    behavior_asset_free(asset);
}

static void mixed_struct_expression()
{
    behavior_asset *asset = behavior_asset_new("Mixed struct expression");
    math_expression_error error;
    node *call = math_expression_add(asset, asset->graph, "pos + input * movement * dt", &error);
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
    assert(math_expression_refresh(asset, call, &error) == 0);
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
    math_expression_error error;
    node *call = math_expression_add(asset, asset->graph, "pos + input * movement * dt", &error);
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
    assert(math_expression_refresh(asset, call, &error) == 0);
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
    ImGuiContext *previous = ImGui_GetCurrentContext();
    ImGuiContext *context = ImGui_CreateContext(NULL);
    editor *e = editor_new(behavior_asset_new("Expression title"));
    math_expression_error error;
    node *call = editor_add_math_expression(e, "a + b", 0, 0, &error);
    assert(call);
    graph *generated = behavior_asset_find_graph(editor_behavior_asset(e), call->reference_id);
    assert(generated && !strcmp(generated->name, "a + b"));
    assert(editor_set_math_expression(e, call, "a * b", &error) == 0);
    assert(!strcmp(generated->name, "a * b"));
    assert(
        editor_command_apply(
            e,
            (editor_command){ .kind = EDITOR_COMMAND_RENAME_GRAPH,
                              .data.rename_graph = { generated->graph_id, "Custom Title" } }
        ) == 0
    );
    assert(editor_set_math_expression(e, call, "a / b", &error) == 0);
    assert(!strcmp(generated->name, "Custom Title"));
    assert(
        editor_command_apply(
            e,
            (editor_command){ .kind = EDITOR_COMMAND_RENAME_GRAPH,
                              .data.rename_graph = { generated->graph_id, "a / b" } }
        ) == 0
    );
    assert(editor_set_math_expression(e, call, "a - b", &error) == 0);
    assert(!strcmp(generated->name, "a - b"));
    editor_free(e);
    ImGui_DestroyContext(context);
    ImGui_SetCurrentContext(previous);
}

static void editor_history()
{
    ImGuiContext *previous = ImGui_GetCurrentContext();
    ImGuiContext *context = ImGui_CreateContext(NULL);
    editor *e = editor_new(behavior_asset_new("Expression history"));
    math_expression_error error;
    node *call = editor_add_math_expression(e, "a + b", 12, 34, &error);
    assert(call);
    uint64_t id = call->id;
    uint64_t pin_id = call->input_pins[0].id;
    assert(editor_set_math_expression(e, call, "a * b", &error) == 0);
    assert(editor_set_math_expression(e, call, "a +", &error) == -1);
    editor_undo(e);
    call = graph_node_by_id(editor_behavior_asset(e)->graph, id);
    assert(call && !strcmp(math_expression_source(editor_behavior_asset(e), call), "a + b"));
    assert(call->input_pins[0].id == pin_id);
    editor_redo(e);
    call = graph_node_by_id(editor_behavior_asset(e)->graph, id);
    assert(call && !strcmp(math_expression_source(editor_behavior_asset(e), call), "a * b"));
    assert(editor_open_graph_call(e, call->id) == 0);
    assert(editor_active_graph(e)->math_expression);
    assert(editor_has_inspection_selection(e));
    assert(!editor_spawn(e, registry_node_by_name("Real"), 0, 0));
    assert(!editor_add_math_expression(e, "x + y", 0, 0, &error));
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
    ImGui_DestroyContext(context);
    ImGui_SetCurrentContext(previous);
}

static void editor_delete_expression()
{
    ImGuiContext *previous = ImGui_GetCurrentContext();
    ImGuiContext *context = ImGui_CreateContext(NULL);
    behavior_asset *asset = behavior_asset_new("Delete expression");
    node *call = math_expression_add(asset, asset->graph, "negate(a)", NULL);
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
    node *sink = graph_add_node(asset->graph, registry_node_by_name("Abs"));
    assert(graph_connect_data(asset->graph, value, 0, call, 0) == 0);
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
    ImGui_DestroyContext(context);
    ImGui_SetCurrentContext(previous);
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
    node *expression = math_expression_add(asset, g, "speed * 2 + sin(pi / 2)", NULL);
    assert(expression);
    assert(graph_connect_data(g, get, 0, expression, 0) == 0);
    assert(graph_connect_data(g, expression, 0, set, 0) == 0);
    game_system *system = game_system_new(world, asset);
    assert(!compile_report_blocked(game_system_compile(system)));
    assert(game_system_uses_bytecode(system));
    flow_error error;
    GAME_GameMovement_get(world, entity)->speed = 3;
    assert(game_system_tick(system, 0, .016, &error) == FLOW_OK);
    assert(GAME_GameMovement_get(world, entity)->speed == 7);
    assert(game_system_tick(system, .016, .016, &error) == FLOW_OK);
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
    expression = math_expression_add(asset, g, "speed * 2 + 1", NULL);
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
    assert(game_system_tick(system, 0, .016, &error) == FLOW_OK);
    assert(GAME_GameMovement_get(world, entity)->speed == 15);
    assert(game_system_tick(system, .016, .016, &error) == FLOW_OK);
    assert(GAME_GameMovement_get(world, entity)->speed == 63);
    game_system_free(system);

    asset = behavior_asset_new("Random Expression System");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    g = asset->graph;
    g->dataflow = true;
    tick = graph_add_node(g, &NODE_Event);
    assert(graph_set_event_name(g, tick, "Tick") == 0);
    set = movement_field_node(g, "Set_GameMovement", false);
    expression = math_expression_add(asset, g, "rand() + rand()", NULL);
    assert(expression && graph_connect_data(g, expression, 0, set, 0) == 0);
    system = game_system_new(world, asset);
    assert(game_system_uses_bytecode(system));
    srand(912);
    double expected = (double)rand() / ((double)RAND_MAX + 1);
    expected += (double)rand() / ((double)RAND_MAX + 1);
    srand(912);
    assert(game_system_tick(system, 0, .016, &error) == FLOW_OK);
    assert(GAME_GameMovement_get(world, entity)->speed == (float)expected);
    assert(game_system_tick(system, .016, .016, &error) == FLOW_OK);
    assert(GAME_GameMovement_get(world, entity)->speed != (float)expected);
    game_system_free(system);
    ecs_world_free(world);
}

void test_math_expression()
{
    test_system_expression();
    vector_expression();
    field_expression();
    mixed_struct_expression();
    component_struct_input_expression();
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
    math_expression_error error;
    node *call = math_expression_add(asset, g, "a + b + a", &error);
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
    assert(math_expression_set(asset, call, "b + a + c", &error) == 0);
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
        assert(math_expression_set(asset, call, invalid[i], &error) == -1);
        assert(error.message[0] && error.offset >= 0 && error.length > 0);
        char *after = behavior_asset_save_json(asset);
        assert(!strcmp(snapshot, after));
        free(after);
    }
    assert(math_expression_set(asset, call, "b + c", &error) == 0);
    assert(call->input_count == 2 && call->input_pins[0].id == b_id);
    assert(acount(g->edges) == 1 && evaluate(asset, call) == 7);
    behavior_asset_free(asset);
    asset = behavior_asset_load_json(snapshot);
    free(snapshot);
    assert(asset);
    call = graph_node_by_id(asset->graph, call_id);
    assert(call && !strcmp(math_expression_source(asset, call), "b + a + c"));
    assert(call->input_pins[0].id == a_id && call->input_pins[1].id == b_id);
    assert(evaluate(asset, call) == 9);
    g = asset->graph;
    assert(graph_set_pin_type(g, call, 0, 0, PIN_BOOL, "bool") == 0);
    assert(acount(g->edges) == 1);
    assert(math_expression_set(asset, call, "b + a + c + 0", &error) == 0);
    assert(call->input_pins[0].type == PIN_F64 && call->input_pins[0].id == a_id);
    assert(call->input_pins[1].id == b_id && acount(g->edges) == 1);
    assert(call->input_pins[2].default_value.d == 4 && evaluate(asset, call) == 7);
    behavior_asset_free(asset);

    asset = behavior_asset_new("Random expression");
    call = math_expression_add(asset, asset->graph, "rand() + rand()", &error);
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
    puts("math expression PASS: precedence, functions, named pins, stable wires, atomic errors, serialization and random sampling");
}
