#include "execution/definition.h"
#include "execution/function.h"
#include "execution/intrinsics.h"
#include "execution/program_internal.h"
#include "execution/request.h"
#include "execution/status.h"
#include "execution/task.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>

/* Finished slots keep their results for later awaits until this many have
 * accumulated; the oldest is then reused under a new generation, and awaiting
 * its old handle fails. */
#define TASK_RETAINED 32

const node_command EXECUTION_Yield = { .intrinsic = NODE_COMMAND_YIELD };
const node_command EXECUTION_TaskJoin = { .intrinsic = NODE_COMMAND_TASK_JOIN };
const node_command EXECUTION_TaskCancel = { .intrinsic = NODE_COMMAND_TASK_CANCEL };
const node_command EXECUTION_TaskRunning = { .intrinsic = NODE_COMMAND_TASK_RUNNING };

static const struct
{
    uint64_t id;
    const node_command *command;
} commands[] = {
    { EXECUTION_YIELD_ID, &EXECUTION_Yield },
    { EXECUTION_TASK_JOIN_ID, &EXECUTION_TaskJoin },
    { EXECUTION_TASK_CANCEL_ID, &EXECUTION_TaskCancel },
    { EXECUTION_TASK_RUNNING_ID, &EXECUTION_TaskRunning },
};

const node_command *execution_task_command(uint64_t id, uint64_t abi)
{
    for (size_t i = 0;
         abi == EXECUTION_TASK_ABI && i < sizeof commands / sizeof *commands;
         i++) {
        if (commands[i].id == id) {
            return commands[i].command;
        }
    }
    return NULL;
}
uint64_t execution_task_command_id(const node_command *command)
{
    for (size_t i = 0; i < sizeof commands / sizeof *commands; i++) {
        if (commands[i].command == command) {
            return commands[i].id;
        }
    }
    return 0;
}

static uint64_t handle_of(const execution_instance *instance, const execution_task *t)
{
    return (uint64_t)t->generation << 32 | (uint64_t)(t - instance->tasks + 1);
}
static execution_task *find(execution_instance *instance, uint64_t handle)
{
    int slot = (int)(handle & UINT32_MAX) - 1;
    if (slot < 0 || slot >= acount(instance->tasks) ||
        instance->tasks[slot].generation != handle >> 32) {
        return NULL;
    }
    return &instance->tasks[slot];
}
static execution_task *slot(execution_instance *instance, int link)
{
    return link ? &instance->tasks[link - 1] : NULL;
}
static int link_of(const execution_instance *instance, const execution_task *t)
{
    return (int)(t - instance->tasks + 1);
}
/* Drops the oldest finished task's results and invalidates its handle. */
static void evict(execution_instance *instance)
{
    execution_task *t = slot(instance, instance->finished_first);
    int link = instance->finished_first;
    instance->finished_first = t->next;
    instance->finished_last = t->next ? instance->finished_last : 0;
    instance->finished_count--;
    arena_free(&t->storage);
    *t = (execution_task){ .generation = t->generation + 1, .next = instance->spare };
    instance->spare = link;
}
/* Reuses an evicted slot. Finishing evicts, so starting a task never
 * expires a result early. */
static execution_task *task_new(execution_instance *instance)
{
    if (!instance->spare) {
        apush(instance->tasks, ((execution_task){ .generation = 1 }));
        return &instance->tasks[acount(instance->tasks) - 1];
    }
    execution_task *t = slot(instance, instance->spare);
    instance->spare = t->next;
    t->next = 0;
    return t;
}
static void adopt(execution_instance *instance, execution_task *parent, execution_task *t)
{
    execution_task *first = slot(instance, parent->child);
    t->parent = link_of(instance, parent);
    t->next = parent->child;
    if (first) {
        first->previous = link_of(instance, t);
    }
    parent->child = link_of(instance, t);
}
/* Leaves the parent's child list, orphans the children and joins the queue of
 * finished slots. */
static void retire(execution_instance *instance, execution_task *t)
{
    execution_task *previous = slot(instance, t->previous),
                   *next = slot(instance, t->next);
    if (previous) {
        previous->next = t->next;
    } else if (t->parent) {
        slot(instance, t->parent)->child = t->next;
    }
    if (next) {
        next->previous = t->previous;
    }
    for (execution_task *c = slot(instance, t->child); c; c = slot(instance, c->next)) {
        c->parent = 0;
    }
    int link = link_of(instance, t);
    *t = (execution_task){ .generation = t->generation, .storage = t->storage };
    if (instance->finished_last) {
        slot(instance, instance->finished_last)->next = link;
    } else {
        instance->finished_first = link;
    }
    instance->finished_last = link;
    if (++instance->finished_count > TASK_RETAINED) {
        evict(instance);
    }
}

static void box_release(execution_box *box)
{
    if (--box->holders) {
        return;
    }
    arena_free(&box->storage);
    free(box);
}
/* The caller's box over a local, made on the first task that keeps it. */
static execution_box *box_for(behavior_function_context *owner, pin_value *slot)
{
    execution_activation *caller = owner->inspection_user;
    for (int i = 0; i < acount(caller->boxes); i++) {
        if (caller->boxes[i]->owner == owner && caller->boxes[i]->value == slot) {
            return caller->boxes[i];
        }
    }
    execution_box *box = xcalloc(1, sizeof *box);
    *box = (execution_box){ .holders = 1, .owner = owner, .value = slot };
    apush(caller->boxes, box);
    return box;
}
/* The box keeps the local's value once its frame no longer does. */
static void box_detach(execution_box *box)
{
    behavior_function_context *owner = box->owner;
    if (owner->initialize_local) {
        owner->initialize_local(owner, box->value, false);
    }
    int status = pin_value_snapshot_to_arena(&box->storage, &box->kept, box->value);
    assert(!status);
    (void)status;
    box->detached = (behavior_function_context){ .value_storage = &box->storage };
    box->owner = &box->detached;
    box->value = &box->kept;
    box_release(box);
}
static void detach_where(execution_activation *a, behavior_function_context *owner, pin_value *slot)
{
    for (int i = 0; i < acount(a->boxes);) {
        execution_box *box = a->boxes[i];
        if ((owner && box->owner != owner) || (slot && box->value != slot)) {
            i++;
            continue;
        }
        adel(a->boxes, i);
        box_detach(box);
    }
}
void program_boxes_detach(execution_activation *a, behavior_function_context *context)
{
    detach_where(a, context, NULL);
}
void program_boxes_free(execution_activation *a)
{
    detach_where(a, NULL, NULL);
    afree(a->boxes);
    for (int i = 0; i < acount(a->held); i++) {
        box_release(a->held[i]);
    }
    afree(a->held);
}
static void release_local(behavior_function_context *ctx, pin_value *local)
{
    detach_where(ctx->inspection_user, ctx, local);
}
/* The activation owns its argument copies; references in them move to it,
 * and a reference to a local shares the local through a box. */
static void capture_references(execution_activation *a, const behavior_function_invocation *call)
{
    pin_value *arguments = (pin_value *)a->context.arguments;
    for (int i = 0; i < call->count; i++) {
        if (behavior_function_kind(call->function, BEHAVIOR_BANK_ARGUMENT, i) !=
            BEHAVIOR_REFERENCE) {
            continue;
        }
        behavior_function_context *owner;
        pin_value *slot;
        execution_box *box;
        if (execution_reference_local(arguments[i], &owner, &slot, &box)) {
            box = box ? box : box_for(owner, slot);
            box->holders++;
            apush(a->held, box);
        }
        execution_reference_capture(&a->payload, &a->context, &arguments[i], box);
    }
}
uint64_t program_owning_task(const execution_activation *a)
{
    while (!a->task && a->continuation_owner) {
        a = a->continuation_owner;
    }
    return a->task;
}
static int start_task(behavior_function_context *ctx, int index)
{
    execution_activation *caller = ctx->inspection_user;
    execution_instance *instance = caller->instance;
    const behavior_function *function = caller->function;
    behavior_function_active_context(&caller->context, &function);
    const behavior_function_invocation *call = &function->calls[index];
    execution_activation *a = program_activation_new(
        instance,
        call->function,
        ctx->data->values + call->first,
        call->count,
        ctx->now,
        ctx->delta_seconds,
        ctx->budget
    );
    if (!a) {
        ctx->error_message = "task arguments do not match the function";
        return FLOW_ERR_ARGUMENT;
    }
    capture_references(a, call);
    /* An outcome continuation runs on behalf of the task that owns it. */
    execution_task *t = task_new(instance),
                   *parent = find(instance, program_owning_task(caller));
    t->activation = a;
    if (parent) {
        adopt(instance, parent, t);
    }
    a->task = handle_of(instance, t);
    ctx->data->values[call->result_first] = (pin_value){ .type = PIN_U64,
                                                         .u64 = a->task };
    int status = program_activation_run(a, ctx->now, ctx->delta_seconds, ctx->budget);
    if (!a->finished || acount(a->requests)) {
        apush(instance->activations, a);
    } else {
        program_activation_free(a);
    }
    return status < 0 ? status : FLOW_OK;
}
void program_bind_tasks(behavior_function_context *ctx)
{
    ctx->start_task = start_task;
    ctx->release_local = release_local;
}

void program_task_finished(execution_activation *a, bool cancelled)
{
    execution_instance *instance = a->instance;
    execution_task *t = find(instance, a->task);
    if (!t || t->activation != a) {
        return;
    }
    flow_host_request **waiters = t->waiters;
    retire(instance, t);
    int count = acount(a->function->result_defaults);
    t->results = arena_alloc(&t->storage, (size_t)(count + 1) * sizeof *t->results);
    t->result_count = count;
    for (int i = 0; i < count; i++) {
        const pin_value *value = cancelled || a->status < 0
                                   ? &a->function->result_defaults[i]
                                   : &a->context.results[i];
        int status = pin_value_snapshot_to_arena(&t->storage, &t->results[i], value);
        assert(!status);
        (void)status;
    }
    for (int i = 0; i < acount(waiters); i++) {
        program_request_deliver(waiters[i], t->results, t->result_count);
        flow_host_release(waiters[i]);
    }
    afree(waiters);
}

/* Whether the task or one of its descendants is the running caller. */
static bool running(execution_instance *instance, const execution_task *t)
{
    if (t->activation && t->activation->running) {
        return true;
    }
    for (execution_task *c = slot(instance, t->child); c; c = slot(instance, c->next)) {
        if (running(instance, c)) {
            return true;
        }
    }
    return false;
}
/* Whether `caller` works for `t` or one of its descendants, as an outcome
 * continuation that resumed on its own does. */
static bool works_for(execution_instance *instance, const execution_activation *caller, const execution_task *t)
{
    for (execution_task *c = find(instance, program_owning_task(caller)); c;
         c = slot(instance, c->parent)) {
        if (c == t) {
            return true;
        }
    }
    return false;
}
static int cancel(execution_instance *instance, const execution_activation *caller, uint64_t handle)
{
    execution_task *t = find(instance, handle);
    if (!t || !t->activation) {
        return FLOW_OK;
    }
    /* Checked before anything is cancelled, so a refusal changes nothing. */
    if (running(instance, t) || works_for(instance, caller, t)) {
        return FLOW_ERR_STATE;
    }
    /* Cancelling a child retires it from this list. */
    for (execution_task *c = slot(instance, t->child); c; c = slot(instance, t->child)) {
        int status = cancel(instance, caller, handle_of(instance, c));
        if (status < 0) {
            return status;
        }
    }
    t = find(instance, handle);
    execution_activation *a = t->activation;
    program_activation_cancel(a);
    program_task_finished(a, true);
    return FLOW_OK;
}

int program_task_command(
    behavior_function_context *ctx,
    const behavior_function_command *spec,
    node_command_intrinsic kind,
    flow_host_request *request
)
{
    execution_activation *a = ctx->inspection_user;
    execution_instance *instance = a->instance;
    uint64_t handle = ctx->command_arguments[spec->first].u64;
    execution_task *t = find(instance, handle);
    bool running = t && t->activation;
    switch (kind) {
        case NODE_COMMAND_TASK_RUNNING:
            program_request_deliver(request, &(pin_value){ .type = PIN_BOOL, .b = running }, 1);
            break;
        case NODE_COMMAND_TASK_CANCEL: {
            int status = cancel(instance, a, handle);
            if (status < 0) {
                ctx->error_message = "a task cannot cancel itself or a task "
                                     "that is running it";
                return status;
            }
            program_request_deliver(request, NULL, 0);
            break;
        }
        case NODE_COMMAND_TASK_JOIN:
            if (running) {
                if (program_owning_task(a) == handle) {
                    ctx->error_message = "a task cannot await itself";
                    return FLOW_ERR_STATE;
                }
                apush(t->waiters, request);
                return FLOW_OK;
            }
            if (!t && handle) {
                ctx->error_message = "the awaited task's results have expired; "
                                     "keep a result you need longer in a state";
                return FLOW_ERR_STATE;
            }
            program_request_deliver(request, t ? t->results : NULL, t ? t->result_count : 0);
            break;
        default: assert(false); return FLOW_ERR_STATE;
    }
    flow_host_release(request);
    return FLOW_OK;
}

void program_tasks_free(execution_instance *instance)
{
    for (int i = 0; i < acount(instance->tasks); i++) {
        execution_task *t = &instance->tasks[i];
        for (int j = 0; j < acount(t->waiters); j++) {
            flow_host_release(t->waiters[j]);
        }
        afree(t->waiters);
        arena_free(&t->storage);
    }
    afree(instance->tasks);
}
