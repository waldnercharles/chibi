#include "execution/definition.h"
#include "execution/program_internal.h"
#include "execution/status.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/catalog.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static bool depends(const execution_module_binding *after, const execution_module_binding *before)
{
    for (int r = 0; r < after->read_count; r++) {
        for (int w = 0; w < before->write_count; w++) {
            if (after->reads[r] == before->writes[w]) {
                return true;
            }
        }
    }
    return false;
}
bool program_binding_order(execution_program *p)
{
    const execution_module *m = p->module;
    bool *done = xcalloc((size_t)(m->plan.binding_count + 1), sizeof *done);
    for (int n = 0; n < m->plan.binding_count; n++) {
        int ready = -1;
        for (int i = 0; i < m->plan.binding_count && ready < 0; i++) {
            if (done[i]) {
                continue;
            }
            bool blocked = false;
            for (int j = 0; j < m->plan.binding_count; j++) {
                blocked |= i != j && !done[j] &&
                           depends(&m->plan.bindings[i], &m->plan.bindings[j]);
            }
            if (!blocked) {
                ready = i;
            }
        }
        if (ready < 0) {
            free(done);
            return false;
        }
        done[ready] = true;
        apush(p->binding_order, ready);
    }
    free(done);
    return true;
}
int program_binding_run(execution_instance *instance, int binding_index, double now, double delta, unsigned budget)
{
    if (instance->query_managed && !instance->query_ready) {
        return FLOW_OK;
    }
    if (!program_instance_busy(instance)) {
        program_query_consume(instance);
    }
    execution_program *p = instance->program;
    const execution_module_binding *binding = &p->module->plan.bindings[binding_index];
    binding_cache *cache = &instance->bindings[binding_index];
    const ecs_component_desc *component = ecs_world_component(p->world, binding->component);
    if (!component || type_layout_hash(component->type) != binding->layout) {
        return FLOW_ERR_STATE;
    }
    void *row = ecs_component_data(
        p->world,
        ecs_entity_from_u64(instance->owner),
        binding->component
    );
    if (!row) {
        cache->valid = false;
        return FLOW_OK;
    }
    execution_activation *dependencies = NULL;
    if (binding->dependencies) {
        dependencies = program_activation_new(instance, binding->dependencies, NULL, 0, now, delta, budget);
        if (!dependencies) {
            return FLOW_ERR_ARGUMENT;
        }
        int status = program_activation_run(dependencies, now, delta, budget);
        if (status != FLOW_OK) {
            program_activation_free(dependencies);
            cache->valid = false;
            return status > 0 ? FLOW_ERR_STATE : status;
        }
        bool same = cache->valid;
        for (int i = 0; same && i < binding->dependency_count; i++) {
            same = pin_value_equal(&cache->inputs[i], &dependencies->context.results[i]);
        }
        if (same) {
            int written = ecs_component_write_value(
                p->world,
                ecs_entity_from_u64(instance->owner),
                binding->component,
                binding->offset,
                binding->target_type,
                cache->output
            );
            program_activation_free(dependencies);
            return written == ECS_OK ? FLOW_OK : FLOW_ERR_ARGUMENT;
        }
    }
    execution_activation
        *a = program_activation_new(instance, binding->body, NULL, 0, now, delta, budget);
    if (!a) {
        if (dependencies) {
            program_activation_free(dependencies);
        }
        return FLOW_ERR_ARGUMENT;
    }
    int status = program_activation_run(a, now, delta, budget);
    if (status != FLOW_OK) {
        program_activation_free(a);
        if (dependencies) {
            program_activation_free(dependencies);
        }
        cache->valid = false;
        return status > 0 ? FLOW_ERR_STATE : status;
    }
    pin_value value = a->context.results[0];
    if (!program_same_type(binding->target_type, value)) {
        program_activation_free(a);
        if (dependencies) {
            program_activation_free(dependencies);
        }
        cache->valid = false;
        return FLOW_ERR_ARGUMENT;
    }
    size_t width = value.container >= PIN_CONTAINER_ARRAY ? sizeof(pin_array *)
                 : value.type == PIN_STRUCT ? type_catalog_find(value.type_name)->size
                                            : behavior_scalar_size(value.type);
    if (binding->offset > component->element_size ||
        width > component->element_size - binding->offset) {
        program_activation_free(a);
        if (dependencies) {
            program_activation_free(dependencies);
        }
        cache->valid = false;
        return FLOW_ERR_STATE;
    }
    int written = ecs_component_write_value(
        p->world,
        ecs_entity_from_u64(instance->owner),
        binding->component,
        binding->offset,
        binding->target_type,
        value
    );
    if (written == ECS_OK && dependencies) {
        arena next = { 0 };
        pin_value *inputs = xcalloc((size_t)(binding->dependency_count + 1), sizeof *inputs);
        for (int i = 0; i < binding->dependency_count; i++) {
            int status = pin_value_snapshot_to_arena(
                &next,
                &inputs[i],
                &dependencies->context.results[i]
            );
            assert(!status);
            (void)status;
        }
        pin_value output;
        int status = pin_value_snapshot_to_arena(&next, &output, &value);
        assert(!status);
        (void)status;
        free(cache->inputs);
        arena_free(&cache->storage);
        *cache = (binding_cache){ .inputs = inputs,
                                  .output = output,
                                  .storage = next,
                                  .valid = true };
    } else {
        cache->valid = false;
    }
    if (dependencies) {
        program_activation_free(dependencies);
    }
    program_activation_free(a);
    return written == ECS_OK ? FLOW_OK : FLOW_ERR_ARGUMENT;
}
typedef struct
{
    execution_instance *instance;
    int binding;
} binding_work;
static size_t binding_width(const execution_module_binding *b)
{
    return b->target_type.container >= PIN_CONTAINER_ARRAY ? sizeof(pin_array *)
         : b->target_type.type == PIN_STRUCT
             ? type_catalog_find(b->target_type.type_name)->size
             : behavior_scalar_size(b->target_type.type);
}
static bool work_depends(binding_work reader, binding_work writer)
{
    const execution_module_binding *r = &reader.instance->program->module->plan
                                             .bindings[reader.binding];
    const execution_module_binding *w = &writer.instance->program->module->plan
                                             .bindings[writer.binding];
    for (int i = 0; i < r->field_count; i++) {
        execution_field_access f = r->fields[i];
        if ((f.component && f.component != w->component) ||
            (!f.any_entity && reader.instance->owner != writer.instance->owner)) {
            continue;
        }
        size_t width = binding_width(w);
        if (!f.size || (f.offset < w->offset + width && w->offset < f.offset + f.size)) {
            return true;
        }
    }
    return reader.instance == writer.instance && depends(r, w);
}
int execution_program_bindings_all(
    execution_program *const *programs,
    int count,
    double now,
    double delta,
    unsigned budget,
    char *error,
    size_t size
)
{
    binding_work *work = NULL;
    for (int i = 0; i < count; i++) {
        execution_program *p = programs[i];
        assert(p && !p->dispatching && !p->running);
        p->now = now;
        p->delta = delta;
        p->budget = budget ? budget : 10000;
        for (int a = 0; a < acount(p->instances); a++) {
            if (p->instances[a]->query_managed && !p->instances[a]->query_ready) {
                continue;
            }
            for (int b = 0; b < p->module->plan.binding_count; b++) {
                apush(work, ((binding_work){ p->instances[a], b }));
            }
        }
    }
    bool *done = xcalloc((size_t)(acount(work) + 1), sizeof *done);
    int *order = NULL;
    for (int n = 0; n < acount(work); n++) {
        int ready = -1;
        for (int i = 0; i < acount(work) && ready < 0; i++) {
            if (done[i]) {
                continue;
            }
            bool blocked = false;
            for (int j = 0; j < acount(work); j++) {
                blocked |= i != j && !done[j] && work_depends(work[i], work[j]);
            }
            if (!blocked) {
                ready = i;
            }
        }
        if (ready < 0) {
            snprintf(error, size, "reactive bindings contain a cycle across attached component fields");
            afree(order);
            free(done);
            afree(work);
            return FLOW_ERR_COMPILE;
        }
        done[ready] = true;
        apush(order, ready);
    }
    free(done);
    for (int i = 0; i < count; i++) {
        programs[i]->dispatching = true;
    }
    int result = FLOW_OK;
    for (int i = 0; i < acount(order); i++) {
        binding_work item = work[order[i]];
        result = program_binding_run(item.instance, item.binding, now, delta, budget ? budget : 10000);
        if (result) {
            break;
        }
    }
    for (int i = 0; i < count && !result; i++) {
        result = program_observers_maintain(programs[i], now, delta, budget ? budget : 10000);
    }
    for (int i = 0; i < count; i++) {
        programs[i]->dispatching = false;
        program_collect_states(programs[i]);
    }
    afree(order);
    afree(work);
    if (!result && size) {
        error[0] = 0;
    }
    return result;
}
