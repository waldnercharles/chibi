#include "execution/definition.h"
#include "execution/function.h"
#include "execution/intrinsics.h"
#include "execution/program_internal.h"
#include "execution/request.h"
#include "execution/status.h"
#include "execution/task.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/copy_internal.h"
#include "types/map.h"
#include "types/pointer.h"
#include "types/query.h"
#include "types/record.h"
#include "types/union.h"

#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct task_waiter
{
    flow_host_request *request;
    uint64_t tag;
} task_waiter;

const node_command EXECUTION_Yield = { .intrinsic = NODE_COMMAND_YIELD };
const node_command EXECUTION_TaskJoin = { .intrinsic = NODE_COMMAND_TASK_JOIN };
const node_command EXECUTION_TaskCancel = { .intrinsic = NODE_COMMAND_TASK_CANCEL };
const node_command EXECUTION_TaskRunning = { .intrinsic = NODE_COMMAND_TASK_RUNNING };
const node_command EXECUTION_TaskCancelAfter = { .intrinsic = NODE_COMMAND_TASK_CANCEL_AFTER };

static const struct
{
    uint64_t id;
    const node_command *command;
} commands[] = {
    { EXECUTION_YIELD_ID, &EXECUTION_Yield },
    { EXECUTION_TASK_JOIN_ID, &EXECUTION_TaskJoin },
    { EXECUTION_TASK_CANCEL_ID, &EXECUTION_TaskCancel },
    { EXECUTION_TASK_RUNNING_ID, &EXECUTION_TaskRunning },
    { EXECUTION_TASK_CANCEL_AFTER_ID, &EXECUTION_TaskCancelAfter },
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

/* Generations come from one counter for every instance, so a handle names
 * one task in the whole program and another instance never finds it. */
static _Atomic uint32_t generations;

static uint32_t generation_new()
{
    uint32_t g;
    do {
        g = atomic_fetch_add_explicit(&generations, 1, memory_order_relaxed) + 1;
    } while (!g);
    return g;
}
static uint64_t handle_of(const execution_instance *instance, const execution_task *t)
{
    return (uint64_t)t->generation << 32 | (uint64_t)(t - instance->tasks + 1);
}
static execution_task *find(execution_instance *instance, uint64_t handle)
{
    uint32_t link = (uint32_t)handle;
    if (!link || link > (uint32_t)acount(instance->tasks) || !(handle >> 32)) {
        return NULL;
    }
    int slot = (int)(link - 1);
    if (instance->tasks[slot].generation != handle >> 32) {
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
static void release_results(execution_task *task)
{
    for (int i = 0; i < task->result_count; i++) {
        pin_value_collection_release(&task->results[i]);
    }
    task->result_count = 0;
}
/* Collection recycles a finished task only after every observing handle is gone. */
static void reclaim(execution_instance *instance, execution_task *t)
{
    int link = link_of(instance, t);
    release_results(t);
    arena_free(&t->storage);
    *t = (execution_task){ .next = instance->spare };
    instance->spare = link;
}
static execution_task *task_new(execution_instance *instance)
{
    if (!instance->spare) {
        apush(instance->tasks, ((execution_task){ .generation = generation_new() }));
        return &instance->tasks[acount(instance->tasks) - 1];
    }
    execution_task *t = slot(instance, instance->spare);
    instance->spare = t->next;
    t->next = 0;
    t->generation = generation_new();
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
/* Leaves the parent's child list and orphans its children. */
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
    *t = (execution_task){ .generation = t->generation, .storage = t->storage };
}

static void box_release(execution_box *box)
{
    if (--box->holders) {
        return;
    }
    behavior_function_collection_clear(&box->detached);
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
    int status = pin_value_snapshot_to_arena(&box->storage, &box->kept, box->value);
    assert(!status);
    (void)status;
    box->detached = (behavior_function_context){ .value_storage = &box->storage,
                                                 .result_storage = &box->storage,
                                                 .managed_heap = box->owner->managed_heap };
    behavior_function_collection_transfer(box->owner, &box->detached, &box->kept);
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
    t->started = ctx->now;
    if (parent) {
        adopt(instance, parent, t);
    }
    a->task = handle_of(instance, t);
    ctx->data->values[call->result_first] = (pin_value){ .type = PIN_TASK,
                                                         .u64 = a->task };
    int status = program_activation_run(a, ctx->now, ctx->delta_seconds, ctx->budget);
    if (!a->finished || acount(a->requests)) {
        apush(instance->activations, a);
    } else {
        program_activation_free(a);
    }
    /* The started task cancelled its caller. */
    if (caller->stopping) {
        return FLOW_SUSPENDED_RESULT;
    }
    return status < 0 ? status : FLOW_OK;
}
void program_bind_tasks(behavior_function_context *ctx)
{
    ctx->start_task = start_task;
    ctx->release_local = release_local;
}

/* Delivery snapshots into each waiter's payload, independently of task lifetime. */
static void deliver(const execution_task *t, task_waiter waiter)
{
    pin_union *borrowed = NULL;
    pin_value result;
    if (!t || t->cancelled) {
        pin_union_cancelled(&result);
    } else if (
        waiter.tag == 0 && t->result_count == 1 && t->results[0].type == PIN_UNION &&
        t->results[0].container == PIN_CONTAINER_SINGLE
    ) {
        result = t->results[0];
    } else {
        assert(waiter.tag && t->result_count >= 0);
        assert((size_t)t->result_count <= (SIZE_MAX - sizeof(pin_union)) / sizeof(pin_value));
        size_t bytes = (size_t)t->result_count * sizeof(pin_value);
        borrowed = xmalloc(sizeof *borrowed + bytes);
        *borrowed = (pin_union){ .tag = waiter.tag, .count = t->result_count };
        if (bytes) {
            memcpy(borrowed->values, t->results, bytes);
            pin_copy_record(PIN_COPY_VALUE_BUFFER, bytes);
        }
        result = (pin_value){ .type = PIN_UNION, .ptr = borrowed };
    }
    /* Only the wrapper borrows task storage. Delivery synchronously snapshots
     * it into the waiter's arena before completing its request; no borrowed
     * payload survives this call, and each waiter gets an independent copy. */
    program_request_deliver(waiter.request, &result, 1);
    free(borrowed);
    flow_host_release(waiter.request);
}
void program_task_finished(execution_activation *a, bool cancelled)
{
    execution_instance *instance = a->instance;
    execution_task *t = find(instance, a->task);
    if (!t || t->activation != a) {
        return;
    }
    task_waiter *waiters = t->waiters;
    retire(instance, t);
    t->cancelled = cancelled || a->status < 0;
    int count = t->cancelled ? 0 : acount(a->function->result_defaults);
    t->results = count ? arena_alloc(&t->storage, (size_t)count * sizeof *t->results)
                       : NULL;
    t->result_count = count;
    for (int i = 0; i < count; i++) {
        pin_value owned;
        int status = pin_value_collection_copy(
            &a->context.managed_heap->storage,
            &owned,
            &a->context.results[i]
        );
        if (!status) {
            status = pin_value_snapshot_to_arena(&t->storage, &t->results[i], &owned);
        }
        assert(!status);
        (void)status;
    }
    for (int i = 0; i < acount(waiters); i++) {
        deliver(t, waiters[i]);
    }
    afree(waiters);
}

/* Cancels the task and every task it started. A running one, such as the
 * caller or the task that started it, stops when control returns to it. */
static void cancel(execution_instance *instance, uint64_t handle)
{
    execution_task *t = find(instance, handle);
    if (!t || !t->activation) {
        return;
    }
    /* Cancelling a child may retire it from this list, so read ahead. */
    for (execution_task *c = slot(instance, t->child), *next; c; c = next) {
        next = slot(instance, c->next);
        cancel(instance, handle_of(instance, c));
    }
    execution_activation *a = find(instance, handle)->activation;
    if (a->running) {
        a->stopping = true;
        return;
    }
    program_activation_cancel(a);
    program_task_finished(a, true);
}

bool program_task_due(const execution_activation *a, double now)
{
    execution_task *t = find(a->instance, program_owning_task(a));
    return t && t->activation && t->timed && t->deadline <= now;
}
void program_tasks_tick(execution_instance *instance, double now)
{
    for (int i = 0; i < acount(instance->tasks); i++) {
        execution_task *t = &instance->tasks[i];
        if (t->activation && t->timed && t->deadline <= now) {
            cancel(instance, handle_of(instance, t));
        }
    }
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
    /* The command consumes its handle. A pending join is rooted by its waiter;
     * keeping protocol scratch here would retain an otherwise dropped result. */
    ctx->command_arguments[spec->first].u64 = 0;
    execution_task *t = find(instance, handle);
    bool running = t && t->activation;
    switch (kind) {
        case NODE_COMMAND_TASK_RUNNING:
            program_request_deliver(request, &(pin_value){ .type = PIN_BOOL, .b = running }, 1);
            break;
        case NODE_COMMAND_TASK_CANCEL_AFTER:
            if (running) {
                double duration = ctx->command_arguments[spec->first + 1].d;
                /* Negative and NaN durations mean zero. Positive infinity
                 * behaves like an infinite Delay and sets no finite deadline. */
                if (!(duration >= 0)) {
                    duration = 0;
                }
                double deadline = t->started + duration;
                if (!t->timed || deadline < t->deadline) {
                    t->deadline = deadline;
                    t->timed = true;
                }
                if (t->deadline > ctx->now) {
                    program_request_deliver(request, NULL, 0);
                    break;
                }
            }
            /* An overdue pending task is cancelled at this safe point. */
            /* fall through */
        case NODE_COMMAND_TASK_CANCEL: {
            /* A continuation is not yet listed while it first runs, so
             * cancelling its task, which may itself be running, cannot
             * reach it there. */
            uint64_t owner = program_owning_task(a);
            bool owned = find(instance, owner) && find(instance, owner)->activation;
            cancel(instance, handle);
            execution_task *own = find(instance, owner);
            a->stopping |= owned &&
                           (!own || !own->activation || own->activation->stopping);
            /* A caller that cancelled itself stops here. */
            if (a->stopping) {
                flow_host_release(request);
                return FLOW_OK;
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
                apush(
                    t->waiters,
                    ((task_waiter){ request,
                                    ctx->command_arguments[spec->first + 1].u64 })
                );
                return FLOW_OK;
            }
            if (!t && handle) {
                ctx->error_message = "the task handle does not belong to this "
                                     "owner";
                return FLOW_ERR_STATE;
            }
            deliver(
                t,
                (task_waiter){ request, ctx->command_arguments[spec->first + 1].u64 }
            );
            return FLOW_OK;
        default: assert(false); return FLOW_ERR_STATE;
    }
    flow_host_release(request);
    return FLOW_OK;
}

typedef struct
{
    uint64_t handle;
    execution_task *task;
    execution_instance *instance;
} task_root;
typedef struct
{
    task_root *index;
    execution_task **pending;
    pin_value *values; /* Reached values whose contents are not yet marked. */
    program_pointer_set pointers; /* Visited pointer roots. */
    const pin_query **queries;    /* Visited query values. */
    uint64_t lookup_steps;
} task_roots;
static int compare_roots(const void *left, const void *right)
{
    uint64_t a = ((const task_root *)left)->handle;
    uint64_t b = ((const task_root *)right)->handle;
    return (a > b) - (a < b);
}
static void mark_task(task_roots *roots, uint64_t handle)
{
    if (!handle) {
        return;
    }
    int first = 0, end = acount(roots->index);
    while (first < end) {
        int middle = first + (end - first) / 2;
        const task_root *entry = &roots->index[middle];
        roots->lookup_steps++;
        if (entry->handle < handle) {
            first = middle + 1;
            continue;
        }
        end = middle;
    }
    /* A generation can wrap after billions of starts. Conservatively retain
     * every equal global handle; owner-local lookup still enforces ownership. */
    for (int i = first; i < acount(roots->index) && roots->index[i].handle == handle;
         i++) {
        const task_root *entry = &roots->index[i];
        roots->lookup_steps++;
        if (!entry->task->marked) {
            entry->task->marked = true;
            apush(roots->pending, entry->task);
        }
    }
}
static size_t pointer_slot(const program_pointer_set *set, const pin_pointer *pointer)
{
    uint64_t hash = (uint64_t)(uintptr_t)pointer * 0x9e3779b97f4a7c15ull;
    size_t i = (size_t)(hash >> 32) & (set->capacity - 1);
    while (set->slots[i] && set->slots[i] != pointer) {
        i = (i + 1) & (set->capacity - 1);
    }
    return i;
}
/* Keeps the table at most half full, so long pointer chains stay linear. */
bool program_pointer_first_visit(program_pointer_set *set, const pin_pointer *pointer)
{
    if (2 * (set->count + 1) > set->capacity) {
        const pin_pointer **old = set->slots;
        size_t old_capacity = set->capacity;
        set->capacity = old_capacity ? old_capacity * 2 : 64;
        set->slots = xcalloc(set->capacity, sizeof *set->slots);
        for (size_t i = 0; i < old_capacity; i++) {
            if (old[i]) {
                set->slots[pointer_slot(set, old[i])] = old[i];
            }
        }
        free(old);
    }
    size_t i = pointer_slot(set, pointer);
    if (set->slots[i]) {
        return false;
    }
    set->slots[i] = pointer;
    set->count++;
    return true;
}
static void push_fields(task_roots *roots, const type_desc *type, const void *data, const pin_schema *schema)
{
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        const void *member = (const char *)data + field->offset;
        pin_value value = { .type = field->type,
                            .type_name = field->type_name,
                            .schema = schema };
        if (field->type == PIN_STRUCT) {
            value.ptr = (void *)member;
        } else if (field->type == PIN_TASK) {
            memcpy(&value.u64, member, sizeof value.u64);
        } else if (field->type == PIN_UNION || field->type == PIN_POINTER || field->type == PIN_QUERY) {
            memcpy(&value.ptr, member, sizeof value.ptr);
        } else {
            continue;
        }
        apush(roots->values, value);
    }
}
/* Marks one value's own task handle and queues the values it reaches. */
static void mark_one(task_roots *roots, const pin_value *value)
{
    if (value->type == PIN_QUERY && value->container == PIN_CONTAINER_SINGLE) {
        const pin_query *query = value->ptr;
        if (!query) {
            return;
        }
        for (int i = 0; i < acount(roots->queries); i++) {
            if (roots->queries[i] == query) {
                return;
            }
        }
        apush(roots->queries, query);
        for (int i = 0; i < query->count; i++) {
            apush(roots->values, query->values[i]);
        }
        return;
    }
    if (value->type == PIN_POINTER && value->container == PIN_CONTAINER_SINGLE) {
        const pin_pointer *pointer = value->ptr;
        if (!pointer) {
            return;
        }
        while (pointer->base) {
            pointer = pointer->base;
        }
        if (program_pointer_first_visit(&roots->pointers, pointer)) {
            apush(roots->values, pointer->value);
        }
        return;
    }
    if (value->container == PIN_CONTAINER_MAP) {
        const pin_map *map = value->ptr;
        for (int i = 0; map && i < map->count; i++) {
            apush(roots->values, map->entries[i].key);
            apush(roots->values, map->entries[i].value);
        }
        return;
    }
    if (value->container != PIN_CONTAINER_SINGLE) {
        const pin_array *array = value->ptr;
        if (array && !pin_array_view_valid(array)) {
            return;
        }
        /* Packed pointer and record arrays can reach managed task handles. */
        if (array && array->stride && value->type != PIN_POINTER &&
            value->type != PIN_QUERY && value->type != PIN_STRUCT &&
            value->type != PIN_TASK && value->type != PIN_UNION) {
            return;
        }
        for (int i = 0; array && i < array->count; i++) {
            apush(roots->values, pin_array_get(array, i));
        }
        return;
    }
    switch (value->type) {
        case PIN_TASK: mark_task(roots, value->u64); break;
        case PIN_UNION: {
            const pin_union *un = value->ptr;
            for (int i = 0; un && i < un->count; i++) {
                apush(roots->values, un->values[i]);
            }
            break;
        }
        case PIN_STRUCT:
            if (value->ptr) {
                const type_desc *type = pin_value_type(value);
                assert(type);
                push_fields(roots, type, value->ptr, value->schema);
            }
            break;
        default: break;
    }
}
/* A worklist, not recursion, so a long valid pointer chain cannot exhaust the
 * native stack. */
static void mark_value(void *user, const pin_value *value)
{
    task_roots *roots = user;
    mark_one(roots, value);
    while (acount(roots->values)) {
        pin_value next = apop(roots->values);
        mark_one(roots, &next);
    }
}
static void mark_activation(task_roots *roots, execution_activation *a)
{
    if (!a->finished) {
        behavior_function_visit_values(a->function, &a->context, mark_value, roots);
        for (int i = 0; i < acount(a->actions); i++) {
            for (int j = 0; j < a->actions[i].count; j++) {
                mark_value(roots, &a->actions[i].values[j]);
            }
        }
    }
    /* Captured references keep detached locals after their declaring frame returns. */
    for (int i = 0; i < acount(a->held); i++) {
        mark_value(roots, a->held[i]->value);
    }
    for (int i = 0; i < acount(a->boxes); i++) {
        mark_value(roots, a->boxes[i]->value);
    }
}
void program_tasks_collect(execution_program *p)
{
    p = p->owner_program;
    assert(!p->running);
    task_roots roots = { 0 };
    p->task_lookup_steps = 0;
    for (int scope = -1; scope < acount(p->imports); scope++) {
        execution_program *program = scope < 0 ? p : p->imports[scope];
        assert(!program->running);
        for (int i = 0; i < acount(program->instances); i++) {
            execution_instance *instance = program->instances[i];
            for (int j = 0; j < acount(instance->tasks); j++) {
                execution_task *t = &instance->tasks[j];
                t->marked = false;
                if (t->generation) {
                    apush(roots.index, ((task_root){ handle_of(instance, t), t, instance }));
                }
            }
        }
    }
    if (!acount(roots.index)) {
        return;
    }
    /* One global index avoids searching every owner for every handle root. */
    qsort(roots.index, (size_t)acount(roots.index), sizeof *roots.index, compare_roots);
    for (int scope = -1; scope < acount(p->imports); scope++) {
        execution_program *program = scope < 0 ? p : p->imports[scope];
        for (int i = 0; i < acount(program->query_values); i++) {
            mark_value(&roots, &program->query_values[i]);
        }
        for (int i = 0; i < acount(program->system_values); i++) {
            mark_value(&roots, &program->system_values[i]);
        }
        for (int i = 0; i < program->module->plan.state_count; i++) {
            mark_value(&roots, &program->shared[i].value);
        }
        for (int i = 0; i < acount(program->events); i++) {
            for (int j = 0; j < program->events[i].count; j++) {
                mark_value(&roots, &program->events[i].arguments[j]);
            }
        }
        for (const queued_event_batch *batch = program->dispatch_events; batch;
             batch = batch->previous) {
            for (int i = batch->first; i < acount(batch->events); i++) {
                for (int j = 0; j < batch->events[i].count; j++) {
                    mark_value(&roots, &batch->events[i].arguments[j]);
                }
            }
        }
        for (int i = 0; i < acount(program->instances); i++) {
            execution_instance *instance = program->instances[i];
            mark_value(&roots, &instance->query_row);
            for (int j = 0; j < program->module->plan.state_count; j++) {
                mark_value(&roots, &instance->values[j]);
            }
            for (int j = 0; j < acount(instance->activations); j++) {
                mark_activation(&roots, instance->activations[j]);
            }
        }
    }
    for (const behavior_managed_heap *heap = p->managed; heap; heap = heap->previous) {
        for (int i = 0; i < acount(heap->roots); i++) {
            mark_value(&roots, heap->roots[i]);
        }
    }
    /* A result can itself contain task handles. Iterate instead of recursing
     * down arbitrarily long chains of completed tasks. */
    for (int i = 0; i < acount(roots.pending); i++) {
        execution_task *t = roots.pending[i];
        for (int j = 0; j < t->result_count; j++) {
            mark_value(&roots, &t->results[j]);
        }
    }
    for (int i = 0; i < acount(roots.index); i++) {
        execution_task *t = roots.index[i].task;
        if (!t->activation && !t->marked) {
            reclaim(roots.index[i].instance, t);
        }
    }
    p->task_lookup_steps = roots.lookup_steps;
    afree(roots.pending);
    afree(roots.values);
    free(roots.pointers.slots);
    afree(roots.queries);
    afree(roots.index);
}

void program_tasks_free(execution_instance *instance)
{
    for (int i = 0; i < acount(instance->tasks); i++) {
        execution_task *t = &instance->tasks[i];
        for (int j = 0; j < acount(t->waiters); j++) {
            flow_host_release(t->waiters[j].request);
        }
        afree(t->waiters);
        release_results(t);
        arena_free(&t->storage);
    }
    afree(instance->tasks);
}
