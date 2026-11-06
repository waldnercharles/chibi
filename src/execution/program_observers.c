#include "execution/program_internal.h"
#include "execution/scalar.h"
#include "execution/status.h"
#include "support/array.h"
#include "types/array.h"
#include "types/catalog.h"

#include <stdlib.h>

bool program_instance_busy(const execution_instance *instance)
{
    for (int i = 0; i < acount(instance->activations); i++) {
        /* Tasks run beside later updates rather than delaying them. */
        if (!instance->activations[i]->finished &&
            !program_owning_task(instance->activations[i])) {
            return true;
        }
    }
    return instance->running != NULL;
}
void program_observers_free(execution_instance *instance)
{
    for (int i = 0; i < acount(instance->observations); i++) {
        arena_free(&instance->observations[i].storage);
    }
    afree(instance->observations);
}
static const type_field *observer_field(const type_desc *type, size_t offset, pin_type wanted, int depth)
{
    if (!type || depth == TYPE_MAX_NESTING) {
        return NULL;
    }
    if (type->alias_of) {
        return observer_field(type_catalog_find(type->alias_of), offset, wanted, depth + 1);
    }
    for (int n = 0; n < type->field_count; n++) {
        const type_field *field = &type->fields[n];
        if (field->offset == offset && field->type == wanted) {
            return field;
        }
        if (field->type == PIN_STRUCT && offset >= field->offset &&
            offset - field->offset < field->size) {
            const type_field *nested = observer_field(
                type_catalog_find(field->type_name),
                offset - field->offset,
                wanted,
                depth + 1
            );
            if (nested) {
                return nested;
            }
        }
    }
    return NULL;
}
static int observe_target(
    execution_instance *instance,
    int index,
    uint64_t entity,
    double now,
    double delta,
    unsigned budget,
    uint64_t changed_entity,
    uint64_t component,
    size_t offset,
    const pin_value *edge
)
{
    execution_program *p = instance->program;
    const execution_module_function *function = &p->module->plan.functions[index];
    const execution_field_observer *observer = &function->observer;
    const void *
        row = ecs_component_data(p->world, ecs_entity_from_u64(entity), observer->component);
    if (!row) {
        return FLOW_OK;
    }
    int found = -1;
    for (int i = 0; i < acount(instance->observations); i++) {
        if (instance->observations[i].function == index &&
            instance->observations[i].entity == entity) {
            found = i;
            break;
        }
    }
    bool targeted = edge && entity == changed_entity &&
                    component == observer->component && offset == observer->offset;
    if (edge && !targeted) {
        if (found >= 0) {
            instance->observations[found].seen = true;
        }
        return FLOW_OK;
    }
    arena temporary = { 0 };
    pin_value value = observer->type;
    if (targeted) {
        if (!program_same_type(observer->type, *edge)) {
            return FLOW_ERR_ARGUMENT;
        }
        value = *edge;
    } else {
        const ecs_component_desc
            *component_type = ecs_world_component(p->world, observer->component);
        const type_field *actual = observer_field(
            component_type ? component_type->type : NULL,
            observer->offset,
            value.type,
            0
        );
        if (!actual) {
            return FLOW_ERR_ARGUMENT;
        }
        type_field field = *actual;
        field.offset = observer->offset;
        if (pin_read_field(&temporary, row, &field, &value)) {
            arena_free(&temporary);
            return FLOW_ERR_ARGUMENT;
        }
        value.type_name = observer->type.type_name;
    }
    /* A queued host edge is authoritative, including repeated payloads within
     * one input batch. Sampling alone establishes a silent initial baseline. */
    bool changed = targeted ||
                   (found >= 0 &&
                    !pin_value_equal(&instance->observations[found].value, &value));
    if (found < 0) {
        found = acount(instance->observations);
        apush(instance->observations, ((observer_cache){ .function = index, .entity = entity }));
    }
    observer_cache *cache = &instance->observations[found];
    cache->seen = true;
    if (!cache->value.type || changed) {
        arena next = { 0 };
        pin_value owned;
        /* Comparison needs the previous content, not a live mutable alias. */
        if (pin_value_capture_to_arena(&next, &owned, &value)) {
            arena_free(&next);
            arena_free(&temporary);
            return FLOW_ERR_ARGUMENT;
        }
        arena_free(&cache->storage);
        cache->storage = next;
        cache->value = owned;
    }
    bool system = false;
    for (int i = 0; i < p->module->plan.function_count; i++) {
        system |= p->module->plan.functions[i].kind == EXECUTION_FUNCTION_SYSTEM;
    }
    if (!changed || (system && program_instance_busy(instance))) {
        arena_free(&temporary);
        return FLOW_OK;
    }
    int status = program_dispatch_handler(instance, function, &value, 1, now, delta, budget);
    arena_free(&temporary);
    return status;
}
int program_observe(
    execution_instance *instance,
    double now,
    double delta,
    unsigned budget,
    uint64_t entity,
    uint64_t component,
    size_t offset,
    const pin_value *edge
)
{
    execution_program *p = instance->program;
    for (int i = 0; i < acount(instance->observations); i++) {
        instance->observations[i].seen = false;
    }
    for (int i = 0; i < p->module->plan.function_count; i++) {
        const execution_module_function *function = &p->module->plan.functions[i];
        if (!function->observer.component) {
            continue;
        }
        execution_activation *receiver = program_activation_new(
            instance,
            function->subscription,
            NULL,
            0,
            now,
            delta,
            budget
        );
        if (!receiver) {
            return FLOW_ERR_ARGUMENT;
        }
        int status = program_activation_run(receiver, now, delta, budget);
        if (status == FLOW_OK) {
            pin_value value = receiver->context.results[0];
            if (value.container >= PIN_CONTAINER_ARRAY) {
                const pin_array *array = value.ptr;
                for (int j = 0; array && j < array->count && status == FLOW_OK; j++) {
                    status = observe_target(
                        instance,
                        i,
                        pin_array_get(array, j).u64,
                        now,
                        delta,
                        budget,
                        entity,
                        component,
                        offset,
                        edge
                    );
                }
            } else {
                status = observe_target(instance, i, value.u64, now, delta, budget, entity, component, offset, edge);
            }
        }
        program_activation_free(receiver);
        if (status != FLOW_OK) {
            return status > 0 ? FLOW_ERR_STATE : status;
        }
    }
    for (int i = 0; i < acount(instance->observations);) {
        if (instance->observations[i].seen) {
            i++;
            continue;
        }
        arena_free(&instance->observations[i].storage);
        adel(instance->observations, i);
    }
    return FLOW_OK;
}
static void edge_free(observer_edge *edge)
{
    pin_value_collection_release(&edge->value);
    arena_free(&edge->storage);
}
static queued_event *edge_roots(observer_edge *edges, int count)
{
    queued_event *roots = NULL;
    for (int i = 0; i < count; i++) {
        apush(roots, ((queued_event){ .arguments = &edges[i].value, .count = 1 }));
    }
    return roots;
}
int program_observers_maintain(execution_program *p, double now, double delta, unsigned budget)
{
    observer_edge *edges = p->field_edges;
    p->field_edges = NULL;
    queued_event *roots = edge_roots(edges, acount(edges));
    queued_event_batch batch = { .previous = p->dispatch_events, .events = roots };
    p->dispatch_events = &batch;
    int status = FLOW_OK;
    for (int e = 0; e < acount(edges) && status == FLOW_OK; e++) {
        for (int i = 0; i < acount(p->instances) && status == FLOW_OK; i++) {
            observer_edge *edge = &edges[e];
            status = program_observe(
                p->instances[i],
                now,
                delta,
                budget,
                edge->entity,
                edge->component,
                edge->offset,
                &edge->value
            );
        }
    }
    p->dispatch_events = batch.previous;
    afree(roots);
    for (int e = 0; e < acount(edges); e++) {
        edge_free(&edges[e]);
    }
    afree(edges);
    for (int i = 0; i < acount(p->instances) && status == FLOW_OK; i++) {
        status = program_observe(p->instances[i], now, delta, budget, 0, 0, 0, NULL);
    }
    return status;
}
int execution_program_field_changed(
    execution_program *p,
    uint64_t entity,
    uint64_t component,
    size_t offset,
    pin_value value,
    double now,
    double delta,
    unsigned budget
)
{
    if (program_validate_values(p, &value, 1)) {
        return FLOW_ERR_ARGUMENT;
    }
    pin_value owned;
    if (pin_value_collection_copy(&p->owner_program->managed->storage, &owned, &value)) {
        return FLOW_ERR_ARGUMENT;
    }
    observer_edge edge = { .entity = entity, .component = component, .offset = offset };
    if (pin_value_snapshot_to_arena(&edge.storage, &edge.value, &owned)) {
        pin_value_collection_release(&owned);
        arena_free(&edge.storage);
        return FLOW_ERR_ARGUMENT;
    }
    if (p->dispatching || p->running) {
        apush(p->field_edges, edge);
        return FLOW_OK;
    }
    queued_event *roots = edge_roots(&edge, 1);
    queued_event_batch batch = { .previous = p->dispatch_events, .events = roots };
    p->dispatch_events = &batch;
    p->dispatching = true;
    int status = FLOW_OK;
    for (int i = 0; i < acount(p->instances) && status == FLOW_OK; i++) {
        status = program_observe(
            p->instances[i],
            now,
            delta,
            budget ? budget : 10000,
            entity,
            component,
            offset,
            &edge.value
        );
    }
    p->dispatch_events = batch.previous;
    afree(roots);
    edge_free(&edge);
    p->dispatching = false;
    return status;
}
