#include "graph/authoring/graph_edit.h"
#include "test/suite_internal.h"

/* Vendor test helper: render one fmt format into a stream and compare. The
 * lowercase "fmt:" check prefix keeps these out of the requirement-ID
 * scanner, where FMT-x means the serialization family. */
void fmt_check_stream(fmt_stream *ss, const char *expected, const char *msg)
{
    test_check(ss->data && strcmp(ss->data, expected) == 0, msg);
    fmt_close(ss);
    *ss = (fmt_stream){ 0 };
}

/* Hand-written constant node: no inputs, one output, eval pre-sets nothing
 * (the caller sets output[0]). This is the escape hatch: a node_desc authored
 * directly in C, not emitted by reflect. */
static pin_spec g_const_out[] = {
    { "value", PIN_F32, "float" },
};
node_desc g_const_desc = {
    .name = "const_f32",
    .input_count = 0,
    .output_count = 1,
    .inputs = NULL,
    .outputs = g_const_out,
    .pure_kind = NODE_PURE_CONST,
    .display_class = NODE_DISPLAY_LITERAL,
};

/* Hand-written counting node for the memo check: increments once per eval. */
int g_count;
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
node_desc g_counter_desc = {
    .name = "counter",
    .input_count = 0,
    .output_count = 1,
    .inputs = NULL,
    .outputs = g_counter_out,
    .pure_kind = NODE_PURE_CALL,
    .fn = eval_counter,
};

static pin_spec g_test_add_inputs[] = {
    { "a", PIN_F32, "float" },
    { "b", PIN_F32, "float" },
};
static pin_spec g_test_add_outputs[] = {
    { "result", PIN_F32, "float" },
};
static pin_value eval_test_add(const pin_value *inputs, int input_count, arena *a)
{
    (void)a;
    assert(input_count == 2);
    return (pin_value){
        .type = PIN_F32,
        .type_name = "float",
        .f = inputs[0].f + inputs[1].f,
    };
}
node_desc g_test_add_desc = {
    .name = "test_add_f32",
    .input_count = 2,
    .output_count = 1,
    .inputs = g_test_add_inputs,
    .outputs = g_test_add_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = eval_test_add,
};

static pin_spec g_all_types_outputs[] = {
    { "bool", PIN_BOOL, "bool" },         { "i8", PIN_I8, "int8_t" },
    { "u8", PIN_U8, "uint8_t" },          { "i16", PIN_I16, "int16_t" },
    { "u16", PIN_U16, "uint16_t" },       { "i32", PIN_I32, "int" },
    { "u32", PIN_U32, "unsigned" },       { "i64", PIN_I64, "int64_t" },
    { "u64", PIN_U64, "uint64_t" },       { "f32", PIN_F32, "float" },
    { "f64", PIN_F64, "double" },         { "str", PIN_STR, "const char *" },
    { "struct", PIN_STRUCT, "CF_V3" },    { "enum", PIN_ENUM, "TestEnum" },
    { "flow", PIN_FLOW, NULL, PIN_EXEC },
};
node_desc g_all_types_desc = {
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
char g_flow_log[128];
int g_flow_log_count;
int g_flow_indices[128];
int g_flow_index_count;
static int g_flow_sink_fail;

void flow_log_reset(void)
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

node_desc g_sink_a_desc = {
    .name = "A",
    .input_count = 2,
    .output_count = 1,
    .inputs = g_flow_sink_inputs,
    .outputs = g_flow_sink_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_sink,
};
node_desc g_sink_b_desc = {
    .name = "B",
    .input_count = 2,
    .output_count = 1,
    .inputs = g_flow_sink_inputs,
    .outputs = g_flow_sink_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_sink,
};
node_desc g_sink_c_desc = {
    .name = "C",
    .input_count = 2,
    .output_count = 1,
    .inputs = g_flow_sink_inputs,
    .outputs = g_flow_sink_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_sink,
};
node_desc g_sink_done_desc = {
    .name = "D",
    .input_count = 2,
    .output_count = 1,
    .inputs = g_flow_sink_inputs,
    .outputs = g_flow_sink_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_sink,
};
node_desc g_sink_body_desc = {
    .name = "Body",
    .input_count = 2,
    .output_count = 1,
    .inputs = g_flow_sink_inputs,
    .outputs = g_flow_sink_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_sink,
};
node_desc g_sink_error_desc = {
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
int g_parameter_integer;
int g_parameter_flag;
char g_parameter_text[32];

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

node_desc g_parameter_sink_desc = {
    .name = "ParameterSink",
    .input_count = 4,
    .output_count = 1,
    .inputs = g_parameter_sink_inputs,
    .outputs = g_parameter_sink_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_parameter_sink,
};

char g_print_messages[8][64];
const node *g_print_origins[8];
int g_print_count;
flow_run *g_pause_target;

void print_sink(const node *origin, const char *message, void *user)
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
    { "Value", PIN_STRUCT, "CF_V3", PIN_DATA },
};
CF_V3 g_watch_struct;

static int flow_watch_struct(node *n, flow_context *ctx)
{
    g_watch_struct = (CF_V3){ 4.0f, 5.0f, 6.0f };
    *flow_context_output_value(ctx, 1) = (pin_value){
        .type = PIN_STRUCT,
        .ptr = &g_watch_struct,
        .type_name = "CF_V3",
    };
    return flow_context_continue(ctx, 0);
}

node_desc g_watch_desc = {
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
int g_captured_int;
char g_captured_str[64];
uint64_t g_captured_handle;
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
node_desc g_capture_desc = {
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
node_desc g_owned_watch_desc = {
    .name = "WatchOwnedValues",
    .input_count = 3,
    .output_count = 1,
    .inputs = g_owned_watch_inputs,
    .outputs = g_capture_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_watch_owned,
};
int g_foreach_values[8];
int g_foreach_count;
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
node_desc g_foreach_capture_desc = {
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
node_desc g_str_capture_desc = {
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
    { "Value", PIN_HANDLE, "CF_V3", PIN_DATA },
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
node_desc g_handle_capture_desc = {
    .name = "CaptureHandle",
    .input_count = 2,
    .output_count = 1,
    .inputs = g_handle_capture_inputs,
    .outputs = g_capture_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_capture_handle,
};

node *make_string(graph *g, const char *value)
{
    node *n = graph_add_node(g, registry_node_by_name("String"));
    if (!n) {
        return NULL;
    }
    pin_value authored = { .type = PIN_STR, .type_name = "const char *", .str = value };
    graph_set_pin_default(g, n, 0, 0, &authored);
    return n;
}

node *make_const(graph *g, float v)
{
    node *n = graph_add_node(g, &g_const_desc);
    if (!n) {
        return NULL;
    }
    pin_value authored = { .type = PIN_F32, .type_name = "float", .f = v };
    graph_set_pin_default(g, n, 1, 0, &authored);
    return n;
}
