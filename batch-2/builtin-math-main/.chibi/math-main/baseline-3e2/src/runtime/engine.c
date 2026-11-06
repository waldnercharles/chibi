#include "runtime/engine.h"

#include "diagnostics/tracy.h"
#include "execution/status.h"
#include "runtime/component_catalog.h"
#include "runtime/components.h" /* IWYU pragma: keep */
#include "runtime/engine_input.h"
#include "runtime/events.h"
#include "runtime/particle_emitters.h"
#include "runtime/scenes/physics.h"
#include "runtime/scenes/physics3d.h"
#include "runtime/source_input.h"
#include "runtime/task_pool.h"
#include "runtime/transform_propagation.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct
{
    char *name;
    int phase, order;
    execution_program *program;
    source_registry *sources;
    source_system_batch *batch;
    int batch_index;
    bool seen;
} authored_system;

typedef enum
{
    STEP_IDLE,
    STEP_BEHAVIORS,
    STEP_PRE_PHYSICS,
    STEP_POST_PHYSICS,
    STEP_CONTACTS,
    STEP_COMMANDS,
    STEP_POST_EVENTS,
    STEP_PRESENTATION
} step_phase;

struct engine
{
    ecs_world *world;
    engine_modules modules;
    source_input source_input;
    particle_emitters *emitters;
    schedule *systems;
    task_pool *pool;
    task_system tasks;
    authored_system **authored;
    engine_render_fn render;
    void *render_user;
    engine_focus focus;
    bool playing;
    bool step_requested;
    engine_input *input;
    step_phase phase;
    engine_frame_timing timing;
    uint64_t scene_steps; /* simulation_steps when the scene started */
    double last_time;
    double accumulator;
    double time_scale;
    bool has_time;
    bool debug_paused;
    bool recording_input;
    input_keyframe *recorded; /* keys where held input changed, since the last reset */
    input_keyframe *replay; /* keys held in place of live samples; NULL when live */
    char error[256];
};

const visibility *engine_visibility(const engine *engine)
{
    return engine ? engine->modules.visibility : NULL;
}

audio *engine_audio(const engine *engine)
{
    return engine->modules.audio;
}

ui *engine_ui(const engine *engine)
{
    return engine->modules.ui;
}

/* Whether an entity shows: it isn't hidden and, when its render state culls
 * it, the fog sees `ground`. The fog leaves an entity with no ground, NULL. */
static bool visible_at(const engine *engine, ecs_world *world, ecs_entity entity, const CF_V2 *ground)
{
    const RenderState *state = RenderState_get_const(world, entity);
    if (!engine || !state) {
        return true;
    }
    if (state->hidden) {
        return false;
    }
    const visibility *visibility = engine_visibility(engine);
    return !state->visibility_culled || !ground || !visibility ||
           visibility_at(visibility, *ground).visible;
}

bool engine_entity_visible(const engine *engine, ecs_world *world, ecs_entity entity)
{
    const GlobalTransform2D *global = GlobalTransform2D_get_const(world, entity);
    return visible_at(engine, world, entity, global ? &global->position : NULL);
}

bool engine_entity_visible_3d(const engine *engine, ecs_world *world, ecs_entity entity, CF_V3 position)
{
    CF_V2 ground = { position.x, position.z };
    return visible_at(engine, world, entity, &ground);
}

static double elapsed_ms(struct timespec start, struct timespec end)
{
    int64_t seconds = (int64_t)end.tv_sec - (int64_t)start.tv_sec;
    int64_t nanoseconds = (int64_t)end.tv_nsec - (int64_t)start.tv_nsec;
    return (double)(seconds * INT64_C(1000000000) + nanoseconds) / 1000000.0;
}

static int clock_now(struct timespec *out)
{
    return clock_gettime(CLOCK_MONOTONIC, out) == 0 ? 0 : -1;
}

static int sync_authored_systems(engine *);
static void *schedule_enqueue(task_range *run, int count, int minimum, void *context, void *user)
{
    engine *e = user;
    task_pool_begin(e->pool, true);
    const task_system *tasks = task_pool_tasks(e->pool);
    void *task = tasks->enqueue(run, count, minimum, context, tasks->user);
    if (!task) {
        task_pool_end(e->pool);
    }
    return task;
}
static void schedule_finish(void *task, void *user)
{
    engine *e = user;
    const task_system *tasks = task_pool_tasks(e->pool);
    tasks->finish(task, tasks->user);
    task_pool_end(e->pool);
}
static bool has_simulation(const engine *e)
{
    return e->modules.sources || e->modules.entities || e->modules.physics ||
           e->modules.physics3d || e->modules.materials ||
           e->modules.particles || e->modules.visibility;
}
static bool engine_paused(const engine *e)
{
    return e->modules.paused && e->modules.paused(e->modules.context);
}
static bool system_stage(const engine *e)
{
    return e->phase >= STEP_PRE_PHYSICS && e->phase != STEP_CONTACTS;
}
static void boundary(engine *e)
{
    e->source_input.now = e->timing.simulation_time;
    e->source_input.delta = ENGINE_FIXED_SECONDS;
    if (e->modules.boundary) {
        e->modules
            .boundary(e->modules.context, e->source_input.now, e->source_input.delta);
    }
}
static int update_sources(engine *e)
{
    source_registry *sources = e->modules.sources;
    if (!sources) {
        return FLOW_OK;
    }
    int status = source_begin(sources, e->timing.simulation_time, ENGINE_FIXED_SECONDS);
    pin_value dt = { .type = PIN_F32, .f = (float)ENGINE_FIXED_SECONDS };
    if (status >= 0) {
        status = source_dispatch_all(sources, "Tick", &dt, 1, e->timing.simulation_time, ENGINE_FIXED_SECONDS);
    }
    if (status >= 0) {
        status = source_step(sources, e->timing.simulation_time, ENGINE_FIXED_SECONDS);
    }
    if (status < 0) {
        snprintf(e->error, sizeof e->error, "%s", source_error(sources));
    }
    return status < 0 ? status : FLOW_OK;
}
static int pump_sources(engine *e)
{
    if (!e->modules.sources) {
        return FLOW_OK;
    }
    int status = source_pump(e->modules.sources, e->timing.simulation_time, ENGINE_FIXED_SECONDS);
    if (status < 0) {
        snprintf(e->error, sizeof e->error, "%s", source_error(e->modules.sources));
    }
    return status;
}
/* Unbinds before the engine frees its input, since sources outlive it. */
static void bind_input(engine *e, engine_input *input)
{
    if (!e->modules.sources) {
        return;
    }
    int status = source_bind_service(e->modules.sources, INPUT_SERVICE, input);
    assert(!status);
    (void)status;
}

engine *engine_new(ecs_world *world, const engine_modules *modules)
{
    if (!world) {
        return NULL;
    }
    engine *result = (engine *)xcalloc(1, sizeof *result);
    result->world = world;
    result->pool = task_pool_acquire();
    result->tasks = (task_system){ task_pool_tasks(result->pool)->workers,
                                   result,
                                   schedule_enqueue,
                                   schedule_finish };
    result->systems = schedule_new(world, &result->tasks);
    if (modules) {
        result->modules = *modules;
    }
    assert(!result->modules.sources || source_world(result->modules.sources) == world);
    assert(!result->modules.entities || entities_world(result->modules.entities) == world);
    result->source_input.sources = result->modules.sources;
    if (result->modules.particles) {
        result->emitters = particle_emitters_new(result->modules.particles);
        particle_emitters_bind_materials(result->emitters, result->modules.materials);
    }
    result->playing = true;
    result->time_scale = 1;
    if (!modules || modules->input) {
        engine_input_host host = result->modules.sources
                                   ? source_input_host(&result->source_input)
                                   : (engine_input_host){ 0 };
        result->input = engine_input_new(world, &host);
        bind_input(result, result->input);
    }
    boundary(result);
    if (result->modules.sources && sync_authored_systems(result) < 0) {
        engine_free(result);
        return NULL;
    }
    return result;
}

void engine_free(engine *engine)
{
    if (!engine) {
        return;
    }
    if (engine->modules.sources) {
        source_cancel_systems(engine->modules.sources);
    }
    if (engine->emitters) {
        particle_emitters_free(engine->emitters);
    }
    schedule_free(engine->systems);
    task_pool_release(engine->pool);
    for (int i = 0; i < acount(engine->authored); i++) {
        free(engine->authored[i]->name);
        free(engine->authored[i]);
    }
    afree(engine->authored);
    afree(engine->recorded);
    afree(engine->replay);
    if (engine->input) {
        bind_input(engine, NULL);
        engine_input_free(engine->input);
    }
    free(engine);
}

int engine_register_system(engine *engine, const schedule_desc *desc)
{
    for (int i = 0; desc && desc->name && i < acount(engine->authored); i++) {
        if (!strcmp(engine->authored[i]->name, desc->name)) {
            return -1;
        }
    }
    return schedule_register(engine->systems, desc);
}
const schedule *engine_system_schedule(const engine *engine)
{
    return engine->systems;
}

typedef struct
{
    char *name;
    int phase, order;
    execution_program *program;
} source_system_binding;
static void source_bindings_free(source_system_binding *bindings)
{
    for (int i = 0; i < acount(bindings); i++) {
        free(bindings[i].name);
    }
    afree(bindings);
}
static int source_system_run(ecs_world *, double, double, void *user, schedule_error *error)
{
    authored_system *system = user;
    assert(system->batch);
    int status = source_systems_run(
        system->batch,
        system->batch_index,
        error->message,
        sizeof error->message
    );
    error->status = status;
    return status;
}
static int source_system_run_parallel(void *user, const task_system *tasks, schedule_error *error)
{
    authored_system *system = user;
    assert(system->batch);
    error->status = source_systems_run_parallel(
        system->batch,
        system->batch_index,
        tasks,
        error->message,
        sizeof error->message
    );
    return error->status;
}
static int source_system_prepare(void *user, schedule_error *error)
{
    authored_system *system = user;
    assert(system->batch);
    error->status = source_systems_prepare_index(
        system->batch,
        system->batch_index,
        error->message,
        sizeof error->message
    );
    return error->status;
}
static int source_system_finish(void *user, bool commit, schedule_error *error)
{
    authored_system *system = user;
    assert(system->batch);
    error->status = source_systems_finish_index(
        system->batch,
        system->batch_index,
        commit,
        error->message,
        sizeof error->message
    );
    return error->status;
}
static bool source_system_parallel(void *user)
{
    authored_system *system = user;
    assert(system->batch);
    return source_systems_parallel(system->batch, system->batch_index);
}
static int sync_authored_systems(engine *engine)
{
    if (!engine->modules.sources) {
        return 0;
    }
    source_registry *sources = engine->modules.sources;
    source_system_binding *bindings = NULL;
    ecs_component_id id = COMPONENT_EngineSystem.id;
    if (ecs_world_component(engine->world, id)) {
        ecs_query query;
        int status = ecs_query_begin(engine->world, &query, &id, 1);
        if (status != ECS_OK) {
            return FLOW_ERR_STATE;
        }
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            const EngineSystem *binding = EngineSystem_get_const(engine->world, entity);
            bool duplicate = false;
            for (int i = 0; i < acount(bindings); i++) {
                duplicate |= binding->asset &&
                             !strcmp(bindings[i].name, binding->asset);
            }
            if (!binding->asset || !source_is_system(sources, binding->asset) ||
                binding->phase < 0 || binding->phase >= SYSTEM_PHASE_COUNT ||
                duplicate) {
                snprintf(
                    engine->error,
                    sizeof engine->error,
                    "Invalid or duplicate Scene source system: %s",
                    binding->asset ? binding->asset : ""
                );
                ecs_query_end(&query);
                source_bindings_free(bindings);
                return FLOW_ERR_COMPILE;
            }
            apush(
                bindings,
                ((source_system_binding){ xstrdup(binding->asset),
                                          binding->phase,
                                          binding->order,
                                          source_program(sources, binding->asset) })
            );
        }
        ecs_query_end(&query);
    }
    for (int i = 0; i < acount(bindings); i++) {
        for (int j = 0; j < schedule_count(engine->systems); j++) {
            const char *name = schedule_at(engine->systems, j);
            if (strcmp(name, bindings[i].name)) {
                continue;
            }
            bool authored = false;
            for (int k = 0; k < acount(engine->authored); k++) {
                authored |= !strcmp(engine->authored[k]->name, name);
            }
            if (!authored) {
                snprintf(engine->error, sizeof engine->error, "Source system conflicts with native system: %s", name);
                source_bindings_free(bindings);
                return FLOW_ERR_COMPILE;
            }
        }
    }
    const char **names = NULL;
    for (int i = 0; i < acount(bindings); i++) {
        apush(names, bindings[i].name);
    }
    int status = source_select_systems(sources, names, acount(names));
    afree(names);
    if (status < 0) {
        snprintf(engine->error, sizeof engine->error, "%s", source_error(sources));
        source_bindings_free(bindings);
        return status;
    }
    for (int i = 0; i < acount(engine->authored); i++) {
        authored_system *system = engine->authored[i];
        system->seen = false;
        for (int j = 0; j < acount(bindings); j++) {
            source_system_binding *binding = &bindings[j];
            if (system->program == binding->program) {
                system->seen = !strcmp(system->name, binding->name) &&
                               system->phase == binding->phase &&
                               system->order == binding->order;
                break;
            }
        }
        if (!system->seen) {
            schedule_remove(engine->systems, system->name);
        }
    }
    for (int i = 0; i < acount(bindings); i++) {
        source_system_binding *binding = &bindings[i];
        authored_system *system = NULL;
        for (int j = 0; j < acount(engine->authored); j++) {
            if (engine->authored[j]->program == binding->program) {
                system = engine->authored[j];
                break;
            }
        }
        /* Publication can preserve the program pointer while replacing its
         * immutable generation. Refresh copied scheduling effects each boundary. */
        if (!system) {
            system = xcalloc(1, sizeof *system);
            apush(engine->authored, system);
        } else {
            free(system->name);
        }
        system->name = xstrdup(binding->name);
        system->phase = binding->phase;
        system->order = binding->order;
        system->program = binding->program;
        system->sources = sources;
        system->seen = true;
        execution_effect_summary effects = execution_module_effects(
            execution_program_module(system->program)
        );
        schedule_desc desc = { .name = system->name,
                               .phase = system->phase,
                               .order = system->order,
                               .identity = system->program,
                               .run = source_system_run,
                               .run_parallel = source_system_run_parallel,
                               .prepare = source_system_prepare,
                               .finish = source_system_finish,
                               .parallel_ready = source_system_parallel,
                               .user = system,
                               .reads = effects.reads,
                               .read_count = effects.read_count,
                               .writes = effects.writes,
                               .write_count = effects.write_count,
                               .reduces = effects.reduces,
                               .reduce_count = effects.reduce_count,
                               .declared = effects.declared,
                               .structural = effects.structural,
                               .host_sink = effects.host_sink,
                               .world_read = effects.world_read,
                               .deterministic = effects.deterministic,
                               .parallel_safe = effects.parallel_safe };
        status = schedule_register(engine->systems, &desc);
        assert(!status);
        (void)status;
    }
    source_bindings_free(bindings);
    for (int i = acount(engine->authored) - 1; i >= 0; i--) {
        authored_system *system = engine->authored[i];
        if (system->seen) {
            continue;
        }
        free(system->name);
        free(system);
        adel(engine->authored, i);
    }
    return 0;
}

static int maintain_bindings(engine *engine)
{
    schedule_error scheduling = { 0 };
    const char *message = NULL;
    int result = schedule_maintain(engine->systems, engine->timing.simulation_time, ENGINE_FIXED_SECONDS, &scheduling);
    if (result < 0) {
        message = scheduling.message;
    }
    if (result >= 0 && engine->modules.sources) {
        source_registry *sources = engine->modules.sources;
        result = source_maintain(sources, engine->timing.simulation_time, ENGINE_FIXED_SECONDS);
        if (result < 0) {
            message = source_error(sources);
        }
    }
    if (result < 0) {
        if (!message) {
            message = schedule_error_message(engine->systems);
        }
        snprintf(engine->error, sizeof engine->error, "%s", message && *message ? message : "Reactive maintenance failed");
    }
    return result;
}

static int run_system_phase(engine *engine, system_phase phase)
{
    if (!schedule_build(engine->systems)) {
        snprintf(
            engine->error,
            sizeof engine->error,
            "%s",
            schedule_error_message(engine->systems)
        );
        return FLOW_ERR_COMPILE;
    }
    const char **names = NULL;
    for (int i = 0; i < acount(engine->authored); i++) {
        authored_system *system = engine->authored[i];
        if (system->seen && system->phase == (int)phase) {
            system->batch_index = acount(names);
            apush(names, system->name);
        }
    }
    source_registry *sources = engine->modules.sources;
    source_system_batch *batch = sources ? source_systems_prepare(
                                               sources,
                                               names,
                                               acount(names),
                                               engine->timing.simulation_time,
                                               ENGINE_FIXED_SECONDS
                                           )
                                         : NULL;
    afree(names);
    if (sources && !batch) {
        snprintf(engine->error, sizeof engine->error, "%s", source_error(sources));
        return FLOW_ERR_COMPILE;
    }
    for (int i = 0; i < acount(engine->authored); i++) {
        authored_system *system = engine->authored[i];
        if (system->seen && system->phase == (int)phase) {
            system->batch = batch;
        }
    }
    int result = schedule_run(engine->systems, phase, engine->timing.simulation_time, ENGINE_FIXED_SECONDS);
    int source_status = batch ? source_systems_finish(batch) : 0;
    for (int i = 0; i < acount(engine->authored); i++) {
        engine->authored[i]->batch = NULL;
    }
    if (result >= 0 && source_status < 0) {
        snprintf(engine->error, sizeof engine->error, "%s", source_error(sources));
        return source_status;
    }
    if (result < 0) {
        snprintf(
            engine->error,
            sizeof engine->error,
            "%s",
            schedule_error_message(engine->systems)
        );
    }
    return result != FLOW_OK ? result : maintain_bindings(engine);
}

void engine_set_render_callback(engine *engine, engine_render_fn fn, void *user)
{
    if (!engine) {
        return;
    }
    engine->render = fn;
    engine->render_user = user;
}

void engine_reset(engine *engine)
{
    engine->timing = (engine_frame_timing){ .alpha = 1 };
    engine->last_time = 0;
    engine->accumulator = 0;
    engine->has_time = false;
    engine->debug_paused = false;
    engine->step_requested = false;
    /* Recording and replay follow a scene from its start. */
    aclear(engine->recorded);
    if (engine->input) {
        engine_input_reset(engine->input);
    }
    /* Only a restart clears audio; it carries on across scene changes. */
    if (engine_audio(engine)) {
        audio_reset(engine_audio(engine));
    }
    engine_reset_scene(engine);
}

void engine_reset_scene(engine *engine)
{
    /* Held work belongs to the old scene, including unpublished reductions. */
    if (engine->modules.sources) {
        source_cancel_systems(engine->modules.sources);
    }
    schedule_reset(engine->systems);
    if (engine->modules.terrain_storage) {
        terrain_storage_reset(engine->modules.terrain_storage);
    }
    engine->phase = STEP_IDLE;
    engine->error[0] = 0;
    engine->scene_steps = engine->timing.simulation_steps;
    if (engine->emitters) {
        particle_emitters_reset(engine->emitters);
    }
    if (has_simulation(engine)) {
        if (engine->modules.save) {
            save_discard(engine->modules.save);
        }
        boundary(engine);
        if (engine->modules.physics) {
            physics_reset(engine->modules.physics);
        }
        if (engine->modules.physics3d) {
            physics3d_reset(engine->modules.physics3d);
        }
        if (engine->modules.materials) {
            materials_reset(engine->modules.materials);
        }
        if (engine->modules.visibility) {
            visibility_reset(engine->modules.visibility);
        }
        if (engine_ui(engine)) {
            ui_reset(engine_ui(engine));
        }
    }
}

const materials *engine_materials(const engine *engine)
{
    return engine ? engine->modules.materials : NULL;
}

int engine_set_focus(engine *engine, engine_focus focus)
{
    if (!engine || focus < ENGINE_FOCUS_NONE || focus > ENGINE_FOCUS_CANVAS) {
        return -1;
    }
    if (engine->focus == ENGINE_FOCUS_VIEWPORT &&
        focus != ENGINE_FOCUS_VIEWPORT && !engine->replay && engine->input) {
        engine_input_clear(engine->input);
    }
    engine->focus = focus;
    return 0;
}

engine_focus engine_get_focus(const engine *engine)
{
    return engine ? engine->focus : ENGINE_FOCUS_NONE;
}

void engine_set_input_actions(engine *engine, const input_action *actions, int count)
{
    if (engine->input) {
        engine_input_set_actions(engine->input, actions, count);
    }
}

int engine_sample_controls(engine *engine, uint64_t controls, bool *firing)
{
    if (!engine->input) {
        return FLOW_ERR_STATE;
    }
    /* A stopped transport keeps the controls held when it stopped, so hosts
     * that sample nothing while paused never press them again on resume. */
    if (engine->replay || (!engine->playing && !engine->step_requested)) {
        *firing = false;
        return FLOW_OK;
    }
    return engine_input_controls(engine->input, controls, firing);
}

int engine_sample_devices(
    engine *engine,
    uint64_t controls,
    const input_controller_state controllers[INPUT_CONTROLLER_MAX],
    bool *firing
)
{
    if (!engine->input) {
        return FLOW_ERR_STATE;
    }
    if (engine->replay || (!engine->playing && !engine->step_requested)) {
        *firing = false;
        return FLOW_OK;
    }
    return engine_input_devices(engine->input, controls, controllers, firing);
}

int engine_sample_controller(engine *engine, int controller, input_controller_state state, bool *firing)
{
    if (!engine->input) {
        return FLOW_ERR_STATE;
    }
    if (engine->replay || (!engine->playing && !engine->step_requested)) {
        *firing = false;
        return FLOW_OK;
    }
    return engine_input_controller(engine->input, controller, state, firing);
}

void engine_record_input(engine *engine, unsigned seed)
{
    assert(engine->input);
    srand(seed);
    aclear(engine->recorded);
    engine->recording_input = true;
}

void engine_stop_recording(engine *engine)
{
    engine->recording_input = false;
}

const input_keyframe *engine_recorded_input(const engine *engine, int *count)
{
    *count = acount(engine->recorded);
    return engine->recorded;
}

void engine_replay_input(engine *engine, const input_keyframe *keys, int count, unsigned seed)
{
    assert(engine->input);
    assert(count > 0 && keys[0].step == 0);
    srand(seed);
    aclear(engine->replay);
    for (int i = 0; i < count; i++) {
        assert(!i || keys[i].step >= keys[i - 1].step);
        apush(engine->replay, keys[i]);
    }
}

void engine_stop_replay(engine *engine)
{
    afree(engine->replay);
    engine->replay = NULL;
}

bool engine_replaying_input(const engine *engine)
{
    return engine->replay != NULL;
}

/* The index of the key in effect at `step`: the last one at or before it. */
static int replay_key(const engine *engine, int step)
{
    int low = 0, high = acount(engine->replay) - 1;
    while (low < high) {
        int middle = (low + high + 1) / 2;
        if (engine->replay[middle].step <= step) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    return low;
}

/* Sample the keys recorded at this step in order, then hold the last one. */
static int replay_step(engine *engine, int step)
{
    int last = replay_key(engine, step), first = last;
    while (engine->replay[last].step == step && first > 0 &&
           engine->replay[first - 1].step == step) {
        first--;
    }
    int result = FLOW_OK;
    for (int i = first; i <= last && result >= 0; i++) {
        result = engine_input_hold(engine->input, &engine->replay[i]);
    }
    return result;
}

/* Record the held input where it changes, after the control changes since the
 * last step when there were several, since they may press and release an
 * action that the held controls alone would not show. */
static void record_step(engine *engine, int step)
{
    input_keyframe held = engine_input_held(engine->input);
    held.step = step;
    const input_keyframe *trail;
    int changes = engine_input_trail(engine->input, &trail);
    for (int i = 0; i + 1 < changes; i++) {
        input_keyframe key = held;
        key.controls = trail[i].controls;
        memcpy(key.controllers, trail[i].controllers, sizeof key.controllers);
        apush(engine->recorded, key);
    }
    int count = acount(engine->recorded);
    if (changes > 1 || !count ||
        !input_keyframe_same(&engine->recorded[count - 1], &held)) {
        apush(engine->recorded, held);
    }
}

/* Replay or record this step's input. A replayed key is held every step, which
 * live samples also do, so a pause or focus change that clears input cannot
 * drop it. */
static int follow_input(engine *engine)
{
    if (!engine->input) {
        return FLOW_OK;
    }
    int step = (int)engine->timing.simulation_steps - 1;
    if (engine->replay) {
        return replay_step(engine, step);
    }
    if (engine->recording_input) {
        record_step(engine, step);
    }
    return FLOW_OK;
}

int engine_set_playing(engine *engine, bool playing)
{
    if (!engine) {
        return -1;
    }
    if (engine->playing == playing) {
        return 0;
    }
    engine->playing = playing;
    engine->timing.alpha = 1;
    engine->accumulator = 0;
    engine->has_time = false;
    engine->step_requested = false;
    if (!playing && engine->input) {
        engine_input_clear(engine->input);
    }
    return 0;
}

bool engine_is_playing(const engine *engine)
{
    return engine && engine->playing;
}

int engine_step(engine *engine)
{
    if (!engine || engine->playing) {
        return FLOW_ERR_STATE;
    }
    engine->step_requested = true;
    return 0;
}

void engine_set_time_scale(engine *engine, double scale)
{
    assert(isfinite(scale) && scale > 0);
    engine->time_scale = scale;
}

double engine_time_scale(const engine *engine)
{
    return engine->time_scale;
}

int engine_dispatch_input(engine *engine, const char *action, double value, int pressed, engine_input_failure *error)
{
    if (error) {
        *error = (engine_input_failure){ 0 };
    }
    if (!engine || !action || !isfinite(value)) {
        return FLOW_ERR_ARGUMENT;
    }
    if (engine->focus != ENGINE_FOCUS_VIEWPORT || !engine->playing || !engine->input) {
        return FLOW_ERR_STATE;
    }
    int result = engine_input_action(engine->input, action, pressed != 0);
    if (result < 0 && error) {
        *error = (engine_input_failure){ .code = result, .message = "Unknown local input action." };
    }
    return result;
}

int engine_sample_movement(engine *engine, float x, float y)
{
    if (!engine || !isfinite(x) || !isfinite(y)) {
        return FLOW_ERR_ARGUMENT;
    }
    if (engine->focus != ENGINE_FOCUS_VIEWPORT || !engine->playing || !engine->input) {
        return FLOW_ERR_STATE;
    }
    if (engine->replay) {
        return FLOW_OK;
    }
    engine_input_movement(engine->input, (CF_V2){ x, y });
    return FLOW_OK;
}

int engine_sample_aim(engine *engine, CF_V2 aim, bool firing, bool point)
{
    if (!engine || !isfinite(aim.x) || !isfinite(aim.y)) {
        return FLOW_ERR_ARGUMENT;
    }
    if (!engine->input) {
        return FLOW_ERR_STATE;
    }
    if (engine->replay) {
        return FLOW_OK;
    }
    bool active = engine->playing && engine->focus == ENGINE_FOCUS_VIEWPORT;
    engine_input_aim(engine->input, active ? aim : (CF_V2){ 0 }, active && firing, active && point);
    return FLOW_OK;
}

int engine_sample_pointer_ray(engine *engine, input_pointer_ray pointer)
{
    CF_V3 o = pointer.origin, d = pointer.direction;
    if (!engine ||
        (pointer.active &&
         (!isfinite(o.x) || !isfinite(o.y) || !isfinite(o.z) || !isfinite(d.x) ||
          !isfinite(d.y) || !isfinite(d.z) || (!d.x && !d.y && !d.z)))) {
        return FLOW_ERR_ARGUMENT;
    }
    if (!engine->input) {
        return FLOW_ERR_STATE;
    }
    if (engine->replay) {
        return FLOW_OK;
    }
    pointer.active &= engine->playing && engine->focus == ENGINE_FOCUS_VIEWPORT;
    engine_input_pointer(engine->input, pointer);
    return FLOW_OK;
}

static int flush_initializers(engine *engine)
{
    if (!engine->modules.entities) {
        return FLOW_OK;
    }
    int result = entities_flush_initializers(
        engine->modules.entities,
        engine->timing.simulation_time
    );
    if (result == FLOW_ERR_STEP_BUDGET) {
        snprintf(engine->error, sizeof engine->error, "Spawn initialization exceeded its work budget.");
    }
    return result;
}

static int pump_events(engine *engine)
{
    int result = engine->modules.materials
                   ? materials_dispatch_events(engine->modules.materials)
                   : FLOW_OK;
    if (result < 0) {
        return result;
    }
    entities *entities = engine->modules.entities;
    if (!entities) {
        return FLOW_OK;
    }
    for (int batch = 0; batch < ENGINE_EVENT_BATCHES_MAX; batch++) {
        if (!entities_pending_events(entities)) {
            break;
        }
        CHIBI_TRACE_ZONE_BEGIN(dispatch_zone, "Contact batch dispatch");
        result = entities_dispatch_events(entities);
        CHIBI_TRACE_ZONE_END(dispatch_zone);
        if (result < 0) {
            return result;
        }
        CHIBI_TRACE_ZONE_BEGIN(lifecycle_zone, "Contact batch lifecycle");
        result = flush_initializers(engine);
        CHIBI_TRACE_ZONE_END(lifecycle_zone);
        if (result != 0 || engine_paused(engine)) {
            return result < 0 ? result : FLOW_PAUSED_RESULT;
        }
    }
    engine->timing.event_backlog = entities_pending_events(entities);
    return FLOW_OK;
}

static int propagate_transforms(engine *engine)
{
    CHIBI_TRACE_ZONE_BEGIN(zone, "Transform propagation");
    int result = transform_propagate(engine->world);
    CHIBI_TRACE_ZONE_END(zone);
    if (result != ECS_OK) {
        snprintf(engine->error, sizeof engine->error, "Transform propagation failed (%d).", result);
    }
    return result;
}

static int apply_step_commands(engine *engine)
{
    CHIBI_TRACE_ZONE_BEGIN(zone, "ECS command apply");
    int result = ecs_world_apply_commands(engine->world);
    CHIBI_TRACE_ZONE_END(zone);
    engine->timing.command_apply_count++;
    return result != ECS_OK ? result : maintain_bindings(engine);
}

static int finish_systems(engine *engine)
{
    int result = FLOW_OK;
    bool simulation = has_simulation(engine);
    if (engine->phase == STEP_IDLE) {
        assert(!simulation);
        engine->phase = STEP_COMMANDS;
    }
    if (engine->phase == STEP_BEHAVIORS) {
        result = sync_authored_systems(engine);
        if (result < 0) {
            return result;
        }
        engine->phase = STEP_PRE_PHYSICS;
    }
    if (!simulation && engine->phase == STEP_COMMANDS) {
        if ((result = apply_step_commands(engine)) != FLOW_OK) {
            return result;
        }
        engine->phase = STEP_PRE_PHYSICS;
    }
    if (engine->phase == STEP_PRE_PHYSICS) {
        result = run_system_phase(engine, SYSTEM_PRE_PHYSICS);
        if (result != FLOW_OK) {
            return result;
        }
        if (simulation) {
            struct timespec start, end;
            if (clock_now(&start) != 0) {
                return FLOW_ERR_STATE;
            }
            CHIBI_TRACE_ZONE_BEGIN(physics_zone, "Physics");
            result = engine->modules.physics
                       ? physics_tick(engine->modules.physics, (float)ENGINE_FIXED_SECONDS)
                       : FLOW_OK;
            CHIBI_TRACE_ZONE_END(physics_zone);
            if (result < 0) {
                snprintf(
                    engine->error,
                    sizeof engine->error,
                    "%s",
                    physics_error(engine->modules.physics)
                );
                return result;
            }
            CHIBI_TRACE_ZONE_BEGIN(physics3d_zone, "Physics 3D");
            result = engine->modules.physics3d
                       ? physics3d_tick(engine->modules.physics3d, (float)ENGINE_FIXED_SECONDS)
                       : FLOW_OK;
            CHIBI_TRACE_ZONE_END(physics3d_zone);
            if (result < 0) {
                snprintf(
                    engine->error,
                    sizeof engine->error,
                    "%s",
                    physics3d_error(engine->modules.physics3d)
                );
                return result;
            }
            if (clock_now(&end) != 0) {
                return FLOW_ERR_STATE;
            }
            engine->timing.physics_ms += elapsed_ms(start, end);
        }
        if ((result = propagate_transforms(engine)) < 0) {
            return result;
        }
        engine->phase = STEP_POST_PHYSICS;
    }
    if (engine->phase == STEP_POST_PHYSICS) {
        result = run_system_phase(engine, SYSTEM_POST_PHYSICS);
        if (result != FLOW_OK) {
            return result;
        }
        engine->phase = simulation ? STEP_CONTACTS : STEP_POST_EVENTS;
    }
    if (engine->phase == STEP_CONTACTS) {
        struct timespec start, end;
        if (clock_now(&start) != 0) {
            return FLOW_ERR_STATE;
        }
        CHIBI_TRACE_ZONE_BEGIN(events_zone, "Contact events");
        result = pump_events(engine);
        CHIBI_TRACE_ZONE_END(events_zone);
        if (clock_now(&end) != 0) {
            return FLOW_ERR_STATE;
        }
        engine->timing.events_ms += elapsed_ms(start, end);
        if (result != 0) {
            return result;
        }
        engine->phase = STEP_COMMANDS;
    }
    if (engine->phase == STEP_COMMANDS) {
        if ((result = apply_step_commands(engine)) != FLOW_OK) {
            return result;
        }
        engine->phase = STEP_POST_EVENTS;
    }
    CHIBI_TRACE_ZONE_BEGIN(systems_zone, "ECS systems");
    if (engine->phase == STEP_POST_EVENTS) {
        result = run_system_phase(engine, SYSTEM_POST_EVENTS);
        if (result != FLOW_OK) {
            CHIBI_TRACE_ZONE_END(systems_zone);
            return result;
        }
        engine->phase = STEP_PRESENTATION;
    }
    assert(engine->phase == STEP_PRESENTATION);
    result = run_system_phase(engine, SYSTEM_PRESENTATION);
    if (result != FLOW_OK) {
        CHIBI_TRACE_ZONE_END(systems_zone);
        return result;
    }
    CHIBI_TRACE_ZONE_END(systems_zone);
    bool checkpoint = engine->modules.terrain_storage &&
                      terrain_storage_pending(engine->modules.terrain_storage);
    if (checkpoint && (result = apply_step_commands(engine)) != FLOW_OK) {
        return result;
    }
    /* All authored phases have finished and structural commands are published.
     * Checkpoints include this step's edits without reentering an ECS query.
     * Prepare new terrain tiles before propagating their render transforms. */
    if (checkpoint) {
        terrain_storage_flush(engine->modules.terrain_storage);
    }
    if ((result = propagate_transforms(engine)) < 0) {
        return result;
    }
    if (engine->emitters && !particle_emitters_collect(
                                engine->emitters,
                                engine->world,
                                NULL,
                                engine->timing.simulation_time,
                                engine->error,
                                sizeof engine->error
                            )) {
        return FLOW_ERR_NODE;
    }
    if (engine->modules.visibility) {
        const char *error = visibility_update(engine->modules.visibility, ENGINE_FIXED_SECONDS);
        if (error) {
            snprintf(engine->error, sizeof engine->error, "%s", error);
            return FLOW_ERR_NODE;
        }
    }
    engine->phase = STEP_IDLE;
    return FLOW_OK;
}

static int finish_step(engine *engine)
{
    struct timespec start, end;
    if (clock_now(&start) != 0) {
        return FLOW_ERR_STATE;
    }
    int result = finish_systems(engine);
    if (clock_now(&end) != 0) {
        return FLOW_ERR_STATE;
    }
    engine->timing.systems_ms += elapsed_ms(start, end);
    return result;
}

static int continue_step(engine *engine)
{
    engine_modules *live = &engine->modules;
    if (!has_simulation(engine) || system_stage(engine)) {
        /* A held scheduler phase owns its remaining rows and unpublished
         * reductions. Resume it before pumping unrelated source work. */
        if (system_stage(engine)) {
            int result = sync_authored_systems(engine);
            if (result < 0) {
                return result;
            }
            if (!schedule_paused(engine->systems) && live->sources) {
                /* Changed descriptors restart this phase. Its old private
                 * contributions must not outlive the scheduler cursor. */
                source_cancel_systems(live->sources);
            }
        }
        return finish_step(engine);
    }
    boundary(engine);
    if (engine->phase == STEP_IDLE && sync_authored_systems(engine) < 0) {
        return FLOW_ERR_COMPILE;
    }
    int result = flush_initializers(engine);
    if (result != 0) {
        return result < 0 ? result : FLOW_OK;
    }
    if (engine->phase == STEP_IDLE) {
        struct timespec start, end;
        if (clock_now(&start) != 0) {
            return FLOW_ERR_STATE;
        }
        CHIBI_TRACE_ZONE_BEGIN(visual_zone, "Visual systems");
        result = live->particles
                   ? particles_tick(live->particles, ENGINE_FIXED_SECONDS)
                   : FLOW_OK;
        if (clock_now(&end) != 0) {
            CHIBI_TRACE_ZONE_END(visual_zone);
            return FLOW_ERR_STATE;
        }
        CHIBI_TRACE_ZONE_END(visual_zone);
        engine->timing.systems_ms += elapsed_ms(start, end);
        if (result >= 0) {
            result = follow_input(engine);
        }
        if (result >= 0) {
            result = maintain_bindings(engine);
        }
        if (result >= 0 && engine->input) {
            CHIBI_TRACE_ZONE_BEGIN(input_zone, "Input actions");
            result = engine_input_apply(engine->input);
            CHIBI_TRACE_ZONE_END(input_zone);
            if (result < 0) {
                const char *message = engine_input_error(engine->input);
                snprintf(
                    engine->error,
                    sizeof engine->error,
                    "%s",
                    message && *message ? message : "Input event dispatch failed"
                );
            }
        }
        if (result >= 0) {
            CHIBI_TRACE_ZONE_BEGIN(events_zone, "Input events");
            result = live->entities ? entities_dispatch_events(live->entities) : FLOW_OK;
            CHIBI_TRACE_ZONE_END(events_zone);
        }
        if (result >= 0) {
            result = maintain_bindings(engine);
        }
        if (result >= 0) {
            CHIBI_TRACE_ZONE_BEGIN(behavior_zone, "Behavior update");
            result = update_sources(engine);
            CHIBI_TRACE_ZONE_END(behavior_zone);
        }
        if (result >= 0) {
            CHIBI_TRACE_ZONE_BEGIN(behavior_pump_zone, "Behavior pump");
            result = pump_sources(engine);
            CHIBI_TRACE_ZONE_END(behavior_pump_zone);
        }
        if (result >= 0) {
            CHIBI_TRACE_ZONE_BEGIN(events_zone, "Behavior events");
            result = live->entities ? entities_dispatch_events(live->entities) : FLOW_OK;
            CHIBI_TRACE_ZONE_END(events_zone);
        }
        engine->phase = STEP_BEHAVIORS;
    } else {
        /* Resume a held activation before completing its remaining step stages. */
        CHIBI_TRACE_ZONE_BEGIN(behavior_pump_zone, "Behavior pump");
        result = pump_sources(engine);
        CHIBI_TRACE_ZONE_END(behavior_pump_zone);
    }
    if (result < 0) {
        return result;
    }
    result = flush_initializers(engine);
    if (result != 0 || engine_paused(engine)) {
        return result < 0 ? result : FLOW_OK;
    }
    if ((result = maintain_bindings(engine)) < 0) {
        return result;
    }
    return finish_step(engine);
}

static int fixed_step(engine *engine)
{
    CHIBI_TRACE_ZONE_BEGIN(zone, "Fixed step");
    engine->timing.simulation_steps++;
    engine->timing.simulation_time = engine->timing.simulation_steps * ENGINE_FIXED_SECONDS;
    engine->timing.steps++;
    int result = continue_step(engine);
    CHIBI_TRACE_ZONE_END(zone);
    return result;
}

static void discard_backlog(engine *engine)
{
    double whole = floor((engine->accumulator + 1e-10) / ENGINE_FIXED_SECONDS) *
                   ENGINE_FIXED_SECONDS;
    engine->timing.dropped_seconds += whole;
    engine->accumulator = fmax(0, engine->accumulator - whole);
}

static int advance_frame(engine *engine, double monotonic_time)
{
    if (!engine || !isfinite(monotonic_time)) {
        return FLOW_ERR_ARGUMENT;
    }
    struct timespec start, end;
    if (clock_now(&start) != 0) {
        return FLOW_ERR_STATE;
    }
    double elapsed = engine->has_time ? fmax(0, monotonic_time - engine->last_time)
                                      : 0;
    if (!isfinite(elapsed)) {
        return FLOW_ERR_ARGUMENT;
    }
    engine->last_time = engine->has_time ? fmax(engine->last_time, monotonic_time)
                                         : monotonic_time;
    engine->has_time = true;
    engine->timing.steps = 0;
    engine->timing.behavior_ms = 0;
    engine->timing.rendering_ms = 0;
    engine->timing.systems_ms = 0;
    engine->timing.physics_ms = 0;
    engine->timing.events_ms = 0;
    if (has_simulation(engine)) {
        boundary(engine);
    }
    int result = engine->modules.entities ? entities_flush(engine->modules.entities)
                                          : FLOW_OK;
    if (result < 0) {
        return result;
    }
    bool paused = engine_paused(engine);
    if (!paused && engine->debug_paused) {
        engine->timing.dropped_seconds += elapsed + engine->accumulator;
        engine->accumulator = 0;
        elapsed = 0;
    }
    if (!paused &&
        (engine->playing || engine->step_requested || engine->phase != STEP_IDLE)) {
        if (!system_stage(engine)) {
            result = flush_initializers(engine);
            if (result < 0) {
                return result;
            }
        }
        paused = engine_paused(engine);
        if (!result && !paused && engine->phase != STEP_IDLE) {
            result = continue_step(engine);
            if (result < 0) {
                return result;
            }
            paused = engine_paused(engine);
        }
    }
    if (!paused && engine->phase == STEP_IDLE &&
        (engine->playing || engine->step_requested)) {
        if (engine->playing) {
            engine->timing.dropped_seconds += fmax(0, elapsed - ENGINE_FRAME_SECONDS_MAX);
            engine->accumulator += fmin(elapsed, ENGINE_FRAME_SECONDS_MAX) *
                                   engine->time_scale;
        }
        int count = engine->step_requested
                      ? 1
                      : (int)floor((engine->accumulator + 1e-10) / ENGINE_FIXED_SECONDS);
        if (count > ENGINE_STEPS_MAX) {
            count = ENGINE_STEPS_MAX;
        }
        for (int i = 0; i < count; i++) {
            result = fixed_step(engine);
            if (engine->playing) {
                engine->accumulator = fmax(0, engine->accumulator - ENGINE_FIXED_SECONDS);
            }
            engine->step_requested = false;
            if (result < 0) {
                return result;
            }
            if (result == FLOW_PAUSED_RESULT || engine_paused(engine)) {
                break;
            }
        }
        discard_backlog(engine);
    } else if (paused) {
        engine->timing.dropped_seconds += elapsed + engine->accumulator;
        engine->accumulator = 0;
    }
    engine->debug_paused = engine_paused(engine);
    if (engine->modules.visibility &&
        engine->timing.simulation_steps == engine->scene_steps) {
        const char *error = visibility_update(engine->modules.visibility, 0);
        if (error) {
            snprintf(engine->error, sizeof engine->error, "%s", error);
            return FLOW_ERR_NODE;
        }
    }
    engine->timing.alpha = engine->playing && !engine->debug_paused
                             ? (float)(engine->accumulator / ENGINE_FIXED_SECONDS)
                             : 1;
    if (clock_now(&end) != 0) {
        return FLOW_ERR_STATE;
    }
    engine->timing.systems_ms = fmax(
        0,
        engine->timing.systems_ms - engine->timing.physics_ms - engine->timing.events_ms
    );
    engine->timing.behavior_ms = fmax(
        0,
        elapsed_ms(start, end) - engine->timing.systems_ms - engine->timing.physics_ms
    );
    start = end;
    if (engine->render) {
        CHIBI_TRACE_ZONE_BEGIN(render_zone, "Render submission");
        engine->render(engine->render_user);
        CHIBI_TRACE_ZONE_END(render_zone);
    }
    if (clock_now(&end) != 0) {
        return FLOW_ERR_STATE;
    }
    engine->timing.rendering_ms = elapsed_ms(start, end);
    engine->timing.frame++;
    return FLOW_OK;
}

int engine_frame(engine *engine, double monotonic_time)
{
    if (!engine) {
        return FLOW_ERR_ARGUMENT;
    }
    CHIBI_TRACE_ZONE_BEGIN(zone, "Engine frame");
    engine->error[0] = 0;
    int result = advance_frame(engine, monotonic_time);
    CHIBI_TRACE_FRAME_MARK("Engine frame");
    CHIBI_TRACE_PLOT("Engine fixed steps", engine->timing.steps);
    CHIBI_TRACE_PLOT("Engine event backlog", engine->timing.event_backlog);
    CHIBI_TRACE_ZONE_END(zone);
    if (result < 0 && !engine->error[0]) {
        snprintf(engine->error, sizeof engine->error, "Simulation step failed (%d).", result);
    }
    return result;
}

const char *engine_error(const engine *engine)
{
    return engine->error[0] ? engine->error : NULL;
}

const engine_frame_timing *engine_last_timing(const engine *engine)
{
    return engine ? &engine->timing : NULL;
}

void engine_set_behavior_profiling(engine *engine, bool enabled)
{
    if (engine->modules.sources) {
        source_profile_enable(engine->modules.sources, enabled);
    }
}

GlobalTransform2D engine_render_transform(const engine *engine, const ecs_world *world, ecs_entity entity)
{
    if (!engine) {
        return transform_world_2d(world, entity);
    }
    const GlobalTransform2D *global = GlobalTransform2D_get_const(world, entity);
    assert(global);
    GlobalTransform2D shown = *global;
    if (!engine->modules.physics) {
        return shown;
    }
    for (ecs_entity at = entity; at.slot; at = ecs_parent(world, at)) {
        CF_V2 offset;
        if (physics_interpolation_offset(
                engine->modules.physics,
                at,
                engine->timing.alpha,
                &offset
            )) {
            shown.position.x += offset.x;
            shown.position.y += offset.y;
            break;
        }
    }
    return shown;
}

GlobalTransform3D engine_render_transform_3d(const engine *engine, const ecs_world *world, ecs_entity entity)
{
    if (!engine) {
        return transform_world_3d(world, entity);
    }
    const GlobalTransform3D *global = GlobalTransform3D_get_const(world, entity);
    assert(global);
    GlobalTransform3D shown = *global;
    if (!engine->modules.physics3d) {
        return shown;
    }
    for (ecs_entity at = entity; at.slot; at = ecs_parent(world, at)) {
        CF_Quat rotation;
        CF_V3 translation;
        if (physics3d_interpolation(
                engine->modules.physics3d,
                at,
                engine->timing.alpha,
                &rotation,
                &translation
            )) {
            shown.x_axis = cf_mul_q_v3(rotation, shown.x_axis);
            shown.y_axis = cf_mul_q_v3(rotation, shown.y_axis);
            shown.z_axis = cf_mul_q_v3(rotation, shown.z_axis);
            shown.position = cf_add_v3(cf_mul_q_v3(rotation, shown.position), translation);
            break;
        }
    }
    return shown;
}
