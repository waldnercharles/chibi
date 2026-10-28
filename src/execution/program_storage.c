#include "execution/program_internal.h"
#include "execution/scalar.h"
#include "execution/status.h"
#include "support/array.h"
#include "types/trace.h"

#include <assert.h>

/* Collect once the payload allocated since the last collection outgrows what
 * it kept, so many live tasks don't each trace every task on every run. */
#define PAYLOAD_COLLECT_MIN_BYTES ((size_t)4 * 1024)

/* Each activation is listed by its own instance once it has run. */
static bool listed(const execution_activation *a)
{
    for (int i = 0; i < acount(a->instance->activations); i++) {
        if (a->instance->activations[i] == a) {
            return true;
        }
    }
    return false;
}
static void scope_activations(const execution_program *p, execution_activation ***values)
{
    for (int i = 0; i < acount(p->instances); i++) {
        const execution_instance *instance = p->instances[i];
        for (int j = 0; j < acount(instance->activations); j++) {
            apush(*values, instance->activations[j]);
        }
    }
}
static execution_activation **activations(const execution_program *p, execution_activation *extra)
{
    execution_activation **values = NULL;
    scope_activations(p, &values);
    for (int i = 0; i < acount(p->imports); i++) {
        scope_activations(p->imports[i], &values);
    }
    if (extra && !listed(extra)) {
        apush(values, extra);
    }
    return values;
}
typedef struct
{
    size_t allocated, kept;
    bool running;
} payload_totals;
static void add_totals(payload_totals *totals, const execution_activation *a)
{
    totals->allocated += a->payload.allocated;
    totals->kept += a->payload.kept;
    totals->running |= a->running;
}
static void scope_totals(const execution_program *p, payload_totals *totals)
{
    /* A system paused under reductions keeps its activations, whose slots
     * and payloads no collection lists, until the system finishes. */
    totals->running |= p->running != NULL ||
                       (p->system_prepared && p->system_status == FLOW_PAUSED_RESULT);
    for (int i = 0; i < acount(p->instances); i++) {
        const execution_instance *instance = p->instances[i];
        for (int j = 0; j < acount(instance->activations); j++) {
            add_totals(totals, instance->activations[j]);
        }
    }
}
/* Whether the payload has outgrown the last collection while nothing runs: a
 * nested synchronous invocation has not saved its live slots. */
static bool collection_due(const execution_program *p, const execution_activation *extra)
{
    payload_totals totals = { 0 };
    for (behavior_managed_heap *heap = p->owner_program->managed; heap;
         heap = heap->previous) {
        totals.allocated += heap->storage.allocated;
        totals.kept += heap->storage.kept;
    }
    scope_totals(p, &totals);
    for (int i = 0; i < acount(p->imports); i++) {
        scope_totals(p->imports[i], &totals);
    }
    if (extra && !listed(extra)) {
        add_totals(&totals, extra);
    }
    return !totals.running && totals.allocated >= PAYLOAD_COLLECT_MIN_BYTES &&
           totals.allocated >= totals.kept;
}
static void trace_action_values(const execution_activation *a, arena_marker marker, void *user)
{
    for (int i = 0; i < acount(a->actions); i++) {
        const action_values *action = &a->actions[i];
        for (int j = 0; j < action->count; j++) {
            const pin_value *value = &action->values[j];
            behavior_managed_heap_trace_value(value, marker, user);
            if (value->type == BF_POINTER_TYPE &&
                value->container == PIN_CONTAINER_SINGLE) {
                marker(user, (const void *)(uintptr_t)behavior_scalar_bits(*value));
            }
        }
    }
}
static void managed_value(arena_collection *collection, const pin_value *value)
{
    behavior_managed_heap_trace_value(value, arena_collect_mark, collection);
}
static void managed_scope_roots(const execution_program *p, arena_collection *collection)
{
    const execution_module_plan *plan = &p->module->plan;
    for (int i = 0; i < plan->state_count; i++) {
        managed_value(collection, &p->shared[i].value);
    }
    for (int i = 0; i < acount(p->events); i++) {
        for (int j = 0; j < p->events[i].count; j++) {
            managed_value(collection, &p->events[i].arguments[j]);
        }
    }
    for (const queued_event_batch *batch = p->dispatch_events; batch;
         batch = batch->previous) {
        for (int i = batch->first; i < acount(batch->events); i++) {
            for (int j = 0; j < batch->events[i].count; j++) {
                managed_value(collection, &batch->events[i].arguments[j]);
            }
        }
    }
    for (int i = 0; i < acount(p->field_edges); i++) {
        managed_value(collection, &p->field_edges[i].value);
    }
    for (int i = 0; i < acount(p->reductions); i++) {
        managed_value(collection, &p->reductions[i].value);
    }
    for (int i = 0; i < acount(p->query_values); i++) {
        managed_value(collection, &p->query_values[i]);
    }
    for (int i = 0; i < acount(p->system_values); i++) {
        managed_value(collection, &p->system_values[i]);
    }
    for (int i = 0; i < acount(p->instances); i++) {
        const execution_instance *instance = p->instances[i];
        managed_value(collection, &instance->query_row);
        for (int j = 0; j < plan->state_count; j++) {
            managed_value(collection, &instance->values[j]);
            managed_value(collection, &instance->states[j].value);
        }
        for (int j = 0; j < acount(instance->node_states); j++) {
            managed_value(collection, &instance->node_states[j]);
        }
        for (int j = 0; j < plan->binding_count; j++) {
            const binding_cache *cache = &instance->bindings[j];
            if (!cache->valid) {
                continue;
            }
            managed_value(collection, &cache->output);
            for (int k = 0; k < plan->bindings[j].dependency_count; k++) {
                managed_value(collection, &cache->inputs[k]);
            }
        }
        for (int j = 0; j < acount(instance->observations); j++) {
            managed_value(collection, &instance->observations[j].value);
        }
        for (int j = 0; j < acount(instance->tasks); j++) {
            const execution_task *task = &instance->tasks[j];
            for (int k = 0; k < task->result_count; k++) {
                managed_value(collection, &task->results[k]);
            }
        }
    }
}
void program_collect_payloads(execution_program *p, execution_activation *extra)
{
    p = p->owner_program;
    if (!collection_due(p, extra)) {
        return;
    }
    execution_activation **values = activations(p, extra);
    arena **arenas = NULL;
    for (int i = 0; i < acount(values); i++) {
        apush(arenas, &values[i]->payload);
    }
    for (behavior_managed_heap *heap = p->managed; heap; heap = heap->previous) {
        apush(arenas, &heap->storage);
    }
    arena_collection c;
    arena_collect_begin(&c, arenas, acount(arenas));
    for (int i = 0; i < acount(values); i++) {
        behavior_function_trace_values(values[i]->function, &values[i]->context, arena_collect_mark, &c);
        trace_action_values(values[i], arena_collect_mark, &c);
    }
    behavior_managed_heap_trace_roots(p->managed, arena_collect_mark, &c);
    managed_scope_roots(p, &c);
    for (int i = 0; i < acount(p->imports); i++) {
        managed_scope_roots(p->imports[i], &c);
    }
    for (int i = 0; i < acount(values); i++) {
        const execution_activation *a = values[i];
        for (int j = 0; j < acount(a->boxes); j++) {
            managed_value(&c, a->boxes[j]->value);
        }
        for (int j = 0; j < acount(a->held); j++) {
            managed_value(&c, a->held[j]->value);
        }
    }
    arena_collect_end(&c);
    afree(arenas);
    afree(values);
    behavior_managed_heap_release_empty(p->managed);
}
execution_memory_usage execution_program_memory_usage(const execution_program *p)
{
    assert(p && !p->owner_program->running);
    execution_memory_usage result = { 0 };
    for (const behavior_managed_heap *heap = p->owner_program->managed; heap;
         heap = heap->previous) {
        size_t bytes, capacity;
        arena_usage(&heap->storage, &bytes, &capacity);
        result.payload_bytes += bytes;
        result.payload_capacity += capacity;
    }
    execution_activation **values = activations(p->owner_program, NULL);
    result.activation_bytes = (size_t)acount(values) * sizeof(execution_activation);
    for (int i = 0; i < acount(values); i++) {
        size_t bytes, capacity;
        arena_usage(&values[i]->payload, &bytes, &capacity);
        result.payload_bytes += bytes;
        result.payload_capacity += capacity;
    }
    afree(values);
    p = p->owner_program;
    result.task_lookup_steps = p->task_lookup_steps;
    result.query_rows = p->query_rows;
    result.query_lookup_steps = p->query_lookup_steps;
    result.ecs_change_bytes = p->world ? ecs_world_change_tracking_bytes(p->world) : 0;
    for (int scope = -1; scope < acount(p->imports); scope++) {
        const execution_program *program = scope < 0 ? p : p->imports[scope];
        result.activation_cache_bytes += program_activation_cache_bytes(program);
        result.row_worker_capacity += program_row_worker_bytes(program);
        result.row_parallel_jobs += program->row_parallel_jobs;
        result.row_parallel_workers += program->row_parallel_workers;
        result.query_cache_bytes += program->query_cache_bytes;
        result.reduction_bytes += (size_t)acount(program->reductions) *
                                  sizeof *program->reductions;
        result.reduction_capacity += program->reductions
                                       ? sizeof(dyna_hdr) +
                                             (size_t)acap(program->reductions) *
                                                 sizeof *program->reductions
                                       : 0;
        result.reduction_capacity += program->reduction_table
                                       ? sizeof(dyna_hdr) +
                                             (size_t)acap(program->reduction_table) *
                                                 sizeof *program->reduction_table
                                       : 0;
        result.reduction_contributions += program->reduction_contributions;
        result.reduction_records += program->reduction_records;
        result.reduction_peak_bytes += program->reduction_peak_bytes;
        result.system_row_bytes += (size_t)acount(program->system_rows) *
                                   sizeof *program->system_rows;
        result.system_row_bytes += (size_t)(acount(program->query_values) +
                                            acount(program->system_values)) *
                                   sizeof(pin_value);
        if (program->query_values) {
            result.system_row_capacity += sizeof(dyna_hdr) +
                                          (size_t)acap(program->query_values) *
                                              sizeof(pin_value);
        }
        if (program->system_values) {
            result.system_row_capacity += sizeof(dyna_hdr) +
                                          (size_t)acap(program->system_values) *
                                              sizeof(pin_value);
        }
        result.system_row_capacity += program->system_rows
                                        ? sizeof(dyna_hdr) +
                                              (size_t)acap(program->system_rows) *
                                                  sizeof *program->system_rows
                                        : 0;
        for (int i = 0; i < acount(program->system_activations); i++) {
            const execution_activation *activation = program->system_activations[i];
            if (activation) {
                result.activation_bytes += sizeof *activation;
                size_t bytes, capacity;
                arena_usage(&activation->payload, &bytes, &capacity);
                result.payload_bytes += bytes;
                result.payload_capacity += capacity;
            }
        }
        result.instance_count += acount(program->instances);
        result.instance_bytes += (size_t)acount(program->instances) *
                                 sizeof(execution_instance);
        result.query_index_bytes += (size_t)acount(program->row_table) *
                                    sizeof *program->row_table;
        result.binding_schedule_bytes += program_binding_storage_bytes(program);
        result.query_history_bytes += (size_t)acount(program->instances) *
                                      (size_t)acount(program->change_queries) *
                                      2 * sizeof(query_change);
        result.query_history_bytes += (size_t)acount(program->query_admissions) *
                                      sizeof(uint64_t);
        result.state_bank_bytes += (size_t)acount(program->instances) *
                                   (size_t)program->module->plan.state_count *
                                   (sizeof(state_value) + sizeof(pin_value) +
                                    sizeof(behavior_variable_storage));
        for (int i = 0; i < acount(program->instances); i++) {
            const execution_instance *instance = program->instances[i];
            result.task_slots += acount(instance->tasks);
            for (int j = 0; j < acount(instance->tasks); j++) {
                const execution_task *task = &instance->tasks[j];
                result.retained_tasks += task->generation && !task->activation;
                result.task_result_bytes += arena_storage_bytes(&task->storage);
            }
        }
    }
    return result;
}
