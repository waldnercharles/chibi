#include "graph/document/serialize.h"
#include "runtime/behaviors/live.h"
#include "runtime/engine.h"
#include "runtime/generated.h"
#include "runtime/scenes/prototype_preview.h"
#include "support/array.h"
#include "support/event_fixture.h"
#include "support/xalloc.h"
#include "test/support/event_fixture.h"
#include "test/support/simulation_fixture.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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

static behavior_asset *read_behavior(const char *path)
{
    char *text = read_content(path);
    behavior_asset *asset = behavior_asset_load(text);
    if (asset) {
        asset = link_test_libraries(asset);
    }
    free(text);
    if (!asset) {
        fprintf(stderr, "Could not load %s: %s\n", path, graph_last_error());
    }
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

static game_prototype *read_prototype(const char *path, const game_prototype_registry *registry)
{
    char *text = read_content(path);
    char error[256];
    game_prototype *prototype = game_prototype_load(text, registry, error, sizeof error);
    free(text);
    assert(prototype);
    return prototype;
}

static ecs_entity projectile_in(ecs_world *world)
{
    ecs_query query;
    ecs_component_id id = GAME_COMPONENT_GameProjectile.id;
    assert(ecs_query_begin(world, &query, &id, 1) == ECS_OK);
    ecs_entity result = { 0 }, found;
    while (ecs_query_next(&query, &found)) {
        assert(!result.slot);
        result = found;
    }
    ecs_query_end(&query);
    return result;
}

static int attach(ecs_world *, ecs_entity entity, const char *name, void *user)
{
    return game_behavior_attach(user, entity, name);
}

void test_projectile()
{
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    game_prototype *player = read_prototype("test/fixtures/content/prototypes/Player.kdl", &registry);
    game_prototype *projectile = read_prototype("test/fixtures/content/prototypes/Projectile.kdl", &registry);
    const char *names[] = { "InputBehavior",         "WeaponBehavior",
                            "AutoWeaponBehavior",    "ProjectileBehavior",
                            "HealthBehavior",        "ProjectileHitBehavior",
                            "PlayerContactBehavior", "MovementTrailBehavior" };
    for (int isolated = 0; isolated < 2; isolated++) {
        ecs_world *world = isolated ? NULL : ecs_world_new();
        game_live_world *live = NULL;
        engine *runtime = NULL;
        game_prototype_preview *preview = NULL;
        if (isolated) {
            preview = game_prototype_preview_new(&registry);
            register_test_preview_catalog(preview);
            world = game_prototype_preview_world(preview);
        } else {
            for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
                assert(
                    ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK
                );
            }
            live = game_live_world_new(world);
            register_damage_event(live);
            runtime = engine_new(world, live);
        }
        for (int i = 0; i < (int)(sizeof names / sizeof names[0]); i++) {
            char path[256];
            const char *root = !strcmp(names[i], "InputBehavior") ||
                                       !strcmp(names[i], "AutoWeaponBehavior") ||
                                       !strcmp(names[i], "MovementTrailBehavior") ||
                                       !strcmp(names[i], "WeaponBehavior")
                                 ? "test/fixtures/content/behaviors"
                                 : "content/behaviors";
            snprintf(path, sizeof path, "%s/%s.kdl", root, names[i]);
            behavior_asset *asset = read_behavior(path);
            if (isolated) {
                assert(game_prototype_preview_register_behavior(preview, names[i], asset) == 0);
                behavior_asset_free(asset);
            } else {
                assert(
                    game_live_add_definition(live, game_live_definition_new(names[i], asset)) == 0
                );
            }
        }
        char error[256];
        ecs_entity owner;
        if (isolated) {
            assert(game_prototype_preview_register_prototype(preview, projectile) == 0);
            assert(game_prototype_preview_launch(preview, player, error, sizeof error) == 0);
            owner = game_prototype_preview_entity(preview);

        } else {
            assert(game_entities_register(live->entities, projectile) == 0);
            assert(
                game_prototype_spawn(
                    player,
                    world,
                    &(game_prototype_spawn_hooks){ attach, live->behaviors },
                    &owner,
                    error,
                    sizeof error
                ) == 0
            );
            engine_set_focus(runtime, ENGINE_FOCUS_VIEWPORT);
            assert(engine_set_playing(runtime, true) == 0);
        }
        simulation_fixture clock = simulation_fixture_new(runtime, preview);
        simulation_advance(&clock, 0);
        simulation_action(&clock, "Fire");
        ecs_entity shot = projectile_in(world);
        assert(shot.slot && shot.slot != owner.slot);
        float origin = GAME_GameTransform_get_const(world, owner)->x;
        assert(fabsf(GAME_GameTransform_get_const(world, shot)->x - origin - 7.0f) < 0.01f);
        assert(
            GAME_GameCollider_get_const(world, shot)->ignore_entity ==
            ecs_entity_to_u64(owner)
        );
        assert(GAME_GameVelocity_get_const(world, shot)->x == 420);
        simulation_advance(&clock, 0.1);
        assert(fabsf(GAME_GameTransform_get_const(world, shot)->x - origin - 42.0f) < 0.01f);
        simulation_advance(&clock, 2.1);
        assert(!ecs_entity_is_alive(world, shot));
        assert(!projectile_in(world).slot);
        assert(
            game_prototype_write_field(
                projectile,
                "GameProjectile",
                "speed",
                (pin_value){ .type = PIN_F32, .f = 100.0f }
            ) == 0
        );
        assert(
            game_prototype_write_field(
                projectile,
                "GameProjectile",
                "lifetime",
                (pin_value){ .type = PIN_F32, .f = 0.2f }
            ) == 0
        );
        if (isolated) {
            assert(game_prototype_preview_register_prototype(preview, projectile) == 0);

        } else {
            assert(game_entities_register(live->entities, projectile) == 0);
        }
        simulation_action(&clock, "Fire");
        shot = projectile_in(world);
        assert(shot.slot);
        const GameProjectile *state = GAME_GameProjectile_get_const(world, shot);
        assert(state->speed == 100.0f && state->lifetime == 0.2f);
        assert(GAME_GameVelocity_get_const(world, shot)->x == 100);
        simulation_advance(&clock, clock.time + 0.18);
        assert(ecs_entity_is_alive(world, shot));
        simulation_advance(&clock, clock.time + 0.04);
        assert(!ecs_entity_is_alive(world, shot));
        assert(
            game_prototype_write_field(
                projectile,
                "GameProjectile",
                "speed",
                (pin_value){ .type = PIN_F32, .f = 420.0f }
            ) == 0
        );
        assert(
            game_prototype_write_field(
                projectile,
                "GameProjectile",
                "lifetime",
                (pin_value){ .type = PIN_F32, .f = 2.0f }
            ) == 0
        );
        ecs_entity wall = ecs_world_create_entity(world);
        assert(GAME_GameTransform_add(world, wall, 0) == ECS_OK);
        assert(GAME_GameCollider_add(world, wall, 0) == ECS_OK);
        assert(GAME_GameBody_add(world, wall, 0) == ECS_OK);
        assert(ecs_world_apply_commands(world) == ECS_OK);
        const GameTransform *player_position = GAME_GameTransform_get_const(world, owner);
        *GAME_GameTransform_get(world, wall) = (GameTransform){ player_position->x + 14,
                                                                player_position->y };
        *GAME_GameCollider_get(world, wall) = (GameCollider){ .shape = GAME_COLLIDER_BOX,
                                                              .width = 2,
                                                              .height = 80,
                                                              .category = 8,
                                                              .mask = 7 };
        assert(
            game_prototype_write_field(
                projectile,
                "GameProjectile",
                "speed",
                (pin_value){ .type = PIN_F32, .f = 4800 }
            ) == 0
        );
        assert(
            (preview ? game_prototype_preview_register_prototype(preview, projectile)
                     : game_entities_register(live->entities, projectile)) == 0
        );
        game_particles *particles = preview
                                      ? game_prototype_preview_particles(preview)
                                      : live->particles;
        int before_impact = game_particles_count(particles);
        simulation_action(&clock, "Fire");
        assert(!projectile_in(world).slot);
        assert(ecs_entity_is_alive(world, wall));
        assert(GAME_GameHealth_get_const(world, owner)->current == 100);
        assert(game_particles_count(particles) == before_impact + 12);
        assert(
            game_prototype_write_field(
                projectile,
                "GameProjectile",
                "speed",
                (pin_value){ .type = PIN_F32, .f = 420 }
            ) == 0
        );
        if (isolated) {
            game_prototype_preview_free(preview);
        } else {
            engine_free(runtime);
            game_live_world_free(live);
            ecs_world_free(world);
        }
    }
    game_prototype_free(projectile);
    game_prototype_free(player);
    puts("projectile PASS: authored launch velocity, shooter exclusion, expiry and fast shots against an adjacent thin wall in both hosts");
}
