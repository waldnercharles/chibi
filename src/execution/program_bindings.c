#include "execution/definition.h"
#include "execution/program_internal.h"
#include "execution/status.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/record.h"

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
            /* Replaying the output must not expose the historical cache to
             * later mutations through the component's retained array objects. */
            arena replay = { 0 };
            pin_value output;
            if (pin_value_capture_to_arena(&replay, &output, &cache->output)) {
                arena_free(&replay);
                program_activation_free(dependencies);
                return FLOW_ERR_ARGUMENT;
            }
            int written = ecs_component_write_value(
                p->world,
                ecs_entity_from_u64(instance->owner),
                binding->component,
                binding->offset,
                binding->target_type,
                output
            );
            arena_free(&replay);
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
                 : value.type == PIN_STRUCT ? pin_value_type(&value)->size
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
        /* Cache content at this evaluation, including nested mutable arrays. */
        for (int i = 0; i < binding->dependency_count; i++) {
            int status = pin_value_capture_to_arena(
                &next,
                &inputs[i],
                &dependencies->context.results[i]
            );
            assert(!status);
            (void)status;
        }
        pin_value output;
        int status = pin_value_capture_to_arena(&next, &output, &value);
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
    uint64_t owner;
    int binding;
} binding_work;
/* Retaining a tiny generation key prevents pointer reuse after peer teardown
 * without retaining the peer's module, instances or managed values. Programs
 * have immutable modules; replacement swaps the complete program generation. */
struct binding_identity
{
    unsigned references;
};
struct binding_schedule
{
    struct binding_identity **identities;
    int program_count;
    binding_work *work;
    int *order;
};
enum
{
    BINDING_CACHE_PROGRAMS = 16,
    BINDING_CACHE_ROWS = 1024,
    BINDING_CACHE_BYTES = 64 * 1024,
};
static void identity_free(struct binding_identity *identity)
{
    if (identity && !--identity->references) {
        free(identity);
    }
}
static void schedule_free(execution_program *p)
{
    struct binding_schedule *s = p->binding_schedule;
    if (!s) {
        return;
    }
    for (int i = 0; i < s->program_count; i++) {
        identity_free(s->identities[i]);
    }
    free(s->identities);
    afree(s->work);
    afree(s->order);
    free(s);
    p->binding_schedule = NULL;
}
void program_bindings_free(execution_program *p)
{
    schedule_free(p);
    identity_free(p->binding_identity);
    p->binding_identity = NULL;
}
static size_t schedule_bytes(const struct binding_schedule *s)
{
    if (!s) {
        return 0;
    }
    return sizeof *s +
           (size_t)s->program_count * (sizeof *s->identities + sizeof **s->identities) +
           (s->work ? sizeof(dyna_hdr) + (size_t)acap(s->work) * sizeof *s->work : 0) +
           (s->order ? sizeof(dyna_hdr) + (size_t)acap(s->order) * sizeof *s->order : 0);
}
size_t program_binding_storage_bytes(const execution_program *p)
{
    return schedule_bytes(p->binding_schedule) +
           (p->binding_identity ? sizeof *p->binding_identity : 0);
}
static bool schedule_matches(const struct binding_schedule *s, execution_program *const *programs, int count)
{
    if (!s || count != s->program_count) {
        return false;
    }
    int row = 0;
    for (int i = 0; i < count; i++) {
        execution_program *p = programs[i];
        if (p->binding_identity != s->identities[i]) {
            return false;
        }
        for (int a = 0; a < acount(p->instances); a++) {
            execution_instance *instance = p->instances[a];
            if (instance->query_managed && !instance->query_ready) {
                continue;
            }
            for (int b = 0; b < p->module->plan.binding_count; b++, row++) {
                if (row == acount(s->work) || s->work[row].instance != instance ||
                    s->work[row].owner != instance->owner ||
                    s->work[row].binding != b) {
                    return false;
                }
            }
        }
    }
    /* No cached instance is dereferenced before the whole current stream has
     * matched. Recycled instances with the same owner/module have the same
     * conservative field topology; execution uses that current live address. */
    return row == acount(s->work);
}
static bool schedule_save(execution_program *const *programs, int count, binding_work *work, int *order)
{
    if (count <= 0 || count > BINDING_CACHE_PROGRAMS ||
        acount(work) > BINDING_CACHE_ROWS) {
        return false;
    }
    struct binding_schedule candidate = {
        .program_count = count,
        .work = work,
        .order = order,
    };
    if (schedule_bytes(&candidate) + sizeof(struct binding_identity) >
        BINDING_CACHE_BYTES) {
        return false;
    }
    struct binding_schedule *s = xmalloc(sizeof *s);
    *s = candidate;
    s->identities = xmalloc((size_t)count * sizeof *s->identities);
    for (int i = 0; i < count; i++) {
        execution_program *p = programs[i];
        if (!p->binding_identity) {
            p->binding_identity = xmalloc(sizeof *p->binding_identity);
            p->binding_identity->references = 1;
        }
        s->identities[i] = p->binding_identity;
        s->identities[i]->references++;
    }
    programs[0]->binding_schedule = s;
    return true;
}
static size_t binding_width(const execution_module_binding *b)
{
    return b->target_type.container >= PIN_CONTAINER_ARRAY ? sizeof(pin_array *)
         : b->target_type.type == PIN_STRUCT
             ? pin_value_type(&b->target_type)->size
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
    for (int i = 0; i < count; i++) {
        assert(programs[i]);
        if (programs[i]->system_prepared && programs[i]->module->effects.reduce_count) {
            return FLOW_PAUSED_RESULT;
        }
    }
    for (int i = 0; i < count; i++) {
        execution_program *p = programs[i];
        assert(p && !p->dispatching && !p->running);
        p->now = now;
        p->delta = delta;
        p->budget = budget ? budget : 10000;
    }
    execution_program *owner = count > 0 ? programs[0] : NULL;
    bool cached = owner && schedule_matches(owner->binding_schedule, programs, count);
    binding_work *work = cached ? owner->binding_schedule->work : NULL;
    int *order = cached ? owner->binding_schedule->order : NULL;
    if (!cached && owner) {
        schedule_free(owner);
    }
    for (int i = 0; !cached && i < count; i++) {
        execution_program *p = programs[i];
        for (int a = 0; a < acount(p->instances); a++) {
            if (p->instances[a]->query_managed && !p->instances[a]->query_ready) {
                continue;
            }
            for (int b = 0; b < p->module->plan.binding_count; b++) {
                apush(work, ((binding_work){ p->instances[a], p->instances[a]->owner, b }));
            }
        }
    }
    bool *done = cached ? NULL : xcalloc((size_t)(acount(work) + 1), sizeof *done);
    for (int n = 0; !cached && n < acount(work); n++) {
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
    if (!cached) {
        cached = schedule_save(programs, count, work, order);
    }
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
    if (!cached) {
        afree(order);
        afree(work);
    } else if (result) {
        schedule_free(owner);
    }
    if (!result && size) {
        error[0] = 0;
    }
    return result;
}
