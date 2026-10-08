#include "execution/program_internal.h"
#include "execution/scalar.h"
#include "support/array.h"
#include "types/trace.h"

#include <assert.h>

static void add_activation(execution_activation ***values, execution_activation *a)
{
    for (int i = 0; i < acount(*values); i++) {
        if ((*values)[i] == a) {
            return;
        }
    }
    apush(*values, a);
}
static void scope_activations(const execution_program *p, execution_activation ***values)
{
    for (int i = 0; i < acount(p->instances); i++) {
        const execution_instance *instance = p->instances[i];
        for (int j = 0; j < acount(instance->activations); j++) {
            add_activation(values, instance->activations[j]);
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
    if (extra) {
        add_activation(&values, extra);
    }
    return values;
}
static void mark(void *user, const void *address)
{
    execution_activation **values = user;
    for (int i = 0; i < acount(values); i++) {
        arena_mark(&values[i]->payload, address);
    }
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
    if (p->running) {
        return; /* A nested synchronous invocation has not saved its live slots. */
    }
    for (int i = 0; i < acount(p->imports); i++) {
        if (p->imports[i]->running) {
            return;
        }
    }
    execution_activation **values = activations(p, extra);
    for (int i = 0; i < acount(values); i++) {
        if (values[i]->running) {
            afree(values);
            return;
        }
    }
    for (int i = 0; i < acount(values); i++) {
        arena_collect_begin(&values[i]->payload);
    }
    for (int i = 0; i < acount(values); i++) {
        behavior_function_trace_values(values[i]->function, &values[i]->context, mark, values);
        trace_action_values(values[i], mark, values);
    }
    bool progress;
    do {
        progress = false;
        for (int i = 0; i < acount(values); i++) {
            progress |= arena_trace_marked(&values[i]->payload, mark, values);
        }
    } while (progress);
    for (int i = 0; i < acount(values); i++) {
        arena_sweep(&values[i]->payload);
    }
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
