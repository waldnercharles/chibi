#include "authoring/engine_preview.h"
#include "authoring/native_prototype.h"
#include "authoring/preview.h"
#include "graph/document/serialize.h"
#include "runtime/component_catalog.h"
#include "runtime/engine.h"
#include "support/array.h"
#include "support/text_file.h"
#include "test/support/code_fixture.h"
#include "test/support/event_fixture.h"
#include "test/support/simulation_fixture.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    ecs_world *world;
    preview_world *live;
    engine *runtime;
    simulation_fixture simulation;
    ecs_entity player;
} dash_fixture;

static void load_behavior(preview_world *live, const char *name)
{
    char path[256];
    const char *root = !strcmp(name, "DashBehavior")
                         ? "test/fixtures/content/behaviors"
                         : "content/behaviors";
    snprintf(path, sizeof path, "%s/%s.chibi", root, name);
    char *text = text_file_read(path);
    assert(text);
    test_source_fixture fixture = test_source_prepare(path, text);
    free(text);
    test_source_register(live->runtime, &fixture);
    test_source_free(&fixture);
}

static ecs_entity body(dash_fixture *f, float x, float y, int type, unsigned category, unsigned mask)
{
    ecs_entity entity = ecs_world_create_entity(f->world);
    assert(Transform_add(f->world, entity, 0) == ECS_OK);
    assert(Velocity_add(f->world, entity, 0) == ECS_OK);
    assert(Body_add(f->world, entity, 0) == ECS_OK);
    assert(Collider_add(f->world, entity, 0) == ECS_OK);
    assert(Orientation_add(f->world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f->world) == ECS_OK);
    *Transform_get(f->world, entity) = (Transform){ x, y };
    *Body_get(f->world, entity) = (Body){ .type = type, .continuous = true };
    *Collider_get(f->world, entity) = (Collider){ .radius = 12,
                                                  .category = category,
                                                  .mask = mask };
    return entity;
}

static void add_health(dash_fixture *f, ecs_entity entity, int amount)
{
    assert(Health_add(f->world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f->world) == ECS_OK);
    *Health_get(f->world, entity) = (Health){ amount, amount };
    assert(
        source_attach(
            f->live->runtime->sources,
            entity,
            "HealthBehavior",
            f->live->runtime->deferred,
            f->live->runtime->now,
            f->live->runtime->delta
        ) == 0
    );
}

static dash_fixture create()
{
    dash_fixture f = { .world = ecs_world_new() };
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(f.world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    f.live = preview_world_new(f.world);
    register_damage_event(f.live);
    const char *names[] = { "DashBehavior",
                            "TwinStickInputBehavior",
                            "PlayerContactBehavior",
                            "HealthBehavior" };
    for (int i = 0; i < 4; i++) {
        load_behavior(f.live, names[i]);
    }
    f.runtime = authoring_engine_new(f.world, f.live->runtime);
    engine_set_focus(f.runtime, ENGINE_FOCUS_VIEWPORT);
    f.simulation = simulation_fixture_new(f.runtime, NULL);
    f.player = body(&f, 0, 0, BODY_DYNAMIC, 1, 2 | 8);
    assert(Input_add(f.world, f.player, 0) == ECS_OK);
    assert(Movement_add(f.world, f.player, 0) == ECS_OK);
    assert(Aim_add(f.world, f.player, 0) == ECS_OK);
    assert(Dash_add(f.world, f.player, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    *Movement_get(f.world, f.player) = (Movement){ .speed = 180 };
    *Aim_get(f.world, f.player) = (Aim){ .direction = { 1, 0 } };
    *Dash_get(f.world, f.player) = (Dash){ .speed = 720,
                                           .duration = .16f,
                                           .cooldown = .7f,
                                           .damage = 40,
                                           .direction = { 1, 0 } };
    add_health(&f, f.player, 100);
    assert(
        source_attach(
            f.live->runtime->sources,
            f.player,
            "TwinStickInputBehavior",
            f.live->runtime->deferred,
            f.live->runtime->now,
            f.live->runtime->delta
        ) == 0
    );
    assert(
        source_attach(
            f.live->runtime->sources,
            f.player,
            "PlayerContactBehavior",
            f.live->runtime->deferred,
            f.live->runtime->now,
            f.live->runtime->delta
        ) == 0
    );
    assert(
        source_attach(
            f.live->runtime->sources,
            f.player,
            "DashBehavior",
            f.live->runtime->deferred,
            f.live->runtime->now,
            f.live->runtime->delta
        ) == 0
    );
    simulation_frame(&f.simulation, 0);
    return f;
}

static void dispose(dash_fixture *f)
{
    engine_free(f->runtime);
    preview_world_free(f->live);
    ecs_world_free(f->world);
}

static void damage(dash_fixture *f, ecs_entity target, ecs_entity source, int amount)
{
    pin_value values[] = { { .type = PIN_I32, .type_name = "int", .i = amount },
                           { .type = PIN_HANDLE,
                             .type_name = "ecs.entity",
                             .u64 = ecs_entity_to_u64(source) } };
    assert(
        entities_request_event(
            f->live->runtime->entities,
            target,
            "Damage",
            (execution_arguments){ values, 2 }
        ) == 0
    );
}

static void direction_and_timing()
{
    dash_fixture f = create();
    engine_sample_movement(f.runtime, 1, 0);
    engine_sample_aim(f.runtime, (CF_V2){ 0, 1 }, false, false);
    simulation_action(&f.simulation, "Dash");
    Dash *dash = Dash_get(f.world, f.player);
    assert(dash->remaining == dash->duration && dash->cooldown_remaining == dash->cooldown);
    assert(dash->direction.x == 1 && dash->direction.y == 0);
    assert(fabsf(Transform_get(f.world, f.player)->x - 12) < .01f);
    engine_sample_movement(f.runtime, -1, 0);
    engine_sample_aim(f.runtime, (CF_V2){ 0, -1 }, false, false);
    float previous = dash->remaining;
    simulation_action(&f.simulation, "Dash");
    dash = Dash_get(f.world, f.player);
    assert(dash->remaining < previous && dash->direction.x == 1);
    assert(Velocity_get(f.world, f.player)->x > 719);
    assert(fabsf(Orientation_get(f.world, f.player)->radians) < .001f);
    damage(&f, f.player, f.player, 17);
    simulation_step(&f.simulation);
    assert(Health_get(f.world, f.player)->current == 100);
    simulation_advance(&f.simulation, .2);
    dash = Dash_get(f.world, f.player);
    assert(dash->remaining == 0 && dash->cooldown_remaining > .49f);
    assert(fabsf(Velocity_get(f.world, f.player)->x + 180) < .01f);
    damage(&f, f.player, f.player, 17);
    simulation_step(&f.simulation);
    assert(Health_get(f.world, f.player)->current == 83);
    simulation_advance(&f.simulation, .75);
    engine_sample_movement(f.runtime, 0, 0);
    engine_sample_aim(f.runtime, (CF_V2){ 0, 1 }, false, false);
    simulation_action(&f.simulation, "Dash");
    dash = Dash_get(f.world, f.player);
    assert(dash->remaining > .15f && fabsf(dash->direction.y - 1) < .001f);
    // Losing local input during a burst must restore zero velocity at its end.
    assert(Input_remove(f.world, f.player, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    simulation_advance(&f.simulation, .96);
    const Velocity *velocity = Velocity_get_const(f.world, f.player);
    assert(fabsf(velocity->x) + fabsf(velocity->y) < .001f);
    dispose(&f);
}

static void cooldown_boundaries()
{
    dash_fixture f = create();
    engine_sample_movement(f.runtime, 1, 0);
    simulation_action(&f.simulation, "Dash");
    const double started = f.simulation.time;
    // Stop one boundary before expiry; action delivery precedes timer Tick.
    simulation_advance(&f.simulation, started + .7 - 2 * ENGINE_FIXED_SECONDS);
    engine_sample_movement(f.runtime, -1, 0);
    simulation_action(&f.simulation, "Dash");
    const Dash *dash = Dash_get_const(f.world, f.player);
    assert(dash->remaining == 0 && dash->direction.x == 1);
    assert(fabs(f.simulation.time - started - .7 + ENGINE_FIXED_SECONDS) < 1e-9);
    simulation_action(&f.simulation, "Dash");
    dash = Dash_get_const(f.world, f.player);
    assert(fabs(f.simulation.time - started - .7) < 1e-9);
    assert(dash->remaining == dash->duration && dash->direction.x == -1);
    assert(dash->cooldown_remaining == dash->cooldown);
    // Another edge next tick must not restart the accepted burst.
    engine_sample_movement(f.runtime, 1, 0);
    simulation_action(&f.simulation, "Dash");
    dash = Dash_get_const(f.world, f.player);
    assert(dash->remaining < dash->duration && dash->direction.x == -1);
    dispose(&f);
}

static void active_burst_cannot_restart()
{
    dash_fixture f = create();
    Dash_get(f.world, f.player)->cooldown = .02f;
    ecs_entity enemy = body(&f, 25, 0, BODY_STATIC, 2, 1);
    add_health(&f, enemy, 160);
    engine_sample_movement(f.runtime, 1, 0);
    simulation_action(&f.simulation, "Dash");
    assert(Health_get(f.world, enemy)->current == 120);
    engine_sample_movement(f.runtime, -1, 0);
    for (int i = 0; i < 5; i++) {
        float before = Dash_get(f.world, f.player)->remaining;
        simulation_action(&f.simulation, "Dash");
        const Dash *dash = Dash_get_const(f.world, f.player);
        assert(dash->remaining > 0 && dash->remaining < before);
        assert(dash->direction.x == 1 && dash->direction.y == 0);
        assert(Health_get(f.world, enemy)->current == 120);
    }
    assert(Dash_get(f.world, f.player)->cooldown_remaining == 0);
    dispose(&f);
}

static void held_contact()
{
    dash_fixture f = create();
    ecs_entity enemy = body(&f, 25, 0, BODY_STATIC, 2, 1);
    add_health(&f, enemy, 160);
    engine_sample_movement(f.runtime, 1, 0);
    simulation_advance(&f.simulation, .2);
    assert(Health_get(f.world, f.player)->current == 90);
    // The contact already fired and its shapes have been separated by the solver.
    float separation = 25 - Transform_get(f.world, f.player)->x;
    assert(separation >= 23.5f && separation <= 24.7f);
    *Health_get(f.world, f.player) = (Health){ 100, 100 };
    simulation_action(&f.simulation, "Dash");
    assert(Health_get(f.world, enemy)->current == 120);
    assert(Health_get(f.world, f.player)->current == 100);
    // Repeated delivery in the same activation must not damage this target twice.
    pin_value other = { .type = PIN_HANDLE,
                        .type_name = "ecs.entity",
                        .u64 = ecs_entity_to_u64(enemy) };
    assert(
        entities_request_event(
            f.live->runtime->entities,
            f.player,
            "Contact Begin",
            (execution_arguments){ &other, 1 }
        ) == 0
    );
    simulation_advance(&f.simulation, .4);
    assert(Health_get(f.world, enemy)->current == 120);
    simulation_advance(&f.simulation, 1);
    simulation_action(&f.simulation, "Dash");
    assert(Health_get(f.world, enemy)->current == 80);
    dispose(&f);
}

static ecs_entity wall(dash_fixture *f, float x, float y, float width, float height, float angle)
{
    /* Continuous on a static wall must not remove it from the player's CCD. */
    ecs_entity entity = body(f, x, y, BODY_STATIC, 8, 1);
    Collider *collider = Collider_get(f->world, entity);
    collider->shape = COLLIDER_BOX;
    collider->width = width;
    collider->height = height;
    Orientation_get(f->world, entity)->radians = angle;
    return entity;
}

static void thin_wall(float angle)
{
    dash_fixture f = create();
    wall(&f, 48, 0, 1, 400, angle);
    ecs_entity enemy = body(&f, 90, 0, BODY_STATIC, 2, 1);
    add_health(&f, enemy, 40);
    engine_sample_movement(f.runtime, 1, 0);
    simulation_action(&f.simulation, "Dash");
    for (int i = 0; i < 15; i++) {
        const Transform *position = Transform_get_const(f.world, f.player);
        float normal_distance = (position->x - 48) * cosf(angle) +
                                position->y * sinf(angle);
        assert(normal_distance <= -11.8f);
        simulation_step(&f.simulation);
    }
    assert(ecs_entity_is_alive(f.world, enemy));
    assert(Health_get(f.world, enemy)->current == 40);
    assert(Dash_get(f.world, f.player)->remaining == 0);
    dispose(&f);
}

static void corner()
{
    dash_fixture f = create();
    wall(&f, 40, 0, 1, 400, 0);
    wall(&f, 0, 40, 400, 1, 0);
    engine_sample_movement(f.runtime, 1, 1);
    simulation_action(&f.simulation, "Dash");
    for (int i = 0; i < 15; i++) {
        const Transform *position = Transform_get_const(f.world, f.player);
        assert(position->x < 28.2f && position->y < 28.2f);
        simulation_step(&f.simulation);
    }
    const Transform *position = Transform_get_const(f.world, f.player);
    assert(position->x > 26 && position->y > 26);
    dispose(&f);
}

static Transform cadence(int display_hz)
{
    dash_fixture f = create();
    ecs_entity enemy = body(&f, 72, 0, BODY_STATIC, 2, 1);
    add_health(&f, enemy, 40);
    assert(engine_dispatch_input(f.runtime, "Dash", 1, 1, NULL) >= 0);
    assert(engine_dispatch_input(f.runtime, "Dash", 0, 0, NULL) >= 0);
    for (int i = 1; i <= display_hz; i++) {
        simulation_frame(&f.simulation, (double)i / display_hz);
    }
    assert(engine_last_timing(f.runtime)->simulation_steps == 60);
    assert(!ecs_entity_is_alive(f.world, enemy));
    assert(Health_get(f.world, f.player)->current == 100);
    Transform position = *Transform_get(f.world, f.player);
    assert(position.x > 90 && fabsf(position.y) < .001f);
    assert(Dash_get(f.world, f.player)->remaining == 0);
    dispose(&f);
    return position;
}

static void preview_restart()
{
    dash_fixture definitions = create();
    prototype_registry registry = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT };
    prototype_preview *preview = prototype_preview_new(&registry);
    register_test_preview_catalog(preview);
    for (int i = 0; i < preview_definition_count(definitions.live); i++) {
        preview_definition *definition = preview_definition_at(definitions.live, i);
        if (definition->working->domain != BEHAVIOR_DOMAIN_EVENT) {
            continue;
        }
        assert(
            prototype_preview_register_behavior(
                preview,
                definition->name,
                definition->working
            ) == 0
        );
    }
    char *text = text_file_read("content/prototypes/ArenaPlayer.kdl");
    char error[256];
    prototype *prototype = prototype_load(text, &registry, error, sizeof error);
    free(text);
    assert(prototype);
    assert(prototype_add_native_component(prototype, &COMPONENT_Dash) == 0);
    for (int i = 0; i < acount(prototype->components); i++) {
        prototype_component *component = &prototype->components[i];
        if (component->desc->id == COMPONENT_Dash.id) {
            *(
                 Dash *
            )component->data = *Dash_get_const(definitions.world, definitions.player);
            break;
        }
    }
    assert(prototype_add_graph(prototype, "DashBehavior") == 0);
    for (int i = acount(prototype->graphs) - 1; i >= 0; i--) {
        const char *name = prototype->graphs[i];
        if (strcmp(name, "DashBehavior") && strcmp(name, "TwinStickInputBehavior") &&
            strcmp(name, "PlayerContactBehavior") && strcmp(name, "HealthBehavior")) {
            assert(prototype_remove_graph(prototype, name) == 0);
        }
    }
    assert(prototype_preview_launch(preview, prototype, error, sizeof error) == 0);
    engine *runtime = prototype_preview_runtime(preview);
    ecs_world *world = prototype_preview_world(preview);
    ecs_entity entity = prototype_preview_entity(preview);
    simulation_fixture clock = simulation_fixture_new(runtime, preview);
    simulation_frame(&clock, 0);
    simulation_action(&clock, "Dash");
    float remaining = Dash_get(world, entity)->remaining;
    assert(remaining > 0);
    assert(engine_set_playing(runtime, false) == 0);
    simulation_frame(&clock, 1);
    assert(Dash_get(world, entity)->remaining == remaining);
    assert(engine_step(runtime) == 0);
    simulation_frame(&clock, 1);
    assert(
        fabsf(Dash_get(world, entity)->remaining - remaining + (float)ENGINE_FIXED_SECONDS) < .0001f
    );
    prototype_preview_stop(preview);
    assert(prototype_preview_launch(preview, prototype, error, sizeof error) == 0);
    ecs_entity restarted = prototype_preview_entity(preview);
    assert(ecs_entity_to_u64(restarted) != ecs_entity_to_u64(entity));
    const Dash *dash = Dash_get_const(world, restarted);
    assert(dash->remaining == 0 && dash->cooldown_remaining == 0);
    clock = simulation_fixture_new(runtime, preview);
    simulation_frame(&clock, 2);
    simulation_action(&clock, "Dash");
    assert(Dash_get(world, restarted)->remaining > .15f);
    prototype_preview_free(preview);
    prototype_free(prototype);
    dispose(&definitions);
}

/* Input recorded at an uneven frame rate replays exactly at another, and
 * live samples cannot disturb the replay. */
static void recorded_input_replays()
{
    dash_fixture f = create();
    engine_record_input(f.runtime, 7);
    static const double intervals[] = { .004, .031, .017, .05, .009, .022 };
    for (int i = 0; i < 90; i++) {
        float turn = (float)(i / 15) * .7f;
        engine_sample_movement(f.runtime, cosf(turn), sinf(turn));
        engine_sample_aim(f.runtime, (CF_V2){ -sinf(turn), cosf(turn) }, i % 30 < 12, false);
        simulation_frame(&f.simulation, f.simulation.time + intervals[i % 6]);
    }
    engine_stop_recording(f.runtime);
    int count;
    const input_keyframe *recorded = engine_recorded_input(f.runtime, &count);
    assert(count > 6 && recorded[0].step == 0);
    input_keyframe *keys = NULL;
    for (int i = 0; i < count; i++) {
        apush(keys, recorded[i]);
    }
    uint64_t steps = engine_last_timing(f.runtime)->simulation_steps;
    Transform position = *Transform_get_const(f.world, f.player);
    Aim aim = *Aim_get_const(f.world, f.player);
    dispose(&f);

    f = create();
    engine_replay_input(f.runtime, keys, count, 7);
    while (engine_last_timing(f.runtime)->simulation_steps < steps) {
        engine_sample_movement(f.runtime, -1, 0);
        simulation_step(&f.simulation);
    }
    assert(engine_last_timing(f.runtime)->simulation_steps == steps);
    const Transform *replayed = Transform_get_const(f.world, f.player);
    assert(replayed->x == position.x && replayed->y == position.y);
    const Aim *replayed_aim = Aim_get_const(f.world, f.player);
    assert(replayed_aim->direction.x == aim.direction.x && replayed_aim->firing == aim.firing);
    assert(position.x != 0 || position.y != 0);
    engine_stop_replay(f.runtime);
    assert(!engine_replaying_input(f.runtime));
    afree(keys);
    dispose(&f);
}

void test_dash()
{
    recorded_input_replays();
    direction_and_timing();
    cooldown_boundaries();
    active_burst_cannot_restart();
    preview_restart();
    held_contact();
    thin_wall(0);
    thin_wall(.38f);
    thin_wall(-.61f);
    corner();
    Transform a = cadence(30), b = cadence(60), c = cadence(120);
    assert(fabsf(a.x - b.x) < .001f && fabsf(a.x - c.x) < .001f);
    puts("  dash: recorded input replays, captured input, cooldown, invulnerability, held contact, thin/rotated walls, corner, 30/60/120 cadence PASS");
}
