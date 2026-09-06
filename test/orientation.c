#include "../core/array.h"
#include "../core/serialize.h"
#include "../core/xalloc.h"
#include "../engine/engine.h"
#include "../game/generated.h"
#include "../game/live.h"
#include "../game/prototype_preview.h"

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
    snprintf(path, sizeof path, "content/behaviors/%s.json", name);
    char *text = read_content(path);
    behavior_asset *asset = behavior_asset_load_json(text);
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

static void tick(engine *runtime, game_prototype_preview *preview, double time)
{
    int result = preview ? game_prototype_preview_tick(preview, time)
                         : engine_frame(runtime, time);
    assert(result >= 0);
}

static void headings(bool isolated, bool projectile)
{
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    const char *name = projectile ? "ProjectileBehavior" : "InputBehavior";
    char *text = read_content(
        projectile ? "content/prototypes/Projectile.json" : "content/prototypes/Player.json"
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
    tick(runtime, preview, 0);
    const CF_V2 directions[] = { { 1, 1 },  { -1, 1 }, { -1, -1 },
                                 { 1, -1 }, { 0, 1 },  { 0, -1 },
                                 { -1, 0 }, { 1, 0 },  { -3, 4 } };
    double time = 0;
    for (int i = 0; i < (int)(sizeof directions / sizeof directions[0]); i++) {
        CF_V2 direction = directions[i];
        float expected = atan2f(-direction.y, direction.x);
        for (int idle = 0; idle < 2; idle++) {
            CF_V2 input = idle ? (CF_V2){ 0 } : direction;
            if (projectile) {
                GameProjectile *shot = GAME_GameProjectile_get(world, entity);
                shot->direction_x = input.x;
                shot->direction_y = input.y;
            } else {
                pin_value value = { .type = PIN_STRUCT,
                                    .type_name = "CF_V2",
                                    .ptr = &input };
                assert(
                    game_behavior_dispatch(behaviors, entity, "Input", (flow_inputs){ &value, 1, NULL }, NULL) >=
                    0
                );
            }
            time += 0.01;
            tick(runtime, preview, time);
            check_heading(world, entity, expected);
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
