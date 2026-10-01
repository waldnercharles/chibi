#include "editor/workspace/movement_input.h"
#include "graph/document/serialize.h"
#include "render/particles.h"
#include "runtime/behaviors/live.h"
#include "runtime/engine.h"
#include "runtime/generated.h"
#include "runtime/scenes/prototype_preview.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "test/support/event_fixture.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_content(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    char *text = xmalloc((size_t)size + 1);
    assert(fread(text, 1, (size_t)size, file) == (size_t)size);
    text[size] = 0;
    fclose(file);
    return text;
}

static behavior_asset *read_behavior(const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "test/fixtures/content/behaviors/%s.kdl", name);
    char *text = read_content(path);
    behavior_asset *asset = behavior_asset_load(text);
    if (asset) {
        asset = link_test_libraries(asset);
    }
    free(text);
    assert(asset);
    compile_report report;
    compile_report_init(&report);
    int errors = behavior_asset_compile(asset, &report);
    for (int i = 0; i < acount(report.items); i++) {
        fprintf(stderr, "%s: %s\n", path, report.items[i].message);
    }
    assert(errors == 0);
    compile_report_free(&report);
    return asset;
}

static int attach(ecs_world *, ecs_entity entity, const char *name, void *user)
{
    return game_behavior_attach(user, entity, name);
}

static void trail_case(bool isolated)
{
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    char *text = read_content("test/fixtures/content/prototypes/Player.kdl");
    char error[256];
    game_prototype *prototype = game_prototype_load(text, &registry, error, sizeof error);
    free(text);
    assert(prototype);
    for (int i = acount(prototype->graphs) - 1; i >= 0; i--) {
        const char *name = prototype->graphs[i];
        if (strcmp(name, "InputBehavior") && strcmp(name, "MovementTrailBehavior")) {
            assert(game_prototype_remove_graph(prototype, name) == 0);
        }
    }
    game_prototype_preview *preview = isolated ? game_prototype_preview_new(&registry)
                                               : NULL;
    register_test_preview_catalog(preview);
    ecs_world *world = preview ? game_prototype_preview_world(preview)
                               : ecs_world_new();
    game_live_world *live = preview ? NULL : game_live_world_new(world);
    const char *names[] = { "InputBehavior", "MovementTrailBehavior" };
    for (int i = 0; i < 2; i++) {
        behavior_asset *asset = read_behavior(names[i]);
        if (preview) {
            assert(game_prototype_preview_register_behavior(preview, names[i], asset) == 0);
            behavior_asset_free(asset);
        } else {
            assert(
                game_live_add_definition(live, game_live_definition_new(names[i], asset)) == 0
            );
        }
    }
    ecs_entity entity;
    if (preview) {
        assert(game_prototype_preview_launch(preview, prototype, error, sizeof error) == 0);
        entity = game_prototype_preview_entity(preview);
    } else {
        for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
            assert(
                ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK
            );
        }
        assert(
            game_prototype_spawn(
                prototype,
                world,
                &(game_prototype_spawn_hooks){ attach, live->behaviors },
                &entity,
                error,
                sizeof error
            ) == 0
        );
    }
    game_behavior_world *behaviors = preview
                                       ? game_prototype_preview_behaviors(preview)
                                       : live->behaviors;
    game_particles *particles = preview
                                  ? (game_particles *)game_prototype_preview_particles(preview)
                                  : live->particles;
    for (int direction = 0; direction < 4; direction++) {
        game_particles_clear(particles);
        app_movement_input keys = app_movement_from_keys(
            direction == 0,
            direction == 1,
            direction == 2,
            direction == 3
        );
        CF_V2 vector = { keys.x, keys.y };
        pin_value input = { .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = &vector };
        GameTransform before = *GAME_GameTransform_get(world, entity);
        game_behavior_set_delta_seconds(behaviors, 1.0 / 64);
        assert(
            game_behavior_dispatch(behaviors, entity, "Input", (flow_inputs){ &input, 1, NULL }, NULL) >= 0
        );
        const GameTransform *after = GAME_GameTransform_get_const(world, entity);
        float speed = GAME_GameMovement_get_const(world, entity)->speed;
        const GameVelocity *velocity = GAME_GameVelocity_get_const(world, entity);
        assert(velocity && fabsf(velocity->x - vector.x * speed) < 0.0001f);
        assert(fabsf(velocity->y - vector.y * speed) < 0.0001f);
        assert(after->x == before.x && after->y == before.y);
        assert(game_particles_count(particles) == 1);
        const game_particle *particle = game_particles_data(particles);
        assert(fabsf(particle->position.x - after->x + vector.x * 9) < 0.0001f);
        assert(fabsf(particle->position.y - after->y + vector.y * 9) < 0.0001f);
        assert(particle->velocity.x * vector.x + particle->velocity.y * vector.y < 0);
        assert(particle->lifetime >= 0.2f && particle->lifetime <= 0.4f);
        assert(particle->size >= 2 && particle->size <= 4);
        assert(particle->color == 0xFF991AFFu && particle->end_color == 0x4D00FF80u);
        vector = (CF_V2){ 0 };
        assert(
            game_behavior_dispatch(behaviors, entity, "Input", (flow_inputs){ &input, 1, NULL }, NULL) >= 0
        );
        assert(game_particles_count(particles) == 1);
    }
    game_particles_clear(particles);
    game_behavior_set_delta_seconds(behaviors, 1.0 / 128);
    CF_V2 vector = { 1, 0 };
    pin_value input = { .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = &vector };
    for (int i = 0; i < 8; i++) {
        assert(
            game_behavior_dispatch(behaviors, entity, "Input", (flow_inputs){ &input, 1, NULL }, NULL) >= 0
        );
        assert(game_particles_count(particles) == (i + 1) / 2);
    }
    assert(game_particles_tick(particles, 0.5) == 0);
    assert(game_particles_count(particles) == 0);
    if (preview) {
        game_prototype_preview_free(preview);
    } else {
        game_live_world_free(live);
        ecs_world_free(world);
    }
    game_prototype_free(prototype);
}

void test_movement_trail()
{
    trail_case(false);
    trail_case(true);
    puts("movement trail PASS: keyboard direction sets velocity and rear exhaust, fractional emission and idle in both hosts");
}
