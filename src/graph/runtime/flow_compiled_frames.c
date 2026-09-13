#include "graph/runtime/flow_compiled_internal.h"
#include "support/xalloc.h"

#include <assert.h>

void flow_compiled_add_frames(flow_compiled *c, flow_compiled_invocation *invocation)
{
    invocation->base = acount(c->frames);
    int count = behavior_function_frame_count(invocation->function);
    for (int i = 0; i < count; i++) {
        int parent = behavior_function_frame_parent(invocation->function, i);
        apush(
            c->frames,
            ((flow_compiled_frame){ invocation,
                                    i,
                                    parent < 0 ? invocation->parent_frame
                                               : invocation->base + parent })
        );
        apush(c->stepping, false);
        apush(c->watch_frames, NULL);
        apush(c->pending_watches, NULL);
        apush(c->outlined_watches, NULL);
        apush(c->watch_order, 0);
    }
}

bool flow_compiled_descendant(const flow_compiled *c, int frame, int ancestor)
{
    for (; frame >= 0; frame = c->frames[frame].parent) {
        if (frame == ancestor) {
            return true;
        }
    }
    return false;
}

const behavior_function_source *flow_compiled_frame_source(const flow_compiled *c, int frame)
{
    const flow_compiled_frame *view = &c->frames[frame];
    return behavior_function_frame_source(view->invocation->function, view->local);
}

flow_compiled_invocation *flow_compiled_active(const flow_compiled *c)
{
    const behavior_function *f = c->function;
    behavior_function_context *context = behavior_function_active_context(
        (behavior_function_context *)&c->context,
        &f
    );
    return context->inspection_user;
}

int flow_compiled_active_frame(const flow_run *run)
{
    flow_compiled *c = run->compiled;
    if (c->finished) {
        return 0;
    }
    if (run->state == FLOW_FAILED && c->failed_return) {
        return c->failed_return->base;
    }
    const flow_compiled_invocation *invocation = flow_compiled_active(c);
    const behavior_function_context *context = invocation->context;
    if (run->state == FLOW_FAILED && context->failure == BEHAVIOR_FUNCTION_NO_RETURN) {
        for (int frame = 1;
             frame < behavior_function_frame_count(invocation->function);
             frame++) {
            if (behavior_function_frame_stack(invocation->function, frame) ==
                context->error_stack) {
                return invocation->base + frame;
            }
        }
    }
    return invocation->base + (run->state == FLOW_FAILED ? context->error_frame
                                                         : context->activation->frame);
}

static bool observed(const flow_compiled *c, int frame)
{
    for (int i = 0; i < acount(c->root.run->host_requests); i++) {
        if (flow_compiled_descendant(c, c->root.run->host_requests[i]->compiled_frame, frame)) {
            return true;
        }
    }
    return false;
}

void flow_compiled_call_enter(const behavior_function *f, behavior_function_context *child)
{
    flow_compiled_invocation *parent = child->parent->inspection_user;
    flow_run *run = parent->run;
    flow_compiled *c = run->compiled;
    int parent_frame = parent->base + child->parent->activation->frame;
    const graph *g = flow_compiled_frame_source(c, parent_frame)->graph;
    const node *call = graph_node_by_id(g, child->parent->activation->node_id);
    assert(call);
    flow_compiled_invocation *invocation = NULL;
    for (int i = 0; i < acount(c->invocations); i++) {
        flow_compiled_invocation *candidate = c->invocations[i];
        if (candidate->completed && candidate->parent_frame == parent_frame &&
            candidate->call == call && !observed(c, candidate->base)) {
            invocation = candidate;
            for (int frame = invocation->base; frame < acount(c->frames); frame++) {
                if (flow_compiled_descendant(c, frame, invocation->base)) {
                    flow_compiled_clear_frame(c, frame);
                    c->stepping[frame] = false;
                }
            }
            break;
        }
    }
    if (!invocation) {
        invocation = xcalloc(1, sizeof *invocation);
        *invocation = (flow_compiled_invocation){ .run = run,
                                                  .function = f,
                                                  .parent = parent,
                                                  .parent_frame = parent_frame,
                                                  .call = call };
        apush(c->invocations, invocation);
        flow_compiled_add_frames(c, invocation);
    }
    assert(invocation->function == f);
    invocation->completed = false;
    invocation->context = child;
    child->inspection_user = invocation;
    int frame = invocation->base;
    flow_run *pending = c->pending_watches[frame];
    c->pending_watches[frame] = c->outlined_watches[parent_frame];
    c->outlined_watches[parent_frame] = pending;
    c->stepping[frame] = c->stepping[parent_frame] && !run->step_over;
    c->current_frame = frame;
    c->step_nodes = 0;
    if (c->context.watch) {
        c->watch_order[frame] = ++c->watch_serial;
    }
    c->revision++;
}

void flow_compiled_call_leave(behavior_function_context *child, int result)
{
    flow_compiled_invocation *invocation = child->inspection_user;
    flow_run *run = invocation->run;
    flow_compiled *c = run->compiled;
    if (!c->stopping) {
        c->revision++;
        if (!result) {
            flow_compiled_commit_watches(run, invocation->base);
        }
        if (result && !child->returned_node_id &&
            child->parent->failure == BEHAVIOR_FUNCTION_NO_RETURN) {
            flow_compiled_frame_view(run, invocation->base);
            c->failed_return = invocation;
        }
        for (int i = 0; i < behavior_function_frame_count(invocation->function); i++) {
            flow_run *view = c->watch_frames[invocation->base + i];
            if (!view) {
                continue;
            }
            flow_compiled_sync_view(view);
            view->state = FLOW_COMPLETED;
            view->steps = i ? behavior_function_frame_steps(
                                  invocation->function,
                                  child->activation,
                                  i
                              )
                            : child->steps;
            view->waiting_node = NULL;
            view->host_request = NULL;
            view->error = (flow_error){ 0 };
        }
    }
    invocation->completed = true;
    invocation->context = NULL;
}

void flow_compiled_build_stack(flow_run *run)
{
    const flow_run *root = run->compiled_owner ? run->compiled_owner : run;
    const flow_compiled *c = root->compiled;
    if (!c || c->finished ||
        (run->compiled_owner &&
         (run->state == FLOW_COMPLETED || run->state == FLOW_IDLE))) {
        return;
    }
    int frame = run->compiled_owner ? run->compiled_frame : 0;
    const flow_compiled_frame *boundary_frame = &c->frames[frame];
    const flow_compiled_invocation *invocation = flow_compiled_active(c);
    if (root->state == FLOW_FAILED && c->failed_return) {
        const flow_compiled_invocation *failed = c->failed_return;
        const graph *g = flow_compiled_frame_source(c, failed->parent_frame)->graph;
        apush(run->stack_cache, ((flow_call_frame){ g, g->graph_id, failed->call->id }));
    }
    int stack = root->state == FLOW_FAILED ? invocation->context->error_stack
                                           : invocation->context->activation->stack;
    for (;;) {
        int boundary = invocation == boundary_frame->invocation
                         ? behavior_function_frame_stack(
                               invocation->function,
                               boundary_frame->local
                           )
                         : -1;
        for (int at = stack; at >= 0 && at != boundary;) {
            const behavior_function_source
                *source = behavior_function_stack_at(invocation->function, at);
            apush(
                run->stack_cache,
                ((flow_call_frame){ source->graph,
                                    source->graph->graph_id,
                                    source->call->id })
            );
            at = source->parent;
        }
        if (invocation == boundary_frame->invocation) {
            break;
        }
        const graph *g = flow_compiled_frame_source(c, invocation->parent_frame)->graph;
        apush(
            run->stack_cache,
            ((flow_call_frame){ g, g->graph_id, invocation->call->id })
        );
        invocation = invocation->parent;
        assert(invocation && invocation->context);
        stack = invocation->context->activation->stack;
    }
    int count = acount(run->stack_cache);
    for (int i = 0; i < count / 2; i++) {
        flow_call_frame swap = run->stack_cache[i];
        run->stack_cache[i] = run->stack_cache[count - i - 1];
        run->stack_cache[count - i - 1] = swap;
    }
}
