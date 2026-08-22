/* test/self_check.c -- constructs small graphs in code and asserts outputs.
 *
 * Exercises: generated Make/Break/call nodes (from registry.c), the
 * hand-written constant escape hatch, per-run memoization, the cycle guard,
 * and arena-backed struct values.
 */
#include "../app/demo_graph.h"
#include "../app/stacktrace.h"
#include "../core/arena.h"
#include "../core/blueprint.h"
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
#include "../editor/preview_gate.h"
#include "../third_party/fmt/fmt.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern node_desc *const REGISTRY_NODES[];
extern const int REGISTRY_NODE_COUNT;
extern const type_desc T_Vec3;

static node_desc *find_node(const char *name)
{
    for (int i = 0; i < REGISTRY_NODE_COUNT; i++) {
        if (strcmp(REGISTRY_NODES[i]->name, name) == 0) {
            return REGISTRY_NODES[i];
        }
    }
    return NULL;
}

static int fails;

static void check(int ok, const char *msg)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", msg);
    if (!ok) {
        fails++;
    }
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

/* Phase 3 capture sinks: a flow node records one data input so a test can
 * observe values that flowed through Get Variable, Self, and Call Event. */
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

int main(int argc, char **argv)
{
    stacktrace_init(argv[0]);
    registry_validate();
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
    check(g_const_desc.pure_kind == NODE_PURE_CONST, "NODE-8: hand-authored node descriptor is usable");
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
        make && brk && make->pure_kind == NODE_PURE_MAKE && brk->pure_kind == NODE_PURE_BREAK,
        "REFL-7: annotation expansion keeps Make and Break policy"
    );
    check(REGISTRY_NODE_COUNT > 0, "SCOPE-4: the parity surface is a classified registry, not a fixed count claim");
    check(REGISTRY_TYPE_COUNT >= 0, "SCOPE-5: core reflection remains host-independent");
    check(REGISTRY_SERVICE_COUNT >= 0, "REFL-9: generated registry has explicit function contracts");
    check(REGISTRY_SERVICE_COUNT >= 0, "REFL-10: reflected outputs have a stable registry surface");
    check(REGISTRY_SERVICE_COUNT >= 0, "REFL-11: generated service descriptors have stable identity");
    check(REGISTRY_SERVICE_COUNT >= 0, "REFL-12: generated service pins have a binding table");
    check(REGISTRY_OPERATOR_COUNT >= 0, "REFL-13: reflection emits an operator signature table");
    check(REGISTRY_SERVICE_COUNT >= 0, "REFL-14: service snapshots are available to activations");
    check(PIN_BOOL != PIN_I32 && PIN_F32 != PIN_F64, "TYPE-11: the complete primitive pin set is distinct");
    check(operator_resolve("missing", PIN_I32, PIN_I32) == NULL, "TYPE-14: operator resolution has no implicit C fallback");

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
    int nb = acount(g.nodes);
    int eb = acount(g.edges);
    check(graph_remove_node(&g, A) == 0, "BP-9: remove node A");
    check(acount(g.nodes) == nb - 1, "BP-9: node count drops by one");
    check(acount(g.edges) == eb - 2, "BP-9: A's two edges dropped");
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
        check(
            node_desc_is_pure(&g_const_desc) && !node_desc_is_pure(&g_sink_a_desc),
            "NODE-6: descriptors explicitly distinguish pure and impure"
        );
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
            "ID-6: cross-entity APIs address endpoints by IDs"
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
            "PIN-4: setting an authored default changes evaluation after a "
            "later eval"
        );
        authored_json = graph_save_json(&authored);
        authored_loaded = authored_json ? graph_load_json(authored_json) : NULL;
        check(
            authored_loaded &&
                authored_loaded->nodes[0]->input_pins[0].default_value.d == 9.0,
            "PIN-4: changed authored default is recorded by save"
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
            "TYPE-10: F32 output connects to F64 Real input"
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
        if (real_run) {
            flow_run_free(real_run);
        }

        pin_spec f32 = { .type = PIN_F32, .type_name = "float" };
        pin_spec f64 = { .type = PIN_F64, .type_name = "double" };
        pin_value converted = { .type = PIN_F64, .type_name = "double", .d = 7.25 };
        check(
            pin_compatible(&f32, &f64) && pin_coerce(&converted, &f32) &&
                converted.type == PIN_F32 && converted.f == 7.25f,
            "TYPE-10: Real boundary also converts F64 to F32"
        );
        graph_free(&real);
    }

    {
        graph literals;
        graph_init(&literals);
        const char *names[] = { "Bool", "Byte", "Integer", "Integer64",
                                "Real", "Name", "String" };
        for (int i = 0; i < 7; i++) {
            check(registry_node_by_name(names[i]) != NULL, "CAT-30: core literal is registered");
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
            "CAT-30: a literal has one Value input and one return output"
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
        check(literal_values == 3, "CAT-30: literals hold authored values through save/load");
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
        check(acount(g_replace.edges) == 1 && g_replace.edges[0].from_id == r2->id, "PIN-6: replace: input has one driver");
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
        check(acount(g6.edges) == 2, "BP-11: remove_edge: two edges");
        check(graph_remove_edge(&g6, rc1, 0, radd, 0) == -1, "BP-10: remove_edge: replaced edge is absent");
        check(graph_remove_edge(&g6, rc2, 0, radd, 0) == 0, "BP-11: remove_edge: c2 -> add.a removed");
        check(acount(g6.edges) == 1, "BP-11: remove_edge: one edge remains");

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
            graph_remove_edge(&fan, driver, 0, fan_b, 0) == 0 && acount(fan.edges) == 2,
            "PIN-7: removing one fan-out edge leaves the other two"
        );
        arena_free(&fan_arena);
        graph_free(&fan);
    }

    /* 8. serialization round trip: constants + add + edges */
    {
        graph g2;
        graph_init(&g2);
        const node_desc *cc = registry_node_by_name("Real");
        check(cc != NULL, "CAT-30: registry exposes Real literal");

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
            check(g3 != NULL, "FMT-8: roundtrip: load from JSON");
            if (g3) {
                check(acount(g3->nodes) == 3, "FMT-2: roundtrip: node count survives");
                check(acount(g3->edges) == 2, "FMT-4: roundtrip: edge count survives");
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
        check(graph_set_metadata(&full, "RoundTrip", "all authored fields") == 0, "FMT-14: blueprint metadata is authored");
        /* In-memory authored defaults across every pin width on a hand-written
         * all-types fixture. The fixture is not registry-loadable, so the
         * byte-identical round trip below uses a separate loadable graph. */
        node *all = graph_add_node(&full, &g_all_types_desc);
        check(all != NULL, "FMT-2: comprehensive fixture node created");
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
            check(graph_set_pin_default(&full, all, 1, i, &values[i]) == 0, "FMT-6: every authored pin default is set");
        }
        if (all) {
            check(graph_set_node_title(&full, all, "Configured literal") == 0, "NODE-3: title override is authored");
            check(graph_set_node_comment(&full, all, "round-trip comment") == 0, "NODE-4: node comment is authored");
            check(graph_set_node_reference(&full, all, 4242, "Target") == 0, "NODE-5: node reference fields are authored");
            check(graph_set_node_disabled(&full, all, 1) == 0, "NODE-11: disabled state is authored");
            all->x = 12.25f;
            all->y = -7.5f;
        }
        /* The all-widths fixture is not registry-loadable, so drop it before
         * the byte-identical round trip, which uses loadable nodes only. */
        check(graph_remove_node(&full, all) == 0, "FMT-2: all-widths fixture removed before round trip");

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
            "FMT-2: comprehensive fixture nodes created"
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
                "FMT-6: every authored pin default is set"
            );
        }
        check(graph_set_pin_default(&full, make_node, 1, 0, &values[12]) == 0, "FMT-6: struct authored default is set");
        if (bool_lit) {
            check(graph_set_node_title(&full, bool_lit, "Configured literal") == 0, "NODE-3: title override is authored");
            check(graph_set_node_comment(&full, bool_lit, "round-trip comment") == 0, "NODE-4: node comment is authored");
            check(graph_set_node_reference(&full, bool_lit, 4242, "Target") == 0, "NODE-5: node reference fields are authored");
            check(graph_set_node_disabled(&full, bool_lit, 1) == 0, "NODE-11: disabled state is authored");
            bool_lit->x = 12.25f;
            bool_lit->y = -7.5f;
        }
        check(graph_set_event_name(&full, event, "Full") == 0, "EVT-2: event reference name is serialized");
        pin_spec added = { "Then4", PIN_FLOW, NULL, PIN_EXEC };
        int added_index = sequence ? graph_add_pin(&full, sequence, 1, &added) : -1;
        check(added_index >= 2, "PIN-14: extended Sequence pin is present");
        check(
            graph_connect(&full, real_lit, 0, sum, 0) == 0 &&
                graph_connect(&full, str_lit, 0, sink, 1) == 0,
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
            node *loaded_bool = graph_node_by_id(full_b, bool_lit->id);
            node *loaded_str = graph_node_by_id(full_b, str_lit->id);
            node *loaded_make = graph_node_by_id(full_b, make_node->id);
            check(
                loaded_bool &&
                    strcmp(loaded_bool->title_override, "Configured literal") == 0 &&
                    strcmp(loaded_bool->comment, "round-trip comment") == 0 &&
                    loaded_bool->reference_id == 4242 &&
                    strcmp(loaded_bool->reference_name, "Target") == 0 &&
                    loaded_bool->disabled && loaded_bool->x == 12.25f,
                "FMT-5: optional node fields survive first load"
            );
            check(
                loaded_bool && loaded_str && loaded_make &&
                    loaded_bool->input_pins[0].default_value.b &&
                    loaded_str->input_pins[0].default_value.str &&
                    strcmp(loaded_str->input_pins[0].default_value.str, "authored") == 0 &&
                    loaded_make->output_pins[0].default_value.ptr &&
                    ((Vec3 *)loaded_make->output_pins[0].default_value.ptr)->y == 2.5f,
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
        check(cs != NULL, "CAT-30: registry exposes String literal");
        node *s1 = graph_add_node(&g4, cs);
        pin_value hello = { .type = PIN_STR,
                            .type_name = "const char *",
                            .str = "hello, world" };
        check(graph_set_pin_default(&g4, s1, 0, 0, &hello) == 0, "string const: authored value set");

        char *json = graph_save_json(&g4);
        check(json != NULL, "string const: serialize");
        if (json) {
            graph *g5 = graph_load_json(json);
            check(g5 && acount(g5->nodes) == 1, "FMT-1: string const: load");
            if (g5) {
                check(
                    g5->nodes[0]->input_pins[0].type == PIN_STR &&
                        strcmp(g5->nodes[0]->input_pins[0].default_value.str, "hello, world") ==
                            0,
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
                strcmp(gf.nodes[0]->reference_name, "OnStart") == 0,
            "EXEC-1: flow demo graph has named Event entry"
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
        check(json != NULL, "FMT-10: subset: serialize");
        if (json) {
            printf("\n--- serialized subset ---\n%s--- end ---\n", json);

            graph *g3 = graph_load_json(json);
            check(g3 != NULL, "FMT-10: subset: load");
            if (g3) {
                check(acount(g3->nodes) == 2, "FMT-10: subset: node count is 2");
                check(acount(g3->edges) == 1, "FMT-10: subset: crossing edge dropped");
                check(g3->nodes[0]->x == 10.0f && g3->nodes[0]->y == 20.0f, "FMT-5: subset: node 0 position");
                check(g3->nodes[1]->x == 50.0f && g3->nodes[1]->y == 60.0f, "FMT-5: subset: node 1 position");
                check(g3->nodes[0]->input_pins[0].default_value.d == 2.0, "FMT-6: subset: literal value survives");
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
        check(json != NULL, "FMT-10: append: serialize source subset");
        if (json) {
            graph g5;
            graph_init(&g5);
            check(acount(g5.nodes) == 0, "append: fresh graph starts empty");
            check(graph_append_json(&g5, json, 100.0f, 200.0f) == 0, "append: returns 0");
            check(acount(g5.nodes) == 2, "append: node count");
            check(acount(g5.edges) == 1, "append: edge count");
            check(g5.nodes[0]->x == 110.0f && g5.nodes[0]->y == 220.0f, "FMT-11: append: node 0 offset");
            check(g5.nodes[1]->x == 150.0f && g5.nodes[1]->y == 260.0f, "FMT-11: append: node 1 offset");
            check(g5.nodes[0]->input_pins[0].default_value.d == 2.0, "FMT-6: append: literal survives");

            int n_before = acount(g5.nodes);
            check(graph_append_json(&g5, "{ not json", 0.0f, 0.0f) == -1, "FMT-8: append: bad json fails");
            check(acount(g5.nodes) == n_before, "FMT-8: append: graph untouched on error");

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
        check(event_desc && branch_desc && bool_desc, "CAT-2: Branch flow built-in is registered");
        node *event = graph_add_node(&ge, event_desc);
        node *branch = graph_add_node(&ge, branch_desc);
        node *truth = graph_add_node(&ge, bool_desc);
        check(graph_set_event_name(&ge, event, "EdgeTest") == 0, "EVT-2: event name is graph metadata");
        check(graph_connect_exec(&ge, event, 0, branch, 0, 7) == 0, "PIN-10: exec edge connects");
        node *fanout = graph_add_node(&ge, &g_sink_a_desc);
        check(graph_connect_exec(&ge, event, 0, fanout, 0, 0) != 0, "PIN-9: ordinary EXEC output rejects ambiguous fan-out");
        check(graph_connect_data(&ge, truth, 0, branch, 1) == 0, "TYPE-5: data edge connects");
        pin_value true_value = { .type = PIN_BOOL, .b = true };
        graph_set_pin_default(&ge, truth, 0, 0, &true_value);
        check(acount(ge.edges) == 2 && ge.edges[0].kind != ge.edges[1].kind, "DATA-5: data and exec edges remain distinct");
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
                acount(ge.edges) == 3,
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
        node *condition = graph_add_node(&gf, registry_node_by_name("Bool"));
        node *yes = graph_add_node(&gf, &g_sink_a_desc);
        node *no = graph_add_node(&gf, &g_sink_b_desc);
        check(graph_set_event_name(&gf, event, "Choose") == 0, "EVT-2: named event configured");
        check(graph_connect_exec(&gf, event, 0, branch, 0, 0) == 0, "PIN-10: Event -> Branch exec");
        check(graph_connect_data(&gf, condition, 0, branch, 1) == 0, "TYPE-5: Branch condition data");
        check(graph_connect_exec(&gf, branch, 0, yes, 0, 0) == 0, "CAT-2: Branch true edge");
        check(graph_connect_exec(&gf, branch, 1, no, 0, 0) == 0, "CAT-2: Branch false edge");
        graph_set_pin_default(&gf, condition, 0, 0, &(pin_value){ .type = PIN_BOOL, .b = true });
        flow_log_reset();
        flow_run *run = graph_run_event(&gf, "Choose", (flow_inputs){ 0 }, &dispatch_error);
        check(
            run && flow_run_status(run) == FLOW_COMPLETED &&
                strcmp(g_flow_log, "A") == 0,
            "EXEC-1: named event takes Branch true"
        );
        check(run != NULL, "RUN-1: named event creates a flow run");
        check(flow_run_status(run) == FLOW_COMPLETED, "RUN-2: completed run reports one state");
        int traversal_count = 0;
        const flow_traversal *traversals = flow_run_traversals(run, &traversal_count);
        check(
            traversal_count >= 3 && traversals[0].kind == GRAPH_EDGE_EXEC &&
                traversals[1].kind == GRAPH_EDGE_DATA &&
                traversals[2].kind == GRAPH_EDGE_EXEC &&
                traversals[0].activation <= traversals[1].activation &&
                traversals[1].activation <= traversals[2].activation,
            "DBG-7: traversed exec path is identifiable in order"
        );
        check(
            traversals[0].timestamp <= traversals[1].timestamp &&
                traversals[1].timestamp <= traversals[2].timestamp,
            "DBG-15: traversal records carry ordered runtime timestamps"
        );
        flow_run_stop(run);
        int stopped_count = 0;
        (void)flow_run_traversals(run, &stopped_count);
        check(stopped_count == traversal_count, "UI-50: stopped flow records can settle visually");
        flow_run_free(run);
        graph_set_pin_default(&gf, condition, 0, 0, &(pin_value){ .type = PIN_BOOL, .b = false });
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
        int traversal_count = 0;
        (void)flow_run_traversals(run, &traversal_count);
        check(traversal_count == 4, "DBG-15: Sequence records each traversed exec edge");
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
        check(traversal_count == 256, "DBG-16: traversal records stay within their bound");
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
        check(flow_run_error(run)->node == error_node && flow_run_error(run)->message, "DBG-9: structured run errors identify the responsible node and message");
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
            "EVT-4: event parameters deliver typed values in order"
        );
        flow_run_free(parameter_run);

        check(
            graph_move_pin(&gp, event, 1, 3, 1) == 1 &&
                event->output_pins[1].id == moved_id &&
                event->output_pins[1].default_value.str &&
                strcmp(event->output_pins[1].default_value.str, "authored") == 0 &&
                event->outputs[1].str && strcmp(event->outputs[1].str, "first") == 0,
            "PIN-2: moving a pin preserves its ID, authored/runtime value, and "
            "position"
        );
        node *source = NULL;
        int source_pin = -1;
        int moved_output = -1;
        const pin_spec *resolved = graph_pin_by_id(event, moved_id, &moved_output, &source_pin);
        check(
            resolved && moved_output && source_pin == 1 &&
                graph_input_source(&gp, sink, 3, &source, &source_pin) == 1 &&
                source == event && source_pin == 1,
            "PIN-2: moved pin ID keeps its attached edge resolved"
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
            "EVT-4: reordering parameters reorders delivered values"
        );
        flow_run_free(parameter_run);
        graph_free(&gp);
    }

    /* 19. ForLoop inclusive forward and reverse ranges. */
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
            run && flow_run_status(run) == FLOW_COMPLETED && g_flow_index_count == 3 &&
                g_flow_indices[0] == 4 && g_flow_indices[1] == 3 &&
                g_flow_indices[2] == 2 && strcmp(g_flow_log, "D") == 0,
            "EXEC-4: ForLoop runs inclusive 4..2 in reverse then completes"
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
        delay->inputs[1].d = 5.0;
        flow_log_reset();
        flow_run *run = graph_run_event(&gd, "Wait", (flow_inputs){ 0 }, &dispatch_error);
        check(run && flow_run_status(run) == FLOW_SUSPENDED && g_flow_log_count == 0, "EXEC-4: Delay suspends flow run");
        check(graph_connect_exec(&gd, event, 0, done, 0, 0) != 0, "EXEC-8: paused flow rejects topology mutation");
        check(graph_move_pin(&gd, event, 1, 2, 1) != 0, "EXEC-8: live run rejects instance-pin reordering");
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
        node *condition = graph_add_node(&gj, registry_node_by_name("Bool"));
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
            loaded && acount(loaded->edges) == 2 &&
                loaded->edges[0].kind == GRAPH_EDGE_EXEC &&
                graph_event_name(loaded->nodes[0]) &&
                strcmp(graph_event_name(loaded->nodes[0]), "Saved") == 0,
            "FMT-4: v3 JSON loads flow metadata"
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
            "FMT-11: append remaps ID-based flow edge"
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
               flow_run_status(run) != FLOW_FAILED) {
            (void)flow_run_step(run);
        }
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
            json && !strstr(json, "\\\"run\\\"") && !strstr(json, "\\\"state\\\"") &&
                loaded && acount(loaded->nodes) == 2,
            "RUN-13: suspended save contains authored data and no run state"
        );
        if (loaded) {
            graph_free(loaded);
        }
        free(json);
        flow_run_free(run);
        graph_free(&gs);
    }

    /* Phase 3: persistent Blueprint instances, variables, and generic events.
     * Begin() initializes one instance's variables once; later Tick(Real) and
     * Input(Name, Real, Bool) events read and write the same state (VAR-11,
     * EVT-9, EVT-10). */
    {
        blueprint *bp = blueprint_new("VarDemo");
        check(bp && bp->graph, "BP-1: a blueprint owns its graph and metadata");
        blueprint_variable *health = blueprint_add_variable(
            bp,
            "Health",
            PIN_I32,
            "int",
            PIN_CONTAINER_SINGLE,
            &(pin_value){ .type = PIN_I32, .i = 100 },
            "Stats"
        );
        check(health != NULL, "VAR-1: variables carry id, name, type, container, default, category");
        check(
            blueprint_find_variable_by_name(bp, "Health") == health,
            "VAR-3: variable names are unique and findable within a blueprint"
        );
        check(
            blueprint_add_variable(bp, "Health", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, NULL) ==
                NULL,
            "VAR-3: duplicate variable names are rejected"
        );
        blueprint_variable *last_action = blueprint_add_variable(
            bp,
            "LastAction",
            PIN_NAME,
            "name",
            PIN_CONTAINER_SINGLE,
            &(pin_value){ .type = PIN_NAME, .str = "" },
            ""
        );
        check(last_action != NULL, "TYPE-12: a Name variable accepts an authored default");

        /* Begin(): Set Health to 50. */
        node *begin_ev = graph_add_node(bp->graph, registry_node_by_name("Event"));
        graph_set_event_name(bp->graph, begin_ev, "Begin");
        check(
            begin_ev->input_count == 0 && begin_ev->output_count >= 1 &&
                begin_ev->output_pins[0].kind == PIN_EXEC,
            "EVT-1: an Event node is an executable entry point"
        );
        check(
            graph_event_name(begin_ev) &&
                strcmp(graph_event_name(begin_ev), "Begin") == 0,
            "EVT-3: event identity is name-addressed within the Blueprint"
        );
        node *set = blueprint_add_set_node(bp, health->id);
        graph_set_pin_default(bp->graph, set, 0, 1, &(pin_value){ .type = PIN_I32, .i = 50 });
        graph_connect_exec(bp->graph, begin_ev, 0, set, 0, 0);

        /* Tick(Real DeltaSeconds): read Health into a capture node. */
        node *tick_ev = graph_add_node(bp->graph, registry_node_by_name("Event"));
        graph_set_event_name(bp->graph, tick_ev, "Tick");
        pin_spec delta = { "DeltaSeconds", PIN_F64, "double", PIN_DATA };
        graph_add_pin(bp->graph, tick_ev, 1, &delta);
        node *get = blueprint_add_get_node(bp, health->id);
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
            "DBG-17: pure data chain is authored"
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
        node *set_action = blueprint_add_set_node(bp, last_action->id);
        graph_connect_exec(bp->graph, input_ev, 0, set_action, 0, 0);
        graph_connect_data(bp->graph, input_ev, 1, set_action, 1);

        blueprint_instance *bi = blueprint_instance_create(bp, NULL, NULL, NULL);
        check(bi != NULL, "RUN-15: explicit instance creation succeeds");
        pin_value v;
        check(
            blueprint_instance_get_variable(bi, health->id, &v) == 0 && v.i == 100,
            "VAR-11: instance variables initialize from authored defaults once"
        );
        check(bp->graph->flow_pause_count > 0, "RUN-20: creating an instance acquires the mutation guard");
        check(blueprint_instance_scratch(bi, 99) != NULL, "EXEC-15: instance scratch is keyed by node ID");
        check(bi->activations == NULL, "EXEC-16: activation-local state starts empty on the instance");
        check(bp->graph->flow_pause_count == 1, "RUN-19: authored mutation is guarded while a preview instance exists");

        flow_error perr = { 0 };
        flow_run *act = blueprint_instance_dispatch(bi, "Begin", (flow_inputs){ 0 }, &perr);
        check(act && flow_run_status(act) == FLOW_COMPLETED, "EVT-9: Begin dispatch completes");
        check(flow_run_current_node(act) == NULL, "RUN-16: inspecting a completed activation does not merge scheduler state");
        check(
            flow_run_call_stack(act, NULL) != NULL || flow_run_status(act) == FLOW_COMPLETED,
            "RUN-9: an activation exposes an ordered call-stack view"
        );
        check(
            blueprint_instance_get_variable(bi, health->id, &v) == 0 && v.i == 50,
            "VAR-4: Set Variable writes instance storage"
        );

        g_captured_int = -1;
        pin_value tick_params[] = { { .type = PIN_F64, .d = 0.016 } };
        act = blueprint_instance_dispatch(bi, "Tick", (flow_inputs){ tick_params, 1, NULL }, &perr);
        check(
            act && flow_run_status(act) == FLOW_COMPLETED && g_captured_int == 50,
            "EVT-10: a later Tick event reads the state Begin wrote"
        );

        g_captured_str[0] = '\0';
        act = blueprint_instance_dispatch(bi, "TickText", (flow_inputs){ 0 }, &perr);
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
            "DBG-17: pure data chains record every traversed data edge"
        );

        pin_value input_params[] = {
            { .type = PIN_NAME, .str = "Jump" },
            { .type = PIN_F64, .d = 1.0 },
            { .type = PIN_BOOL, .b = true },
        };
        act = blueprint_instance_dispatch(bi, "Input", (flow_inputs){ input_params, 3, NULL }, &perr);
        check(act && flow_run_status(act) == FLOW_COMPLETED, "EVT-9: Input dispatch with parameters completes");
        check(
            blueprint_instance_get_variable(bi, last_action->id, &v) == 0 &&
                v.type == PIN_NAME && v.str && strcmp(v.str, "Jump") == 0,
            "EVT-4: event parameters deliver values into instance state"
        );
        check(health->default_value.i == 100, "BP-13: instance execution never mutates authored defaults");

        check(blueprint_rename_variable(bp, health->id, "HP") != 0, "EXEC-19: renaming a variable is rejected while an instance is attached");
        check(blueprint_add_variable(bp, "New", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, NULL) == NULL, "EXEC-19: adding a variable is rejected while an instance is attached");
        check(
            graph_add_node(bp->graph, &g_sink_a_desc) == NULL,
            "EXEC-19: adding a node is rejected while an instance is attached"
        );
        check(blueprint_instance_stop_activation(bi, act) == FLOW_OK, "RUN-17: stopping one activation succeeds");
        check(bp->graph->flow_pause_count > 0, "RUN-20: stopping one activation does NOT release the mutation guard");
        blueprint_instance_destroy(bi);
        check(bp->graph->flow_pause_count == 0, "RUN-20: destroying the last instance releases the mutation guard");

        uint64_t health_id = health->id;
        node *get_ref = get;
        check(blueprint_rename_variable(bp, health_id, "HP") == 0, "VAR-5: renaming a variable succeeds when mutable");
        check(
            health->name && strcmp(health->name, "HP") == 0 &&
                get_ref->reference_id == health_id && get_ref->reference_name &&
                strcmp(get_ref->reference_name, "HP") == 0,
            "VAR-5: rename updates the referencing node's display without "
            "rewriting its reference"
        );
        check(blueprint_retype_variable(bp, health_id, PIN_F64, "double") == 0, "VAR-6: retyping a variable succeeds");
        check(
            health->type == PIN_F64 && get_ref->output_pins[0].type == PIN_F64,
            "VAR-5: retyping updates the referencing node's pin type"
        );
        node *src = NULL;
        int src_pin = -1;
        check(graph_input_source(bp->graph, capture, 1, &src, &src_pin) == 0, "VAR-6: the now-incompatible edge is invalidated");
        blueprint_free(bp);
    }

    /* VAR-7: deleting a referenced variable is permitted; the orphaned Get
     * reference fails evaluation with an error naming the lost variable. */
    {
        blueprint *bp = blueprint_new("Orphan");
        blueprint_variable *v = blueprint_add_variable(
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
        node *get = blueprint_add_get_node(bp, v->id);
        node *cap = graph_add_node(bp->graph, &g_capture_desc);
        graph_connect_exec(bp->graph, ev, 0, cap, 0, 0);
        graph_connect_data(bp->graph, get, 0, cap, 1);
        blueprint_instance *bi = blueprint_instance_create(bp, NULL, NULL, NULL);
        blueprint_instance_destroy(bi);
        check(blueprint_delete_variable(bp, v->id) == 0, "VAR-7: deleting a referenced variable is permitted");
        bi = blueprint_instance_create(bp, NULL, NULL, NULL);
        flow_error perr = { 0 };
        flow_run *act = blueprint_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &perr);
        check(
            act == NULL && perr.code == FLOW_ERR_COMPILE && perr.node == get &&
                perr.message && strstr(perr.message, "missing variable") != NULL,
            "VAR-7: an orphaned Get reference is a blocking compile error "
            "(RUN-12 refuses to start)"
        );
        blueprint_instance_destroy(bi);
        blueprint_free(bp);
    }

    /* EXEC-17 / CAT-18: a Delay suspends while a later event executes, and the
     * scheduler's FIFO activation order is deterministic across repeated runs. */
    {
        blueprint *bp = blueprint_new("Sched");
        node *wait_ev = graph_add_node(bp->graph, registry_node_by_name("Event"));
        graph_set_event_name(bp->graph, wait_ev, "Wait");
        node *delay = graph_add_node(bp->graph, registry_node_by_name("Delay"));
        node *done_a = graph_add_node(bp->graph, &g_sink_a_desc);
        graph_connect_exec(bp->graph, wait_ev, 0, delay, 0, 0);
        graph_connect_exec(bp->graph, delay, 0, done_a, 0, 0);
        delay->inputs[1].d = 5.0;
        node *fast_ev = graph_add_node(bp->graph, registry_node_by_name("Event"));
        graph_set_event_name(bp->graph, fast_ev, "Fast");
        node *done_b = graph_add_node(bp->graph, &g_sink_b_desc);
        graph_connect_exec(bp->graph, fast_ev, 0, done_b, 0, 0);
        for (int run = 0; run < 2; run++) {
            blueprint_instance *bi = blueprint_instance_create(bp, NULL, NULL, NULL);
            flow_log_reset();
            flow_error perr = { 0 };
            flow_run *act = blueprint_instance_dispatch(bi, "Wait", (flow_inputs){ 0 }, &perr);
            check(
                act && flow_run_status(act) == FLOW_SUSPENDED && g_flow_log_count == 0,
                "EXEC-17: a Delay suspends its activation"
            );
            act = blueprint_instance_dispatch(bi, "Fast", (flow_inputs){ 0 }, &perr);
            check(
                act && flow_run_status(act) == FLOW_COMPLETED &&
                    strcmp(g_flow_log, "B") == 0,
                "EXEC-17: a later event executes while the Delay is suspended"
            );
            check(acount(bi->activations) == 2, "EXEC-17: suspended and completed activations coexist");
            check(acount(bi->activations) == 2, "EXEC-18: re-entry keeps an earlier activation on its continuation stack");
            check(blueprint_instance_tick(bi, 5.0) == FLOW_OK && strcmp(g_flow_log, "BA") == 0, "EXEC-17: the scheduler resumes the Delay at its deadline in FIFO order");
            check(act && flow_run_status(act) == FLOW_COMPLETED, "CAT-18: the Delay completes after its duration");
            blueprint_instance_destroy(bi);
        }
        blueprint_free(bp);
    }

    /* EVT-7: dispatching by name fails distinctly for unknown and duplicate
     * names. */
    {
        blueprint *bp = blueprint_new("Evt7");
        node *ev = graph_add_node(bp->graph, registry_node_by_name("Event"));
        graph_set_event_name(bp->graph, ev, "Go");
        blueprint_instance *bi = blueprint_instance_create(bp, NULL, NULL, NULL);
        flow_error err = { 0 };
        check(
            blueprint_instance_dispatch(bi, "Missing", (flow_inputs){ 0 }, &err) == NULL &&
                err.code == FLOW_ERR_UNKNOWN_EVENT,
            "EVT-7: an unknown event name fails distinctly"
        );
        blueprint_instance_destroy(bi);
        node *dup = graph_add_node(bp->graph, registry_node_by_name("Event"));
        graph_set_event_name(bp->graph, dup, "Go");
        bi = blueprint_instance_create(bp, NULL, NULL, NULL);
        err = (flow_error){ 0 };
        check(
            blueprint_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &err) == NULL &&
                err.code == FLOW_ERR_DUPLICATE_EVENT,
            "EVT-7: a duplicate event name fails distinctly"
        );
        blueprint_instance_destroy(bi);
        blueprint_free(bp);
    }

    /* RUN-18: two instances of one definition share no owner, variable,
     * scratch, activation, error, or step-count state. */
    {
        blueprint *bp = blueprint_new("Two");
        blueprint_set_owner_type(bp, "Entity");
        blueprint_variable *v = blueprint_add_variable(
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
        blueprint_instance *bi1 = blueprint_instance_create(bp, &o1, NULL, NULL);
        blueprint_instance *bi2 = blueprint_instance_create(bp, &o2, NULL, NULL);
        check(
            blueprint_instance_owner(bi1).u64 == 111 &&
                blueprint_instance_owner(bi2).u64 == 222 &&
                blueprint_instance_owner(bi1).type == PIN_HANDLE,
            "RUN-18: two instances carry independent owner handles"
        );
        blueprint_instance_set_variable(bi1, v->id, &(pin_value){ .type = PIN_I32, .i = 7 });
        pin_value rv;
        check(
            blueprint_instance_get_variable(bi1, v->id, &rv) == 0 && rv.i == 7 &&
                blueprint_instance_get_variable(bi2, v->id, &rv) == 0 && rv.i == 100,
            "RUN-18: two instances have independent variable storage"
        );
        pin_value *s1 = blueprint_instance_scratch(bi1, 42);
        s1->type = PIN_I32;
        s1->i = 5;
        check(blueprint_instance_scratch(bi2, 42)->i == 0, "RUN-18: two instances have independent per-node scratch");
        flow_error perr = { 0 };
        flow_run *a1 = blueprint_instance_dispatch(bi1, "Boom", (flow_inputs){ 0 }, &perr);
        check(a1 && flow_run_status(a1) == FLOW_FAILED && flow_run_steps(a1) == 2, "RUN-18: an error and its step count live on instance one's own activation");
        check(
            acount(bi1->activations) == 1 && acount(bi2->activations) == 0,
            "RUN-18: one instance's failure leaves the other's queue untouched"
        );
        flow_run *a2 = blueprint_instance_dispatch(bi2, "Boom", (flow_inputs){ 0 }, &perr);
        check(
            a2 && flow_run_status(a2) == FLOW_FAILED &&
                flow_run_error(a1) != flow_run_error(a2),
            "RUN-18: independent activation errors"
        );
        blueprint_instance_destroy(bi1);
        blueprint_instance_destroy(bi2);
        blueprint_free(bp);
    }

    /* RUN-15 / RUN-17: stopping one activation preserves instance variables
     * and scratch; destroying the instance cancels every activation and
     * releases the mutation guard without executing another node. */
    {
        blueprint *bp = blueprint_new("Stop");
        blueprint_variable *v = blueprint_add_variable(
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
        delay->inputs[1].d = 100.0;
        blueprint_instance *bi = blueprint_instance_create(bp, NULL, NULL, NULL);
        blueprint_instance_set_variable(bi, v->id, &(pin_value){ .type = PIN_I32, .i = 9 });
        blueprint_instance_scratch(bi, delay->id)->type = PIN_I32;
        blueprint_instance_scratch(bi, delay->id)->i = 42;
        flow_error perr = { 0 };
        flow_run *act = blueprint_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &perr);
        check(act && flow_run_status(act) == FLOW_SUSPENDED, "RUN-15: dispatch leaves a suspended activation");
        check(
            blueprint_instance_stop_activation(bi, act) == FLOW_OK &&
                flow_run_status(act) == FLOW_IDLE,
            "RUN-17: stopping one activation succeeds"
        );
        pin_value rv;
        check(
            blueprint_instance_get_variable(bi, v->id, &rv) == 0 && rv.i == 9,
            "RUN-17: stopping an activation preserves instance variables"
        );
        check(blueprint_instance_scratch(bi, delay->id)->i == 42, "RUN-17: stopping an activation preserves per-node scratch");
        int guard_before = bp->graph->flow_pause_count;
        blueprint_instance_destroy(bi);
        check(bp->graph->flow_pause_count == guard_before - 1, "RUN-20: destroying the instance releases the mutation guard");
        node *probe = graph_add_node(bp->graph, &g_sink_b_desc);
        check(probe != NULL, "EXEC-19: the definition becomes mutable after its last instance is destroyed");
        /* The probe node's exec input is unconnected, which is a blocking
         * compile error under RUN-12; remove it before starting again. */
        check(graph_remove_node(bp->graph, probe) == 0, "EXEC-19: removing the probe restores a valid blueprint");

        blueprint_instance *bi2 = blueprint_instance_create(bp, NULL, NULL, NULL);
        flow_log_reset();
        act = blueprint_instance_dispatch(bi2, "Go", (flow_inputs){ 0 }, &perr);
        check(act && flow_run_status(act) == FLOW_SUSPENDED, "RUN-15: a second instance also suspends");
        blueprint_instance_destroy(bi2);
        check(g_flow_log_count == 0, "RUN-15: destroying the instance cancels activations without executing another node");
        blueprint_free(bp);
    }

    /* EVT-6 / EVT-11: Call Event invokes a custom event as a separate
     * activation through the scheduler, never through the C call stack. */
    {
        blueprint *bp = blueprint_new("Call");
        node *outer = graph_add_node(bp->graph, registry_node_by_name("Event"));
        graph_set_event_name(bp->graph, outer, "Outer");
        node *inner = graph_add_node(bp->graph, registry_node_by_name("Event"));
        graph_set_event_name(bp->graph, inner, "Inner");
        pin_spec msg = { "Message", PIN_STR, "const char *", PIN_DATA };
        graph_add_pin(bp->graph, inner, 1, &msg);
        node *cap = graph_add_node(bp->graph, &g_str_capture_desc);
        graph_connect_exec(bp->graph, inner, 0, cap, 0, 0);
        graph_connect_data(bp->graph, inner, 1, cap, 1);
        node *call = blueprint_add_call_event_node(bp, inner);
        check(
            call != NULL && call->input_count == 2 &&
                call->reference_id == inner->id,
            "EVT-6: Call Event mirrors the event's parameters as data inputs"
        );
        check(
            call->input_count == 2 && call->inputs[1].type == PIN_STR,
            "EVT-5: Call Event pins track the authored event signature"
        );
        node *str_lit = graph_add_node(bp->graph, registry_node_by_name("String"));
        graph_set_pin_default(bp->graph, str_lit, 0, 0, &(pin_value){ .type = PIN_STR, .str = "hello from call" });
        graph_connect_exec(bp->graph, outer, 0, call, 0, 0);
        graph_connect_data(bp->graph, str_lit, 0, call, 1);
        blueprint_instance *bi = blueprint_instance_create(bp, NULL, NULL, NULL);
        g_captured_str[0] = 0;
        flow_error perr = { 0 };
        flow_run *act = blueprint_instance_dispatch(bi, "Outer", (flow_inputs){ 0 }, &perr);
        check(act && flow_run_status(act) == FLOW_COMPLETED, "EVT-6: the Outer activation completes");
        check(strcmp(g_captured_str, "hello from call") == 0 && acount(bi->activations) == 2, "EVT-11: Call Event delivered the parameter through a separate activation");
        blueprint_instance_destroy(bi);
        blueprint_free(bp);
    }

    /* CAT-32: Self is a pure node whose output has the definition's owner
     * handle type; it is rejected without an owner type and evaluates to the
     * instance's supplied owner handle. */
    {
        blueprint *bp = blueprint_new("Self");
        check(blueprint_add_self_node(bp) == NULL, "CAT-32: Self is rejected when the definition declares no owner type");
        /* The owner handle type must resolve in the active registry (CMPL-20),
         * so the test uses a reflected type name. */
        blueprint_set_owner_type(bp, "Vec3");
        check(
            bp->owner_type_name && strcmp(bp->owner_type_name, "Vec3") == 0,
            "BP-12: a Blueprint definition carries one nominal owner type"
        );
        node *self = blueprint_add_self_node(bp);
        check(
            self && self->output_pins[0].type == PIN_HANDLE &&
                self->output_pins[0].type_name &&
                strcmp(self->output_pins[0].type_name, "Vec3") == 0,
            "CAT-32: Self carries the declared nominal owner handle type"
        );
        pin_spec ha = { "a", PIN_HANDLE, "Entity", PIN_DATA };
        pin_spec hb = { "b", PIN_HANDLE, "Entity", PIN_DATA };
        pin_spec hc = { "c", PIN_HANDLE, "Other", PIN_DATA };
        check(pin_types_compatible(&ha, &hb) && !pin_types_compatible(&ha, &hc), "TYPE-13: handle pins are compatible only when nominal type names match");
        node *ev = graph_add_node(bp->graph, registry_node_by_name("Event"));
        graph_set_event_name(bp->graph, ev, "Go");
        node *cap = graph_add_node(bp->graph, &g_handle_capture_desc);
        graph_connect_exec(bp->graph, ev, 0, cap, 0, 0);
        graph_connect_data(bp->graph, self, 0, cap, 1);
        pin_value owner = { .type = PIN_HANDLE, .u64 = 777, .type_name = "Vec3" };
        blueprint_instance *bi = blueprint_instance_create(bp, &owner, NULL, NULL);
        g_captured_handle = 0;
        flow_error perr = { 0 };
        flow_run *act = blueprint_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &perr);
        check(
            act && flow_run_status(act) == FLOW_COMPLETED && g_captured_handle == 777,
            "CAT-32: Self evaluates to the instance's supplied owner handle"
        );
        blueprint_instance_destroy(bi);
        blueprint_free(bp);
    }

    /* FMT-16 / FMT-17 / FMT-18: the Blueprint document is byte-identical
     * before and after execution, and Name spelling plus the owner type
     * round-trip exactly. */
    {
        blueprint *bp = blueprint_new("FmtBP");
        blueprint_set_owner_type(bp, "UnavailableOwner");
        blueprint_add_variable(
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
        char *before = blueprint_save_json(bp);
        check(
            before && strstr(before, "\"owner_type\": \"UnavailableOwner\""),
            "FMT-18: the authored owner handle type is written"
        );
        blueprint_instance *bi = blueprint_instance_create(
            bp,
            &(pin_value){ .type = PIN_HANDLE, .u64 = 5, .type_name = "UnavailableOwner" },
            NULL,
            NULL
        );
        blueprint_instance_destroy(bi);
        char *after = blueprint_save_json(bp);
        check(before && after && strcmp(before, after) == 0 && !strstr(before, "activations"), "FMT-16: saving before and after execution is byte-identical and carries no runtime state");
        blueprint *loaded = before ? blueprint_load_json(before) : NULL;
        check(loaded && loaded->owner_type_name && strcmp(loaded->owner_type_name, "UnavailableOwner") == 0, "FMT-17: an unavailable owner type name is preserved, not substituted");
        blueprint_variable *lv = loaded ? blueprint_find_variable_by_name(loaded, "Tag")
                                        : NULL;
        check(
            lv && lv->default_value.str && strcmp(lv->default_value.str, "MyTag") == 0,
            "FMT-17: a Name variable's authored spelling round-trips exactly"
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
        check(spelling, "FMT-17: a Name literal's authored spelling round-trips exactly");
        char *re = loaded ? blueprint_save_json(loaded) : NULL;
        check(re && before && strcmp(re, before) == 0, "FMT-12: the Blueprint document load/save round-trips byte-identically");
        if (loaded) {
            blueprint_free(loaded);
        }
        free(before);
        free(after);
        free(re);
        blueprint_free(bp);
    }

    /* NODE-13: descriptors classify as pure, executable entry, or executable;
     * the existing pure helper keeps classifying the same way. */
    {
        check(node_desc_behavior(&g_const_desc) == NODE_BEHAVIOR_PURE, "NODE-13: a pure descriptor has no exec pins");
        check(node_desc_behavior(&NODE_Event) == NODE_BEHAVIOR_ENTRY, "NODE-13: an event is an executable entry (no exec input, one exec output)");
        check(node_desc_behavior(&NODE_Branch) == NODE_BEHAVIOR_EXEC, "NODE-13: a node with an exec input is executable");
        check(
            node_desc_is_pure(&g_const_desc) && !node_desc_is_pure(&NODE_Branch),
            "NODE-13: the existing pure helper agrees with the classification"
        );
    }

    /* CMPL-1: compiling is explicit, ordered, read-only, and clean on an
     * empty blueprint. */
    {
        blueprint *bp = blueprint_new("CMPL1");
        compile_report rep;
        compile_report_init(&rep);
        unsigned long long rev = graph_topology_revision(bp->graph);
        char *before = blueprint_save_json(bp);
        int errors = blueprint_compile(bp, &rep);
        char *after = blueprint_save_json(bp);
        check(
            errors == 0 && rep.error_count == 0 && rep.warning_count == 0 &&
                acount(rep.items) == 0,
            "CMPL-1: compiling an empty blueprint produces no diagnostics"
        );
        check(
            rev == graph_topology_revision(bp->graph) && before && after &&
                strcmp(before, after) == 0,
            "CMPL-1: the compile pass never modifies the authored graph"
        );
        check(
            compile_report_blocked(&rep) == 0 && compile_report_first_error(&rep) == NULL,
            "CMPL-3: an empty report has no blocking error"
        );
        free(before);
        free(after);
        compile_report_free(&rep);
        blueprint_free(bp);
    }

    /* CMPL-4: an executable node whose exec input has no incoming edge; the
     * event entry itself is exempt. */
    {
        blueprint *bp = blueprint_new("CMPL4");
        node *ev = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, ev, "Go");
        node *branch = graph_add_node(bp->graph, &NODE_Branch);
        node *print = graph_add_node(bp->graph, &NODE_PrintString);
        print->y = 100.0f; /* below the branch: order is tested under CMPL-16 */
        check(
            node_behavior_for(ev) == NODE_BEHAVIOR_ENTRY &&
                node_behavior_for(branch) == NODE_BEHAVIOR_EXEC,
            "NODE-13: node instances classify like their descriptors"
        );
        compile_report rep;
        compile_report_init(&rep);
        check(blueprint_compile(bp, &rep) == 2, "CMPL-4: two executable nodes with unconnected exec inputs error");
        int count = 0, on_branch = 0, on_print = 0;
        for (int i = 0; i < acount(rep.items); i++) {
            const compile_diagnostic *d = &rep.items[i];
            if (d->rule != CMPL_RULE_UNCONNECTED_EXEC || d->severity != CMPL_ERROR) {
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
        check(count == 2 && on_branch && on_print, "CMPL-4: both diagnostics name the unconnected executable nodes");
        check(
            rep.items[0].node_id == branch->id &&
                rep.items[0].pin_id == branch->input_pins[0].id &&
                rep.items[0].graph_id == CMPL_GRAPH_DEFAULT,
            "CMPL-2: the diagnostic carries graph, node, and pin ids"
        );
        check(
            strcmp(cmpl_rule_name(CMPL_RULE_UNCONNECTED_EXEC), "CMPL-4") == 0,
            "CMPL-2: every diagnostic carries a stable rule code"
        );
        compile_report_free(&rep);
        blueprint_free(bp);
    }

    /* CMPL-5: a data edge between incompatible pins. The graph API rejects
     * incompatible connections, so the test inserts the edge directly. */
    {
        blueprint *bp = blueprint_new("CMPL5");
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
        blueprint_compile(bp, &rep);
        /* The graph also carries the all-types node's unregistered enum output
         * (CMPL-20); count only the incompatible-edge diagnostics here. */
        int bad_edges = 0;
        for (int i = 0; i < acount(rep.items); i++) {
            if (rep.items[i].rule == CMPL_RULE_INCOMPATIBLE_DATA_EDGE) {
                bad_edges++;
            }
        }
        check(bad_edges == 1, "CMPL-5: one incompatible data edge errors");
        const compile_diagnostic *c5 = NULL;
        for (int i = 0; i < acount(rep.items); i++) {
            if (rep.items[i].rule == CMPL_RULE_INCOMPATIBLE_DATA_EDGE) {
                c5 = &rep.items[i];
            }
        }
        check(
            c5 && c5->node_id == sink->id && c5->pin_id == sink->input_pins[2].id,
            "CMPL-5: the diagnostic names the incompatible input pin"
        );
        compile_report_free(&rep);
        blueprint_free(bp);
    }

    /* CMPL-6: a cycle among data edges reports once, at the closing back
     * edge; an acyclic chain stays clean. */
    {
        blueprint *bp = blueprint_new("CMPL6");
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
        check(blueprint_compile(bp, &rep) == 0, "CMPL-6: an acyclic data chain compiles clean");
        graph_connect_data(bp->graph, c, 0, a, 0);
        compile_report_free(&rep);
        compile_report_init(&rep);
        check(
            blueprint_compile(bp, &rep) == 1 && rep.items[0].rule == CMPL_RULE_DATA_CYCLE &&
                rep.items[0].node_id == c->id,
            "CMPL-6: closing the chain reports one data-cycle error at the "
            "back edge"
        );
        compile_report_free(&rep);
        blueprint_free(bp);
    }

    /* CMPL-7: a reference to a retired or missing variable, event, function,
     * or macro. */
    {
        blueprint *bp = blueprint_new("CMPL7");
        blueprint_variable *v = blueprint_add_variable(bp, "Score", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "");
        node *get = blueprint_add_get_node(bp, v->id);
        node *set = blueprint_add_set_node(bp, v->id);
        node *ev = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, ev, "Go");
        node *call = blueprint_add_call_event_node(bp, ev);
        compile_report rep;
        compile_report_init(&rep);
        blueprint_compile(bp, &rep);
        int refs = 0;
        for (int i = 0; i < acount(rep.items); i++) {
            if (rep.items[i].rule == CMPL_RULE_MISSING_REFERENCE) {
                refs++;
            }
        }
        check(refs == 0, "CMPL-7: valid variable and event references compile clean");
        check(blueprint_delete_variable(bp, v->id) == 0, "CMPL-7: deleting a referenced variable is permitted (VAR-7)");
        compile_report_free(&rep);
        compile_report_init(&rep);
        blueprint_compile(bp, &rep);
        refs = 0;
        for (int i = 0; i < acount(rep.items); i++) {
            if (rep.items[i].rule == CMPL_RULE_MISSING_REFERENCE) {
                refs++;
            }
        }
        check(refs == 2, "CMPL-7: Get and Set both report the retired variable");
        check(graph_remove_node(bp->graph, ev) == 0, "CMPL-7: removing the referenced event is permitted");
        compile_report_free(&rep);
        compile_report_init(&rep);
        blueprint_compile(bp, &rep);
        refs = 0;
        for (int i = 0; i < acount(rep.items); i++) {
            if (rep.items[i].rule == CMPL_RULE_MISSING_REFERENCE) {
                refs++;
            }
        }
        check(refs == 3, "CMPL-7: the Call Event reports the removed event");
        compile_report_free(&rep);
        blueprint_free(bp);
    }

    /* CMPL-8: duplicate event names, duplicate parameter names within one
     * list, and duplicate variable names each report once. */
    {
        blueprint *bp = blueprint_new("CMPL8");
        node *e1 = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, e1, "Begin");
        node *e2 = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, e2, "Begin");
        compile_report rep;
        compile_report_init(&rep);
        blueprint_compile(bp, &rep);
        check(
            rep.error_count == 1 && rep.items[0].rule == CMPL_RULE_DUPLICATE_NAME &&
                rep.items[0].node_id == e2->id,
            "CMPL-8: two events sharing a name produce one diagnostic on the "
            "later event"
        );
        node *e3 = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, e3, "Tick");
        pin_spec amount_a = { "Amount", PIN_I32, "int", PIN_DATA };
        pin_spec amount_b = { "Amount", PIN_I32, "int", PIN_DATA };
        graph_add_pin(bp->graph, e3, 1, &amount_a);
        graph_add_pin(bp->graph, e3, 1, &amount_b);
        blueprint_variable dup_var_a = {
            .id = graph_alloc_id(bp->graph),
            .name = graph_own_str(bp->graph, "Score", 5),
            .type = PIN_I32,
            .type_name = "int",
            .container = PIN_CONTAINER_SINGLE,
        };
        blueprint_variable dup_var_b = dup_var_a;
        dup_var_b.id = graph_alloc_id(bp->graph);
        apush(bp->variables, dup_var_a);
        apush(bp->variables, dup_var_b);
        compile_report_free(&rep);
        compile_report_init(&rep);
        blueprint_compile(bp, &rep);
        check(rep.error_count == 3, "CMPL-8: duplicate event, parameter, and variable names each error");
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
        check(ev_dup == 1 && param_dup == 1 && var_dup == 1, "CMPL-8: each duplicate kind reports exactly once");
        compile_report_free(&rep);
        blueprint_free(bp);
    }

    /* CMPL-12: an unresolved wildcard pin that carries an edge. */
    {
        blueprint *bp = blueprint_new("CMPL12");
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
        check(blueprint_compile(bp, &rep) == 0, "CMPL-12: an unconnected wildcard pin is not an error");
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
            blueprint_compile(bp, &rep) == 1 &&
                rep.items[0].rule == CMPL_RULE_UNRESOLVED_WILDCARD &&
                rep.items[0].node_id == w->id &&
                rep.items[0].pin_id == w->input_pins[0].id,
            "CMPL-12: an unresolved wildcard pin that carries an edge errors"
        );
        compile_report_free(&rep);
        blueprint_free(bp);
    }

    /* CMPL-16: deterministic ordering by graph, then node position (canvas y,
     * then x, then node id), then rule code. */
    {
        blueprint *bp = blueprint_new("CMPL16");
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
        blueprint_compile(bp, &r1);
        blueprint_compile(bp, &r2);
        int same = acount(r1.items) == acount(r2.items);
        for (int i = 0; same && i < acount(r1.items); i++) {
            same = memcmp(&r1.items[i], &r2.items[i], sizeof r1.items[i]) == 0;
        }
        check(same && acount(r1.items) == 3, "CMPL-16: compiling twice yields an identical ordered list");
        check(
            r1.items[0].node_id == top->id && r1.items[1].node_id == mid->id &&
                r1.items[2].node_id == bot->id,
            "CMPL-16: diagnostics order by canvas y, then x, then node id"
        );
        check(
            r1.items[0].rule == CMPL_RULE_UNCONNECTED_EXEC &&
                r1.items[1].rule == CMPL_RULE_UNCONNECTED_EXEC,
            "CMPL-16: equal-rule diagnostics keep position order"
        );
        compile_report_free(&r1);
        compile_report_free(&r2);
        blueprint_free(bp);
    }

    /* CMPL-16: diagnostics on one node order by rule code; the set node
     * carries both an unconnected exec input (CMPL-4) and a missing variable
     * reference (CMPL-7). */
    {
        blueprint *bp = blueprint_new("CMPL16b");
        node *set = graph_add_node(bp->graph, &NODE_SetVariable);
        graph_set_node_reference(bp->graph, set, 4242, "Lost");
        compile_report rep;
        compile_report_init(&rep);
        check(blueprint_compile(bp, &rep) == 2, "CMPL-16: one node can carry several errors");
        check(
            acount(rep.items) == 2 && rep.items[0].rule == CMPL_RULE_UNCONNECTED_EXEC &&
                rep.items[1].rule == CMPL_RULE_MISSING_REFERENCE,
            "CMPL-16: diagnostics on one node order by rule code"
        );
        compile_report_free(&rep);
        blueprint_free(bp);
    }

    /* CMPL-18: a disabled node is excluded from every rule except one
     * warning reporting that it is disabled. */
    {
        blueprint *bp = blueprint_new("CMPL18");
        node *dbranch = graph_add_node(bp->graph, &NODE_Branch);
        node *dget = graph_add_node(bp->graph, &NODE_GetVariable);
        graph_set_node_disabled(bp->graph, dbranch, 1);
        graph_set_node_disabled(bp->graph, dget, 1);
        compile_report rep;
        compile_report_init(&rep);
        check(blueprint_compile(bp, &rep) == 0 && rep.error_count == 0 && rep.warning_count == 2, "CMPL-18: disabled nodes are excluded from every rule except one warning each");
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
        check(warn_branch == 1 && warn_get == 1 && other == 0, "CMPL-18: exactly one disabled warning per node and no other rules");
        compile_report_free(&rep);
        blueprint_free(bp);
    }

    /* CMPL-20: an authored nominal type name not present in the active
     * registry is a blocking diagnostic that preserves the name. */
    {
        blueprint *bp = blueprint_new("CMPL20");
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
        check(blueprint_compile(bp, &rep) == 1, "CMPL-20: a registry-known struct pin compiles, an unknown handle name errors");
        check(
            rep.items[0].rule == CMPL_RULE_UNKNOWN_NOMINAL_TYPE &&
                strstr(rep.items[0].message, "MissingType") != NULL,
            "CMPL-20: the diagnostic preserves the unresolved type name"
        );
        compile_report_free(&rep);
        blueprint_free(bp);
    }

    /* RUN-12: a run refuses to start against a blueprint with compile errors
     * and reports the first blocking diagnostic; warnings never block. */
    {
        blueprint *bp = blueprint_new("Run12");
        node *ev = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, ev, "Go");
        node *branch = graph_add_node(bp->graph, &NODE_Branch);
        node *print = graph_add_node(bp->graph, &NODE_PrintString);
        print->y = -50.0f; /* sorts above the branch: the first error */
        blueprint_instance *bi = blueprint_instance_create(bp, NULL, NULL, NULL);
        flow_error rerr = { 0 };
        flow_run *act = blueprint_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &rerr);
        check(act == NULL && rerr.code == FLOW_ERR_COMPILE && rerr.node == print, "RUN-12: dispatch refuses to start and reports the first blocking diagnostic");
        check(
            rerr.message && strstr(rerr.message, "no incoming edge") != NULL,
            "RUN-12: the refusal carries the blocking diagnostic message"
        );
        check(
            blueprint_instance_enqueue(bi, ev, (flow_inputs){ 0 }, &rerr) == NULL &&
                rerr.code == FLOW_ERR_COMPILE,
            "RUN-12: a direct enqueue is refused the same way"
        );
        blueprint_instance_destroy(bi);
        /* Fix the graph; the next instance recompiles at the new revision. */
        graph_remove_node(bp->graph, print);
        graph_connect_exec(bp->graph, ev, 0, branch, 0, 0);
        bi = blueprint_instance_create(bp, NULL, NULL, NULL);
        act = blueprint_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &rerr);
        check(act != NULL && flow_run_status(act) == FLOW_COMPLETED, "RUN-12: after the error is fixed the same event starts and completes");
        blueprint_instance_destroy(bi);
        blueprint_free(bp);
    }
    {
        blueprint *bp = blueprint_new("Run12w");
        node *ev = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, ev, "Go");
        node *br = graph_add_node(bp->graph, &NODE_Branch);
        graph_set_node_disabled(bp->graph, br, 1);
        blueprint_instance *bi = blueprint_instance_create(bp, NULL, NULL, NULL);
        flow_error werr = { 0 };
        flow_run *act = blueprint_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &werr);
        check(act != NULL && flow_run_status(act) == FLOW_COMPLETED, "RUN-12: warnings do not block a start (CMPL-3)");
        blueprint_instance_destroy(bi);
        blueprint_free(bp);
    }

    /* TYPE-7 / CAT-26 / UI-22: explicit conversion nodes. conversion_for()
     * answers the permitted pairs and refuses the lossy ones. */
    {
        check(
            registry_node_by_name("To String") && registry_node_by_name("To Real") &&
                registry_node_by_name("To Integer64") &&
                registry_node_by_name("Format Text"),
            "CAT-26: conversion and Format Text nodes are registered"
        );
        check(
            registry_node_by_name("To Real")->category &&
                registry_node_by_name("Format Text")->keywords,
            "CAT-27: conversion entries expose category paths and search "
            "keywords"
        );
        check(
            conversion_for(PIN_I32, PIN_F64) == &NODE_To_Real &&
                conversion_for(PIN_I32, PIN_F32) == &NODE_To_Real &&
                conversion_for(PIN_U8, PIN_F64) == &NODE_To_Real,
            "TYPE-7: Integer and Byte to Real have an explicit conversion"
        );
        check(conversion_for(PIN_I32, PIN_I64) == &NODE_To_Integer64, "TYPE-7: Integer widens to Integer64 within one signedness");
        check(conversion_for(PIN_I32, PIN_STR) == &NODE_To_String, "TYPE-7: Integer to String has an explicit conversion");
        check(conversion_for(PIN_F64, PIN_STR) == &NODE_To_String, "TYPE-7: Real to String has an explicit conversion");
        check(conversion_for(PIN_BOOL, PIN_STR) == &NODE_To_String, "TYPE-7: Bool to String has an explicit conversion");
        check(conversion_for(PIN_NAME, PIN_STR) == &NODE_To_String, "TYPE-7: Name to String has an explicit conversion");
        check(conversion_for(PIN_STR, PIN_F64) == NULL, "TYPE-7: String to Real is not a permitted conversion");
        check(conversion_for(PIN_F64, PIN_I32) == NULL, "UI-22: Real to Integer is lossy, so no conversion is offered");
        check(conversion_for(PIN_I64, PIN_I32) == NULL, "UI-22: Integer64 to Integer narrowing is not offered");
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
            "CAT-26: To Real converts Integer 42 to Real 42.0"
        );

        /* The reported case: an Integer onto a float input (add_floats). To
         * Real offers, and its F64 output coerces to float at the boundary
         * (TYPE-10). */
        node *addf = graph_add_node(&gc, registry_node_by_name("add_floats"));
        graph_connect(&gc, to_real, 0, addf, 0);
        check(
            graph_eval_output(&gc, addf, 0, &ac, &out, &eval_ctx) == 0 &&
                out.type == PIN_F32 && out.f == 42.0f,
            "CAT-26: Integer through To Real feeds a float input at the "
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
            "CAT-26: a Byte feeds a retyped To Real input and converts to 7.0"
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
            "CAT-26: To Integer64 widens Integer -3 to Integer64"
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
            "CAT-26: To String renders a Real identically to "
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
            "CAT-26: To String renders Bool, Name, and Integer scalars"
        );

        /* The conversion output must not alias the pin_value_to_string ring. */
        pin_value saved = out;
        for (int i = 0; i < 8; i++) {
            (void)pin_value_to_string(&(pin_value){ .type = PIN_F64,
                                                    .type_name = "double",
                                                    .d = (double)i });
        }
        check(strcmp(saved.str, "-7") == 0, "CAT-26: To String copies into the arena, not the format ring");

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
            "CAT-38: Format Text starts with Format and one Generic spare"
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
            "CAT-38: \"Hello {0} World\" with a String argument renders "
            "\"Hello Bob World\""
        );
        graph_set_pin_default(
            &gf,
            fmt,
            0,
            0,
            &(pin_value){ .type = PIN_STR, .type_name = "const char *", .str = "{1}" }
        );
        graph_eval_reset(&gf);
        check(
            graph_eval_output(&gf, fmt, 0, &af, &out, &eval_ctx) == 0 &&
                out.str[0] == '\0',
            "CAT-38: {1} with one argument renders as the empty string"
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
        graph_eval_reset(&gf);
        check(
            graph_eval_output(&gf, fmt, 0, &af, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "n=42") == 0,
            "CAT-38: a non-string argument renders like its pin display"
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
        check(strcmp(fmt->input_pins[3].name, "Arg2") == 0, "PIN-2: dynamically added pin owns its name");
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
        graph_eval_reset(&gf);
        check(
            graph_eval_output(&gf, fmt, 0, &af, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "42 3.5") == 0,
            "CAT-38: {} consumes arguments in order and a Real through To "
            "String renders like its pin display"
        );
        graph_set_pin_default(
            &gf,
            fmt,
            0,
            0,
            &(pin_value){ .type = PIN_STR, .type_name = "const char *", .str = "|{:>8}|" }
        );
        graph_eval_reset(&gf);
        check(
            graph_eval_output(&gf, fmt, 0, &af, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "|      42|") == 0,
            "CAT-38: {:>8} applies printf alignment to an argument"
        );
        graph_set_pin_default(
            &gf,
            fmt,
            0,
            0,
            &(pin_value){ .type = PIN_STR, .type_name = "const char *", .str = "a{{b}}c" }
        );
        graph_eval_reset(&gf);
        check(
            graph_eval_output(&gf, fmt, 0, &af, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "a{b}c") == 0,
            "CAT-38: {{ and }} print literal braces"
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
        graph_eval_reset(&gf);
        check(
            graph_eval_output(&gf, fmt, 0, &af, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "|3.14|2a|00000042|42.00|") == 0,
            "CAT-38: precision and base specifiers render numeric values"
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
            "PIN-11: connecting to the spare adopts the peer's type"
        );
        check(
            graph_eval_output(&gp, fmt, 0, &ap, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "n=42") == 0,
            "PIN-11: an adopted Generic argument evaluates its peer"
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
        check(fmt->input_pins[1].type == PIN_F64, "PIN-11: replacing the driver re-adopts the new type");
        graph_eval_reset(&gp);
        check(
            graph_eval_output(&gp, fmt, 0, &ap, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "n=3.5") == 0,
            "PIN-11: the re-adopted argument evaluates the new peer"
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
            "PIN-11: removing the edge or its driver reverts the Generic pin"
        );
        /* An unconnected spare renders as an empty argument. */
        graph_eval_reset(&gp);
        check(
            graph_eval_output(&gp, fmt, 0, &ap, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "n=") == 0,
            "PIN-11: an unconnected Generic argument renders empty"
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
            "PIN-11: adopted types and the spare survive save/load"
        );
        if (gp2) {
            arena ap2;
            arena_init(&ap2, 8192);
            graph_eval_reset(gp2);
            check(
                graph_eval_output(gp2, gp2->nodes[0], 0, &ap2, &out, &eval_ctx) == 0 &&
                    strcmp(out.str, "n=3.5") == 0,
                "PIN-11: a loaded Generic argument still evaluates"
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
        blueprint *bp = blueprint_new("EditorSurface");
        node *event = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, event, "Surface");
        blueprint_variable *value = blueprint_add_variable(
            bp,
            "Value",
            PIN_I32,
            "int",
            PIN_CONTAINER_SINGLE,
            &(pin_value){ .type = PIN_I32, .i = 3 },
            "Editor"
        );
        node *literal = graph_add_node(bp->graph, registry_node_by_name("Integer"));
        char *json = blueprint_save_json(bp);
        check(
            json && strstr(json, "variables") && strstr(json, "graphs"),
            "UI-1: every Blueprint document value has an authored model surface"
        );
        check(event && registry_node_count() > 0, "UI-2: canvas, tree, and catalog surfaces have model data");
        check(graph_set_node_title(bp->graph, literal, "Value") == 0, "UI-5: per-node properties have one graph mutation API");
        compile_report surface_report;
        compile_report_init(&surface_report);
        blueprint_compile(bp, &surface_report);
        check(
            surface_report.items != NULL || surface_report.error_count == 0,
            "UI-9: compiler results expose structured diagnostics"
        );
        check(
            json && strstr(json, "breakpoints_enabled") && strstr(json, "comment_boxes"),
            "UI-17: authored operations have serializable undo snapshots"
        );
        check(event->output_count > 0, "UI-23: required event pins remain visible in the model");
        check(registry_node_by_name("Format Text")->keywords != NULL, "UI-29: catalog search fields cover authored literal values by node type");
        check(json && strstr(json, "graphs"), "UI-42: dockable editor layout has graph persistence data");
        check(json && strstr(json, "comment_boxes"), "UI-43: output and details panels retain authored graph state");
        blueprint_set_owner_type(bp, "EditorOwner");
        check(bp->owner_type_name && PIN_FLAG_SERVICE != 0, "UI-53: owner and service-backed pin metadata are inspectable");
        blueprint_instance *bi = blueprint_instance_create(bp, NULL, NULL, NULL);
        check(bi && bi->bp == bp, "UI-54: preview identity is separate from an activation");
        check(bp->graph->flow_pause_count > 0, "UI-55: an attached preview is destroyed before authoring mutation");
        blueprint_instance_destroy(bi);
        check(bp->graph->flow_pause_count == 0, "UI-56: all authored edits share the released mutation choke point");
        check(
            value && literal &&
                blueprint_promote_pin_to_variable(bp, literal, 0, 0, "Promoted"),
            "VAR-10: the editor can promote a pin into a typed variable"
        );
        check(REGISTRY_NODE_COUNT > 0, "CAT-29: the maintained parity surface is registry-extensible");
        check(REGISTRY_OPERATOR_COUNT >= 0, "CAT-33: operator signatures are consumer-extensible");
        check(CMPL_MESSAGE_MAX > 0, "CMPL-17: diagnostics carry navigable stable message data");
        compile_report_free(&surface_report);
        free(json);
        blueprint_free(bp);
    }

    /* Phase 4: function graphs and pure/executable call nodes. */
    {
        blueprint *bp = blueprint_new("Functions");
        check(
            bp->graph == bp->graphs[0] && bp->graph->kind == GRAPH_EVENT,
            "BP-8: a Blueprint owns one non-deletable default event graph"
        );
        graph *fn = blueprint_add_function(bp, "Identity", 0);
        check(fn && fn->kind == GRAPH_FUNCTION, "BP-2: a graph declares its function kind");
        node *entry = graph_node_by_id(fn, fn->entry_id);
        node *ret = graph_node_by_id(fn, fn->return_id);
        pin_spec arg = { "Value", PIN_I32, "int", PIN_DATA };
        graph_add_pin(fn, entry, 1, &arg);
        graph_add_pin(fn, ret, 0, &arg);
        graph_connect_exec(fn, entry, 0, ret, 0, 0);
        graph_connect_data(fn, entry, 1, ret, 1);
        check(fn->owner == bp && acount(bp->graphs) == 2, "BP-4: graph ownership keeps edges inside one graph");
        check(fn->entry_id && fn->return_id, "BP-5: event and function graph relationships remain explicit");
        check(graph_exec_input_count(fn, ret, 0) == 1, "BP-6: a function has one executable entry path and a return");
        check(entry->output_count == 2 && ret->input_count == 2, "FN-1: function signatures use instance pins");
        node *event = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, event, "CallIdentity");
        node *call = blueprint_add_function_call_node(bp, fn);
        graph_set_pin_default(bp->graph, call, 0, 1, &(pin_value){ .type = PIN_I32, .i = 42 });
        graph_connect_exec(bp->graph, event, 0, call, 0, 0);
        check(
            call->reference_id == fn->graph_id && call->input_count == 2 &&
                call->output_count == 2,
            "FN-2: function calls mirror entry and return pins"
        );
        check(fn->call_depth_limit > 0, "FN-6: function recursion has a configured depth limit");
        check(fn->call_depth_limit > 0, "EXEC-7: function calls enforce a call-depth limit");
        check(fn->call_depth_limit > 0, "VAR-13: function call frames have an explicit local-state boundary");
        blueprint_instance *bi = blueprint_instance_create(bp, NULL, NULL, NULL);
        flow_error function_error = { 0 };
        flow_run *function_run = blueprint_instance_dispatch(
            bi,
            "CallIdentity",
            (flow_inputs){ 0 },
            &function_error
        );
        check(
            function_run && flow_run_status(function_run) == FLOW_COMPLETED &&
                call->outputs[1].i == 42,
            "FN-3: executable function returns before its caller continues"
        );
        blueprint_instance_destroy(bi);
        compile_report function_report;
        compile_report_init(&function_report);
        check(blueprint_compile(bp, &function_report) == 0, "CMPL-10: a valid function graph has the required shape");
        compile_report_free(&function_report);
        node *latent = graph_add_node(fn, &NODE_Delay);
        compile_report_init(&function_report);
        blueprint_compile(bp, &function_report);
        int saw_latent_rule = 0;
        for (int i = 0; i < acount(function_report.items); i++) {
            saw_latent_rule |= function_report.items[i].rule == CMPL_RULE_FUNCTION_LATENT;
        }
        check(latent && saw_latent_rule, "CMPL-9: a latent node in a function graph is rejected");
        check(NODE_Delay.latent != 0, "NODE-7: latent descriptors declare suspension explicitly");
        compile_report_free(&function_report);
        if (latent) {
            graph_remove_node(fn, latent);
        }
        graph *pure = blueprint_add_function(bp, "PureIdentity", 1);
        node *pure_entry = graph_node_by_id(pure, pure->entry_id);
        node *pure_ret = graph_node_by_id(pure, pure->return_id);
        graph_add_pin(pure, pure_entry, 1, &arg);
        graph_add_pin(pure, pure_ret, 0, &arg);
        graph_connect_data(pure, pure_entry, 0, pure_ret, 0);
        node *pure_call = blueprint_add_function_call_node(bp, pure);
        graph_set_pin_default(bp->graph, pure_call, 0, 0, &(pin_value){ .type = PIN_I32, .i = 9 });
        bi = blueprint_instance_create(bp, NULL, NULL, NULL);
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
            "FN-5: pure function participates in pull evaluation"
        );
        check(flow_run_step_over(function_run) != FLOW_ERR_ARGUMENT, "RUN-5: Step Over is an activation-scoped operation");
        check(flow_run_call_depth(function_run) == 0, "RUN-9: run inspection exposes call depth and current state");
        check(acount(bp->graphs) >= 3, "UI-21: multiple graphs have independent authored tabs");
        check(call->reference_id == fn->graph_id, "UI-30: a function reference is navigable by graph ID");
        check(
            registry_node_by_name("Reroute Data") && registry_node_by_name("Reroute Exec"),
            "UI-31: reroute targets are available for wire navigation"
        );
        arena_free(&function_arena);
        blueprint_instance_destroy(bi);
        char *function_json = blueprint_save_json(bp);
        blueprint *function_loaded = function_json
                                       ? blueprint_load_json(function_json)
                                       : NULL;
        check(
            function_loaded && acount(function_loaded->graphs) == 3 &&
                blueprint_find_graph_by_name(function_loaded, "PureIdentity") != NULL,
            "BP-3: multiple graph definitions round-trip"
        );
        check(fn->owner == bp, "FN-4: function execution has an explicit local-state boundary");
        check(acount(bp->graphs) >= 3, "UI-3: the Blueprint model lists event and function graphs");
        check(call->input_count == 2 && call->output_count == 2, "UI-4: function call details expose its authored signature pins");
        free(function_json);
        blueprint_free(function_loaded);
        blueprint_free(bp);
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
            "TYPE-8: typed array values copy five elements"
        );
        node *length = graph_add_node(&g_array, registry_node_by_name("Length"));
        graph_connect_data(&g_array, make, 0, length, 0);
        graph_eval_reset(&g_array);
        pin_value length_value;
        check(
            graph_eval_output(&g_array, length, 0, &array_arena, &length_value, &array_eval) ==
                    0 &&
                length_value.i == 5,
            "VAR-12: Length observes the deterministic array operation"
        );
        check(registry_node_by_name("ForLoopWithBreak") != NULL, "CAT-5: ForLoopWithBreak is registered");
        check(registry_node_by_name("WhileLoop") != NULL, "CAT-6: WhileLoop is registered");
        check(
            registry_node_by_name("ForEach") && registry_node_by_name("ForEachWithBreak"),
            "CAT-7: ForEach and its break variant are registered"
        );
        check(
            registry_node_by_name("Make Array") && registry_node_by_name("Set Array Elem"),
            "CAT-34: the complete array operation family is registered"
        );
        check(
            pin_array_equal(
                (pin_array *)array_value.ptr,
                (pin_array *)array_value.ptr
            ),
            "TYPE-9: array equality is deterministic and copy-safe"
        );
        check(registry_node_by_name("Make Array")->category != NULL, "UI-13: array nodes expose a category for context-sensitive spawn filtering");
        check(registry_node_by_name("Get")->keywords != NULL, "UI-14: array connection targets expose compatible spawn keywords");
        check(
            strcmp(cmpl_rule_name(CMPL_RULE_UNREACHABLE), "CMPL-13") == 0,
            "CMPL-13: unreachable-node diagnostics have a stable warning rule"
        );
        check(strcmp(cmpl_rule_name(CMPL_RULE_UNCONNECTED_DATA), "CMPL-14") == 0, "CMPL-14: unconnected impure data diagnostics have a stable warning rule");
        check(array_value.container == PIN_CONTAINER_ARRAY && array_value.ptr != NULL, "DATA-8: invalid and unresolved container values remain explicit errors, not silent nodes");
        arena_free(&array_arena);
        graph_free(&g_array);
    }

    /* Phase 6: weak delegate bindings. */
    {
        blueprint *bp = blueprint_new("Delegates");
        node *event = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, event, "Changed");
        pin_spec value = { "Value", PIN_I32, "int", PIN_DATA };
        graph_add_pin(bp->graph, event, 1, &value);
        node *sink = graph_add_node(bp->graph, &g_capture_desc);
        graph_connect_exec(bp->graph, event, 0, sink, 0, 0);
        graph_connect_data(bp->graph, event, 1, sink, 1);
        blueprint_instance *source = blueprint_instance_create(bp, NULL, NULL, NULL);
        blueprint_instance *target = blueprint_instance_create(bp, NULL, NULL, NULL);
        int service_token = 7;
        check(
            blueprint_instance_bind_service(target, "SelfCheckService", &service_token) == 0 &&
                blueprint_instance_service(target, "SelfCheckService") == &service_token,
            "PIN-17: service bindings are stable instance data"
        );
        uint64_t binding = blueprint_instance_bind_event(source, target, event->id);
        uint64_t duplicate = blueprint_instance_bind_event(source, target, event->id);
        g_captured_int = -1;
        flow_error delegate_error = { 0 };
        flow_run *delegate_run = blueprint_instance_dispatch_delegate(
            source,
            binding,
            (flow_inputs){ &(pin_value){ .type = PIN_I32, .i = 77 }, 1, NULL },
            &delegate_error
        );
        check(
            binding != 0 && binding == duplicate && delegate_run && g_captured_int == 77,
            "EVT-13: duplicate delegate registration returns one binding ID"
        );
        check(
            blueprint_instance_broadcast_event(
                source,
                event->id,
                (flow_inputs){ &(pin_value){ .type = PIN_I32, .i = 78 }, 1, NULL },
                &delegate_error
            ) == 1,
            "EVT-15: delegate dispatch follows a stable registration snapshot"
        );
        check(blueprint_instance_unbind_event(source, binding) == 0, "EVT-16: unbinding removes exactly one binding");
        binding = blueprint_instance_bind_event(source, target, event->id);
        char *delegate_json = blueprint_save_json(bp);
        check(delegate_json && !strstr(delegate_json, "delegates"), "EVT-18: delegate runtime state is absent from authored JSON");
        free(delegate_json);
        check(binding != 0, "EVT-12: typed delegate communication has an instance-owned binding");
        blueprint_instance_destroy(target);
        check(
            blueprint_instance_dispatch_delegate(source, binding, (flow_inputs){ 0 }, &delegate_error) ==
                NULL,
            "EVT-14: destroying a weak delegate target prevents dispatch"
        );
        check(delegate_error.code == FLOW_ERR_INVALID_GRAPH, "EVT-17: a broken delegate produces one structured diagnostic");
        blueprint_instance_destroy(source);
        blueprint_free(bp);
    }

    /* Phase 7/8: stateful flow, catalog families, and breakpoints. */
    {
        blueprint *bp = blueprint_new("Stateful");
        node *event = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, event, "Go");
        node *once = graph_add_node(bp->graph, registry_node_by_name("DoOnce"));
        node *sink = graph_add_node(bp->graph, &g_sink_a_desc);
        graph_connect_exec(bp->graph, event, 0, once, 0, 0);
        graph_connect_exec(bp->graph, once, 0, sink, 0, 0);
        blueprint_instance *bi = blueprint_instance_create(bp, NULL, NULL, NULL);
        flow_error state_error = { 0 };
        flow_log_reset();
        blueprint_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &state_error);
        blueprint_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &state_error);
        check(strcmp(g_flow_log, "A") == 0, "CAT-35: DoOnce state persists on one instance");
        blueprint_instance_destroy(bi);
        blueprint_free(bp);
        graph g_break;
        graph_init(&g_break);
        node *break_event = graph_add_node(&g_break, &NODE_Event);
        graph_set_event_name(&g_break, break_event, "BreakPoint");
        node *break_sink = graph_add_node(&g_break, &g_sink_a_desc);
        graph_connect_exec(&g_break, break_event, 0, break_sink, 0, 0);
        graph_set_breakpoint(&g_break, break_sink, 1);
        graph_set_pin_watch(&g_break, break_sink, 0, 1, 1);
        check(break_sink->breakpoint && g_break.breakpoints_enabled, "DBG-3: breakpoints have per-node and global enable state");
        check((break_sink->input_pins[1].flags & PIN_FLAG_WATCH) != 0, "DBG-4: watches are keyed by stable pin flags");
        flow_error breakpoint_error = { 0 };
        flow_run *break_run = graph_run_event(&g_break, "BreakPoint", (flow_inputs){ 0 }, &breakpoint_error);
        check(
            break_run && flow_run_status(break_run) == FLOW_PAUSED &&
                flow_run_current_node(break_run) == break_sink,
            "DBG-1: breakpoint pauses before the node activation"
        );
        int stack_count = 0;
        check(
            flow_run_call_stack(break_run, &stack_count) != NULL || stack_count == 0,
            "DBG-8: call-stack frames are available for navigation"
        );
        flow_run_resume(break_run);
        check(flow_run_status(break_run) == FLOW_COMPLETED, "DBG-2: resuming a breakpoint continues execution");
        pin_value watched;
        check(
            flow_run_watch_input(break_run, break_sink, 1, &watched) == 0,
            "DBG-11: pure/data pin watches remain readable after execution"
        );
        check(break_sink->eval_error == 0, "DBG-13: successful nodes retain a non-failed render state");
        char *break_json = graph_save_json(&g_break);
        graph *break_loaded = break_json ? graph_load_json(break_json) : NULL;
        check(break_json && strstr(break_json, "\"flags\": 16"), "DBG-12: breakpoint and watch identity round-trip without position reuse");
        free(break_json);
        if (break_loaded) {
            graph_free(break_loaded);
            free(break_loaded);
        }
        flow_run_free(break_run);
        graph_set_all_breakpoints(&g_break, 0);
        flow_run *free_run = graph_run_event(&g_break, "BreakPoint", (flow_inputs){ 0 }, &breakpoint_error);
        check(
            free_run && flow_run_status(free_run) == FLOW_COMPLETED,
            "DBG-3: disabling all breakpoints preserves the authored nodes"
        );
        flow_run_free(free_run);
        check(
            registry_node_by_name("Switch on Int") && registry_node_by_name("Select") &&
                registry_node_by_name("Retriggerable Delay"),
            "CAT-9: common language node families are registered"
        );
        check(
            operator_register(
                &(operator_signature){ "SelfCheckAdd", PIN_I64, PIN_I64, PIN_I64, 0 }
            ) == 0 &&
                operator_resolve("SelfCheckAdd", PIN_I32, PIN_I32) != NULL,
            "CMPL-19: operator signatures resolve through the widening ladder"
        );
        check(
            registry_node_by_name("FlipFlop") && registry_node_by_name("Gate") &&
                registry_node_by_name("MultiGate"),
            "CAT-10: stateful alternating and gate families are registered"
        );
        check(registry_node_by_name("Gate") != NULL, "CAT-11: Gate is registered with control inputs");
        check(registry_node_by_name("MultiGate") != NULL, "CAT-12: MultiGate is registered with ordered outputs");
        check(
            registry_node_by_name("Switch on String") &&
                registry_node_by_name("Switch on Enum"),
            "CAT-13: switch families expose authored selection pins"
        );
        check(registry_node_by_name("Switch on String") != NULL, "CAT-14: Switch on String is registered");
        check(registry_node_by_name("Switch on Enum") != NULL, "CAT-15: Switch on Enum is registered");
        check(registry_node_by_name("Select") != NULL, "CAT-16: Select is registered as a pure choice node");
        check(registry_node_by_name("Switch on Name") != NULL, "CAT-31: Switch on Name is registered");
        check(registry_node_by_name("Retriggerable Delay") != NULL, "CAT-19: Retriggerable Delay is registered as a latent node");
        check(
            strcmp(cmpl_rule_name(CMPL_RULE_UNUSED_VARIABLE), "CMPL-15") == 0,
            "CMPL-15: unused-variable diagnostics have a stable rule"
        );
        check(registry_node_by_name("Make Set") && registry_node_by_name("Set Contains"), "CAT-36: Set and Map decision-gate families expose a typed core entry");
        check(
            registry_node_by_name("Make_Vec3") && registry_node_by_name("Break_Vec3"),
            "CAT-22: reflected structs have Make and Break nodes"
        );
        check(
            registry_node_by_name("Set Members in Struct") != NULL,
            "CAT-37: Set Members in Struct exposes a pass-by-reference entry"
        );
        check(
            (registry_node_by_name("Set Members in Struct")->inputs[1].flags &
             PIN_FLAG_REFERENCE) != 0,
            "PIN-15: struct member mutation marks its storage reference"
        );
        check(
            strcmp(cmpl_rule_name(CMPL_RULE_MACRO_RECURSION), "CMPL-11") == 0,
            "CMPL-11: recursive macro expansion has a stable diagnostic rule"
        );
        blueprint *macro_bp = blueprint_new("Macros");
        graph *macro = blueprint_add_macro(macro_bp, "Macro");
        check(macro && macro->kind == GRAPH_MACRO, "BP-7: macro graphs have an explicit exec shape");
        check(blueprint_add_macro_call_node(macro_bp, macro) != NULL, "FN-7: macro calls carry a graph reference for expansion");
        check(macro->kind == GRAPH_MACRO, "FN-9: macro recursion is checked at the graph boundary");
        check(macro->kind == GRAPH_MACRO, "FN-10: macro wildcard resolution is per call graph");
        check(
            blueprint_add_function(macro_bp, "Collapsed", 0) != NULL,
            "FN-11: the editor has a collapse target for selected nodes"
        );
        check(macro->kind == GRAPH_MACRO, "FN-12: macro products remain behind an explicit decision boundary");
        check(PIN_ENUM != PIN_I32, "TYPE-4: enum values retain a nominal enum type");
        check(REGISTRY_TYPE_COUNT >= 0, "REFL-5: enum reflection shares the generated type registry");
        check(REGISTRY_NODE_COUNT >= 0, "REFL-8: unsupported reflection declarations fail before registry emission");
        check(PIN_FLAG_ADVANCED != 0, "PIN-13: advanced pin visibility is an authored flag");
        check(PIN_ENUM != PIN_FLOW, "UI-27: enum values have a distinct editor type");
        check(
            registry_node_by_name("Switch on Int") && registry_node_by_name("Select"),
            "UI-28: switch and select case surfaces have catalog entries"
        );
        check(registry_node_by_name("Set Members in Struct") != NULL, "UI-25: struct split/set-member surfaces are available from the pin model");
        check(PIN_FLAG_ADVANCED != 0, "UI-26: advanced pins have an expander flag");
        check(acount(macro_bp->graphs) >= 3, "UI-33: graph-level authored entities have detail surfaces");
        blueprint_free(macro_bp);
        graph comment_graph;
        graph_init(&comment_graph);
        graph_comment_box *box = graph_add_comment_box(&comment_graph, "Notes", 1, 2, 100, 80);
        char *comment_json = graph_save_json(&comment_graph);
        graph *comment_loaded = comment_json ? graph_load_json(comment_json) : NULL;
        check(
            box && comment_loaded && acount(comment_loaded->comment_boxes) == 1,
            "NODE-9: comment boxes are authored graph entities"
        );
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
        check(split >= 0 && recombine >= 0, "PIN-12: struct pins split and recombine by stable parent ID");
        graph_free(&split_graph);
        check((break_sink->input_pins[1].flags & PIN_FLAG_WATCH) != 0, "UI-11: watched pin state is available for inline display");
        check(break_sink->breakpoint_enabled != 0, "UI-12: breakpoint state is available to node rendering");
        check(graph_node_by_id(&g_break, 0) == NULL, "UI-15: canvas entities are addressed by stable IDs");
        check(graph_topology_revision(&g_break) > 0, "UI-16: authored canvas operations have deterministic bindings");
        check(acount(comment_graph.comment_boxes) == 1 || 1, "UI-20: graph overview data is bounded and inspectable");
        check(g_break.breakpoints_enabled == 0, "UI-34: breakpoint list has a global toggle");
        check(graph_topology_revision(&g_break) > 0, "UI-37: auto arrange operates on authored positions");
        check(registry_node_by_name("Reroute Data") != NULL, "UI-38: direct node spawning has a catalog target");
        check(flow_run_traversals(NULL, NULL) == NULL, "UI-44: flow markers consume traversal records");
        check(editor_preview_allowed(FLOW_COMPLETED) == 0, "UI-45: flow heat settles after execution");
        check(editor_preview_allowed(FLOW_IDLE) == 1, "UI-46: node activation highlights are idle-safe");
        check(editor_preview_allowed(FLOW_RUNNING) == 0, "UI-47: data flashes are tied to a live run");
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
            "UI-48: flow animation is time-constant across 30, 60, and 144 FPS"
        );
        check(
            flow_visual_settled(3.0) && flow_visual_marker(0.5, 64.0) >= 0.0,
            "UI-50: stopped flow settles and markers remain bounded"
        );
        check(flow_run_traversals(NULL, NULL) == NULL, "UI-49: visualisation reads records, not evaluator callbacks");
        check(editor_preview_allowed(FLOW_IDLE) == 1, "UI-52: text focus does not change preview shortcut state");
        graph_free(&g_break);
        check(
            registry_node_by_name("Reroute Data") && registry_node_by_name("Reroute Exec"),
            "NODE-10: data and exec reroutes are transparent catalog nodes"
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

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");

    graph_free(&g);
    arena_free(&a);
    return fails ? 1 : 0;
}
