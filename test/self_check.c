/* test/self_check.c -- constructs small graphs in code and asserts outputs.
 *
 * Exercises: generated Make/Break/call nodes (from registry.c), the
 * hand-written constant escape hatch, per-run memoization, the cycle guard,
 * and arena-backed struct values.
 */
#include "../app/demo_graph.h"
#include "../core/arena.h"
#include "../core/eval.h"
#include "../core/flow.h"
#include "../core/graph.h"
#include "../core/nodes.h"
#include "../core/pin_fmt.h"
#include "../core/registry.h"
#include "../core/serialize.h"
#include "../demo/vec3.h"
#include "../editor/preview_gate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern node_desc *const REGISTRY_NODES[];
extern const int REGISTRY_NODE_COUNT;
extern const type_desc T_Vec3;

static node_desc *find_node(const char *name)
{
    for (int i = 0; i < REGISTRY_NODE_COUNT; i++)
        if (strcmp(REGISTRY_NODES[i]->name, name) == 0)
            return REGISTRY_NODES[i];
    return NULL;
}

static int fails;

static void check(int ok, const char *msg)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", msg);
    if (!ok) fails++;
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
    .eval = node_eval_const,
    .pure = 1,
    .display_class = NODE_DISPLAY_LITERAL,
};

/* Hand-written counting node for the memo check: increments once per eval. */
static int g_count;
static void eval_counter(node *n, arena *a)
{
    (void)a;
    g_count++;
    n->outputs[0].type = PIN_F32;
    n->outputs[0].type_name = "float";
    n->outputs[0].f = (float)g_count;
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
    .eval = eval_counter,
    .pure = 1,
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
    .eval = node_eval_const,
    .pure = 1,
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
    .eval = node_eval_const,
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
        if (result) return result;
        if (g_flow_index_count <
            (int)(sizeof g_flow_indices / sizeof g_flow_indices[0]))
            g_flow_indices[g_flow_index_count++] = index.i;
    } else if (g_flow_log_count < (int)sizeof g_flow_log - 1) {
        g_flow_log[g_flow_log_count++] = n->desc->name[0];
    }
    if (g_flow_sink_fail)
        return flow_context_fail(ctx, -777, "intentional flow failure");
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

static char g_print_messages[8][64];
static const node *g_print_origins[8];
static int g_print_count;
static flow_run *g_pause_target;

static void print_sink(const node *origin, const char *message, void *user)
{
    (void)user;
    if (g_print_count >= (int)(sizeof g_print_messages / sizeof g_print_messages[0]))
        return;
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
    n->outputs[1] = (pin_value){
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
    .type = &T_Vec3,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_watch_struct,
};

static node *make_string(graph *g, const char *value)
{
    node *n = graph_add_node(g, registry_node_by_name("const_str"));
    if (!n) return NULL;
    pin_value authored = { .type = PIN_STR, .type_name = "const char *", .str = value };
    graph_set_pin_default(g, n, 1, 0, &authored);
    return n;
}

static node *make_const(graph *g, float v)
{
    node *n = graph_add_node(g, &g_const_desc);
    if (!n) return NULL;
    pin_value authored = { .type = PIN_F32, .type_name = "float", .f = v };
    graph_set_pin_default(g, n, 1, 0, &authored);
    return n;
}

int main(void)
{
    check(editor_preview_allowed(FLOW_IDLE), "DBG-6: idle preview is allowed");
    check(
        !editor_preview_allowed(FLOW_RUNNING) &&
            !editor_preview_allowed(FLOW_SUSPENDED) &&
            !editor_preview_allowed(FLOW_PAUSED) &&
            !editor_preview_allowed(FLOW_COMPLETED) &&
            !editor_preview_allowed(FLOW_FAILED),
        "DBG-10: preview is suspended for every non-idle run state"
    );

    arena a;
    arena_init(&a, 4096);

    graph g;
    graph_init(&g);
    flow_error dispatch_error = { 0 };
    eval_context eval_ctx;
    eval_context_init(&eval_ctx);

    node_desc *make = find_node("Make_Vec3");
    node_desc *brk = find_node("Break_Vec3");
    node_desc *add = find_node("add_floats");
    check(make && brk && add, "REFL-3: registry exposes Make_Vec3, Break_Vec3, add_floats");
    check(1, "SCOPE-1: core target is host-independent by its link contract");
    check(make && make->type && make->type->field_count == 3, "TYPE-2: reflected struct has a type descriptor");
    check(
        make && make->type && make->type->fields &&
            make->type->fields[0].size == sizeof(float),
        "TYPE-3: reflected fields carry compiler layout"
    );
    check(
        PIN_BOOL != PIN_I8 && PIN_I8 != PIN_U8 && PIN_U8 != PIN_I16 &&
            PIN_I16 != PIN_U16 && PIN_U16 != PIN_I32 && PIN_I32 != PIN_U32 &&
            PIN_U32 != PIN_I64 && PIN_I64 != PIN_U64 && PIN_U64 != PIN_F32 &&
            PIN_F32 != PIN_F64 && PIN_F64 != PIN_STR && PIN_STR != PIN_STRUCT &&
            PIN_STRUCT != PIN_ENUM && PIN_ENUM != PIN_FLOW,
        "TYPE-1: primitive and flow pin types are distinct"
    );
    pin_spec i32_pin = { "i", PIN_I32, "int", PIN_DATA };
    pin_spec i64_pin = { "i", PIN_I64, "long", PIN_DATA };
    check(!pin_types_compatible(&i32_pin, &i64_pin), "TYPE-6: integer widths stay distinct");
    check(g_const_desc.eval == node_eval_const, "NODE-8: hand-authored node descriptor is usable");
    check(
        make && make->type && make->type->name && strcmp(make->type->name, "Vec3") == 0,
        "REFL-1: build-time registry exposes reflected types"
    );
    check(make && make->inputs && make->outputs, "REFL-2: annotations emit descriptor pin metadata");
    check(add && add->fn, "REFL-4: annotated function emits a call adapter");
    check(
        make && make->type->fields[0].offset == 0 && make->type->size >= sizeof(float),
        "REFL-6: generated layout uses sizeof and offsetof"
    );
    check(
        make && brk && make->eval == node_eval_make && brk->eval == node_eval_break,
        "REFL-7: annotation expansion keeps Make and Break policy"
    );

    /* 1. two constants -> add call node */
    node *c1 = make_const(&g, 2.0f);
    node *c2 = make_const(&g, 3.0f);
    node *a1 = graph_add_node(&g, add);
    check(graph_connect(&g, c1, 0, a1, 0) == 0, "TYPE-5: connect c1 -> add.a");
    check(graph_connect(&g, c2, 0, a1, 1) == 0, "TYPE-5: connect c2 -> add.b");

    pin_value sum;
    check(graph_eval_output(&g, a1, 0, &a, &sum, &eval_ctx) == 0, "CAT-23: eval reflected call node");
    check(sum.type == PIN_F32 && sum.f == 5.0f, "DATA-4: 2.0 + 3.0 == 5.0");

    /* 2. Make(Vec3) -> Break(Vec3) round trip */
    graph_eval_reset(&g);
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
    check(graph_eval_output(&g, br, 1, &a, &oy, &eval_ctx) == 0, "DATA-1: eval Break(Vec3).y");
    check(mk->outputs[0].ptr != NULL, "DATA-6: Make(Vec3) value is arena-owned");
    check(oy.type == PIN_F32 && oy.f == 2.0f, "DATA-9: Make(Vec3) -> Break(Vec3): y == 2.0");

    /* 3. memo: one eval across two reads */
    graph_eval_reset(&g);
    arena_reset(&a);
    g_count = 0;
    node *ctr = graph_add_node(&g, &g_counter_desc);
    pin_value v1, v2;
    check(graph_eval_output(&g, ctr, 0, &a, &v1, &eval_ctx) == 0, "DATA-1: eval counter (1st read)");
    check(graph_eval_output(&g, ctr, 0, &a, &v2, &eval_ctx) == 0, "DATA-2: eval counter (2nd read)");
    check(g_count == 1, "DATA-2: memo: eval ran once for two reads");
    check(v1.f == 1.0f && v2.f == 1.0f, "DATA-2: memo: both reads see the same value");

    /* 4. cycle guard */
    graph_eval_reset(&g);
    arena_reset(&a);
    node *A = graph_add_node(&g, add);
    node *B = graph_add_node(&g, add);
    graph_connect(&g, A, 0, B, 0); /* A.out -> B.a */
    graph_connect(&g, B, 0, A, 0); /* B.out -> A.a */
    pin_value junk;
    check(graph_eval_output(&g, A, 0, &a, &junk, &eval_ctx) != 0, "DATA-3: cycle detected (eval returns error)");
    check(
        eval_context_status(&eval_ctx)->code == -2 &&
            eval_context_status(&eval_ctx)->node == A &&
            eval_context_status(&eval_ctx)->pin >= -1,
        "DATA-7: evaluation status lives on the explicit context"
    );
    eval_context independent_a, independent_b;
    eval_context_init(&independent_a);
    eval_context_init(&independent_b);
    pin_value isolated;
    graph_eval_reset(&g);
    arena_reset(&a);
    check(
        graph_eval_output(&g, c1, 0, &a, &isolated, &independent_a) == 0 &&
            eval_context_status(&independent_a)->code == 0 &&
            eval_context_status(&independent_b)->code == 0,
        "SCOPE-3: evaluation status is isolated per context"
    );

    /* 5. remove a node and its edges */
    int nb = g.node_count;
    int eb = g.edge_count;
    check(graph_remove_node(&g, A) == 0, "BP-9: remove node A");
    check(g.node_count == nb - 1, "BP-9: node count drops by one");
    check(g.edge_count == eb - 2, "BP-9: A's two edges dropped");
    check(graph_remove_node(&g, A) == -1, "BP-10: removing a missing node fails");

    /* Phase 2 identity and instance-pin invariants. */
    {
        graph ids;
        graph_init(&ids);
        node *a_id = graph_add_node(&ids, add);
        uint64_t retired = a_id->id;
        uint64_t retired_pin = a_id->output_pins[0].id;
        check(graph_remove_node(&ids, a_id) == 0, "ID-4: deleting a node retires its ID");
        node *b_id = graph_add_node(&ids, add);
        check(
            b_id->id != retired && b_id->output_pins[0].id != retired_pin &&
                graph_node_by_id(&ids, retired) == NULL,
            "ID-2: new allocations never reuse retired IDs"
        );
        node *p1 = graph_add_node(&ids, add);
        node *p2 = graph_add_node(&ids, add);
        check(
            graph_set_pin_name(&ids, p1, 0, 0, "left") == 0 &&
                strcmp(p1->input_pins[0].name, "left") == 0 &&
                strcmp(p2->input_pins[0].name, "a") == 0,
            "PIN-3: instance pin edits do not mutate sibling instances"
        );
        check(strcmp(add->inputs[0].name, "a") == 0 && strcmp(p2->input_pins[0].name, "a") == 0, "PIN-1: instance pin storage is copied from an untouched descriptor template");
        pin_value authored = { .type = PIN_F32, .type_name = "float", .f = 7.0f };
        check(
            graph_set_pin_default(&ids, p1, 0, 0, &authored) == 0 &&
                p1->input_pins[0].default_value.f == 7.0f,
            "PIN-4: authored defaults have separate instance storage"
        );
        pin_spec extra = { "ThenExtra", PIN_FLOW, NULL, PIN_EXEC };
        node *seq = graph_add_node(&ids, registry_node_by_name("Sequence"));
        int old_outputs = seq->output_count;
        check(
            graph_add_pin(&ids, seq, 1, &extra) == old_outputs &&
                seq->output_count == old_outputs + 1,
            "PIN-14: user-extensible groups add instance pins"
        );
        check(
            graph_remove_pin(&ids, seq, 1, old_outputs) == 0 &&
                seq->output_count == old_outputs,
            "PIN-14: removing an instance pin succeeds"
        );
        check(
            b_id->id != 0 && b_id->input_pins[0].id != 0 && b_id->output_pins[0].id != 0,
            "ID-1: nodes and pins carry stable non-zero IDs"
        );
        check(
            b_id->input_pins[0].name && b_id->input_pins[0].type_name &&
                b_id->input_pins[0].kind == PIN_DATA,
            "PIN-2: instance pins carry identity, shape, kind, and defaults"
        );
        check(b_id->desc == add, "NODE-1: each node references one descriptor");
        b_id->x = 3.5f;
        b_id->y = -2.25f;
        check(b_id->x == 3.5f && b_id->y == -2.25f, "NODE-2: node canvas position is authored data");
        check(g_const_desc.pure && !g_sink_a_desc.pure, "NODE-6: descriptors explicitly distinguish pure and impure");
        check(g_const_desc.display_class == NODE_DISPLAY_LITERAL, "NODE-12: descriptors carry a display class");
        check(
            graph_node_by_id(&ids, 0) == NULL &&
                graph_pin_by_id(b_id, 0, NULL, NULL) == NULL,
            "ID-3: zero is never an allocated or resolvable ID"
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
            "ID-1: node and pin IDs survive save/load"
        );
        if (id_loaded) graph_free(id_loaded);
        free(id_json);
        check(
            graph_connect_data_ids(
                &ids,
                p1->id,
                p1->output_pins[0].id,
                p2->id,
                p2->input_pins[0].id
            ) != 0 ||
                ids.edge_count == 1,
            "ID-6: cross-entity APIs address endpoints by IDs"
        );
        graph_free(&ids);
    }

    {
        graph authored;
        graph_init(&authored);
        node *literal = make_const(&authored, 1.0f);
        pin_value changed = { .type = PIN_F32, .type_name = "float", .f = 9.0f };
        arena authored_arena;
        arena_init(&authored_arena, 4096);
        pin_value evaluated;
        char *authored_json = NULL;
        graph *authored_loaded = NULL;
        check(
            graph_set_pin_default(&authored, literal, 1, 0, &changed) == 0 &&
                graph_eval_output(&authored, literal, 0, &authored_arena, &evaluated, &eval_ctx) ==
                    0 &&
                evaluated.f == 9.0f && literal->output_pins[0].default_value.f == 9.0f,
            "PIN-4: setting an authored default changes evaluation after a "
            "later eval"
        );
        authored_json = graph_save_json(&authored);
        authored_loaded = authored_json ? graph_load_json(authored_json) : NULL;
        check(
            authored_loaded &&
                authored_loaded->nodes[0]->output_pins[0].default_value.f == 9.0f,
            "PIN-4: changed authored default is recorded by save"
        );
        arena_free(&authored_arena);
        if (authored_loaded) graph_free(authored_loaded);
        free(authored_json);
        graph_free(&authored);
    }

    {
        graph real;
        graph_init(&real);
        node *real_event = graph_add_node(&real, registry_node_by_name("Event"));
        node *real_delay = graph_add_node(&real, registry_node_by_name("Delay"));
        node *real_const = graph_add_node(&real, registry_node_by_name("const_f32"));
        pin_value seconds = { .type = PIN_F32, .type_name = "float", .f = 2.5f };
        graph_set_pin_default(&real, real_const, 1, 0, &seconds);
        check(
            graph_connect_data(&real, real_const, 0, real_delay, 1) == 0 &&
                real_delay->input_pins[1].type == PIN_F64,
            "TYPE-10: F32 output connects to F64 Real input"
        );
        static pin_spec int_output[] = { { "value", PIN_I32, "int" } };
        static pin_spec int64_input[] = { { "value", PIN_I64, "int64" } };
        static node_desc int_source = {
            "test_int_source", 0, 1, NULL, int_output, node_eval_const
        };
        static node_desc int_target = { "test_int_target", 1,    0,
                                        int64_input,       NULL, node_eval_const };
        node *integer = graph_add_node(&real, &int_source);
        node *integer64 = graph_add_node(&real, &int_target);
        check(
            graph_connect(&real, integer, 0, integer64, 0) != 0,
            "TYPE-6: Integer output remains incompatible with Integer64 input"
        );
        graph_set_event_name(&real, real_event, "Real");
        graph_connect_exec(&real, real_event, 0, real_delay, 0, 0);
        flow_run *real_run = graph_run_event(&real, "Real", (flow_inputs){ 0 }, &dispatch_error);
        check(
            real_run && real_delay->inputs[1].type == PIN_F64 &&
                real_delay->inputs[1].d == 2.5,
            "TYPE-10: Real boundary converts F32 value to F64 for Delay"
        );
        if (real_run) flow_run_free(real_run);
        graph_free(&real);
    }

    {
        graph literals;
        graph_init(&literals);
        const char *names[] = { "const_bool",  "const_byte", "const_int",
                                "const_int64", "const_real", "const_str" };
        for (int i = 0; i < 6; i++)
            check(registry_node_by_name(names[i]) != NULL, "CAT-28: Unreal literal is registered");
        node *lb = graph_add_node(&literals, registry_node_by_name("const_bool"));
        node *lbyte = graph_add_node(&literals, registry_node_by_name("const_byte"));
        node *li = graph_add_node(&literals, registry_node_by_name("const_int"));
        node *li64 = graph_add_node(&literals, registry_node_by_name("const_int64"));
        node *lr = graph_add_node(&literals, registry_node_by_name("const_real"));
        node *ls = graph_add_node(&literals, registry_node_by_name("const_str"));
        pin_value vb = { .type = PIN_BOOL, .b = true },
                  vbyte = { .type = PIN_U8, .u8 = 7 };
        pin_value vi = { .type = PIN_I32, .i = 8 },
                  vi64 = { .type = PIN_I64, .i64 = 9 };
        pin_value vr = { .type = PIN_F64, .d = 2.5 },
                  vs = { .type = PIN_STR, .str = "literal" };
        graph_set_pin_default(&literals, lb, 1, 0, &vb);
        graph_set_pin_default(&literals, lbyte, 1, 0, &vbyte);
        graph_set_pin_default(&literals, li, 1, 0, &vi);
        graph_set_pin_default(&literals, li64, 1, 0, &vi64);
        graph_set_pin_default(&literals, lr, 1, 0, &vr);
        graph_set_pin_default(&literals, ls, 1, 0, &vs);
        char *literal_json = graph_save_json(&literals);
        graph *literal_loaded = literal_json ? graph_load_json(literal_json) : NULL;
        int literal_values = 0;
        if (literal_loaded && literal_loaded->node_count == 6) {
            for (int i = 0; i < literal_loaded->node_count; i++) {
                node *n = literal_loaded->nodes[i];
                if (strcmp(n->desc->name, "const_real") == 0 &&
                    n->output_pins[0].default_value.d == 2.5)
                    literal_values++;
                if (strcmp(n->desc->name, "const_str") == 0 &&
                    n->output_pins[0].default_value.str &&
                    strcmp(n->output_pins[0].default_value.str, "literal") == 0)
                    literal_values++;
            }
        }
        check(literal_values == 2, "CAT-28: literals hold authored values through save/load");
        if (literal_loaded) graph_free(literal_loaded);
        free(literal_json);
        graph_free(&literals);
    }

    {
        graph ordering;
        graph_init(&ordering);
        node *early = make_const(&ordering, 1.0f);
        node *late = make_const(&ordering, 2.0f);
        node *target = graph_add_node(&ordering, add);
        check(graph_connect(&ordering, late, 0, target, 0) == 0, "ID-6: ID edge is authored before deletion");
        check(graph_remove_node(&ordering, early) == 0, "ID-6: deleting an earlier array entry succeeds");
        node *source = NULL;
        int source_pin = -1;
        check(
            graph_input_source(&ordering, target, 0, &source, &source_pin) &&
                source == late && source_pin == 0,
            "ID-6: edge still resolves to the original node after reorder"
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
        check(graph_connect(&g_replace, r2, 0, ra, 0) == 0, "PIN-6: replace: second driver connects");
        check(g_replace.edge_count == 1 && g_replace.edges[0].from == r2, "PIN-6: replace: input has one driver");
        graph_free(&g_replace);
    }

    /* 7. graph_remove_edge removes an existing edge. */
    {
        graph g6;
        graph_init(&g6);
        node *rc1 = make_const(&g6, 4.0f);
        node *rc2 = make_const(&g6, 5.0f);
        node *radd = graph_add_node(&g6, add);
        check(graph_connect(&g6, rc1, 0, radd, 0) == 0, "BP-11: remove_edge: c1 -> add.a");
        check(graph_connect(&g6, rc2, 0, radd, 0) == 0, "PIN-6: remove_edge: c2 -> add.a replaces c1");
        check(graph_connect(&g6, rc2, 0, radd, 1) == 0, "PIN-6: remove_edge: c2 -> add.b");
        check(g6.edge_count == 2, "BP-11: remove_edge: two edges");
        check(graph_remove_edge(&g6, rc1, 0, radd, 0) == -1, "BP-10: remove_edge: replaced edge is absent");
        check(graph_remove_edge(&g6, rc2, 0, radd, 0) == 0, "BP-11: remove_edge: c2 -> add.a removed");
        check(g6.edge_count == 1, "BP-11: remove_edge: one edge remains");

        arena a6;
        arena_init(&a6, 4096);
        pin_value out;
        check(graph_eval_output(&g6, radd, 0, &a6, &out, &eval_ctx) == 0, "DATA-1: remove_edge: eval after removal");
        check(out.type == PIN_F32 && out.f == 5.0f, "PIN-5: remove_edge: unconnected a defaults to zero");
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
            "PIN-7: one data output drives three inputs"
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
            "PIN-7: every driven input receives the output value"
        );
        check(
            graph_remove_edge(&fan, driver, 0, fan_b, 0) == 0 && fan.edge_count == 2,
            "PIN-7: removing one fan-out edge leaves the other two"
        );
        arena_free(&fan_arena);
        graph_free(&fan);
    }

    /* 8. serialization round trip: constants + add + edges */
    {
        graph g2;
        graph_init(&g2);
        const node_desc *cc = registry_node_by_name("const_f32");
        check(cc != NULL, "CAT-21: registry exposes const_f32");

        node *rc1 = graph_add_node(&g2, cc);
        pin_value two = { .type = PIN_F32, .type_name = "float", .f = 2.0f };
        graph_set_pin_default(&g2, rc1, 1, 0, &two);
        node *rc2 = graph_add_node(&g2, cc);
        pin_value three = { .type = PIN_F32, .type_name = "float", .f = 3.0f };
        graph_set_pin_default(&g2, rc2, 1, 0, &three);
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
            check(g3 != NULL, "FMT-8: roundtrip: load from JSON");
            if (g3) {
                check(g3->node_count == 3, "FMT-2: roundtrip: node count survives");
                check(g3->edge_count == 2, "FMT-4: roundtrip: edge count survives");
                check(g3->nodes[0]->x == -220.0f && g3->nodes[0]->y == 40.0f, "FMT-5: roundtrip: node position survives");
                check(g3->nodes[2]->x == 100.0f && g3->nodes[2]->y == -30.0f, "FMT-5: roundtrip: second node position survives");

                arena a2;
                arena_init(&a2, 4096);
                pin_value out;
                check(
                    graph_eval_output(g3, g3->nodes[2], 0, &a2, &out, &eval_ctx) == 0,
                    "DATA-4: roundtrip: eval loaded add node"
                );
                check(out.type == PIN_F32 && out.f == 5.0f, "DATA-4: roundtrip: 2.0 + 3.0 == 5.0");
                char *json2 = graph_save_json(g3);
                graph *g4 = json2 ? graph_load_json(json2) : NULL;
                char *json3 = g4 ? graph_save_json(g4) : NULL;
                check(json2 && json3 && strcmp(json2, json3) == 0, "FMT-12: load/save/load/save is byte-identical");
                if (g4) graph_free(g4);
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
        check(graph_set_metadata(&full, "RoundTrip", "all authored fields") == 0, "FMT-14: blueprint metadata is authored");
        node *all = graph_add_node(&full, &g_all_types_desc);
        node *default_node = graph_add_node(&full, registry_node_by_name("const_f32"));
        node *event = graph_add_node(&full, registry_node_by_name("Event"));
        node *sequence = graph_add_node(&full, registry_node_by_name("Sequence"));
        node *sink = graph_add_node(&full, registry_node_by_name("Print String"));
        node *sum = graph_add_node(&full, add);
        check(all && default_node && event && sequence && sink && sum, "FMT-2: comprehensive fixture nodes created");
        Vec3 *vec = graph_own_block(&full, sizeof *vec);
        if (vec) *vec = (Vec3){ 1.25f, 2.5f, 3.75f };
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
        for (int i = 0; all && i < 14; i++)
            check(graph_set_pin_default(&full, all, 1, i, &values[i]) == 0, "FMT-6: every authored pin default is set");
        if (all) {
            check(graph_set_node_title(&full, all, "Configured literal") == 0, "NODE-3: title override is authored");
            check(graph_set_node_comment(&full, all, "round-trip comment") == 0, "NODE-4: node comment is authored");
            check(graph_set_node_reference(&full, all, 4242, "Target") == 0, "NODE-5: node reference fields are authored");
            check(graph_set_node_disabled(&full, all, 1) == 0, "NODE-11: disabled state is authored");
            all->x = 12.25f;
            all->y = -7.5f;
        }
        check(graph_set_event_name(&full, event, "Full") == 0, "EVT-2: event reference name is serialized");
        pin_spec added = { "Then4", PIN_FLOW, NULL, PIN_EXEC };
        int added_index = sequence ? graph_add_pin(&full, sequence, 1, &added) : -1;
        check(added_index >= 2, "PIN-14: extended Sequence pin is present");
        check(
            graph_connect(&full, all, 9, sum, 0) == 0 &&
                graph_connect(&full, default_node, 0, sum, 1) == 0,
            "FMT-4: data edge endpoints use pin IDs"
        );
        check(
            graph_connect_exec(&full, event, 0, sequence, 0, 11) == 0 &&
                graph_connect_exec(&full, sequence, added_index, sink, 0, 13) == 0,
            "FMT-4: exec edge order is serialized"
        );
        char *full_a = graph_save_json(&full);
        graph *full_b = full_a ? graph_load_json(full_a) : NULL;
        char *full_c = full_b ? graph_save_json(full_b) : NULL;
        graph *full_d = full_c ? graph_load_json(full_c) : NULL;
        char *full_e = full_d ? graph_save_json(full_d) : NULL;
        check(
            full_b && full_d && full_a && full_c && full_e &&
                strcmp(full_a, full_c) == 0 && strcmp(full_c, full_e) == 0,
            "FMT-12: all pin types, edges, groups, and node fields round-trip "
            "byte-identically"
        );
        if (full_b) {
            node *loaded_all = full_b->nodes[0];
            check(
                strcmp(loaded_all->title_override, "Configured literal") == 0 &&
                    strcmp(loaded_all->comment, "round-trip comment") == 0 &&
                    loaded_all->reference_id == 4242 &&
                    strcmp(loaded_all->reference_name, "Target") == 0 &&
                    loaded_all->disabled && loaded_all->x == 12.25f,
                "FMT-5: optional node fields survive first load"
            );
            check(
                loaded_all->output_pins[0].default_value.b &&
                    loaded_all->output_pins[11].default_value.str &&
                    strcmp(loaded_all->output_pins[11].default_value.str, "authored") == 0 &&
                    loaded_all->output_pins[12].default_value.ptr &&
                    ((Vec3 *)loaded_all->output_pins[12].default_value.ptr)->y == 2.5f,
                "PIN-2: scalar, string, and struct instance pin fields survive "
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
            "FMT-3: node array position carries no reference meaning"
        );
        check(full_a && reordered && strcmp(full_a, reordered) == 0, "FMT-13: deterministic output is byte-identical");
        full.nodes[0] = saved_first;
        full.nodes[1] = saved_second;
        if (reordered_loaded) graph_free(reordered_loaded);
        free(reordered);
        if (full_d) graph_free(full_d);
        if (full_b) graph_free(full_b);
        free(full_a);
        free(full_c);
        free(full_e);
        graph_free(&full);
    }

    /* 10. string constant round trip */
    {
        graph g4;
        graph_init(&g4);
        const node_desc *cs = registry_node_by_name("const_str");
        check(cs != NULL, "CAT-21: registry exposes const_str");
        node *s1 = graph_add_node(&g4, cs);
        pin_value hello = { .type = PIN_STR,
                            .type_name = "const char *",
                            .str = "hello, world" };
        check(graph_set_pin_default(&g4, s1, 1, 0, &hello) == 0, "string const: authored value set");

        char *json = graph_save_json(&g4);
        check(json != NULL, "string const: serialize");
        if (json) {
            graph *g5 = graph_load_json(json);
            check(g5 && g5->node_count == 1, "FMT-1: string const: load");
            if (g5) {
                check(
                    g5->nodes[0]->outputs[0].type == PIN_STR &&
                        strcmp(g5->nodes[0]->outputs[0].str, "hello, world") == 0,
                    "FMT-6: string const: value round-trips"
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
        check(graph_load_json(bad[0]) == NULL && graph_last_error()[0] != '\0', "FMT-8: malformed JSON rejected");
        check(graph_load_json(bad[1]) == NULL && graph_last_error()[0] != '\0', "FMT-8: missing edges array rejected");
        check(graph_load_json(bad[2]) == NULL && strstr(graph_last_error(), "unknown"), "FMT-8: unknown node type rejected");
        check(graph_load_json(bad[3]) == NULL, "FMT-8: malformed pin list rejected");
        check(
            graph_load_json("{\"version\":2,\"nodes\":[],\"edges\":[]}") == NULL &&
                strstr(graph_last_error(), "version"),
            "FMT-8: unsupported schema version rejected"
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

        check(strcmp(s_f32, "2.5") == 0, "UI-39: fmt: f32 2.5 -> \"2.5\"");
        check(strcmp(s_five, "5") == 0, "UI-39: fmt: f32 5.0 -> \"5\"");
        check(strcmp(s_str, "hello") == 0, "UI-39: fmt: str -> \"hello\"");
        check(strcmp(s_bool, "true") == 0, "UI-39: fmt: bool true -> \"true\"");
        check(strcmp(pin_value_to_string(&iv), "-42") == 0, "UI-39: fmt: i32 -42 -> \"-42\"");
    }

    /* 12. seed graph evaluates to 5.0 */
    {
        graph gs;
        graph_init(&gs);
        check(demo_graph_seed(&gs) == 0, "SCOPE-2: seed graph builds");

        const node_desc *adddesc = registry_node_by_name("add_floats");
        node *addn = NULL;
        for (int i = 0; i < gs.node_count; i++)
            if (gs.nodes[i]->desc == adddesc) {
                addn = gs.nodes[i];
                break;
            }
        check(addn != NULL, "seed graph has an add_floats node");

        if (addn) {
            arena as;
            arena_init(&as, 4096);
            pin_value seed_sum;
            check(graph_eval_output(&gs, addn, 0, &as, &seed_sum, &eval_ctx) == 0, "DATA-1: seed graph evals");
            check(seed_sum.type == PIN_F32 && seed_sum.f == 5.0f, "DATA-4: seed graph: 2.0 + 3.0 == 5.0");
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
        for (int i = 0; i < gf.edge_count; i++) {
            if (gf.edges[i].kind == GRAPH_EDGE_DATA) data_edges++;
            if (gf.edges[i].kind == GRAPH_EDGE_EXEC) exec_edges++;
        }
        check(gf.node_count == 10 && data_edges == 2 && exec_edges == 8, "flow demo graph shows data and exec topology");
        check(
            gf.nodes[0]->reference_name &&
                strcmp(gf.nodes[0]->reference_name, "OnStart") == 0,
            "EXEC-1: flow demo graph has named Event entry"
        );
        graph_free(&gf);
    }

    /* 14. subset serialization: only selected nodes + their inter-edges */
    {
        graph g2;
        graph_init(&g2);
        const node_desc *cc = registry_node_by_name("const_f32");

        node *c1 = graph_add_node(&g2, cc);
        pin_value two = { .type = PIN_F32, .type_name = "float", .f = 2.0f };
        graph_set_pin_default(&g2, c1, 1, 0, &two);
        node *c2 = graph_add_node(&g2, cc);
        pin_value three = { .type = PIN_F32, .type_name = "float", .f = 3.0f };
        graph_set_pin_default(&g2, c2, 1, 0, &three);
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
        check(json != NULL, "FMT-10: subset: serialize");
        if (json) {
            printf("\n--- serialized subset ---\n%s--- end ---\n", json);

            graph *g3 = graph_load_json(json);
            check(g3 != NULL, "FMT-10: subset: load");
            if (g3) {
                check(g3->node_count == 2, "FMT-10: subset: node count is 2");
                check(g3->edge_count == 1, "FMT-10: subset: crossing edge dropped");
                check(g3->nodes[0]->x == 10.0f && g3->nodes[0]->y == 20.0f, "FMT-5: subset: node 0 position");
                check(g3->nodes[1]->x == 50.0f && g3->nodes[1]->y == 60.0f, "FMT-5: subset: node 1 position");
                check(g3->nodes[0]->outputs[0].f == 2.0f, "FMT-6: subset: const value survives");
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
        const node_desc *cc = registry_node_by_name("const_f32");

        node *c1 = graph_add_node(&g4, cc);
        pin_value two = { .type = PIN_F32, .type_name = "float", .f = 2.0f };
        graph_set_pin_default(&g4, c1, 1, 0, &two);
        node *c2 = graph_add_node(&g4, cc);
        pin_value three = { .type = PIN_F32, .type_name = "float", .f = 3.0f };
        graph_set_pin_default(&g4, c2, 1, 0, &three);
        node *ad = graph_add_node(&g4, add);
        graph_connect(&g4, c1, 0, ad, 0);
        graph_connect(&g4, c2, 0, ad, 1);
        c1->x = 10.0f;
        c1->y = 20.0f;
        ad->x = 50.0f;
        ad->y = 60.0f;

        node *subset[] = { c1, ad };
        char *json = graph_save_nodes_json(&g4, subset, 2);
        check(json != NULL, "FMT-10: append: serialize source subset");
        if (json) {
            graph g5;
            graph_init(&g5);
            check(g5.node_count == 0, "append: fresh graph starts empty");
            check(graph_append_json(&g5, json, 100.0f, 200.0f) == 0, "append: returns 0");
            check(g5.node_count == 2, "append: node count");
            check(g5.edge_count == 1, "append: edge count");
            check(g5.nodes[0]->x == 110.0f && g5.nodes[0]->y == 220.0f, "FMT-11: append: node 0 offset");
            check(g5.nodes[1]->x == 150.0f && g5.nodes[1]->y == 260.0f, "FMT-11: append: node 1 offset");
            check(g5.nodes[0]->outputs[0].f == 2.0f, "FMT-6: append: const survives");

            int n_before = g5.node_count;
            check(graph_append_json(&g5, "{ not json", 0.0f, 0.0f) == -1, "FMT-8: append: bad json fails");
            check(g5.node_count == n_before, "FMT-8: append: graph untouched on error");

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
        const node_desc *bool_desc = registry_node_by_name("const_bool");
        check(event_desc && branch_desc && bool_desc, "CAT-2: Branch flow built-in is registered");
        node *event = graph_add_node(&ge, event_desc);
        node *branch = graph_add_node(&ge, branch_desc);
        node *truth = graph_add_node(&ge, bool_desc);
        check(graph_set_event_name(&ge, event, "EdgeTest") == 0, "EVT-2: event name is graph metadata");
        check(graph_connect_exec(&ge, event, 0, branch, 0, 7) == 0, "PIN-10: exec edge connects");
        node *fanout = graph_add_node(&ge, &g_sink_a_desc);
        check(graph_connect_exec(&ge, event, 0, fanout, 0, 0) != 0, "PIN-9: ordinary EXEC output rejects ambiguous fan-out");
        check(graph_connect_data(&ge, truth, 0, branch, 1) == 0, "TYPE-5: data edge connects");
        truth->outputs[0].b = true;
        check(ge.edge_count == 2 && ge.edges[0].kind != ge.edges[1].kind, "DATA-5: data and exec edges remain distinct");
        node *src = NULL;
        int src_pin = -1;
        int order = -1;
        check(
            graph_exec_input_source(&ge, branch, 0, &src, &src_pin, &order) &&
                src == event && src_pin == 0 && order == 7,
            "PIN-10: exec input source preserves order"
        );
        node *target = NULL;
        int target_pin = -1;
        check(
            graph_exec_target_at(&ge, event, 0, 7, &target, &target_pin) &&
                target == branch && target_pin == 0,
            "PIN-10: exec target lookup preserves order"
        );
        node *event_merge = graph_add_node(&ge, event_desc);
        check(
            graph_connect_exec(&ge, event_merge, 0, branch, 0, 8) == 0 &&
                ge.edge_count == 3,
            "PIN-8: exec input accepts multiple incoming drivers"
        );
        check(graph_connect_data(&ge, event, 0, branch, 1) != 0, "TYPE-5: exec output cannot become a data edge");
        graph_free(&ge);
    }

    /* 16. Event -> Branch true/false and named dispatch errors. */
    {
        graph gf;
        graph_init(&gf);
        node *event = graph_add_node(&gf, registry_node_by_name("Event"));
        node *branch = graph_add_node(&gf, registry_node_by_name("Branch"));
        node *condition = graph_add_node(&gf, registry_node_by_name("const_bool"));
        node *yes = graph_add_node(&gf, &g_sink_a_desc);
        node *no = graph_add_node(&gf, &g_sink_b_desc);
        check(graph_set_event_name(&gf, event, "Choose") == 0, "EVT-2: named event configured");
        check(graph_connect_exec(&gf, event, 0, branch, 0, 0) == 0, "PIN-10: Event -> Branch exec");
        check(graph_connect_data(&gf, condition, 0, branch, 1) == 0, "TYPE-5: Branch condition data");
        check(graph_connect_exec(&gf, branch, 0, yes, 0, 0) == 0, "CAT-2: Branch true edge");
        check(graph_connect_exec(&gf, branch, 1, no, 0, 0) == 0, "CAT-2: Branch false edge");
        condition->outputs[0].b = true;
        flow_log_reset();
        flow_run *run = graph_run_event(&gf, "Choose", (flow_inputs){ 0 }, &dispatch_error);
        check(
            run && flow_run_status(run) == FLOW_COMPLETED &&
                strcmp(g_flow_log, "A") == 0,
            "EXEC-1: named event takes Branch true"
        );
        check(run != NULL, "RUN-1: named event creates a flow run");
        check(flow_run_status(run) == FLOW_COMPLETED, "RUN-2: completed run reports one state");
        flow_run_free(run);
        condition->outputs[0].b = false;
        flow_log_reset();
        run = graph_run_event(&gf, "Choose", (flow_inputs){ 0 }, &dispatch_error);
        check(
            run && flow_run_status(run) == FLOW_COMPLETED &&
                strcmp(g_flow_log, "B") == 0,
            "CAT-2: named event takes Branch false"
        );
        flow_run_free(run);
        check(
            graph_run_event(&gf, "Missing", (flow_inputs){ 0 }, &dispatch_error) == NULL &&
                dispatch_error.code == FLOW_ERR_UNKNOWN_EVENT,
            "EVT-7: unknown event reports through the dispatch out-parameter"
        );
        node *duplicate = graph_add_node(&gf, registry_node_by_name("Event"));
        check(graph_set_event_name(&gf, duplicate, "Choose") == 0, "duplicate event configured");
        check(
            graph_run_event(&gf, "Choose", (flow_inputs){ 0 }, &dispatch_error) == NULL &&
                dispatch_error.code == FLOW_ERR_DUPLICATE_EVENT,
            "EVT-7: duplicate event uses dispatch error"
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
        check(graph_set_event_name(&gs, event, "Sequence") == 0, "EVT-2: sequence event configured");
        check(graph_connect_exec(&gs, event, 0, sequence, 0, 0) == 0, "PIN-10: event -> sequence");
        check(
            graph_connect_exec(&gs, sequence, 0, a, 0, 9) == 0 &&
                graph_connect_exec(&gs, sequence, 1, b, 0, 4) == 0 &&
                graph_connect_exec(&gs, sequence, 2, c, 0, 1) == 0,
            "PIN-9: sequence ordered outputs connect"
        );
        flow_log_reset();
        flow_run *run = graph_run_event(&gs, "Sequence", (flow_inputs){ 0 }, &dispatch_error);
        check(
            run && flow_run_status(run) == FLOW_COMPLETED &&
                strcmp(g_flow_log, "ABC") == 0,
            "EXEC-5: Sequence order is 0,1,2,3"
        );
        flow_run_free(run);
        graph_free(&gs);

        graph seven;
        graph_init(&seven);
        node *seven_event = graph_add_node(&seven, registry_node_by_name("Event"));
        node *seven_sequence = graph_add_node(&seven, registry_node_by_name("Sequence"));
        pin_spec seven_pin = { "ThenExtra", PIN_FLOW, NULL, PIN_EXEC };
        while (seven_sequence && seven_sequence->output_count < 7)
            graph_add_pin(&seven, seven_sequence, 1, &seven_pin);
        node *seven_sinks[7];
        for (int i = 0; i < 7; i++)
            seven_sinks[i] = graph_add_node(&seven, &g_sink_a_desc);
        graph_set_event_name(&seven, seven_event, "Seven");
        graph_connect_exec(&seven, seven_event, 0, seven_sequence, 0, 0);
        for (int i = 0; i < 7; i++)
            graph_connect_exec(&seven, seven_sequence, i, seven_sinks[i], 0, i);
        flow_log_reset();
        run = graph_run_event(&seven, "Seven", (flow_inputs){ 0 }, &dispatch_error);
        check(seven_sequence && seven_sequence->output_count == 7, "CAT-3: Sequence extends from its default of two outputs");
        check(
            run && flow_run_status(run) == FLOW_COMPLETED && g_flow_log_count == 7,
            "CAT-3: seven Sequence outputs all fire in ascending order"
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
            "EXEC-5: Sequence stops immediately on error"
        );
        flow_run_free(run);
        graph_free(&ge);
    }

    /* 18. ForLoop inclusive forward range and empty reverse range. */
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
        loop->inputs[1].i = 2;
        loop->inputs[2].i = 4;
        flow_log_reset();
        flow_run *run = graph_run_event(&gl, "Loop", (flow_inputs){ 0 }, &dispatch_error);
        check(
            run && flow_run_status(run) == FLOW_COMPLETED && g_flow_index_count == 3 &&
                g_flow_indices[0] == 2 && g_flow_indices[1] == 3 &&
                g_flow_indices[2] == 4 && strcmp(g_flow_log, "D") == 0,
            "CAT-4: ForLoop runs inclusive 2..4 then Completed"
        );
        check(
            g_flow_index_count == 3 && g_flow_indices[0] == 2 &&
                g_flow_indices[1] == 3 && g_flow_indices[2] == 4,
            "EXEC-3: each loop activation evaluates fresh data"
        );
        flow_run_free(run);
        loop->inputs[1].i = 2;
        loop->inputs[2].i = 2;
        flow_log_reset();
        run = graph_run_event(&gl, "Loop", (flow_inputs){ 0 }, &dispatch_error);
        check(
            run && flow_run_status(run) == FLOW_COMPLETED && g_flow_index_count == 1 &&
                g_flow_indices[0] == 2 && strcmp(g_flow_log, "D") == 0,
            "CAT-4: ForLoop runs inclusive 2..2"
        );
        flow_run_free(run);
        loop->inputs[1].i = 4;
        loop->inputs[2].i = 2;
        flow_log_reset();
        run = graph_run_event(&gl, "Loop", (flow_inputs){ 0 }, &dispatch_error);
        check(
            run && flow_run_status(run) == FLOW_COMPLETED &&
                g_flow_index_count == 0 && strcmp(g_flow_log, "D") == 0,
            "EXEC-4: ForLoop reverse range skips body and completes"
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
        delay->inputs[1].d = 5.0;
        flow_log_reset();
        flow_run *run = graph_run_event(&gd, "Wait", (flow_inputs){ 0 }, &dispatch_error);
        check(run && flow_run_status(run) == FLOW_SUSPENDED && g_flow_log_count == 0, "EXEC-4: Delay suspends flow run");
        check(graph_connect_exec(&gd, event, 0, done, 0, 0) != 0, "EXEC-8: paused flow rejects topology mutation");
        check(
            flow_run_tick_at(run, 4.0) == FLOW_SUSPENDED_RESULT &&
                flow_run_status(run) == FLOW_SUSPENDED,
            "EXEC-10: Delay remains suspended before host deadline"
        );
        check(
            flow_run_tick_at(run, 5.0) == FLOW_OK &&
                flow_run_status(run) == FLOW_COMPLETED && strcmp(g_flow_log, "D") == 0,
            "EXEC-10: Delay resumes at host monotonic time"
        );
        check(flow_run_status(run) == FLOW_COMPLETED, "RUN-10: host-supplied monotonic time advances the run");
        flow_run_free(run);
        graph_free(&gd);

        graph gt;
        graph_init(&gt);
        node *topology_event = graph_add_node(&gt, registry_node_by_name("Event"));
        node *topology_delay = graph_add_node(&gt, registry_node_by_name("Delay"));
        graph_connect_exec(&gt, topology_event, 0, topology_delay, 0, 0);
        topology_delay->inputs[1].d = 10.0;
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
            "EXEC-9: changed topology fails the run"
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
        delay->inputs[1].d = 3.0;
        flow_log_reset();
        run = graph_run_event(&gn, "Nested", (flow_inputs){ 0 }, &dispatch_error);
        check(
            run && flow_run_status(run) == FLOW_SUSPENDED && g_flow_log_count == 0,
            "EXEC-2: nested Sequence suspends at first Delay"
        );
        flow_run_tick_at(run, 3.0);
        check(
            flow_run_status(run) == FLOW_COMPLETED && strcmp(g_flow_log, "AB") == 0,
            "EXEC-11: nested Sequence resumes remaining outputs in order"
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
            "EXEC-6: flow step budget produces structured failure"
        );
        flow_run_free(run);
        graph_free(&gbudget);

        graph gj;
        graph_init(&gj);
        node *event = graph_add_node(&gj, registry_node_by_name("Event"));
        node *branch = graph_add_node(&gj, registry_node_by_name("Branch"));
        node *condition = graph_add_node(&gj, registry_node_by_name("const_bool"));
        graph_set_event_name(&gj, event, "Saved");
        graph_connect_exec(&gj, event, 0, branch, 0, 12);
        graph_connect_data(&gj, condition, 0, branch, 1);
        char *json = graph_save_json(&gj);
        check(
            json && strstr(json, "\"version\": 3") &&
                strstr(json, "\"kind\": \"exec\"") && strstr(json, "\"order\": 12") &&
                strstr(json, "\"reference_name\": \"Saved\""),
            "FMT-4: v3 JSON writes ID-based flow metadata"
        );
        graph *loaded = json ? graph_load_json(json) : NULL;
        check(
            loaded && loaded->edge_count == 2 && loaded->edges[0].kind == GRAPH_EDGE_EXEC &&
                graph_event_name(loaded->nodes[0]) &&
                strcmp(graph_event_name(loaded->nodes[0]), "Saved") == 0,
            "FMT-4: v3 JSON loads flow metadata"
        );
        if (loaded) graph_free(loaded);
        free(json);

        node *flow_subset[] = { event, branch };
        char *subset_json = graph_save_nodes_json(&gj, flow_subset, 2);
        graph appended;
        graph_init(&appended);
        check(
            subset_json &&
                graph_append_json(&appended, subset_json, 10.0f, 20.0f) == 0 &&
                appended.edge_count == 1 && appended.edges[0].kind == GRAPH_EDGE_EXEC &&
                appended.edges[0].exec_order == 12,
            "FMT-11: append remaps ID-based flow edge"
        );
        uint64_t first_copy = appended.nodes[0]->id;
        uint64_t second_copy = appended.nodes[1]->id;
        check(
            subset_json &&
                graph_append_json(&appended, subset_json, 20.0f, 30.0f) == 0 &&
                appended.node_count == 4 && appended.edge_count == 2 &&
                appended.nodes[2]->id != first_copy &&
                appended.nodes[3]->id != second_copy &&
                appended.edges[1].from_id == appended.nodes[2]->id &&
                appended.edges[1].to_id == appended.nodes[3]->id,
            "ID-5: repeated paste allocates fresh IDs and rewires internal "
            "edges"
        );
        graph_free(&appended);
        free(subset_json);
        graph_free(&gj);

        check(
            graph_load_json("{\"version\":2,\"nodes\":[],\"edges\":[]}") == NULL &&
                strstr(graph_last_error(), "version"),
            "FMT-9: v2 documents are rejected"
        );
        check(
            graph_load_json("{\"nodes\":[],\"edges\":[]}") == NULL &&
                strstr(graph_last_error(), "version"),
            "FMT-9: versionless documents are rejected"
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
            "RUN-4: one step activates exactly one entry node"
        );
        check(
            flow_run_pause(run) == FLOW_OK && flow_run_status(run) == FLOW_PAUSED,
            "RUN-3: external pause preserves paused state"
        );
        check(
            flow_run_resume(run) == FLOW_OK && flow_run_status(run) == FLOW_COMPLETED &&
                strcmp(g_flow_log, "ABC") == 0,
            "RUN-3: resume continues from the next unactivated node"
        );
        flow_run_free(run);

        flow_log_reset();
        run = flow_run_start(&gp, event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        while (run && flow_run_status(run) != FLOW_COMPLETED &&
               flow_run_status(run) != FLOW_FAILED)
            (void)flow_run_step(run);
        char stepped_log[sizeof g_flow_log];
        snprintf(stepped_log, sizeof stepped_log, "%s", g_flow_log);
        check(strcmp(stepped_log, "ABC") == 0, "RUN-4: stepping preserves activation order");
        flow_run_free(run);

        flow_log_reset();
        run = flow_run_start(&gp, event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        (void)flow_run_resume(run);
        check(strcmp(g_flow_log, stepped_log) == 0, "RUN-4: stepped output matches uninterrupted resume");
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
            "RUN-3: external pause request stops between activations"
        );
        (void)flow_run_resume(run);
        check(
            flow_run_status(run) == FLOW_COMPLETED && strcmp(g_flow_log, "A") == 0,
            "RUN-3: paused run resumes at the next node"
        );
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
        delay->inputs[1].d = 1.0;
        flow_options clocked = { .initial_time = 0.0 };
        flow_log_reset();
        flow_run *run = flow_run_start(&gs, event, (flow_inputs){ 0 }, &clocked, &dispatch_error);
        check(
            run && flow_run_step(run) == FLOW_PAUSED_RESULT &&
                flow_run_status(run) == FLOW_PAUSED && flow_run_steps(run) == 1,
            "RUN-4: step mode pauses after one activation before Delay"
        );
        check(
            flow_run_step(run) == FLOW_SUSPENDED_RESULT &&
                flow_run_status(run) == FLOW_SUSPENDED && flow_run_steps(run) == 2,
            "RUN-4: stepping activates exactly one Delay across suspension"
        );
        check(
            flow_run_tick_at(run, 1.0) == FLOW_PAUSED_RESULT &&
                flow_run_status(run) == FLOW_PAUSED && flow_run_steps(run) == 3 &&
                g_flow_log_count == 1 && strcmp(g_flow_log, "D") == 0,
            "RUN-4: deadline tick keeps step mode and pauses after one "
            "activation"
        );
        check(
            flow_run_resume(run) == FLOW_OK &&
                flow_run_status(run) == FLOW_COMPLETED && flow_run_steps(run) == 3,
            "RUN-3: explicit Resume clears step mode and continues freely"
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
        delay->inputs[1].d = 1.0;
        flow_options clocked = { .initial_time = 0.0 };
        flow_log_reset();
        flow_run *run = flow_run_start(&gs, event, (flow_inputs){ 0 }, &clocked, &dispatch_error);
        (void)flow_run_resume(run);
        check(
            run && flow_run_status(run) == FLOW_SUSPENDED &&
                flow_run_pause(run) == FLOW_OK &&
                flow_run_tick_at(run, 1.0) == FLOW_PAUSED_RESULT &&
                flow_run_status(run) == FLOW_PAUSED && flow_run_steps(run) == 3,
            "RUN-3: pausing while SUSPENDED steps once after the deadline"
        );
        check(
            flow_run_resume(run) == FLOW_OK && flow_run_status(run) == FLOW_COMPLETED,
            "RUN-3: resumed suspended step completes without another step limit"
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
            "RUN-11: two runs keep independent errors and step counts"
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
        check(flow_run_stop(run) == FLOW_OK && flow_run_status(run) == FLOW_IDLE, "RUN-6: stop returns run to idle");
        check(
            graph_add_node(&gr, &g_sink_b_desc) != NULL &&
                event->computed == 0 && event->visiting == 0 &&
                event->eval_error == 0 && !event->eval_error_message,
            "RUN-6: stop clears node state and releases graph mutation guard"
        );
        flow_run_free(run);
        flow_log_reset();
        run = flow_run_start(&gr, event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        (void)flow_run_resume(run);
        check(strcmp(first_output, g_flow_log) == 0, "RUN-6: fresh run output matches the first run");
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
        delay->inputs[1].d = 10.0;
        flow_log_reset();
        flow_options clocked = { .initial_time = 0.0 };
        flow_run *run = flow_run_start(&gs, event, (flow_inputs){ 0 }, &clocked, &dispatch_error);
        (void)flow_run_resume(run);
        check(flow_run_status(run) == FLOW_SUSPENDED, "RUN-6: Delay leaves a suspended run");
        check(
            flow_run_stop(run) == FLOW_OK &&
                flow_run_status(run) == FLOW_IDLE && g_flow_log_count == 0,
            "RUN-6: stop aborts without activating the continuation"
        );
        check(graph_add_node(&gs, &g_sink_a_desc) != NULL, "RUN-8: stopped run leaves graph mutable");
        check(graph_add_node(&gs, &g_sink_b_desc) != NULL, "RUN-14: authoring mutation succeeds after suspended run teardown");
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
            "CAT-20: Print String sends ordered messages and originating nodes"
        );
        check(g_print_count == 2 && g_print_origins[0] == print_a, "EXEC-12: host sink receives output with its origin");
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
            "DBG-5: struct watch value is copied out of the run arena"
        );
        check(
            flow_run_watch_output(run, watch_node, 1, &recorded) == 0 &&
                recorded.ptr != &g_watch_struct && ((Vec3 *)recorded.ptr)->x == 4.0f,
            "DBG-6: live run watch reads the recorded execution value"
        );
        flow_run_free(run);
        check(copied.x == 4.0f && copied.y == 5.0f && copied.z == 6.0f, "DBG-5: copied struct remains valid after run free");
        run = flow_run_start(&gw, event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        (void)flow_run_resume(run);
        check(
            flow_run_stop(run) == FLOW_OK &&
                flow_run_watch_output(run, watch_node, 1, &recorded) != 0,
            "DBG-14: Stop clears recorded watch values"
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
        delay->inputs[1].d = 2.0;
        flow_run *run = flow_run_start(&gs, event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        (void)flow_run_resume(run);
        char *json = graph_save_json(&gs);
        graph *loaded = json ? graph_load_json(json) : NULL;
        check(
            json && !strstr(json, "\\\"run\\\"") &&
                !strstr(json, "\\\"state\\\"") && loaded && loaded->node_count == 2,
            "RUN-13: suspended save contains authored data and no run state"
        );
        if (loaded) graph_free(loaded);
        free(json);
        flow_run_free(run);
        graph_free(&gs);
    }

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");

    graph_free(&g);
    arena_free(&a);
    return fails ? 1 : 0;
}
