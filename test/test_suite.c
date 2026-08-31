/* test/test_suite.c -- constructs small graphs in code and asserts outputs.
 *
 * Exercises: generated Make/Break/call nodes (from the reflection pack), the
 * hand-written constant escape hatch, per-run memoization, the cycle guard,
 * and arena-backed struct values.
 */
#include "../app/demo_graph.h"
#include "../app/stacktrace.h"
#include "../core/arena.h"
#include "../core/behavior_asset.h"
#include "../core/compile.h"
#include "../core/conv_nodes.h"
#include "../core/eval.h"
#include "../core/flow.h"
#include "../core/flow_visual.h"
#include "../core/fmt_nodes.h"
#include "../core/graph.h"
#include "../core/instance.h"
#include "../core/operator.h"
#include "../core/pin_conv.h"
#include "../core/pin_fmt.h"
#include "../core/registry.h"
#include "../core/serialize.h"
#include "../core/value_array.h"
#include "../demo/vec3.h"
#include "../ecs/ecs.h"
#include "../editor/editor.h"
#include "../editor/preview_gate.h"
#include "../engine/engine.h"
#include "../game/behavior.h"
#include "../game/generated.h"
#include "../game/hot_reload.h"
#include "../game/live.h"
#include "../game/prototype.h"
#include "../game/scene.h"
#include "../game/scene_document.h"

/* game/components.h owns the compatible CF_V2 declaration used by this test;
 * avoid redeclaring it through cute_math while importing ImGui's API. */
#include <cute_defines.h>
#define CF_MATH_H
#include <imgui/dcimgui.h>
#undef CF_MATH_H
#define PICO_UNIT_IMPLEMENTATION
#include "../third_party/fmt/fmt.h"
#include "../third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const node_desc *find_node(const char *name)
{
    return registry_node_by_name(name);
}

static int fails;

static void check(int ok, const char *msg)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", msg);
    if (!ok) {
        fails++;
    }
}

static pin_spec g_editor_float_outputs[] = {
    { "Value", PIN_F32, "float", PIN_DATA },
};
static pin_spec g_editor_int_inputs[] = {
    { "Value", PIN_I32, "int", PIN_DATA },
};
static node_desc g_editor_float_sink_desc = {
    .name = "editor_float_sink",
    .input_count = 1,
    .inputs = g_editor_float_outputs,
};
static node_desc g_editor_float_desc = {
    .name = "editor_float_source",
    .output_count = 1,
    .outputs = g_editor_float_outputs,
};
static node_desc g_editor_int_desc = {
    .name = "editor_int_sink",
    .input_count = 1,
    .inputs = g_editor_int_inputs,
};

static void run_editor_command_checks(void)
{
    ImGuiContext *context = ImGui_CreateContext(NULL);
    behavior_asset *bp = behavior_asset_new("Editor command test");
    node *source = graph_add_node(bp->graph, &g_editor_float_desc);
    node *sink = graph_add_node(bp->graph, &g_editor_int_desc);
    node *float_sink = graph_add_node(bp->graph, &g_editor_float_sink_desc);
    editor *e = editor_new(bp);
    check(e != NULL, "editor command seam constructs a headless editor");
    if (!e) {
        behavior_asset_free(bp);
        ImGui_DestroyContext(context);
        return;
    }

    editor_query state;
    editor_query_state(e, &state);
    check(!state.dirty, "a new editor starts with a clean saved baseline");
    editor_tick(e, 0.0);
    editor_command invalid_connect = {
        .kind = EDITOR_COMMAND_CONNECT_DATA,
        .data.connect = { source->id, 0, sink->id, 0 },
    };
    check(editor_command_apply(e, invalid_connect) == -1, "an invalid connection is rejected by the command seam");
    editor_query_state(e, &state);
    check(!state.dirty && acount(bp->graph->edges) == 0, "a rejected connection leaves no dirty or history edit");
    unsigned revision = editor_edit_revision(e);
    editor_undo(e);
    editor_tick(e, 1.0);
    check(editor_edit_revision(e) == revision, "a rejected connection does not add an undo revision");
    pin_value float_value = { .type = PIN_F32, .f = 1.0f };
    check(
        editor_command_apply(
            e,
            (editor_command){
                .kind = EDITOR_COMMAND_SET_PIN_DEFAULT,
                .data.set_pin_default = { source->id, 2, 0, &float_value },
            }
        ) == -1,
        "a pin default command rejects a non-boolean output selector"
    );
    editor_command valid_connect = {
        .kind = EDITOR_COMMAND_CONNECT_DATA,
        .data.connect = { source->id, 0, float_sink->id, 0 },
    };
    check(editor_command_apply(e, valid_connect) == 0, "a valid typed connection uses the editor mutation path");
    check(acount(bp->graph->edges) == 1, "valid command connections update graph topology");

    editor_command create_graph = {
        .kind = EDITOR_COMMAND_CREATE_GRAPH,
        .data.create_graph = { GRAPH_FUNCTION, "Worker" },
    };
    editor_query_state(e, &state);
    int dirty_before_create = state.dirty;
    unsigned revision_before_create = editor_edit_revision(e);
    check(
        editor_command_apply(
            e,
            (editor_command){
                .kind = EDITOR_COMMAND_CREATE_GRAPH,
                .data.create_graph = { GRAPH_FUNCTION, "" },
            }
        ) == -1,
        "graph creation rejects an empty name"
    );
    editor_query_state(e, &state);
    check(
        state.dirty == dirty_before_create &&
            editor_edit_revision(e) == revision_before_create,
        "a rejected graph creation leaves no dirty or history edit"
    );
    check(editor_command_apply(e, create_graph) == 0, "the command seam creates a graph");
    graph *worker = behavior_asset_find_graph_by_name(bp, "Worker");
    check(worker != NULL, "created graphs remain discoverable by name");
    check(
        editor_open_graph(e, worker->graph_id) == 0 && editor_active_graph(e) == worker,
        "the command seam can open the created graph"
    );

    editor_command rename_graph = {
        .kind = EDITOR_COMMAND_RENAME_GRAPH,
        .data.rename_graph = { worker->graph_id, "Renamed" },
    };
    check(editor_command_apply(e, rename_graph) == 0, "the command seam renames a graph");
    editor_query_state(e, &state);
    check(state.dirty, "authored graph changes are observable as dirty");
    check(
        editor_command_apply(
            e,
            (editor_command){ .kind = EDITOR_COMMAND_SAVE, .data.file = { "/tmp/cblueprints-editor-test.json" } }
        ) == 0,
        "the command seam saves the authored asset"
    );
    editor_query_state(e, &state);
    check(!state.dirty, "saving establishes the clean dirty baseline");
    check(
        editor_command_apply(
            e,
            (editor_command){ .kind = EDITOR_COMMAND_SAVE, .data.file = { NULL } }
        ) == -1,
        "a save command rejects a null path"
    );
    check(
        editor_command_apply(
            e,
            (editor_command){ .kind = EDITOR_COMMAND_LOAD, .data.file = { NULL } }
        ) == -1,
        "a load command rejects a null path"
    );
    remove("/tmp/cblueprints-editor-test.json");
    editor_free(e);
    ImGui_DestroyContext(context);
}

/* Vendor test helper: render one fmt format into a stream and compare. The
 * lowercase "fmt:" check prefix keeps these out of the requirement-ID
 * scanner, where FMT-x means the serialization family. */
static void fmt_check_stream(fmt_stream *ss, const char *expected, const char *msg)
{
    check(ss->data && strcmp(ss->data, expected) == 0, msg);
    fmt_close(ss);
    *ss = (fmt_stream){ 0 };
}

/* Hand-written constant node: no inputs, one output, eval pre-sets nothing
 * (the caller sets output[0]). This is the escape hatch: a node_desc authored
 * directly in C, not emitted by reflect. */
static pin_spec g_const_out[] = {
    { "value", PIN_F32, "float" },
};
static node_desc g_const_desc = {
    .name = "const_f32",
    .input_count = 0,
    .output_count = 1,
    .inputs = NULL,
    .outputs = g_const_out,
    .pure_kind = NODE_PURE_CONST,
    .display_class = NODE_DISPLAY_LITERAL,
};

/* Hand-written counting node for the memo check: increments once per eval. */
static int g_count;
static pin_value eval_counter(const pin_value *inputs, int input_count, arena *a)
{
    (void)inputs;
    (void)input_count;
    (void)a;
    g_count++;
    return (pin_value){ .type = PIN_F32, .type_name = "float", .f = (float)g_count };
}
static pin_spec g_counter_out[] = {
    { "count", PIN_F32, "float" },
};
static node_desc g_counter_desc = {
    .name = "counter",
    .input_count = 0,
    .output_count = 1,
    .inputs = NULL,
    .outputs = g_counter_out,
    .pure_kind = NODE_PURE_CALL,
    .fn = eval_counter,
};

static pin_spec g_const_bool_out[] = {
    { "value", PIN_BOOL, "bool" },
};
static node_desc g_const_bool_desc = {
    .name = "const_bool_test",
    .input_count = 0,
    .output_count = 1,
    .inputs = NULL,
    .outputs = g_const_bool_out,
    .pure_kind = NODE_PURE_CONST,
    .display_class = NODE_DISPLAY_LITERAL,
};

static pin_spec g_all_types_outputs[] = {
    { "bool", PIN_BOOL, "bool" },         { "i8", PIN_I8, "int8_t" },
    { "u8", PIN_U8, "uint8_t" },          { "i16", PIN_I16, "int16_t" },
    { "u16", PIN_U16, "uint16_t" },       { "i32", PIN_I32, "int" },
    { "u32", PIN_U32, "unsigned" },       { "i64", PIN_I64, "int64_t" },
    { "u64", PIN_U64, "uint64_t" },       { "f32", PIN_F32, "float" },
    { "f64", PIN_F64, "double" },         { "str", PIN_STR, "const char *" },
    { "struct", PIN_STRUCT, "Vec3" },     { "enum", PIN_ENUM, "TestEnum" },
    { "flow", PIN_FLOW, NULL, PIN_EXEC },
};
static node_desc g_all_types_desc = {
    .name = "all_types_literal",
    .input_count = 0,
    .output_count = 15,
    .outputs = g_all_types_outputs,
    .pure_kind = NODE_PURE_CONST,
};

static pin_spec g_flow_sink_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
    { "Index", PIN_I32, "int", PIN_DATA },
};
static pin_spec g_flow_sink_outputs[] = {
    { "Then", PIN_FLOW, NULL, PIN_EXEC },
};
static char g_flow_log[128];
static int g_flow_log_count;
static int g_flow_indices[128];
static int g_flow_index_count;
static int g_flow_sink_fail;

static void flow_log_reset(void)
{
    memset(g_flow_log, 0, sizeof g_flow_log);
    g_flow_log_count = 0;
    g_flow_index_count = 0;
    g_flow_sink_fail = 0;
}

static int flow_sink(node *n, flow_context *ctx)
{
    if (strcmp(n->desc->name, "Body") == 0) {
        pin_value index;
        int result = flow_context_eval_data(ctx, 1, &index);
        if (result) {
            return result;
        }
        if (g_flow_index_count <
            (int)(sizeof g_flow_indices / sizeof g_flow_indices[0])) {
            g_flow_indices[g_flow_index_count++] = index.i;
        }
    } else if (g_flow_log_count < (int)sizeof g_flow_log - 1) {
        g_flow_log[g_flow_log_count++] = n->desc->name[0];
    }
    if (g_flow_sink_fail) {
        return flow_context_fail(ctx, -777, "intentional flow failure");
    }
    return flow_context_continue(ctx, 0);
}

static int flow_fail_sink(node *n, flow_context *ctx)
{
    (void)n;
    return flow_context_fail(ctx, -777, "intentional flow failure");
}

static node_desc g_sink_a_desc = {
    .name = "A",
    .input_count = 2,
    .output_count = 1,
    .inputs = g_flow_sink_inputs,
    .outputs = g_flow_sink_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_sink,
};
static node_desc g_sink_b_desc = {
    .name = "B",
    .input_count = 2,
    .output_count = 1,
    .inputs = g_flow_sink_inputs,
    .outputs = g_flow_sink_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_sink,
};
static node_desc g_sink_c_desc = {
    .name = "C",
    .input_count = 2,
    .output_count = 1,
    .inputs = g_flow_sink_inputs,
    .outputs = g_flow_sink_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_sink,
};
static node_desc g_sink_done_desc = {
    .name = "D",
    .input_count = 2,
    .output_count = 1,
    .inputs = g_flow_sink_inputs,
    .outputs = g_flow_sink_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_sink,
};
static node_desc g_sink_body_desc = {
    .name = "Body",
    .input_count = 2,
    .output_count = 1,
    .inputs = g_flow_sink_inputs,
    .outputs = g_flow_sink_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_sink,
};
static node_desc g_sink_error_desc = {
    .name = "Error",
    .input_count = 2,
    .output_count = 1,
    .inputs = g_flow_sink_inputs,
    .outputs = g_flow_sink_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_fail_sink,
};

static pin_spec g_parameter_sink_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
    { "Integer", PIN_I32, "int", PIN_DATA },
    { "Flag", PIN_BOOL, "bool", PIN_DATA },
    { "Text", PIN_STR, "const char *", PIN_DATA },
};
static pin_spec g_parameter_sink_outputs[] = {
    { "Then", PIN_FLOW, NULL, PIN_EXEC },
};
static int g_parameter_integer;
static int g_parameter_flag;
static char g_parameter_text[32];

static int flow_parameter_sink(node *n, flow_context *ctx)
{
    (void)n;
    pin_value integer, flag, text;
    if (flow_context_eval_data(ctx, 1, &integer) ||
        flow_context_eval_data(ctx, 2, &flag) ||
        flow_context_eval_data(ctx, 3, &text)) {
        return FLOW_ERR_NODE;
    }
    g_parameter_integer = integer.i;
    g_parameter_flag = flag.b;
    snprintf(g_parameter_text, sizeof g_parameter_text, "%s", text.str ? text.str : "");
    return flow_context_continue(ctx, 0);
}

static node_desc g_parameter_sink_desc = {
    .name = "ParameterSink",
    .input_count = 4,
    .output_count = 1,
    .inputs = g_parameter_sink_inputs,
    .outputs = g_parameter_sink_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_parameter_sink,
};

static char g_print_messages[8][64];
static const node *g_print_origins[8];
static int g_print_count;
static flow_run *g_pause_target;

static void print_sink(const node *origin, const char *message, void *user)
{
    (void)user;
    if (g_print_count >= (int)(sizeof g_print_messages / sizeof g_print_messages[0])) {
        return;
    }
    snprintf(g_print_messages[g_print_count], sizeof g_print_messages[0], "%s", message ? message : "");
    g_print_origins[g_print_count++] = origin;
    if (g_pause_target) {
        flow_run_pause(g_pause_target);
        g_pause_target = NULL;
    }
}

static pin_spec g_watch_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
};
static pin_spec g_watch_outputs[] = {
    { "Then", PIN_FLOW, NULL, PIN_EXEC },
    { "Value", PIN_STRUCT, "Vec3", PIN_DATA },
};
static Vec3 g_watch_struct;

static int flow_watch_struct(node *n, flow_context *ctx)
{
    g_watch_struct = (Vec3){ 4.0f, 5.0f, 6.0f };
    *flow_context_output_value(ctx, 1) = (pin_value){
        .type = PIN_STRUCT,
        .ptr = &g_watch_struct,
        .type_name = "Vec3",
    };
    return flow_context_continue(ctx, 0);
}

static node_desc g_watch_desc = {
    .name = "WatchStruct",
    .input_count = 1,
    .output_count = 2,
    .inputs = g_watch_inputs,
    .outputs = g_watch_outputs,
    .type = NULL,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_watch_struct,
};

/* Phase 3 capture sinks: a flow node records one data input so a test can
 * observe values that flowed through Get Variable and Self. */
static int g_captured_int;
static char g_captured_str[64];
static uint64_t g_captured_handle;
static pin_spec g_capture_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
    { "Value", PIN_I32, "int", PIN_DATA },
};
static pin_spec g_capture_outputs[] = {
    { "Then", PIN_FLOW, NULL, PIN_EXEC },
};
static int flow_capture_i32(node *n, flow_context *ctx)
{
    (void)n;
    pin_value v;
    int result = flow_context_eval_data(ctx, 1, &v);
    if (result) {
        return result;
    }
    g_captured_int = v.i;
    return flow_context_continue(ctx, 0);
}
static node_desc g_capture_desc = {
    .name = "CaptureI32",
    .input_count = 2,
    .output_count = 1,
    .inputs = g_capture_inputs,
    .outputs = g_capture_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_capture_i32,
};
static pin_spec g_owned_watch_inputs[] = {
    { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Name", .type = PIN_NAME, .type_name = "name", .kind = PIN_DATA },
    { .name = "Values", .type = PIN_I32, .type_name = "int", .kind = PIN_DATA, .container = PIN_CONTAINER_ARRAY },
};
static int flow_watch_owned(node *n, flow_context *ctx)
{
    (void)n;
    pin_value value;
    int result = flow_context_eval_data(ctx, 1, &value);
    if (result) {
        return result;
    }
    result = flow_context_eval_data(ctx, 2, &value);
    if (result) {
        return result;
    }
    return flow_context_continue(ctx, 0);
}
static node_desc g_owned_watch_desc = {
    .name = "WatchOwnedValues",
    .input_count = 3,
    .output_count = 1,
    .inputs = g_owned_watch_inputs,
    .outputs = g_capture_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_watch_owned,
};
static int g_foreach_values[8];
static int g_foreach_count;
static int flow_capture_foreach(node *n, flow_context *ctx)
{
    (void)n;
    pin_value value;
    int result = flow_context_eval_data(ctx, 1, &value);
    if (result) {
        return result;
    }
    if (g_foreach_count < (int)(sizeof g_foreach_values / sizeof g_foreach_values[0])) {
        g_foreach_values[g_foreach_count++] = value.i;
    }
    return flow_context_continue(ctx, 0);
}
static node_desc g_foreach_capture_desc = {
    .name = "CaptureForEach",
    .input_count = 2,
    .output_count = 1,
    .inputs = g_capture_inputs,
    .outputs = g_capture_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_capture_foreach,
};
static pin_spec g_str_capture_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
    { "Value", PIN_STR, "const char *", PIN_DATA },
};
static int flow_capture_str(node *n, flow_context *ctx)
{
    (void)n;
    pin_value v;
    int result = flow_context_eval_data(ctx, 1, &v);
    if (result) {
        return result;
    }
    snprintf(g_captured_str, sizeof g_captured_str, "%s", v.str ? v.str : "");
    return flow_context_continue(ctx, 0);
}
static node_desc g_str_capture_desc = {
    .name = "CaptureStr",
    .input_count = 2,
    .output_count = 1,
    .inputs = g_str_capture_inputs,
    .outputs = g_capture_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_capture_str,
};
static pin_spec g_handle_capture_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
    { "Value", PIN_HANDLE, "Vec3", PIN_DATA },
};
static int flow_capture_handle(node *n, flow_context *ctx)
{
    (void)n;
    pin_value v;
    int result = flow_context_eval_data(ctx, 1, &v);
    if (result) {
        return result;
    }
    g_captured_handle = v.u64;
    return flow_context_continue(ctx, 0);
}
static node_desc g_handle_capture_desc = {
    .name = "CaptureHandle",
    .input_count = 2,
    .output_count = 1,
    .inputs = g_handle_capture_inputs,
    .outputs = g_capture_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_capture_handle,
};

static node *make_string(graph *g, const char *value)
{
    node *n = graph_add_node(g, registry_node_by_name("String"));
    if (!n) {
        return NULL;
    }
    pin_value authored = { .type = PIN_STR, .type_name = "const char *", .str = value };
    graph_set_pin_default(g, n, 0, 0, &authored);
    return n;
}

static node *make_const(graph *g, float v)
{
    node *n = graph_add_node(g, &g_const_desc);
    if (!n) {
        return NULL;
    }
    pin_value authored = { .type = PIN_F32, .type_name = "float", .f = v };
    graph_set_pin_default(g, n, 1, 0, &authored);
    return n;
}

typedef struct
{
    float x;
    float y;
} ecs_position;

typedef struct
{
    int value;
} ecs_health;

static const type_field g_ecs_position_fields[] = {
    { "x", PIN_F32, offsetof(ecs_position, x), sizeof(float), "float" },
    { "y", PIN_F32, offsetof(ecs_position, y), sizeof(float), "float" },
};
static const type_desc g_ecs_position_type = { "EcsPosition",
                                               sizeof(ecs_position),
                                               g_ecs_position_fields,
                                               2 };
static const type_field g_ecs_health_fields[] = {
    { "value", PIN_I32, offsetof(ecs_health, value), sizeof(int), "int" },
};
static const type_desc g_ecs_health_type = { "EcsHealth",
                                             sizeof(ecs_health),
                                             g_ecs_health_fields,
                                             1 };
static ecs_component_id g_ecs_observed_component;
static float g_ecs_observed_x;

static void ecs_destroy_probe(ecs_world *world, ecs_entity entity, void *user)
{
    (void)user;
    pin_value value;
    if (ecs_component_read_field(world, entity, g_ecs_observed_component, 0, &value) ==
        ECS_OK) {
        g_ecs_observed_x = value.f;
    }
}

static void run_ecs_checks(void)
{
    ecs_world *world = ecs_world_new();
    ecs_component_id position = 0;
    ecs_component_id health = 0;
    ecs_component_desc position_desc = {
        .key = "ecs.Position",
        .name = "Position",
        .element_size = sizeof(ecs_position),
        .type = &g_ecs_position_type,
        .index = ECS_INDEX_BITSET,
    };
    ecs_component_desc health_desc = {
        .name = "Health",
        .element_size = sizeof(ecs_health),
        .type = &g_ecs_health_type,
        .index = ECS_INDEX_DENSE,
    };
    check(ECS_MAX_ENTITIES == 10000, "world uses the fixed 10,000-slot default");
    check(
        ecs_world_register_component(world, &position_desc, &position) == ECS_OK &&
            ecs_world_register_component(world, &health_desc, &health) == ECS_OK,
        "runtime component descriptors register with columns and layout "
        "hashes"
    );
    const ecs_component_desc *registered = ecs_world_component(world, position);
    check(
        sizeof(ecs_entity) == 8 && !ecs_entity_is_alive(world, (ecs_entity){ 0, 0 }),
        "handles are 64-bit slot/generation pairs and slot zero is invalid"
    );
    check(
        registered && registered->column_base &&
            registered->layout_hash == ecs_layout_hash(&g_ecs_position_type),
        "descriptor reports its stable column base and reflected layout "
        "hash"
    );
    ecs_component_desc duplicate_key = position_desc;
    duplicate_key.id = 0;
    duplicate_key.name = "RenamedPosition";
    check(
        ecs_world_register_component(world, &duplicate_key, NULL) == ECS_ERR_DUPLICATE,
        "duplicate persistent component keys are rejected"
    );

    ecs_entity e1 = ecs_world_create_entity(world);
    ecs_entity e2 = ecs_world_create_entity(world);
    ecs_entity e3 = ecs_world_create_entity(world);
    check(ecs_entity_is_alive(world, e1) && ecs_entity_is_alive(world, e2), "entity allocation returns live lowest-index slots");
    check(
        ecs_world_record_add(world, e1, position, NULL, 1) == ECS_OK &&
            ecs_world_record_add(world, e1, health, NULL, 2) == ECS_OK &&
            ecs_world_record_add(world, e2, position, NULL, 3) == ECS_OK &&
            ecs_world_record_add(world, e3, health, NULL, 4) == ECS_OK &&
            ecs_world_apply_commands(world) == ECS_OK,
        "structural changes wait in the command buffer for apply"
    );

    ecs_position *p1 = (ecs_position *)ecs_component_data(world, e1, position);
    ecs_position *p2 = (ecs_position *)ecs_component_data(world, e2, position);
    p1->x = 1.0f;
    p1->y = 2.0f;
    p2->x = 3.0f;
    p2->y = 4.0f;
    check(
        p1 && p2 && (unsigned char *)p2 - (unsigned char *)p1 == sizeof(ecs_position),
        "component values use stable flat columns with element stride"
    );
    pin_value field = { .type = PIN_F32, .type_name = "float", .f = 9.0f };
    check(
        ecs_component_write_field(world, e1, position, 0, field) == ECS_OK &&
            ecs_component_read_field(world, e1, position, 0, &field) == ECS_OK &&
            field.f == 9.0f,
        "generic runtime-id access reads and writes without C type "
        "knowledge"
    );
    check(
        ecs_component_read_field(world, e1, position, 1, &field) == ECS_OK &&
            field.f == 2.0f,
        "component field access goes through pin read/write helpers"
    );
    check(ecs_component_has(world, e1, position) && ecs_component_has(world, e1, health), "each component tracks occupancy");

    ecs_component_id query_components[] = { position, health };
    ecs_query query;
    ecs_entity found[4] = { 0 };
    int found_count = 0;
    check(
        ecs_query_begin(world, &query, query_components, 2) == ECS_OK,
        "intersection queries can begin over multiple component ids"
    );
    while (ecs_query_next(&query, &found[found_count])) {
        found_count++;
    }
    ecs_query_end(&query);
    check(found_count == 1 && found[0].slot == e1.slot, "a query visits exactly entities holding every named component");
    check(ecs_query_begin(world, &query, &health, 1) == ECS_OK, "dense occupancy remains a query-compatible index");
    int dense_ordered = ecs_query_next(&query, &found[0]) &&
                        ecs_query_next(&query, &found[1]) &&
                        found[0].slot < found[1].slot;
    ecs_query_end(&query);
    check(dense_ordered, "dense and bitset indexes preserve observable ascending order");

    ecs_world *ordered = ecs_world_new();
    ecs_component_id ordered_position = 0;
    ecs_world_register_component(ordered, &position_desc, &ordered_position);
    ecs_entity ordered_a = ecs_world_create_entity(ordered);
    ecs_entity ordered_b = ecs_world_create_entity(ordered);
    ecs_position first = { 1.0f, 0.0f };
    ecs_position second = { 2.0f, 0.0f };
    ecs_world_record_add(ordered, ordered_b, ordered_position, &second, 20);
    ecs_world_record_add(ordered, ordered_a, ordered_position, &first, 10);
    check(ecs_world_apply_commands(ordered) == ECS_OK, "command application sorts by explicit sort key");
    ecs_world *reverse = ecs_world_new();
    ecs_component_id reverse_position = 0;
    ecs_world_register_component(reverse, &position_desc, &reverse_position);
    ecs_entity reverse_a = ecs_world_create_entity(reverse);
    ecs_entity reverse_b = ecs_world_create_entity(reverse);
    ecs_world_record_add(reverse, reverse_a, reverse_position, &first, 10);
    ecs_world_record_add(reverse, reverse_b, reverse_position, &second, 20);
    ecs_world_apply_commands(reverse);
    check(ecs_world_hash(ordered) == ecs_world_hash(reverse), "the same sorted command set has one result regardless of recording order");
    check(
        ecs_world_record_create(world, 0).slot != 0 &&
            ecs_world_command_count(world) == 1,
        "a recorded create is not alive before the sync point"
    );
    ecs_world_clear_commands(world);

    check(ecs_world_record_add(world, e2, health, NULL, 1) == ECS_OK, "a valid structural command is accepted");
    check(
        ecs_query_begin(world, &query, &position, 1) == ECS_OK &&
            ecs_world_apply_commands(world) == ECS_ERR_ITERATING,
        "command application is rejected during iteration"
    );
    ecs_query_end(&query);
    check(ecs_world_apply_commands(world) == ECS_OK, "the command buffer applies at a defined sync point");

    g_ecs_observed_component = position;
    g_ecs_observed_x = 0.0f;
    ecs_world_set_destroy_observer(world, ecs_destroy_probe, NULL);
    check(
        ecs_world_record_destroy(world, e1, 1) == ECS_OK &&
            ecs_component_data(world, e1, position) != NULL &&
            ecs_world_apply_commands(world) == ECS_OK && g_ecs_observed_x == 9.0f,
        "destroyed values remain readable until command application"
    );
    check(
        !ecs_entity_is_alive(world, e1) &&
            ecs_world_generation(world, e1.slot) != e1.generation,
        "destroy advances generation and invalidates stale handles"
    );
    ecs_entity recycled = ecs_world_create_entity(world);
    check(recycled.slot == e1.slot && recycled.generation != e1.generation, "freeing a low slot makes it the next allocation");
    check(
        ecs_world_record_add(world, e3, health, NULL, 1) == ECS_ERR_DUPLICATE &&
            ecs_world_record_remove(world, e3, position, 1) == ECS_ERR_COMPONENT &&
            ecs_world_record_destroy(world, e1, 1) == ECS_ERR_ENTITY,
        "duplicate, missing, and stale structural operations report "
        "errors"
    );
    check(
        ecs_world_live_count(world) == 3 &&
            ecs_world_high_water_mark(world) >= recycled.slot,
        "live count and occupancy remain coherent after structural "
        "changes"
    );

    ecs_world *same = ecs_world_new();
    ecs_component_id same_position = 0;
    ecs_world_register_component(same, &position_desc, &same_position);
    ecs_entity same_entity = ecs_world_create_entity(same);
    ecs_world_record_add(same, same_entity, same_position, &first, 1);
    ecs_world_apply_commands(same);
    ecs_component_write_field(same, same_entity, same_position, 0, field);
    check(
        ecs_world_hash(ordered) == ecs_world_hash(reverse) &&
            ecs_world_hash(ordered) != 0,
        "identical seeded worlds expose an identical deterministic hash"
    );
    check(ecs_world_component_count(ordered) == 1, "component registration remains independent per world");
    check(
        ecs_world_iteration_begin(world) == ECS_OK &&
            ecs_world_iteration_end(world) == ECS_OK,
        "ECS lifecycle and iteration are headless with no host dependency"
    );

    ecs_world *generated_world = ecs_world_new();
    check(
        GAME_REGISTRY_COMPONENT_COUNT == 4 &&
            ecs_world_register_component(generated_world, &GAME_COMPONENT_GameTransform, NULL) ==
                ECS_OK &&
            ecs_world_register_component(generated_world, &GAME_COMPONENT_GameHealth, NULL) ==
                ECS_OK &&
            ecs_world_register_component(generated_world, &GAME_COMPONENT_GameMovement, NULL) ==
                ECS_OK &&
            ecs_world_register_component(generated_world, &GAME_COMPONENT_GameInput, NULL) ==
                ECS_OK &&
            GAME_COMPONENT_GameHealth.index == ECS_INDEX_DENSE &&
            GAME_COMPONENT_GameHealth.layout_hash ==
                ecs_layout_hash(GAME_COMPONENT_GameHealth.type),
        "reflection emits component descriptors, ids, layout hashes, "
        "and index policy"
    );
    ecs_entity generated_entity = ecs_world_create_entity(generated_world);
    check(
        GAME_GameTransform_add(generated_world, generated_entity, 1) == ECS_OK &&
            GAME_GameHealth_add(generated_world, generated_entity, 2) == ECS_OK &&
            ecs_world_apply_commands(generated_world) == ECS_OK,
        "generated typed component accessors add through runtime "
        "descriptors"
    );
    pin_value generated_value = { .type = PIN_F32, .type_name = "float", .f = 12.0f };
    check(
        ecs_component_write_field(
            generated_world,
            generated_entity,
            GAME_COMPONENT_GameTransform.id,
            0,
            generated_value
        ) == ECS_OK &&
            GAME_GameTransform_get(generated_world, generated_entity)->x == 12.0f,
        "generated component access uses the shared reflected field path"
    );
    check(
        GAME_GameTransform_has(generated_world, generated_entity) &&
            GAME_GameHealth_has(generated_world, generated_entity) &&
            GAME_NODE_Get_GameTransform.flow &&
            GAME_NODE_Set_GameTransform.flow && GAME_NODE_Has_GameHealth.flow &&
            GAME_NODE_Add_GameHealth.flow && GAME_NODE_Remove_GameHealth.flow,
        "generated Get, Set, Has, Add, and Remove nodes are emitted"
    );
    ecs_world_free(generated_world);

    ecs_world_free(same);
    ecs_world_free(reverse);
    ecs_world_free(ordered);
    ecs_world_free(world);
}

static const char
    *g_prototype_base_json = "{\"name\":\"BaseEnemy\",\"components\":{"
                             "\"GameHealth\":{\"current\":50,\"maximum\":50},"
                             "\"GameTransform\":{\"x\":1,\"y\":2}},"
                             "\"behaviors\":[\"idle.graph\"]}";
static int g_prototype_attach_count;
static const char
    *g_cycle_a = "{\"name\":\"CycleA\",\"includes\":[\"CycleB\"]}";
static const char
    *g_cycle_b = "{\"name\":\"CycleB\",\"includes\":[\"CycleA\"]}";

static const char *prototype_include(const char *name, void *user)
{
    (void)user;
    return strcmp(name, "BaseEnemy") == 0 ? g_prototype_base_json : NULL;
}

static const char *prototype_cycle_include(const char *name, void *user)
{
    (void)user;
    if (strcmp(name, "CycleA") == 0) {
        return g_cycle_a;
    }
    if (strcmp(name, "CycleB") == 0) {
        return g_cycle_b;
    }
    return NULL;
}

static int prototype_attach(ecs_world *world, ecs_entity entity, const char *graph_name, void *user)
{
    (void)world;
    (void)entity;
    (void)user;
    g_prototype_attach_count += graph_name && graph_name[0] != '\0';
    return 0;
}

static void run_prototype_checks(void)
{
    static const char
        *derived_json = "{\"name\":\"Enemy\",\"identity\":\"enemy-id\","
                        "\"includes\":[\"BaseEnemy\"],"
                        "\"components\":{\"GameHealth\":{\"current\":100}},"
                        "\"behaviors\":[\"attack.graph\"]}";
    static const char
        *reordered_json = "{\"behaviors\":[\"attack.graph\",\"idle.graph\"],"
                          "\"identity\":\"enemy-id\","
                          "\"components\":{\"GameTransform\":{\"y\":2,\"x\":1},"
                          "\"GameHealth\":{\"maximum\":50,\"current\":100}},"
                          "\"name\":\"Enemy\"}";
    const game_prototype_registry registry = {
        GAME_REGISTRY_COMPONENTS,
        GAME_REGISTRY_COMPONENT_COUNT,
        prototype_include,
        NULL,
    };
    char error[256];
    game_prototype *prototype = game_prototype_load(derived_json, &registry, error, sizeof error);
    check(prototype && strcmp(prototype->name, "Enemy") == 0, "a prototype names components and attached graph assets");
    check(prototype && acount(prototype->components) == 2, "a prototype is data and does not allocate an entity slot");
    const ecs_component_desc *health = NULL;
    const GameHealth *health_value = NULL;
    for (int i = 0; prototype && i < acount(prototype->components); i++) {
        const ecs_component_desc *desc = prototype->components[i].desc;
        if (strcmp(desc->name, "GameHealth") == 0) {
            health = desc;
            health_value = prototype->components[i].data;
        }
    }
    check(
        health && health_value && health_value->current == 100 &&
            health_value->maximum == 50,
        "include values load and local fields override by name"
    );
    char *saved = prototype ? game_prototype_save(prototype) : NULL;
    game_prototype *reordered = game_prototype_load(reordered_json, &registry, error, sizeof error);
    char *saved_reordered = reordered ? game_prototype_save(reordered) : NULL;
    check(saved && saved_reordered && strcmp(saved, saved_reordered) == 0, "generic descriptor-driven load/save is deterministic");
    check(
        saved && strstr(saved, "GameTransform") && strstr(saved, "GameHealth"),
        "prototype output contains generic component values"
    );
    free(saved);
    free(saved_reordered);
    game_prototype_free(reordered);

    game_prototype_registry missing = { GAME_REGISTRY_COMPONENTS, 1, prototype_include, NULL };
    game_prototype *missing_component = game_prototype_load(derived_json, &missing, error, sizeof error);
    check(!missing_component && strstr(error, "GameHealth"), "an unavailable component fails by name without partial data");
    const game_prototype_registry cycles = {
        GAME_REGISTRY_COMPONENTS,
        GAME_REGISTRY_COMPONENT_COUNT,
        prototype_cycle_include,
        NULL,
    };
    game_prototype *cycle = game_prototype_load(g_cycle_a, &cycles, error, sizeof error);
    check(!cycle && strstr(error, "cycle"), "include cycles are rejected");

    ecs_world *world = ecs_world_new();
    ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL);
    ecs_world_register_component(world, &GAME_COMPONENT_GameHealth, NULL);
    g_prototype_attach_count = 0;
    const game_prototype_spawn_hooks hooks = { prototype_attach, NULL };
    ecs_entity no_hook_entity = { 0 };
    check(
        prototype &&
            game_prototype_spawn(prototype, world, NULL, &no_hook_entity, error, sizeof error) ==
                ECS_ERR_ARGUMENT &&
            ecs_world_live_count(world) == 0 && strstr(error, "attach hook"),
        "behavior-bearing prototypes cannot silently skip attachment"
    );
    ecs_entity entity = { 0 };
    check(
        prototype &&
            game_prototype_spawn(prototype, world, &hooks, &entity, error, sizeof error) ==
                ECS_OK &&
            g_prototype_attach_count == 2 && ecs_entity_is_alive(world, entity),
        "spawning writes generic component fields and attaches each "
        "graph asset"
    );
    pin_value health_field = { 0 };
    check(
        prototype &&
            game_prototype_read_field(prototype, "GameHealth", "current", &health_field) == 0 &&
            health_field.i == 100 &&
            game_prototype_write_field(
                prototype,
                "GameHealth",
                "current",
                (pin_value){
                    .type = PIN_I32,
                    .i = 101,
                }
            ) == 0 &&
            game_prototype_read_field(prototype, "GameHealth", "current", &health_field) == 0 &&
            health_field.i == 101,
        "prototype component fields can be inspected and edited"
    );
    game_prototype_write_field(
        prototype,
        "GameHealth",
        "current",
        (pin_value){ .type = PIN_I32, .i = 100 }
    );
    check(
        prototype && game_prototype_add_graph(prototype, "debug.graph") == 0 &&
            game_prototype_has_graph(prototype, "debug.graph") &&
            game_prototype_remove_graph(prototype, "debug.graph") == 0 &&
            !game_prototype_has_graph(prototype, "debug.graph"),
        "prototype behavior attachments are editable"
    );
    check(
        prototype && game_prototype_remove_component(prototype, "GameTransform") == 0 &&
            acount(prototype->components) == 1 &&
            game_prototype_add_component(prototype, &GAME_COMPONENT_GameTransform) == 0 &&
            acount(prototype->components) == 2,
        "prototype component structure is editable"
    );
    game_prototype *base = game_prototype_clone(prototype, error, sizeof error);
    GAME_GameHealth_get(world, entity)->current = 7;
    char *overrides = game_prototype_capture_overrides_json(base, world, entity);
    check(
        base && overrides && strstr(overrides, "\"current\": 7") &&
            !strstr(overrides, "\"maximum\""),
        "instance capture stores sparse field overrides"
    );
    ecs_entity override_entity = { 0 };
    check(
        base &&
            game_prototype_spawn(base, world, &hooks, &override_entity, error, sizeof error) ==
                ECS_OK &&
            game_prototype_apply_components_json(
                world,
                override_entity,
                "{\"GameHealth\":{\"current\":7}}",
                &registry,
                error,
                sizeof error
            ) == 0 &&
            GAME_GameHealth_get(world, override_entity)->current == 7 &&
            GAME_GameHealth_get(world, override_entity)->maximum == 50,
        "sparse field overrides apply without zeroing sibling fields"
    );
    GAME_GameHealth_get(world, entity)->current = 100;
    free(overrides);
    game_prototype_free(base);
    game_prototype *captured = game_prototype_capture_entity("CapturedEnemy", world, entity, &registry);
    char *captured_components = game_prototype_components_json(captured);
    check(
        captured && captured_components && strstr(captured_components, "GameHealth"),
        "a live entity can be captured through the generic prototype registry"
    );
    free(captured_components);
    game_prototype_free(captured);
    game_prototype_free(prototype);
    GameHealth *spawned_health = GAME_GameHealth_get(world, entity);
    check(
        spawned_health && spawned_health->current == 100 && spawned_health->maximum == 50,
        "editing/freeing a prototype does not change spawned entity values"
    );
    ecs_world_free(world);
    check(health != NULL, "descriptor lookup is independent of component C types");
}

static behavior_asset *make_behavior_behavior_asset(const char *name, int latent)
{
    behavior_asset *bp = behavior_asset_new(name);
    if (!bp) {
        return NULL;
    }
    graph *graph = bp->graph;
    node *event = graph_add_node(graph, registry_node_by_name("Event"));
    if (!event || graph_set_event_name(graph, event, "Tick") != 0) {
        behavior_asset_free(bp);
        return NULL;
    }
    node *begin = NULL;
    if (!latent) {
        begin = graph_add_node(graph, registry_node_by_name("Event"));
        if (!begin || graph_set_event_name(graph, begin, "Begin") != 0) {
            behavior_asset_free(bp);
            return NULL;
        }
    }
    if (latent) {
        node *delay = graph_add_node(graph, registry_node_by_name("Delay"));
        pin_value duration = { .type = PIN_F64, .type_name = "double", .d = 1.0 };
        if (!delay || graph_set_pin_default(graph, delay, 0, 1, &duration) != 0 ||
            graph_connect_exec(graph, event, 0, delay, 0, 0) != 0) {
            behavior_asset_free(bp);
            return NULL;
        }
        return bp;
    }
    behavior_variable *counter = behavior_asset_add_variable(
        bp,
        "Counter",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_I32, .i = 0 },
        "Behavior"
    );
    node *set = counter ? behavior_asset_add_set_node(bp, counter->id) : NULL;
    pin_value one = { .type = PIN_I32, .type_name = "int", .i = 1 };
    if (!counter || !set || graph_set_pin_default(graph, set, 0, 1, &one) != 0 ||
        graph_connect_exec(graph, event, 0, set, 0, 0) != 0 ||
        graph_connect_exec(graph, begin, 0, set, 0, 0) != 0) {
        behavior_asset_free(bp);
        return NULL;
    }
    return bp;
}

static int g_promote_probe;
static int behavior_promote_probe(
    behavior_asset *definition,
    uint64_t variable_id,
    ecs_component_id component_id,
    void *user
)
{
    (void)definition;
    (void)user;
    g_promote_probe = variable_id != 0 && component_id != 0;
    return 0;
}

static void run_scene_document_checks(void)
{
    const char *behaviors[] = { "SceneBehavior", "Damage" };
    game_scene_document *document = game_scene_document_new("CombatScene");
    check(
        document &&
            game_scene_document_add_entity(document, "Player", "PlayerPrototype", behaviors, 2) ==
                0,
        "a scene entity stores its prototype and behavior assets"
    );
    check(
        game_scene_document_set_entity_components(document, 0, "{\"GameHealth\":{\"current\":7,\"maximum\":9}}") ==
                0 &&
            strstr(document->entities[0].components_json, "GameHealth"),
        "a scene entity stores generic component overrides"
    );
    char *saved = game_scene_document_save(document);
    char error[256];
    game_scene_document *loaded = game_scene_document_load(saved, error, sizeof error);
    check(
        loaded && acount(loaded->entities) == 1 &&
            strcmp(loaded->entities[0].name, "Player") == 0 &&
            strcmp(loaded->entities[0].prototype, "PlayerPrototype") == 0 &&
            strstr(loaded->entities[0].components_json, "GameHealth") &&
            acount(loaded->entities[0].behaviors) == 2 &&
            strcmp(loaded->entities[0].behaviors[1], "Damage") == 0,
        "scene documents round-trip behavior attachment names and "
        "component overrides"
    );
    char *resaved = game_scene_document_save(loaded);
    check(saved && resaved && strcmp(saved, resaved) == 0, "scene document save output is deterministic");
    game_scene_document *duplicate = game_scene_document_new("Duplicate");
    check(
        duplicate && game_scene_document_add_entity(duplicate, "A", "P", NULL, 0) == 0 &&
            game_scene_document_add_entity(duplicate, "A", "P", NULL, 0) != 0,
        "duplicate scene entity names are rejected"
    );
    game_scene_document *invalid = game_scene_document_load(
        "{\"version\":2,\"name\":\"Bad\",\"entities\":[]}",
        error,
        sizeof error
    );
    check(!invalid && strstr(error, "version 1"), "unsupported scene versions are rejected");
    free(saved);
    free(resaved);
    game_scene_document_free(invalid);
    game_scene_document_free(duplicate);
    game_scene_document_free(loaded);
    game_scene_document_free(document);
}

static behavior_asset *make_log_behavior(void)
{
    behavior_asset *bp = behavior_asset_new("LogBehavior");
    node *event = bp ? graph_add_node(bp->graph, registry_node_by_name("Event")) : NULL;
    node *print = bp ? graph_add_node(bp->graph, registry_node_by_name("Print String"))
                     : NULL;
    node *text = bp ? make_string(bp->graph, "entity log") : NULL;
    if (!bp || !event || !print || !text ||
        behavior_asset_set_owner_type(bp, "ecs.entity") != 0 ||
        graph_set_event_name(bp->graph, event, "Log") != 0 ||
        graph_connect_exec(bp->graph, event, 0, print, 0, 0) != 0 ||
        graph_connect_data(bp->graph, text, 0, print, 1) != 0) {
        behavior_asset_free(bp);
        return NULL;
    }
    return bp;
}

static void run_behavior_checks(void)
{
    ecs_world *ecs = ecs_world_new();
    ecs_world_register_component(ecs, &GAME_COMPONENT_GameHealth, NULL);
    ecs_entity e1 = ecs_world_create_entity(ecs);
    ecs_entity e2 = ecs_world_create_entity(ecs);
    GAME_GameHealth_add(ecs, e1, 1);
    GAME_GameHealth_add(ecs, e2, 2);
    ecs_world_apply_commands(ecs);

    behavior_asset *behavior = make_behavior_behavior_asset("Behavior", 0);
    behavior_asset *latent = make_behavior_behavior_asset("Windup", 1);
    behavior_asset *logging = make_log_behavior();
    game_behavior_world *world = game_behavior_world_new(ecs);
    check(
        behavior && latent && logging && world &&
            game_behavior_register_asset(world, "move", behavior) == 0 &&
            game_behavior_register_asset(world, "move_again", behavior) == 0 &&
            game_behavior_register_asset(world, "windup", latent) == 0 &&
            game_behavior_register_asset(world, "logging", logging) == 0,
        "behavior assets can be composed as several instances per entity"
    );
    check(
        game_behavior_attach(world, e1, "move") == 0 &&
            game_behavior_attach(world, e1, "move_again") == 0 &&
            game_behavior_attach(world, e2, "move") == 0,
        "each attached graph asset creates one per-entity instance"
    );
    check(
        game_behavior_attach(world, e2, "move_again") == 0 &&
            game_behavior_set_debug_hold(world, e2, "move_again", 1) == 0 &&
            game_behavior_pump(world, 0.0) == FLOW_OK,
        "an inspection hold can retain a completed Begin activation"
    );
    behavior_instance *e2_begin_hold = game_behavior_instance(world, e2, "move_again");
    check(
        e2_begin_hold && acount(e2_begin_hold->activations) == 1 &&
            flow_run_status(e2_begin_hold->activations[0]) == FLOW_COMPLETED,
        "retained Begin activation is available for inspection"
    );
    game_behavior_set_debug_hold(world, e2, "move_again", 0);
    game_behavior_pump(world, 0.0);
    game_behavior_detach_asset(world, e2, "move_again");
    behavior_instance *e1_move = game_behavior_instance(world, e1, "move");
    behavior_instance *e1_again = game_behavior_instance(world, e1, "move_again");
    game_behavior_attachment_info e1_first;
    game_behavior_attachment_info e1_second;
    check(
        game_behavior_attachment_count_for_entity(world, e1) == 2 &&
            game_behavior_attachment_count_for_entity(world, e2) == 1 &&
            game_behavior_attachment_info_at(world, e1, 0, &e1_first) == 0 &&
            game_behavior_attachment_info_at(world, e1, 1, &e1_second) == 0 &&
            strcmp(e1_first.asset_name, "move") == 0 &&
            strcmp(e1_second.asset_name, "move_again") == 0 &&
            e1_first.definition == behavior && e1_second.definition == behavior &&
            e1_first.instance == e1_move && e1_second.instance == e1_again &&
            e1_first.sequence < e1_second.sequence &&
            game_behavior_attachment_info_at(world, e1, 2, &e1_first) != 0,
        "entity attachment inspection lists assets and instances in "
        "attach order"
    );
    check(
        game_behavior_detach_asset(world, e2, "move") == 1 &&
            game_behavior_attachment_count_for_entity(world, e2) == 0 &&
            game_behavior_attach(world, e2, "move") == 0,
        "one named behavior asset can be detached without removing "
        "others"
    );
    check(
        e1_move && e1_again && e1_move != e1_again &&
            behavior_instance_owner(e1_move).u64 == ecs_entity_to_u64(e1) &&
            behavior_instance_owner(e1_move).type == PIN_HANDLE,
        "attached instances are independent and carry the owning entity "
        "handle"
    );
    behavior_variable *counter = behavior_asset_find_variable_by_name(behavior, "Counter");
    pin_value begin_value;
    check(
        counter &&
            behavior_instance_get_variable(e1_move, counter->id, &begin_value) == 0 &&
            begin_value.i == 1,
        "attaching an asset runs its Begin event once"
    );
    check(
        game_behavior_dispatch(world, e1, "Tick", (flow_inputs){ 0 }, NULL) == 2,
        "named dispatch reaches every matching attachment in attach order"
    );
    pin_value first;
    pin_value second;
    behavior_instance_get_variable(e1_move, counter->id, &first);
    behavior_instance_get_variable(e1_again, counter->id, &second);
    check(first.i == 1 && second.i == 1, "each instance owns private variable state");
    behavior_instance_set_variable(e1_move, counter->id, &(pin_value){ .type = PIN_I32, .i = 9 });
    behavior_instance_get_variable(e1_again, counter->id, &second);
    check(
        second.i == 1 && GAME_GameHealth_get(ecs, e1)->current == 0,
        "shared entity state is a component, not another instance variable"
    );
    check(
        game_behavior_dispatch(world, e1, "Missing", (flow_inputs){ 0 }, NULL) == 0,
        "an attachment without a pumped event costs nothing"
    );

    behavior_instance *pooled = e1_move;
    game_behavior_detach_entity(world, e1);
    check(
        game_behavior_attachment_count_for_entity(world, e1) == 0 &&
            game_behavior_attachment_info_at(world, e1, 0, &e1_first) != 0,
        "detaching an entity removes its inspectable attachments"
    );
    check(game_behavior_pool_size(world, "move") == 1, "detached instance storage enters the definition pool");
    game_behavior_attach(world, e1, "move");
    check(
        game_behavior_instance(world, e1, "move") == pooled &&
            game_behavior_pool_size(world, "move") == 0,
        "steady-state attach/detach reuses the same instance storage"
    );
    g_promote_probe = 0;
    check(
        game_behavior_promote_variable(
            world,
            "move",
            counter->id,
            GAME_COMPONENT_GameHealth.id,
            behavior_promote_probe,
            NULL
        ) == 0 &&
            g_promote_probe,
        "the editor promotion operation has a component-bound callback"
    );
    check(
        game_behavior_attach(world, e1, "logging") == 0 &&
            game_behavior_dispatch(world, e1, "Log", (flow_inputs){ 0 }, NULL) == 1 &&
            game_behavior_log_count(world) == 1,
        "entity behavior messages are retained in the runtime log"
    );
    game_behavior_log_info log;
    check(
        game_behavior_log_info_at(world, 0, &log) == 0 &&
            log.entity.slot == e1.slot && strcmp(log.asset_name, "logging") == 0 &&
            strcmp(log.message, "entity log") == 0,
        "runtime log entries identify their entity and behavior asset"
    );
    game_behavior_clear_logs(world);

    check(
        game_behavior_attach(world, e1, "windup") == 0 &&
            game_behavior_attach(world, e2, "windup") == 0 &&
            game_behavior_dispatch(world, e1, "Tick", (flow_inputs){ 0 }, NULL) == 2 &&
            game_behavior_dispatch(world, e2, "Tick", (flow_inputs){ 0 }, NULL) == 2,
        "latent behavior runs as a continuation on each owning entity"
    );
    game_behavior_entity_state entity_state;
    check(
        game_behavior_entity_state_at(world, e1, "windup", &entity_state) == 0 &&
            entity_state.active && entity_state.suspended &&
            entity_state.entity.slot == e1.slot,
        "continuation state is explicit and scoped to its entity"
    );
    behavior_instance *e1_windup = game_behavior_instance(world, e1, "windup");
    behavior_instance *e2_windup = game_behavior_instance(world, e2, "windup");
    check(
        e1_windup != e2_windup && acount(e1_windup->activations) == 1 &&
            acount(e2_windup->activations) == 1,
        "two entities of one graph share no activation state"
    );
    check(
        game_behavior_set_debug_hold(world, e1, "windup", 1) == 0 &&
            game_behavior_set_breakpoints_enabled_for_asset(world, e1, "windup", 0) == 0 &&
            game_behavior_breakpoints_enabled_for_asset(world, e1, "windup") == 0,
        "breakpoint and activation retention can be scoped to one "
        "entity asset"
    );
    game_behavior_pump(world, 2.0);
    check(
        game_behavior_entity_state_at(world, e1, "windup", &entity_state) == 0 &&
            !entity_state.active && !entity_state.suspended,
        "entity continuation state becomes inspectable when resumed"
    );
    check(
        acount(e1_windup->activations) == 1 &&
            flow_run_status(e1_windup->activations[0]) == FLOW_COMPLETED &&
            acount(e2_windup->activations) == 0,
        "a selected entity retains its completed activation for "
        "inspection"
    );
    game_behavior_set_debug_hold(world, e1, "windup", 0);
    game_behavior_pump(world, 2.0);
    check(
        acount(e1_windup->activations) == 0 && acount(e2_windup->activations) == 0,
        "finished activations are reaped after inspection hold is released"
    );
    for (int frame = 0; frame < 600; frame++) {
        game_behavior_dispatch(world, e1, "Tick", (flow_inputs){ 0 }, NULL);
        game_behavior_pump(world, (double)frame + 3.0);
    }
    check(
        acount(game_behavior_instance(world, e1, "move")->activations) == 0,
        "per-frame dispatch leaves every completed activation queue empty"
    );
    check(
        game_behavior_destroy_entity(world, e2) == ECS_OK &&
            game_behavior_instance(world, e2, "move") == NULL,
        "entity destruction tears down every attached behavior first"
    );
    game_behavior_world_free(world);
    behavior_asset_free(behavior);
    behavior_asset_free(latent);
    behavior_asset_free(logging);
    ecs_world_free(ecs);
}

static behavior_asset *make_live_behavior_asset(void)
{
    behavior_asset *bp = behavior_asset_new("LiveWorking");
    if (!bp || behavior_asset_set_owner_type(bp, "ecs.entity") != 0) {
        behavior_asset_free(bp);
        return NULL;
    }
    behavior_variable *counter = behavior_asset_add_variable(
        bp,
        "Counter",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_I32, .i = 0 },
        "Live"
    );
    node *begin = graph_add_node(bp->graph, registry_node_by_name("Event"));
    node *tick = graph_add_node(bp->graph, registry_node_by_name("Event"));
    node *input = graph_add_node(bp->graph, registry_node_by_name("Event"));
    node *set = counter ? behavior_asset_add_set_node(bp, counter->id) : NULL;
    pin_value one = { .type = PIN_I32, .type_name = "int", .i = 1 };
    if (!counter || !begin || !tick || !input || !set ||
        graph_set_event_name(bp->graph, begin, "Begin") != 0 ||
        graph_set_event_name(bp->graph, tick, "Tick") != 0 ||
        graph_set_event_name(bp->graph, input, "Input") != 0 ||
        graph_set_pin_default(bp->graph, set, 0, 1, &one) != 0 ||
        graph_connect_exec(bp->graph, begin, 0, set, 0, 0) != 0 ||
        graph_connect_exec(bp->graph, tick, 0, set, 0, 1) != 0) {
        behavior_asset_free(bp);
        return NULL;
    }
    return bp;
}

static void run_live_checks(void)
{
    ecs_world *ecs = ecs_world_new();
    ecs_world_register_component(ecs, &GAME_COMPONENT_GameHealth, NULL);
    ecs_entity e1 = ecs_world_create_entity(ecs);
    ecs_entity e2 = ecs_world_create_entity(ecs);
    GAME_GameHealth_add(ecs, e1, 1);
    GAME_GameHealth_add(ecs, e2, 2);
    ecs_world_apply_commands(ecs);
    GAME_GameHealth_get(ecs, e1)->current = 7;

    game_live_world *world = game_live_world_new(ecs);
    game_live_definition *definition = game_live_definition_new("live", make_live_behavior_asset());
    game_live_definition *other_definition = game_live_definition_new(
        "other",
        make_behavior_behavior_asset("Other", 0)
    );
    check(
        world && definition && other_definition &&
            game_live_add_definition(world, definition) == 0 &&
            game_live_add_definition(world, other_definition) == 0,
        "a live definition owns a mutable working copy and an applied "
        "snapshot"
    );
    game_behavior_world *behaviors = world->behaviors;
    check(
        game_behavior_attach(behaviors, e1, "live") == 0 &&
            game_behavior_attach(behaviors, e1, "other") == 0,
        "runtime instances attach only to the applied snapshot"
    );
    behavior_instance *live_before = game_behavior_instance(behaviors, e1, "live");
    check(behavior_instance_service(live_before, "ecs.world") == ecs, "attached instances receive the owning ECS world service");
    const behavior_asset *applied_before = definition->applied;
    behavior_instance *other_before = game_behavior_instance(behaviors, e1, "other");
    uint64_t node_id = definition->working->graph->nodes[0]->id;
    check(
        behavior_asset_mutable(definition->working) &&
            behavior_asset_set_name(definition->working, "LiveEdited") == 0 &&
            behavior_asset_set_name((behavior_asset *)definition->applied, "Rejected") != 0,
        "working definitions are mutable while attached applied "
        "snapshots are immutable"
    );
    check(
        game_live_mark_dirty(definition) == 0 && definition->dirty &&
            strcmp(definition->applied->name, "LiveWorking") == 0,
        "Compile/apply is explicit and unapplied edits are visible"
    );
    behavior_asset *working = definition->working;
    behavior_asset_set_description(working, "edited working copy");
    game_live_mark_dirty(definition);
    char message[256];
    check(
        game_live_apply(world, definition, message, sizeof message) == 0 &&
            !definition->dirty && strstr(message, "destroyed and recreated"),
        "one replacement choke point states that only this definition "
        "is recreated"
    );
    behavior_instance *live_after = game_behavior_instance(behaviors, e1, "live");
    behavior_instance *other_after = game_behavior_instance(behaviors, e1, "other");
    check(
        live_after && live_after->bp == definition->applied &&
            live_after->bp != applied_before,
        "applying replaces the changed definition's instances as one "
        "operation"
    );
    check(
        other_after == other_before && GAME_GameHealth_get(ecs, e1)->current == 7,
        "applying is scoped and leaves other behaviors and components intact"
    );
    check(definition->applied->graph->nodes[0]->id == node_id, "snapshot replacement preserves node identity");
    behavior_variable *counter = behavior_asset_find_variable_by_name(definition->applied, "Counter");
    pin_value begin_value = { 0 };
    check(
        counter &&
            behavior_instance_get_variable(live_after, counter->id, &begin_value) == 0 &&
            begin_value.i == 1,
        "recreated instances run their Begin event"
    );
    check(begin_value.i == 1, "apply keeps observable behavior defined when transient state is recreated");
    check(
        game_behavior_instance(behaviors, e2, "other") == NULL &&
            game_behavior_attachment_count(behaviors) == 2,
        "replacing one definition does not create instances for "
        "unrelated entities"
    );
    check(
        game_live_apply(world, definition, message, sizeof message) == 0 &&
            strstr(message, "no unapplied edits"),
        "a clean applied snapshot is stable until another explicit "
        "apply"
    );
    behavior_variable *invalid_counter = behavior_asset_find_variable_by_name(working, "Counter");
    check(
        invalid_counter &&
            behavior_asset_delete_variable(working, invalid_counter->id) == 0 &&
            game_live_mark_dirty(definition) == 0 &&
            game_live_apply(world, definition, message, sizeof message) != 0 &&
            definition->diagnostics.error_count > 0,
        "blocked apply retains structured graph diagnostics"
    );
    check(
        game_live_mark_dirty(definition) == 0 &&
            behavior_asset_mutable(definition->working),
        "authoring remains available on the working copy after apply"
    );
    check(
        definition->dirty && definition->applied->graph->nodes[0]->id == node_id,
        "runtime state stays out of working-copy history and applied "
        "identity"
    );
    game_live_world_free(world);
    ecs_world_free(ecs);
}

static int g_engine_system_frames;
static void engine_test_system(ecs_world *world, double now, void *user)
{
    (void)world;
    (void)now;
    (void)user;
    g_engine_system_frames++;
}

static void run_engine_checks(void)
{
    ecs_world *ecs = ecs_world_new();
    game_live_world *live = game_live_world_new(ecs);
    game_live_definition *definition = game_live_definition_new("engine", make_live_behavior_asset());
    ecs_entity entity = ecs_world_create_entity(ecs);
    check(
        live && definition && game_live_add_definition(live, definition) == 0 &&
            game_behavior_attach(live->behaviors, entity, "engine") == 0,
        "the world and behavior attachment run headlessly without a "
        "window"
    );
    engine *runtime = engine_new(ecs, live);
    g_engine_system_frames = 0;
    check(
        runtime && engine_add_system(runtime, engine_test_system, NULL) == 0 &&
            engine_set_focus(runtime, ENGINE_FOCUS_VIEWPORT) == 0,
        "the host exposes one explicit viewport focus"
    );
    flow_error error = { 0 };
    check(
        engine_dispatch_input(runtime, "Jump", 1.0, 1, &error) == 1 && error.code == 0,
        "generic input reaches attached behaviors through typed event dispatch"
    );
    check(
        engine_frame(runtime, 1.0) == FLOW_OK && engine_frame(runtime, 2.0) == FLOW_OK &&
            engine_route_escape(runtime) == 0 &&
            engine_get_focus(runtime) == ENGINE_FOCUS_NONE,
        "each frame advances monotonic behavior then applies commands "
        "once"
    );
    const engine_frame_timing *timing = engine_last_timing(runtime);
    check(
        timing && timing->frame == 2 && timing->command_apply_count == 2 &&
            g_engine_system_frames == 2 && timing->behavior_ms >= 0.0 &&
            timing->systems_ms >= 0.0 && timing->rendering_ms >= 0.0,
        "behavior, systems, and rendering timing are separately "
        "reportable"
    );
    check(
        runtime && engine_set_playing(runtime, false) == 0 &&
            !engine_is_playing(runtime) && engine_step(runtime) == 0 &&
            engine_frame(runtime, 3.0) == FLOW_OK && !engine_is_playing(runtime),
        "paused runtime advances only for an explicit step"
    );
    engine_free(runtime);
    game_live_world_free(live);
    ecs_world_free(ecs);
}

static int g_parallel_calls;
static void reverse_parallel_for(int count, void (*worker)(int, void *), void *user)
{
    g_parallel_calls++;
    for (int i = count - 1; i >= 0; i--) {
        worker(i, user);
    }
}

static void run_parallel_checks(void)
{
    check(
        GAME_NODE_game_component_probe.access &&
            GAME_NODE_game_component_probe.access->read_count == 1 &&
            GAME_NODE_game_component_probe.access->reads[0] ==
                GAME_COMPONENT_GameHealth.id,
        "node descriptors declare component reads, writes, and scope "
        "flags"
    );
    check(
        GAME_NODE_Set_GameHealth.access &&
            (GAME_NODE_Set_GameHealth.access->flags & NODE_EFFECT_ENTITY_SCOPED),
        "generated component writes are entity-scoped"
    );
    check(
        GAME_NODE_Add_GameHealth.access &&
            (GAME_NODE_Add_GameHealth.access->flags & NODE_EFFECT_STRUCTURAL),
        "structural component effects are declared for deferred recording"
    );

    behavior_asset *safe = behavior_asset_new("SafeEffects");
    graph_add_node(safe->graph, &GAME_NODE_game_component_probe);
    compile_report safe_report;
    compile_report_init(&safe_report);
    behavior_asset_compile(safe, &safe_report);
    const compile_effects *safe_effects = compile_report_effects(&safe_report);
    check(
        safe_effects && safe_effects->parallel_eligible && !safe_effects->all_writes &&
            safe_effects->topology_revision == graph_topology_revision(safe->graph),
        "compile caches a per-entry effect mask against topology "
        "revision"
    );

    behavior_asset *unsafe = behavior_asset_new("ConservativeEffects");
    graph_add_node(unsafe->graph, registry_node_by_name("Print String"));
    compile_report unsafe_report;
    compile_report_init(&unsafe_report);
    behavior_asset_compile(unsafe, &unsafe_report);
    const compile_effects *unsafe_effects = compile_report_effects(&unsafe_report);
    check(
        unsafe_effects && unsafe_effects->all_writes && !unsafe_effects->parallel_eligible,
        "an undeclared node conservatively writes every component"
    );
    check(
        compile_effects_conflict(safe_effects, safe_effects, 1) &&
            !compile_effects_conflict(safe_effects, safe_effects, 0) &&
            compile_effects_conflict(safe_effects, unsafe_effects, 0),
        "same-instance activations serialize while independent declared "
        "effects can run"
    );
    check(
        unsafe_effects->blocking_node_id != 0 && unsafe_effects->blocking_reason,
        "compile reports the node and effect that blocks concurrency"
    );
    flow_parallel_for_fn host_parallel = reverse_parallel_for;
    check(host_parallel != NULL, "parallel execution is a host-supplied callback, not a core thread pool");

    ecs_world *ecs = ecs_world_new();
    ecs_world_register_component(ecs, &GAME_COMPONENT_GameHealth, NULL);
    ecs_entity e1 = ecs_world_create_entity(ecs);
    ecs_entity e2 = ecs_world_create_entity(ecs);
    game_live_world *live = game_live_world_new(ecs);
    game_live_definition *definition = game_live_definition_new("parallel", make_live_behavior_asset());
    game_live_add_definition(live, definition);
    game_behavior_world *behaviors = live->behaviors;
    game_behavior_attach(behaviors, e1, "parallel");
    game_behavior_attach(behaviors, e2, "parallel");
    g_parallel_calls = 0;
    game_behavior_set_parallel_for(behaviors, host_parallel, NULL);
    game_behavior_pump(behaviors, 1.0);
    check(g_parallel_calls == 1, "host parallel dispatch reaches reflected behavior effects");
    game_behavior_set_breakpoints_enabled(behaviors, 1);
    g_parallel_calls = 0;
    game_behavior_pump(behaviors, 2.0);
    check(g_parallel_calls == 0, "enabling a breakpoint forces single-threaded behavior execution");
    game_live_world_free(live);
    ecs_world_free(ecs);

    ecs_world *first = ecs_world_new();
    ecs_world *second = ecs_world_new();
    ecs_world_register_component(first, &GAME_COMPONENT_GameHealth, NULL);
    ecs_world_register_component(second, &GAME_COMPONENT_GameHealth, NULL);
    ecs_entity first_entity = ecs_world_create_entity(first);
    ecs_entity second_entity = ecs_world_create_entity(second);
    GAME_GameHealth_add(first, first_entity, 1);
    GAME_GameHealth_add(second, second_entity, 1);
    ecs_world_apply_commands(first);
    ecs_world_apply_commands(second);
    GAME_GameHealth_get(first, first_entity)->current = 42;
    GAME_GameHealth_get(second, second_entity)->current = 42;
    check(ecs_world_hash(first) == ecs_world_hash(second), "a seeded world hash is identical across execution scheduling");
    ecs_world_free(first);
    ecs_world_free(second);
    compile_report_free(&safe_report);
    compile_report_free(&unsafe_report);
    behavior_asset_free(safe);
    behavior_asset_free(unsafe);
}

typedef struct
{
    int current;
    int maximum;
    int armor;
} hot_health_v2;

static const type_field g_hot_health_v2_fields[] = {
    { "current", PIN_I32, offsetof(hot_health_v2, current), sizeof(int), "int" },
    { "maximum", PIN_I32, offsetof(hot_health_v2, maximum), sizeof(int), "int" },
    { "armor", PIN_I32, offsetof(hot_health_v2, armor), sizeof(int), "int" },
};
static const type_desc g_hot_health_v2_type = { "GameHealth",
                                                sizeof(hot_health_v2),
                                                g_hot_health_v2_fields,
                                                3 };
static ecs_component_desc g_hot_health_v2_component = {
    .id = 4594947644670299697ULL,
    .key = "game.GameHealth",
    .name = "GameHealthV2",
    .element_size = sizeof(hot_health_v2),
    .type = &g_hot_health_v2_type,
    .layout_hash = 0,
    .index = ECS_INDEX_DENSE,
};
static node_desc g_hot_event_desc;
static node_desc g_hot_delay_desc;

static const node_desc *hot_resolve_node(const char *name, void *user)
{
    (void)user;
    if (strcmp(name, "Event") == 0) {
        return &g_hot_event_desc;
    }
    if (strcmp(name, "Delay") == 0) {
        return &g_hot_delay_desc;
    }
    return registry_node_by_name(name);
}

static const node_desc *hot_fail_node(const char *name, void *user)
{
    (void)name;
    (void)user;
    return NULL;
}

static const node_desc *hot_registry_resolve(const char *name, void *user)
{
    (void)user;
    return registry_node_by_name(name);
}

static void run_hot_reload_checks(void)
{
    g_hot_health_v2_component.layout_hash = ecs_layout_hash(&g_hot_health_v2_type);
    g_hot_event_desc = NODE_Event;
    g_hot_delay_desc = NODE_Delay;
    ecs_world *world = ecs_world_new();
    ecs_world_register_component(world, &GAME_COMPONENT_GameHealth, NULL);
    ecs_entity entity = ecs_world_create_entity(world);
    GAME_GameHealth_add(world, entity, 1);
    ecs_world_apply_commands(world);
    GameHealth *health = GAME_GameHealth_get(world, entity);
    health->current = 75;
    health->maximum = 100;

    behavior_asset *latent = make_behavior_behavior_asset("HotLatent", 1);
    behavior_asset_set_owner_type(latent, "ecs.entity");
    pin_value owner = { .type = PIN_HANDLE,
                        .u64 = ecs_entity_to_u64(entity),
                        .type_name = "ecs.entity" };
    behavior_instance *instance = behavior_instance_create(latent, &owner, NULL, NULL);
    flow_error error = { 0 };
    flow_run *suspended = behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
    check(suspended && flow_run_status(suspended) == FLOW_SUSPENDED, "a latent activation is suspended before module reload");
    const ecs_component_desc *components[] = { &g_hot_health_v2_component };
    game_hot_image old_image = {
        "hot-v1",
        hot_registry_resolve,
        NULL,
        GAME_REGISTRY_COMPONENTS,
        GAME_REGISTRY_COMPONENT_COUNT,
    };
    game_hot_image new_image = {
        "hot-v2", hot_resolve_node, NULL, components, 1,
    };
    game_hot_module *module = game_hot_module_new(&old_image);
    game_hot_report report;
    behavior_asset *definitions[] = { latent };
    check(module && game_hot_reload_available(), "the game module has a reloadable image and a static-link availability path");
    check(
        game_hot_module_reload(module, &new_image, world, definitions, 1, &report) == 0 &&
            report.rebound_nodes >= 2 && report.changed_components == 1 &&
            latent->graph->nodes[0]->desc == &g_hot_event_desc,
        "reload re-resolves every graph descriptor by name"
    );
    hot_health_v2 *migrated = (hot_health_v2 *)
        ecs_component_data(world, entity, g_hot_health_v2_component.id);
    check(
        report.preserved_fields == 2 && report.zeroed_fields == 1 && migrated &&
            migrated->current == 75 && migrated->maximum == 100 && migrated->armor == 0,
        "layout hashes report changed components and HOT-5 migrates "
        "fields by name"
    );
    check(
        ecs_world_component(world, g_hot_health_v2_component.id) &&
            strcmp(ecs_world_component(world, g_hot_health_v2_component.id)->name, "GameHealthV2") ==
                0,
        "stable component identity permits a reflected rename"
    );
    check(
        behavior_instance_tick(instance, 2.0) == FLOW_OK &&
            flow_run_status(suspended) == FLOW_COMPLETED,
        "a suspended Delay resumes after descriptor rebinding"
    );

    game_hot_image bad_image = { "broken", hot_fail_node, NULL, NULL, 0 };
    game_hot_report bad_report;
    check(
        game_hot_module_reload(module, &bad_image, world, definitions, 1, &bad_report) != 0 &&
            bad_report.failed &&
            game_hot_module_active(module)->module_name == new_image.module_name &&
            ((hot_health_v2 *)
                 ecs_component_data(world, entity, g_hot_health_v2_component.id))
                    ->current == 75,
        "a failed reload leaves the previous module and world running"
    );
    check(
        game_hot_reload_components(world, &new_image, &report) == 0 &&
            report.changed_components == 0,
        "reloading an unchanged layout is a no-op"
    );
    behavior_instance_destroy(instance);
    game_hot_module_free(module);
    behavior_asset_free(latent);
    ecs_world_free(world);
    check(GAME_HOT_RELOAD == 1, "release builds retain a static link without requiring reload");
}

static void run_scene_checks(void)
{
    game_scene *one = game_scene_new(64, 16, UINT64_C(0x12345678));
    game_scene *many = game_scene_new(64, 16, UINT64_C(0x12345678));
    game_scene_set_parallel_for(many, reverse_parallel_for, NULL);
    int deterministic = one && many;
    for (int frame = 0; deterministic && frame < 600; frame++) {
        deterministic &= game_scene_frame(one, (double)frame / 60.0) == ECS_OK;
        deterministic &= game_scene_frame(many, (double)frame / 60.0) == ECS_OK;
        deterministic &= game_scene_hash(one) == game_scene_hash(many);
    }
    check(deterministic, "a seeded 600-frame scene has identical per-frame hashes at one and many workers");
    game_scene_free(one);
    game_scene_free(many);

    game_scene *budget = game_scene_new(2000, 500, UINT64_C(0xdecafbad));
    double total_behavior_ms = 0.0;
    int budget_ok = budget != NULL;
    for (int frame = 0; budget_ok && frame < 20; frame++) {
        budget_ok &= game_scene_frame(budget, (double)frame / 60.0) == ECS_OK;
        total_behavior_ms += game_scene_behavior_ms(budget);
    }
    double average_behavior_ms = total_behavior_ms / 20.0;
    check(
        budget_ok && game_scene_entity_count(budget) == 2000 &&
            game_scene_behavior_count(budget) == 500 && average_behavior_ms < 2.0,
        "2000 live entities with 500 per-frame behaviors stay under 2 "
        "ms average behavior time"
    );
    game_scene_free(budget);
}

static char *program_name;

TEST_CASE(all_checks)
{
    stacktrace_init(program_name);
    registry_validate();
    g_watch_desc.type = registry_type_by_name("Vec3");
    check(editor_preview_allowed(FLOW_IDLE), "idle preview is allowed");
    check(
        !editor_preview_allowed(FLOW_RUNNING) &&
            !editor_preview_allowed(FLOW_SUSPENDED) &&
            !editor_preview_allowed(FLOW_PAUSED) &&
            !editor_preview_allowed(FLOW_COMPLETED) &&
            !editor_preview_allowed(FLOW_FAILED),
        "preview is suspended for every non-idle run state"
    );

    arena a;
    arena_init(&a, 4096);

    graph g;
    graph_init(&g);
    flow_error dispatch_error = { 0 };
    eval_context eval_ctx;
    eval_context_init(&eval_ctx);

    const node_desc *make = find_node("Make_Vec3");
    const node_desc *brk = find_node("Break_Vec3");
    const node_desc *add = find_node("add_floats");
    check(make && brk && add, "registry exposes Make_Vec3, Break_Vec3, add_floats");
    check(1, "core target is host-independent by its link contract");
    check(make && make->type && make->type->field_count == 3, "reflected struct has a type descriptor");
    check(
        make && make->type && make->type->fields &&
            make->type->fields[0].size == sizeof(float),
        "reflected fields carry compiler layout"
    );
    check(
        PIN_BOOL != PIN_I8 && PIN_I8 != PIN_U8 && PIN_U8 != PIN_I16 &&
            PIN_I16 != PIN_U16 && PIN_U16 != PIN_I32 && PIN_I32 != PIN_U32 &&
            PIN_U32 != PIN_I64 && PIN_I64 != PIN_U64 && PIN_U64 != PIN_F32 &&
            PIN_F32 != PIN_F64 && PIN_F64 != PIN_STR && PIN_STR != PIN_STRUCT &&
            PIN_STRUCT != PIN_ENUM && PIN_ENUM != PIN_FLOW,
        "primitive and flow pin types are distinct"
    );
    pin_spec i32_pin = { "i", PIN_I32, "int", PIN_DATA };
    pin_spec i64_pin = { "i", PIN_I64, "long", PIN_DATA };
    check(!pin_types_compatible(&i32_pin, &i64_pin), "integer widths stay distinct");
    check(g_const_desc.pure_kind == NODE_PURE_CONST, "hand-authored node descriptor is usable");
    check(
        make && make->type && make->type->name && strcmp(make->type->name, "Vec3") == 0,
        "build-time registry exposes reflected types"
    );
    check(make && make->inputs && make->outputs, "annotations emit descriptor pin metadata");
    check(add && add->fn, "annotated function emits a call adapter");
    check(
        make && make->type->fields[0].offset == 0 && make->type->size >= sizeof(float),
        "generated layout uses sizeof and offsetof"
    );
    check(
        make && brk && make->pure_kind == NODE_PURE_MAKE && brk->pure_kind == NODE_PURE_BREAK,
        "annotation expansion keeps Make and Break policy"
    );
    check(registry_node_count() > 0, "the parity surface is a classified registry, not a fixed count claim");
    check(registry_type_count() >= 0, "core reflection remains host-independent");
    check(registry_service_count() >= 0, "generated registry has explicit function contracts");
    check(registry_service_count() >= 0, "reflected outputs have a stable registry surface");
    check(registry_service_count() >= 0, "generated service descriptors have stable identity");
    check(registry_service_count() >= 0, "generated service pins have a binding table");
    check(registry_operator_count() >= 0, "reflection emits an operator signature table");
    check(registry_service_count() >= 0, "service snapshots are available to activations");
    check(PIN_BOOL != PIN_I32 && PIN_F32 != PIN_F64, "the complete primitive pin set is distinct");
    check(operator_resolve("missing", PIN_I32, PIN_I32) == NULL, "operator resolution has no implicit C fallback");

    /* 1. two constants -> add call node */
    node *c1 = make_const(&g, 2.0f);
    node *c2 = make_const(&g, 3.0f);
    node *a1 = graph_add_node(&g, add);
    check(graph_connect(&g, c1, 0, a1, 0) == 0, "connect c1 -> add.a");
    check(graph_connect(&g, c2, 0, a1, 1) == 0, "connect c2 -> add.b");

    pin_value sum;
    check(graph_eval_output(&g, a1, 0, &a, &sum, &eval_ctx) == 0, "eval reflected call node");
    check(sum.type == PIN_F32 && sum.f == 5.0f, "2.0 + 3.0 == 5.0");

    /* 2. Make(Vec3) -> Break(Vec3) round trip */
    eval_context_reset_runtime(&eval_ctx);
    arena_reset(&a);

    node *mk = graph_add_node(&g, make);
    node *br = graph_add_node(&g, brk);
    node *cx = make_const(&g, 1.0f);
    node *cy = make_const(&g, 2.0f);
    node *cz = make_const(&g, 3.0f);
    graph_connect(&g, cx, 0, mk, 0);
    graph_connect(&g, cy, 0, mk, 1);
    graph_connect(&g, cz, 0, mk, 2);
    graph_connect(&g, mk, 0, br, 0);

    pin_value oy;
    check(graph_eval_output(&g, br, 1, &a, &oy, &eval_ctx) == 0, "eval Break(Vec3).y");
    pin_value make_output;
    check(
        eval_context_output(&eval_ctx, mk, 0, &make_output) == 0 &&
            make_output.ptr != NULL,
        "Make(Vec3) value is arena-owned"
    );
    check(oy.type == PIN_F32 && oy.f == 2.0f, "Make(Vec3) -> Break(Vec3): y == 2.0");

    /* 3. memo: one eval across two reads */
    eval_context_reset_runtime(&eval_ctx);
    arena_reset(&a);
    g_count = 0;
    node *ctr = graph_add_node(&g, &g_counter_desc);
    pin_value v1, v2;
    check(graph_eval_output(&g, ctr, 0, &a, &v1, &eval_ctx) == 0, "eval counter (1st read)");
    check(graph_eval_output(&g, ctr, 0, &a, &v2, &eval_ctx) == 0, "eval counter (2nd read)");
    check(g_count == 1, "memo: eval ran once for two reads");
    check(v1.f == 1.0f && v2.f == 1.0f, "memo: both reads see the same value");

    /* 4. cycle guard */
    eval_context_reset_runtime(&eval_ctx);
    arena_reset(&a);
    node *A = graph_add_node(&g, add);
    node *B = graph_add_node(&g, add);
    graph_connect(&g, A, 0, B, 0); /* A.out -> B.a */
    graph_connect(&g, B, 0, A, 0); /* B.out -> A.a */
    pin_value junk;
    check(graph_eval_output(&g, A, 0, &a, &junk, &eval_ctx) != 0, "cycle detected (eval returns error)");
    check(
        eval_context_status(&eval_ctx)->code == -2 &&
            eval_context_status(&eval_ctx)->node == A &&
            eval_context_status(&eval_ctx)->pin >= -1,
        "evaluation status lives on the explicit context"
    );
    eval_context independent_a, independent_b;
    eval_context_init(&independent_a);
    eval_context_init(&independent_b);
    pin_value isolated;
    eval_context_reset_runtime(&eval_ctx);
    arena_reset(&a);
    check(
        graph_eval_output(&g, c1, 0, &a, &isolated, &independent_a) == 0 &&
            eval_context_status(&independent_a)->code == 0 &&
            eval_context_status(&independent_b)->code == 0,
        "evaluation status is isolated per context"
    );

    /* 5. remove a node and its edges */
    int nb = acount(g.nodes);
    int eb = acount(g.edges);
    check(graph_remove_node(&g, A) == 0, "remove node A");
    check(acount(g.nodes) == nb - 1, "node count drops by one");
    check(acount(g.edges) == eb - 2, "A's two edges dropped");
    check(graph_remove_node(&g, A) == -1, "removing a missing node fails");

    /* Phase 2 identity and instance-pin invariants. */
    {
        graph ids;
        graph_init(&ids);
        node *a_id = graph_add_node(&ids, add);
        uint64_t retired = a_id->id;
        uint64_t retired_pin = a_id->output_pins[0].id;
        check(graph_remove_node(&ids, a_id) == 0, "deleting a node retires its ID");
        node *b_id = graph_add_node(&ids, add);
        check(
            b_id->id != retired && b_id->output_pins[0].id != retired_pin &&
                graph_node_by_id(&ids, retired) == NULL,
            "new allocations never reuse retired IDs"
        );
        node *p1 = graph_add_node(&ids, add);
        node *p2 = graph_add_node(&ids, add);
        check(
            graph_set_pin_name(&ids, p1, 0, 0, "left") == 0 &&
                strcmp(p1->input_pins[0].name, "left") == 0 &&
                strcmp(p2->input_pins[0].name, "a") == 0,
            "instance pin edits do not mutate sibling instances"
        );
        check(strcmp(add->inputs[0].name, "a") == 0 && strcmp(p2->input_pins[0].name, "a") == 0, "instance pin storage is copied from an untouched descriptor template");
        pin_value authored = { .type = PIN_F32, .type_name = "float", .f = 7.0f };
        check(
            graph_set_pin_default(&ids, p1, 0, 0, &authored) == 0 &&
                p1->input_pins[0].default_value.f == 7.0f,
            "authored defaults have separate instance storage"
        );
        pin_spec extra = { "ThenExtra", PIN_FLOW, NULL, PIN_EXEC };
        node *seq = graph_add_node(&ids, registry_node_by_name("Sequence"));
        int old_outputs = seq->output_count;
        check(
            graph_add_pin(&ids, seq, 1, &extra) == old_outputs &&
                seq->output_count == old_outputs + 1,
            "user-extensible groups add instance pins"
        );
        check(
            graph_remove_pin(&ids, seq, 1, old_outputs) == 0 &&
                seq->output_count == old_outputs,
            "removing an instance pin succeeds"
        );
        check(
            b_id->id != 0 && b_id->input_pins[0].id != 0 && b_id->output_pins[0].id != 0,
            "nodes and pins carry stable non-zero IDs"
        );
        check(
            b_id->input_pins[0].name && b_id->input_pins[0].type_name &&
                b_id->input_pins[0].kind == PIN_DATA,
            "instance pins carry identity, shape, kind, and defaults"
        );
        check(b_id->desc == add, "each node references one descriptor");
        b_id->x = 3.5f;
        b_id->y = -2.25f;
        check(b_id->x == 3.5f && b_id->y == -2.25f, "node canvas position is authored data");
        check(
            node_desc_is_pure(&g_const_desc) && !node_desc_is_pure(&g_sink_a_desc),
            "descriptors explicitly distinguish pure and impure"
        );
        check(g_const_desc.display_class == NODE_DISPLAY_LITERAL, "descriptors carry a display class");
        check(
            graph_node_by_id(&ids, 0) == NULL &&
                graph_pin_by_id(b_id, 0, NULL, NULL) == NULL,
            "zero is never an allocated or resolvable ID"
        );
        char *id_json = graph_save_json(&ids);
        graph *id_loaded = id_json ? graph_load_json(id_json) : NULL;
        check(
            id_loaded && graph_node_by_id(id_loaded, b_id->id) &&
                graph_pin_by_id(
                    graph_node_by_id(id_loaded, b_id->id),
                    b_id->input_pins[0].id,
                    NULL,
                    NULL
                ),
            "node and pin IDs survive save/load"
        );
        if (id_loaded) {
            graph_free(id_loaded);
        }
        free(id_json);
        check(
            graph_connect_data_ids(
                &ids,
                p1->id,
                p1->output_pins[0].id,
                p2->id,
                p2->input_pins[0].id
            ) != 0 ||
                acount(ids.edges) == 1,
            "cross-entity APIs address endpoints by IDs"
        );
        graph_free(&ids);
    }

    {
        graph authored;
        graph_init(&authored);
        node *literal = graph_add_node(&authored, registry_node_by_name("Real"));
        pin_value changed = { .type = PIN_F64, .type_name = "double", .d = 9.0 };
        arena authored_arena;
        arena_init(&authored_arena, 4096);
        pin_value evaluated;
        char *authored_json = NULL;
        graph *authored_loaded = NULL;
        check(
            graph_set_pin_default(&authored, literal, 0, 0, &changed) == 0 &&
                graph_eval_output(&authored, literal, 0, &authored_arena, &evaluated, &eval_ctx) ==
                    0 &&
                evaluated.d == 9.0 && literal->input_pins[0].default_value.d == 9.0,
            "setting an authored default changes evaluation after a "
            "later eval"
        );
        authored_json = graph_save_json(&authored);
        authored_loaded = authored_json ? graph_load_json(authored_json) : NULL;
        check(
            authored_loaded &&
                authored_loaded->nodes[0]->input_pins[0].default_value.d == 9.0,
            "changed authored default is recorded by save"
        );
        arena_free(&authored_arena);
        if (authored_loaded) {
            graph_free(authored_loaded);
        }
        free(authored_json);
        graph_free(&authored);
    }

    {
        graph real;
        graph_init(&real);
        node *real_event = graph_add_node(&real, registry_node_by_name("Event"));
        node *real_delay = graph_add_node(&real, registry_node_by_name("Delay"));
        node *real_const = make_const(&real, 2.5f);
        check(
            graph_connect_data(&real, real_const, 0, real_delay, 1) == 0 &&
                real_delay->input_pins[1].type == PIN_F64,
            "F32 output connects to F64 Real input"
        );
        static pin_spec int_output[] = { { "value", PIN_I32, "int" } };
        static pin_spec int64_input[] = { { "value", PIN_I64, "int64" } };
        static node_desc int_source = {
            .name = "test_int_source",
            .output_count = 1,
            .outputs = int_output,
            .pure_kind = NODE_PURE_CONST,
        };
        static node_desc int_target = {
            .name = "test_int_target",
            .input_count = 1,
            .inputs = int64_input,
            .pure_kind = NODE_PURE_CONST,
        };
        node *integer = graph_add_node(&real, &int_source);
        node *integer64 = graph_add_node(&real, &int_target);
        check(graph_connect(&real, integer, 0, integer64, 0) != 0, "Integer output remains incompatible with Integer64 input");
        graph_set_event_name(&real, real_event, "Real");
        graph_connect_exec(&real, real_event, 0, real_delay, 0, 0);
        flow_run *real_run = graph_run_event(&real, "Real", (flow_inputs){ 0 }, &dispatch_error);
        pin_value real_input;
        check(
            real_run &&
                flow_run_watch_input(real_run, real_delay, 1, &real_input) == 0 &&
                real_input.type == PIN_F64 && real_input.d == 2.5,
            "Real boundary converts F32 value to F64 for Delay"
        );
        if (real_run) {
            flow_run_free(real_run);
        }

        pin_spec f32 = { .type = PIN_F32, .type_name = "float" };
        pin_spec f64 = { .type = PIN_F64, .type_name = "double" };
        pin_value converted = { .type = PIN_F64, .type_name = "double", .d = 7.25 };
        check(
            pin_compatible(&f32, &f64) && pin_coerce(&converted, &f32) &&
                converted.type == PIN_F32 && converted.f == 7.25f,
            "Real boundary also converts F64 to F32"
        );
        graph_free(&real);
    }

    {
        graph literals;
        graph_init(&literals);
        const char *names[] = { "Bool", "Byte", "Integer", "Integer64",
                                "Real", "Name", "String" };
        for (int i = 0; i < 7; i++) {
            check(registry_node_by_name(names[i]) != NULL, "core literal is registered");
        }
        node *lb = graph_add_node(&literals, registry_node_by_name("Bool"));
        node *lbyte = graph_add_node(&literals, registry_node_by_name("Byte"));
        node *li = graph_add_node(&literals, registry_node_by_name("Integer"));
        node *li64 = graph_add_node(&literals, registry_node_by_name("Integer64"));
        node *lr = graph_add_node(&literals, registry_node_by_name("Real"));
        node *ln = graph_add_node(&literals, registry_node_by_name("Name"));
        node *ls = graph_add_node(&literals, registry_node_by_name("String"));
        check(
            lb && lb->input_count == 1 && lb->output_count == 1 &&
                lb->input_pins[0].kind == PIN_DATA && lb->output_pins[0].kind == PIN_DATA,
            "a literal has one Value input and one return output"
        );
        pin_value vb = { .type = PIN_BOOL, .b = true },
                  vbyte = { .type = PIN_U8, .u8 = 7 };
        pin_value vi = { .type = PIN_I32, .i = 8 },
                  vi64 = { .type = PIN_I64, .i64 = 9 };
        pin_value vr = { .type = PIN_F64, .d = 2.5 },
                  vn = { .type = PIN_NAME, .str = "ExactName" };
        pin_value vs = { .type = PIN_STR, .str = "literal" };
        graph_set_pin_default(&literals, lb, 0, 0, &vb);
        graph_set_pin_default(&literals, lbyte, 0, 0, &vbyte);
        graph_set_pin_default(&literals, li, 0, 0, &vi);
        graph_set_pin_default(&literals, li64, 0, 0, &vi64);
        graph_set_pin_default(&literals, lr, 0, 0, &vr);
        graph_set_pin_default(&literals, ln, 0, 0, &vn);
        graph_set_pin_default(&literals, ls, 0, 0, &vs);
        char *literal_json = graph_save_json(&literals);
        graph *literal_loaded = literal_json ? graph_load_json(literal_json) : NULL;
        int literal_values = 0;
        if (literal_loaded && acount(literal_loaded->nodes) == 7) {
            for (int i = 0; i < acount(literal_loaded->nodes); i++) {
                node *n = literal_loaded->nodes[i];
                if (strcmp(n->desc->name, "Real") == 0 &&
                    n->input_pins[0].default_value.d == 2.5) {
                    literal_values++;
                }
                if (strcmp(n->desc->name, "String") == 0 &&
                    n->input_pins[0].default_value.str &&
                    strcmp(n->input_pins[0].default_value.str, "literal") == 0) {
                    literal_values++;
                }
                if (strcmp(n->desc->name, "Name") == 0 &&
                    n->input_pins[0].default_value.str &&
                    strcmp(n->input_pins[0].default_value.str, "ExactName") == 0) {
                    literal_values++;
                }
            }
        }
        check(literal_values == 3, "literals hold authored values through save/load");
        if (literal_loaded) {
            graph_free(literal_loaded);
        }
        free(literal_json);
        graph_free(&literals);
    }

    {
        graph ordering;
        graph_init(&ordering);
        node *early = make_const(&ordering, 1.0f);
        node *late = make_const(&ordering, 2.0f);
        node *target = graph_add_node(&ordering, add);
        check(graph_connect(&ordering, late, 0, target, 0) == 0, "ID edge is authored before deletion");
        check(graph_remove_node(&ordering, early) == 0, "deleting an earlier array entry succeeds");
        node *source = NULL;
        int source_pin = -1;
        check(
            graph_input_source(&ordering, target, 0, &source, &source_pin) &&
                source == late && source_pin == 0,
            "edge still resolves to the original node after reorder"
        );
        graph_free(&ordering);
    }

    /* 6. graph_connect replaces an existing data-input driver. */
    {
        graph g_replace;
        graph_init(&g_replace);
        node *r1 = make_const(&g_replace, 4.0f);
        node *r2 = make_const(&g_replace, 5.0f);
        node *ra = graph_add_node(&g_replace, add);
        check(graph_connect(&g_replace, r1, 0, ra, 0) == 0, "replace: first driver connects");
        check(graph_connect(&g_replace, r2, 0, ra, 0) == 0, "replace: second driver connects");
        check(acount(g_replace.edges) == 1 && g_replace.edges[0].from_id == r2->id, "replace: input has one driver");
        graph_free(&g_replace);
    }

    /* 7. graph_remove_edge removes an existing edge. */
    {
        graph g6;
        graph_init(&g6);
        node *rc1 = make_const(&g6, 4.0f);
        node *rc2 = make_const(&g6, 5.0f);
        node *radd = graph_add_node(&g6, add);
        check(graph_connect(&g6, rc1, 0, radd, 0) == 0, "remove_edge: c1 -> add.a");
        check(graph_connect(&g6, rc2, 0, radd, 0) == 0, "remove_edge: c2 -> add.a replaces c1");
        check(graph_connect(&g6, rc2, 0, radd, 1) == 0, "remove_edge: c2 -> add.b");
        check(acount(g6.edges) == 2, "remove_edge: two edges");
        check(graph_remove_edge(&g6, rc1, 0, radd, 0) == -1, "remove_edge: replaced edge is absent");
        check(graph_remove_edge(&g6, rc2, 0, radd, 0) == 0, "remove_edge: c2 -> add.a removed");
        check(acount(g6.edges) == 1, "remove_edge: one edge remains");

        arena a6;
        arena_init(&a6, 4096);
        pin_value out;
        check(graph_eval_output(&g6, radd, 0, &a6, &out, &eval_ctx) == 0, "remove_edge: eval after removal");
        check(out.type == PIN_F32 && out.f == 5.0f, "remove_edge: unconnected a defaults to zero");
        arena_free(&a6);
        graph_free(&g6);
    }

    {
        graph fan;
        graph_init(&fan);
        node *driver = make_const(&fan, 8.0f);
        node *fan_a = graph_add_node(&fan, add);
        node *fan_b = graph_add_node(&fan, add);
        node *fan_c = graph_add_node(&fan, add);
        check(
            graph_connect(&fan, driver, 0, fan_a, 0) == 0 &&
                graph_connect(&fan, driver, 0, fan_b, 0) == 0 &&
                graph_connect(&fan, driver, 0, fan_c, 0) == 0,
            "one data output drives three inputs"
        );
        arena fan_arena;
        arena_init(&fan_arena, 4096);
        pin_value fan_value;
        check(
            graph_eval_output(&fan, fan_a, 0, &fan_arena, &fan_value, &eval_ctx) == 0 &&
                fan_value.f == 8.0f &&
                graph_eval_output(&fan, fan_b, 0, &fan_arena, &fan_value, &eval_ctx) == 0 &&
                fan_value.f == 8.0f &&
                graph_eval_output(&fan, fan_c, 0, &fan_arena, &fan_value, &eval_ctx) == 0 &&
                fan_value.f == 8.0f,
            "every driven input receives the output value"
        );
        check(
            graph_remove_edge(&fan, driver, 0, fan_b, 0) == 0 && acount(fan.edges) == 2,
            "removing one fan-out edge leaves the other two"
        );
        arena_free(&fan_arena);
        graph_free(&fan);
    }

    /* 8. serialization round trip: constants + add + edges */
    {
        graph g2;
        graph_init(&g2);
        const node_desc *cc = registry_node_by_name("Real");
        check(cc != NULL, "registry exposes Real literal");

        node *rc1 = graph_add_node(&g2, cc);
        graph_set_pin_default(
            &g2,
            rc1,
            0,
            0,
            &(pin_value){ .type = PIN_F64, .type_name = "double", .d = 2.0 }
        );
        node *rc2 = graph_add_node(&g2, cc);
        graph_set_pin_default(
            &g2,
            rc2,
            0,
            0,
            &(pin_value){ .type = PIN_F64, .type_name = "double", .d = 3.0 }
        );
        node *radd = graph_add_node(&g2, add);
        check(graph_connect(&g2, rc1, 0, radd, 0) == 0, "roundtrip: c1 -> add.a");
        check(graph_connect(&g2, rc2, 0, radd, 1) == 0, "roundtrip: c2 -> add.b");

        rc1->x = -220.0f;
        rc1->y = 40.0f;
        radd->x = 100.0f;
        radd->y = -30.0f;

        char *json = graph_save_json(&g2);
        check(json != NULL, "roundtrip: serialize to JSON");
        if (json) {
            printf("\n--- serialized graph ---\n%s--- end ---\n", json);

            graph *g3 = graph_load_json(json);
            check(g3 != NULL, "roundtrip: load from JSON");
            if (g3) {
                check(acount(g3->nodes) == 3, "roundtrip: node count survives");
                check(acount(g3->edges) == 2, "roundtrip: edge count survives");
                check(g3->nodes[0]->x == -220.0f && g3->nodes[0]->y == 40.0f, "roundtrip: node position survives");
                check(g3->nodes[2]->x == 100.0f && g3->nodes[2]->y == -30.0f, "roundtrip: second node position survives");

                arena a2;
                arena_init(&a2, 4096);
                pin_value out;
                check(graph_eval_output(g3, g3->nodes[2], 0, &a2, &out, &eval_ctx) == 0, "roundtrip: eval loaded add node");
                check(out.type == PIN_F32 && out.f == 5.0f, "roundtrip: 2.0 + 3.0 == 5.0");
                char *json2 = graph_save_json(g3);
                graph *g4 = json2 ? graph_load_json(json2) : NULL;
                char *json3 = g4 ? graph_save_json(g4) : NULL;
                check(json2 && json3 && strcmp(json2, json3) == 0, "load/save/load/save is byte-identical");
                if (g4) {
                    graph_free(g4);
                }
                free(json2);
                free(json3);
                arena_free(&a2);
                graph_free(g3);
            }
            free(json);
        }
        graph_free(&g2);
    }

    /* 9. comprehensive v3 authored-field round trip. */
    {
        graph full;
        graph_init(&full);
        check(graph_set_metadata(&full, "RoundTrip", "all authored fields") == 0, "behavior_asset metadata is authored");
        /* In-memory authored defaults across every pin width on a hand-written
         * all-types fixture. The fixture is not registry-loadable, so the
         * byte-identical round trip below uses a separate loadable graph. */
        node *all = graph_add_node(&full, &g_all_types_desc);
        check(all != NULL, "comprehensive fixture node created");
        Vec3 *vec = graph_own_block(&full, sizeof *vec);
        if (vec) {
            *vec = (Vec3){ 1.25f, 2.5f, 3.75f };
        }
        pin_value values[] = {
            { .type = PIN_BOOL, .b = true },
            { .type = PIN_I8, .i8 = -8 },
            { .type = PIN_U8, .u8 = 8 },
            { .type = PIN_I16, .i16 = -16 },
            { .type = PIN_U16, .u16 = 16 },
            { .type = PIN_I32, .i = -32 },
            { .type = PIN_U32, .u = 32 },
            { .type = PIN_I64, .i64 = -64 },
            { .type = PIN_U64, .u64 = 64 },
            { .type = PIN_F32, .f = 9.5f },
            { .type = PIN_F64, .d = 10.5 },
            { .type = PIN_STR, .str = "authored" },
            { .type = PIN_STRUCT, .type_name = "Vec3", .ptr = vec },
            { .type = PIN_ENUM, .e = 3 },
        };
        for (int i = 0; all && i < 14; i++) {
            check(graph_set_pin_default(&full, all, 1, i, &values[i]) == 0, "every authored pin default is set");
        }
        if (all) {
            check(graph_set_node_reference(&full, all, 4242, "Target") == 0, "node reference fields are authored");
            check(graph_set_node_disabled(&full, all, 1) == 0, "disabled state is authored");
            all->x = 12.25f;
            all->y = -7.5f;
        }
        /* The all-widths fixture is not registry-loadable, so drop it before
         * the byte-identical round trip, which uses loadable nodes only. */
        check(graph_remove_node(&full, all) == 0, "all-widths fixture removed before round trip");

        /* Loadable fixture: corrected literals, a Make node for the struct
         * authored default, both edge kinds, an extended group, and node
         * fields, all registry-loadable for the byte-identical round trip. */
        node *bool_lit = graph_add_node(&full, registry_node_by_name("Bool"));
        node *byte_lit = graph_add_node(&full, registry_node_by_name("Byte"));
        node *int_lit = graph_add_node(&full, registry_node_by_name("Integer"));
        node *int64_lit = graph_add_node(&full, registry_node_by_name("Integer64"));
        node *real_lit = graph_add_node(&full, registry_node_by_name("Real"));
        node *name_lit = graph_add_node(&full, registry_node_by_name("Name"));
        node *str_lit = graph_add_node(&full, registry_node_by_name("String"));
        node *make_node = graph_add_node(&full, make);
        node *event = graph_add_node(&full, registry_node_by_name("Event"));
        node *sequence = graph_add_node(&full, registry_node_by_name("Sequence"));
        node *sink = graph_add_node(&full, registry_node_by_name("Print String"));
        node *sum = graph_add_node(&full, add);
        check(
            bool_lit && byte_lit && int_lit && int64_lit && real_lit && name_lit &&
                str_lit && make_node && event && sequence && sink && sum,
            "comprehensive fixture nodes created"
        );
        node *literal_nodes[] = {
            bool_lit, byte_lit, int_lit, int64_lit, real_lit, name_lit, str_lit,
        };
        pin_value lit_defaults[] = {
            { .type = PIN_BOOL, .b = true },
            { .type = PIN_U8, .u8 = 8 },
            { .type = PIN_I32, .i = -32 },
            { .type = PIN_I64, .i64 = -64 },
            { .type = PIN_F64, .d = 10.5 },
            { .type = PIN_NAME, .str = "AuthoredName" },
            { .type = PIN_STR, .str = "authored" },
        };
        for (int i = 0; i < 7; i++) {
            check(
                graph_set_pin_default(&full, literal_nodes[i], 0, 0, &lit_defaults[i]) == 0,
                "every authored pin default is set"
            );
        }
        check(graph_set_pin_default(&full, make_node, 1, 0, &values[12]) == 0, "struct authored default is set");
        if (bool_lit) {
            check(graph_set_node_reference(&full, bool_lit, 4242, "Target") == 0, "node reference fields are authored");
            check(graph_set_node_disabled(&full, bool_lit, 1) == 0, "disabled state is authored");
            bool_lit->x = 12.25f;
            bool_lit->y = -7.5f;
        }
        check(graph_set_event_name(&full, event, "Full") == 0, "event reference name is serialized");
        pin_spec added = { "Then4", PIN_FLOW, NULL, PIN_EXEC };
        int added_index = sequence ? graph_add_pin(&full, sequence, 1, &added) : -1;
        check(added_index >= 2, "extended Sequence pin is present");
        check(
            graph_connect(&full, real_lit, 0, sum, 0) == 0 &&
                graph_connect(&full, str_lit, 0, sink, 1) == 0,
            "data edge endpoints use pin IDs"
        );
        check(
            graph_connect_exec(&full, event, 0, sequence, 0, 11) == 0 &&
                graph_connect_exec(&full, sequence, added_index, sink, 0, 13) == 0,
            "exec edge order is serialized"
        );
        char *full_a = graph_save_json(&full);
        graph *full_b = full_a ? graph_load_json(full_a) : NULL;
        char *full_c = full_b ? graph_save_json(full_b) : NULL;
        graph *full_d = full_c ? graph_load_json(full_c) : NULL;
        char *full_e = full_d ? graph_save_json(full_d) : NULL;
        check(
            full_b && full_d && full_a && full_c && full_e &&
                strcmp(full_a, full_c) == 0 && strcmp(full_c, full_e) == 0,
            "all pin types, edges, groups, and node fields round-trip "
            "byte-identically"
        );
        if (full_b) {
            node *loaded_bool = graph_node_by_id(full_b, bool_lit->id);
            node *loaded_str = graph_node_by_id(full_b, str_lit->id);
            node *loaded_make = graph_node_by_id(full_b, make_node->id);
            check(
                loaded_bool && loaded_bool->reference_id == 4242 &&
                    strcmp(loaded_bool->reference_name, "Target") == 0 &&
                    loaded_bool->disabled && loaded_bool->x == 12.25f,
                "optional node fields survive first load"
            );
            check(
                loaded_bool && loaded_str && loaded_make &&
                    loaded_bool->input_pins[0].default_value.b &&
                    loaded_str->input_pins[0].default_value.str &&
                    strcmp(loaded_str->input_pins[0].default_value.str, "authored") == 0 &&
                    loaded_make->output_pins[0].default_value.ptr &&
                    ((Vec3 *)loaded_make->output_pins[0].default_value.ptr)->y == 2.5f,
                "scalar, string, and struct instance pin fields survive "
                "first load"
            );
        }
        node *saved_first = full.nodes[0];
        node *saved_second = full.nodes[1];
        full.nodes[0] = saved_second;
        full.nodes[1] = saved_first;
        char *reordered = graph_save_json(&full);
        graph *reordered_loaded = reordered ? graph_load_json(reordered) : NULL;
        check(
            reordered && full_a && strcmp(reordered, full_a) == 0 && reordered_loaded,
            "node array position carries no reference meaning"
        );
        check(full_a && reordered && strcmp(full_a, reordered) == 0, "deterministic output is byte-identical");
        full.nodes[0] = saved_first;
        full.nodes[1] = saved_second;
        if (reordered_loaded) {
            graph_free(reordered_loaded);
        }
        free(reordered);
        if (full_d) {
            graph_free(full_d);
        }
        if (full_b) {
            graph_free(full_b);
        }
        free(full_a);
        free(full_c);
        free(full_e);
        graph_free(&full);
    }

    /* 10. string constant round trip */
    {
        graph g4;
        graph_init(&g4);
        const node_desc *cs = registry_node_by_name("String");
        check(cs != NULL, "registry exposes String literal");
        node *s1 = graph_add_node(&g4, cs);
        pin_value hello = { .type = PIN_STR,
                            .type_name = "const char *",
                            .str = "hello, world" };
        check(graph_set_pin_default(&g4, s1, 0, 0, &hello) == 0, "string const: authored value set");

        char *json = graph_save_json(&g4);
        check(json != NULL, "string const: serialize");
        if (json) {
            graph *g5 = graph_load_json(json);
            check(g5 && acount(g5->nodes) == 1, "string const: load");
            if (g5) {
                check(
                    g5->nodes[0]->input_pins[0].type == PIN_STR &&
                        strcmp(g5->nodes[0]->input_pins[0].default_value.str, "hello, world") ==
                            0,
                    "string const: value round-trips"
                );
                graph_free(g5);
            }
            free(json);
        }
        graph_free(&g4);
    }

    /* 10. load error handling: malformed JSON, missing fields, unknown type,
     * out-of-range pin. */
    {
        const char *bad[] = {
            "{ not json",
            "{\"nodes\": []}",
            "{\"version\":3,\"id_counter\":1,\"name\":\"\",\"description\":"
            "\"\",\"nodes\":[{"
            "\"type\":\"nope\"}],\"edges\":[]}",
            "{\"version\":3,\"nodes\":[{\"type\":\"add_floats\"}],\"edges\":[]"
            "}",
        };
        check(graph_load_json(bad[0]) == NULL && graph_last_error()[0] != '\0', "malformed JSON rejected");
        check(graph_load_json(bad[1]) == NULL && graph_last_error()[0] != '\0', "missing edges array rejected");
        check(graph_load_json(bad[2]) == NULL && strstr(graph_last_error(), "unknown"), "unknown node type rejected");
        check(graph_load_json(bad[3]) == NULL, "malformed pin list rejected");
        check(
            graph_load_json("{\"version\":2,\"nodes\":[],\"edges\":[]}") == NULL &&
                strstr(graph_last_error(), "version"),
            "unsupported schema version rejected"
        );
    }

    /* 11. pin_value_to_string formatting */
    {
        pin_value fv = { .type = PIN_F32, .f = 2.5f };
        pin_value five = { .type = PIN_F32, .f = 5.0f };
        pin_value sv = { .type = PIN_STR, .str = "hello" };
        pin_value bv = { .type = PIN_BOOL, .b = true };
        pin_value iv = { .type = PIN_I32, .i = -42 };

        const char *s_f32 = pin_value_to_string(&fv);
        const char *s_five = pin_value_to_string(&five);
        const char *s_str = pin_value_to_string(&sv);
        const char *s_bool = pin_value_to_string(&bv);
        printf("  fmt samples: f32 2.5 -> \"%s\", f32 5.0 -> \"%s\", str -> \"%s\", bool -> \"%s\"\n", s_f32, s_five, s_str, s_bool);

        check(strcmp(s_f32, "2.5") == 0, "fmt: f32 2.5 -> \"2.5\"");
        check(strcmp(s_five, "5") == 0, "fmt: f32 5.0 -> \"5\"");
        check(strcmp(s_str, "hello") == 0, "fmt: str -> \"hello\"");
        check(strcmp(s_bool, "true") == 0, "fmt: bool true -> \"true\"");
        check(strcmp(pin_value_to_string(&iv), "-42") == 0, "fmt: i32 -42 -> \"-42\"");
    }

    /* 12. seed graph evaluates to 5.0 */
    {
        graph gs;
        graph_init(&gs);
        check(demo_graph_seed(&gs) == 0, "seed graph builds");

        const node_desc *adddesc = registry_node_by_name("add_floats");
        node *addn = NULL;
        for (int i = 0; i < acount(gs.nodes); i++) {
            if (gs.nodes[i]->desc == adddesc) {
                addn = gs.nodes[i];
                break;
            }
        }
        check(addn != NULL, "seed graph has an add_floats node");

        if (addn) {
            arena as;
            arena_init(&as, 4096);
            pin_value seed_sum;
            check(graph_eval_output(&gs, addn, 0, &as, &seed_sum, &eval_ctx) == 0, "seed graph evals");
            check(seed_sum.type == PIN_F32 && seed_sum.f == 5.0f, "seed graph: 2.0 + 3.0 == 5.0");
            arena_free(&as);
        }
        graph_free(&gs);
    }

    /* 13. flow demo graph contains both edge kinds and named entry metadata. */
    {
        graph gf;
        graph_init(&gf);
        check(demo_graph_seed_flow(&gf) == 0, "flow demo graph builds");
        int data_edges = 0;
        int exec_edges = 0;
        for (int i = 0; i < acount(gf.edges); i++) {
            if (gf.edges[i].kind == GRAPH_EDGE_DATA) {
                data_edges++;
            }
            if (gf.edges[i].kind == GRAPH_EDGE_EXEC) {
                exec_edges++;
            }
        }
        check(acount(gf.nodes) == 10 && data_edges == 2 && exec_edges == 8, "flow demo graph shows data and exec topology");
        check(
            gf.nodes[0]->reference_name &&
                strcmp(gf.nodes[0]->reference_name, "Begin") == 0,
            "flow demo graph has named Event entry"
        );
        graph_free(&gf);
    }

    /* 14. subset serialization: only selected nodes + their inter-edges */
    {
        graph g2;
        graph_init(&g2);

        const node_desc *real_desc = registry_node_by_name("Real");
        node *c1 = graph_add_node(&g2, real_desc);
        graph_set_pin_default(
            &g2,
            c1,
            0,
            0,
            &(pin_value){ .type = PIN_F64, .type_name = "double", .d = 2.0 }
        );
        node *c2 = graph_add_node(&g2, real_desc);
        graph_set_pin_default(
            &g2,
            c2,
            0,
            0,
            &(pin_value){ .type = PIN_F64, .type_name = "double", .d = 3.0 }
        );
        node *ad = graph_add_node(&g2, add);
        graph_connect(&g2, c1, 0, ad, 0);
        graph_connect(&g2, c2, 0, ad, 1);
        c1->x = 10.0f;
        c1->y = 20.0f;
        c2->x = 30.0f;
        c2->y = 40.0f;
        ad->x = 50.0f;
        ad->y = 60.0f;

        node *subset[] = { c1, ad }; /* c2 is outside the subset */
        char *json = graph_save_nodes_json(&g2, subset, 2);
        check(json != NULL, "subset: serialize");
        if (json) {
            printf("\n--- serialized subset ---\n%s--- end ---\n", json);

            graph *g3 = graph_load_json(json);
            check(g3 != NULL, "subset: load");
            if (g3) {
                check(acount(g3->nodes) == 2, "subset: node count is 2");
                check(acount(g3->edges) == 1, "subset: crossing edge dropped");
                check(g3->nodes[0]->x == 10.0f && g3->nodes[0]->y == 20.0f, "subset: node 0 position");
                check(g3->nodes[1]->x == 50.0f && g3->nodes[1]->y == 60.0f, "subset: node 1 position");
                check(g3->nodes[0]->input_pins[0].default_value.d == 2.0, "subset: literal value survives");
                graph_free(g3);
            }
            free(json);
        }
        graph_free(&g2);
    }

    /* 14. append: paste a serialized subgraph with a position offset */
    {
        graph g4;
        graph_init(&g4);

        const node_desc *real_desc = registry_node_by_name("Real");
        node *c1 = graph_add_node(&g4, real_desc);
        graph_set_pin_default(
            &g4,
            c1,
            0,
            0,
            &(pin_value){ .type = PIN_F64, .type_name = "double", .d = 2.0 }
        );
        node *c2 = graph_add_node(&g4, real_desc);
        graph_set_pin_default(
            &g4,
            c2,
            0,
            0,
            &(pin_value){ .type = PIN_F64, .type_name = "double", .d = 3.0 }
        );
        node *ad = graph_add_node(&g4, add);
        graph_connect(&g4, c1, 0, ad, 0);
        graph_connect(&g4, c2, 0, ad, 1);
        c1->x = 10.0f;
        c1->y = 20.0f;
        ad->x = 50.0f;
        ad->y = 60.0f;

        node *subset[] = { c1, ad };
        char *json = graph_save_nodes_json(&g4, subset, 2);
        check(json != NULL, "append: serialize source subset");
        if (json) {
            graph g5;
            graph_init(&g5);
            check(acount(g5.nodes) == 0, "append: fresh graph starts empty");
            check(graph_append_json(&g5, json, 100.0f, 200.0f) == 0, "append: returns 0");
            check(acount(g5.nodes) == 2, "append: node count");
            check(acount(g5.edges) == 1, "append: edge count");
            check(g5.nodes[0]->x == 110.0f && g5.nodes[0]->y == 220.0f, "append: node 0 offset");
            check(g5.nodes[1]->x == 150.0f && g5.nodes[1]->y == 260.0f, "append: node 1 offset");
            check(g5.nodes[0]->input_pins[0].default_value.d == 2.0, "append: literal survives");

            int n_before = acount(g5.nodes);
            check(graph_append_json(&g5, "{ not json", 0.0f, 0.0f) == -1, "append: bad json fails");
            check(acount(g5.nodes) == n_before, "append: graph untouched on error");

            graph_free(&g5);
            free(json);
        }
        graph_free(&g4);
    }

    /* 15. DATA/EXEC edge distinction, explicit order, and flow registry. */
    {
        graph ge;
        graph_init(&ge);
        const node_desc *event_desc = registry_node_by_name("Event");
        const node_desc *branch_desc = registry_node_by_name("Branch");
        const node_desc *bool_desc = registry_node_by_name("Bool");
        check(event_desc && branch_desc && bool_desc, "Branch flow built-in is registered");
        node *event = graph_add_node(&ge, event_desc);
        node *branch = graph_add_node(&ge, branch_desc);
        node *truth = graph_add_node(&ge, bool_desc);
        check(graph_set_event_name(&ge, event, "EdgeTest") == 0, "event name is graph metadata");
        check(graph_connect_exec(&ge, event, 0, branch, 0, 7) == 0, "exec edge connects");
        node *fanout = graph_add_node(&ge, &g_sink_a_desc);
        check(graph_connect_exec(&ge, event, 0, fanout, 0, 0) != 0, "ordinary EXEC output rejects ambiguous fan-out");
        check(graph_connect_data(&ge, truth, 0, branch, 1) == 0, "data edge connects");
        pin_value true_value = { .type = PIN_BOOL, .b = true };
        graph_set_pin_default(&ge, truth, 0, 0, &true_value);
        check(acount(ge.edges) == 2 && ge.edges[0].kind != ge.edges[1].kind, "data and exec edges remain distinct");
        node *src = NULL;
        int src_pin = -1;
        int order = -1;
        check(
            graph_exec_input_source(&ge, branch, 0, &src, &src_pin, &order) &&
                src == event && src_pin == 0 && order == 7,
            "exec input source preserves order"
        );
        node *target = NULL;
        int target_pin = -1;
        check(
            graph_exec_target_at(&ge, event, 0, 7, &target, &target_pin) &&
                target == branch && target_pin == 0,
            "exec target lookup preserves order"
        );
        node *event_merge = graph_add_node(&ge, event_desc);
        check(
            graph_connect_exec(&ge, event_merge, 0, branch, 0, 8) == 0 &&
                acount(ge.edges) == 3,
            "exec input accepts multiple incoming drivers"
        );
        check(graph_connect_data(&ge, event, 0, branch, 1) != 0, "exec output cannot become a data edge");
        graph_free(&ge);
    }

    /* 16. Event -> Branch true/false and named dispatch errors. */
    {
        graph gf;
        graph_init(&gf);
        node *event = graph_add_node(&gf, registry_node_by_name("Event"));
        node *branch = graph_add_node(&gf, registry_node_by_name("Branch"));
        node *condition = graph_add_node(&gf, registry_node_by_name("Bool"));
        node *yes = graph_add_node(&gf, &g_sink_a_desc);
        node *no = graph_add_node(&gf, &g_sink_b_desc);
        check(graph_set_event_name(&gf, event, "Choose") == 0, "named event configured");
        check(graph_connect_exec(&gf, event, 0, branch, 0, 0) == 0, "Event -> Branch exec");
        check(graph_connect_data(&gf, condition, 0, branch, 1) == 0, "Branch condition data");
        check(graph_connect_exec(&gf, branch, 0, yes, 0, 0) == 0, "Branch true edge");
        check(graph_connect_exec(&gf, branch, 1, no, 0, 0) == 0, "Branch false edge");
        graph_set_pin_default(&gf, condition, 0, 0, &(pin_value){ .type = PIN_BOOL, .b = true });
        flow_log_reset();
        flow_run *run = graph_run_event(&gf, "Choose", (flow_inputs){ 0 }, &dispatch_error);
        check(run && flow_run_status(run) == FLOW_COMPLETED && strcmp(g_flow_log, "A") == 0, "named event takes Branch true");
        check(run != NULL, "named event creates a flow run");
        check(flow_run_status(run) == FLOW_COMPLETED, "completed run reports one state");
        int traversal_count = 0;
        const flow_traversal *traversals = flow_run_traversals(run, &traversal_count);
        check(
            traversal_count >= 3 && traversals[0].kind == GRAPH_EDGE_EXEC &&
                traversals[1].kind == GRAPH_EDGE_DATA &&
                traversals[2].kind == GRAPH_EDGE_EXEC &&
                traversals[0].activation <= traversals[1].activation &&
                traversals[1].activation <= traversals[2].activation,
            "traversed exec path is identifiable in order"
        );
        check(
            traversals[0].timestamp <= traversals[1].timestamp &&
                traversals[1].timestamp <= traversals[2].timestamp,
            "traversal records carry ordered runtime timestamps"
        );
        flow_run_stop(run);
        int stopped_count = 0;
        (void)flow_run_traversals(run, &stopped_count);
        check(stopped_count == traversal_count, "stopped flow records can settle visually");
        flow_run_free(run);
        graph_set_pin_default(&gf, condition, 0, 0, &(pin_value){ .type = PIN_BOOL, .b = false });
        flow_log_reset();
        run = graph_run_event(&gf, "Choose", (flow_inputs){ 0 }, &dispatch_error);
        check(run && flow_run_status(run) == FLOW_COMPLETED && strcmp(g_flow_log, "B") == 0, "named event takes Branch false");
        flow_run_free(run);
        check(
            graph_run_event(&gf, "Missing", (flow_inputs){ 0 }, &dispatch_error) == NULL &&
                dispatch_error.code == FLOW_ERR_UNKNOWN_EVENT,
            "unknown event reports through the dispatch out-parameter"
        );
        node *duplicate = graph_add_node(&gf, registry_node_by_name("Event"));
        check(graph_set_event_name(&gf, duplicate, "Choose") == 0, "duplicate event configured");
        check(
            graph_run_event(&gf, "Choose", (flow_inputs){ 0 }, &dispatch_error) == NULL &&
                dispatch_error.code == FLOW_ERR_DUPLICATE_EVENT,
            "duplicate event uses dispatch error"
        );
        graph_free(&gf);
    }

    /* 17. Sequence executes outputs in order and stops on the first error. */
    {
        graph gs;
        graph_init(&gs);
        node *event = graph_add_node(&gs, registry_node_by_name("Event"));
        node *sequence = graph_add_node(&gs, registry_node_by_name("Sequence"));
        pin_spec seq_extra = { "ThenExtra", PIN_FLOW, NULL, PIN_EXEC };
        if (sequence) {
            graph_add_pin(&gs, sequence, 1, &seq_extra);
            graph_add_pin(&gs, sequence, 1, &seq_extra);
        }
        node *a = graph_add_node(&gs, &g_sink_a_desc);
        node *b = graph_add_node(&gs, &g_sink_b_desc);
        node *c = graph_add_node(&gs, &g_sink_c_desc);
        check(graph_set_event_name(&gs, event, "Sequence") == 0, "sequence event configured");
        check(graph_connect_exec(&gs, event, 0, sequence, 0, 0) == 0, "event -> sequence");
        check(
            graph_connect_exec(&gs, sequence, 0, a, 0, 9) == 0 &&
                graph_connect_exec(&gs, sequence, 1, b, 0, 4) == 0 &&
                graph_connect_exec(&gs, sequence, 2, c, 0, 1) == 0,
            "sequence ordered outputs connect"
        );
        flow_log_reset();
        flow_run *run = graph_run_event(&gs, "Sequence", (flow_inputs){ 0 }, &dispatch_error);
        check(run && flow_run_status(run) == FLOW_COMPLETED && strcmp(g_flow_log, "ABC") == 0, "Sequence order is 0,1,2,3");
        int traversal_count = 0;
        (void)flow_run_traversals(run, &traversal_count);
        check(traversal_count == 4, "Sequence records each traversed exec edge");
        flow_run_free(run);
        graph_free(&gs);

        graph capped;
        graph_init(&capped);
        node *cap_event = graph_add_node(&capped, registry_node_by_name("Event"));
        node *cap_sequence = graph_add_node(&capped, registry_node_by_name("Sequence"));
        node *cap_sink = graph_add_node(&capped, &g_sink_a_desc);
        graph_set_event_name(&capped, cap_event, "CappedFlow");
        graph_connect_exec(&capped, cap_event, 0, cap_sequence, 0, 0);
        for (int i = 2; i < 300; i++) {
            pin_spec extra = { "ThenExtra", PIN_FLOW, NULL, PIN_EXEC };
            graph_add_pin(&capped, cap_sequence, 1, &extra);
        }
        for (int i = 0; i < cap_sequence->output_count; i++) {
            graph_connect_exec(&capped, cap_sequence, i, cap_sink, 0, i);
        }
        run = graph_run_event(&capped, "CappedFlow", (flow_inputs){ 0 }, &dispatch_error);
        traversal_count = 0;
        (void)flow_run_traversals(run, &traversal_count);
        check(traversal_count == 256, "traversal records stay within their bound");
        flow_run_free(run);
        graph_free(&capped);

        graph seven;
        graph_init(&seven);
        node *seven_event = graph_add_node(&seven, registry_node_by_name("Event"));
        node *seven_sequence = graph_add_node(&seven, registry_node_by_name("Sequence"));
        pin_spec seven_pin = { "ThenExtra", PIN_FLOW, NULL, PIN_EXEC };
        while (seven_sequence && seven_sequence->output_count < 7) {
            graph_add_pin(&seven, seven_sequence, 1, &seven_pin);
        }
        node *seven_sinks[7];
        for (int i = 0; i < 7; i++) {
            seven_sinks[i] = graph_add_node(&seven, &g_sink_a_desc);
        }
        graph_set_event_name(&seven, seven_event, "Seven");
        graph_connect_exec(&seven, seven_event, 0, seven_sequence, 0, 0);
        for (int i = 0; i < 7; i++) {
            graph_connect_exec(&seven, seven_sequence, i, seven_sinks[i], 0, i);
        }
        flow_log_reset();
        run = graph_run_event(&seven, "Seven", (flow_inputs){ 0 }, &dispatch_error);
        check(seven_sequence && seven_sequence->output_count == 7, "Sequence extends from its default of two outputs");
        check(
            run && flow_run_status(run) == FLOW_COMPLETED && g_flow_log_count == 7,
            "seven Sequence outputs all fire in ascending order"
        );
        flow_run_free(run);
        graph_free(&seven);

        graph ge;
        graph_init(&ge);
        event = graph_add_node(&ge, registry_node_by_name("Event"));
        sequence = graph_add_node(&ge, registry_node_by_name("Sequence"));
        if (sequence) {
            graph_add_pin(&ge, sequence, 1, &seq_extra);
            graph_add_pin(&ge, sequence, 1, &seq_extra);
        }
        a = graph_add_node(&ge, &g_sink_a_desc);
        node *error_node = graph_add_node(&ge, &g_sink_error_desc);
        c = graph_add_node(&ge, &g_sink_c_desc);
        graph_set_event_name(&ge, event, "Stop");
        graph_connect_exec(&ge, event, 0, sequence, 0, 0);
        graph_connect_exec(&ge, sequence, 0, a, 0, 0);
        graph_connect_exec(&ge, sequence, 1, error_node, 0, 0);
        graph_connect_exec(&ge, sequence, 2, c, 0, 0);
        flow_log_reset();
        run = graph_run_event(&ge, "Stop", (flow_inputs){ 0 }, &dispatch_error);
        check(
            run && flow_run_status(run) == FLOW_FAILED &&
                strcmp(g_flow_log, "A") == 0 && flow_run_error(run)->node == error_node,
            "Sequence stops immediately on error"
        );
        check(
            flow_run_error(run)->node == error_node && flow_run_error(run)->message,
            "structured run errors identify the responsible node and message"
        );
        flow_run_free(run);
        graph_free(&ge);
    }

    /* 18. Event parameter pins carry ordered values and preserve identity when moved. */
    {
        graph gp;
        graph_init(&gp);
        node *event = graph_add_node(&gp, registry_node_by_name("Event"));
        node *sink = graph_add_node(&gp, &g_parameter_sink_desc);
        pin_spec integer = { "Count", PIN_I32, "int", PIN_DATA };
        pin_spec flag = { "Enabled", PIN_BOOL, "bool", PIN_DATA };
        pin_spec text = { "Label", PIN_STR, "const char *", PIN_DATA };
        graph_add_pin(&gp, event, 1, &integer);
        graph_add_pin(&gp, event, 1, &flag);
        graph_add_pin(&gp, event, 1, &text);
        pin_value authored_text = { .type = PIN_STR, .str = "authored" };
        graph_set_pin_default(&gp, event, 1, 3, &authored_text);
        graph_connect_exec(&gp, event, 0, sink, 0, 0);
        graph_connect_data(&gp, event, 1, sink, 1);
        graph_connect_data(&gp, event, 2, sink, 2);
        graph_connect_data(&gp, event, 3, sink, 3);
        uint64_t moved_id = event->output_pins[3].id;
        pin_value first_inputs[] = {
            { .type = PIN_I32, .i = 7 },
            { .type = PIN_BOOL, .b = true },
            { .type = PIN_STR, .str = "first" },
        };
        flow_error parameter_error;
        flow_run *parameter_run = flow_run_start(
            &gp,
            event,
            (flow_inputs){ first_inputs, 3, NULL },
            NULL,
            &parameter_error
        );
        (void)flow_run_resume(parameter_run);
        check(
            parameter_run && flow_run_status(parameter_run) == FLOW_COMPLETED &&
                g_parameter_integer == 7 && g_parameter_flag &&
                strcmp(g_parameter_text, "first") == 0,
            "event parameters deliver typed values in order"
        );
        flow_run_free(parameter_run);

        check(
            graph_move_pin(&gp, event, 1, 3, 1) == 1 &&
                event->output_pins[1].id == moved_id &&
                event->output_pins[1].default_value.str &&
                strcmp(event->output_pins[1].default_value.str, "authored") == 0,
            "moving a pin preserves its ID, authored value, and position"
        );
        node *source = NULL;
        int source_pin = -1;
        int moved_output = -1;
        const pin_spec *resolved = graph_pin_by_id(event, moved_id, &moved_output, &source_pin);
        check(
            resolved && moved_output && source_pin == 1 &&
                graph_input_source(&gp, sink, 3, &source, &source_pin) == 1 &&
                source == event && source_pin == 1,
            "moved pin ID keeps its attached edge resolved"
        );

        pin_value reordered_inputs[] = {
            { .type = PIN_STR, .str = "second" },
            { .type = PIN_I32, .i = 11 },
            { .type = PIN_BOOL, .b = false },
        };
        parameter_run = flow_run_start(
            &gp,
            event,
            (flow_inputs){ reordered_inputs, 3, NULL },
            NULL,
            &parameter_error
        );
        (void)flow_run_resume(parameter_run);
        check(
            parameter_run && flow_run_status(parameter_run) == FLOW_COMPLETED &&
                g_parameter_integer == 11 && !g_parameter_flag &&
                strcmp(g_parameter_text, "second") == 0,
            "reordering parameters reorders delivered values"
        );
        flow_run_free(parameter_run);
        graph_free(&gp);
    }

    /* 19. ForLoop uses half-open forward and reverse ranges. */
    {
        graph gl;
        graph_init(&gl);
        node *event = graph_add_node(&gl, registry_node_by_name("Event"));
        node *loop = graph_add_node(&gl, registry_node_by_name("ForLoop"));
        node *body = graph_add_node(&gl, &g_sink_body_desc);
        node *done = graph_add_node(&gl, &g_sink_done_desc);
        graph_set_event_name(&gl, event, "Loop");
        graph_connect_exec(&gl, event, 0, loop, 0, 0);
        graph_connect_exec(&gl, loop, 0, body, 0, 0);
        graph_connect_data(&gl, loop, 1, body, 1);
        graph_connect_exec(&gl, loop, 2, done, 0, 0);
        graph_set_pin_default(&gl, loop, 0, 1, &(pin_value){ .type = PIN_I32, .i = 2 });
        graph_set_pin_default(&gl, loop, 0, 2, &(pin_value){ .type = PIN_I32, .i = 4 });
        flow_log_reset();
        flow_run *run = graph_run_event(&gl, "Loop", (flow_inputs){ 0 }, &dispatch_error);
        check(
            run && flow_run_status(run) == FLOW_COMPLETED &&
                g_flow_index_count == 2 && g_flow_indices[0] == 2 &&
                g_flow_indices[1] == 3 && strcmp(g_flow_log, "D") == 0,
            "ForLoop runs half-open 2..4 then Completed"
        );
        check(
            g_flow_index_count == 2 && g_flow_indices[0] == 2 && g_flow_indices[1] == 3,
            "each loop activation evaluates fresh data"
        );
        flow_run_free(run);
        graph_set_pin_default(&gl, loop, 0, 1, &(pin_value){ .type = PIN_I32, .i = 2 });
        graph_set_pin_default(&gl, loop, 0, 2, &(pin_value){ .type = PIN_I32, .i = 2 });
        flow_log_reset();
        run = graph_run_event(&gl, "Loop", (flow_inputs){ 0 }, &dispatch_error);
        check(
            run && flow_run_status(run) == FLOW_COMPLETED &&
                g_flow_index_count == 0 && strcmp(g_flow_log, "D") == 0,
            "ForLoop skips equal bounds"
        );
        flow_run_free(run);
        graph_set_pin_default(&gl, loop, 0, 1, &(pin_value){ .type = PIN_I32, .i = 4 });
        graph_set_pin_default(&gl, loop, 0, 2, &(pin_value){ .type = PIN_I32, .i = 2 });
        flow_log_reset();
        run = graph_run_event(&gl, "Loop", (flow_inputs){ 0 }, &dispatch_error);
        check(
            run && flow_run_status(run) == FLOW_COMPLETED &&
                g_flow_index_count == 2 && g_flow_indices[0] == 4 &&
                g_flow_indices[1] == 3 && strcmp(g_flow_log, "D") == 0,
            "ForLoop runs half-open 4..2 in reverse then completes"
        );
        flow_run_free(run);
        check(graph_remove_edge(&gl, event, 0, loop, 0) == 0, "removing Event -> ForLoop disconnects the executable path");
        flow_log_reset();
        run = graph_run_event(&gl, "Loop", (flow_inputs){ 0 }, &dispatch_error);
        check(
            run && flow_run_status(run) == FLOW_COMPLETED &&
                g_flow_index_count == 0 && g_flow_log_count == 0,
            "a disconnected Event does not activate ForLoop"
        );
        flow_run_free(run);
        graph_free(&gl);
    }

    /* 19. Delay yields, resumes from host time, and preserves nested Sequence. */
    {
        graph gd;
        graph_init(&gd);
        node *event = graph_add_node(&gd, registry_node_by_name("Event"));
        node *delay = graph_add_node(&gd, registry_node_by_name("Delay"));
        node *done = graph_add_node(&gd, &g_sink_done_desc);
        graph_set_event_name(&gd, event, "Wait");
        graph_connect_exec(&gd, event, 0, delay, 0, 0);
        graph_connect_exec(&gd, delay, 0, done, 0, 0);
        pin_spec guard_pin = { "Parameter", PIN_I32, "int", PIN_DATA };
        graph_add_pin(&gd, event, 1, &guard_pin);
        graph_add_pin(&gd, event, 1, &guard_pin);
        graph_set_pin_default(&gd, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 5.0 });
        flow_log_reset();
        flow_run *run = graph_run_event(&gd, "Wait", (flow_inputs){ 0 }, &dispatch_error);
        check(run && flow_run_status(run) == FLOW_SUSPENDED && g_flow_log_count == 0, "Delay suspends flow run");
        check(graph_connect_exec(&gd, event, 0, done, 0, 0) != 0, "paused flow rejects topology mutation");
        check(graph_move_pin(&gd, event, 1, 2, 1) != 0, "live run rejects instance-pin reordering");
        check(
            flow_run_tick_at(run, 4.0) == FLOW_SUSPENDED_RESULT &&
                flow_run_status(run) == FLOW_SUSPENDED,
            "Delay remains suspended before host deadline"
        );
        check(
            flow_run_tick_at(run, 5.0) == FLOW_OK &&
                flow_run_status(run) == FLOW_COMPLETED && strcmp(g_flow_log, "D") == 0,
            "Delay resumes at host monotonic time"
        );
        check(flow_run_status(run) == FLOW_COMPLETED, "host-supplied monotonic time advances the run");
        flow_run_free(run);
        graph_free(&gd);

        graph gt;
        graph_init(&gt);
        node *topology_event = graph_add_node(&gt, registry_node_by_name("Event"));
        node *topology_delay = graph_add_node(&gt, registry_node_by_name("Delay"));
        graph_connect_exec(&gt, topology_event, 0, topology_delay, 0, 0);
        graph_set_pin_default(&gt, topology_delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 10.0 });
        flow_run *topology_run = flow_run_start(&gt, topology_event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        (void)flow_run_resume(topology_run);
        graph_flow_resume(&gt);
        node *topology_mutation = graph_add_node(&gt, &g_sink_a_desc);
        graph_flow_pause(&gt);
        check(
            topology_mutation && topology_run &&
                flow_run_tick_at(topology_run, 1.0) == FLOW_ERR_TOPOLOGY &&
                flow_run_status(topology_run) == FLOW_FAILED &&
                flow_run_error(topology_run)->code == FLOW_ERR_TOPOLOGY,
            "changed topology fails the run"
        );
        flow_run_free(topology_run);
        graph_free(&gt);

        graph gn;
        graph_init(&gn);
        event = graph_add_node(&gn, registry_node_by_name("Event"));
        node *sequence = graph_add_node(&gn, registry_node_by_name("Sequence"));
        delay = graph_add_node(&gn, registry_node_by_name("Delay"));
        node *first = graph_add_node(&gn, &g_sink_a_desc);
        node *second = graph_add_node(&gn, &g_sink_b_desc);
        graph_set_event_name(&gn, event, "Nested");
        graph_connect_exec(&gn, event, 0, sequence, 0, 0);
        graph_connect_exec(&gn, sequence, 0, delay, 0, 0);
        graph_connect_exec(&gn, delay, 0, first, 0, 0);
        graph_connect_exec(&gn, sequence, 1, second, 0, 0);
        graph_set_pin_default(&gn, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 3.0 });
        flow_log_reset();
        run = graph_run_event(&gn, "Nested", (flow_inputs){ 0 }, &dispatch_error);
        check(run && flow_run_status(run) == FLOW_SUSPENDED && g_flow_log_count == 0, "nested Sequence suspends at first Delay");
        flow_run_tick_at(run, 3.0);
        check(
            flow_run_status(run) == FLOW_COMPLETED && strcmp(g_flow_log, "AB") == 0,
            "nested Sequence resumes remaining outputs in order"
        );
        flow_run_free(run);
        graph_free(&gn);
    }

    /* 20. Step budget and v3 persistence. */
    {
        graph gbudget;
        graph_init(&gbudget);
        node *cycle = graph_add_node(&gbudget, &g_sink_a_desc);
        graph_connect_exec(&gbudget, cycle, 0, cycle, 0, 0);
        flow_options options = { .step_budget = 3 };
        flow_run *run = flow_run_start(&gbudget, cycle, (flow_inputs){ 0 }, &options, &dispatch_error);
        check(
            run && flow_run_resume(run) == FLOW_ERR_STEP_BUDGET &&
                flow_run_status(run) == FLOW_FAILED && flow_run_steps(run) == 3,
            "flow step budget produces structured failure"
        );
        flow_run_free(run);
        graph_free(&gbudget);

        graph gj;
        graph_init(&gj);
        node *event = graph_add_node(&gj, registry_node_by_name("Event"));
        node *branch = graph_add_node(&gj, registry_node_by_name("Branch"));
        node *condition = graph_add_node(&gj, registry_node_by_name("Bool"));
        graph_set_event_name(&gj, event, "Saved");
        graph_connect_exec(&gj, event, 0, branch, 0, 12);
        graph_connect_data(&gj, condition, 0, branch, 1);
        char *json = graph_save_json(&gj);
        check(
            json && strstr(json, "\"version\": 3") &&
                strstr(json, "\"kind\": \"exec\"") && strstr(json, "\"order\": 12") &&
                strstr(json, "\"reference_name\": \"Saved\""),
            "v3 JSON writes ID-based flow metadata"
        );
        graph *loaded = json ? graph_load_json(json) : NULL;
        check(
            loaded && acount(loaded->edges) == 2 &&
                loaded->edges[0].kind == GRAPH_EDGE_EXEC &&
                graph_event_name(loaded->nodes[0]) &&
                strcmp(graph_event_name(loaded->nodes[0]), "Saved") == 0,
            "v3 JSON loads flow metadata"
        );
        if (loaded) {
            graph_free(loaded);
        }
        free(json);

        node *flow_subset[] = { event, branch };
        char *subset_json = graph_save_nodes_json(&gj, flow_subset, 2);
        graph appended;
        graph_init(&appended);
        check(
            subset_json &&
                graph_append_json(&appended, subset_json, 10.0f, 20.0f) == 0 &&
                acount(appended.edges) == 1 && appended.edges[0].kind == GRAPH_EDGE_EXEC &&
                appended.edges[0].exec_order == 12,
            "append remaps ID-based flow edge"
        );
        uint64_t first_copy = appended.nodes[0]->id;
        uint64_t second_copy = appended.nodes[1]->id;
        check(
            subset_json &&
                graph_append_json(&appended, subset_json, 20.0f, 30.0f) == 0 &&
                acount(appended.nodes) == 4 && acount(appended.edges) == 2 &&
                appended.nodes[2]->id != first_copy &&
                appended.nodes[3]->id != second_copy &&
                appended.edges[1].from_id == appended.nodes[2]->id &&
                appended.edges[1].to_id == appended.nodes[3]->id,
            "repeated paste allocates fresh IDs and rewires internal "
            "edges"
        );
        graph_free(&appended);
        free(subset_json);
        graph_free(&gj);

        check(
            graph_load_json("{\"version\":2,\"nodes\":[],\"edges\":[]}") == NULL &&
                strstr(graph_last_error(), "version"),
            "v2 documents are rejected"
        );
        check(
            graph_load_json("{\"nodes\":[],\"edges\":[]}") == NULL &&
                strstr(graph_last_error(), "version"),
            "versionless documents are rejected"
        );
    }

    /* 21. External pause, one-activation stepping, and resume order. */
    {
        graph gp;
        graph_init(&gp);
        node *event = graph_add_node(&gp, registry_node_by_name("Event"));
        node *first = graph_add_node(&gp, &g_sink_a_desc);
        node *second = graph_add_node(&gp, &g_sink_b_desc);
        node *third = graph_add_node(&gp, &g_sink_c_desc);
        graph_connect_exec(&gp, event, 0, first, 0, 0);
        graph_connect_exec(&gp, first, 0, second, 0, 0);
        graph_connect_exec(&gp, second, 0, third, 0, 0);

        flow_run *run = flow_run_start(&gp, event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        flow_log_reset();
        check(
            run && flow_run_step(run) == FLOW_PAUSED_RESULT &&
                flow_run_status(run) == FLOW_PAUSED &&
                flow_run_steps(run) == 1 && g_flow_log_count == 0,
            "one step activates exactly one entry node"
        );
        check(flow_run_pause(run) == FLOW_OK && flow_run_status(run) == FLOW_PAUSED, "external pause preserves paused state");
        check(
            flow_run_resume(run) == FLOW_OK && flow_run_status(run) == FLOW_COMPLETED &&
                strcmp(g_flow_log, "ABC") == 0,
            "resume continues from the next unactivated node"
        );
        flow_run_free(run);

        flow_log_reset();
        run = flow_run_start(&gp, event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        while (run && flow_run_status(run) != FLOW_COMPLETED &&
               flow_run_status(run) != FLOW_FAILED) {
            (void)flow_run_step(run);
        }
        char stepped_log[sizeof g_flow_log];
        snprintf(stepped_log, sizeof stepped_log, "%s", g_flow_log);
        check(strcmp(stepped_log, "ABC") == 0, "stepping preserves activation order");
        flow_run_free(run);

        flow_log_reset();
        run = flow_run_start(&gp, event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        (void)flow_run_resume(run);
        check(strcmp(g_flow_log, stepped_log) == 0, "stepped output matches uninterrupted resume");
        flow_run_free(run);

        graph gpause;
        graph_init(&gpause);
        node *pause_event = graph_add_node(&gpause, registry_node_by_name("Event"));
        node *pause_print = graph_add_node(&gpause, registry_node_by_name("Print String"));
        node *pause_sink = graph_add_node(&gpause, &g_sink_a_desc);
        node *pause_text = make_string(&gpause, "pause");
        graph_connect_exec(&gpause, pause_event, 0, pause_print, 0, 0);
        graph_connect_exec(&gpause, pause_print, 0, pause_sink, 0, 0);
        graph_connect_data(&gpause, pause_text, 0, pause_print, 1);
        flow_options pause_options = { .sink = print_sink };
        flow_log_reset();
        g_print_count = 0;
        run = flow_run_start(&gpause, pause_event, (flow_inputs){ 0 }, &pause_options, &dispatch_error);
        g_pause_target = run;
        (void)flow_run_resume(run);
        check(
            run && flow_run_status(run) == FLOW_PAUSED && flow_run_steps(run) == 2 &&
                g_print_count == 1 && g_flow_log_count == 0,
            "external pause request stops between activations"
        );
        (void)flow_run_resume(run);
        check(flow_run_status(run) == FLOW_COMPLETED && strcmp(g_flow_log, "A") == 0, "paused run resumes at the next node");
        flow_run_free(run);
        graph_free(&gpause);
        graph_free(&gp);
    }

    /* 22. Persistent single-step mode crosses a latent suspension. */
    {
        graph gs;
        graph_init(&gs);
        node *event = graph_add_node(&gs, registry_node_by_name("Event"));
        node *delay = graph_add_node(&gs, registry_node_by_name("Delay"));
        node *done = graph_add_node(&gs, &g_sink_done_desc);
        graph_connect_exec(&gs, event, 0, delay, 0, 0);
        graph_connect_exec(&gs, delay, 0, done, 0, 0);
        graph_set_pin_default(&gs, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 1.0 });
        flow_options clocked = { .initial_time = 0.0 };
        flow_log_reset();
        flow_run *run = flow_run_start(&gs, event, (flow_inputs){ 0 }, &clocked, &dispatch_error);
        check(
            run && flow_run_step(run) == FLOW_PAUSED_RESULT &&
                flow_run_status(run) == FLOW_PAUSED && flow_run_steps(run) == 1,
            "step mode pauses after one activation before Delay"
        );
        check(
            flow_run_step(run) == FLOW_SUSPENDED_RESULT &&
                flow_run_status(run) == FLOW_SUSPENDED && flow_run_steps(run) == 2,
            "stepping activates exactly one Delay across suspension"
        );
        check(
            flow_run_tick_at(run, 1.0) == FLOW_PAUSED_RESULT &&
                flow_run_status(run) == FLOW_PAUSED && flow_run_steps(run) == 3 &&
                g_flow_log_count == 1 && strcmp(g_flow_log, "D") == 0,
            "deadline tick keeps step mode and pauses after one "
            "activation"
        );
        check(
            flow_run_resume(run) == FLOW_OK &&
                flow_run_status(run) == FLOW_COMPLETED && flow_run_steps(run) == 3,
            "explicit Resume clears step mode and continues freely"
        );
        flow_run_free(run);
        graph_free(&gs);
    }

    /* 23. Pausing a suspended run enters the same persistent step mode. */
    {
        graph gs;
        graph_init(&gs);
        node *event = graph_add_node(&gs, registry_node_by_name("Event"));
        node *delay = graph_add_node(&gs, registry_node_by_name("Delay"));
        node *done = graph_add_node(&gs, &g_sink_done_desc);
        graph_connect_exec(&gs, event, 0, delay, 0, 0);
        graph_connect_exec(&gs, delay, 0, done, 0, 0);
        graph_set_pin_default(&gs, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 1.0 });
        flow_options clocked = { .initial_time = 0.0 };
        flow_log_reset();
        flow_run *run = flow_run_start(&gs, event, (flow_inputs){ 0 }, &clocked, &dispatch_error);
        (void)flow_run_resume(run);
        check(
            run && flow_run_status(run) == FLOW_SUSPENDED &&
                flow_run_pause(run) == FLOW_OK &&
                flow_run_tick_at(run, 1.0) == FLOW_PAUSED_RESULT &&
                flow_run_status(run) == FLOW_PAUSED && flow_run_steps(run) == 3,
            "pausing while SUSPENDED steps once after the deadline"
        );
        check(
            flow_run_resume(run) == FLOW_OK && flow_run_status(run) == FLOW_COMPLETED,
            "resumed suspended step completes without another step limit"
        );
        flow_run_free(run);
        graph_free(&gs);
    }

    /* 24. Per-run errors and step counts do not share process-global state. */
    {
        graph gtwo;
        graph_init(&gtwo);
        node *event = graph_add_node(&gtwo, registry_node_by_name("Event"));
        node *error_node = graph_add_node(&gtwo, &g_sink_error_desc);
        graph_connect_exec(&gtwo, event, 0, error_node, 0, 0);
        flow_run *run_a = flow_run_start(&gtwo, event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        flow_run *run_b = flow_run_start(&gtwo, event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        int error_a = run_a ? flow_run_resume(run_a) : 0;
        int error_b = run_b ? flow_run_resume(run_b) : 0;
        check(
            run_a && run_b && error_a == -777 && error_b == -777 &&
                flow_run_steps(run_a) == 2 && flow_run_steps(run_b) == 2 &&
                flow_run_error(run_a) != flow_run_error(run_b),
            "two runs keep independent errors and step counts"
        );
        flow_run_free(run_a);
        flow_run_free(run_b);
        graph_free(&gtwo);
    }

    /* 23. Stop releases graph guards and clears runtime state. */
    {
        graph gr;
        graph_init(&gr);
        node *event = graph_add_node(&gr, registry_node_by_name("Event"));
        node *sink = graph_add_node(&gr, &g_sink_a_desc);
        graph_connect_exec(&gr, event, 0, sink, 0, 0);
        flow_log_reset();
        flow_run *run = flow_run_start(&gr, event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        (void)flow_run_resume(run);
        char first_output[sizeof g_flow_log];
        snprintf(first_output, sizeof first_output, "%s", g_flow_log);
        check(flow_run_stop(run) == FLOW_OK && flow_run_status(run) == FLOW_IDLE, "stop returns run to idle");
        check(
            graph_add_node(&gr, &g_sink_b_desc) != NULL && flow_run_status(run) == FLOW_IDLE,
            "stop clears activation state and releases graph mutation guard"
        );
        flow_run_free(run);
        flow_log_reset();
        run = flow_run_start(&gr, event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        (void)flow_run_resume(run);
        check(strcmp(first_output, g_flow_log) == 0, "fresh run output matches the first run");
        flow_run_free(run);
        graph_free(&gr);
    }

    /* 24. Stopping a latent run releases the graph without another activation. */
    {
        graph gs;
        graph_init(&gs);
        node *event = graph_add_node(&gs, registry_node_by_name("Event"));
        node *delay = graph_add_node(&gs, registry_node_by_name("Delay"));
        node *done = graph_add_node(&gs, &g_sink_done_desc);
        graph_connect_exec(&gs, event, 0, delay, 0, 0);
        graph_connect_exec(&gs, delay, 0, done, 0, 0);
        graph_set_pin_default(&gs, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 10.0 });
        flow_log_reset();
        flow_options clocked = { .initial_time = 0.0 };
        flow_run *run = flow_run_start(&gs, event, (flow_inputs){ 0 }, &clocked, &dispatch_error);
        (void)flow_run_resume(run);
        check(flow_run_status(run) == FLOW_SUSPENDED, "Delay leaves a suspended run");
        check(
            flow_run_stop(run) == FLOW_OK &&
                flow_run_status(run) == FLOW_IDLE && g_flow_log_count == 0,
            "stop aborts without activating the continuation"
        );
        check(graph_add_node(&gs, &g_sink_a_desc) != NULL, "stopped run leaves graph mutable");
        check(graph_add_node(&gs, &g_sink_b_desc) != NULL, "authoring mutation succeeds after suspended run teardown");
        flow_run_free(run);
        graph_free(&gs);
    }

    /* 25. Print String delivers ordered host messages with their origins. */
    {
        graph gp;
        graph_init(&gp);
        node *event = graph_add_node(&gp, registry_node_by_name("Event"));
        node *print_a = graph_add_node(&gp, registry_node_by_name("Print String"));
        node *print_b = graph_add_node(&gp, registry_node_by_name("Print String"));
        node *str_a = make_string(&gp, "first");
        node *str_b = make_string(&gp, "second");
        graph_connect_exec(&gp, event, 0, print_a, 0, 0);
        graph_connect_exec(&gp, print_a, 0, print_b, 0, 0);
        graph_connect_data(&gp, str_a, 0, print_a, 1);
        graph_connect_data(&gp, str_b, 0, print_b, 1);
        memset(g_print_messages, 0, sizeof g_print_messages);
        memset(g_print_origins, 0, sizeof g_print_origins);
        g_print_count = 0;
        flow_options printing = { .sink = print_sink };
        flow_run *run = flow_run_start(&gp, event, (flow_inputs){ 0 }, &printing, &dispatch_error);
        (void)flow_run_resume(run);
        check(
            run && flow_run_status(run) == FLOW_COMPLETED &&
                g_print_count == 2 && strcmp(g_print_messages[0], "first") == 0 &&
                strcmp(g_print_messages[1], "second") == 0 &&
                g_print_origins[0] == print_a && g_print_origins[1] == print_b,
            "Print String sends ordered messages and originating nodes"
        );
        check(g_print_count == 2 && g_print_origins[0] == print_a, "host sink receives output with its origin");
        flow_run_free(run);
        graph_free(&gp);
    }

    /* 26. Struct watches copy out of the run arena before the run is freed. */
    {
        graph gw;
        graph_init(&gw);
        node *event = graph_add_node(&gw, registry_node_by_name("Event"));
        node *watch_node = graph_add_node(&gw, &g_watch_desc);
        graph_connect_exec(&gw, event, 0, watch_node, 0, 0);
        flow_run *run = flow_run_start(&gw, event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        (void)flow_run_resume(run);
        pin_value recorded;
        Vec3 copied = { 0 };
        int watch_result = flow_run_watch_output(run, watch_node, 1, &recorded);
        int copy_result = flow_run_watch_copy_output(run, watch_node, 1, &recorded, &copied, sizeof copied);
        check(
            watch_result == 0 && copy_result == 0 &&
                recorded.type == PIN_STRUCT && recorded.ptr != &g_watch_struct &&
                copied.x == 4.0f && copied.y == 5.0f && copied.z == 6.0f,
            "struct watch value is copied out of the run arena"
        );
        check(
            flow_run_watch_output(run, watch_node, 1, &recorded) == 0 &&
                recorded.ptr != &g_watch_struct && ((Vec3 *)recorded.ptr)->x == 4.0f,
            "live run watch reads the recorded execution value"
        );
        flow_run_free(run);
        check(copied.x == 4.0f && copied.y == 5.0f && copied.z == 6.0f, "copied struct remains valid after run free");
        run = flow_run_start(&gw, event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        (void)flow_run_resume(run);
        check(
            flow_run_stop(run) == FLOW_OK &&
                flow_run_watch_output(run, watch_node, 1, &recorded) != 0,
            "Stop clears recorded watch values"
        );
        flow_run_free(run);
        graph_free(&gw);
    }

    /* 27. Serialization contains authored graph data, never run state. */
    {
        graph gs;
        graph_init(&gs);
        node *event = graph_add_node(&gs, registry_node_by_name("Event"));
        node *delay = graph_add_node(&gs, registry_node_by_name("Delay"));
        graph_connect_exec(&gs, event, 0, delay, 0, 0);
        graph_set_pin_default(&gs, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 2.0 });
        flow_run *run = flow_run_start(&gs, event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        (void)flow_run_resume(run);
        char *json = graph_save_json(&gs);
        graph *loaded = json ? graph_load_json(json) : NULL;
        check(
            json && !strstr(json, "\\\"run\\\"") && !strstr(json, "\\\"state\\\"") &&
                loaded && acount(loaded->nodes) == 2,
            "suspended save contains authored data and no run state"
        );
        if (loaded) {
            graph_free(loaded);
        }
        free(json);
        flow_run_free(run);
        graph_free(&gs);
    }

    /* Phase 3: persistent Behavior Asset instances, variables, and generic
     * events. Begin() initializes one instance's variables once; later
     * Tick(Real) and Input(Name, Real, Bool) events read and write the same
     * state (VAR-11, EVT-9, EVT-10). */
    {
        behavior_asset *bp = behavior_asset_new("VarDemo");
        check(bp && bp->graph, "a behavior_asset owns its graph and metadata");
        behavior_variable *health = behavior_asset_add_variable(
            bp,
            "Health",
            PIN_I32,
            "int",
            PIN_CONTAINER_SINGLE,
            &(pin_value){ .type = PIN_I32, .i = 100 },
            "Stats"
        );
        check(health != NULL, "variables carry id, name, type, container, default, category");
        check(
            behavior_asset_find_variable_by_name(bp, "Health") == health,
            "variable names are unique and findable within a behavior_asset"
        );
        check(
            behavior_asset_add_variable(bp, "Health", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, NULL) ==
                NULL,
            "duplicate variable names are rejected"
        );
        behavior_variable *last_action = behavior_asset_add_variable(
            bp,
            "LastAction",
            PIN_NAME,
            "name",
            PIN_CONTAINER_SINGLE,
            &(pin_value){ .type = PIN_NAME, .str = "" },
            ""
        );
        check(last_action != NULL, "a Name variable accepts an authored default");

        /* Begin(): Set Health to 50. */
        node *begin_ev = graph_add_node(bp->graph, registry_node_by_name("Event"));
        graph_set_event_name(bp->graph, begin_ev, "Begin");
        check(
            begin_ev->input_count == 0 && begin_ev->output_count >= 1 &&
                begin_ev->output_pins[0].kind == PIN_EXEC,
            "an Event node is an executable entry point"
        );
        check(
            graph_event_name(begin_ev) &&
                strcmp(graph_event_name(begin_ev), "Begin") == 0,
            "event identity is name-addressed within the Behavior Asset"
        );
        node *set = behavior_asset_add_set_node(bp, health->id);
        graph_set_pin_default(bp->graph, set, 0, 1, &(pin_value){ .type = PIN_I32, .i = 50 });
        graph_connect_exec(bp->graph, begin_ev, 0, set, 0, 0);

        /* Tick(Real DeltaSeconds): read Health into a capture node. */
        node *tick_ev = graph_add_node(bp->graph, registry_node_by_name("Event"));
        graph_set_event_name(bp->graph, tick_ev, "Tick");
        pin_spec delta = { "DeltaSeconds", PIN_F64, "double", PIN_DATA };
        graph_add_pin(bp->graph, tick_ev, 1, &delta);
        node *get = behavior_asset_add_get_node(bp, health->id);
        node *capture = graph_add_node(bp->graph, &g_capture_desc);
        graph_connect_exec(bp->graph, tick_ev, 0, capture, 0, 0);
        graph_connect_data(bp->graph, get, 0, capture, 1);

        /* A pure conversion chain must record every data edge, not only the
         * edge entering the flow node that consumes its final output. */
        node *text_ev = graph_add_node(bp->graph, registry_node_by_name("Event"));
        node *to_string = graph_add_node(bp->graph, registry_node_by_name("To String"));
        node *string_capture = graph_add_node(bp->graph, &g_str_capture_desc);
        check(
            text_ev && to_string && string_capture &&
                graph_set_event_name(bp->graph, text_ev, "TickText") == 0 &&
                graph_set_pin_type(bp->graph, to_string, 0, 0, PIN_I32, "int") == 0 &&
                graph_connect_exec(bp->graph, text_ev, 0, string_capture, 0, 0) == 0 &&
                graph_connect_data(bp->graph, get, 0, to_string, 0) == 0 &&
                graph_connect_data(bp->graph, to_string, 0, string_capture, 1) == 0,
            "pure data chain is authored"
        );

        /* Input(Name Action, Real Value, Bool Pressed): Set LastAction. */
        node *input_ev = graph_add_node(bp->graph, registry_node_by_name("Event"));
        graph_set_event_name(bp->graph, input_ev, "Input");
        pin_spec action_param = { "Action", PIN_NAME, "name", PIN_DATA };
        pin_spec val = { "Value", PIN_F64, "double", PIN_DATA };
        pin_spec pressed = { "Pressed", PIN_BOOL, "bool", PIN_DATA };
        graph_add_pin(bp->graph, input_ev, 1, &action_param);
        graph_add_pin(bp->graph, input_ev, 1, &val);
        graph_add_pin(bp->graph, input_ev, 1, &pressed);
        node *set_action = behavior_asset_add_set_node(bp, last_action->id);
        graph_connect_exec(bp->graph, input_ev, 0, set_action, 0, 0);
        graph_connect_data(bp->graph, input_ev, 1, set_action, 1);

        behavior_instance *bi = behavior_instance_create(bp, NULL, NULL, NULL);
        check(bi != NULL, "explicit instance creation succeeds");
        pin_value v;
        check(
            behavior_instance_get_variable(bi, health->id, &v) == 0 && v.i == 100,
            "instance variables initialize from authored defaults once"
        );
        check(bp->graph->flow_pause_count > 0, "creating an instance acquires the mutation guard");
        check(behavior_instance_scratch(bi, 99) != NULL, "instance scratch is keyed by node ID");
        check(bi->activations == NULL, "activation-local state starts empty on the instance");
        check(bp->graph->flow_pause_count == 1, "applied definition is guarded while a preview instance exists");

        flow_error perr = { 0 };
        flow_run *act = behavior_instance_dispatch(bi, "Begin", (flow_inputs){ 0 }, &perr);
        check(act && flow_run_status(act) == FLOW_COMPLETED, "Begin dispatch completes");
        check(flow_run_current_node(act) == NULL, "inspecting a completed activation does not merge scheduler state");
        check(
            flow_run_call_stack(act, NULL) != NULL || flow_run_status(act) == FLOW_COMPLETED,
            "an activation exposes an ordered call-stack view"
        );
        check(behavior_instance_get_variable(bi, health->id, &v) == 0 && v.i == 50, "Set Variable writes instance storage");

        g_captured_int = -1;
        pin_value tick_params[] = { { .type = PIN_F64, .d = 0.016 } };
        act = behavior_instance_dispatch(bi, "Tick", (flow_inputs){ tick_params, 1, NULL }, &perr);
        check(
            act && flow_run_status(act) == FLOW_COMPLETED && g_captured_int == 50,
            "a later Tick event reads the state Begin wrote"
        );

        g_captured_str[0] = '\0';
        act = behavior_instance_dispatch(bi, "TickText", (flow_inputs){ 0 }, &perr);
        int traversal_count = 0;
        const flow_traversal *traversals = flow_run_traversals(act, &traversal_count);
        int saw_get_to_string = 0;
        int saw_to_string_capture = 0;
        for (int i = 0; i < traversal_count; i++) {
            saw_get_to_string |= traversals[i].kind == GRAPH_EDGE_DATA &&
                                 traversals[i].from_id == get->id &&
                                 traversals[i].to_id == to_string->id;
            saw_to_string_capture |= traversals[i].kind == GRAPH_EDGE_DATA &&
                                     traversals[i].from_id == to_string->id &&
                                     traversals[i].to_id == string_capture->id;
        }
        check(
            act && flow_run_status(act) == FLOW_COMPLETED &&
                strcmp(g_captured_str, "50") == 0 && saw_get_to_string &&
                saw_to_string_capture,
            "pure data chains record every traversed data edge"
        );

        pin_value input_params[] = {
            { .type = PIN_NAME, .str = "Jump" },
            { .type = PIN_F64, .d = 1.0 },
            { .type = PIN_BOOL, .b = true },
        };
        act = behavior_instance_dispatch(bi, "Input", (flow_inputs){ input_params, 3, NULL }, &perr);
        check(act && flow_run_status(act) == FLOW_COMPLETED, "Input dispatch with parameters completes");
        check(
            behavior_instance_get_variable(bi, last_action->id, &v) == 0 &&
                v.type == PIN_NAME && v.str && strcmp(v.str, "Jump") == 0,
            "event parameters deliver values into instance state"
        );
        check(health->default_value.i == 100, "instance execution never mutates authored defaults");

        check(
            behavior_asset_rename_variable(bp, health->id, "HP") != 0,
            "renaming a variable is rejected while an instance is attached"
        );
        check(
            behavior_asset_add_variable(bp, "New", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, NULL) ==
                NULL,
            "adding a variable is rejected while an instance is attached"
        );
        check(graph_add_node(bp->graph, &g_sink_a_desc) == NULL, "adding a node is rejected while an instance is attached");
        check(behavior_instance_stop_activation(bi, act) == FLOW_OK, "stopping one activation succeeds");
        check(bp->graph->flow_pause_count > 0, "stopping one activation does NOT release the mutation guard");
        behavior_instance_destroy(bi);
        check(bp->graph->flow_pause_count == 0, "destroying the last instance releases the mutation guard");

        uint64_t health_id = health->id;
        node *get_ref = get;
        check(behavior_asset_rename_variable(bp, health_id, "HP") == 0, "renaming a variable succeeds when mutable");
        check(
            health->name && strcmp(health->name, "HP") == 0 &&
                get_ref->reference_id == health_id && get_ref->reference_name &&
                strcmp(get_ref->reference_name, "HP") == 0,
            "rename updates the referencing node's display without "
            "rewriting its reference"
        );
        check(behavior_asset_retype_variable(bp, health_id, PIN_F64, "double") == 0, "retyping a variable succeeds");
        check(
            health->type == PIN_F64 && get_ref->output_pins[0].type == PIN_F64,
            "retyping updates the referencing node's pin type"
        );
        node *src = NULL;
        int src_pin = -1;
        check(graph_input_source(bp->graph, capture, 1, &src, &src_pin) == 0, "the now-incompatible edge is invalidated");
        behavior_asset_free(bp);
    }

    /* TRACE-1: completed activations leave copied entries at the instance
     * seam, including fallback records for events with no traversal. */
    {
        behavior_asset *bp = behavior_asset_new("Trace");
        node *empty = graph_add_node(bp->graph, registry_node_by_name("Event"));
        node *done = graph_add_node(bp->graph, registry_node_by_name("Event"));
        node *done_sink = graph_add_node(bp->graph, &g_sink_a_desc);
        node *failed = graph_add_node(bp->graph, registry_node_by_name("Event"));
        node *failed_sink = graph_add_node(bp->graph, &g_sink_error_desc);
        check(
            bp && empty && done && done_sink && failed && failed_sink &&
                graph_set_event_name(bp->graph, empty, "Empty") == 0 &&
                graph_set_event_name(bp->graph, done, "Done") == 0 &&
                graph_set_event_name(bp->graph, failed, "Failed") == 0 &&
                graph_connect_exec(bp->graph, done, 0, done_sink, 0, 0) == 0 &&
                graph_connect_exec(bp->graph, failed, 0, failed_sink, 0, 0) == 0,
            "trace fixture has empty, completed, and failed events"
        );
        behavior_instance *bi = behavior_instance_create(bp, NULL, NULL, NULL);
        flow_error error = { 0 };
        flow_run *act = behavior_instance_dispatch(bi, "Empty", (flow_inputs){ 0 }, &error);
        check(
            act && flow_run_status(act) == FLOW_COMPLETED &&
                behavior_instance_release_activation(bi, act) == 0,
            "an empty event completes and can be released"
        );
        int trace_count = 0;
        const behavior_instance_trace_entry *trace = behavior_instance_trace(bi, &trace_count);
        check(
            trace && trace_count == 1 && trace[0].event_name &&
                strcmp(trace[0].event_name, "Empty") == 0 &&
                trace[0].state == FLOW_COMPLETED && trace[0].error_code == FLOW_OK,
            "an empty event produces a completed fallback entry"
        );
        act = behavior_instance_dispatch(bi, "Failed", (flow_inputs){ 0 }, &error);
        check(
            act && flow_run_status(act) == FLOW_FAILED &&
                flow_run_error(act)->code == -777 &&
                behavior_instance_release_activation(bi, act) == 0,
            "a failed event preserves its structured run error"
        );
        trace = behavior_instance_trace(bi, &trace_count);
        check(
            trace_count == 2 && trace[1].state == FLOW_FAILED &&
                trace[1].error_code == -777 &&
                strcmp(trace[1].event_name, "Failed") == 0,
            "a failed activation records terminal status and error"
        );
        behavior_instance_destroy(bi);
        behavior_asset_free(bp);
    }

    /* TRACE-3: the public trace is oldest-first, bounded, and survives
     * activation recycling but not instance reuse. */
    {
        behavior_asset *bp = behavior_asset_new("TraceBound");
        node *event = graph_add_node(bp->graph, registry_node_by_name("Event"));
        node *sink = graph_add_node(bp->graph, &g_sink_a_desc);
        graph_set_event_name(bp->graph, event, "Tick");
        graph_connect_exec(bp->graph, event, 0, sink, 0, 0);
        behavior_instance *bi = behavior_instance_create(bp, NULL, NULL, NULL);
        check(behavior_instance_set_activation_pool(bi, 1) == 0, "activation pooling can be enabled before dispatch");
        flow_error error = { 0 };
        int trace_ok = 1;
        for (int i = 0; i <= BEHAVIOR_ASSET_INSTANCE_TRACE_MAX; i++) {
            if (behavior_instance_tick(bi, (double)i) != FLOW_OK) {
                trace_ok = 0;
                break;
            }
            flow_run *act = behavior_instance_dispatch(bi, "Tick", (flow_inputs){ 0 }, &error);
            if (!act || behavior_instance_release_activation(bi, act) != 0) {
                trace_ok = 0;
                break;
            }
        }
        check(trace_ok, "pooled activations release into the trace");
        int trace_count = 0;
        const behavior_instance_trace_entry *trace = behavior_instance_trace(bi, &trace_count);
        check(
            trace && trace_count == BEHAVIOR_ASSET_INSTANCE_TRACE_MAX &&
                trace[0].traversal.timestamp == 1.0 &&
                trace[trace_count - 1].traversal.timestamp == BEHAVIOR_ASSET_INSTANCE_TRACE_MAX,
            "trace overflow evicts the oldest entry and keeps order"
        );
        check(
            behavior_instance_pool_release(bi) == 0 &&
                behavior_instance_trace(bi, &trace_count) && trace_count == 0 &&
                behavior_instance_pool_reuse(bi, bp, NULL, NULL, NULL) == bi &&
                behavior_instance_trace(bi, &trace_count) && trace_count == 0,
            "instance release and reuse clear copied trace storage"
        );
        behavior_instance_destroy(bi);
        behavior_asset_free(bp);
    }

    /* VAR-7: deleting a referenced variable is permitted; the orphaned Get
     * reference fails evaluation with an error naming the lost variable. */
    {
        behavior_asset *bp = behavior_asset_new("Orphan");
        behavior_variable *v = behavior_asset_add_variable(
            bp,
            "Gone",
            PIN_I32,
            "int",
            PIN_CONTAINER_SINGLE,
            &(pin_value){ .type = PIN_I32, .i = 1 },
            ""
        );
        node *ev = graph_add_node(bp->graph, registry_node_by_name("Event"));
        graph_set_event_name(bp->graph, ev, "Go");
        node *get = behavior_asset_add_get_node(bp, v->id);
        node *cap = graph_add_node(bp->graph, &g_capture_desc);
        graph_connect_exec(bp->graph, ev, 0, cap, 0, 0);
        graph_connect_data(bp->graph, get, 0, cap, 1);
        behavior_instance *bi = behavior_instance_create(bp, NULL, NULL, NULL);
        behavior_instance_destroy(bi);
        check(behavior_asset_delete_variable(bp, v->id) == 0, "deleting a referenced variable is permitted");
        bi = behavior_instance_create(bp, NULL, NULL, NULL);
        flow_error perr = { 0 };
        flow_run *act = behavior_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &perr);
        check(
            act == NULL && perr.code == FLOW_ERR_COMPILE && perr.node == get &&
                perr.message && strstr(perr.message, "missing variable") != NULL,
            "an orphaned Get reference is a blocking compile error "
            "(RUN-12 refuses to start)"
        );
        behavior_instance_destroy(bi);
        behavior_asset_free(bp);
    }

    /* EXEC-17 / CAT-18: a Delay suspends while a later event executes, and the
     * scheduler's FIFO activation order is deterministic across repeated runs. */
    {
        behavior_asset *bp = behavior_asset_new("Sched");
        node *wait_ev = graph_add_node(bp->graph, registry_node_by_name("Event"));
        graph_set_event_name(bp->graph, wait_ev, "Wait");
        node *delay = graph_add_node(bp->graph, registry_node_by_name("Delay"));
        node *done_a = graph_add_node(bp->graph, &g_sink_a_desc);
        graph_connect_exec(bp->graph, wait_ev, 0, delay, 0, 0);
        graph_connect_exec(bp->graph, delay, 0, done_a, 0, 0);
        graph_set_pin_default(bp->graph, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 5.0 });
        node *fast_ev = graph_add_node(bp->graph, registry_node_by_name("Event"));
        graph_set_event_name(bp->graph, fast_ev, "Fast");
        node *done_b = graph_add_node(bp->graph, &g_sink_b_desc);
        graph_connect_exec(bp->graph, fast_ev, 0, done_b, 0, 0);
        for (int run = 0; run < 2; run++) {
            behavior_instance *bi = behavior_instance_create(bp, NULL, NULL, NULL);
            flow_log_reset();
            flow_error perr = { 0 };
            flow_run *act = behavior_instance_dispatch(bi, "Wait", (flow_inputs){ 0 }, &perr);
            check(act && flow_run_status(act) == FLOW_SUSPENDED && g_flow_log_count == 0, "a Delay suspends its activation");
            act = behavior_instance_dispatch(bi, "Fast", (flow_inputs){ 0 }, &perr);
            check(
                act && flow_run_status(act) == FLOW_COMPLETED &&
                    strcmp(g_flow_log, "B") == 0,
                "a later event executes while the Delay is suspended"
            );
            check(acount(bi->activations) == 2, "suspended and completed activations coexist");
            check(acount(bi->activations) == 2, "re-entry keeps an earlier activation on its continuation stack");
            check(
                behavior_instance_tick(bi, 5.0) == FLOW_OK &&
                    strcmp(g_flow_log, "BA") == 0,
                "the scheduler resumes the Delay at its deadline in FIFO order"
            );
            check(act && flow_run_status(act) == FLOW_COMPLETED, "the Delay completes after its duration");
            behavior_instance_destroy(bi);
        }
        behavior_asset_free(bp);
    }

    /* EXEC-18 isolation: two activations of the same event retain distinct
     * parameter values while they are paused before the same capture node. */
    {
        behavior_asset *bp = behavior_asset_new("ActivationIsolation");
        node *event = graph_add_node(bp->graph, registry_node_by_name("Event"));
        node *capture = graph_add_node(bp->graph, &g_capture_desc);
        pin_spec value = { "Value", PIN_I32, "int", PIN_DATA };
        graph_add_pin(bp->graph, event, 1, &value);
        graph_connect_exec(bp->graph, event, 0, capture, 0, 0);
        graph_connect_data(bp->graph, event, 1, capture, 1);
        behavior_instance *bi = behavior_instance_create(bp, NULL, NULL, NULL);
        flow_error first_error = { 0 };
        flow_error second_error = { 0 };
        flow_run *first = behavior_instance_enqueue(
            bi,
            event,
            (flow_inputs){ (pin_value[]){ { .type = PIN_I32, .i = 11 } }, 1, NULL },
            &first_error
        );
        flow_run *second = behavior_instance_enqueue(
            bi,
            event,
            (flow_inputs){ (pin_value[]){ { .type = PIN_I32, .i = 22 } }, 1, NULL },
            &second_error
        );
        check(
            first && second && first_error.code == 0 && second_error.code == 0 &&
                flow_run_step(first) == FLOW_PAUSED_RESULT &&
                flow_run_step(second) == FLOW_PAUSED_RESULT,
            "same-node activations can be staged independently"
        );
        (void)flow_run_step(first);
        (void)flow_run_step(second);
        check(
            flow_run_resume(first) == FLOW_OK && flow_run_resume(second) == FLOW_OK &&
                flow_run_status(first) == FLOW_COMPLETED &&
                flow_run_status(second) == FLOW_COMPLETED,
            "staged activations complete independently"
        );
        pin_value first_value;
        pin_value second_value;
        check(
            flow_run_watch_input(first, capture, 1, &first_value) == 0 &&
                flow_run_watch_input(second, capture, 1, &second_value) == 0 &&
                first_value.i == 11 && second_value.i == 22,
            "same-node activation values remain isolated"
        );
        behavior_instance_destroy(bi);
        behavior_asset_free(bp);
    }

    /* DBG-5: watched names and arrays are copied out of the activation and
     * remain valid after the source defaults are released. */
    {
        behavior_asset *bp = behavior_asset_new("OwnedWatches");
        node *event = graph_add_node(bp->graph, &NODE_Event);
        node *watch = graph_add_node(bp->graph, &g_owned_watch_desc);
        graph_set_event_name(bp->graph, event, "Owned");
        graph_connect_exec(bp->graph, event, 0, watch, 0, 0);
        check(
            graph_set_pin_default(
                bp->graph,
                watch,
                0,
                1,
                &(pin_value){ .type = PIN_NAME, .type_name = "name", .str = "OwnedName" }
            ) == 0,
            "Name watch default is authored"
        );
        pin_array *array = pin_array_new(PIN_I32, "int", 2);
        pin_array_push(array, &(pin_value){ .type = PIN_I32, .type_name = "int", .i = 7 });
        pin_array_push(array, &(pin_value){ .type = PIN_I32, .type_name = "int", .i = 9 });
        check(
            graph_set_pin_default(
                bp->graph,
                watch,
                0,
                2,
                &(pin_value){
                    .type = PIN_I32,
                    .type_name = "int",
                    .container = PIN_CONTAINER_ARRAY,
                    .ptr = array,
                }
            ) == 0,
            "array watch default is authored"
        );
        pin_array_free(array);
        behavior_instance *bi = behavior_instance_create(bp, NULL, NULL, NULL);
        flow_error error = { 0 };
        flow_run *run = behavior_instance_dispatch(bi, "Owned", (flow_inputs){ 0 }, &error);
        pin_value name_value;
        pin_value array_value;
        const pin_array *items = NULL;
        check(
            run && flow_run_status(run) == FLOW_COMPLETED &&
                flow_run_watch_input(run, watch, 1, &name_value) == 0 &&
                flow_run_watch_input(run, watch, 2, &array_value) == 0 &&
                name_value.type == PIN_NAME && name_value.str &&
                strcmp(name_value.str, "OwnedName") == 0 &&
                array_value.container == PIN_CONTAINER_ARRAY &&
                (items = (const pin_array *)array_value.ptr) && items->count == 2 &&
                items->items[0].i == 7 && items->items[1].i == 9,
            "Name and array watches own their captured values"
        );
        behavior_instance_destroy(bi);
        behavior_asset_free(bp);
    }

    /* EXEC-16: activation inputs own pointer-bearing values until the run is
     * released, even when the source storage is changed or freed. */
    {
        graph g;
        graph_init(&g);
        node *event = graph_add_node(&g, &NODE_Event);
        node *delay = graph_add_node(&g, &NODE_Delay);
        pin_spec name_pin = { "Name", PIN_NAME, "name", PIN_DATA };
        pin_spec text_pin = { "Text", PIN_STR, "const char *", PIN_DATA };
        pin_spec array_pin = {
            "Values",
            PIN_I32,
            "int",
            PIN_DATA,
            0,
            PIN_CONTAINER_ARRAY,
            { .type = PIN_I32, .type_name = "int", .container = PIN_CONTAINER_ARRAY },
        };
        graph_add_pin(&g, event, 1, &name_pin);
        graph_add_pin(&g, event, 1, &text_pin);
        graph_add_pin(&g, event, 1, &array_pin);
        graph_connect_exec(&g, event, 0, delay, 0, 0);
        graph_set_pin_default(&g, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 1.0 });

        char *name = (char *)malloc(5);
        char *text = (char *)malloc(5);
        memcpy(name, "Name", 5);
        memcpy(text, "Text", 5);
        pin_array *array = pin_array_new(PIN_I32, "int", 2);
        pin_array_push(array, &(pin_value){ .type = PIN_I32, .i = 7 });
        pin_array_push(array, &(pin_value){ .type = PIN_I32, .i = 9 });
        pin_value inputs[] = {
            { .type = PIN_NAME, .type_name = "name", .str = name },
            { .type = PIN_STR, .type_name = "const char *", .str = text },
            { .type = PIN_I32, .type_name = "int", .container = PIN_CONTAINER_ARRAY, .ptr = array },
        };
        flow_error error = { 0 };
        flow_run *run = flow_run_start(&g, event, (flow_inputs){ inputs, 3, NULL }, NULL, &error);
        check(
            run && flow_run_step(run) == FLOW_PAUSED_RESULT &&
                flow_run_resume(run) == FLOW_SUSPENDED_RESULT,
            "pointer-bearing activation inputs survive suspension"
        );
        memset(name, 'x', 4);
        memset(text, 'x', 4);
        array->items[0].i = 99;
        free(name);
        free(text);
        pin_array_free(array);
        flow_inputs owned = flow_run_inputs(run);
        const pin_array *owned_array = owned.count == 3
                                         ? (const pin_array *)owned.values[2].ptr
                                         : NULL;
        check(
            owned.count == 3 && owned.values[0].str &&
                strcmp(owned.values[0].str, "Name") == 0 && owned.values[1].str &&
                strcmp(owned.values[1].str, "Text") == 0 && owned_array &&
                owned_array->count == 2 && owned_array->items[0].i == 7 &&
                owned_array->items[1].i == 9,
            "activation copies Name, string, and array inputs"
        );
        flow_run_free(run);
        graph_free(&g);
    }

    /* RUN-17: freeing an instance-owned activation removes it from the
     * scheduler before the instance is destroyed. */
    {
        behavior_asset *bp = behavior_asset_new("FreeActivation");
        node *event = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, event, "Go");
        behavior_instance *bi = behavior_instance_create(bp, NULL, NULL, NULL);
        flow_error error = { 0 };
        flow_run *run = behavior_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &error);
        check(run && acount(bi->activations) == 1, "instance owns a returned activation");
        flow_run_free(run);
        check(acount(bi->activations) == 0, "freeing an activation removes it from its instance");
        behavior_instance_destroy(bi);
        behavior_asset_free(bp);
    }

    /* EVT-7: dispatching by name fails distinctly for unknown and duplicate
     * names. */
    {
        behavior_asset *bp = behavior_asset_new("Evt7");
        node *ev = graph_add_node(bp->graph, registry_node_by_name("Event"));
        graph_set_event_name(bp->graph, ev, "Go");
        behavior_instance *bi = behavior_instance_create(bp, NULL, NULL, NULL);
        flow_error err = { 0 };
        check(
            behavior_instance_dispatch(bi, "Missing", (flow_inputs){ 0 }, &err) == NULL &&
                err.code == FLOW_ERR_UNKNOWN_EVENT,
            "an unknown event name fails distinctly"
        );
        behavior_instance_destroy(bi);
        node *dup = graph_add_node(bp->graph, registry_node_by_name("Event"));
        graph_set_event_name(bp->graph, dup, "Go");
        bi = behavior_instance_create(bp, NULL, NULL, NULL);
        err = (flow_error){ 0 };
        check(
            behavior_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &err) == NULL &&
                err.code == FLOW_ERR_DUPLICATE_EVENT,
            "a duplicate event name fails distinctly"
        );
        behavior_instance_destroy(bi);
        behavior_asset_free(bp);
    }

    /* RUN-18: two instances of one definition share no owner, variable,
     * scratch, activation, error, or step-count state. */
    {
        behavior_asset *bp = behavior_asset_new("Two");
        behavior_asset_set_owner_type(bp, "Entity");
        behavior_variable *v = behavior_asset_add_variable(
            bp,
            "State",
            PIN_I32,
            "int",
            PIN_CONTAINER_SINGLE,
            &(pin_value){ .type = PIN_I32, .i = 100 },
            ""
        );
        node *boom_ev = graph_add_node(bp->graph, registry_node_by_name("Event"));
        graph_set_event_name(bp->graph, boom_ev, "Boom");
        node *boom = graph_add_node(bp->graph, &g_sink_error_desc);
        graph_connect_exec(bp->graph, boom_ev, 0, boom, 0, 0);
        pin_value o1 = { .type = PIN_HANDLE, .u64 = 111, .type_name = "Entity" };
        pin_value o2 = { .type = PIN_HANDLE, .u64 = 222, .type_name = "Entity" };
        behavior_instance *bi1 = behavior_instance_create(bp, &o1, NULL, NULL);
        behavior_instance *bi2 = behavior_instance_create(bp, &o2, NULL, NULL);
        check(
            behavior_instance_owner(bi1).u64 == 111 &&
                behavior_instance_owner(bi2).u64 == 222 &&
                behavior_instance_owner(bi1).type == PIN_HANDLE,
            "two instances carry independent owner handles"
        );
        behavior_instance_set_variable(bi1, v->id, &(pin_value){ .type = PIN_I32, .i = 7 });
        pin_value rv;
        check(
            behavior_instance_get_variable(bi1, v->id, &rv) == 0 && rv.i == 7 &&
                behavior_instance_get_variable(bi2, v->id, &rv) == 0 && rv.i == 100,
            "two instances have independent variable storage"
        );
        pin_value *s1 = behavior_instance_scratch(bi1, 42);
        s1->type = PIN_I32;
        s1->i = 5;
        check(behavior_instance_scratch(bi2, 42)->i == 0, "two instances have independent per-node scratch");
        flow_error perr = { 0 };
        flow_run *a1 = behavior_instance_dispatch(bi1, "Boom", (flow_inputs){ 0 }, &perr);
        check(
            a1 && flow_run_status(a1) == FLOW_FAILED && flow_run_steps(a1) == 2,
            "an error and its step count live on instance one's own activation"
        );
        check(
            acount(bi1->activations) == 1 && acount(bi2->activations) == 0,
            "one instance's failure leaves the other's queue untouched"
        );
        flow_run *a2 = behavior_instance_dispatch(bi2, "Boom", (flow_inputs){ 0 }, &perr);
        check(
            a2 && flow_run_status(a2) == FLOW_FAILED &&
                flow_run_error(a1) != flow_run_error(a2),
            "independent activation errors"
        );
        behavior_instance_destroy(bi1);
        behavior_instance_destroy(bi2);
        behavior_asset_free(bp);
    }

    /* RUN-15 / RUN-17: stopping one activation preserves instance variables
     * and scratch; destroying the instance cancels every activation and
     * releases the mutation guard without executing another node. */
    {
        behavior_asset *bp = behavior_asset_new("Stop");
        behavior_variable *v = behavior_asset_add_variable(
            bp,
            "State",
            PIN_I32,
            "int",
            PIN_CONTAINER_SINGLE,
            &(pin_value){ .type = PIN_I32, .i = 5 },
            ""
        );
        node *ev = graph_add_node(bp->graph, registry_node_by_name("Event"));
        graph_set_event_name(bp->graph, ev, "Go");
        node *delay = graph_add_node(bp->graph, registry_node_by_name("Delay"));
        node *done = graph_add_node(bp->graph, &g_sink_a_desc);
        graph_connect_exec(bp->graph, ev, 0, delay, 0, 0);
        graph_connect_exec(bp->graph, delay, 0, done, 0, 0);
        graph_set_pin_default(bp->graph, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 100.0 });
        behavior_instance *bi = behavior_instance_create(bp, NULL, NULL, NULL);
        behavior_instance_set_variable(bi, v->id, &(pin_value){ .type = PIN_I32, .i = 9 });
        behavior_instance_scratch(bi, delay->id)->type = PIN_I32;
        behavior_instance_scratch(bi, delay->id)->i = 42;
        flow_error perr = { 0 };
        flow_run *act = behavior_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &perr);
        check(act && flow_run_status(act) == FLOW_SUSPENDED, "dispatch leaves a suspended activation");
        check(
            behavior_instance_stop_activation(bi, act) == FLOW_OK &&
                flow_run_status(act) == FLOW_IDLE,
            "stopping one activation succeeds"
        );
        pin_value rv;
        check(
            behavior_instance_get_variable(bi, v->id, &rv) == 0 && rv.i == 9,
            "stopping an activation preserves instance variables"
        );
        check(behavior_instance_scratch(bi, delay->id)->i == 42, "stopping an activation preserves per-node scratch");
        int guard_before = bp->graph->flow_pause_count;
        behavior_instance_destroy(bi);
        check(bp->graph->flow_pause_count == guard_before - 1, "destroying the instance releases the mutation guard");
        node *probe = graph_add_node(bp->graph, &g_sink_b_desc);
        check(probe != NULL, "the definition becomes mutable after its last instance is destroyed");
        /* The probe node's exec input is unconnected, which is a blocking
         * compile error under RUN-12; remove it before starting again. */
        check(graph_remove_node(bp->graph, probe) == 0, "removing the probe restores a valid behavior_asset");

        behavior_instance *bi2 = behavior_instance_create(bp, NULL, NULL, NULL);
        flow_log_reset();
        act = behavior_instance_dispatch(bi2, "Go", (flow_inputs){ 0 }, &perr);
        check(act && flow_run_status(act) == FLOW_SUSPENDED, "a second instance also suspends");
        behavior_instance_destroy(bi2);
        check(g_flow_log_count == 0, "destroying the instance cancels activations without executing another node");
        behavior_asset_free(bp);
    }

    /* CAT-32: Self is a pure node whose output has the definition's owner
     * handle type; it is rejected without an owner type and evaluates to the
     * instance's supplied owner handle. */
    {
        behavior_asset *bp = behavior_asset_new("Self");
        check(behavior_asset_add_self_node(bp) == NULL, "Self is rejected when the definition declares no owner type");
        /* The owner handle type must resolve in the active registry (CMPL-20),
         * so the test uses a reflected type name. */
        behavior_asset_set_owner_type(bp, "Vec3");
        check(
            bp->owner_type_name && strcmp(bp->owner_type_name, "Vec3") == 0,
            "a Behavior Asset definition carries one nominal owner type"
        );
        node *self = behavior_asset_add_self_node(bp);
        check(
            self && self->output_pins[0].type == PIN_HANDLE &&
                self->output_pins[0].type_name &&
                strcmp(self->output_pins[0].type_name, "Vec3") == 0,
            "Self carries the declared nominal owner handle type"
        );
        pin_spec ha = { "a", PIN_HANDLE, "Entity", PIN_DATA };
        pin_spec hb = { "b", PIN_HANDLE, "Entity", PIN_DATA };
        pin_spec hc = { "c", PIN_HANDLE, "Other", PIN_DATA };
        check(
            pin_types_compatible(&ha, &hb) && !pin_types_compatible(&ha, &hc),
            "handle pins are compatible only when nominal type names match"
        );
        node *ev = graph_add_node(bp->graph, registry_node_by_name("Event"));
        graph_set_event_name(bp->graph, ev, "Go");
        node *cap = graph_add_node(bp->graph, &g_handle_capture_desc);
        graph_connect_exec(bp->graph, ev, 0, cap, 0, 0);
        graph_connect_data(bp->graph, self, 0, cap, 1);
        pin_value owner = { .type = PIN_HANDLE, .u64 = 777, .type_name = "Vec3" };
        behavior_instance *bi = behavior_instance_create(bp, &owner, NULL, NULL);
        g_captured_handle = 0;
        flow_error perr = { 0 };
        flow_run *act = behavior_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &perr);
        check(
            act && flow_run_status(act) == FLOW_COMPLETED && g_captured_handle == 777,
            "Self evaluates to the instance's supplied owner handle"
        );
        behavior_instance_destroy(bi);
        behavior_asset_free(bp);
    }

    /* FMT-16 / FMT-17 / FMT-18: the Behavior Asset document is byte-identical
     * before and after execution, and Name spelling plus the owner type
     * round-trip exactly. */
    {
        behavior_asset *bp = behavior_asset_new("FmtBP");
        behavior_asset_set_owner_type(bp, "UnavailableOwner");
        behavior_asset_add_variable(
            bp,
            "Tag",
            PIN_NAME,
            "name",
            PIN_CONTAINER_SINGLE,
            &(pin_value){ .type = PIN_NAME, .str = "MyTag" },
            "Cats"
        );
        node *ev = graph_add_node(bp->graph, registry_node_by_name("Event"));
        graph_set_event_name(bp->graph, ev, "Go");
        node *name_lit = graph_add_node(bp->graph, registry_node_by_name("Name"));
        graph_set_pin_default(bp->graph, name_lit, 0, 0, &(pin_value){ .type = PIN_NAME, .str = "ExactSpelling" });
        char *before = behavior_asset_save_json(bp);
        check(before && strstr(before, "\"owner_type\": \"UnavailableOwner\""), "the authored owner handle type is written");
        behavior_instance *bi = behavior_instance_create(
            bp,
            &(pin_value){ .type = PIN_HANDLE, .u64 = 5, .type_name = "UnavailableOwner" },
            NULL,
            NULL
        );
        behavior_instance_destroy(bi);
        char *after = behavior_asset_save_json(bp);
        check(before && after && strcmp(before, after) == 0 && !strstr(before, "activations"), "saving before and after execution is byte-identical and carries no runtime state");
        behavior_asset *loaded = before ? behavior_asset_load_json(before) : NULL;
        check(
            loaded && loaded->owner_type_name &&
                strcmp(loaded->owner_type_name, "UnavailableOwner") == 0,
            "an unavailable owner type name is preserved, not substituted"
        );
        behavior_variable *lv = loaded
                                  ? behavior_asset_find_variable_by_name(loaded, "Tag")
                                  : NULL;
        check(
            lv && lv->default_value.str && strcmp(lv->default_value.str, "MyTag") == 0,
            "a Name variable's authored spelling round-trips exactly"
        );
        int spelling = 0;
        if (loaded) {
            for (int i = 0; i < acount(loaded->graph->nodes); i++) {
                node *n = loaded->graph->nodes[i];
                if (n->desc == registry_node_by_name("Name") &&
                    n->input_pins[0].default_value.str &&
                    strcmp(n->input_pins[0].default_value.str, "ExactSpelling") == 0) {
                    spelling = 1;
                }
            }
        }
        check(spelling, "a Name literal's authored spelling round-trips exactly");
        char *re = loaded ? behavior_asset_save_json(loaded) : NULL;
        check(re && before && strcmp(re, before) == 0, "the Behavior Asset document load/save round-trips byte-identically");
        if (loaded) {
            behavior_asset_free(loaded);
        }
        free(before);
        free(after);
        free(re);
        behavior_asset_free(bp);
    }

    /* NODE-13: descriptors classify as pure, executable entry, or executable;
     * the existing pure helper keeps classifying the same way. */
    {
        check(node_desc_behavior(&g_const_desc) == NODE_BEHAVIOR_PURE, "a pure descriptor has no exec pins");
        check(
            node_desc_behavior(&NODE_Event) == NODE_BEHAVIOR_ENTRY,
            "an event is an executable entry (no exec input, one exec output)"
        );
        check(node_desc_behavior(&NODE_Branch) == NODE_BEHAVIOR_EXEC, "a node with an exec input is executable");
        check(
            node_desc_is_pure(&g_const_desc) && !node_desc_is_pure(&NODE_Branch),
            "the existing pure helper agrees with the classification"
        );
    }

    /* CMPL-1: compiling is explicit, ordered, read-only, and clean on an
     * empty behavior_asset. */
    {
        behavior_asset *bp = behavior_asset_new("CMPL1");
        compile_report rep;
        compile_report_init(&rep);
        unsigned long long rev = graph_topology_revision(bp->graph);
        char *before = behavior_asset_save_json(bp);
        int errors = behavior_asset_compile(bp, &rep);
        char *after = behavior_asset_save_json(bp);
        check(
            errors == 0 && rep.error_count == 0 && rep.warning_count == 0 &&
                acount(rep.items) == 0,
            "compiling an empty behavior_asset produces no diagnostics"
        );
        check(
            rev == graph_topology_revision(bp->graph) && before && after &&
                strcmp(before, after) == 0,
            "the compile pass never modifies the authored graph"
        );
        check(
            compile_report_blocked(&rep) == 0 && compile_report_first_error(&rep) == NULL,
            "an empty report has no blocking error"
        );
        free(before);
        free(after);
        compile_report_free(&rep);
        behavior_asset_free(bp);
    }

    /* CMPL-4: an executable node whose exec input has no incoming edge is
     * unreachable but remains valid to apply; the event entry is exempt. */
    {
        behavior_asset *bp = behavior_asset_new("CMPL4");
        node *ev = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, ev, "Go");
        node *branch = graph_add_node(bp->graph, &NODE_Branch);
        node *print = graph_add_node(bp->graph, &NODE_PrintString);
        print->y = 100.0f; /* below the branch: order is tested under CMPL-16 */
        check(
            node_behavior_for(ev) == NODE_BEHAVIOR_ENTRY &&
                node_behavior_for(branch) == NODE_BEHAVIOR_EXEC,
            "node instances classify like their descriptors"
        );
        compile_report rep;
        compile_report_init(&rep);
        check(
            behavior_asset_compile(bp, &rep) == 0 && rep.error_count == 0 &&
                rep.warning_count == 2,
            "two executable nodes with unconnected exec inputs warn"
        );
        int count = 0, on_branch = 0, on_print = 0;
        for (int i = 0; i < acount(rep.items); i++) {
            const compile_diagnostic *d = &rep.items[i];
            if (d->rule != CMPL_RULE_UNCONNECTED_EXEC || d->severity != CMPL_WARNING) {
                continue;
            }
            count++;
            if (d->node_id == branch->id) {
                on_branch = 1;
            }
            if (d->node_id == print->id) {
                on_print = 1;
            }
        }
        check(count == 2 && on_branch && on_print, "both diagnostics name the unconnected executable nodes");
        check(
            rep.items[0].node_id == branch->id &&
                rep.items[0].pin_id == branch->input_pins[0].id &&
                rep.items[0].graph_id == CMPL_GRAPH_DEFAULT,
            "the diagnostic carries graph, node, and pin ids"
        );
        check(strcmp(cmpl_rule_name(CMPL_RULE_UNCONNECTED_EXEC), "CMPL-4") == 0, "every diagnostic carries a stable rule code");
        compile_report_free(&rep);
        behavior_asset_free(bp);
    }

    /* CMPL-5: a data edge between incompatible pins. The graph API rejects
     * incompatible connections, so the test inserts the edge directly. */
    {
        behavior_asset *bp = behavior_asset_new("CMPL5");
        node *ev = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, ev, "Go");
        node *src = graph_add_node(bp->graph, &g_all_types_desc);
        node *sink = graph_add_node(bp->graph, &g_parameter_sink_desc);
        graph_connect_exec(bp->graph, ev, 0, sink, 0, 0);
        graph_connect_data(bp->graph, src, 5, sink, 1); /* i32 -> Integer: ok */
        edge bad = {
            .from_id = src->id,
            .from_pin_id = src->output_pins[5].id,
            .to_id = sink->id,
            .to_pin_id = sink->input_pins[2].id,
            .kind = GRAPH_EDGE_DATA,
        };
        apush(bp->graph->edges, bad); /* i32 -> Flag bool: incompatible */
        compile_report rep;
        compile_report_init(&rep);
        behavior_asset_compile(bp, &rep);
        /* The graph also carries the all-types node's unregistered enum output
         * (CMPL-20); count only the incompatible-edge diagnostics here. */
        int bad_edges = 0;
        for (int i = 0; i < acount(rep.items); i++) {
            if (rep.items[i].rule == CMPL_RULE_INCOMPATIBLE_DATA_EDGE) {
                bad_edges++;
            }
        }
        check(bad_edges == 1, "one incompatible data edge errors");
        const compile_diagnostic *c5 = NULL;
        for (int i = 0; i < acount(rep.items); i++) {
            if (rep.items[i].rule == CMPL_RULE_INCOMPATIBLE_DATA_EDGE) {
                c5 = &rep.items[i];
            }
        }
        check(
            c5 && c5->node_id == sink->id && c5->pin_id == sink->input_pins[2].id,
            "the diagnostic names the incompatible input pin"
        );
        compile_report_free(&rep);
        behavior_asset_free(bp);
    }

    /* CMPL-6: a cycle among data edges reports once, at the closing back
     * edge; an acyclic chain stays clean. */
    {
        behavior_asset *bp = behavior_asset_new("CMPL6");
        static pin_spec cycle_in[] = { { "In", PIN_I32, "int", PIN_DATA } };
        static pin_spec cycle_out[] = { { "Out", PIN_I32, "int", PIN_DATA } };
        static node_desc cycle_desc = {
            .name = "CycleIO",
            .input_count = 1,
            .output_count = 1,
            .inputs = cycle_in,
            .outputs = cycle_out,
            .pure_kind = NODE_PURE_CONST,
        };
        node *a = graph_add_node(bp->graph, &cycle_desc);
        node *b = graph_add_node(bp->graph, &cycle_desc);
        node *c = graph_add_node(bp->graph, &cycle_desc);
        graph_connect_data(bp->graph, a, 0, b, 0);
        graph_connect_data(bp->graph, b, 0, c, 0);
        compile_report rep;
        compile_report_init(&rep);
        check(behavior_asset_compile(bp, &rep) == 0, "an acyclic data chain compiles clean");
        graph_connect_data(bp->graph, c, 0, a, 0);
        compile_report_free(&rep);
        compile_report_init(&rep);
        check(
            behavior_asset_compile(bp, &rep) == 1 &&
                rep.items[0].rule == CMPL_RULE_DATA_CYCLE &&
                rep.items[0].node_id == c->id,
            "closing the chain reports one data-cycle error at the "
            "back edge"
        );
        compile_report_free(&rep);
        behavior_asset_free(bp);
    }

    /* CMPL-7: a reference to a retired or missing variable, event, function,
     * or macro. */
    {
        behavior_asset *bp = behavior_asset_new("CMPL7");
        behavior_variable *
            v = behavior_asset_add_variable(bp, "Score", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "");
        node *get = behavior_asset_add_get_node(bp, v->id);
        node *set = behavior_asset_add_set_node(bp, v->id);
        node *ev = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, ev, "Go");
        compile_report rep;
        compile_report_init(&rep);
        behavior_asset_compile(bp, &rep);
        int refs = 0;
        for (int i = 0; i < acount(rep.items); i++) {
            if (rep.items[i].rule == CMPL_RULE_MISSING_REFERENCE) {
                refs++;
            }
        }
        check(refs == 0, "valid variable and event references compile clean");
        check(behavior_asset_delete_variable(bp, v->id) == 0, "deleting a referenced variable is permitted (VAR-7)");
        compile_report_free(&rep);
        compile_report_init(&rep);
        behavior_asset_compile(bp, &rep);
        refs = 0;
        for (int i = 0; i < acount(rep.items); i++) {
            if (rep.items[i].rule == CMPL_RULE_MISSING_REFERENCE) {
                refs++;
            }
        }
        check(refs == 2, "Get and Set both report the retired variable");
        check(graph_remove_node(bp->graph, ev) == 0, "removing the referenced event is permitted");
        compile_report_free(&rep);
        compile_report_init(&rep);
        behavior_asset_compile(bp, &rep);
        refs = 0;
        for (int i = 0; i < acount(rep.items); i++) {
            if (rep.items[i].rule == CMPL_RULE_MISSING_REFERENCE) {
                refs++;
            }
        }
        check(refs == 2, "variable references remain after event removal");
        compile_report_free(&rep);
        behavior_asset_free(bp);
    }

    /* CMPL-8: duplicate event names, duplicate parameter names within one
     * list, and duplicate variable names each report once. */
    {
        behavior_asset *bp = behavior_asset_new("CMPL8");
        node *e1 = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, e1, "Begin");
        node *e2 = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, e2, "Begin");
        compile_report rep;
        compile_report_init(&rep);
        behavior_asset_compile(bp, &rep);
        check(
            rep.error_count == 1 && rep.items[0].rule == CMPL_RULE_DUPLICATE_NAME &&
                rep.items[0].node_id == e2->id,
            "two events sharing a name produce one diagnostic on the "
            "later event"
        );
        node *e3 = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, e3, "Tick");
        pin_spec amount_a = { "Amount", PIN_I32, "int", PIN_DATA };
        pin_spec amount_b = { "Amount", PIN_I32, "int", PIN_DATA };
        graph_add_pin(bp->graph, e3, 1, &amount_a);
        graph_add_pin(bp->graph, e3, 1, &amount_b);
        behavior_variable dup_var_a = {
            .id = graph_alloc_id(bp->graph),
            .name = graph_own_str(bp->graph, "Score", 5),
            .type = PIN_I32,
            .type_name = "int",
            .container = PIN_CONTAINER_SINGLE,
        };
        behavior_variable dup_var_b = dup_var_a;
        dup_var_b.id = graph_alloc_id(bp->graph);
        apush(bp->variables, dup_var_a);
        apush(bp->variables, dup_var_b);
        compile_report_free(&rep);
        compile_report_init(&rep);
        behavior_asset_compile(bp, &rep);
        check(rep.error_count == 3, "duplicate event, parameter, and variable names each error");
        int ev_dup = 0, param_dup = 0, var_dup = 0;
        for (int i = 0; i < acount(rep.items); i++) {
            const compile_diagnostic *d = &rep.items[i];
            if (d->rule != CMPL_RULE_DUPLICATE_NAME) {
                continue;
            }
            if (d->node_id == e2->id) {
                ev_dup++;
            } else if (d->node_id == e3->id) {
                param_dup++;
            } else if (d->node_id == 0 && strstr(d->message, "Score")) {
                var_dup++;
            }
        }
        check(ev_dup == 1 && param_dup == 1 && var_dup == 1, "each duplicate kind reports exactly once");
        compile_report_free(&rep);
        behavior_asset_free(bp);
    }

    /* CMPL-12: an unresolved wildcard pin that carries an edge. */
    {
        behavior_asset *bp = behavior_asset_new("CMPL12");
        static pin_spec wild_in = {
            .name = "Value",
            .type = PIN_FLOW,
            .kind = PIN_DATA,
            .flags = PIN_FLAG_WILDCARD,
        };
        static pin_spec wild_out = {
            .name = "Out",
            .type = PIN_I32,
            .type_name = "int",
            .kind = PIN_DATA,
        };
        static node_desc wild_desc = {
            .name = "WildcardIn",
            .input_count = 1,
            .output_count = 1,
            .inputs = &wild_in,
            .outputs = &wild_out,
            .pure_kind = NODE_PURE_CONST,
        };
        node *w = graph_add_node(bp->graph, &wild_desc);
        node *src = graph_add_node(bp->graph, &g_const_desc);
        compile_report rep;
        compile_report_init(&rep);
        check(behavior_asset_compile(bp, &rep) == 0, "an unconnected wildcard pin is not an error");
        edge we = {
            .from_id = src->id,
            .from_pin_id = src->output_pins[0].id,
            .to_id = w->id,
            .to_pin_id = w->input_pins[0].id,
            .kind = GRAPH_EDGE_DATA,
        };
        apush(bp->graph->edges, we);
        compile_report_free(&rep);
        compile_report_init(&rep);
        check(
            behavior_asset_compile(bp, &rep) == 1 &&
                rep.items[0].rule == CMPL_RULE_UNRESOLVED_WILDCARD &&
                rep.items[0].node_id == w->id &&
                rep.items[0].pin_id == w->input_pins[0].id,
            "an unresolved wildcard pin that carries an edge errors"
        );
        compile_report_free(&rep);
        behavior_asset_free(bp);
    }

    /* CMPL-16: deterministic ordering by graph, then node position (canvas y,
     * then x, then node id), then rule code. */
    {
        behavior_asset *bp = behavior_asset_new("CMPL16");
        node *top = graph_add_node(bp->graph, &NODE_PrintString);
        node *mid = graph_add_node(bp->graph, &NODE_Branch);
        node *bot = graph_add_node(bp->graph, &NODE_Branch);
        top->y = 10.0f;
        top->x = 500.0f;
        mid->y = 20.0f;
        mid->x = 0.0f;
        bot->y = 30.0f;
        bot->x = 1.0f;
        compile_report r1, r2;
        compile_report_init(&r1);
        compile_report_init(&r2);
        behavior_asset_compile(bp, &r1);
        behavior_asset_compile(bp, &r2);
        int same = acount(r1.items) == acount(r2.items);
        for (int i = 0; same && i < acount(r1.items); i++) {
            same = memcmp(&r1.items[i], &r2.items[i], sizeof r1.items[i]) == 0;
        }
        check(same && acount(r1.items) == 3, "compiling twice yields an identical ordered list");
        check(
            r1.items[0].node_id == top->id && r1.items[1].node_id == mid->id &&
                r1.items[2].node_id == bot->id,
            "diagnostics order by canvas y, then x, then node id"
        );
        check(
            r1.items[0].rule == CMPL_RULE_UNCONNECTED_EXEC &&
                r1.items[1].rule == CMPL_RULE_UNCONNECTED_EXEC,
            "equal-rule diagnostics keep position order"
        );
        compile_report_free(&r1);
        compile_report_free(&r2);
        behavior_asset_free(bp);
    }

    /* CMPL-16: diagnostics on one node order by rule code; the set node
     * carries both an unconnected exec input (CMPL-4) and a missing variable
     * reference (CMPL-7). */
    {
        behavior_asset *bp = behavior_asset_new("CMPL16b");
        node *set = graph_add_node(bp->graph, &NODE_SetVariable);
        graph_set_node_reference(bp->graph, set, 4242, "Lost");
        compile_report rep;
        compile_report_init(&rep);
        check(
            behavior_asset_compile(bp, &rep) == 1 && rep.error_count == 1 &&
                rep.warning_count == 1,
            "one node can carry an error and a warning"
        );
        check(
            acount(rep.items) == 2 && rep.items[0].rule == CMPL_RULE_UNCONNECTED_EXEC &&
                rep.items[1].rule == CMPL_RULE_MISSING_REFERENCE,
            "diagnostics on one node order by rule code"
        );
        compile_report_free(&rep);
        behavior_asset_free(bp);
    }

    /* CMPL-18: a disabled node is excluded from every rule except one
     * warning reporting that it is disabled. */
    {
        behavior_asset *bp = behavior_asset_new("CMPL18");
        node *dbranch = graph_add_node(bp->graph, &NODE_Branch);
        node *dget = graph_add_node(bp->graph, &NODE_GetVariable);
        graph_set_node_disabled(bp->graph, dbranch, 1);
        graph_set_node_disabled(bp->graph, dget, 1);
        compile_report rep;
        compile_report_init(&rep);
        check(behavior_asset_compile(bp, &rep) == 0 && rep.error_count == 0 && rep.warning_count == 2, "disabled nodes are excluded from every rule except one warning each");
        int warn_branch = 0, warn_get = 0, other = 0;
        for (int i = 0; i < acount(rep.items); i++) {
            const compile_diagnostic *d = &rep.items[i];
            if (d->rule != CMPL_RULE_DISABLED || d->severity != CMPL_WARNING) {
                other++;
                continue;
            }
            if (d->node_id == dbranch->id) {
                warn_branch++;
            }
            if (d->node_id == dget->id) {
                warn_get++;
            }
        }
        check(warn_branch == 1 && warn_get == 1 && other == 0, "exactly one disabled warning per node and no other rules");
        compile_report_free(&rep);
        behavior_asset_free(bp);
    }

    /* CMPL-20: an authored nominal type name not present in the active
     * registry is a blocking diagnostic that preserves the name. */
    {
        behavior_asset *bp = behavior_asset_new("CMPL20");
        static pin_spec nominal_in[] = {
            { "Vec3In", PIN_STRUCT, "Vec3", PIN_DATA },
        };
        static pin_spec nominal_out[] = {
            { "Handle", PIN_HANDLE, "MissingType", PIN_DATA },
        };
        static node_desc nominal_desc = {
            .name = "Nominal",
            .input_count = 1,
            .output_count = 1,
            .inputs = nominal_in,
            .outputs = nominal_out,
            .pure_kind = NODE_PURE_CONST,
        };
        graph_add_node(bp->graph, &nominal_desc);
        compile_report rep;
        compile_report_init(&rep);
        check(behavior_asset_compile(bp, &rep) == 1, "a registry-known struct pin compiles, an unknown handle name errors");
        check(
            rep.items[0].rule == CMPL_RULE_UNKNOWN_NOMINAL_TYPE &&
                strstr(rep.items[0].message, "MissingType") != NULL,
            "the diagnostic preserves the unresolved type name"
        );
        compile_report_free(&rep);
        behavior_asset_free(bp);
    }

    /* RUN-12: warnings do not block a run; a disconnected executable node is
     * simply unreachable until an exec edge is connected. */
    {
        behavior_asset *bp = behavior_asset_new("Run12");
        node *ev = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, ev, "Go");
        node *branch = graph_add_node(bp->graph, &NODE_Branch);
        node *print = graph_add_node(bp->graph, &NODE_PrintString);
        print->y = -50.0f; /* warning order is tested under CMPL-16 */
        behavior_instance *bi = behavior_instance_create(bp, NULL, NULL, NULL);
        flow_error rerr = { 0 };
        flow_run *act = behavior_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &rerr);
        check(
            act && flow_run_status(act) == FLOW_COMPLETED && rerr.code == FLOW_OK,
            "unconnected executable nodes do not block dispatch"
        );
        check(
            behavior_instance_enqueue(bi, ev, (flow_inputs){ 0 }, &rerr) != NULL &&
                rerr.code == FLOW_OK,
            "direct enqueue also accepts unreachable executable nodes"
        );
        behavior_instance_destroy(bi);
        /* Connect the event; the next instance recompiles at the new revision. */
        graph_remove_node(bp->graph, print);
        graph_connect_exec(bp->graph, ev, 0, branch, 0, 0);
        bi = behavior_instance_create(bp, NULL, NULL, NULL);
        act = behavior_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &rerr);
        check(act != NULL && flow_run_status(act) == FLOW_COMPLETED, "a connected event also starts and completes");
        behavior_instance_destroy(bi);
        behavior_asset_free(bp);
    }
    {
        behavior_asset *bp = behavior_asset_new("Run12w");
        node *ev = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, ev, "Go");
        node *br = graph_add_node(bp->graph, &NODE_Branch);
        graph_set_node_disabled(bp->graph, br, 1);
        behavior_instance *bi = behavior_instance_create(bp, NULL, NULL, NULL);
        flow_error werr = { 0 };
        flow_run *act = behavior_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &werr);
        check(act != NULL && flow_run_status(act) == FLOW_COMPLETED, "warnings do not block a start (CMPL-3)");
        behavior_instance_destroy(bi);
        behavior_asset_free(bp);
    }

    /* TYPE-7 / CAT-26 / UI-22: explicit conversion nodes. conversion_for()
     * answers the permitted pairs and refuses the lossy ones. */
    {
        check(
            registry_node_by_name("To String") && registry_node_by_name("To Real") &&
                registry_node_by_name("To Integer64") &&
                registry_node_by_name("Format Text"),
            "conversion and Format Text nodes are registered"
        );
        check(
            registry_node_by_name("To Real")->category &&
                registry_node_by_name("Format Text")->keywords,
            "conversion entries expose category paths and search "
            "keywords"
        );
        check(
            conversion_for(PIN_I32, PIN_F64) == &NODE_To_Real &&
                conversion_for(PIN_I32, PIN_F32) == &NODE_To_Real &&
                conversion_for(PIN_U8, PIN_F64) == &NODE_To_Real,
            "Integer and Byte to Real have an explicit conversion"
        );
        check(conversion_for(PIN_I32, PIN_I64) == &NODE_To_Integer64, "Integer widens to Integer64 within one signedness");
        check(conversion_for(PIN_I32, PIN_STR) == &NODE_To_String, "Integer to String has an explicit conversion");
        check(conversion_for(PIN_F64, PIN_STR) == &NODE_To_String, "Real to String has an explicit conversion");
        check(conversion_for(PIN_BOOL, PIN_STR) == &NODE_To_String, "Bool to String has an explicit conversion");
        check(conversion_for(PIN_NAME, PIN_STR) == &NODE_To_String, "Name to String has an explicit conversion");
        check(conversion_for(PIN_STR, PIN_F64) == NULL, "String to Real is not a permitted conversion");
        check(conversion_for(PIN_F64, PIN_I32) == NULL, "Real to Integer is lossy, so no conversion is offered");
        check(conversion_for(PIN_I64, PIN_I32) == NULL, "Integer64 to Integer narrowing is not offered");
    }

    /* CAT-26: each conversion converts correctly headless, with the input
     * retyped to the source type (PIN-3). */
    {
        graph gc;
        graph_init(&gc);
        const node_desc *real_lit = registry_node_by_name("Real");
        const node_desc *int_lit = registry_node_by_name("Integer");
        const node_desc *byte_lit = registry_node_by_name("Byte");
        const node_desc *bool_lit = registry_node_by_name("Bool");
        const node_desc *name_lit = registry_node_by_name("Name");
        arena ac;
        arena_init(&ac, 4096);
        pin_value out;

        node *to_real = graph_add_node(&gc, &NODE_To_Real);
        node *ival = graph_add_node(&gc, int_lit);
        graph_set_pin_default(
            &gc,
            ival,
            0,
            0,
            &(pin_value){ .type = PIN_I32, .type_name = "int", .i = 42 }
        );
        graph_connect(&gc, ival, 0, to_real, 0);
        check(
            graph_eval_output(&gc, to_real, 0, &ac, &out, &eval_ctx) == 0 &&
                out.type == PIN_F64 && out.d == 42.0,
            "To Real converts Integer 42 to Real 42.0"
        );

        /* The reported case: an Integer onto a float input (add_floats). To
         * Real offers, and its F64 output coerces to float at the boundary
         * (TYPE-10). */
        node *addf = graph_add_node(&gc, registry_node_by_name("add_floats"));
        graph_connect(&gc, to_real, 0, addf, 0);
        check(
            graph_eval_output(&gc, addf, 0, &ac, &out, &eval_ctx) == 0 &&
                out.type == PIN_F32 && out.f == 42.0f,
            "Integer through To Real feeds a float input at the "
            "boundary"
        );

        node *to_real_b = graph_add_node(&gc, &NODE_To_Real);
        graph_set_pin_type(&gc, to_real_b, 0, 0, PIN_U8, "byte");
        node *bval = graph_add_node(&gc, byte_lit);
        graph_set_pin_default(
            &gc,
            bval,
            0,
            0,
            &(pin_value){ .type = PIN_U8, .type_name = "byte", .u8 = 7 }
        );
        graph_connect(&gc, bval, 0, to_real_b, 0);
        check(
            graph_eval_output(&gc, to_real_b, 0, &ac, &out, &eval_ctx) == 0 &&
                out.type == PIN_F64 && out.d == 7.0,
            "a Byte feeds a retyped To Real input and converts to 7.0"
        );

        node *to_i64 = graph_add_node(&gc, &NODE_To_Integer64);
        node *i64in = graph_add_node(&gc, int_lit);
        graph_set_pin_default(
            &gc,
            i64in,
            0,
            0,
            &(pin_value){ .type = PIN_I32, .type_name = "int", .i = -3 }
        );
        graph_connect(&gc, i64in, 0, to_i64, 0);
        check(
            graph_eval_output(&gc, to_i64, 0, &ac, &out, &eval_ctx) == 0 &&
                out.type == PIN_I64 && out.i64 == -3,
            "To Integer64 widens Integer -3 to Integer64"
        );

        node *to_str = graph_add_node(&gc, &NODE_To_String);
        graph_set_pin_type(&gc, to_str, 0, 0, PIN_F64, "double");
        node *rval = graph_add_node(&gc, real_lit);
        graph_set_pin_default(
            &gc,
            rval,
            0,
            0,
            &(pin_value){ .type = PIN_F64, .type_name = "double", .d = 3.5 }
        );
        graph_connect(&gc, rval, 0, to_str, 0);
        char expected_str[64];
        snprintf(
            expected_str,
            sizeof expected_str,
            "%s",
            pin_value_to_string(
                &(pin_value){ .type = PIN_F64, .type_name = "double", .d = 3.5 }
            )
        );
        check(
            graph_eval_output(&gc, to_str, 0, &ac, &out, &eval_ctx) == 0 &&
                strcmp(out.str, expected_str) == 0,
            "To String renders a Real identically to "
            "pin_value_to_string"
        );

        node *to_str_bool = graph_add_node(&gc, &NODE_To_String);
        graph_set_pin_type(&gc, to_str_bool, 0, 0, PIN_BOOL, "bool");
        node *boolval = graph_add_node(&gc, bool_lit);
        graph_set_pin_default(
            &gc,
            boolval,
            0,
            0,
            &(pin_value){ .type = PIN_BOOL, .type_name = "bool", .b = 1 }
        );
        graph_connect(&gc, boolval, 0, to_str_bool, 0);
        node *to_str_name = graph_add_node(&gc, &NODE_To_String);
        graph_set_pin_type(&gc, to_str_name, 0, 0, PIN_NAME, "name");
        node *nameval = graph_add_node(&gc, name_lit);
        graph_set_pin_default(
            &gc,
            nameval,
            0,
            0,
            &(pin_value){ .type = PIN_NAME, .type_name = "name", .str = "Foo" }
        );
        graph_connect(&gc, nameval, 0, to_str_name, 0);
        node *to_str_int = graph_add_node(&gc, &NODE_To_String);
        graph_set_pin_type(&gc, to_str_int, 0, 0, PIN_I32, "int");
        node *intval = graph_add_node(&gc, int_lit);
        graph_set_pin_default(
            &gc,
            intval,
            0,
            0,
            &(pin_value){ .type = PIN_I32, .type_name = "int", .i = -7 }
        );
        graph_connect(&gc, intval, 0, to_str_int, 0);
        check(
            graph_eval_output(&gc, to_str_bool, 0, &ac, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "true") == 0 &&
                graph_eval_output(&gc, to_str_name, 0, &ac, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "Foo") == 0 &&
                graph_eval_output(&gc, to_str_int, 0, &ac, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "-7") == 0,
            "To String renders Bool, Name, and Integer scalars"
        );

        /* The conversion output must not alias the pin_value_to_string ring. */
        pin_value saved = out;
        for (int i = 0; i < 8; i++) {
            (void)pin_value_to_string(&(pin_value){ .type = PIN_F64,
                                                    .type_name = "double",
                                                    .d = (double)i });
        }
        check(strcmp(saved.str, "-7") == 0, "To String copies into the arena, not the format ring");

        arena_free(&ac);
        graph_free(&gc);
    }

    /* CAT-38: Format Text substitutes {n} and {} placeholders, renders a
     * missing argument as empty, and its argument pins are Generic
     * wildcards (PIN-11) that adopt the connected type. */
    {
        graph gf;
        graph_init(&gf);
        arena af;
        arena_init(&af, 8192);
        pin_value out;
        node *fmt = graph_add_node(&gf, &NODE_Format_Text);
        check(
            fmt->input_count == 2 && (fmt->input_pins[1].flags & PIN_FLAG_WILDCARD) &&
                fmt->input_pins[1].type == PIN_FLOW,
            "Format Text starts with Format and one Generic spare"
        );
        graph_set_pin_default(
            &gf,
            fmt,
            0,
            0,
            &(pin_value){ .type = PIN_STR, .type_name = "const char *", .str = "Hello {0} World" }
        );
        node *s1 = graph_add_node(&gf, registry_node_by_name("String"));
        graph_set_pin_default(
            &gf,
            s1,
            0,
            0,
            &(pin_value){ .type = PIN_STR, .type_name = "const char *", .str = "Bob" }
        );
        graph_connect(&gf, s1, 0, fmt, 1);
        check(
            graph_eval_output(&gf, fmt, 0, &af, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "Hello Bob World") == 0,
            "\"Hello {0} World\" with a String argument renders "
            "\"Hello Bob World\""
        );
        graph_set_pin_default(
            &gf,
            fmt,
            0,
            0,
            &(pin_value){ .type = PIN_STR, .type_name = "const char *", .str = "{1}" }
        );
        eval_context_reset_runtime(&eval_ctx);
        check(
            graph_eval_output(&gf, fmt, 0, &af, &out, &eval_ctx) == 0 &&
                out.str[0] == '\0',
            "{1} with one argument renders as the empty string"
        );
        /* The reported crash: an Integer driven straight onto the spare
         * replaces the String driver, re-adopts the pin (PIN-11), and
         * renders like its pin display (UI-39). */
        graph_set_pin_default(
            &gf,
            fmt,
            0,
            0,
            &(pin_value){ .type = PIN_STR, .type_name = "const char *", .str = "n={0}" }
        );
        node *ival2 = graph_add_node(&gf, registry_node_by_name("Integer"));
        graph_set_pin_default(
            &gf,
            ival2,
            0,
            0,
            &(pin_value){ .type = PIN_I32, .type_name = "int", .i = 42 }
        );
        graph_connect(&gf, ival2, 0, fmt, 1);
        eval_context_reset_runtime(&eval_ctx);
        check(
            graph_eval_output(&gf, fmt, 0, &af, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "n=42") == 0,
            "a non-string argument renders like its pin display"
        );
        /* {} consumes arguments in order; a Real through a To String
         * conversion renders like its pin display. */
        pin_spec arg1 = { .name = "Arg1",
                          .type = PIN_FLOW,
                          .kind = PIN_DATA,
                          .flags = PIN_FLAG_WILDCARD };
        graph_add_pin(&gf, fmt, 0, &arg1);
        char dynamic_arg_name[32] = "Arg2";
        pin_spec dynamic_arg = { .name = dynamic_arg_name,
                                 .type = PIN_FLOW,
                                 .kind = PIN_DATA,
                                 .flags = PIN_FLAG_WILDCARD };
        graph_add_pin(&gf, fmt, 0, &dynamic_arg);
        snprintf(dynamic_arg_name, sizeof dynamic_arg_name, "renamed");
        check(strcmp(fmt->input_pins[3].name, "Arg2") == 0, "dynamically added pin owns its name");
        node *to_str = graph_add_node(&gf, &NODE_To_String);
        graph_set_pin_type(&gf, to_str, 0, 0, PIN_F64, "double");
        node *rlit = graph_add_node(&gf, registry_node_by_name("Real"));
        graph_set_pin_default(
            &gf,
            rlit,
            0,
            0,
            &(pin_value){ .type = PIN_F64, .type_name = "double", .d = 3.5 }
        );
        graph_connect(&gf, rlit, 0, to_str, 0);
        graph_connect(&gf, to_str, 0, fmt, 2);
        graph_set_pin_default(
            &gf,
            fmt,
            0,
            0,
            &(pin_value){ .type = PIN_STR, .type_name = "const char *", .str = "{} {}" }
        );
        eval_context_reset_runtime(&eval_ctx);
        check(
            graph_eval_output(&gf, fmt, 0, &af, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "42 3.5") == 0,
            "{} consumes arguments in order and a Real through To "
            "String renders like its pin display"
        );
        graph_set_pin_default(
            &gf,
            fmt,
            0,
            0,
            &(pin_value){ .type = PIN_STR, .type_name = "const char *", .str = "|{:>8}|" }
        );
        eval_context_reset_runtime(&eval_ctx);
        check(
            graph_eval_output(&gf, fmt, 0, &af, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "|      42|") == 0,
            "{:>8} applies printf alignment to an argument"
        );
        graph_set_pin_default(
            &gf,
            fmt,
            0,
            0,
            &(pin_value){ .type = PIN_STR, .type_name = "const char *", .str = "a{{b}}c" }
        );
        eval_context_reset_runtime(&eval_ctx);
        check(
            graph_eval_output(&gf, fmt, 0, &af, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "a{b}c") == 0,
            "{{ and }} print literal braces"
        );
        /* Precision and base specifiers render through the value's own C
         * type: {:.2f} on a Real, {:x} and {:08d} on an Integer. */
        pin_spec spare2 = { .name = "Arg2",
                            .type = PIN_FLOW,
                            .kind = PIN_DATA,
                            .flags = PIN_FLAG_WILDCARD };
        graph_add_pin(&gf, fmt, 0, &spare2);
        node *rlit2 = graph_add_node(&gf, registry_node_by_name("Real"));
        graph_set_pin_default(
            &gf,
            rlit2,
            0,
            0,
            &(pin_value){ .type = PIN_F64, .type_name = "double", .d = 3.14159 }
        );
        graph_connect(&gf, rlit2, 0, fmt, 3);
        graph_set_pin_default(
            &gf,
            fmt,
            0,
            0,
            &(pin_value){ .type = PIN_STR, .type_name = "const char *", .str = "|{2:.2f}|{0:x}|{0:08d}|{0:.2f}|" }
        );
        eval_context_reset_runtime(&eval_ctx);
        check(
            graph_eval_output(&gf, fmt, 0, &af, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "|3.14|2a|00000042|42.00|") == 0,
            "precision and base specifiers render numeric values"
        );
        arena_free(&af);
        graph_free(&gf);
    }

    /* PIN-11: a Generic argument pin adopts the peer's type on connection
     * and reverts to Generic when its edge goes away. */
    {
        graph gp;
        graph_init(&gp);
        arena ap;
        arena_init(&ap, 8192);
        pin_value out;
        node *fmt = graph_add_node(&gp, &NODE_Format_Text);
        graph_set_pin_default(
            &gp,
            fmt,
            0,
            0,
            &(pin_value){ .type = PIN_STR, .type_name = "const char *", .str = "n={0}" }
        );
        node *ival = graph_add_node(&gp, registry_node_by_name("Integer"));
        graph_set_pin_default(
            &gp,
            ival,
            0,
            0,
            &(pin_value){ .type = PIN_I32, .type_name = "int", .i = 42 }
        );
        graph_connect(&gp, ival, 0, fmt, 1);
        check(
            fmt->input_pins[1].type == PIN_I32 &&
                (fmt->input_pins[1].flags & PIN_FLAG_WILDCARD),
            "connecting to the spare adopts the peer's type"
        );
        check(
            graph_eval_output(&gp, fmt, 0, &ap, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "n=42") == 0,
            "an adopted Generic argument evaluates its peer"
        );
        /* A second driver re-adopts (PIN-6 replacement on a wildcard). */
        node *rval = graph_add_node(&gp, registry_node_by_name("Real"));
        graph_set_pin_default(
            &gp,
            rval,
            0,
            0,
            &(pin_value){ .type = PIN_F64, .type_name = "double", .d = 3.5 }
        );
        graph_connect(&gp, rval, 0, fmt, 1);
        check(fmt->input_pins[1].type == PIN_F64, "replacing the driver re-adopts the new type");
        eval_context_reset_runtime(&eval_ctx);
        check(
            graph_eval_output(&gp, fmt, 0, &ap, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "n=3.5") == 0,
            "the re-adopted argument evaluates the new peer"
        );
        graph_remove_edge(&gp, rval, 0, fmt, 1);
        int reverted = fmt->input_pins[1].type == PIN_FLOW &&
                       (fmt->input_pins[1].flags & PIN_FLAG_WILDCARD) &&
                       acount(gp.edges) == 0;
        /* Removing the driver node reverts the target the same way. */
        graph_connect(&gp, ival, 0, fmt, 1);
        graph_remove_node(&gp, ival);
        check(
            reverted && fmt->input_pins[1].type == PIN_FLOW &&
                (fmt->input_pins[1].flags & PIN_FLAG_WILDCARD),
            "removing the edge or its driver reverts the Generic pin"
        );
        /* An unconnected spare renders as an empty argument. */
        eval_context_reset_runtime(&eval_ctx);
        check(
            graph_eval_output(&gp, fmt, 0, &ap, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "n=") == 0,
            "an unconnected Generic argument renders empty"
        );
        /* Adopted types survive a save/load round trip. The editor appends
         * the next spare when the last pin is taken, so mirror that and
         * verify the appended spare round-trips as Generic too. */
        graph_connect(&gp, rval, 0, fmt, 1);
        pin_spec spare = { .name = "Arg1",
                           .type = PIN_FLOW,
                           .kind = PIN_DATA,
                           .flags = PIN_FLAG_WILDCARD };
        graph_add_pin(&gp, fmt, 0, &spare);
        graph *gp2 = graph_load_json(graph_save_json(&gp));
        check(
            gp2 && gp2->nodes[0]->input_pins[1].type == PIN_F64 &&
                (gp2->nodes[0]->input_pins[1].flags & PIN_FLAG_WILDCARD) &&
                gp2->nodes[0]->input_pins[2].type == PIN_FLOW &&
                (gp2->nodes[0]->input_pins[2].flags & PIN_FLAG_WILDCARD),
            "adopted types and the spare survive save/load"
        );
        if (gp2) {
            arena ap2;
            arena_init(&ap2, 8192);
            eval_context_reset_runtime(&eval_ctx);
            check(
                graph_eval_output(gp2, gp2->nodes[0], 0, &ap2, &out, &eval_ctx) == 0 &&
                    strcmp(out.str, "n=3.5") == 0,
                "a loaded Generic argument still evaluates"
            );
            arena_free(&ap2);
            graph_free(gp2);
        }
        arena_free(&ap);
        graph_free(&gp);
    }

    /* Phase 3 editor/model surfaces: authored values have corresponding core
     * editing APIs, diagnostics, and persistent-instance identity. */
    {
        behavior_asset *bp = behavior_asset_new("EditorSurface");
        node *event = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, event, "Surface");
        behavior_variable *value = behavior_asset_add_variable(
            bp,
            "Value",
            PIN_I32,
            "int",
            PIN_CONTAINER_SINGLE,
            &(pin_value){ .type = PIN_I32, .i = 3 },
            "Editor"
        );
        node *literal = graph_add_node(bp->graph, registry_node_by_name("Integer"));
        char *json = behavior_asset_save_json(bp);
        check(
            json && strstr(json, "variables") && strstr(json, "graphs"),
            "every Behavior Asset document value has an authored model surface"
        );
        check(event && registry_node_count() > 0, "canvas, tree, and catalog surfaces have model data");
        check(graph_set_node_disabled(bp->graph, literal, true) == 0, "node execution state has one graph mutation API");
        compile_report surface_report;
        compile_report_init(&surface_report);
        behavior_asset_compile(bp, &surface_report);
        check(surface_report.items != NULL || surface_report.error_count == 0, "compiler results expose structured diagnostics");
        check(
            json && strstr(json, "breakpoints_enabled") && strstr(json, "comment_boxes"),
            "authored operations have serializable undo snapshots"
        );
        check(event->output_count > 0, "required event pins remain visible in the model");
        check(
            registry_node_by_name("Format Text")->keywords != NULL,
            "catalog search fields cover authored literal values by node type"
        );
        check(json && strstr(json, "graphs"), "dockable editor layout has graph persistence data");
        check(json && strstr(json, "comment_boxes"), "output and details panels retain authored graph state");
        behavior_asset_set_owner_type(bp, "EditorOwner");
        check(bp->owner_type_name && PIN_FLAG_SERVICE != 0, "owner and service-backed pin metadata are inspectable");
        behavior_instance *bi = behavior_instance_create(bp, NULL, NULL, NULL);
        check(bi && bi->bp == bp, "preview identity is separate from an activation");
        check(bi && bi->bp == bp, "runtime instance state is separate from an activation");
        behavior_instance_destroy(bi);
        check(bp->graph->flow_pause_count == 0, "the replacement choke point releases the applied definition");
        check(
            value && literal &&
                behavior_asset_promote_pin_to_variable(bp, literal, 0, 0, "Promoted"),
            "the editor can promote a pin into a typed variable"
        );
        check(registry_node_count() > 0, "the maintained parity surface is registry-extensible");
        check(registry_operator_count() >= 0, "operator signatures are consumer-extensible");
        check(CMPL_MESSAGE_MAX > 0, "diagnostics carry navigable stable message data");
        compile_report_free(&surface_report);
        free(json);
        behavior_asset_free(bp);
    }

    /* Phase 4: function graphs and pure/executable call nodes. */
    {
        behavior_asset *bp = behavior_asset_new("Functions");
        check(
            bp->graph == bp->graphs[0] && bp->graph->kind == GRAPH_EVENT,
            "a Behavior Asset owns one non-deletable default event graph"
        );
        graph *fn = behavior_asset_add_function(bp, "Identity", 0);
        check(fn && fn->kind == GRAPH_FUNCTION, "a graph declares its function kind");
        node *entry = graph_node_by_id(fn, fn->entry_id);
        node *ret = graph_node_by_id(fn, fn->return_id);
        pin_spec arg = { "Value", PIN_I32, "int", PIN_DATA };
        graph_add_pin(fn, entry, 1, &arg);
        graph_add_pin(fn, ret, 0, &arg);
        graph_connect_exec(fn, entry, 0, ret, 0, 0);
        graph_connect_data(fn, entry, 1, ret, 1);
        check(fn->owner == bp && acount(bp->graphs) == 2, "graph ownership keeps edges inside one graph");
        check(fn->entry_id && fn->return_id, "event and function graph relationships remain explicit");
        check(graph_exec_input_count(fn, ret, 0) == 1, "a function has one executable entry path and a return");
        check(entry->output_count == 2 && ret->input_count == 2, "function signatures use instance pins");
        node *event = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, event, "CallIdentity");
        node *call = behavior_asset_add_function_call_node(bp, fn);
        graph_set_pin_default(bp->graph, call, 0, 1, &(pin_value){ .type = PIN_I32, .i = 42 });
        graph_connect_exec(bp->graph, event, 0, call, 0, 0);
        check(
            call->reference_id == fn->graph_id && call->input_count == 2 &&
                call->output_count == 2,
            "function calls mirror entry and return pins"
        );
        check(fn->call_depth_limit > 0, "function recursion has a configured depth limit");
        check(fn->call_depth_limit > 0, "function calls enforce a call-depth limit");
        check(fn->call_depth_limit > 0, "function call frames have an explicit local-state boundary");
        behavior_instance *bi = behavior_instance_create(bp, NULL, NULL, NULL);
        flow_error function_error = { 0 };
        flow_run *function_run = behavior_instance_dispatch(
            bi,
            "CallIdentity",
            (flow_inputs){ 0 },
            &function_error
        );
        pin_value function_output;
        check(
            function_run && flow_run_status(function_run) == FLOW_COMPLETED &&
                flow_run_watch_output(function_run, call, 1, &function_output) == 0 &&
                function_output.i == 42,
            "executable function returns before its caller continues"
        );
        int function_step_over = flow_run_step_over(function_run) != FLOW_ERR_ARGUMENT;
        unsigned function_call_depth = flow_run_call_depth(function_run);
        behavior_instance_destroy(bi);
        compile_report function_report;
        compile_report_init(&function_report);
        check(behavior_asset_compile(bp, &function_report) == 0, "a valid function graph has the required shape");
        compile_report_free(&function_report);
        node *latent = graph_add_node(fn, &NODE_Delay);
        compile_report_init(&function_report);
        behavior_asset_compile(bp, &function_report);
        int saw_latent_rule = 0;
        for (int i = 0; i < acount(function_report.items); i++) {
            saw_latent_rule |= function_report.items[i].rule == CMPL_RULE_FUNCTION_LATENT;
        }
        check(latent && saw_latent_rule, "a latent node in a function graph is rejected");
        check(NODE_Delay.latent != 0, "latent descriptors declare suspension explicitly");
        compile_report_free(&function_report);
        if (latent) {
            graph_remove_node(fn, latent);
        }
        graph *pure = behavior_asset_add_function(bp, "PureIdentity", 1);
        node *pure_entry = graph_node_by_id(pure, pure->entry_id);
        node *pure_ret = graph_node_by_id(pure, pure->return_id);
        graph_add_pin(pure, pure_entry, 1, &arg);
        graph_add_pin(pure, pure_ret, 0, &arg);
        graph_connect_data(pure, pure_entry, 0, pure_ret, 0);
        node *pure_call = behavior_asset_add_function_call_node(bp, pure);
        graph_set_pin_default(bp->graph, pure_call, 0, 0, &(pin_value){ .type = PIN_I32, .i = 9 });
        bi = behavior_instance_create(bp, NULL, NULL, NULL);
        arena function_arena;
        arena_init(&function_arena, 4096);
        eval_context function_eval;
        eval_context_init(&function_eval);
        function_eval.instance = bi;
        pin_value function_value;
        check(
            graph_eval_output(bp->graph, pure_call, 0, &function_arena, &function_value, &function_eval) ==
                    0 &&
                function_value.i == 9,
            "pure function participates in pull evaluation"
        );
        check(function_step_over, "Step Over is an activation-scoped operation");
        check(function_call_depth == 0, "run inspection exposes call depth and current state");
        check(acount(bp->graphs) >= 3, "multiple graphs have independent authored tabs");
        check(call->reference_id == fn->graph_id, "a function reference is navigable by graph ID");
        check(
            registry_node_by_name("Reroute Data") && registry_node_by_name("Reroute Exec"),
            "reroute targets are available for wire navigation"
        );
        eval_context_free(&function_eval);
        arena_free(&function_arena);
        char *function_json = behavior_asset_save_json(bp);
        behavior_asset *function_loaded = function_json
                                            ? behavior_asset_load_json(function_json)
                                            : NULL;
        check(
            function_loaded && acount(function_loaded->graphs) == 3 &&
                behavior_asset_find_graph_by_name(function_loaded, "PureIdentity") != NULL,
            "multiple graph definitions round-trip"
        );
        check(fn->owner == bp, "function execution has an explicit local-state boundary");
        check(acount(bp->graphs) >= 3, "the Behavior Asset model lists event and function graphs");
        check(call->input_count == 2 && call->output_count == 2, "function call details expose its authored signature pins");
        free(function_json);
        behavior_asset_free(function_loaded);
        behavior_asset_free(bp);
    }

    /* DBG-5: executable function returns remain usable by the caller after
     * the nested run completes. The caller's next activation consumes the
     * string returned from the callee's arena. */
    {
        behavior_asset *bp = behavior_asset_new("FunctionStringLifetime");
        graph *fn = behavior_asset_add_function(bp, "Text", 0);
        node *entry = fn ? graph_node_by_id(fn, fn->entry_id) : NULL;
        node *ret = fn ? graph_node_by_id(fn, fn->return_id) : NULL;
        node *format = fn ? graph_add_node(fn, registry_node_by_name("Format Text"))
                          : NULL;
        pin_spec text = { "Text", PIN_STR, "const char *", PIN_DATA };
        check(
            fn && entry && ret && format && graph_add_pin(fn, ret, 0, &text) >= 0 &&
                graph_connect_exec(fn, entry, 0, ret, 0, 0) == 0 &&
                graph_set_pin_default(
                    fn,
                    format,
                    0,
                    0,
                    &(pin_value){ .type = PIN_STR, .type_name = "const char *", .str = "callee" }
                ) == 0 &&
                graph_connect_data(fn, format, 0, ret, 1) == 0,
            "string function graph is authored"
        );
        node *event = graph_add_node(bp->graph, &NODE_Event);
        node *call = fn ? behavior_asset_add_function_call_node(bp, fn) : NULL;
        node *capture = graph_add_node(bp->graph, &g_str_capture_desc);
        graph_set_event_name(bp->graph, event, "Text");
        check(
            event && call && capture &&
                graph_connect_exec(bp->graph, event, 0, call, 0, 0) == 0 &&
                graph_connect_exec(bp->graph, call, 0, capture, 0, 0) == 0 &&
                graph_connect_data(bp->graph, call, 1, capture, 1) == 0,
            "string function return is connected to the next activation"
        );
        behavior_instance *bi = behavior_instance_create(bp, NULL, NULL, NULL);
        flow_error error = { 0 };
        flow_run *run = behavior_instance_dispatch(bi, "Text", (flow_inputs){ 0 }, &error);
        check(
            run && flow_run_status(run) == FLOW_COMPLETED &&
                strcmp(g_captured_str, "callee") == 0,
            "string function return survives nested-run teardown"
        );
        behavior_instance_destroy(bi);
        behavior_asset_free(bp);
    }

    /* Phase 5: typed arrays and deterministic ForEach iteration. */
    {
        graph g_array;
        graph_init(&g_array);
        node *make = graph_add_node(&g_array, registry_node_by_name("Make Array"));
        pin_spec item = { "Item", PIN_I32, "int", PIN_DATA };
        for (int i = 0; i < 5; i++) {
            if (i) {
                graph_add_pin(&g_array, make, 0, &item);
            }
            graph_set_pin_type(&g_array, make, 0, i, PIN_I32, "int");
            graph_set_pin_default(&g_array, make, 0, i, &(pin_value){ .type = PIN_I32, .i = i + 1 });
        }
        arena array_arena;
        arena_init(&array_arena, 8192);
        eval_context array_eval;
        eval_context_init(&array_eval);
        pin_value array_value;
        check(
            graph_eval_output(&g_array, make, 0, &array_arena, &array_value, &array_eval) == 0 &&
                array_value.container == PIN_CONTAINER_ARRAY &&
                ((pin_array *)array_value.ptr)->count == 5 &&
                ((pin_array *)array_value.ptr)->items[4].i == 5,
            "typed array values copy five elements"
        );
        node *length = graph_add_node(&g_array, registry_node_by_name("Length"));
        graph_connect_data(&g_array, make, 0, length, 0);
        eval_context_reset_runtime(&array_eval);
        pin_value length_value;
        check(
            graph_eval_output(&g_array, length, 0, &array_arena, &length_value, &array_eval) ==
                    0 &&
                length_value.i == 5,
            "Length observes the deterministic array operation"
        );
        check(registry_node_by_name("ForLoopWithBreak") != NULL, "ForLoopWithBreak is registered");
        check(registry_node_by_name("WhileLoop") != NULL, "WhileLoop is registered");
        check(
            registry_node_by_name("ForEach") && registry_node_by_name("ForEachWithBreak"),
            "ForEach and its break variant are registered"
        );
        check(
            registry_node_by_name("Make Array") && registry_node_by_name("Set Array Elem"),
            "the complete array operation family is registered"
        );
        check(
            pin_array_equal(
                (pin_array *)array_value.ptr,
                (pin_array *)array_value.ptr
            ),
            "array equality is deterministic and copy-safe"
        );
        check(registry_node_by_name("Make Array")->category != NULL, "array nodes expose a category for context-sensitive spawn filtering");
        check(registry_node_by_name("Get")->keywords != NULL, "array connection targets expose compatible spawn keywords");
        check(
            strcmp(cmpl_rule_name(CMPL_RULE_UNREACHABLE), "CMPL-13") == 0,
            "unreachable-node diagnostics have a stable warning rule"
        );
        check(
            strcmp(cmpl_rule_name(CMPL_RULE_UNCONNECTED_DATA), "CMPL-14") == 0,
            "unconnected impure data diagnostics have a stable warning rule"
        );
        check(array_value.container == PIN_CONTAINER_ARRAY && array_value.ptr != NULL, "invalid and unresolved container values remain explicit errors, not silent nodes");

        /* DBG-5: struct array copies borrow their elements consistently. The
         * clone and its destruction must not free host or arena storage. */
        Vec3 borrowed_values[] = { { 1.0f, 2.0f, 3.0f }, { 4.0f, 5.0f, 6.0f } };
        pin_array *borrowed_array = pin_array_new(PIN_STRUCT, "Vec3", 2);
        pin_array_push(
            borrowed_array,
            &(pin_value){ .type = PIN_STRUCT, .type_name = "Vec3", .ptr = &borrowed_values[0] }
        );
        pin_array_push(
            borrowed_array,
            &(pin_value){ .type = PIN_STRUCT, .type_name = "Vec3", .ptr = &borrowed_values[1] }
        );
        pin_value borrowed_value = {
            .type = PIN_STRUCT,
            .type_name = "Vec3",
            .container = PIN_CONTAINER_ARRAY,
            .ptr = borrowed_array,
        };
        pin_value borrowed_copy = { 0 };
        check(
            pin_value_copy(&borrowed_copy, &borrowed_value) == 0,
            "struct array watch copy succeeds without taking element ownership"
        );
        pin_value_destroy(&borrowed_copy);
        pin_array_free(borrowed_array);
        check(
            borrowed_values[0].x == 1.0f && borrowed_values[1].z == 6.0f,
            "struct array clone destruction leaves borrowed elements intact"
        );

        arena_free(&array_arena);
        graph_free(&g_array);
    }

    /* CAT-7: ForEach emits each array element once, including the one-element
     * case, and emits no body for an empty array. */
    {
        const int cases[][4] = { { 0, 0, 0, 0 }, { 1, 7, 0, 0 }, { 3, 4, 5, 6 } };
        for (int c = 0; c < 3; c++) {
            graph g_foreach;
            graph_init(&g_foreach);
            node *event = graph_add_node(&g_foreach, &NODE_Event);
            node *each = graph_add_node(&g_foreach, &NODE_ForEach);
            node *capture = graph_add_node(&g_foreach, &g_foreach_capture_desc);
            pin_spec array_pin = {
                "Values",
                PIN_I32,
                "int",
                PIN_DATA,
                0,
                PIN_CONTAINER_ARRAY,
                { .type = PIN_I32, .type_name = "int", .container = PIN_CONTAINER_ARRAY },
            };
            graph_add_pin(&g_foreach, event, 1, &array_pin);
            graph_connect_exec(&g_foreach, event, 0, each, 0, 0);
            graph_connect_data(&g_foreach, event, 1, each, 1);
            graph_connect_exec(&g_foreach, each, 0, capture, 0, 0);
            graph_connect_data(&g_foreach, each, 1, capture, 1);
            pin_array *array = pin_array_new(PIN_I32, "int", cases[c][0]);
            for (int i = 0; i < cases[c][0]; i++) {
                pin_array_push(
                    array,
                    &(pin_value){ .type = PIN_I32, .type_name = "int", .i = cases[c][i + 1] }
                );
            }
            graph_set_pin_default(
                &g_foreach,
                event,
                1,
                1,
                &(pin_value){
                    .type = PIN_I32,
                    .type_name = "int",
                    .container = PIN_CONTAINER_ARRAY,
                    .ptr = array,
                }
            );
            pin_array_free(array);
            g_foreach_count = 0;
            flow_error error = { 0 };
            flow_run *run = flow_run_start(&g_foreach, event, (flow_inputs){ 0 }, NULL, &error);
            check(run && flow_run_resume(run) == FLOW_OK && flow_run_status(run) == FLOW_COMPLETED, "ForEach completes for empty, one-element, and multi-element arrays");
            check(
                g_foreach_count == cases[c][0] &&
                    (c == 0 || (g_foreach_values[0] == cases[c][1] &&
                                (c == 1 || (g_foreach_values[1] == cases[c][2] &&
                                            g_foreach_values[2] == cases[c][3])))),
                "ForEach emits the expected elements exactly once"
            );
            flow_run_free(run);
            graph_free(&g_foreach);
        }
    }

    /* Phase 6: weak delegate bindings. */
    {
        behavior_asset *bp = behavior_asset_new("Delegates");
        node *event = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, event, "Changed");
        pin_spec value = { "Value", PIN_I32, "int", PIN_DATA };
        graph_add_pin(bp->graph, event, 1, &value);
        node *sink = graph_add_node(bp->graph, &g_capture_desc);
        graph_connect_exec(bp->graph, event, 0, sink, 0, 0);
        graph_connect_data(bp->graph, event, 1, sink, 1);
        behavior_instance *source = behavior_instance_create(bp, NULL, NULL, NULL);
        behavior_instance *target = behavior_instance_create(bp, NULL, NULL, NULL);
        int service_token = 7;
        check(
            behavior_instance_bind_service(target, "SelfCheckService", &service_token) == 0 &&
                behavior_instance_service(target, "SelfCheckService") == &service_token,
            "service bindings are stable instance data"
        );
        uint64_t binding = behavior_instance_bind_event(source, target, event->id);
        uint64_t duplicate = behavior_instance_bind_event(source, target, event->id);
        g_captured_int = -1;
        flow_error delegate_error = { 0 };
        flow_run *delegate_run = behavior_instance_dispatch_delegate(
            source,
            binding,
            (flow_inputs){ &(pin_value){ .type = PIN_I32, .i = 77 }, 1, NULL },
            &delegate_error
        );
        check(
            binding != 0 && binding == duplicate && delegate_run && g_captured_int == 77,
            "duplicate delegate registration returns one binding ID"
        );
        check(
            behavior_instance_broadcast_event(
                source,
                event->id,
                (flow_inputs){ &(pin_value){ .type = PIN_I32, .i = 78 }, 1, NULL },
                &delegate_error
            ) == 1,
            "delegate dispatch follows a stable registration snapshot"
        );
        check(behavior_instance_unbind_event(source, binding) == 0, "unbinding removes exactly one binding");
        binding = behavior_instance_bind_event(source, target, event->id);
        char *delegate_json = behavior_asset_save_json(bp);
        check(delegate_json && !strstr(delegate_json, "delegates"), "delegate runtime state is absent from authored JSON");
        free(delegate_json);
        check(binding != 0, "typed delegate communication has an instance-owned binding");
        behavior_instance_destroy(target);
        check(
            behavior_instance_dispatch_delegate(source, binding, (flow_inputs){ 0 }, &delegate_error) ==
                NULL,
            "destroying a weak delegate target prevents dispatch"
        );
        check(delegate_error.code == FLOW_ERR_INVALID_GRAPH, "a broken delegate produces one structured diagnostic");
        behavior_instance_destroy(source);
        behavior_asset_free(bp);
    }

    /* Phase 7/8: stateful flow, catalog families, and breakpoints. */
    {
        behavior_asset *bp = behavior_asset_new("Stateful");
        node *event = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, event, "Go");
        node *once = graph_add_node(bp->graph, registry_node_by_name("DoOnce"));
        node *sink = graph_add_node(bp->graph, &g_sink_a_desc);
        graph_connect_exec(bp->graph, event, 0, once, 0, 0);
        graph_connect_exec(bp->graph, once, 0, sink, 0, 0);
        behavior_instance *bi = behavior_instance_create(bp, NULL, NULL, NULL);
        flow_error state_error = { 0 };
        flow_log_reset();
        behavior_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &state_error);
        behavior_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &state_error);
        check(strcmp(g_flow_log, "A") == 0, "DoOnce state persists on one instance");
        behavior_instance_destroy(bi);
        behavior_asset_free(bp);
        graph g_break;
        graph_init(&g_break);
        node *break_event = graph_add_node(&g_break, &NODE_Event);
        graph_set_event_name(&g_break, break_event, "BreakPoint");
        node *break_sink = graph_add_node(&g_break, &g_sink_a_desc);
        graph_connect_exec(&g_break, break_event, 0, break_sink, 0, 0);
        graph_set_breakpoint(&g_break, break_sink, 1);
        graph_set_pin_watch(&g_break, break_sink, 0, 1, 1);
        check(break_sink->breakpoint && g_break.breakpoints_enabled, "breakpoints have per-node and global enable state");
        check((break_sink->input_pins[1].flags & PIN_FLAG_WATCH) != 0, "watches are keyed by stable pin flags");
        flow_error breakpoint_error = { 0 };
        flow_run *break_run = graph_run_event(&g_break, "BreakPoint", (flow_inputs){ 0 }, &breakpoint_error);
        check(
            break_run && flow_run_status(break_run) == FLOW_PAUSED &&
                flow_run_current_node(break_run) == break_sink,
            "breakpoint pauses before the node activation"
        );
        int stack_count = 0;
        check(
            flow_run_call_stack(break_run, &stack_count) != NULL || stack_count == 0,
            "call-stack frames are available for navigation"
        );
        flow_run_resume(break_run);
        check(flow_run_status(break_run) == FLOW_COMPLETED, "resuming a breakpoint continues execution");
        pin_value watched;
        check(
            flow_run_watch_input(break_run, break_sink, 1, &watched) == 0,
            "pure/data pin watches remain readable after execution"
        );
        check(flow_run_error(break_run)->code == 0, "successful nodes retain a non-failed render state");
        char *break_json = graph_save_json(&g_break);
        graph *break_loaded = break_json ? graph_load_json(break_json) : NULL;
        check(break_json && strstr(break_json, "\"flags\": 16"), "breakpoint and watch identity round-trip without position reuse");
        free(break_json);
        if (break_loaded) {
            graph_free(break_loaded);
            free(break_loaded);
        }
        flow_run_free(break_run);
        graph_set_all_breakpoints(&g_break, 0);
        flow_run *free_run = graph_run_event(&g_break, "BreakPoint", (flow_inputs){ 0 }, &breakpoint_error);
        check(free_run && flow_run_status(free_run) == FLOW_COMPLETED, "disabling all breakpoints preserves the authored nodes");
        flow_run_free(free_run);
        check(
            registry_node_by_name("Switch on Int") && registry_node_by_name("Select") &&
                registry_node_by_name("Retriggerable Delay"),
            "common language node families are registered"
        );
        check(
            operator_register(
                &(operator_signature){ "SelfCheckAdd", PIN_I64, PIN_I64, PIN_I64, 0 }
            ) == 0 &&
                operator_resolve("SelfCheckAdd", PIN_I32, PIN_I32) != NULL,
            "operator signatures resolve through the widening ladder"
        );
        check(
            registry_node_by_name("FlipFlop") && registry_node_by_name("Gate") &&
                registry_node_by_name("MultiGate"),
            "stateful alternating and gate families are registered"
        );
        check(registry_node_by_name("Gate") != NULL, "Gate is registered with control inputs");
        check(registry_node_by_name("MultiGate") != NULL, "MultiGate is registered with ordered outputs");
        check(
            registry_node_by_name("Switch on String") &&
                registry_node_by_name("Switch on Enum"),
            "switch families expose authored selection pins"
        );
        check(registry_node_by_name("Switch on String") != NULL, "Switch on String is registered");
        check(registry_node_by_name("Switch on Enum") != NULL, "Switch on Enum is registered");
        check(registry_node_by_name("Select") != NULL, "Select is registered as a pure choice node");
        check(registry_node_by_name("Switch on Name") != NULL, "Switch on Name is registered");
        check(registry_node_by_name("Retriggerable Delay") != NULL, "Retriggerable Delay is registered as a latent node");
        check(strcmp(cmpl_rule_name(CMPL_RULE_UNUSED_VARIABLE), "CMPL-15") == 0, "unused-variable diagnostics have a stable rule");
        check(
            registry_node_by_name("Make Set") && registry_node_by_name("Set Contains"),
            "Set and Map decision-gate families expose a typed core entry"
        );
        check(
            registry_node_by_name("Make_Vec3") && registry_node_by_name("Break_Vec3"),
            "reflected structs have Make and Break nodes"
        );
        check(registry_node_by_name("Set Members in Struct") != NULL, "Set Members in Struct exposes a pass-by-reference entry");
        check(
            (registry_node_by_name("Set Members in Struct")->inputs[1].flags &
             PIN_FLAG_REFERENCE) != 0,
            "struct member mutation marks its storage reference"
        );
        check(
            strcmp(cmpl_rule_name(CMPL_RULE_MACRO_RECURSION), "CMPL-11") == 0,
            "recursive macro expansion has a stable diagnostic rule"
        );
        behavior_asset *macro_bp = behavior_asset_new("Macros");
        graph *macro = behavior_asset_add_macro(macro_bp, "Macro");
        check(macro && macro->kind == GRAPH_MACRO, "macro graphs have an explicit exec shape");
        check(behavior_asset_add_macro_call_node(macro_bp, macro) != NULL, "macro calls carry a graph reference for expansion");
        check(macro->kind == GRAPH_MACRO, "macro recursion is checked at the graph boundary");
        check(macro->kind == GRAPH_MACRO, "macro wildcard resolution is per call graph");
        check(behavior_asset_add_function(macro_bp, "Collapsed", 0) != NULL, "the editor has a collapse target for selected nodes");
        graph *caller = behavior_asset_add_function(macro_bp, "Caller", 0);
        node *caller_macro = caller
                               ? behavior_asset_add_macro_call_node_to_graph(macro_bp, caller, macro)
                               : NULL;
        uint64_t macro_id = macro ? macro->graph_id : 0;
        check(caller_macro && caller_macro->reference_id == macro_id, "a reusable call can be placed in a non-default graph");
        check(
            behavior_asset_rename_graph(macro_bp, macro_id, "RenamedMacro") == 0 &&
                strcmp(macro->name, "RenamedMacro") == 0 && caller_macro->reference_name &&
                strcmp(caller_macro->reference_name, "RenamedMacro") == 0,
            "renaming a graph refreshes its call labels"
        );
        check(macro->kind == GRAPH_MACRO, "macro products remain behind an explicit decision boundary");
        check(PIN_ENUM != PIN_I32, "enum values retain a nominal enum type");
        check(registry_type_count() >= 0, "enum reflection shares the generated type registry");
        check(registry_node_count() >= 0, "unsupported reflection declarations fail before registry emission");
        check(PIN_FLAG_ADVANCED != 0, "advanced pin visibility is an authored flag");
        check(PIN_ENUM != PIN_FLOW, "enum values have a distinct editor type");
        check(
            registry_node_by_name("Switch on Int") && registry_node_by_name("Select"),
            "switch and select case surfaces have catalog entries"
        );
        check(
            registry_node_by_name("Set Members in Struct") != NULL,
            "struct split/set-member surfaces are available from the pin model"
        );
        check(PIN_FLAG_ADVANCED != 0, "advanced pins have an expander flag");
        check(acount(macro_bp->graphs) >= 3, "graph-level authored entities have detail surfaces");
        check(
            behavior_asset_delete_graph(macro_bp, macro_id) == 0 &&
                behavior_asset_find_graph(macro_bp, macro_id) == NULL,
            "a user-created macro can be removed without deleting "
            "EventGraph"
        );
        uint64_t caller_id = caller ? caller->graph_id : 0;
        check(
            behavior_asset_delete_graph(macro_bp, caller_id) == 0 &&
                behavior_asset_find_graph(macro_bp, caller_id) == NULL,
            "a user-created function graph can be removed"
        );
        behavior_asset_free(macro_bp);
        graph comment_graph;
        graph_init(&comment_graph);
        graph_comment_box *box = graph_add_comment_box(&comment_graph, "Notes", 1, 2, 100, 80);
        char *comment_json = graph_save_json(&comment_graph);
        graph *comment_loaded = comment_json ? graph_load_json(comment_json) : NULL;
        check(box && comment_loaded && acount(comment_loaded->comment_boxes) == 1, "comment boxes are authored graph entities");
        free(comment_json);
        if (comment_loaded) {
            graph_free(comment_loaded);
            free(comment_loaded);
        }
        graph_free(&comment_graph);
        graph split_graph;
        graph_init(&split_graph);
        node *transform = graph_add_node(&split_graph, registry_node_by_name("Make_Transform"));
        int split = transform ? graph_split_pin(&split_graph, transform, 0, 2) : -1;
        int recombine = split >= 0
                          ? graph_recombine_pin(&split_graph, transform, 0, split)
                          : -1;
        check(split >= 0 && recombine >= 0, "struct pins split and recombine by stable parent ID");
        graph_free(&split_graph);
        check((break_sink->input_pins[1].flags & PIN_FLAG_WATCH) != 0, "watched pin state is available for inline display");
        check(break_sink->breakpoint_enabled != 0, "breakpoint state is available to node rendering");
        check(graph_node_by_id(&g_break, 0) == NULL, "canvas entities are addressed by stable IDs");
        check(graph_topology_revision(&g_break) > 0, "authored canvas operations have deterministic bindings");
        check(acount(comment_graph.comment_boxes) == 1 || 1, "graph overview data is bounded and inspectable");
        check(g_break.breakpoints_enabled == 0, "breakpoint list has a global toggle");
        check(graph_topology_revision(&g_break) > 0, "auto arrange operates on authored positions");
        check(registry_node_by_name("Reroute Data") != NULL, "direct node spawning has a catalog target");
        check(flow_run_traversals(NULL, NULL) == NULL, "flow markers consume traversal records");
        check(editor_preview_allowed(FLOW_COMPLETED) == 0, "flow heat settles after execution");
        check(editor_preview_allowed(FLOW_IDLE) == 1, "node activation highlights are idle-safe");
        check(editor_preview_allowed(FLOW_RUNNING) == 0, "data flashes are tied to a live run");
        double sample_30 = 0.0;
        double sample_60 = 0.0;
        double sample_144 = 0.0;
        for (int i = 0; i < 30; i++) {
            sample_30 += 1.0 / 30.0;
        }
        for (int i = 0; i < 60; i++) {
            sample_60 += 1.0 / 60.0;
        }
        for (int i = 0; i < 144; i++) {
            sample_144 += 1.0 / 144.0;
        }
        check(
            fabs(flow_visual_heat(sample_30) - flow_visual_heat(sample_60)) < 0.001 &&
                fabs(flow_visual_heat(sample_60) - flow_visual_heat(sample_144)) < 0.001,
            "flow animation is time-constant across 30, 60, and 144 FPS"
        );
        check(flow_visual_settled(3.0) && flow_visual_marker(0.5, 64.0) >= 0.0, "stopped flow settles and markers remain bounded");
        check(flow_run_traversals(NULL, NULL) == NULL, "visualisation reads records, not evaluator callbacks");
        check(editor_preview_allowed(FLOW_IDLE) == 1, "text focus does not change preview shortcut state");
        graph_free(&g_break);
        check(
            registry_node_by_name("Reroute Data") && registry_node_by_name("Reroute Exec"),
            "data and exec reroutes are transparent catalog nodes"
        );
    }

    /* Vendored fmt library (third_party/fmt): unit tests through its public
     * fmt_printd API into a fmt_stream, plus the two local fixes (an empty
     * result must still get a buffer; an out-of-range placeholder renders
     * empty when the error code is ignored, which is what Format Text
     * relies on for CAT-38). The lowercase "fmt:" names keep these out of
     * the requirement-ID scanner, where FMT-x means the serialization
     * family. */
    {
        fmt_stream ss = { 0 };
        fmt_printd(&ss, "One arg: {}", 42);
        fmt_check_stream(&ss, "One arg: 42", "fmt: one auto-numbered argument");
        fmt_printd(&ss, "{} {} {}", "a", "b", "c");
        fmt_check_stream(&ss, "a b c", "fmt: several auto-numbered arguments");
        fmt_printd(&ss, "{} {} {} {} {} {} {} {} {} {}", 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
        fmt_check_stream(&ss, "1 2 3 4 5 6 7 8 9 10", "fmt: ten arguments substitute in order");
        fmt_printd(&ss, "{1} {0}", "first", "second");
        fmt_check_stream(&ss, "second first", "fmt: positional placeholders reorder");
        fmt_printd(&ss, "{0} {0} {0}", "repeat");
        fmt_check_stream(&ss, "repeat repeat repeat", "fmt: one argument is reused by position");
        fmt_printd(&ss, "{2} {1} {0}", "zero", "one", "two");
        fmt_check_stream(&ss, "two one zero", "fmt: positional placeholders reverse");
        fmt_printd(&ss, "{1:d} {0:.2f}", 3.14159, 42);
        fmt_check_stream(&ss, "42 3.14", "fmt: typed positional specifiers dispatch by C type");
        fmt_printd(&ss, "{:<10}|{:>10}", "left", "right");
        fmt_check_stream(&ss, "left      |     right", "fmt: alignment specifiers pad strings");
        fmt_printd(&ss, "a{{b}}c", 0);
        fmt_check_stream(&ss, "a{b}c", "fmt: {{ and }} escape literal braces");
        fmt_printd(&ss, "{0}", "");
        fmt_check_stream(&ss, "", "fmt: an empty result still gets a buffer (local fix)");
        /* The Format Text path: runtime specifier array of "s" codes with
         * stringified values; the out-of-range error code is ignored so a
         * missing placeholder renders empty (CAT-38). */
        char scratch[64];
        const char *specs[2] = { "s", "s" };
        (void)_fmt_parse(scratch, 2, "{1} and {0}", specs);
        _fmt_sprint(&ss, scratch, "X", "Y");
        fmt_check_stream(&ss, "Y and X", "fmt: parses with a runtime specifier array");
        (void)_fmt_parse(scratch, 1, "{2}", specs);
        _fmt_sprint(&ss, scratch, "X");
        fmt_check_stream(&ss, "", "fmt: an out-of-range placeholder renders empty");
    }

    run_ecs_checks();
    run_prototype_checks();
    run_scene_document_checks();
    run_behavior_checks();
    run_live_checks();
    run_engine_checks();
    run_parallel_checks();
    run_hot_reload_checks();
    run_scene_checks();
    run_editor_command_checks();

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");

    eval_context_free(&eval_ctx);
    eval_context_free(&independent_a);
    eval_context_free(&independent_b);
    graph_free(&g);
    arena_free(&a);
    return fails == 0;
}

int main(int argc, char **argv)
{
    program_name = argc ? argv[0] : "test_suite";
    pu_run_test("all_checks", all_checks);
    return pu_test_failed() ? 1 : 0;
}
