#include "graph/compiler/behavior_jit.h"
#include "graph/compiler/behavior_lower_internal.h"
#include "graph/model/registry.h"
#include "graph/nodes/state_nodes.h"
#include "graph/runtime/flow.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

int behavior_lower_schedule(builder *b, task t)
{
    if (!t.node) {
        return t.after;
    }
    for (int i = 0; i < acount(b->tasks); i++) {
        const task *stored = &b->tasks[i];
        if (stored->node == t.node && stored->after == t.after &&
            stored->frame == t.frame && stored->loop == t.loop &&
            stored->input == t.input && stored->continuation == t.continuation &&
            stored->from == t.from && stored->output == t.output) {
            return i;
        }
    }
    if (acount(b->tasks) >= 1024) {
        return -2;
    }
    int parent = t.after >= 0 ? b->tasks[t.after].stack
                              : b->function->frames[t.frame].stack;
    t.stack = acount(b->function->stacks);
    apush(
        b->function->stacks,
        ((behavior_function_source){ b->function->frames[t.frame].source.graph, t.node, parent })
    );
    t.pc = -1;
    apush(b->tasks, t);
    return acount(b->tasks) - 1;
}
int behavior_lower_successor(builder *b, node *n, int pin, int after)
{
    const edge *e = NULL;
    int count = graph_exec_output_edges(b->graph, n, pin, &e);
    if (!count) {
        return after;
    }
    if (count != 1) {
        return -2;
    }
    node *target = graph_node_by_id(b->graph, e->to_id);
    int output, index;
    if (!target || !graph_pin_by_id(target, e->to_pin_id, &output, &index) || output ||
        (index != 0 && !target->desc->instance_state &&
         !(index == 1 && (target->desc == &NODE_ForLoopWithBreak ||
                          target->desc == &NODE_ForEachWithBreak)))) {
        return -2;
    }
    bool immediate = n->desc == &NODE_Sequence ||
                     (n->desc->command && n->desc->command->begin);
    if (immediate) {
        behavior_lower_trace(b, n, pin, target, index, GRAPH_EDGE_EXEC);
    }
    return behavior_lower_schedule(
        b,
        (task){ .node = target,
                .after = after,
                .frame = b->frame,
                .loop = b->loop,
                .input = index,
                .from = b->recording && !immediate && n->desc != &NODE_CallFunction ? n : NULL,
                .output = pin }
    );
}
static int call(builder *b, node *n, int after)
{
    graph *target = behavior_asset_find_graph(b->asset, n->reference_id);
    if (!target || target->kind != GRAPH_FUNCTION) {
        behavior_lower_unsupported(b, n, -1, false, "function graph");
        return -2;
    }
    for (int frame = b->frame; frame >= 0;
         frame = b->function->frames[frame].source.parent) {
        if (b->function->frames[frame].source.graph == target) {
            int resume = behavior_lower_successor(b, n, 0, after);
            return resume >= -1 && behavior_lower_invoke(b, n, target) ? resume : -2;
        }
    }
    if (acount(b->function->frames) >= 128) {
        behavior_lower_unsupported(b, n, -1, false, "function expansion limit");
        return -2;
    }
    node *entry = graph_node_by_id(target, target->entry_id);
    node *ret = graph_node_by_id(target, target->return_id);
    if (!entry || !ret || entry->output_count == 0 ||
        entry->output_pins[0].kind != PIN_EXEC) {
        behavior_lower_unsupported(b, n, -1, false, "function entry or return");
        return -2;
    }
    int resume = behavior_lower_successor(b, n, 0, after);
    if (resume < -1) {
        return -2;
    }
    int parent = b->frame, frame = acount(b->function->frames);
    int source = acount(b->function->sources);
    apush(b->function->sources, ((behavior_function_source){ target, n, b->source }));
    int stack = acount(b->function->stacks);
    apush(b->function->stacks, ((behavior_function_source){ b->graph, n, b->stack }));
    apush(
        b->function->frames,
        ((behavior_function_frame){ .source = { target, n, parent },
                                    .depth = b->function->frames[parent].depth + 1,
                                    .resume = resume,
                                    .source_index = source,
                                    .stack = stack })
    );
    if (target->call_depth_limit) {
        behavior_lower_emit(
            b,
            (behavior_function_instruction){ .op = BF_CALL_CHECK,
                                             .left = target->call_depth_limit,
                                             .node_id = n->id }
        );
    }
    int *args = NULL, *parameters = NULL;
    int at = 1;
    for (int i = 1; i < entry->output_count; i++) {
        if (entry->output_pins[i].kind != PIN_DATA || entry->output_pins[i].field_source) {
            continue;
        }
        while (at < n->input_count && n->input_pins[at].kind != PIN_DATA) {
            at++;
        }
        int arg = at < n->input_count ? behavior_lower_input(b, n, at++) : -1;
        int parameter = behavior_lower_local(b, frame, entry, i);
        if (arg >= 0) {
            arg = behavior_lower_convert(b, arg, entry->output_pins[i].type);
        }
        if (arg < 0 || parameter < 0) {
            afree(args);
            afree(parameters);
            return -2;
        }
        apush(args, arg);
        apush(parameters, parameter);
    }
    behavior_lower_watch_reset(b, frame);
    b->pending_watch_frame = frame;
    behavior_lower_capture(b, n, false);
    b->pending_watch_frame = 0;
    behavior_lower_emit(b, (behavior_function_instruction){ .op = BF_ENTER, .target = frame });
    /* Parameter copies execute inside the callee, including their yields. */
    b->frame = frame;
    for (int i = 0; i < acount(args); i++) {
        if (!behavior_lower_store(b, BF_SAVE, args[i], parameters[i])) {
            afree(args);
            afree(parameters);
            return -2;
        }
    }
    afree(args);
    afree(parameters);
    int end = acount(b->tasks);
    apush(b->tasks, ((task){ .pc = -1, .after = -1, .frame = frame, .stack = stack }));
    int next = behavior_lower_schedule(
        b,
        (task){ .node = entry, .after = end, .frame = frame, .loop = -1 }
    );
    b->frame = parent;
    return next;
}
static bool return_values(builder *b, node *n)
{
    behavior_function_frame frame = b->function->frames[b->frame];
    node *call = frame.source.call;
    int at = 0;
    for (int i = 0; i < n->input_count; i++) {
        if (n->input_pins[i].kind != PIN_DATA) {
            continue;
        }
        int slot = behavior_lower_input(b, n, i), target = i;
        if (call) {
            while (at < call->output_count && (call->output_pins[at].kind != PIN_DATA ||
                                               call->output_pins[at].field_source)) {
                at++;
            }
            if (at == call->output_count) {
                return false;
            }
            slot = behavior_lower_convert(b, slot, call->output_pins[at].type);
            target = behavior_lower_local(b, frame.source.parent, call, at++);
        }
        if (slot < 0 || target < 0) {
            return false;
        }
        if (!behavior_lower_store(b, call ? BF_SAVE : BF_RESULT, slot, target)) {
            behavior_lower_unsupported(b, n, i, false, "return value ownership");
            return false;
        }
    }
    behavior_lower_capture(b, n, false);
    if (call) {
        behavior_lower_emit(b, (behavior_function_instruction){ .op = BF_LEAVE, .target = b->frame });
        int parent = b->frame;
        graph *previous = b->graph;
        int source = b->source;
        b->frame = frame.source.parent;
        behavior_lower_watch_commit(b, parent);
        b->source = b->function->frames[b->frame].source_index;
        b->graph = b->function->frames[b->frame].source.graph;
        behavior_lower_capture(b, call, true);
        const edge *edges;
        if (graph_exec_output_edges(b->graph, call, 0, &edges)) {
            node *to = graph_node_by_id(b->graph, edges[0].to_id);
            int output, input;
            bool found = graph_pin_by_id(to, edges[0].to_pin_id, &output, &input);
            assert(found && !output);
            behavior_lower_trace(b, call, 0, to, input, GRAPH_EDGE_EXEC);
        }
        behavior_lower_emit(
            b,
            (behavior_function_instruction){ .op = BF_JUMP, .target = frame.resume }
        );
        b->frame = parent;
        b->source = source;
        b->graph = previous;
        return true;
    }
    behavior_lower_emit(b, (behavior_function_instruction){ .op = BF_RETURN, .node_id = n->id });
    return true;
}
static bool block(builder *b, int index)
{
    task t = b->tasks[index];
    node *n = t.node;
    b->frame = t.frame;
    b->loop = t.loop;
    b->stack = t.stack;
    behavior_function_frame frame = b->function->frames[b->frame];
    b->graph = frame.source.graph;
    b->source = frame.source_index;
    b->entry = b->frame ? graph_node_by_id(b->graph, b->graph->entry_id) : b->root_entry;
    b->tasks[index].pc = acount(b->function->instructions);
    aclear(b->outputs);
    aclear(b->inputs);
    aclear(b->traced);
    if (!n) {
        behavior_lower_emit(b, (behavior_function_instruction){ .op = BF_LEAVE, .target = b->frame });
        b->frame = frame.source.parent;
        b->source = b->function->frames[b->frame].source_index;
        behavior_lower_emit(
            b,
            (behavior_function_instruction){ .op = BF_FAIL,
                                             .node_id = frame.source.call->id,
                                             .left = BEHAVIOR_FUNCTION_NO_RETURN }
        );
        return true;
    }
    if (t.from) {
        behavior_lower_trace(b, t.from, t.output, n, t.input, GRAPH_EDGE_EXEC);
    }
    if (n->disabled) {
        behavior_lower_unsupported(b, n, -1, false, "disabled node or breakpoint");
        return false;
    }
    behavior_lower_emit(b, (behavior_function_instruction){ .op = BF_CHECK, .node_id = n->id });
    b->stack = b->function->stacks[t.stack].parent;
    int next = -2;
    if (n->desc == &NODE_Event || n->desc == &NODE_FunctionEntry) {
        next = behavior_lower_successor(b, n, 0, t.after);
    } else if (n->desc == &NODE_Return && n->id == b->graph->return_id) {
        return return_values(b, n);
    } else if (n->desc == &NODE_CallFunction) {
        next = call(b, n, t.after);
        if (next < -1) {
            return false;
        }
    } else if (n->desc == &NODE_Delay || n->desc == &NODE_RetriggerableDelay) {
        next = behavior_lower_delay(b, t);
    } else if (n->desc == &NODE_SetVariable) {
        int source = behavior_lower_input(b, n, 1), target = behavior_lower_variable(
                                                        b,
                                                        n->reference_id,
                                                        n->input_pins[1].type,
                                                        n->input_pins[1].container
                                                    );
        if (source < 0 || target < 0) {
            return false;
        }
        int stored = behavior_lower_local(b, b->frame, n, 1);
        if (!behavior_lower_store(b, BF_SAVE, source, stored)) {
            return false;
        }
        aggregate *a = behavior_lower_aggregate(b, source);
        if (behavior_lower_array(b, source) || (a && !type_is_plain(a->type)) ||
            behavior_scalar_string(b->function->slots[source])) {
            /* Retain a self-assignment before the variable store releases its
             * previous payload. Watches read the same retained input. */
            source = behavior_lower_source(b, BF_LOCAL, stored, &n->output_pins[1], n, 1);
            if (b->recording) {
                apush(b->inputs, ((output){ n, 1, source }));
            }
        }
        if (!behavior_lower_store(b, BF_STORE, source, target)) {
            behavior_lower_unsupported(b, n, 1, false, "persistent aggregate write");
            return false;
        }
        next = behavior_lower_successor(b, n, 0, t.after);
    } else if (n->desc->lower) {
        b->function->custom_lowering = true;
        if (n->desc->lower(b, n, -1) < 0) {
            return false;
        }
        next = behavior_lower_successor(b, n, 0, t.after);
    } else if (n->desc->host_call) {
        if (behavior_lower_host_call(b, n, -1) < 0) {
            return false;
        }
        next = behavior_lower_successor(b, n, 0, t.after);
    } else if (n->desc->command) {
        next = behavior_lower_command(b, n, t.after);
    } else if (n->desc->component_kind) {
        if (!behavior_lower_component_write(b, n)) {
            return false;
        }
        next = behavior_lower_successor(b, n, 0, t.after);
    } else if (
        n->desc == &NODE_WhileLoop || n->desc == &NODE_ForLoop ||
        n->desc == &NODE_ForLoopWithBreak || n->desc == &NODE_ForEach ||
        n->desc == &NODE_ForEachWithBreak || n->desc == &NODE_Break
    ) {
        next = behavior_lower_loop(b, t);
    } else if (behavior_lower_is_switch(n->desc)) {
        next = behavior_lower_switch(b, t);
    } else if (behavior_lower_is_state(n->desc)) {
        next = behavior_lower_state(b, t);
    } else if (n->desc == &NODE_Branch) {
        int condition = behavior_lower_input(b, n, 1);
        if (condition < 0 || b->function->slots[condition] != PIN_BOOL) {
            return false;
        }
        behavior_lower_capture(b, n, false);
        int yes = behavior_lower_successor(b, n, 0, t.after);
        int no = behavior_lower_successor(b, n, 1, t.after);
        if (yes < -1 || no < -1) {
            return false;
        }
        behavior_lower_emit(
            b,
            (behavior_function_instruction){ .op = BF_BRANCH, .left = condition, .target = yes }
        );
        next = no;
    } else if (n->desc == &NODE_Sequence) {
        next = t.after;
        for (int i = n->output_count - 1; i >= 0; i--) {
            next = behavior_lower_successor(b, n, i, next);
            if (next < -1) {
                return false;
            }
        }
    } else {
        behavior_lower_unsupported(b, n, -1, false, "control operation");
        return false;
    }
    if (next < -1) {
        return false;
    }
    if (n->desc != &NODE_CallFunction && n->desc != &NODE_Delay &&
        !behavior_lower_is_state(n->desc) &&
        !(n->desc->command && n->desc->command->begin)) {
        behavior_lower_capture(b, n, t.continuation);
    }
    behavior_lower_emit(b, (behavior_function_instruction){ .op = BF_JUMP, .target = next });
    return true;
}
/* Stored node outputs are observable only when another compiled expression
 * reads them. Remove unused storage before assigning native stack offsets. */
static void compact_locals(behavior_function *f)
{
    int *map = xcalloc((size_t)(acount(f->locals) + 1), sizeof *map);
    for (int i = 0; behavior_function_inspectable(f) && i < acount(f->locals); i++) {
        map[i] = f->locals[i].parameter;
    }
    for (int i = 0; i < acount(f->instructions); i++) {
        if (f->instructions[i].op == BF_LOCAL) {
            map[f->instructions[i].left] = 1;
        }
    }
    int count = 0;
    for (int i = 0; i < acount(f->locals); i++) {
        if (map[i]) {
            f->locals[count] = f->locals[i];
            map[i] = count++;
        } else {
            map[i] = -1;
        }
    }
    asetlen(f->locals, count);
    int *pcs = xcalloc((size_t)acount(f->instructions), sizeof *pcs);
    behavior_function_instruction *instructions = NULL;
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        behavior_function_instruction i = f->instructions[pc];
        pcs[pc] = acount(instructions);
        if (i.op == BF_SAVE) {
            i.target = map[i.target];
            if (i.target < 0) {
                continue;
            }
        }
        if (i.op == BF_LOCAL) {
            i.left = map[i.left];
        }
        apush(instructions, i);
    }
    for (int i = 0; i < acount(instructions); i++) {
        if (instructions[i].op == BF_JUMP || instructions[i].op == BF_BRANCH) {
            instructions[i].target = pcs[instructions[i].target];
        }
    }
    afree(f->instructions);
    f->instructions = instructions;
    free(pcs);
    free(map);
}
static bool batch_operation(behavior_function_op op)
{
    if (behavior_function_math(op) || behavior_function_observer(op) || op == BF_POINT) {
        return true;
    }
    switch (op) {
        case BF_CONSTANT:
        case BF_VARIABLE:
        case BF_DELTA:
        case BF_ARGUMENT:
        case BF_LOCAL:
        case BF_PRESENT:
        case BF_TYPE_NAME:
        case BF_OWNER:
        case BF_WORLD:
        case BF_CONTEXT:
        case BF_STORAGE_NAME:
        case BF_ARRAY:
        case BF_RANDOM:
        case BF_STATE_SELECT:
        case BF_STRING_EQUAL:
        case BF_RANDOM_RANGE:
        case BF_HOST_SLOT:
        case BF_HOST_SERVICE:
        case BF_HOST_CALL:
        case BF_COPY_ARRAY:
        case BF_COPY_STRING:
        case BF_INTERN_STRING:
        case BF_STORE_VALUE:
        case BF_OWN_NAME:
        case BF_ADD:
        case BF_SUB:
        case BF_MUL:
        case BF_DIV:
        case BF_MOD:
        case BF_NEG:
        case BF_ABS:
        case BF_MIN:
        case BF_MAX:
        case BF_CONVERT:
        case BF_CAST:
        case BF_FLAGS:
        case BF_LT:
        case BF_LE:
        case BF_GT:
        case BF_GE:
        case BF_EQ:
        case BF_NE:
        case BF_SELECT:
        case BF_ENTITY:
        case BF_COMPONENT:
        case BF_COLUMN:
        case BF_COLUMN_TRY:
        case BF_ROW:
        case BF_INDEX:
        case BF_LOAD:
        case BF_READ:
        case BF_STORE:
        case BF_SAVE:
        case BF_STORE_HEADER:
        case BF_STORE_TYPE_NAME:
        case BF_REQUIRE:
        case BF_ASSERT:
        case BF_WRITE:
        case BF_CHECK:
        case BF_CALL_CHECK:
        case BF_ENTER:
        case BF_LEAVE:
        case BF_FAIL:
        case BF_JUMP:
        case BF_BRANCH:
        case BF_RETURN:
        case BF_END: return true;
        default: return false;
    }
}
static bool batch_event(const behavior_function *f, const graph *g, const node *entry)
{
    if (entry->desc->flow_kind != NODE_FLOW_EVENT || entry->output_count != 1 ||
        g->return_id || f->waits || f->breakpoints) {
        return false;
    }
    for (int i = 0; i < acount(f->instructions); i++) {
        if (!batch_operation(f->instructions[i].op)) {
            return false;
        }
    }
    return true;
}

static bool preserves_components_uncached(const behavior_function *f, const uint64_t *components, int count)
{
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < acount(f->component_writes); j++) {
            if (f->component_writes[j] == components[i]) {
                return false;
            }
        }
    }
    for (int i = 0; i < acount(f->host_calls); i++) {
        const node_access *a = f->host_calls[i].access;
        if (!a || (a->flags & NODE_EFFECT_STRUCTURAL)) {
            return false;
        }
        for (int j = 0; j < a->write_count; j++) {
            for (int k = 0; k < count; k++) {
                if (a->writes[j] == components[k]) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool behavior_function_preserves_components(const behavior_function *f, const uint64_t *components, int count)
{
    if (!f || !f->batchable) {
        return false;
    }
    for (int i = 0; i < acount(f->component_proofs); i++) {
        const behavior_component_proof *proof = &f->component_proofs[i];
        if (acount(proof->components) != count) {
            continue;
        }
        bool same = true;
        for (int j = 0; j < count; j++) {
            if (proof->components[j] != components[j]) {
                same = false;
                break;
            }
        }
        if (same) {
            return proof->result;
        }
    }
    /* Published functions are immutable. Keep the proof cache mutable so
     * repeated query-scope checks do not rescan a function generation. */
    behavior_component_proof proof = {
        .result = preserves_components_uncached(f, components, count)
    };
    for (int i = 0; i < count; i++) {
        apush(proof.components, components[i]);
    }
    apush(((behavior_function *)f)->component_proofs, proof);
    return proof.result;
}
bool behavior_function_batchable(const behavior_function *f)
{
    return f && f->batchable;
}
bool behavior_function_uses_storage(const behavior_function *f)
{
    return f->uses_storage;
}
static bool managed_pin(const pin_spec *spec)
{
    if (spec->container == PIN_CONTAINER_ARRAY || spec->container == PIN_CONTAINER_SET) {
        return true;
    }
    if (spec->container != PIN_CONTAINER_SINGLE) {
        return false;
    }
    if (behavior_scalar_string(spec->type)) {
        return true;
    }
    const type_desc *type = spec->type == PIN_STRUCT && spec->type_name
                              ? registry_type_by_name(spec->type_name)
                              : NULL;
    return type && !type_is_plain(type);
}
static bool lower_definition(
    const behavior_asset *asset,
    graph *g,
    node *entry,
    behavior_function *function,
    lowering_unit *unit,
    behavior_function_diagnostic *diagnostic
)
{
    builder b = { .diagnostic = diagnostic,
                  .unit = unit,
                  .asset = asset,
                  .graph = g,
                  .entry = entry,
                  .root_entry = entry,
                  .loop = -1,
                  .stack = -1,
                  .recording = unit->recursive,
                  .function = function };
    apush(b.function->sources, ((behavior_function_source){ g, NULL, -1 }));
    apush(b.function->frames, ((behavior_function_frame){ .source = { g, NULL, -1 }, .stack = -1 }));
    for (int j = 0; j < acount(asset->graphs); j++) {
        for (int k = 0; k < acount(asset->graphs[j]->nodes); k++) {
            const node *n = asset->graphs[j]->nodes[k];
            const node_desc *desc = n->desc;
            for (int p = 0; p < n->input_count; p++) {
                b.recording |= managed_pin(&n->input_pins[p]);
            }
            for (int p = 0; p < n->output_count; p++) {
                b.recording |= managed_pin(&n->output_pins[p]);
            }
            b.recording |= desc == &NODE_Delay || desc == &NODE_RetriggerableDelay ||
                           behavior_lower_is_switch(desc) ||
                           behavior_lower_is_state(desc) || desc->host_call ||
                           desc->lower || (desc->command && desc->command->begin);
        }
    }
    bool valid = true;
    bool pure = g->kind == GRAPH_FUNCTION &&
                (!entry->output_count || entry->output_pins[0].kind != PIN_EXEC);
    if (pure) {
        node *ret = graph_node_by_id(g, g->return_id);
        valid = ret && return_values(&b, ret);
    } else {
        behavior_lower_schedule(&b, (task){ .node = entry, .after = -1, .loop = -1 });
        for (int i = 0; valid && i < acount(b.tasks); i++) {
            valid = block(&b, i);
        }
    }
    int end = behavior_lower_emit(&b, (behavior_function_instruction){ .op = BF_END });
    for (int i = 0; valid && i < acount(b.function->instructions); i++) {
        behavior_function_instruction *op = &b.function->instructions[i];
        if (op->op == BF_JUMP || op->op == BF_BRANCH) {
            if (op->target < -1) {
                int index = -op->target - 2;
                assert(index < acount(b.labels) && b.labels[index] >= 0);
                op->target = b.labels[index];
            } else {
                op->target = op->target == -1 ? end : b.tasks[op->target].pc;
            }
        }
    }
    afree(b.tasks);
    afree(b.labels);
    afree(b.loops);
    afree(b.outputs);
    afree(b.inputs);
    afree(b.traced);
    afree(b.evaluating);
    behavior_lower_finish_values(&b);
    if (valid && !behavior_function_inspectable(b.function)) {
        for (int i = 0; i < acount(b.function->instructions); i++) {
            const behavior_function_instruction *op = &b.function->instructions[i];
            if (op->op != BF_CHECK) {
                continue;
            }
            graph *source = b.function->frames[op->frame].source.graph;
            node *n = graph_node_by_id(source, op->node_id);
            if (!n->breakpoint) {
                continue;
            }
            *diagnostic = (behavior_function_diagnostic){
                .graph = source,
                .node = n,
                .pin = -1,
                .reason = "breakpoint requires a resumable activation"
            };
            valid = false;
            break;
        }
    }
    if (valid) {
        behavior_function_fuse_queries(b.function);
        if (!behavior_function_inspectable(b.function)) {
            behavior_function_remove_observers(b.function);
        }
        behavior_function_points(b.function);
        behavior_function_constant_locals(b.function);
        compact_locals(b.function);
        behavior_function_reuse_accesses(b.function);
        compact_locals(b.function);
        behavior_function_prune_values(b.function);
        behavior_function_optimize(b.function);
        behavior_function_finalize(b.function);
        b.function->batchable = batch_event(b.function, g, entry);
        behavior_function_parallel_proof(b.function);
        return true;
    }
    behavior_lower_unsupported(&b, entry, -1, false, "control shape, breakpoint or compilation limit");
    return false;
}

/* Detect a reachable call cycle before emitting watches. The call graph is
 * publication data; no runtime graph traversal is needed by outlined calls. */
static bool recursive(const behavior_asset *asset, graph *g, graph ***path, graph ***done)
{
    for (int i = 0; i < acount(*path); i++) {
        if ((*path)[i] == g) {
            return true;
        }
    }
    for (int i = 0; i < acount(*done); i++) {
        if ((*done)[i] == g) {
            return false;
        }
    }
    apush(*path, g);
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        if (n->desc != &NODE_CallFunction && n->desc != &NODE_CallPureFunction) {
            continue;
        }
        graph *target = behavior_asset_find_graph(asset, n->reference_id);
        if (target && recursive(asset, target, path, done)) {
            return true;
        }
    }
    asetlen(*path, acount(*path) - 1);
    apush(*done, g);
    return false;
}

behavior_function *behavior_function_lower(
    const behavior_asset *asset,
    graph *g,
    node *entry,
    behavior_function_diagnostic *diagnostic
)
{
    *diagnostic = (behavior_function_diagnostic){ 0 };
    graph **path = NULL, **done = NULL;
    lowering_unit unit = { .recursive = recursive(asset, g, &path, &done) };
    afree(path);
    afree(done);
    behavior_function *root = xcalloc(1, sizeof *root);
    apush(unit.definitions, ((lowered_definition){ g, entry, root }));
    bool valid = true;
    for (int i = 0; valid && i < acount(unit.definitions); i++) {
        lowered_definition d = unit.definitions[i];
        valid = lower_definition(asset, d.graph, d.entry, d.function, &unit, diagnostic);
    }
    for (int i = 1; i < acount(unit.definitions); i++) {
        apush(root->definitions, unit.definitions[i].function);
    }
    afree(unit.definitions);
    if (!valid) {
        behavior_function_free(root);
        return NULL;
    }
    return root;
}

bool behavior_function_has_invocations(const behavior_function *function)
{
    return function->calls != NULL;
}
void behavior_function_free(behavior_function *f)
{
    if (!f) {
        return;
    }
    for (int i = 0; i < acount(f->definitions); i++) {
        behavior_function_free(f->definitions[i]);
    }
    afree(f->definitions);
    afree(f->reductions);
    afree(f->query_diagnostics);
    behavior_jit_free(f->code);
    behavior_jit_free(f->system_code);
    behavior_jit_free(f->dispatch);
    free(f->resume_addresses);
    behavior_jit_free(f->inspection_code);
    free(f->inspection_addresses);
    afree(f->points);
    afree(f->watches);
    afree(f->edges);
    behavior_function_free(f->fast);
    for (int i = 0; i < acount(f->suspensions); i++) {
        afree(f->suspensions[i].values);
    }
    afree(f->suspensions);
    afree(f->instructions);
    afree(f->runtime_instructions);
    free(f->runtime_map);
    afree(f->slots);
    afree(f->locals);
    afree(f->frames);
    afree(f->sources);
    afree(f->stacks);
    afree(f->results);
    afree(f->queries);
    afree(f->host_calls);
    afree(f->state_selections);
    afree(f->calls);
    afree(f->component_writes);
    for (int i = 0; i < acount(f->component_proofs); i++) {
        afree(f->component_proofs[i].components);
    }
    afree(f->component_proofs);
    afree(f->parallel_reads);
    afree(f->components);
    afree(f->data_values);
    afree(f->commands);
    afree(f->command_arguments);
    afree(f->command_results);
    free(f);
}
bool behavior_function_compile_system(behavior_function *f)
{
    assert(f && f->batchable && f->code && !f->system_code);
    f->system_code = behavior_function_jit_system(f);
    return f->system_code != NULL;
}
void *behavior_function_system_entry(const behavior_function *f)
{
    return f->system_code;
}
bool behavior_function_compile(behavior_function *f)
{
    for (int i = 0; i < acount(f->definitions); i++) {
        if (!behavior_function_compile(f->definitions[i])) {
            return false;
        }
    }
    if (acount(f->suspensions)) {
        assert(!f->resume_addresses);
        f->resume_addresses = xcalloc((size_t)acount(f->instructions), sizeof *f->resume_addresses);
    }
    f->code = behavior_function_jit(f, false);
    if (f->code && acount(f->points)) {
        f->inspection_addresses = xcalloc(
            (size_t)acount(f->instructions),
            sizeof *f->inspection_addresses
        );
        f->inspection_code = behavior_function_jit(f, true);
        if (!f->inspection_code) {
            return false;
        }
    }
    if (!f->code || !f->fast) {
        return f->code != NULL;
    }
    if (!behavior_function_compile(f->fast)) {
        return false;
    }
    f->dispatch = behavior_function_guard(f);
    return f->dispatch != NULL;
}
void *behavior_function_entry(const behavior_function *f)
{
    return f ? (f->dispatch ? f->dispatch : f->code) : NULL;
}
bool behavior_function_pure_depth(const behavior_function *f, unsigned depth)
{
    return !f->pure_depth_limit || depth < f->pure_depth_limit;
}
const behavior_function_source *behavior_function_source_at(const behavior_function *f, int source)
{
    return &f->sources[source];
}

static const char *copy_string(void *storage, const char *text)
{
    if (!text) {
        return NULL;
    }
    size_t size = strlen(text) + 1;
    char *copy = arena_alloc(storage, size);
    memcpy(copy, text, size);
    return copy;
}
static pin_array *copy_array(void *storage, const pin_array *array)
{
    pin_array *copy = pin_array_snapshot_to_arena(storage, array);
    assert(!array || copy);
    return copy;
}
void behavior_function_bind_storage(behavior_function_context *ctx, arena *storage)
{
    ctx->value_storage = storage;
    ctx->copy_string = copy_string;
    ctx->copy_array = copy_array;
}

void behavior_function_prepare_results(const behavior_function *f, pin_value *results, arena *storage)
{
    for (int i = 0; i < acount(f->results); i++) {
        behavior_function_result r = f->results[i];
        void *data = arena_alloc(storage, r.type->size);
        memset(data, 0, r.type->size);
        results[r.index] = (pin_value){ .type = PIN_STRUCT,
                                        .type_name = r.type->name,
                                        .ptr = data };
    }
}

const behavior_function_source *behavior_function_stack_at(const behavior_function *f, int stack)
{
    return &f->stacks[stack];
}
