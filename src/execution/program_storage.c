#include "execution/program_internal.h"
#include "execution/scalar.h"
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
    totals->running |= p->running != NULL;
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
            pin_value_trace(value, marker, user);
            if (value->type == BF_POINTER_TYPE &&
                value->container == PIN_CONTAINER_SINGLE) {
                marker(user, (const void *)(uintptr_t)behavior_scalar_bits(*value));
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
    arena_collection c;
    arena_collect_begin(&c, arenas, acount(arenas));
    for (int i = 0; i < acount(values); i++) {
        behavior_function_trace_values(values[i]->function, &values[i]->context, arena_collect_mark, &c);
        trace_action_values(values[i], arena_collect_mark, &c);
    }
    arena_collect_end(&c);
    afree(arenas);
    afree(values);
}
execution_memory_usage execution_program_memory_usage(const execution_program *p)
{
    assert(p && !p->owner_program->running);
    execution_memory_usage result = { 0 };
    execution_activation **values = activations(p->owner_program, NULL);
    for (int i = 0; i < acount(values); i++) {
        size_t bytes, capacity;
        arena_usage(&values[i]->payload, &bytes, &capacity);
        result.payload_bytes += bytes;
        result.payload_capacity += capacity;
    }
    afree(values);
    return result;
}
