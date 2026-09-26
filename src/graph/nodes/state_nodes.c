#include "graph/nodes/state_nodes.h"

#include "graph/model/registry.h"
#include "graph/runtime/flow.h"
#include "graph/runtime/instance.h"
#include "support/xalloc.h"

#include <stdlib.h>
#include <string.h>

static pin_spec do_once_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
    { "Reset", PIN_FLOW, NULL, PIN_EXEC },
    { "Start Closed", PIN_BOOL, "bool", PIN_DATA },
};
static pin_spec do_once_outputs[] = {
    { "Completed", PIN_FLOW, NULL, PIN_EXEC },
};
static pin_spec do_n_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
    { "Reset", PIN_FLOW, NULL, PIN_EXEC },
    { "N", PIN_I32, "int", PIN_DATA },
};
static pin_spec do_n_outputs[] = {
    { "Exit", PIN_FLOW, NULL, PIN_EXEC },
    { "Counter", PIN_I32, "int", PIN_DATA },
};
static pin_spec flip_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
};
static pin_spec flip_outputs[] = {
    { "A", PIN_FLOW, NULL, PIN_EXEC },
    { "B", PIN_FLOW, NULL, PIN_EXEC },
    { "Is A", PIN_BOOL, "bool", PIN_DATA },
};
static pin_spec gate_inputs[] = {
    { "Enter", PIN_FLOW, NULL, PIN_EXEC },
    { "Open", PIN_FLOW, NULL, PIN_EXEC },
    { "Close", PIN_FLOW, NULL, PIN_EXEC },
    { "Toggle", PIN_FLOW, NULL, PIN_EXEC },
    { "Start Closed", PIN_BOOL, "bool", PIN_DATA },
};
static pin_spec gate_outputs[] = {
    { "Exit", PIN_FLOW, NULL, PIN_EXEC },
};
static pin_spec multi_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
    { "Reset", PIN_FLOW, NULL, PIN_EXEC },
    { "Loop", PIN_BOOL, "bool", PIN_DATA },
    { "Random", PIN_BOOL, "bool", PIN_DATA },
    { "Start Index", PIN_I32, "int", PIN_DATA },
};
static pin_spec multi_outputs[] = {
    { "Out0", PIN_FLOW, NULL, PIN_EXEC },
    { "Out1", PIN_FLOW, NULL, PIN_EXEC },
};
static pin_spec retrigger_inputs[] = {
    { "Exec", PIN_FLOW, NULL, PIN_EXEC },
    { "Duration", PIN_F64, "double", PIN_DATA },
};
static pin_spec retrigger_outputs[] = {
    { "Completed", PIN_FLOW, NULL, PIN_EXEC },
};

static behavior_instance *state_instance(flow_context *ctx)
{
    return flow_context_instance(ctx);
}

static int state_fail(flow_context *ctx)
{
    return flow_context_fail(ctx, FLOW_ERR_STATE, "stateful flow node requires a Behavior Asset instance");
}

static int flow_do_once(node *n, flow_context *ctx)
{
    behavior_instance *bi = state_instance(ctx);
    if (!bi) {
        return state_fail(ctx);
    }
    pin_value *state = behavior_instance_scratch(bi, n->id);
    if (flow_context_input_pin(ctx) == 1) {
        state->b = false;
        state->type = PIN_BOOL;
        return FLOW_OK;
    }
    if (!state->type) {
        pin_value closed;
        int result = flow_context_eval_data(ctx, 2, &closed);
        if (result) {
            return result;
        }
        state->type = PIN_BOOL;
        state->b = closed.b;
    }
    if (state->b) {
        return FLOW_OK;
    }
    state->b = true;
    return flow_context_continue(ctx, 0);
}

static int flow_do_n(node *n, flow_context *ctx)
{
    behavior_instance *bi = state_instance(ctx);
    if (!bi) {
        return state_fail(ctx);
    }
    pin_value *state = behavior_instance_scratch(bi, n->id);
    if (flow_context_input_pin(ctx) == 1) {
        state->type = PIN_I32;
        state->i = 0;
        return FLOW_OK;
    }
    pin_value limit;
    int result = flow_context_eval_data(ctx, 2, &limit);
    if (result) {
        return result;
    }
    if (limit.type != PIN_I32) {
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "DoN limit is not int");
    }
    if (state->type != PIN_I32) {
        state->type = PIN_I32;
        state->i = 0;
    }
    *flow_context_output_value(ctx, 1) = (pin_value){ .type = PIN_I32,
                                                      .type_name = "int",
                                                      .i = state->i };
    if (state->i >= limit.i) {
        return FLOW_OK;
    }
    state->i++;
    return flow_context_continue(ctx, 0);
}

static int flow_flip_flop(node *n, flow_context *ctx)
{
    behavior_instance *bi = state_instance(ctx);
    if (!bi) {
        return state_fail(ctx);
    }
    pin_value *state = behavior_instance_scratch(bi, n->id);
    state->type = PIN_BOOL;
    state->b = !state->b;
    *flow_context_output_value(ctx, 2) = (pin_value){ .type = PIN_BOOL,
                                                      .b = state->b };
    return flow_context_continue(ctx, state->b ? 0 : 1);
}

static int flow_gate(node *n, flow_context *ctx)
{
    behavior_instance *bi = state_instance(ctx);
    if (!bi) {
        return state_fail(ctx);
    }
    pin_value *state = behavior_instance_scratch(bi, n->id);
    if (state->type != PIN_BOOL) {
        pin_value closed;
        int result = flow_context_eval_data(ctx, 4, &closed);
        if (result) {
            return result;
        }
        state->type = PIN_BOOL;
        state->b = !closed.b;
    }
    switch (flow_context_input_pin(ctx)) {
        case 1: state->b = true; return FLOW_OK;
        case 2: state->b = false; return FLOW_OK;
        case 3: state->b = !state->b; return FLOW_OK;
        default: return state->b ? flow_context_continue(ctx, 0) : FLOW_OK;
    }
}

static int flow_multi_gate(node *n, flow_context *ctx)
{
    behavior_instance *bi = state_instance(ctx);
    if (!bi) {
        return state_fail(ctx);
    }
    pin_value *state = behavior_instance_scratch(bi, n->id);
    if (flow_context_input_pin(ctx) == 1) {
        if (state->type == PIN_STR) {
            memset((char *)state->str, '0', (size_t)n->output_count);
        }
        return FLOW_OK;
    }
    pin_value start, random, loop;
    int result = flow_context_eval_data(ctx, 4, &start);
    if (!result) {
        result = flow_context_eval_data(ctx, 3, &random);
    }
    if (!result) {
        result = flow_context_eval_data(ctx, 2, &loop);
    }
    if (result) {
        return result;
    }
    if (state->type != PIN_STR) {
        char *used = xmalloc((size_t)n->output_count + 1);
        memset(used, '0', (size_t)n->output_count);
        used[n->output_count] = '\0';
        apush(bi->strings, used);
        *state = (pin_value){ .type = PIN_STR, .str = used };
    }
    char *used = (char *)state->str;
    int remaining = 0;
    for (int i = 0; i < n->output_count; i++) {
        remaining += used[i] == '0';
    }
    if (!remaining && !loop.b) {
        return FLOW_OK;
    }
    if (!remaining) {
        memset(used, '0', (size_t)n->output_count);
        remaining = n->output_count;
    }
    if (!remaining) {
        return FLOW_OK;
    }
    int first = start.i >= 0 && start.i < n->output_count ? start.i : 0;
    int choice = random.b ? (int)((double)rand() / ((double)RAND_MAX + 1) * remaining)
                          : 0;
    if (remaining == n->output_count && start.i >= 0 && start.i < n->output_count) {
        choice = 0;
    }
    for (int i = 0; i < n->output_count; i++) {
        int output = (first + i) % n->output_count;
        if (used[output] != '0' || choice-- > 0) {
            continue;
        }
        used[output] = '1';
        return flow_context_continue(ctx, output);
    }
    return FLOW_OK;
}

static int flow_retriggerable_delay(node *n, flow_context *ctx)
{
    pin_value duration;
    int result = flow_context_eval_data(ctx, 1, &duration);
    if (result) {
        return result;
    }
    if (duration.type != PIN_F64) {
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "Retriggerable Delay duration is not double");
    }
    return flow_context_retrigger_delay(ctx, flow_context_now(ctx) + duration.d);
}

node_desc NODE_DoOnce = {
    .name = "DoOnce",
    .input_count = 3,
    .output_count = 1,
    .inputs = do_once_inputs,
    .outputs = do_once_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_do_once,
    .instance_state = true,
    .display_class = NODE_DISPLAY_FLOW,
    .category = "Flow Control",
    .keywords = "stateful do once reset",
    .description = "Fires Completed on the first Exec only until Reset; Start "
                   "Closed blocks it until the first Reset.",
};
node_desc NODE_DoN = {
    .name = "DoN",
    .input_count = 3,
    .output_count = 2,
    .inputs = do_n_inputs,
    .outputs = do_n_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_do_n,
    .instance_state = true,
    .display_class = NODE_DISPLAY_FLOW,
    .category = "Flow Control",
    .keywords = "stateful do n counter reset",
    .description = "Fires Exit on the first N Execs until Reset; Counter is "
                   "how many times Exit fired before this Exec.",
};
node_desc NODE_FlipFlop = {
    .name = "FlipFlop",
    .input_count = 1,
    .output_count = 3,
    .inputs = flip_inputs,
    .outputs = flip_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_flip_flop,
    .instance_state = true,
    .display_class = NODE_DISPLAY_FLOW,
    .category = "Flow Control",
    .keywords = "stateful alternating",
    .description = "Fires A and B alternately on each Exec, starting with A; "
                   "Is A reports which one fired.",
};
node_desc NODE_Gate = {
    .name = "Gate",
    .input_count = 5,
    .output_count = 1,
    .inputs = gate_inputs,
    .outputs = gate_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_gate,
    .instance_state = true,
    .display_class = NODE_DISPLAY_FLOW,
    .category = "Flow Control",
    .keywords = "stateful gate open close toggle",
    .description = "Passes Enter to Exit while open; Open, Close and Toggle "
                   "change the state, which starts open unless Start Closed.",
};
node_desc NODE_MultiGate = {
    .name = "MultiGate",
    .input_count = 5,
    .output_count = 2,
    .inputs = multi_inputs,
    .outputs = multi_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flow_multi_gate,
    .instance_state = true,
    .display_class = NODE_DISPLAY_FLOW,
    .category = "Flow Control",
    .keywords = "stateful multigate sequence reset",
    .description = "Fires one unused output per Exec, in order from Start "
                   "Index or at random; Loop restarts once all fire, Reset "
                   "clears.",
};
node_desc NODE_RetriggerableDelay = {
    .name = "Retriggerable Delay",
    .input_count = 2,
    .output_count = 1,
    .inputs = retrigger_inputs,
    .outputs = retrigger_outputs,
    .flow_kind = NODE_FLOW_DELAY,
    .flow = flow_retriggerable_delay,
    .latent = 1,
    .display_class = NODE_DISPLAY_FLOW,
    .category = "Flow Control",
    .keywords = "latent delay retrigger",
    .description = "Waits Duration seconds, then fires Completed; triggering "
                   "it again while waiting restarts the wait.",
};

node_desc *const STATE_NODES[] = {
    &NODE_DoOnce, &NODE_DoN,       &NODE_FlipFlop,
    &NODE_Gate,   &NODE_MultiGate, &NODE_RetriggerableDelay,
};
const int STATE_NODE_COUNT = sizeof STATE_NODES / sizeof STATE_NODES[0];

const registry_pack CORE_STATE_PACK = {
    .name = "core.state",
    .nodes = STATE_NODES,
    .node_count = sizeof STATE_NODES / sizeof STATE_NODES[0],
};
