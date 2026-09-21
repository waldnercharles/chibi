#include "runtime/medium.h"

#include "runtime/generated.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void set(ecs_world *world, ecs_entity entity, GameMedium settings)
{
    GameMedium
        *current = ecs_component_data(world, entity, GAME_COMPONENT_GameMedium.id);
    if (current) {
        *current = settings;
        return;
    }
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameMedium.id, &settings, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
}

void test_medium()
{
    ecs_world *world = ecs_world_new();
    assert(ecs_world_register_component(world, &GAME_COMPONENT_GameMedium, NULL) == ECS_OK);
    game_medium *medium = game_medium_new(world);
    assert(!game_medium_update(medium, 0));
    assert(!game_medium_inspect(medium).cells);
    ecs_entity entity = ecs_world_create_entity(world);
    GameMedium settings = { .columns = 32,
                            .rows = 24,
                            .cell_size = 4,
                            .density = 1,
                            .variation = .4f,
                            .detail_size = 48,
                            .velocity = { 7, -3 },
                            .extinction = .01f,
                            .scattering = 0x8090a0ff,
                            .depth = 20 };
    set(world, entity, settings);
    assert(!game_medium_update(medium, 0));
    game_medium_state start = game_medium_inspect(medium);
    assert(start.cells == 768 && start.minimum >= .6f && start.maximum <= 1.4f);
    for (int i = 0; i < 600; i++) {
        assert(!game_medium_update(medium, 1.f / 60));
    }
    game_medium_state end = game_medium_inspect(medium);
    assert(fabsf(end.mass - start.mass) < start.mass * .00002f);
    assert(end.minimum >= start.minimum - .000001f && end.maximum <= start.maximum + .000001f);
    assert(end.hash != start.hash);
    assert(!game_medium_update(medium, 0));
    assert(game_medium_inspect(medium).hash == end.hash);
    game_medium_reset(medium);
    assert(!game_medium_update(medium, 0));
    assert(game_medium_inspect(medium).hash == start.hash);
    for (int i = 0; i < 600; i++) {
        assert(!game_medium_update(medium, 1.f / 60));
    }
    assert(game_medium_inspect(medium).hash == end.hash);

    /* Uniform fog cannot develop holes, even under large wrapped displacements. */
    settings.variation = 0;
    settings.velocity = (CF_V2){ -10000, 10000 };
    set(world, entity, settings);
    assert(!game_medium_update(medium, 1));
    end = game_medium_inspect(medium);
    assert(end.minimum == 1 && end.maximum == 1 && end.mass == 12288);

    /* Changing only optical properties must preserve the advected density. */
    settings.depth = 100;
    settings.extinction = .05f;
    set(world, entity, settings);
    assert(!game_medium_update(medium, 0));
    assert(game_medium_inspect(medium).hash == end.hash);
    settings.density = 0;
    set(world, entity, settings);
    assert(!game_medium_update(medium, 1.f / 60));
    assert(game_medium_inspect(medium).mass == 0);

    settings.extinction = NAN;
    set(world, entity, settings);
    assert(game_medium_update(medium, 0));
    settings.extinction = .01f;
    set(world, entity, settings);
    ecs_entity duplicate = ecs_world_create_entity(world);
    set(world, duplicate, settings);
    assert(game_medium_update(medium, 0));
    assert(ecs_world_destroy_entity(world, duplicate) == ECS_OK);
    assert(ecs_world_destroy_entity(world, entity) == ECS_OK);
    assert(!game_medium_update(medium, 0));
    assert(!game_medium_inspect(medium).cells);
    game_medium_free(medium);
    ecs_world_free(world);
    puts("PASS: persistent medium mass, bounds, uniform coverage, deterministic reset, vacuum and lifecycle");
}
