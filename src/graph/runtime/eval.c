#include "graph/runtime/eval.h"

#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/nodes/nodes.h"
#include "graph/runtime/behavior_data.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/runtime.h"
#include "graph/runtime/value_array.h"
#include "support/arena.h"

#include <string.h>

static int eval_copy_struct(arena *a, pin_value *value)
{
    if (value->type != PIN_STRUCT || value->container != PIN_CONTAINER_SINGLE ||
        !value->ptr) {
        return 0;
    }
    const type_desc *type = value->type_name ? registry_type_by_name(value->type_name)
                                             : NULL;
    if (!type || !type->size) {
        return -1;
    }
    void *copy = arena_alloc(a, type->size);
    if (!copy) {
        return -1;
    }
    memcpy(copy, value->ptr, type->size);
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (field->type != PIN_STRUCT && field->type != PIN_STR &&
            field->type != PIN_NAME) {
            continue;
        }
        pin_value member;
        pin_read_field(value->ptr, field, &member);
        if (pin_value_copy_to_arena(a, &member, &member) ||
            eval_copy_struct(a, &member)) {
            return -1;
        }
        pin_write_field(copy, field, member);
    }
    value->ptr = copy;
    return 0;
}

static int eval_value_matches(pin_value *value, const pin_spec *spec)
{
    pin_spec source = {
        .type = value->type,
        .type_name = value->type_name,
        .container = value->container,
        .kind = PIN_DATA,
    };
    if (!pin_compatible(&source, spec)) {
        return 0;
    }
    pin_coerce(value, spec);
    value->type_name = spec->type_name;
    value->container = spec->container;
    return 1;
}

static const char *eval_resolve_component(
    const eval_context *ctx,
    const pin_spec *spec,
    const pin_binding *binding,
    arena *a,
    pin_value *out
)
{
    if (!binding->component_key || !binding->component_key[0] ||
        (binding->kind == PIN_BINDING_COMPONENT_FIELD &&
         (!binding->field_name || !binding->field_name[0]))) {
        return "component binding has incomplete identity";
    }
    eval_component_reader *reader = eval_context_service(ctx, EVAL_COMPONENT_READER_SERVICE);
    if (!reader || !reader->read || !ctx->instance) {
        return "component binding requires a component reader and behavior "
               "instance";
    }
    pin_value owner = behavior_instance_owner(ctx->instance);
    if (owner.type != PIN_HANDLE || !owner.u64) {
        return "component binding requires a live Self entity";
    }
    const char *field_name = binding->kind == PIN_BINDING_COMPONENT
                               ? NULL
                               : binding->field_name;
    int result = reader->read(reader->user, owner.u64, binding->component_key, field_name, out);
    if (result == EVAL_COMPONENT_READ_MISSING_COMPONENT) {
        return "Self does not have the bound component";
    }
    if (result == EVAL_COMPONENT_READ_MISSING_FIELD) {
        return "component binding references a missing field";
    }
    if (result == EVAL_COMPONENT_READ_INVALID_OWNER) {
        return "Self does not have the bound component";
    }
    if (result != EVAL_COMPONENT_READ_OK) {
        return "component binding read failed";
    }
    if (pin_value_copy_to_arena(a, out, out) || eval_copy_struct(a, out)) {
        return "component binding value could not be copied";
    }
    if (!eval_value_matches(out, spec)) {
        return "component binding type is incompatible with the input";
    }
    return NULL;
}

const char *eval_context_resolve_input(
    const eval_context *ctx,
    const pin_spec *spec,
    const pin_binding *binding,
    arena *a,
    pin_value *out
)
{
    if (!ctx || !spec || !a || !out) {
        return "invalid input binding arguments";
    }
    if (!binding) {
        if ((spec->flags & PIN_FLAG_DEFAULT_SELF) && !spec->default_value.u64) {
            if (!ctx->instance) {
                return "Self requires an instance";
            }
            pin_value owner = behavior_instance_owner(ctx->instance);
            if (!eval_value_matches(&owner, spec)) {
                return "Target requires Self or an explicit entity";
            }
            *out = owner;
            return NULL;
        }
        *out = spec->default_value;
        return NULL;
    }
    switch (binding->kind) {
        case PIN_BINDING_VARIABLE:
            if (!ctx->instance) {
                return "variable binding requires an instance";
            }
            if (binding->source_asset &&
                (!ctx->instance->asset || !ctx->instance->asset->name ||
                 strcmp(binding->source_asset, ctx->instance->asset->name) != 0)) {
                return "variable binding belongs to another behavior";
            }
            if (behavior_instance_copy_variable(ctx->instance, binding->variable_id, a, out) !=
                0) {
                return "variable binding references a lost variable";
            }
            return eval_value_matches(out, spec)
                     ? NULL
                     : "variable binding type is incompatible with the input";
        case PIN_BINDING_COMPONENT_FIELD:
        case PIN_BINDING_COMPONENT:
            return eval_resolve_component(ctx, spec, binding, a, out);
        case PIN_BINDING_DELTA_TIME:
            if (spec->container != PIN_CONTAINER_SINGLE ||
                (spec->type != PIN_F32 && spec->type != PIN_F64)) {
                return "Delta Time is only available for real inputs";
            }
            *out = (pin_value){ .type = PIN_F32,
                                .container = PIN_CONTAINER_SINGLE,
                                .type_name = "float",
                                .f = (float)ctx->delta_seconds };
            return eval_value_matches(out, spec)
                     ? NULL
                     : "Delta Time type is incompatible with the input";
        default: return "unknown input binding kind";
    }
}

void *eval_context_service(const eval_context *ctx, const char *type_name)
{
    for (int i = 0; i < acount(ctx->services); i++) {
        if (strcmp(ctx->services[i].type_name, type_name) == 0) {
            return ctx->services[i].value;
        }
    }
    return NULL;
}

void eval_context_init(eval_context *ctx)
{
    if (ctx) {
        *ctx = (eval_context){ 0 };
    }
}

void eval_context_free(eval_context *ctx)
{
    eval_context_detach_runtime(ctx);
}

void eval_context_reset(eval_context *ctx)
{
    if (ctx) {
        ctx->status = (eval_status){ 0, NULL, -1, NULL };
    }
}

void eval_context_reset_runtime(eval_context *ctx)
{
    if (ctx) {
        runtime_store_reset(ctx->runtime);
    }
}

void eval_context_attach_runtime(eval_context *ctx, runtime_store *runtime)
{
    if (!ctx) {
        return;
    }
    eval_context_detach_runtime(ctx);
    ctx->runtime = runtime;
    ctx->runtime_owned = 0;
}

void eval_context_detach_runtime(eval_context *ctx)
{
    if (!ctx) {
        return;
    }
    if (ctx->runtime_owned) {
        runtime_store_free(ctx->runtime);
    }
    ctx->runtime = NULL;
    ctx->runtime_owned = 0;
}

const eval_status *eval_context_status(const eval_context *ctx)
{
    return ctx ? &ctx->status : NULL;
}

static int graph_has_node(const graph *g, const node *n)
{
    return g && n && graph_node_by_id(g, n->id) == n;
}

static runtime_node *runtime_state(eval_context *ctx, node *n)
{
    if (!ctx || !ctx->runtime ||
        !graph_has_node(runtime_store_graph(ctx->runtime), n)) {
        return NULL;
    }
    return runtime_store_node(ctx->runtime, n);
}

static int fail(eval_context *ctx, node *n, int pin, int code, const char *message)
{
    if (ctx) {
        ctx->status = (eval_status){ code, n, pin, message };
        runtime_node *state = runtime_state(ctx, n);
        if (state) {
            runtime_node_set_visiting(state, 0);
        }
    }
    return code;
}

static int eval_pure_function(const graph *g, node *n, arena *a, eval_context *ctx)
{
    if (!ctx->instance) {
        return fail(ctx, n, 0, NODE_EVAL_INVALID, "pure function requires an instance");
    }
    graph *target = behavior_asset_find_graph(ctx->instance->asset, n->reference_id);
    if (!target || target->kind != GRAPH_FUNCTION) {
        return fail(ctx, n, 0, NODE_EVAL_INVALID, "pure function graph is missing");
    }
    if (target->call_depth_limit &&
        ctx->call_depth >= (unsigned)target->call_depth_limit) {
        return fail(ctx, n, 0, NODE_EVAL_INVALID, "pure function call depth limit exceeded");
    }
    node *entry = graph_node_by_id(target, target->entry_id);
    node *ret = graph_node_by_id(target, target->return_id);
    if (!entry || !ret) {
        return fail(ctx, n, 0, NODE_EVAL_INVALID, "pure function has no entry or return");
    }

    runtime_store *target_runtime = runtime_store_create(target);
    eval_context nested;
    eval_context_init(&nested);
    nested.instance = ctx->instance;
    nested.services = ctx->services;
    nested.delta_seconds = ctx->delta_seconds;
    nested.call_depth = ctx->call_depth + 1;
    nested.on_data_edge = ctx->on_data_edge;
    nested.on_data_edge_user = ctx->on_data_edge_user;
    nested.observer = ctx->observer;
    eval_context_attach_runtime(&nested, target_runtime);

    runtime_node *caller_state = runtime_state(ctx, n);
    runtime_node *entry_state = runtime_store_node(target_runtime, entry);
    runtime_node *return_state = runtime_store_node(target_runtime, ret);
    pin_value *caller_inputs = runtime_node_inputs(caller_state);
    pin_value *caller_outputs = runtime_node_outputs(caller_state);
    pin_value *entry_outputs = runtime_node_outputs(entry_state);
    pin_value *return_inputs = runtime_node_inputs(return_state);
    int result = 0;
    int argument = 0;
    for (int i = 0; i < entry->output_count; i++) {
        if (entry->output_pins[i].kind != PIN_DATA || entry->output_pins[i].field_source) {
            continue;
        }
        if (argument >= n->input_count) {
            result = fail(ctx, n, i, NODE_EVAL_INVALID, "pure function argument count mismatch");
            goto done;
        }
        entry_outputs[i] = caller_inputs[argument++];
        pin_coerce(&entry_outputs[i], &entry->output_pins[i]);
    }
    runtime_store_reset_memo(target_runtime);
    int output = 0;
    for (int i = 0; i < ret->input_count; i++) {
        if (ret->input_pins[i].kind != PIN_DATA) {
            continue;
        }
        node *source = NULL;
        int source_pin = -1;
        pin_value value;
        if (graph_input_source(target, ret, i, &source, &source_pin)) {
            if (ctx->on_data_edge) {
                ctx->on_data_edge(source, source_pin, ret, i, ctx->on_data_edge_user);
            }
            result = graph_eval_output(target, source, source_pin, a, &value, &nested);
            if (result) {
                ctx->status = nested.status;
                goto done;
            }
            const node_desc *conversion = graph_input_conversion(target, ret, i);
            if (conversion) {
                value = conversion->fn(&value, 1, a);
                pin_coerce(&value, &ret->input_pins[i]);
                if (value.type != ret->input_pins[i].type ||
                    (value.type == PIN_STRUCT && !value.ptr)) {
                    result = fail(&nested, ret, i, NODE_EVAL_INVALID, "edge conversion failed");
                    ctx->status = nested.status;
                    goto done;
                }
            }
            pin_coerce(&value, &ret->input_pins[i]);
        } else {
            const char *error = eval_context_resolve_input(
                &nested,
                &ret->input_pins[i],
                ret->input_pins[i].binding,
                a,
                &value
            );
            if (error) {
                result = fail(ctx, n, i, NODE_EVAL_INVALID, error);
                goto done;
            }
        }
        return_inputs[i] = value;
        while (output < n->output_count && (n->output_pins[output].kind != PIN_DATA ||
                                            n->output_pins[output].field_source)) {
            output++;
        }
        if (output >= n->output_count) {
            result = fail(ctx, n, i, NODE_EVAL_INVALID, "pure function return count mismatch");
            goto done;
        }
        caller_outputs[output++] = value;
    }

done:
    if (result && ctx->status.node != n) {
        flow_run_data_call(ctx->observer, g, n, target, ctx->status.node);
    }
    eval_context_free(&nested);
    runtime_store_free(target_runtime);
    return result;
}

static _Thread_local uint64_t oracle_calls;

uint64_t eval_oracle_calls()
{
    return oracle_calls;
}

static int eval_node(graph *g, node *n, arena *a, eval_context *ctx)
{
    oracle_calls++;
    runtime_node *state = runtime_state(ctx, n);
    if (!state) {
        return fail(ctx, n, -1, NODE_EVAL_INVALID, "node runtime state is unavailable");
    }
    pin_value *inputs = runtime_node_inputs(state);
    pin_value *outputs = runtime_node_outputs(state);
    if (runtime_node_computed(state)) {
        return 0;
    }
    if (runtime_node_visiting(state)) {
        return fail(ctx, n, -1, -2, "cycle detected");
    }

    runtime_node_set_visiting(state, 1);
    for (int i = 0; i < n->input_count; i++) {
        node *src;
        int src_pin;
        if (graph_input_source(g, n, i, &src, &src_pin)) {
            if (ctx->on_data_edge) {
                ctx->on_data_edge(src, src_pin, n, i, ctx->on_data_edge_user);
            }
            int r = graph_eval_output(g, src, src_pin, a, &inputs[i], ctx);
            if (r) {
                runtime_node_set_visiting(state, 0);
                return r;
            }
            const node_desc *conversion = graph_input_conversion(g, n, i);
            if (conversion) {
                if (!conversion->fn) {
                    runtime_node_set_visiting(state, 0);
                    return fail(ctx, n, i, NODE_EVAL_INVALID, "edge conversion has no function");
                }
                inputs[i] = conversion->fn(&inputs[i], 1, a);
                pin_coerce(&inputs[i], &n->input_pins[i]);
                if (inputs[i].type != n->input_pins[i].type ||
                    (inputs[i].type == PIN_STRUCT && !inputs[i].ptr)) {
                    runtime_node_set_visiting(state, 0);
                    return fail(ctx, n, i, NODE_EVAL_INVALID, "edge conversion failed");
                }
            }
            pin_coerce(&inputs[i], &n->input_pins[i]);
        } else {
            const char *error = eval_context_resolve_input(
                ctx,
                &n->input_pins[i],
                n->input_pins[i].binding,
                a,
                &inputs[i]
            );
            if (error) {
                runtime_node_set_visiting(state, 0);
                return fail(ctx, n, i, NODE_EVAL_INVALID, error);
            }
        }
    }
    return eval_prepared_node(n, a, ctx);
}

int eval_prepared_node(node *n, arena *a, eval_context *ctx)
{
    runtime_node *state = runtime_store_node(ctx->runtime, n);
    pin_value *inputs = runtime_node_inputs(state);
    pin_value *outputs = runtime_node_outputs(state);
    /* Reflected wrappers pass structs by value and dereference ptr. */
    if (n->desc->pure_kind == NODE_PURE_CALL) {
        for (int i = 0; i < n->input_count; i++) {
            if (n->input_pins[i].kind == PIN_DATA && n->input_pins[i].type == PIN_STRUCT &&
                n->input_pins[i].container == PIN_CONTAINER_SINGLE && !inputs[i].ptr) {
                runtime_node_set_visiting(state, 0);
                return fail(ctx, n, i, NODE_EVAL_INVALID, "data input has no struct value");
            }
        }
    }
    switch (n->desc->pure_kind) {
        case NODE_PURE_CONST: node_eval_const(n, state, a); break;
        case NODE_PURE_DELTA:
            outputs[0] = (pin_value){ .type = PIN_F32, .f = (float)ctx->delta_seconds };
            break;
        case NODE_PURE_COPY: outputs[0] = inputs[0]; break;
        case NODE_PURE_MAKE:
            node_eval_make(n, state, a);
            if (n->output_count > 0 && outputs[0].type == PIN_STRUCT &&
                !outputs[0].ptr) {
                return fail(ctx, n, 0, NODE_EVAL_ALLOCATION, "arena allocation failed");
            }
            if (n->output_count > 0 && outputs[0].type != PIN_STRUCT) {
                return fail(ctx, n, 0, NODE_EVAL_INVALID, "Make node produced invalid output");
            }
            break;
        case NODE_PURE_BREAK:
            if (!inputs[0].ptr) {
                return fail(ctx, n, 0, NODE_EVAL_INVALID, "Break node has invalid struct input");
            }
            node_eval_break(n, state, a);
            for (int i = 0; i < n->desc->type->field_count; i++) {
                if (outputs[i].type != n->output_pins[i].type) {
                    return fail(ctx, n, i, NODE_EVAL_INVALID, "Break node produced invalid output");
                }
            }
            break;
        case NODE_PURE_CALL:
            node_eval_call(n, state, a);
            if (n->output_count > 0 && outputs[0].type == PIN_FLOW) {
                return fail(ctx, n, 0, NODE_EVAL_INVALID, "data function produced an invalid result");
            }
            if (n->output_count > 0 && n->output_pins[0].type == PIN_STRUCT &&
                !outputs[0].ptr) {
                return fail(ctx, n, 0, NODE_EVAL_ALLOCATION, "arena allocation failed");
            }
            break;
        case NODE_PURE_GET:
            if (!ctx->instance) {
                return fail(ctx, n, 0, NODE_EVAL_INVALID, "Get Variable requires an instance");
            }
            if (behavior_instance_copy_variable(ctx->instance, n->reference_id, a, &outputs[0]) !=
                0) {
                return fail(ctx, n, 0, NODE_EVAL_INVALID, "Get Variable references a lost variable");
            }
            break;
        case NODE_PURE_SELF:
            if (!ctx->instance) {
                return fail(ctx, n, 0, NODE_EVAL_INVALID, "Self requires an instance");
            }
            outputs[0] = behavior_instance_owner(ctx->instance);
            break;
        case NODE_PURE_FUNCTION:
            if (ctx->program
                    ? behavior_data_call(n, a, ctx)
                    : eval_pure_function(runtime_store_graph(ctx->runtime), n, a, ctx)) {
                runtime_node_set_visiting(state, 0);
                return ctx->status.code;
            }
            break;
        case NODE_PURE_VOID_CALL: break;
        case NODE_PURE_READ: {
            const char *error = n->desc->read_inputs
                                  ? n->desc->read_inputs(ctx, inputs, a, &outputs[0])
                                  : n->desc->read(ctx, a, &outputs[0]);
            if (error) {
                return fail(ctx, n, 0, NODE_EVAL_INVALID, error);
            }
            break;
        }
    }
    runtime_node_set_computed(state, 1);
    runtime_node_set_visiting(state, 0);
    return 0;
}

static int projected_output(const node *n, int index, const pin_value *outputs, pin_value *out)
{
    const pin_spec *pin = &n->output_pins[index];
    if (!pin->field_source && !pin->field_name) {
        *out = outputs[index];
        return 0;
    }
    int source = -1;
    const type_field *field = graph_output_field(n, index, &source);
    if (!field || outputs[source].type != PIN_STRUCT ||
        outputs[source].container != PIN_CONTAINER_SINGLE || !outputs[source].ptr) {
        return -1;
    }
    *out = (pin_value){ 0 };
    pin_read_field(outputs[source].ptr, field, out);
    return 0;
}

int graph_eval_output(graph *g, node *n, int out_pin, arena *a, pin_value *out, eval_context *ctx)
{
    if (!ctx) {
        return -4;
    }
    eval_context_reset(ctx);
    if (!g || !n || !a || !out) {
        return fail(ctx, n, out_pin, -4, "invalid evaluation arguments");
    }
    if (!graph_has_node(g, n)) {
        return fail(ctx, n, out_pin, NODE_EVAL_INVALID, "node is not in graph");
    }
    if (out_pin < 0 || out_pin >= n->output_count) {
        return fail(ctx, n, out_pin, -1, "output pin index out of range");
    }
    if (n->output_pins[out_pin].kind != PIN_DATA) {
        return fail(ctx, n, out_pin, NODE_EVAL_INVALID, "EXEC output is not a data value");
    }
    if (!ctx->runtime) {
        ctx->runtime = runtime_store_create(g);
        ctx->runtime_owned = 1;
    } else if (!runtime_store_matches_graph(ctx->runtime, g)) {
        if (!ctx->runtime_owned) {
            return fail(ctx, n, out_pin, NODE_EVAL_INVALID, "evaluation context graph mismatch");
        }
        runtime_store_free(ctx->runtime);
        ctx->runtime = runtime_store_create(g);
    }
    int r = eval_node(g, n, a, ctx);
    if (r) {
        return r;
    }
    const runtime_node *state = runtime_store_node_const(ctx->runtime, n);
    if (!state) {
        return fail(ctx, n, out_pin, NODE_EVAL_INVALID, "node runtime state is unavailable");
    }
    if (projected_output(n, out_pin, runtime_node_outputs_const(state), out)) {
        return fail(ctx, n, out_pin, NODE_EVAL_INVALID, "exposed field has invalid metadata or no aggregate value");
    }
    return 0;
}

int eval_context_input(const eval_context *ctx, const node *n, int pin, pin_value *out)
{
    if (!ctx || !ctx->runtime || !n || !out || pin < 0 ||
        pin >= n->input_count || runtime_store_graph(ctx->runtime) == NULL ||
        !graph_has_node(runtime_store_graph(ctx->runtime), n)) {
        return -1;
    }
    const runtime_node *state = runtime_store_node_const(ctx->runtime, n);
    if (!state) {
        return -1;
    }
    *out = runtime_node_inputs_const(state)[pin];
    return 0;
}

int eval_context_output(const eval_context *ctx, const node *n, int pin, pin_value *out)
{
    if (!ctx || !ctx->runtime || !n || !out || pin < 0 ||
        pin >= n->output_count || runtime_store_graph(ctx->runtime) == NULL ||
        !graph_has_node(runtime_store_graph(ctx->runtime), n)) {
        return -1;
    }
    const runtime_node *state = runtime_store_node_const(ctx->runtime, n);
    if (!state) {
        return -1;
    }
    return projected_output(n, pin, runtime_node_outputs_const(state), out);
}

/* Deprecated: the graph no longer owns memo state, so only the caller's
 * eval_context can be reset. */
void graph_eval_reset(graph *g)
{
    (void)g;
}
