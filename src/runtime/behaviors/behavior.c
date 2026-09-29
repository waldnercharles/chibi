#include "runtime/behaviors/behavior.h"

#include "diagnostics/tracy.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/random.h"
#include "graph/runtime/update.h"
#include "runtime/behaviors/entities.h"
#include "runtime/behaviors/parallel.h"
#include "runtime/behaviors/pending.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GAME_BEHAVIOR_LOG_MAX 256

typedef struct
{
    char *behavior;
    char *event;
    _Atomic uint64_t inclusive_nanoseconds;
    _Atomic uint64_t nanoseconds, setup_nanoseconds, dispatches;
    _Atomic uint64_t starts;
    _Atomic uint64_t resumes;
} behavior_profile_record;

typedef struct
{
    char *name;
    behavior_asset *definition; /* borrowed by the behavior world */
    behavior_instance **pool;
    behavior_instance_bank *bank;
    behavior_batch *batch;
    int attachment_count;
} behavior_asset_record;

typedef struct
{
    game_behavior_world *world;
    ecs_entity entity;
    const char *asset_name;
    behavior_profile_record **profile;
    int attachment;
} behavior_sink_context;

typedef struct
{
    ecs_entity entity;
    behavior_asset_record *asset;
    behavior_instance *instance;
    uint64_t sequence;
    int debug_hold;
    int begin_state; /* 0 pending, 1 started, -1 failed. */
} behavior_attachment;

struct game_behavior_world
{
    ecs_world *ecs;
    game_save *save;
    game_audio *audio;
    game_ui *ui;
    game_run run;
    eval_component_reader component_reader;
    struct game_entities *entities;
    struct game_particles *particles;
    struct game_visibility *visibility;
    behavior_asset_record *assets;
    behavior_attachment *attachments;
    int **attachments_by_slot; /* Ordered indices into the broadcast array. */
    uint32_t attachment_slot_count;
    behavior_update_row *updates;
    behavior_update_binding **update_bindings;
    const behavior_function *update_query_scope;
    bool update_bindings_invalidated;
    int update_dirty_begin;
    uint64_t attachment_revision, update_revision;
    void *update_code;
    behavior_update_span *update_spans;
    behavior_update_kernel **update_kernels;
    bool update_native_disabled, update_parallel_disabled;
    game_behavior_parallel *update_parallel;
    game_behavior_update_stats update_stats;
    game_behavior_log_info *logs;
    char **retired_names; /* renamed assets' names, which logs may borrow */
    flow_parallel_for_fn parallel_for;
    void *parallel_user;
    flow_sink_fn sink;
    void *sink_user;
    bool defer_begin;
    bool batch_disabled;
    int *pump_results;
    int pump_result_capacity;
    behavior_pending pending;
    int *pumped;
    int last_pump_count;
    uint64_t next_sequence;
    behavior_clock clock;
    behavior_random random;
    behavior_profile_record profile[GAME_BEHAVIOR_PROFILE_MAX];
    int profile_count;
    bool profile_disabled;
    behavior_profile_record profile_overflow;
    game_behavior_profile_frame profile_view;
};

static void destroy_asset_pool(behavior_asset_record *asset);

static void clear_update_bindings(game_behavior_world *world)
{
    /* Both the shared bindings and per-plan copies borrow immutable programs
     * and service values. Invalidate plans before borrowed generations can be
     * released; the next schedule boundary frees their copies safely. */
    game_behavior_parallel_invalidate(world->update_parallel);
    behavior_update_free_bindings(world->update_bindings);
    world->update_bindings = NULL;
    world->update_query_scope = NULL;
    world->update_bindings_invalidated = false;
}

static void mark_attachment_change(game_behavior_world *world, int dirty_begin)
{
    assert(dirty_begin >= 0);
    if (world->attachment_revision == world->update_revision) {
        world->update_dirty_begin = dirty_begin;
    } else if (dirty_begin < world->update_dirty_begin) {
        world->update_dirty_begin = dirty_begin;
    }
    world->attachment_revision++;
}

static behavior_profile_record *profile_record(
    game_behavior_world *world,
    const char *behavior,
    const char *event
)
{
    for (int i = 0; i < world->profile_count; i++) {
        behavior_profile_record *record = &world->profile[i];
        if (!strcmp(record->behavior, behavior) && !strcmp(record->event, event)) {
            return record;
        }
    }
    if (world->profile_count == GAME_BEHAVIOR_PROFILE_MAX) {
        return &world->profile_overflow;
    }
    behavior_profile_record *record = &world->profile[world->profile_count++];
    record->behavior = xstrdup(behavior);
    record->event = xstrdup(event);
    return record;
}

static void profile_instance(behavior_instance *instance)
{
    behavior_sink_context *context = instance->sink_user;
    aclear(context->profile);
    graph *graph = instance->asset->graph;
    for (int i = 0; i < acount(graph->nodes); i++) {
        const node *entry = graph->nodes[i];
        if (entry->desc->flow_kind == NODE_FLOW_EVENT && entry->reference_name) {
            apush(
                context->profile,
                profile_record(context->world, context->asset_name, entry->reference_name)
            );
        }
    }
}

static void record_profile(
    const char *event,
    uint64_t nanoseconds,
    uint64_t inclusive_nanoseconds,
    behavior_profile_phase phase,
    void *user
)
{
    behavior_sink_context *context = user;
    behavior_profile_record *record = &context->world->profile_overflow;
    for (int i = 0; i < acount(context->profile); i++) {
        behavior_profile_record *candidate = context->profile[i];
        if (candidate->event && event && !strcmp(candidate->event, event)) {
            record = candidate;
            break;
        }
    }
    if (phase == BEHAVIOR_PROFILE_SETUP) {
        atomic_fetch_add_explicit(&record->setup_nanoseconds, nanoseconds, memory_order_relaxed);
        atomic_fetch_add_explicit(&record->dispatches, 1, memory_order_relaxed);
        return;
    }
    atomic_fetch_add_explicit(&record->inclusive_nanoseconds, inclusive_nanoseconds, memory_order_relaxed);
    atomic_fetch_add_explicit(&record->nanoseconds, nanoseconds, memory_order_relaxed);
    atomic_fetch_add_explicit(
        phase == BEHAVIOR_PROFILE_START ? &record->starts : &record->resumes,
        1,
        memory_order_relaxed
    );
}

void game_behavior_set_profiling(game_behavior_world *world, bool enabled)
{
    if (world->profile_disabled == !enabled) {
        return;
    }
    world->profile_disabled = !enabled;
    for (int i = 0; i < acount(world->attachments); i++) {
        world->attachments[i].instance->profile = enabled ? record_profile : NULL;
    }
    game_behavior_profile_begin(world);
}

void game_behavior_profile_begin(game_behavior_world *world)
{
    for (int i = 0; i <= world->profile_count; i++) {
        behavior_profile_record *record = i < world->profile_count
                                            ? &world->profile[i]
                                            : &world->profile_overflow;
        atomic_store_explicit(&record->setup_nanoseconds, 0, memory_order_relaxed);
        atomic_store_explicit(&record->dispatches, 0, memory_order_relaxed);
        atomic_store_explicit(&record->nanoseconds, 0, memory_order_relaxed);
        atomic_store_explicit(&record->inclusive_nanoseconds, 0, memory_order_relaxed);
        atomic_store_explicit(&record->starts, 0, memory_order_relaxed);
        atomic_store_explicit(&record->resumes, 0, memory_order_relaxed);
    }
}

const game_behavior_profile_frame *game_behavior_profile(const game_behavior_world *world)
{
    game_behavior_profile_frame *view = &((game_behavior_world *)world)->profile_view;
    view->count = 0;
    view->cpu_ms = 0;
    view->inclusive_cpu_ms = 0;
    view->setup_ms = 0;
    view->overflow = (game_behavior_profile_row){ .behavior = "Other behaviors",
                                                  .event = "Profile capacity" };
    for (int i = 0; i <= world->profile_count; i++) {
        const behavior_profile_record *record = i < world->profile_count
                                                  ? &world->profile[i]
                                                  : &world->profile_overflow;
        game_behavior_profile_row row = {
            .behavior = record->behavior,
            .event = record->event,
            .dispatches = atomic_load_explicit(&record->dispatches, memory_order_relaxed),
            .setup_ms = atomic_load_explicit(&record->setup_nanoseconds, memory_order_relaxed) /
                        1000000.0,
            .starts = atomic_load_explicit(&record->starts, memory_order_relaxed),
            .resumes = atomic_load_explicit(&record->resumes, memory_order_relaxed),
            .inclusive_cpu_ms = atomic_load_explicit(&record->inclusive_nanoseconds, memory_order_relaxed) /
                                1000000.0,
            .cpu_ms = atomic_load_explicit(&record->nanoseconds, memory_order_relaxed) /
                      1000000.0,
        };
        view->inclusive_cpu_ms += row.inclusive_cpu_ms;
        view->cpu_ms += row.cpu_ms;
        view->setup_ms += row.setup_ms;
        if (i == world->profile_count) {
            view->overflow.dispatches = row.dispatches;
            view->overflow.setup_ms = row.setup_ms;
            view->overflow.starts = row.starts;
            view->overflow.resumes = row.resumes;
            view->overflow.inclusive_cpu_ms = row.inclusive_cpu_ms;
            view->overflow.cpu_ms = row.cpu_ms;
        } else if (row.dispatches || row.starts || row.resumes) {
            view->rows[view->count++] = row;
        }
    }
    return view;
}

static behavior_asset_record *find_asset(game_behavior_world *world, const char *name)
{
    for (int i = 0; i < acount(world->assets); i++) {
        if (strcmp(world->assets[i].name, name) == 0) {
            return &world->assets[i];
        }
    }
    return NULL;
}

static void add_log(game_behavior_world *world, game_behavior_log_info entry)
{
    if (acount(world->logs) == GAME_BEHAVIOR_LOG_MAX) {
        adel(world->logs, 0);
    }
    apush(world->logs, entry);
}

/* A failed run stops its event where it failed; the log says where and why,
   as Unreal's message log reports a runtime error. */
static void log_failure(
    game_behavior_world *world,
    ecs_entity entity,
    const char *asset_name,
    const flow_error *error
)
{
    game_behavior_log_info entry = { .entity = entity,
                                     .asset_name = asset_name,
                                     .origin = error ? error->node : NULL,
                                     .failure = true };
    snprintf(
        entry.message,
        sizeof entry.message,
        "%s",
        error && error->message ? error->message : "run failed"
    );
    add_log(world, entry);
}

static void behavior_sink(const node *origin, const char *message, void *user)
{
    behavior_sink_context *context = (behavior_sink_context *)user;
    game_behavior_world *world = context->world;
    game_behavior_log_info entry = {
        .entity = context->entity,
        .asset_name = context->asset_name,
        .origin = origin,
    };
    snprintf(entry.message, sizeof entry.message, "%s", message ? message : "");
    add_log(world, entry);
    if (world->sink) {
        world->sink(origin, message, world->sink_user);
    }
}

static const int *entity_attachments(const game_behavior_world *world, ecs_entity entity)
{
    return entity.slot < world->attachment_slot_count
             ? world->attachments_by_slot[entity.slot]
             : NULL;
}

static void index_attachment(game_behavior_world *world, int index)
{
    world->attachments[index].asset->attachment_count++;
    uint32_t slot = world->attachments[index].entity.slot;
    if (slot >= world->attachment_slot_count) {
        uint32_t old = world->attachment_slot_count;
        uint32_t count = ecs_world_entity_capacity(world->ecs) + 1;
        assert(slot < count);
        world->attachments_by_slot = xrealloc(
            world->attachments_by_slot,
            (size_t)count * sizeof *world->attachments_by_slot
        );
        memset(
            world->attachments_by_slot + old,
            0,
            (size_t)(count - old) * sizeof *world->attachments_by_slot
        );
        world->attachment_slot_count = count;
    }
    apush(world->attachments_by_slot[slot], index);
    behavior_instance *instance = world->attachments[index].instance;
    behavior_sink_context *context = instance->sink_user;
    context->attachment = index;
    behavior_pending_reserve(&world->pending, index + 1);
    if (acount(instance->activations)) {
        behavior_pending_set(&world->pending, index, true);
    }
}

static void rebuild_attachment_index(game_behavior_world *world, int dirty_begin)
{
    mark_attachment_change(world, dirty_begin);
    behavior_pending_reset(&world->pending);
    for (int i = 0; i < acount(world->assets); i++) {
        world->assets[i].attachment_count = 0;
    }
    for (uint32_t slot = 0; slot < world->attachment_slot_count; slot++) {
        aclear(world->attachments_by_slot[slot]);
    }
    for (int i = 0; i < acount(world->attachments); i++) {
        index_attachment(world, i);
    }
}

static void append_attachment(game_behavior_world *world, behavior_attachment attachment)
{
    int index = acount(world->attachments);
    apush(world->attachments, attachment);
    mark_attachment_change(world, index);
    index_attachment(world, index);
}

static behavior_attachment *find_attachment(
    game_behavior_world *world,
    ecs_entity entity,
    const char *asset_name
)
{
    behavior_asset_record *asset = find_asset(world, asset_name);
    if (!asset) {
        return NULL;
    }
    const int *indices = entity_attachments(world, entity);
    for (int i = 0; i < acount(indices); i++) {
        behavior_attachment *attachment = &world->attachments[indices[i]];
        if (attachment->asset == asset && attachment->entity.slot == entity.slot &&
            attachment->entity.generation == entity.generation) {
            return attachment;
        }
    }
    return NULL;
}

static void activations_changed(behavior_instance *instance)
{
    behavior_sink_context *context = instance->sink_user;
    assert(context->attachment >= 0);
    behavior_pending_set(
        &context->world->pending,
        context->attachment,
        acount(instance->activations) != 0
    );
}
static void services_changed(behavior_instance *instance)
{
    behavior_sink_context *context = instance->sink_user;
    mark_attachment_change(context->world, 0);
    context->world->update_bindings_invalidated = true;
}

static behavior_instance *acquire_instance(
    behavior_asset_record *asset,
    game_behavior_world *world,
    ecs_entity entity
)
{
    pin_value owner = {
        .type = PIN_HANDLE,
        .u64 = ecs_entity_to_u64(entity),
        .type_name = "ecs.entity",
    };
    behavior_instance *instance;
    if (acount(asset->pool)) {
        instance = behavior_instance_pool_reuse(apop(asset->pool), asset->definition, &owner, NULL, NULL);
    } else {
        instance = behavior_instance_create_in_bank(&asset->bank, asset->definition, &owner);
    }
    if (!instance) {
        return NULL;
    }
    behavior_instance_set_trace_enabled(instance, 0);
    behavior_instance_bind_clock(instance, &world->clock);
    if (behavior_instance_set_activation_pool(instance, 1) != 0 ||
        behavior_instance_bind_service(instance, "ecs.world", world->ecs) != 0 ||
        behavior_instance_bind_service(
            instance,
            EVAL_COMPONENT_READER_SERVICE,
            &world->component_reader
        ) != 0) {
        behavior_instance_destroy(instance);
        return NULL;
    }
    if (world->entities) {
        behavior_instance_bind_service(instance, "game.entities", world->entities);
    }
    if (world->particles) {
        behavior_instance_bind_service(instance, "game.particles", world->particles);
    }
    behavior_instance_bind_service(instance, "game.save", world->save);
    behavior_instance_bind_service(instance, "game.audio", world->audio);
    behavior_instance_bind_service(instance, "game.ui", world->ui);
    behavior_instance_bind_service(instance, "game.visibility", world->visibility);
    behavior_instance_bind_service(instance, "game.run", &world->run);
    behavior_sink_context *context = (behavior_sink_context *)xcalloc(1, sizeof *context);
    context->world = world;
    context->attachment = -1;
    context->entity = entity;
    context->asset_name = asset->name;
    instance->sink = behavior_sink;
    instance->sink_user = context;
    instance->services_changed = services_changed;
    instance->activations_changed = activations_changed;
    instance->profile = world->profile_disabled ? NULL : record_profile;
    instance->profile_user = context;
    profile_instance(instance);
    return instance;
}

static void release_instance(behavior_asset_record *asset, behavior_instance *instance)
{
    instance->services_changed = NULL;
    instance->activations_changed = NULL;
    behavior_sink_context *context = instance->sink_user;
    if (context->attachment >= 0) {
        behavior_pending_set(&context->world->pending, context->attachment, false);
    }
    afree(context->profile);
    free(instance->sink_user);
    instance->sink = NULL;
    instance->sink_user = NULL;
    if (behavior_instance_pool_release(instance) == 0) {
        apush(asset->pool, instance);
        return;
    }
    behavior_instance_destroy(instance);
}

/* Releases finished runs. A run that failed after it waited, or after the
   debugger resumed it, is logged here as dispatch logs one that fails at once. */
static void reap_instance(game_behavior_world *world, const behavior_attachment *attachment)
{
    behavior_instance *instance = attachment->instance;
    for (int i = 0; i < acount(instance->activations);) {
        flow_run *run = instance->activations[i];
        flow_state state = flow_run_status(run);
        if (state != FLOW_COMPLETED && state != FLOW_FAILED) {
            i++;
            continue;
        }
        if (state == FLOW_FAILED) {
            log_failure(world, attachment->entity, attachment->asset->name, flow_run_error(run));
        }
        behavior_instance_release_activation(instance, run);
    }
}

game_behavior_world *game_behavior_world_new(ecs_world *world)
{
    if (!world) {
        return NULL;
    }
    game_behavior_world *result = (game_behavior_world *)xcalloc(1, sizeof *result);
    result->ecs = world;
    result->save = game_save_new();
    result->audio = game_audio_new();
    result->ui = game_ui_new();
    result->update_parallel = game_behavior_parallel_new();
    result->profile_disabled = true;
    ecs_eval_component_reader_init(&result->component_reader, world);
    result->next_sequence = 1;
    behavior_random_seed(&result->random, 0);
    return result;
}

void game_behavior_seed_random(game_behavior_world *world, uint64_t seed)
{
    behavior_random_seed(&world->random, seed);
}

/* Graphs this world runs draw from its own stream. */
static void use_random(game_behavior_world *world)
{
    behavior_random_use(&world->random);
}

game_audio *game_behavior_audio(game_behavior_world *world)
{
    return world->audio;
}

game_ui *game_behavior_ui(game_behavior_world *world)
{
    return world->ui;
}

game_run *game_behavior_run(game_behavior_world *world)
{
    return &world->run;
}

game_save *game_behavior_save(game_behavior_world *world)
{
    return world->save;
}

void game_behavior_bind_entities(game_behavior_world *world, struct game_entities *entities)
{
    world->entities = entities;
    for (int i = 0; i < acount(world->attachments); i++) {
        behavior_instance_bind_service(world->attachments[i].instance, "game.entities", entities);
    }
}

void game_behavior_bind_particles(game_behavior_world *world, struct game_particles *particles)
{
    world->particles = particles;
    for (int i = 0; i < acount(world->attachments); i++) {
        behavior_instance_bind_service(world->attachments[i].instance, "game.particles", particles);
    }
}

void game_behavior_bind_visibility(game_behavior_world *world, struct game_visibility *visibility)
{
    world->visibility = visibility;
    for (int i = 0; i < acount(world->attachments); i++) {
        behavior_instance_bind_service(world->attachments[i].instance, "game.visibility", visibility);
    }
}

static bool asset_requires_serial(const behavior_asset *asset)
{
    for (int i = 0; i < acount(asset->graphs); i++) {
        for (int j = 0; j < acount(asset->graphs[i]->nodes); j++) {
            const node_access *access = asset->graphs[i]->nodes[j]->desc->access;
            if (access &&
                ((access->flags & (NODE_EFFECT_STRUCTURAL | NODE_EFFECT_WORLD_READ |
                                   NODE_EFFECT_HOST_SINK)) ||
                 (access->write_count && !(access->flags & NODE_EFFECT_ENTITY_SCOPED)))) {
                return true;
            }
        }
    }
    return false;
}

void game_behavior_world_free(game_behavior_world *world)
{
    if (!world) {
        return;
    }
    behavior_random_use(NULL);
    behavior_update_free_code(world->update_code);
    clear_update_bindings(world);
    game_behavior_parallel_free(world->update_parallel);
    afree(world->updates);
    afree(world->update_spans);
    behavior_update_free_kernels(world->update_kernels);
    while (acount(world->attachments)) {
        behavior_attachment *attachment = &alast(world->attachments);
        release_instance(attachment->asset, attachment->instance);
        adel(world->attachments, acount(world->attachments) - 1);
    }
    for (int i = 0; i < acount(world->assets); i++) {
        behavior_asset_record *asset = &world->assets[i];
        destroy_asset_pool(asset);
        behavior_instance_bank_free(asset->bank);
        free(asset->name);
    }
    afree(world->assets);
    for (int i = 0; i < acount(world->retired_names); i++) {
        free(world->retired_names[i]);
    }
    afree(world->retired_names);
    for (uint32_t slot = 0; slot < world->attachment_slot_count; slot++) {
        afree(world->attachments_by_slot[slot]);
    }
    free(world->attachments_by_slot);
    afree(world->attachments);
    free(world->pending.words);
    afree(world->pumped);
    afree(world->logs);
    for (int i = 0; i < world->profile_count; i++) {
        free(world->profile[i].behavior);
        free(world->profile[i].event);
    }
    free(world->pump_results);
    game_save_free(world->save);
    game_audio_free(world->audio);
    game_ui_free(world->ui);
    free(world);
}

int game_behavior_register_asset(game_behavior_world *world, const char *asset_name, behavior_asset *definition)
{
    if (!world || !asset_name || !asset_name[0] || !definition ||
        find_asset(world, asset_name)) {
        return -1;
    }
    behavior_program_publish(definition);
    apush(
        world->assets,
        (behavior_asset_record){
            .name = xstrdup(asset_name),
            .definition = definition,
        }
    );
    return 0;
}

int game_behavior_unregister_asset(
    game_behavior_world *world,
    const char *asset_name,
    behavior_asset **definition
)
{
    if (!world || !asset_name || !asset_name[0] || !definition) {
        return -1;
    }
    for (int i = 0; i < acount(world->assets); i++) {
        behavior_asset_record *asset = &world->assets[i];
        if (strcmp(asset->name, asset_name) != 0) {
            continue;
        }
        for (int j = 0; j < acount(world->attachments); j++) {
            if (world->attachments[j].asset == asset) {
                return -1;
            }
        }
        *definition = asset->definition;
        clear_update_bindings(world);
        mark_attachment_change(world, 0);
        free(asset->name);
        destroy_asset_pool(asset);
        behavior_instance_bank_free(asset->bank);
        adel(world->assets, i);
        return 0;
    }
    return -1;
}

static int detach_asset_entity(game_behavior_world *world, behavior_asset_record *asset, ecs_entity entity);

static int begin_instance(game_behavior_world *world, behavior_attachment *attachment)
{
    if (attachment->begin_state) {
        return attachment->begin_state < 0 ? -1 : 0;
    }
    if (world->defer_begin) {
        return 0;
    }
    use_random(world);
    /* Mark first: Begin may enqueue work or grow the attachment array. */
    attachment->begin_state = 1;
    behavior_instance *instance = attachment->instance;
    uint64_t sequence = attachment->sequence;
    flow_error error = { 0 };
    flow_run *begin = behavior_instance_dispatch(instance, "Begin", (flow_inputs){ 0 }, &error);
    bool failed = begin ? flow_run_status(begin) == FLOW_FAILED
                        : error.code != FLOW_ERR_UNKNOWN_EVENT;
    if (!failed) {
        return 0;
    }
    log_failure(world, attachment->entity, attachment->asset->name, begin ? flow_run_error(begin) : &error);
    for (int i = 0; i < acount(world->attachments); i++) {
        if (world->attachments[i].sequence == sequence) {
            world->attachments[i].begin_state = -1;
        }
    }
    return -1;
}

void game_behavior_set_deferred(game_behavior_world *world, bool deferred)
{
    assert(world);
    world->defer_begin = deferred;
}

int game_behavior_begin_pending(game_behavior_world *world)
{
    if (!world || world->defer_begin) {
        return -1;
    }
    typedef struct
    {
        ecs_entity entity;
        uint64_t sequence;
    } pending_begin;
    pending_begin *pending = NULL;
    for (int i = 0; i < acount(world->attachments); i++) {
        if (world->attachments[i].begin_state < 0) {
            return -1;
        }
    }
    for (int i = 0; i < acount(world->attachments); i++) {
        if (!world->attachments[i].begin_state) {
            apush(
                pending,
                ((pending_begin){ world->attachments[i].entity,
                                  world->attachments[i].sequence })
            );
        }
    }
    for (int i = 0; i < acount(pending); i++) {
        const int *indices = entity_attachments(world, pending[i].entity);
        for (int j = 0; j < acount(indices); j++) {
            behavior_attachment *attachment = &world->attachments[indices[j]];
            if (attachment->sequence != pending[i].sequence) {
                continue;
            }
            if (begin_instance(world, attachment) != 0) {
                afree(pending);
                return -1;
            }
            break;
        }
    }
    afree(pending);
    return 0;
}

int game_behavior_rename_asset(game_behavior_world *world, const char *from, const char *to)
{
    behavior_asset_record *asset = find_asset(world, from);
    if (!asset || !to[0] || find_asset(world, to)) {
        return -1;
    }
    apush(world->retired_names, asset->name);
    asset->name = xstrdup(to);
    for (int i = 0; i < acount(world->attachments); i++) {
        behavior_attachment *attachment = &world->attachments[i];
        if (attachment->asset == asset && attachment->instance->sink_user) {
            ((behavior_sink_context *)attachment->instance->sink_user)
                ->asset_name = asset->name;
        }
    }
    return 0;
}

static void destroy_asset_pool(behavior_asset_record *asset)
{
    for (int i = 0; i < acount(asset->pool); i++) {
        behavior_instance_destroy(asset->pool[i]);
    }
    afree(asset->pool);
    behavior_batch_free(asset->batch);
    asset->batch = NULL;
}

int game_behavior_replace_asset(
    game_behavior_world *world,
    const char *asset_name,
    behavior_asset *definition,
    behavior_asset **old_definition
)
{
    behavior_asset_record *asset = world && asset_name ? find_asset(world, asset_name)
                                                       : NULL;
    if (!asset || !definition || asset->definition == definition) {
        return -1;
    }
    if (behavior_program_diagnostic(behavior_program_publish(definition))) {
        return -1;
    }
    clear_update_bindings(world);
    ecs_entity *entities = NULL;
    for (int i = 0; i < acount(world->attachments); i++) {
        if (world->attachments[i].asset == asset) {
            apush(entities, world->attachments[i].entity);
        }
    }
    for (int i = acount(world->attachments) - 1; i >= 0; i--) {
        if (world->attachments[i].asset != asset) {
            continue;
        }
        release_instance(asset, world->attachments[i].instance);
        adel(world->attachments, i);
    }
    rebuild_attachment_index(world, 0);
    destroy_asset_pool(asset);
    behavior_asset *old = asset->definition;
    asset->definition = definition;
    for (int i = 0; i < acount(entities); i++) {
        if (game_behavior_attach(world, entities[i], asset_name) != 0) {
            detach_asset_entity(world, asset, entities[i]);
            destroy_asset_pool(asset);
            asset->definition = old;
            for (int j = 0; j < acount(entities); j++) {
                game_behavior_attach(world, entities[j], asset_name);
            }
            afree(entities);
            return -1;
        }
    }
    afree(entities);
    if (old_definition) {
        *old_definition = old;
    }
    return 0;
}

int game_behavior_can_preview_asset(game_behavior_world *world, const char *asset_name, behavior_asset *definition)
{
    behavior_asset_record *asset = find_asset(world, asset_name);
    assert(asset && definition);
    if (!definition->owner_type_name) {
        behavior_asset_set_owner_type(definition, "ecs.entity");
    }
    int status = INSTANCE_UPDATE_READY;
    for (int i = 0; i < acount(world->attachments); i++) {
        behavior_attachment *attachment = &world->attachments[i];
        if (attachment->asset != asset) {
            continue;
        }
        int current = behavior_instance_can_update(attachment->instance, definition);
        if (current > status) {
            status = current;
        }
    }
    return status;
}

int game_behavior_preview_asset(
    game_behavior_world *world,
    const char *asset_name,
    behavior_asset *definition,
    behavior_asset **old_definition
)
{
    behavior_asset_record *asset = find_asset(world, asset_name);
    int status = game_behavior_can_preview_asset(world, asset_name, definition);
    if (status != INSTANCE_UPDATE_READY) {
        return status;
    }
    if (behavior_program_diagnostic(behavior_program_publish(definition))) {
        return -1;
    }
    clear_update_bindings(world);
    destroy_asset_pool(asset);
    for (int i = 0; i < acount(world->attachments); i++) {
        behavior_attachment *attachment = &world->attachments[i];
        if (attachment->asset == asset) {
            behavior_instance_update(attachment->instance, definition);
            profile_instance(attachment->instance);
        }
    }
    *old_definition = asset->definition;
    asset->definition = definition;
    mark_attachment_change(world, 0);
    return INSTANCE_UPDATE_READY;
}

int game_behavior_attach(game_behavior_world *world, ecs_entity entity, const char *asset_name)
{
    if (!world || !ecs_entity_is_alive(world->ecs, entity) ||
        find_attachment(world, entity, asset_name)) {
        return -1;
    }
    behavior_asset_record *asset = find_asset(world, asset_name);
    if (!asset) {
        return -1;
    }
    if (!asset->definition->owner_type_name) {
        if (behavior_asset_set_owner_type(asset->definition, "ecs.entity") != 0) {
            return -1;
        }
    } else if (strcmp(asset->definition->owner_type_name, "ecs.entity") != 0) {
        return -1;
    }
    behavior_instance *instance = acquire_instance(asset, world, entity);
    if (!instance) {
        return -1;
    }
    append_attachment(
        world,
        (behavior_attachment){
            .entity = entity,
            .asset = asset,
            .instance = instance,
            .sequence = world->next_sequence++,
        }
    );
    behavior_attachment *attachment = &alast(world->attachments);
    if (begin_instance(world, attachment) != 0) {
        for (int i = 0; i < acount(world->attachments); i++) {
            if (world->attachments[i].instance == instance) {
                release_instance(asset, instance);
                adel(world->attachments, i);
                rebuild_attachment_index(world, 0);
                return -1;
            }
        }
        assert(false);
        return -1;
    }
    return 0;
}

static bool attachment_entity(const behavior_attachment *attachment, ecs_entity entity)
{
    return attachment->entity.slot == entity.slot &&
           attachment->entity.generation == entity.generation;
}

static bool attachments_match(
    const game_behavior_world *world,
    ecs_entity entity,
    const behavior_attachment *requested
)
{
    const int *indices = entity_attachments(world, entity);
    if (acount(indices) != acount(requested)) {
        return false;
    }
    for (int i = 0; i < acount(indices); i++) {
        const behavior_attachment *current = &world->attachments[indices[i]];
        if (indices[i] != indices[0] + i || current->instance != requested[i].instance) {
            return false;
        }
    }
    return true;
}

int game_behavior_set_attachments(
    game_behavior_world *world,
    ecs_entity entity,
    const char *const *names,
    int count
)
{
    if (!world || count < 0 || (count && !names) ||
        !ecs_entity_is_alive(world->ecs, entity)) {
        return -1;
    }
    /* Resolve every requested asset before allocating or starting anything. */
    for (int i = 0; i < count; i++) {
        if (!names[i] || !names[i][0]) {
            return -1;
        }
        behavior_asset_record *asset = find_asset(world, names[i]);
        if (!asset || (asset->definition->owner_type_name &&
                       strcmp(asset->definition->owner_type_name, "ecs.entity") != 0)) {
            return -1;
        }
        for (int j = 0; j < i; j++) {
            if (strcmp(names[i], names[j]) == 0) {
                return -1;
            }
        }
    }
    int dirty_begin = acount(world->attachments);
    for (int i = 0; i < acount(world->attachments); i++) {
        if (attachment_entity(&world->attachments[i], entity)) {
            dirty_begin = i;
            break;
        }
    }
    behavior_attachment *requested = NULL;
    behavior_attachment *created = NULL;
    for (int i = 0; i < count; i++) {
        behavior_attachment *existing = find_attachment(world, entity, names[i]);
        if (existing) {
            apush(requested, *existing);
            continue;
        }
        behavior_asset_record *asset = find_asset(world, names[i]);
        if (!asset->definition->owner_type_name &&
            behavior_asset_set_owner_type(asset->definition, "ecs.entity") != 0) {
            goto fail;
        }
        behavior_instance *instance = acquire_instance(asset, world, entity);
        if (!instance) {
            goto fail;
        }
        behavior_attachment attachment = {
            .entity = entity,
            .asset = asset,
            .instance = instance,
            .sequence = world->next_sequence++,
        };
        apush(created, attachment);
        apush(requested, attachment);
    }
    /* Publish the new attachments for Begin's ordinary lookup semantics, but
     * retain every old instance until all Begin dispatches have succeeded. */
    for (int i = 0; i < acount(created); i++) {
        append_attachment(world, created[i]);
    }
    for (int i = 0; i < acount(created); i++) {
        behavior_attachment
            *published = find_attachment(world, entity, created[i].asset->name);
        if (begin_instance(world, published) != 0) {
            for (int j = acount(world->attachments) - 1; j >= 0; j--) {
                for (int k = 0; k < acount(created); k++) {
                    if (world->attachments[j].instance == created[k].instance) {
                        adel(world->attachments, j);
                        break;
                    }
                }
            }
            goto fail;
        }
        published = find_attachment(world, entity, created[i].asset->name);
        for (int j = 0; j < acount(requested); j++) {
            if (requested[j].instance == created[i].instance) {
                requested[j].begin_state = published->begin_state;
            }
        }
    }
    if (attachments_match(world, entity, requested)) {
        afree(requested);
        afree(created);
        return 0;
    }
    behavior_attachment *ordered = NULL;
    bool inserted = false;
    for (int i = 0; i < acount(world->attachments); i++) {
        behavior_attachment attachment = world->attachments[i];
        if (!attachment_entity(&attachment, entity)) {
            apush(ordered, attachment);
            continue;
        }
        if (!inserted) {
            for (int j = 0; j < acount(requested); j++) {
                apush(ordered, requested[j]);
            }
            inserted = true;
        }
        bool retained = false;
        for (int j = 0; j < acount(requested); j++) {
            if (requested[j].instance == attachment.instance) {
                retained = true;
                break;
            }
        }
        if (!retained) {
            release_instance(attachment.asset, attachment.instance);
        }
    }
    afree(world->attachments);
    world->attachments = ordered;
    rebuild_attachment_index(world, dirty_begin);
    afree(requested);
    afree(created);
    return 0;
fail:
    for (int i = 0; i < acount(created); i++) {
        release_instance(created[i].asset, created[i].instance);
    }
    rebuild_attachment_index(world, 0);
    afree(created);
    afree(requested);
    return -1;
}

static int detach_entities(
    game_behavior_world *world,
    const ecs_entity *entities,
    int count,
    const behavior_asset_record *asset
)
{
    int total = acount(world->attachments);
    if (!total || !count) {
        return 0;
    }
    uint64_t *selected = xcalloc((size_t)(total + 63) / 64, sizeof *selected);
    int detached = 0, dirty_begin = total;
    for (int i = 0; i < count; i++) {
        const int *indices = entity_attachments(world, entities[i]);
        for (int j = 0; j < acount(indices); j++) {
            int index = indices[j];
            const behavior_attachment *attachment = &world->attachments[index];
            if (attachment_entity(attachment, entities[i]) &&
                (!asset || attachment->asset == asset)) {
                uint64_t bit = UINT64_C(1) << (index % 64);
                detached += !(selected[index / 64] & bit);
                selected[index / 64] |= bit;
                if (index < dirty_begin) {
                    dirty_begin = index;
                }
            }
        }
    }
    if (!detached) {
        free(selected);
        return 0;
    }
    /* Indices and counts before the first removed attachment remain valid.
     * Remove only the old suffix from the indexes before compacting it. */
    for (int i = dirty_begin; i < total; i++) {
        behavior_attachment *attachment = &world->attachments[i];
        attachment->asset->attachment_count--;
        int *indices = world->attachments_by_slot[attachment->entity.slot];
        while (acount(indices) && indices[acount(indices) - 1] >= dirty_begin) {
            asetlen(indices, acount(indices) - 1);
        }
    }
    int retained = dirty_begin;
    for (int i = dirty_begin; i < total; i++) {
        behavior_attachment attachment = world->attachments[i];
        if (!(selected[i / 64] & (UINT64_C(1) << (i % 64)))) {
            world->attachments[retained++] = attachment;
            continue;
        }
        release_instance(attachment.asset, attachment.instance);
    }
    free(selected);
    asetlen(world->attachments, retained);
    mark_attachment_change(world, dirty_begin);
    behavior_pending_truncate(&world->pending, dirty_begin);
    for (int i = dirty_begin; i < retained; i++) {
        index_attachment(world, i);
    }
    return detached;
}

static int detach_asset_entity(game_behavior_world *world, behavior_asset_record *asset, ecs_entity entity)
{
    return detach_entities(world, &entity, 1, asset);
}

int game_behavior_detach_asset(game_behavior_world *world, ecs_entity entity, const char *asset_name)
{
    behavior_asset_record *asset = world && asset_name ? find_asset(world, asset_name)
                                                       : NULL;
    return asset ? detach_asset_entity(world, asset, entity) : -1;
}

int game_behavior_set_debug_hold(
    game_behavior_world *world,
    ecs_entity entity,
    const char *asset_name,
    int enabled
)
{
    behavior_asset_record *asset = world && asset_name ? find_asset(world, asset_name)
                                                       : NULL;
    behavior_attachment *attachment = asset ? find_attachment(world, entity, asset_name)
                                            : NULL;
    if (!attachment) {
        return -1;
    }
    attachment->debug_hold = enabled != 0;
    behavior_instance_set_trace_enabled(attachment->instance, enabled);
    return 0;
}

int game_behavior_detach_entity(game_behavior_world *world, ecs_entity entity)
{
    if (!world) {
        return -1;
    }
    return detach_entities(world, &entity, 1, NULL);
}

int game_behavior_destroy_entity(game_behavior_world *world, ecs_entity entity)
{
    if (!world || !ecs_entity_is_alive(world->ecs, entity)) {
        return ECS_ERR_ENTITY;
    }
    game_behavior_detach_entity(world, entity);
    return ecs_world_destroy_entity(world->ecs, entity);
}

int game_behavior_destroy_entities(game_behavior_world *world, const ecs_entity *entities, int count)
{
    assert(world && count >= 0 && (!count || entities));
    CHIBI_TRACE_ZONE_BEGIN(detach_zone, "Detach behavior attachments");
    detach_entities(world, entities, count, NULL);
    CHIBI_TRACE_ZONE_END(detach_zone);
    for (int i = 0; i < count; i++) {
        ecs_entity entity = entities[i];
        if (!ecs_entity_is_alive(world->ecs, entity)) {
            continue;
        }
        /* Preserve the existing structural boundaries: the ECS command sorter
         * orders equal-key destroys by slot, not by request order. */
        int result = ecs_entity_is_retiring(world->ecs, entity)
                       ? ecs_world_apply_commands(world->ecs)
                       : ecs_world_destroy_entity(world->ecs, entity);
        if (result) {
            return result;
        }
    }
    return ECS_OK;
}

static int dispatch_attachment(
    game_behavior_world *world,
    behavior_attachment *attachment,
    const char *event_name,
    flow_inputs inputs,
    flow_error *error,
    bool batch
)
{
    use_random(world);
    flow_error local = { 0 };
    flow_run *run;
    behavior_instance *instance = attachment->instance;
    bool debug_hold = attachment->debug_hold;
    const behavior_program *program = instance->asset->program;
    bool duplicate = false;
    if (program && !behavior_program_diagnostic(program) &&
        !behavior_program_event(program, event_name, &duplicate)) {
        return 0;
    }
    node *entry;
    const behavior_function *function = batch && program && !duplicate
                                          ? behavior_program_batch_event(program, event_name, &entry)
                                          : NULL;
    bool unobserved = !attachment->debug_hold &&
                      (!instance->profile || instance->profile == record_profile);
    bool cache = batch && unobserved && !inputs.count &&
                 !instance->trace_enabled && !acount(instance->activations) &&
                 (instance->backend == BEHAVIOR_SLJIT ||
                  instance->backend == BEHAVIOR_BYTECODE);
    game_entities_query_scope(world->entities, cache ? function : NULL);
    if (batch && unobserved && !inputs.count &&
        behavior_instance_dispatch_batch(
            instance,
            event_name,
            &attachment->asset->batch,
            &run
        )) {
        if (!run) {
            return 1;
        }
    } else {
        game_entities_query_scope(world->entities, NULL);
        run = behavior_instance_dispatch(instance, event_name, inputs, &local);
    }
    if (!run) {
        if (local.code == FLOW_ERR_UNKNOWN_EVENT) {
            return 0;
        }
        if (error) {
            *error = local;
        }
        return local.code;
    }
    if (flow_run_status(run) == FLOW_FAILED) {
        log_failure(world, attachment->entity, attachment->asset->name, flow_run_error(run));
    }
    if (!debug_hold && (flow_run_status(run) == FLOW_COMPLETED ||
                        flow_run_status(run) == FLOW_FAILED)) {
        behavior_instance_release_activation(instance, run);
    }
    return 1;
}

int game_behavior_dispatch(
    game_behavior_world *world,
    ecs_entity entity,
    const char *event_name,
    flow_inputs inputs,
    flow_error *error
)
{
    if (!world || !ecs_entity_is_alive(world->ecs, entity) || !event_name) {
        return FLOW_ERR_ARGUMENT;
    }
    if (error) {
        *error = (flow_error){ 0 };
    }
    int dispatched = 0, at = 0, next = 0;
    uint64_t revision = world->attachment_revision;
    for (;;) {
        /* A sink may grow/reorder attachments while handling an event. Rebind
         * the index after edits and retain the ordered broadcast-array cursor. */
        if (revision != world->attachment_revision) {
            revision = world->attachment_revision;
            at = 0;
        }
        const int *indices = entity_attachments(world, entity);
        while (at < acount(indices) && indices[at] < next) {
            at++;
        }
        if (at == acount(indices)) {
            break;
        }
        int index = indices[at++];
        next = index + 1;
        behavior_attachment *attachment = &world->attachments[index];
        if (!attachment_entity(attachment, entity)) {
            continue;
        }
        int result = dispatch_attachment(world, attachment, event_name, inputs, error, false);
        if (result < 0) {
            return result;
        }
        dispatched += result;
    }
    return dispatched;
}

int game_behavior_dispatch_all(
    game_behavior_world *world,
    const char *event_name,
    flow_inputs inputs,
    flow_error *error
)
{
    if (!world || !event_name) {
        return FLOW_ERR_ARGUMENT;
    }
    if (error) {
        *error = (flow_error){ 0 };
    }
    int dispatched = 0;
    for (int i = 0; i < acount(world->attachments); i++) {
        behavior_attachment *attachment = &world->attachments[i];
        if (!ecs_entity_is_alive(world->ecs, attachment->entity)) {
            continue;
        }
        int result = dispatch_attachment(
            world,
            attachment,
            event_name,
            inputs,
            error,
            !world->batch_disabled
        );
        if (result < 0) {
            game_entities_query_scope(world->entities, NULL);
            return result;
        }
        dispatched += result;
    }
    game_entities_query_scope(world->entities, NULL);
    return dispatched;
}

static void append_update_row(game_behavior_world *world, int attachment_index)
{
    behavior_attachment *attachment = &world->attachments[attachment_index];
    behavior_instance *instance = attachment->instance;
    const behavior_program *program = instance->asset->program;
    bool duplicate = false;
    if (program && !behavior_program_diagnostic(program) &&
        !behavior_program_event(program, "Tick", &duplicate)) {
        return;
    }
    behavior_update_row row = { .instance = instance,
                                .entity = ecs_entity_to_u64(attachment->entity),
                                .attachment = attachment_index,
                                .services_revision = instance->services_revision };
    node *entry;
    const behavior_function *function = program && !duplicate &&
                                                !behavior_program_diagnostic(program)
                                          ? behavior_program_batch_event(program, "Tick", &entry)
                                          : NULL;
    /* A stretch may share query membership only if every body preserves its
     * dependencies. Other bodies keep their existing effect barriers. */
    if (function && game_entities_queries_preserved(function)) {
        row.binding = behavior_update_prepare(&world->update_bindings, instance, function, entry);
        world->update_query_scope = function;
    }
    apush(world->updates, row);
}

static void prepare_updates(game_behavior_world *world)
{
    CHIBI_TRACE_ZONE_BEGIN(zone, "Prepare Tick schedule");
    if (world->update_bindings_invalidated) {
        /* Defer freeing until the current update returns. A host may change a
         * service while an activation is running, so its old binding can still
         * be on the active call stack. */
        clear_update_bindings(world);
    }
    int dirty_begin = world->update_dirty_begin;
    int row_start = 0;
    if (dirty_begin) {
        while (row_start < acount(world->updates) &&
               world->updates[row_start].attachment < dirty_begin) {
            row_start++;
        }
        asetlen(world->updates, row_start);
    } else {
        aclear(world->updates);
        world->update_query_scope = NULL;
    }
    int attachment_begin = dirty_begin;
    if (attachment_begin > acount(world->attachments)) {
        attachment_begin = acount(world->attachments);
    }
    for (int i = attachment_begin; i < acount(world->attachments); i++) {
        append_update_row(world, i);
    }
    int retained_spans = behavior_update_system_spans_incremental(
        world->updates,
        acount(world->updates),
        row_start,
        &world->update_spans,
        &world->update_kernels
    );
    game_behavior_parallel_prepare_incremental(
        world->update_parallel,
        world->updates,
        world->update_spans,
        ((const ecs_execution_view *)world->ecs)->slot_count,
        retained_spans
    );
    world->update_revision = world->attachment_revision;
    CHIBI_TRACE_ZONE_END(zone);
}

int game_behavior_update(game_behavior_world *world, flow_error *error)
{
    if (!world) {
        return FLOW_ERR_ARGUMENT;
    }
    use_random(world);
    world->update_stats = (game_behavior_update_stats){ 0 };
    pin_value delta = { .type = PIN_F32, .f = (float)world->clock.delta_seconds };
    flow_inputs tick_inputs = { .values = &delta, .count = 1 };
    if (world->batch_disabled) {
        int result = game_behavior_dispatch_all(world, "Tick", tick_inputs, error);
        if (result >= 0) {
            world->update_stats.entries = world->update_stats.scheduled = result;
        }
        return result;
    }
    if (error) {
        *error = (flow_error){ 0 };
    }
    int at = 0, span = 0, next_attachment = 0, dispatched = 0;
    for (;;) {
        if (world->update_revision != world->attachment_revision) {
            prepare_updates(world);
            at = span = 0;
            while (at < acount(world->updates) &&
                   world->updates[at].attachment < next_attachment) {
                at++;
            }
        }
        if (at == acount(world->updates)) {
            break;
        }
        while (span < acount(world->update_spans) &&
               world->update_spans[span].end <= at) {
            span++;
        }
        int end_index = acount(world->updates);
        bool system = span < acount(world->update_spans) &&
                      world->update_spans[span].begin <= at;
        if (span < acount(world->update_spans)) {
            const behavior_update_span *current = &world->update_spans[span];
            end_index = system ? current->end : current->begin;
            if (system) {
                int phase = (at - current->begin) % current->period;
                if (phase) {
                    /* After a paused/failed row, finish this cycle with the
                     * prepared loop before entering the next complete one. */
                    system = false;
                    int next = at + current->period - phase;
                    if (next < end_index) {
                        end_index = next;
                    }
                }
            }
        }
        if (system && at == world->update_spans[span].begin &&
            !world->update_parallel_disabled && !world->update_native_disabled &&
            game_behavior_parallel_run(
                world->update_parallel,
                span,
                (const ecs_execution_view *)world->ecs,
                world->entities,
                &world->update_stats
            )) {
            dispatched += end_index - at;
            at = end_index;
            next_attachment = world->updates[at - 1].attachment + 1;
            continue;
        }
        behavior_update_row *row = &world->updates[at],
                            *end = world->updates + end_index;
        behavior_update_row *stopped;
        int result = FLOW_OK;
        behavior_update_refresh(world->update_bindings);
        game_entities_query_scope(world->entities, world->update_query_scope);
        bool native = !world->update_native_disabled &&
                      row->instance->backend == BEHAVIOR_SLJIT &&
                      !row->instance->profile;
        if (native && row->binding && !world->update_code) {
            world->update_code = behavior_update_compile();
        }
        if (native && world->update_code) {
            stopped = row;
            if (system) {
                stopped = ((behavior_update_fn)world->update_spans[span].code)(
                    row,
                    end,
                    (const ecs_execution_view *)world->ecs,
                    &result
                );
                world->update_stats.system_loop += (int)(stopped - row) + (result < 0);
            }
            if (stopped == row && result == FLOW_OK) {
                stopped = ((
                               behavior_update_fn
                )world->update_code)(row, end, (const ecs_execution_view *)world->ecs, &result);
            }
            world->update_stats.native_loop += (int)(stopped - row) + (result < 0);
        } else {
            stopped = behavior_update_run_c(
                row,
                end,
                (const ecs_execution_view *)world->ecs,
                &result,
                record_profile
            );
        }
        assert(result <= FLOW_OK);
        int ran = (int)(stopped - row) + (result < 0);
        world->update_stats.prepared += ran;
        dispatched += ran;
        at = (int)(stopped - world->updates);
        if (stopped == end) {
            next_attachment = end[-1].attachment + 1;
            continue;
        }
        next_attachment = stopped->attachment + 1;
        behavior_attachment *attachment = &world->attachments[stopped->attachment];
        if (result < 0) {
            game_entities_query_scope(world->entities, NULL);
            flow_run *failed = behavior_update_failure(stopped, result);
            log_failure(world, attachment->entity, attachment->asset->name, flow_run_error(failed));
            behavior_instance_release_activation(stopped->instance, failed);
        } else if (ecs_entity_is_alive(world->ecs, attachment->entity)) {
            /* A changed binding is repaired at the next schedule boundary. The
             * guarded row has not executed, so normal dispatch is safe here. */
            if (stopped->services_revision != stopped->instance->services_revision) {
                mark_attachment_change(world, 0);
            }
            game_entities_query_scope(world->entities, NULL);
            result = dispatch_attachment(world, attachment, "Tick", tick_inputs, error, true);
            if (result < 0) {
                game_entities_query_scope(world->entities, NULL);
                return result;
            }
            world->update_stats.scheduled += result;
            dispatched += result;
        }
        at++;
    }
    game_entities_query_scope(world->entities, NULL);
    world->update_stats.entries = dispatched;
    return dispatched;
}

game_behavior_update_stats game_behavior_last_update(const game_behavior_world *world)
{
    return world->update_stats;
}

void game_behavior_set_parallel_updates(game_behavior_world *world, bool enabled)
{
    world->update_parallel_disabled = !enabled;
}
void game_behavior_set_native_update(game_behavior_world *world, bool enabled)
{
    world->update_native_disabled = !enabled;
}

void game_behavior_set_batch_enabled(game_behavior_world *world, bool enabled)
{
    world->batch_disabled = !enabled;
}

bool game_behavior_has_event(const game_behavior_world *world, const char *event_name)
{
    if (!world || !event_name || !event_name[0]) {
        return false;
    }
    for (int i = 0; i < acount(world->assets); i++) {
        const behavior_asset_record *asset = &world->assets[i];
        bool duplicate;
        if (asset->attachment_count &&
            behavior_program_event(asset->definition->program, event_name, &duplicate)) {
            return true;
        }
    }
    return false;
}

static int instance_breakpoints_active(const behavior_instance *instance)
{
    if (!instance || !acount(instance->activations)) {
        return 0;
    }
    if (instance->breakpoints_override) {
        return instance->breakpoints_enabled;
    }
    const graph *graph = instance->asset->graph;
    if (!graph->breakpoints_enabled) {
        return 0;
    }
    for (int i = 0; i < acount(graph->nodes); i++) {
        if (graph->nodes[i]->breakpoint) {
            return 1;
        }
    }
    return 0;
}

static void pump_attachment(int index, void *user)
{
    game_behavior_world *world = user;
    behavior_attachment *attachment = &world->attachments[world->pumped[index]];
    world->pump_results[index] = FLOW_OK;
    if (!acount(attachment->instance->activations)) {
        return;
    }
    if (!ecs_entity_is_alive(world->ecs, attachment->entity)) {
        return;
    }
    int result = behavior_instance_pump(attachment->instance);
    if (result < 0) {
        world->pump_results[index] = result;
    }
}

int game_behavior_pump(game_behavior_world *world, double monotonic_time)
{
    if (!world) {
        return FLOW_ERR_ARGUMENT;
    }
    use_random(world);
    game_behavior_set_time(world, monotonic_time);
    aclear(world->pumped);
    world->last_pump_count = 0;
    bool serial = false;
    for (int i = behavior_pending_next(&world->pending, 0); i >= 0;
         i = behavior_pending_next(&world->pending, i + 1)) {
        behavior_instance *instance = world->attachments[i].instance;
        assert(acount(instance->activations));
        apush(world->pumped, i);
        serial = serial || instance_breakpoints_active(instance) ||
                 asset_requires_serial(instance->asset);
    }
    int count = acount(world->pumped);
    if (!count) {
        return FLOW_OK;
    }
    /* Serial effects can enqueue a later attachment during this pump. Retain
     * the original ordered walk by consulting membership again after each row. */
    int limit = acount(world->attachments);
    int capacity = serial || !world->parallel_for ? limit : count;
    if (capacity > world->pump_result_capacity) {
        world->pump_results = xrealloc(
            world->pump_results,
            (size_t)capacity * sizeof *world->pump_results
        );
        world->pump_result_capacity = capacity;
    }
    if (world->parallel_for && !serial) {
        world->parallel_for(count, pump_attachment, world);
    } else {
        aclear(world->pumped);
        for (int i = behavior_pending_next(&world->pending, 0); i >= 0 && i < limit;
             i = behavior_pending_next(&world->pending, i + 1)) {
            apush(world->pumped, i);
            pump_attachment(acount(world->pumped) - 1, world);
        }
    }
    world->last_pump_count = acount(world->pumped);
    int result = FLOW_OK;
    for (int i = 0; i < acount(world->pumped); i++) {
        if (world->pump_results[i] < 0) {
            result = world->pump_results[i];
        }
        behavior_attachment *attachment = &world->attachments[world->pumped[i]];
        if (!attachment->debug_hold) {
            reap_instance(world, attachment);
        }
    }
    return result;
}

int game_behavior_last_pump_count(const game_behavior_world *world)
{
    return world->last_pump_count;
}

void game_behavior_set_time(game_behavior_world *world, double simulation_time)
{
    world->clock.current_time = simulation_time;
}

static game_behavior_attachment_info info_of(behavior_attachment *attachment)
{
    return (game_behavior_attachment_info){
        .entity = attachment->entity,
        .asset_name = attachment->asset->name,
        .definition = attachment->asset->definition,
        .instance = attachment->instance,
        .sequence = attachment->sequence,
        .debug_hold = attachment->debug_hold,
    };
}

/* The first activation paused at a breakpoint or step, and its attachment. */
static flow_run *first_paused(const game_behavior_world *world, int *attachment)
{
    for (int i = behavior_pending_next(&world->pending, 0); i >= 0;
         i = behavior_pending_next(&world->pending, i + 1)) {
        const behavior_instance *instance = world->attachments[i].instance;
        for (int j = 0; j < acount(instance->activations); j++) {
            if (flow_run_status(instance->activations[j]) == FLOW_PAUSED) {
                *attachment = i;
                return instance->activations[j];
            }
        }
    }
    return NULL;
}

bool game_behavior_paused(const game_behavior_world *world)
{
    int attachment;
    return first_paused(world, &attachment) != NULL;
}

flow_run *game_behavior_paused_run(game_behavior_world *world, game_behavior_attachment_info *out)
{
    int attachment;
    flow_run *run = first_paused(world, &attachment);
    if (run) {
        *out = info_of(&world->attachments[attachment]);
    }
    return run;
}

void game_behavior_set_delta_seconds(game_behavior_world *world, double delta_seconds)
{
    if (!world) {
        return;
    }
    world->clock.delta_seconds = delta_seconds;
}

void game_behavior_set_parallel_for(
    game_behavior_world *world,
    flow_parallel_for_fn parallel_for,
    void *parallel_user
)
{
    if (!world) {
        return;
    }
    world->parallel_for = parallel_for;
    world->parallel_user = parallel_user;
}

void game_behavior_set_sink(game_behavior_world *world, flow_sink_fn sink, void *user)
{
    if (!world) {
        return;
    }
    world->sink = sink;
    world->sink_user = user;
}

int game_behavior_log_count(const game_behavior_world *world)
{
    return world ? acount(world->logs) : 0;
}

int game_behavior_log_info_at(const game_behavior_world *world, int index, game_behavior_log_info *out)
{
    if (!world || !out || index < 0 || index >= acount(world->logs)) {
        return -1;
    }
    *out = world->logs[index];
    return 0;
}

void game_behavior_clear_logs(game_behavior_world *world)
{
    if (world) {
        aclear(world->logs);
    }
}

int game_behavior_attachment_count(const game_behavior_world *world)
{
    return world ? acount(world->attachments) : 0;
}

int game_behavior_attachment_count_for_entity(const game_behavior_world *world, ecs_entity entity)
{
    if (!world) {
        return 0;
    }
    int count = 0;
    const int *indices = entity_attachments(world, entity);
    for (int i = 0; i < acount(indices); i++) {
        const behavior_attachment *attachment = &world->attachments[indices[i]];
        if (attachment->entity.slot == entity.slot &&
            attachment->entity.generation == entity.generation) {
            count++;
        }
    }
    return count;
}

int game_behavior_attachment_info_at(
    game_behavior_world *world,
    ecs_entity entity,
    int index,
    game_behavior_attachment_info *out
)
{
    if (!world || !out || index < 0) {
        return -1;
    }
    const int *indices = entity_attachments(world, entity);
    for (int i = 0; i < acount(indices); i++) {
        behavior_attachment *attachment = &world->attachments[indices[i]];
        if (attachment->entity.slot != entity.slot ||
            attachment->entity.generation != entity.generation) {
            continue;
        }
        if (index-- != 0) {
            continue;
        }
        *out = info_of(attachment);
        return 0;
    }
    return -1;
}

int game_behavior_entity_state_at(
    const game_behavior_world *world,
    ecs_entity entity,
    const char *asset_name,
    game_behavior_entity_state *out
)
{
    behavior_attachment *attachment = world && asset_name
                                        ? find_attachment((game_behavior_world *)world, entity, asset_name)
                                        : NULL;
    if (!attachment || !out) {
        return -1;
    }
    *out = (game_behavior_entity_state){ .entity = entity };
    for (int i = 0; i < acount(attachment->instance->activations); i++) {
        const flow_run *run = attachment->instance->activations[i];
        flow_state state = flow_run_status(run);
        if (state == FLOW_COMPLETED || state == FLOW_FAILED || state == FLOW_IDLE) {
            continue;
        }
        out->active = 1;
        out->suspended = state == FLOW_SUSPENDED;
        if (out->suspended) {
            out->wake_time = flow_run_wake_time(run);
        }
        return 0;
    }
    return 0;
}

int game_behavior_asset_registered(const game_behavior_world *world, const char *asset_name)
{
    return world && asset_name &&
           find_asset((game_behavior_world *)world, asset_name) != NULL;
}

int game_behavior_asset_count(const game_behavior_world *world)
{
    return world ? acount(world->assets) : 0;
}

const char *game_behavior_asset_name_at(const game_behavior_world *world, int index)
{
    return world && index >= 0 && index < acount(world->assets)
             ? world->assets[index].name
             : NULL;
}

const behavior_asset *game_behavior_asset_definition_at(const game_behavior_world *world, int index)
{
    return world && index >= 0 && index < acount(world->assets)
             ? world->assets[index].definition
             : NULL;
}

int game_behavior_pool_size(const game_behavior_world *world, const char *asset_name)
{
    behavior_asset_record *asset = world && asset_name
                                     ? find_asset((game_behavior_world *)world, asset_name)
                                     : NULL;
    return asset ? acount(asset->pool) : 0;
}

behavior_instance *game_behavior_instance(game_behavior_world *world, ecs_entity entity, const char *asset_name)
{
    behavior_attachment *attachment = world ? find_attachment(world, entity, asset_name)
                                            : NULL;
    return attachment ? attachment->instance : NULL;
}

void game_behavior_inspect(game_behavior_world *world, const behavior_instance *instance)
{
    if (!world) {
        return;
    }
    for (int i = 0; i < acount(world->attachments); i++) {
        behavior_attachment *attachment = &world->attachments[i];
        bool enabled = attachment->debug_hold || attachment->instance == instance;
        if (attachment->instance->trace_enabled != enabled) {
            behavior_instance_set_trace_enabled(attachment->instance, enabled);
        }
    }
}
