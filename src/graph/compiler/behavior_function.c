#include "graph/compiler/behavior_jit.h"
#include "graph/compiler/behavior_lower_internal.h"
#include "graph/model/registry.h"
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
            stored->input == t.input && stored->continuation == t.continuation) {
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
        (index != 0 && !(index == 1 && (target->desc == &NODE_ForLoopWithBreak ||
                                        target->desc == &NODE_ForEachWithBreak)))) {
        return -2;
    }
    return behavior_lower_schedule(
        b,
        (task){ .node = target, .after = after, .frame = b->frame, .loop = b->loop, .input = index }
    );
}
static int call(builder *b, node *n, int after)
{
    graph *target = behavior_asset_find_graph(b->asset, n->reference_id);
    if (!target || target->kind != GRAPH_FUNCTION) {
        return behavior_lower_unsupported(b, n, -1, false, "function graph");
    }
    for (int frame = b->frame; frame >= 0;
         frame = b->function->frames[frame].source.parent) {
        if (b->function->frames[frame].source.graph == target) {
            return behavior_lower_unsupported(b, n, -1, false, "recursive function call");
        }
    }
    if (acount(b->function->frames) >= 128) {
        return behavior_lower_unsupported(b, n, -1, false, "function expansion limit");
    }
    node *entry = graph_node_by_id(target, target->entry_id);
    node *ret = graph_node_by_id(target, target->return_id);
    if (!entry || !ret || entry->output_count == 0 ||
        entry->output_pins[0].kind != PIN_EXEC) {
        return behavior_lower_unsupported(b, n, -1, false, "function entry or return");
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
    behavior_lower_emit(b, (behavior_function_instruction){ .op = BF_ENTER, .target = frame });
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
    b->frame = frame;
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
            return false;
        }
    }
    if (call) {
        behavior_lower_emit(b, (behavior_function_instruction){ .op = BF_LEAVE, .target = b->frame });
        behavior_lower_emit(
            b,
            (behavior_function_instruction){ .op = BF_JUMP, .target = frame.resume }
        );
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
    if (n->disabled || n->breakpoint) {
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
        if (next < 0) {
            return false;
        }
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
        if (!behavior_lower_store(b, BF_STORE, source, target) ||
            !behavior_lower_store(b, BF_SAVE, source, stored)) {
            behavior_lower_unsupported(b, n, 1, false, "persistent aggregate write");
            return false;
        }
        next = behavior_lower_successor(b, n, 0, t.after);
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
    } else if (n->desc == &NODE_Branch) {
        int condition = behavior_lower_input(b, n, 1);
        if (condition < 0 || b->function->slots[condition] != PIN_BOOL) {
            return false;
        }
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
    behavior_lower_emit(b, (behavior_function_instruction){ .op = BF_JUMP, .target = next });
    return true;
}
/* Stored node outputs are observable only when another compiled expression
 * reads them. Remove unused storage before assigning native stack offsets. */
static void compact_locals(behavior_function *f)
{
    int *map = xcalloc((size_t)(acount(f->locals) + 1), sizeof *map);
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
behavior_function *behavior_function_lower(
    const behavior_asset *asset,
    graph *g,
    node *entry,
    behavior_function_diagnostic *diagnostic
)
{
    *diagnostic = (behavior_function_diagnostic){ 0 };
    builder b = { .diagnostic = diagnostic,
                  .asset = asset,
                  .graph = g,
                  .entry = entry,
                  .root_entry = entry,
                  .loop = -1,
                  .stack = -1,
                  .function = xcalloc(1, sizeof(behavior_function)) };
    apush(b.function->sources, ((behavior_function_source){ g, NULL, -1 }));
    apush(b.function->frames, ((behavior_function_frame){ .source = { g, NULL, -1 }, .stack = -1 }));
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
            op->target = op->target < 0 ? end : b.tasks[op->target].pc;
        }
    }
    afree(b.tasks);
    afree(b.loops);
    afree(b.outputs);
    afree(b.evaluating);
    behavior_lower_finish_values(&b);
    if (valid) {
        behavior_function_constant_locals(b.function);
        compact_locals(b.function);
        behavior_function_reuse_accesses(b.function);
        compact_locals(b.function);
        behavior_function_prune_values(b.function);
        behavior_function_optimize(b.function);
        return b.function;
    }
    behavior_lower_unsupported(&b, entry, -1, false, "control shape, breakpoint or compilation limit");
    behavior_function_free(b.function);
    return NULL;
}
void behavior_function_free(behavior_function *f)
{
    if (!f) {
        return;
    }
    behavior_jit_free(f->code);
    behavior_jit_free(f->dispatch);
    behavior_function_free(f->fast);
    afree(f->instructions);
    afree(f->slots);
    afree(f->locals);
    afree(f->frames);
    afree(f->sources);
    afree(f->stacks);
    afree(f->results);
    free(f);
}
bool behavior_function_compile(behavior_function *f)
{
    f->code = behavior_function_jit(f);
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
