#include "graph/runtime/behavior_data.h"

#include "graph/model/pin_conv.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/runtime.h"

#include <assert.h>

static int fail(eval_context *ctx, const node *n, int pin, const char *message)
{
    ctx->status = (eval_status){ NODE_EVAL_INVALID, n, pin, message };
    return NODE_EVAL_INVALID;
}

int behavior_data_instruction(behavior_data_context *context, const graph_ir_behavior_instruction *instruction)
{
    eval_context *ctx = context->eval;
    node *n = (node *)instruction->node;
    runtime_node *state = runtime_store_node(ctx->runtime, n);
    pin_value *input = runtime_node_inputs(state);
    switch (instruction->op) {
        case GRAPH_IR_MEMO_BRANCH: return runtime_node_computed(state);
        case GRAPH_IR_EVALUATE:
            return eval_prepared_node(n, context->arena, ctx);
        case GRAPH_IR_BIND_INPUT: {
            const pin_spec *spec = &n->input_pins[instruction->pin];
            const char *error = eval_context_resolve_input(
                ctx,
                spec,
                spec->binding,
                context->arena,
                &input[instruction->pin]
            );
            return error ? fail(ctx, n, instruction->pin, error) : 0;
        }
        case GRAPH_IR_READ_SLOT: {
            const node *source = instruction->source;
            const pin_value *outputs = runtime_node_outputs_const(
                runtime_store_node(ctx->runtime, source)
            );
            pin_value value = outputs[instruction->source_pin];
            if (instruction->field) {
                if (value.type != PIN_STRUCT || !value.ptr) {
                    return fail(ctx, source, instruction->source_pin, "exposed field has no aggregate value");
                }
                pin_read_field(value.ptr, instruction->field, &value);
            }
            if (ctx->on_data_edge) {
                int source_pin = instruction->target;
                ctx->on_data_edge(source, source_pin, n, instruction->pin, ctx->on_data_edge_user);
            }
            if (ctx->observer && ctx->on_data_edge &&
                (source->output_pins[instruction->target].flags & PIN_FLAG_WATCH)) {
                flow_watch_store(ctx->observer, source, 1, instruction->target, &value);
            }
            const pin_spec *spec = &n->input_pins[instruction->pin];
            if (instruction->conversion) {
                value = instruction->conversion(&value, 1, context->arena);
                pin_coerce(&value, spec);
                if (value.type != spec->type ||
                    (value.type == PIN_STRUCT && !value.ptr)) {
                    return fail(ctx, n, instruction->pin, "edge conversion failed");
                }
            }
            pin_coerce(&value, spec);
            input[instruction->pin] = value;
            return 0;
        }
    }
    return fail(ctx, n, instruction->pin, "unknown behavior instruction");
}

int behavior_data_call(node *n, arena *a, eval_context *ctx)
{
    behavior_instance *instance = ctx->instance;
    graph *target = behavior_asset_find_graph(instance->asset, n->reference_id);
    if (!target || target->kind != GRAPH_FUNCTION) {
        return fail(ctx, n, 0, "pure function graph is missing");
    }
    if (target->call_depth_limit &&
        ctx->call_depth >= (unsigned)target->call_depth_limit) {
        return fail(ctx, n, 0, "pure function call depth limit exceeded");
    }
    const behavior_graph_program
        *program = behavior_program_graph(instance->asset->program, target);
    node *entry = graph_node_by_id(target, target->entry_id);
    node *ret = graph_node_by_id(target, target->return_id);
    if (!program || !entry || !ret) {
        return fail(ctx, n, -1, "compiled function entry or return is missing");
    }
    runtime_node *caller = runtime_store_node(ctx->runtime, n);
    pin_value *arguments = runtime_node_inputs(caller);
    pin_value *outputs = runtime_node_outputs(caller);
    const behavior_function *function = behavior_program_function(program, entry);
    if (function && !instance->trace_enabled && !ctx->on_data_edge &&
        (!ctx->observer ||
         (!ctx->observer->step_mode && !acount(ctx->observer->watches))) &&
        behavior_function_pure_depth(function, ctx->call_depth + 1)) {
        /* Exposed output fields occupy caller pins but are not independent
         * function results. Use canonical Return slots, then scatter roots. */
        pin_value *results = outputs;
        if (ret->input_count != n->output_count) {
            results = arena_alloc(a, (size_t)ret->input_count * sizeof *results);
            for (int i = 0; i < ret->input_count; i++) {
                results[i] = ret->input_pins[i].default_value;
            }
        }
        behavior_function_prepare_results(function, results, a);
        behavior_function_context call = {
            .variables = instance->variables,
            .arguments = arguments,
            .argument_count = n->input_count,
            .results = results,
            .delta_seconds = ctx->delta_seconds,
        };
        int result = behavior_function_run(function, &call, ctx->backend == BEHAVIOR_SLJIT);
        if (result) {
            const behavior_function_source
                *source = behavior_function_source_at(function, call.error_source);
            return fail(ctx, graph_node_by_id(source->graph, call.node_id), call.error_pin, "data function produced an invalid result");
        }
        if (results != outputs) {
            int at = 0;
            for (int i = 0; i < ret->input_count; i++) {
                while (at < n->output_count && n->output_pins[at].field_source) {
                    at++;
                }
                assert(at < n->output_count);
                outputs[at++] = results[i];
            }
        }
        return 0;
    }
    runtime_store *runtime = runtime_store_create(target);
    eval_context nested = *ctx;
    nested.runtime = runtime;
    nested.runtime_owned = 0;
    nested.program = program;
    nested.call_depth++;
    pin_value *parameters = runtime_node_outputs(runtime_store_node(runtime, entry));
    int argument = 0;
    for (int i = 0; i < entry->output_count; i++) {
        if (entry->output_pins[i].kind == PIN_DATA &&
            !entry->output_pins[i].field_source) {
            parameters[i] = arguments[argument++];
            pin_coerce(&parameters[i], &entry->output_pins[i]);
        }
    }
    int result = 0;
    int output = 0;
    for (int i = 0; i < ret->input_count; i++) {
        if (ret->input_pins[i].kind != PIN_DATA) {
            continue;
        }
        while (output < n->output_count && n->output_pins[output].field_source) {
            output++;
        }
        pin_value value;
        result = behavior_program_input(program, ret, i, a, &value, &nested, ctx->backend);
        if (result) {
            ctx->status = nested.status;
            break;
        }
        /* Managed return values must outlive the callee's slot storage. */
        result = flow_copy_input(a, &outputs[output++], &value);
        if (result) {
            result = fail(ctx, n, i, "function return copy failed");
            break;
        }
    }
    runtime_store_free(runtime);
    return result;
}

void behavior_data_scalar(behavior_data_context *context, const node *n)
{
    runtime_node *state = runtime_store_node(context->eval->runtime, n);
    context->inputs = runtime_node_inputs(state);
    context->outputs = runtime_node_outputs(state);
    context->outputs[0] = context->inputs[0];
    runtime_node_set_computed(state, 1);
}

/* Returns an output ordinal or a negative diagnostic. The native backend
 * emits the conditional jump; the bytecode backend selects the same target. */
int behavior_control_branch(flow_context *ctx)
{
    pin_value value;
    int result = flow_context_eval_data(ctx, 1, &value);
    if (result) {
        return result;
    }
    if (value.type != PIN_BOOL) {
        return flow_context_fail(ctx, FLOW_ERR_INVALID_GRAPH, "Branch condition is not bool");
    }
    return value.b ? 0 : 1;
}
