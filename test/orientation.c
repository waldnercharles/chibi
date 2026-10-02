#include "graph/document/serialize.h"
#include "runtime/behaviors/live.h"
#include "runtime/engine.h"
#include "runtime/generated.h"
#include "runtime/scenes/prototype_preview.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "test/support/event_fixture.h"
#include "test/support/simulation_fixture.h"

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
    const char *root = !strcmp(name, "InputBehavior")
                         ? "test/fixtures/content/behaviors"
                         : "content/behaviors";
    snprintf(path, sizeof path, "%s/%s.kdl", root, name);
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

static void check_heading(ecs_world *world, ecs_entity entity, float expected)
{
    const GameOrientation *heading = GAME_GameOrientation_get_const(world, entity);
    assert(heading && isfinite(heading->radians));
    assert(fabsf(heading->radians - expected) < 0.0001f);
}

static void headings(bool isolated, bool projectile)
{
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    const char *name = projectile ? "ProjectileBehavior" : "InputBehavior";
    char *text = read_content(
        projectile ? "test/fixtures/content/prototypes/Projectile.kdl"
                   : "test/fixtures/content/prototypes/Player.kdl"
    );
    char error[256];
    game_prototype *prototype = game_prototype_load(text, &registry, error, sizeof error);
    free(text);
    assert(prototype);
    for (int i = acount(prototype->graphs) - 1; i >= 0; i--) {
        if (strcmp(prototype->graphs[i], name) != 0) {
            assert(game_prototype_remove_graph(prototype, prototype->graphs[i]) == 0);
        }
    }
    game_prototype_preview *preview = isolated ? game_prototype_preview_new(&registry)
                                               : NULL;
    register_test_preview_catalog(preview);
    ecs_world *world = preview ? game_prototype_preview_world(preview)
                               : ecs_world_new();
    game_live_world *live = NULL;
    engine *runtime = NULL;
    behavior_asset *asset = read_behavior(name);
    if (preview) {
        assert(game_prototype_preview_register_behavior(preview, name, asset) == 0);
        behavior_asset_free(asset);
    } else {
        for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
            assert(
                ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK
            );
        }
        live = game_live_world_new(world);
        runtime = engine_new(world, live);
        assert(game_live_add_definition(live, game_live_definition_new(name, asset)) == 0);
    }
    ecs_entity entity;
    if (preview) {
        assert(game_prototype_preview_launch(preview, prototype, error, sizeof error) == 0);
        entity = game_prototype_preview_entity(preview);
    } else {
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
        assert(engine_set_playing(runtime, true) == 0);
    }
    game_behavior_world *behaviors = preview
                                       ? game_prototype_preview_behaviors(preview)
                                       : live->behaviors;
    check_heading(world, entity, 0);
    simulation_fixture clock = simulation_fixture_new(runtime, preview);
    simulation_advance(&clock, 0);
    const CF_V2 directions[] = { { 1, 1 },  { -1, 1 }, { -1, -1 },
                                 { 1, -1 }, { 0, 1 },  { 0, -1 },
                                 { -1, 0 }, { 1, 0 },  { -3, 4 } };
    for (int i = 0; i < (int)(sizeof directions / sizeof directions[0]); i++) {
        CF_V2 direction = directions[i];
        float expected = atan2f(direction.y, direction.x);
        for (int idle = 0; idle < 2; idle++) {
            CF_V2 input = idle ? (CF_V2){ 0 } : direction;
            GameTransform before = *GAME_GameTransform_get_const(world, entity);
            if (projectile) {
                GameProjectile *shot = GAME_GameProjectile_get(world, entity);
                *GAME_GameVelocity_get(world, entity) = (GameVelocity){
                    input.x * shot->speed,
                    input.y * shot->speed
                };
            } else {
                pin_value value = { .type = PIN_STRUCT,
                                    .type_name = "CF_V2",
                                    .ptr = &input };
                assert(
                    game_behavior_dispatch(behaviors, entity, "Input", (flow_inputs){ &value, 1, NULL }, NULL) >=
                    0
                );
            }
            simulation_step(&clock);
            check_heading(world, entity, expected);
            if (projectile && !idle) {
                const GameTransform *after = GAME_GameTransform_get_const(world, entity);
                float dx = after->x - before.x, dy = after->y - before.y;
                float distance = hypotf(dx, dy);
                assert(distance > 0);
                const GameOrientation *heading = GAME_GameOrientation_get_const(world, entity);
                assert(fabsf(dx / distance - cosf(heading->radians)) < .0001f);
                assert(fabsf(dy / distance - sinf(heading->radians)) < .0001f);
            }
        }
    }
    if (preview) {
        game_prototype_preview_stop(preview);
        assert(game_prototype_preview_launch(preview, prototype, error, sizeof error) == 0);
        check_heading(world, game_prototype_preview_entity(preview), 0);
        game_prototype_preview_free(preview);
    } else {
        engine_free(runtime);
        game_live_world_free(live);
        ecs_world_free(world);
    }
    game_prototype_free(prototype);
}

void test_orientation()
{
    for (int isolated = 0; isolated < 2; isolated++) {
        headings(isolated, false);
        headings(isolated, true);
    }
    puts("orientation PASS: authored player and projectile headings, quadrants, idle retention and preview reset in both hosts");
}
