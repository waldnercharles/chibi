#include "authoring/engine_preview.h"
#include "authoring/preview.h"
#include "authoring/prototype_prepare.h"
#include "authoring/prototype_preview.h"
#include "graph/document/serialize.h"
#include "runtime/component_catalog.h"
#include "runtime/engine.h"
#include "support/array.h"
#include "support/event_fixture.h"
#include "support/xalloc.h"
#include "test/support/code_fixture.h"
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

static test_source_fixture read_behavior(const char *path)
{
    char *text = read_content(path);
    test_source_fixture fixture = test_source_prepare(path, text);
    free(text);
    return fixture;
}

static prototype *read_prototype(const char *path, const prototype_registry *registry)
{
    char *text = read_content(path);
    char error[256];
    prototype *prototype = prototype_load(text, registry, error, sizeof error);
    free(text);
    assert(prototype);
    return prototype;
}

static ecs_entity projectile_in(ecs_world *world)
{
    ecs_query query;
    ecs_component_id id = COMPONENT_Projectile.id;
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
    preview_runtime *runtime = user;
    return source_attach(
        runtime->sources,
        entity,
        name,
        runtime->deferred,
        runtime->now,
        runtime->delta
    );
}

void test_projectile()
{
    prototype_registry registry = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT };
    prototype *player = read_prototype("test/fixtures/content/prototypes/Player.kdl", &registry);
    prototype *projectile = read_prototype("test/fixtures/content/prototypes/Projectile.kdl", &registry);
    const char *names[] = { "InputBehavior",         "WeaponBehavior",
                            "AutoWeaponBehavior",    "ProjectileBehavior",
                            "HealthBehavior",        "ProjectileHitBehavior",
                            "PlayerContactBehavior", "MovementTrailBehavior" };
    for (int isolated = 0; isolated < 2; isolated++) {
        ecs_world *world = isolated ? NULL : ecs_world_new();
        preview_world *live = NULL;
        engine *runtime = NULL;
        prototype_preview *preview = NULL;
        if (isolated) {
            preview = prototype_preview_new(&registry);
            register_test_preview_catalog(preview);
            world = prototype_preview_world(preview);
        } else {
            for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
                assert(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
            }
            live = preview_world_new(world);
            register_damage_event(live);
            runtime = authoring_engine_new(world, live->runtime);
        }
        for (int i = 0; i < (int)(sizeof names / sizeof names[0]); i++) {
            char path[256];
            const char *root = !strcmp(names[i], "InputBehavior") ||
                                       !strcmp(names[i], "AutoWeaponBehavior") ||
                                       !strcmp(names[i], "MovementTrailBehavior") ||
                                       !strcmp(names[i], "WeaponBehavior")
                                 ? "test/fixtures/content/behaviors"
                                 : "content/behaviors";
            snprintf(path, sizeof path, "%s/%s.chibi", root, names[i]);
            test_source_fixture fixture = read_behavior(path);
            test_source_register(
                preview ? prototype_preview_live(preview)->runtime : live->runtime,
                &fixture
            );
            test_source_free(&fixture);
        }
        char error[256];
        ecs_entity owner;
        if (isolated) {
            assert(prototype_preview_register_prototype(preview, projectile) == 0);
            assert(prototype_preview_launch(preview, player, error, sizeof error) == 0);
            owner = prototype_preview_entity(preview);

        } else {
            assert(entities_register(live->runtime->entities, projectile) == 0);
            assert(
                prototype_spawn(
                    player,
                    world,
                    &(prototype_spawn_hooks){ attach, live->runtime },
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
        float origin = Transform_get_const(world, owner)->x;
        assert(fabsf(Transform_get_const(world, shot)->x - origin - 7.0f) < 0.01f);
        assert(Collider_get_const(world, shot)->ignore_entity == ecs_entity_to_u64(owner));
        assert(Velocity_get_const(world, shot)->x == 420);
        simulation_advance(&clock, 0.1);
        assert(fabsf(Transform_get_const(world, shot)->x - origin - 42.0f) < 0.01f);
        simulation_advance(&clock, 2.1);
        assert(!ecs_entity_is_alive(world, shot));
        assert(!projectile_in(world).slot);
        assert(
            prototype_write_field(
                projectile,
                "Projectile",
                "speed",
                (pin_value){ .type = PIN_F32, .f = 100.0f }
            ) == 0
        );
        assert(
            prototype_write_field(
                projectile,
                "Projectile",
                "lifetime",
                (pin_value){ .type = PIN_F32, .f = 0.2f }
            ) == 0
        );
        if (isolated) {
            assert(prototype_preview_register_prototype(preview, projectile) == 0);

        } else {
            assert(entities_register(live->runtime->entities, projectile) == 0);
        }
        simulation_action(&clock, "Fire");
        shot = projectile_in(world);
        assert(shot.slot);
        const Projectile *state = Projectile_get_const(world, shot);
        assert(state->speed == 100.0f && state->lifetime == 0.2f);
        assert(Velocity_get_const(world, shot)->x == 100);
        simulation_advance(&clock, clock.time + 0.18);
        assert(ecs_entity_is_alive(world, shot));
        simulation_advance(&clock, clock.time + 0.04);
        assert(!ecs_entity_is_alive(world, shot));
        assert(
            prototype_write_field(
                projectile,
                "Projectile",
                "speed",
                (pin_value){ .type = PIN_F32, .f = 420.0f }
            ) == 0
        );
        assert(
            prototype_write_field(
                projectile,
                "Projectile",
                "lifetime",
                (pin_value){ .type = PIN_F32, .f = 2.0f }
            ) == 0
        );
        ecs_entity wall = ecs_world_create_entity(world);
        assert(Transform_add(world, wall, 0) == ECS_OK);
        assert(Collider_add(world, wall, 0) == ECS_OK);
        assert(Body_add(world, wall, 0) == ECS_OK);
        assert(ecs_world_apply_commands(world) == ECS_OK);
        const Transform *player_position = Transform_get_const(world, owner);
        *Transform_get(world, wall) = (Transform){ player_position->x + 14,
                                                   player_position->y };
        *Collider_get(world, wall) = (Collider){ .shape = COLLIDER_BOX,
                                                 .width = 2,
                                                 .height = 80,
                                                 .category = 8,
                                                 .mask = 7 };
        assert(
            prototype_write_field(
                projectile,
                "Projectile",
                "speed",
                (pin_value){ .type = PIN_F32, .f = 4800 }
            ) == 0
        );
        assert(
            (preview ? prototype_preview_register_prototype(preview, projectile)
                     : entities_register(live->runtime->entities, projectile)) == 0
        );
        particles *particles = preview ? prototype_preview_particles(preview)
                                       : live->runtime->particles;
        int before_impact = particles_count(particles);
        simulation_action(&clock, "Fire");
        assert(!projectile_in(world).slot);
        assert(ecs_entity_is_alive(world, wall));
        assert(Health_get_const(world, owner)->current == 100);
        assert(particles_count(particles) == before_impact + 12);
        assert(
            prototype_write_field(
                projectile,
                "Projectile",
                "speed",
                (pin_value){ .type = PIN_F32, .f = 420 }
            ) == 0
        );
        if (isolated) {
            prototype_preview_free(preview);
        } else {
            engine_free(runtime);
            preview_world_free(live);
            ecs_world_free(world);
        }
    }
    prototype_free(projectile);
    prototype_free(player);
    puts("projectile PASS: authored launch velocity, shooter exclusion, expiry and fast shots against an adjacent thin wall in both hosts");
}
