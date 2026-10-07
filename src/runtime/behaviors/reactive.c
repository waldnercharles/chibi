#include "runtime/behaviors/reactive.h"

#include "graph/model/reactive.h"
#include "platform/cute.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    behavior_reactive_kind kind;
    char *event;
    behavior_reactive_path source;
    behavior_reactive_path *dependencies;
    behavior_batch *batch;
    unsigned char *before;
} reactive_plan;

typedef struct
{
    reactive_plan *plans;
    int *order;
    uint64_t revision;
    int references;
} reactive_generation;

typedef struct
{
    uint64_t event_id, source;
    bool initialized, pressed;
} reactive_entry;

typedef struct
{
    behavior_instance *instance;
    uint64_t owner, identity, revision;
    reactive_entry *entries;
    reactive_generation *generation;
    bool seen;
} reactive_instance;
typedef struct
{
    behavior_instance *instance;
    game_reactive *manager;
    uint64_t event;
    ecs_component_id component;
    size_t offset, size;
} reactive_writer;

typedef struct
{
    reactive_writer *writers;
    uint64_t epoch;
} reactive_claims;

typedef struct
{
    ecs_world *world;
    reactive_claims *writers;
    uint64_t epoch;
    int references;
    bool collecting;
} reactive_ownership;

/* Runtime managers are created and maintained on their world's owner thread. */
static _Thread_local reactive_ownership **ownerships;

static reactive_ownership *ownership(ecs_world *world)
{
    uint64_t key = (uint64_t)(uintptr_t)world;
    reactive_ownership *current = cf_map_get(ownerships, key);
    if (!current) {
        current = xcalloc(1, sizeof *current);
        current->world = world;
        cf_map_set(ownerships, key, current);
    }
    return current;
}

static void prune_claims(reactive_ownership *ledger)
{
    for (int i = cf_map_size(ledger->writers) - 1; i >= 0; i--) {
        if (ledger->writers[i].epoch != ledger->epoch) {
            afree(ledger->writers[i].writers);
            cf_map_del(ledger->writers, cf_map_keys(ledger->writers)[i]);
        }
    }
}

void game_reactive_world_begin(ecs_world *world)
{
    reactive_ownership *current = cf_map_get(ownerships, (uint64_t)(uintptr_t)world);
    if (!current) {
        return;
    }
    assert(!current->collecting);
    current->collecting = true;
    current->epoch++;
}

void game_reactive_world_end(ecs_world *world)
{
    reactive_ownership *current = cf_map_get(ownerships, (uint64_t)(uintptr_t)world);
    if (!current) {
        return;
    }
    assert(current->collecting);
    current->collecting = false;
    prune_claims(current);
}

struct game_reactive
{
    ecs_world *world;
    reactive_instance *instances;
    int *by_instance;
    reactive_generation **generations;
    reactive_ownership *ownership;
    game_reactive_stats stats;
};

static bool read_path(
    ecs_world *world,
    behavior_instance *instance,
    const behavior_reactive_path *path,
    pin_value *out,
    uint64_t *source,
    void **address
)
{
    if (!path->valid) {
        return false;
    }
    uint64_t owner = path->self ? behavior_instance_owner(instance).u64 : path->owner;
    if (path->variable_id) {
        pin_value value;
        if (behavior_instance_get_variable(instance, path->variable_id, &value) ||
            value.type != PIN_HANDLE) {
            return false;
        }
        owner = value.u64;
    }
    const void *data = NULL;
    for (int i = 0; i < acount(path->steps); i++) {
        ecs_entity entity = ecs_entity_from_u64(owner);
        if (!ecs_entity_is_alive(world, entity) ||
            ecs_entity_is_retiring(world, entity)) {
            return false;
        }
        data = ecs_component_data_const(world, entity, path->steps[i].component);
        if (!data) {
            return false;
        }
        data = (const unsigned char *)data + path->steps[i].offset;
        if (i + 1 < acount(path->steps)) {
            memcpy(&owner, data, sizeof owner);
        }
    }
    if (source) {
        *source = owner;
    }
    if (!data) {
        return false;
    }
    type_field field = path->field;
    field.offset = 0;
    if (out) {
        pin_read_field(data, &field, out);
    }
    if (address) {
        *address = (void *)data;
    }
    return true;
}

static reactive_generation *generation(game_reactive *reactive, behavior_instance *instance)
{
    uint64_t revision = behavior_program_revision(instance->asset->program);
    reactive_generation *current = cf_map_get(reactive->generations, revision);
    if (current) {
        return current;
    }
    current = xcalloc(1, sizeof *current);
    current->revision = revision;
    const behavior_asset *asset = instance->asset;
    for (int i = 0; i < acount(asset->reactives); i++) {
        const behavior_reactive *r = &asset->reactives[i];
        const graph *g = behavior_asset_find_graph(asset, r->graph_id);
        const node *source = g ? graph_node_by_id(g, r->source_node_id) : NULL;
        const node *event = g ? graph_node_by_id(g, r->event_id) : NULL;
        reactive_plan plan = { .kind = r->kind,
                               .event = xstrdup(event ? event->reference_name : "") };
        plan.source.valid = source &&
                            behavior_reactive_path_compile(
                                g,
                                source,
                                behavior_reactive_output_index(source, r->source_pin_id),
                                &plan.source,
                                0
                            );
        if (plan.kind == BEHAVIOR_REACTIVE_BIND && plan.source.valid) {
            plan.before = xmalloc(plan.source.field.size);
        }
        if (r->kind == BEHAVIOR_REACTIVE_BIND) {
            const node *value = g ? graph_node_by_id(g, r->value_node_id ? r->value_node_id : r->statement_node_id)
                                  : NULL;
            if (value) {
                behavior_reactive_dependencies(asset, g, value, &plan.dependencies, 0);
            }
        }
        apush(current->plans, plan);
    }
    cf_map_set(reactive->generations, revision, current);
    behavior_reactive_order(asset, &current->order);
    return current;
}

static void free_generation(reactive_generation *generation)
{
    for (int i = 0; i < acount(generation->plans); i++) {
        reactive_plan *plan = &generation->plans[i];
        free(plan->event);
        free(plan->before);
        afree(plan->source.steps);
        for (int j = 0; j < acount(plan->dependencies); j++) {
            afree(plan->dependencies[j].steps);
        }
        afree(plan->dependencies);
        behavior_batch_free(plan->batch);
    }
    afree(generation->plans);
    afree(generation->order);
    free(generation);
}

static void release(reactive_instance *instance)
{
    if (instance->generation) {
        instance->generation->references--;
    }
    afree(instance->entries);
}

game_reactive *game_reactive_new(ecs_world *world)
{
    game_reactive *reactive = xcalloc(1, sizeof *reactive);
    reactive->world = world;
    reactive->ownership = ownership(world);
    reactive->ownership->references++;
    return reactive;
}

void game_reactive_free(game_reactive *reactive)
{
    if (!reactive) {
        return;
    }
    for (int i = 0; i < acount(reactive->instances); i++) {
        release(&reactive->instances[i]);
    }
    afree(reactive->instances);
    for (int i = 0; i < cf_map_size(reactive->generations); i++) {
        free_generation(reactive->generations[i]);
    }
    cf_map_free(reactive->generations);
    cf_map_free(reactive->by_instance);
    reactive_ownership *ledger = reactive->ownership;
    if (!--ledger->references) {
        assert(!ledger->collecting);
        cf_map_del(ownerships, (uint64_t)(uintptr_t)ledger->world);
        for (int i = 0; i < cf_map_size(ledger->writers); i++) {
            afree(ledger->writers[i].writers);
        }
        cf_map_free(ledger->writers);
        free(ledger);
        if (!cf_map_size(ownerships)) {
            cf_map_free(ownerships);
        }
    }
    free(reactive);
}

void game_reactive_begin(game_reactive *reactive)
{
    reactive->stats = (game_reactive_stats){ 0 };
    if (!reactive->ownership->collecting) {
        reactive->ownership->epoch++;
    }
    for (int i = 0; i < acount(reactive->instances); i++) {
        reactive->instances[i].seen = false;
    }
}

static reactive_instance *registration(game_reactive *reactive, behavior_instance *instance)
{
    uint64_t owner = behavior_instance_owner(instance).u64;
    uint64_t revision = behavior_program_revision(instance->asset->program);
    int index = cf_map_get(reactive->by_instance, (uint64_t)(uintptr_t)instance);
    if (index) {
        reactive_instance *current = &reactive->instances[index - 1];
        if (current->owner != owner || current->identity != instance->asset->identity ||
            current->revision != revision) {
            release(current);
            *current = (reactive_instance){ .instance = instance,
                                            .owner = owner,
                                            .identity = instance->asset->identity,
                                            .revision = revision };
        }
        if (!current->generation) {
            current->generation = generation(reactive, instance);
            current->generation->references++;
        }
        current->seen = true;
        return current;
    }
    apush(
        reactive->instances,
        ((reactive_instance){ .instance = instance,
                              .owner = owner,
                              .identity = instance->asset->identity,
                              .revision = revision,
                              .seen = true })
    );
    cf_map_set(
        reactive->by_instance,
        (uint64_t)(uintptr_t)instance,
        acount(reactive->instances)
    );
    reactive->instances[acount(reactive->instances) - 1].generation = generation(reactive, instance);
    reactive->instances[acount(reactive->instances) - 1].generation->references++;
    return &reactive->instances[acount(reactive->instances) - 1];
}

static int apply(game_reactive *reactive, behavior_instance *instance, flow_error *error, bool notify)
{
    if (!acount(instance->asset->reactives)) {
        return FLOW_OK;
    }
    reactive_instance *registered = registration(reactive, instance);
    int previous_count = acount(registered->entries);
    asetlen(registered->entries, acount(instance->asset->reactives));
    if (acount(registered->entries) > previous_count) {
        memset(
            registered->entries + previous_count,
            0,
            (size_t)(acount(registered->entries) - previous_count) *
                sizeof *registered->entries
        );
    }
    for (int at = 0; at < acount(registered->generation->order); at++) {
        int i = registered->generation->order[at];
        const behavior_reactive *r = &instance->asset->reactives[i];
        reactive_plan *plan = &registered->generation->plans[i];
        reactive_entry *state = &registered->entries[i];
        if (state->event_id != r->event_id) {
            *state = (reactive_entry){ .event_id = r->event_id };
        }
        pin_value value;
        uint64_t entity;
        void *address;
        bool valid = read_path(reactive->world, instance, &plan->source, &value, &entity, &address);
        if (r->kind == BEHAVIOR_REACTIVE_PRESSED_CHANGED) {
            if (!valid) {
                state->initialized = false;
                reactive->stats.dormant++;
                continue;
            }
            bool changed = state->initialized && state->source == entity &&
                           state->pressed != value.b;
            state->source = entity;
            state->pressed = value.b;
            state->initialized = true;
            if (changed && notify &&
                (!instance->activation_user || !acount(instance->activations))) {
                flow_run *run = behavior_instance_dispatch(
                    instance,
                    plan->event,
                    (flow_inputs){ &value, 1, NULL },
                    error
                );
                if (!run) {
                    return error && error->code ? error->code : FLOW_ERR_STATE;
                }
                reactive->stats.events++;
                if (flow_run_status(run) == FLOW_FAILED) {
                    if (error) {
                        *error = *flow_run_error(run);
                    }
                    behavior_instance_release_activation(instance, run);
                    return FLOW_ERR_NODE;
                }
                if (flow_run_status(run) == FLOW_COMPLETED) {
                    behavior_instance_release_activation(instance, run);
                }
            }
            continue;
        }
        bool dependencies = true;
        for (int j = 0; j < acount(plan->dependencies); j++) {
            dependencies &= read_path(reactive->world, instance, &plan->dependencies[j], NULL, NULL, NULL);
        }
        if (valid) {
            reactive_ownership *ledger = reactive->ownership;
            reactive_claims claims = cf_map_get(ledger->writers, entity);
            if (claims.epoch != ledger->epoch) {
                asetlen(claims.writers, 0);
                claims.epoch = ledger->epoch;
            }
            const behavior_reactive_step
                *step = &plan->source.steps[acount(plan->source.steps) - 1];
            bool claimed = false;
            for (int j = 0; j < acount(claims.writers); j++) {
                const reactive_writer *writer = &claims.writers[j];
                if (writer->manager == reactive && writer->instance == instance &&
                    writer->event == r->event_id) {
                    claimed = true;
                    continue;
                }
                if (writer->component == step->component &&
                    writer->offset < step->offset + plan->source.field.size &&
                    step->offset < writer->offset + writer->size) {
                    if (error) {
                        *error = (flow_error){ .code = FLOW_ERR_COMPILE, .message = "Multiple live bindings own overlapping component fields" };
                    }
                    cf_map_set(ledger->writers, entity, claims);
                    return FLOW_ERR_COMPILE;
                }
            }
            if (!claimed) {
                apush(
                    claims.writers,
                    ((reactive_writer){ .instance = instance,
                                        .manager = reactive,
                                        .event = r->event_id,
                                        .component = step->component,
                                        .offset = step->offset,
                                        .size = plan->source.field.size })
                );
            }
            cf_map_set(ledger->writers, entity, claims);
        }
        if (!valid || !dependencies) {
            /* A missing relationship has its type's zero/null value. This is
             * independent of polling cadence and cannot leave a held input on. */
            if (valid) {
                const unsigned char *bytes = address;
                bool nonzero = false;
                for (size_t j = 0; j < plan->source.field.size; j++) {
                    nonzero |= bytes[j] != 0;
                }
                if (nonzero) {
                    memset(address, 0, plan->source.field.size);
                    reactive->stats.writes++;
                }
            }
            reactive->stats.dormant++;
            continue;
        }
        unsigned char *before = plan->before;
        memcpy(before, address, plan->source.field.size);
        flow_run *failed = NULL;
        if (!behavior_instance_maintain_batch(instance, plan->event, &plan->batch, &failed)) {
            if (error) {
                *error = (flow_error){ .code = FLOW_ERR_COMPILE, .message = "Binding requires a synchronous compiled ECS maintenance body" };
            }
            return FLOW_ERR_COMPILE;
        }
        reactive->stats.evaluations++;
        if (failed) {
            if (error) {
                *error = *flow_run_error(failed);
            }
            behavior_instance_release_activation(instance, failed);
            return FLOW_ERR_NODE;
        }
        if (read_path(reactive->world, instance, &plan->source, NULL, NULL, &address) &&
            memcmp(before, address, plan->source.field.size)) {
            reactive->stats.writes++;
        }
    }
    return FLOW_OK;
}

int game_reactive_apply(game_reactive *reactive, behavior_instance *instance, flow_error *error)
{
    return apply(reactive, instance, error, true);
}

int game_reactive_apply_quiet(game_reactive *reactive, behavior_instance *instance, flow_error *error)
{
    return apply(reactive, instance, error, false);
}

int game_reactive_changed(
    game_reactive *reactive,
    behavior_instance *instance,
    ecs_entity entity,
    ecs_component_id component,
    size_t offset,
    pin_value value,
    flow_error *error
)
{
    if (!acount(instance->asset->reactives)) {
        return FLOW_OK;
    }
    reactive_instance *registered = registration(reactive, instance);
    int count = acount(instance->asset->reactives),
        previous = acount(registered->entries);
    asetlen(registered->entries, count);
    if (count > previous) {
        memset(
            registered->entries + previous,
            0,
            (size_t)(count - previous) * sizeof *registered->entries
        );
    }
    for (int i = 0; i < count; i++) {
        reactive_plan *plan = &registered->generation->plans[i];
        int steps = acount(plan->source.steps);
        if (plan->kind != BEHAVIOR_REACTIVE_PRESSED_CHANGED || !steps ||
            plan->source.steps[steps - 1].component != component ||
            plan->source.steps[steps - 1].offset != offset) {
            continue;
        }
        uint64_t source;
        if (!read_path(reactive->world, instance, &plan->source, NULL, &source, NULL) ||
            source != ecs_entity_to_u64(entity)) {
            continue;
        }
        registered->entries[i] = (reactive_entry){
            .event_id = instance->asset->reactives[i].event_id,
            .source = source,
            .initialized = true,
            .pressed = value.b
        };
        if (instance->activation_user && acount(instance->activations)) {
            continue;
        }
        flow_run *run = behavior_instance_dispatch(
            instance,
            plan->event,
            (flow_inputs){ &value, 1, NULL },
            error
        );
        if (!run) {
            return error && error->code ? error->code : FLOW_ERR_STATE;
        }
        reactive->stats.events++;
        if (flow_run_status(run) == FLOW_FAILED) {
            if (error) {
                *error = *flow_run_error(run);
            }
            behavior_instance_release_activation(instance, run);
            return FLOW_ERR_NODE;
        }
        if (flow_run_status(run) == FLOW_COMPLETED) {
            behavior_instance_release_activation(instance, run);
        }
    }
    return FLOW_OK;
}

void game_reactive_end(game_reactive *reactive)
{
    if (!reactive->ownership->collecting) {
        prune_claims(reactive->ownership);
    }
    for (int i = acount(reactive->instances) - 1; i >= 0; i--) {
        if (!reactive->instances[i].seen) {
            cf_map_del(
                reactive->by_instance,
                (uint64_t)(uintptr_t)reactive->instances[i].instance
            );
            release(&reactive->instances[i]);
            int last = acount(reactive->instances) - 1;
            reactive->instances[i] = reactive->instances[last];
            asetlen(reactive->instances, last);
            if (i < last) {
                cf_map_set(
                    reactive->by_instance,
                    (uint64_t)(uintptr_t)reactive->instances[i].instance,
                    i + 1
                );
            }
        }
    }
    for (int i = cf_map_size(reactive->generations) - 1; i >= 0; i--) {
        reactive_generation *generation = reactive->generations[i];
        if (!generation->references) {
            uint64_t revision = generation->revision;
            free_generation(generation);
            cf_map_del(reactive->generations, revision);
        }
    }
}

game_reactive_stats game_reactive_last_update(const game_reactive *reactive)
{
    return reactive->stats;
}
