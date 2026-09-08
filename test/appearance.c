#include "runtime/behaviors/live.h"
#include "runtime/engine.h"
#include "runtime/generated.h"
#include "runtime/scenes/prototype_preview.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct
{
    const char *name;
    pin_value value;
} style_edits[] = {
    { "shape", { .type = PIN_I32, .type_name = "int", .i = 0 } },
    { "stroke_width", { .type = PIN_F32, .type_name = "float", .f = 2.25f } },
    { "border_width", { .type = PIN_F32, .type_name = "float", .f = 3.5f } },
    { "border_color", { .type = PIN_U32, .type_name = "unsigned int", .u = 0x11223344u } },
    { "glow_radius", { .type = PIN_F32, .type_name = "float", .f = 14 } },
    { "glow_color", { .type = PIN_U32, .type_name = "unsigned int", .u = 0xaabbccddu } },
};

static void edit_style(GameAppearance *appearance)
{
    appearance->shape = 0;
    appearance->stroke_width = 2.25f;
    appearance->border_width = 3.5f;
    appearance->border_color = 0x11223344u;
    appearance->glow_radius = 14;
    appearance->glow_color = 0xaabbccddu;
}

static void expect_edited_style(const GameAppearance *appearance)
{
    assert(appearance->shape == 0 && appearance->border_width == 3.5f);
    assert(appearance->stroke_width == 2.25f);
    assert(appearance->border_color == 0x11223344u);
    assert(appearance->glow_radius == 14 && appearance->glow_color == 0xaabbccddu);
}

static game_prototype *read_prototype(const char *name, const game_prototype_registry *registry)
{
    char path[256], error[256];
    snprintf(path, sizeof path, "content/prototypes/%s.json", name);
    FILE *file = fopen(path, "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    assert(size >= 0);
    rewind(file);
    char *text = xmalloc((size_t)size + 1);
    assert(fread(text, 1, (size_t)size, file) == (size_t)size);
    text[size] = 0;
    fclose(file);
    game_prototype *prototype = game_prototype_load(text, registry, error, sizeof error);
    free(text);
    assert(prototype);
    while (acount(prototype->graphs)) {
        assert(
            game_prototype_remove_graph(
                prototype,
                prototype->graphs[acount(prototype->graphs) - 1]
            ) == 0
        );
    }
    return prototype;
}

static void expect_entity(ecs_world *world, ecs_entity entity, float width, float height, unsigned color)
{
    const GameAppearance *appearance = GAME_GameAppearance_get_const(world, entity);
    assert(appearance && appearance->width == width && appearance->height == height);
    assert(appearance->color == color);
    switch (color) {
        case 0xFFFFFFFFu:
            assert(
                appearance->shape == GAME_SHAPE_STROKED_CHEVRON &&
                appearance->border_width == 0
            );
            assert(appearance->border_color == 0 && appearance->stroke_width == 1);
            assert(appearance->glow_radius == 8 && appearance->glow_color == 0xFFFFFF50u);
            break;
        case 0x140F00FFu:
            assert(appearance->shape == 2 && appearance->border_width == 2);
            assert(appearance->border_color == 0xFFCC00FFu && appearance->stroke_width == 0);
            assert(appearance->glow_radius == 6 && appearance->glow_color == 0xFFCC0040u);
            break;
        case 0x00FFF0FFu:
            assert(appearance->shape == GAME_SHAPE_CAPSULE && appearance->border_width == 0);
            assert(appearance->stroke_width == 0 && appearance->border_color == 0);
            assert(appearance->glow_radius == 6 && appearance->glow_color == 0x00FFF060u);
            break;
        default: assert(false);
    }
}

static ecs_entity spawn(const game_prototype *prototype, ecs_world *world)
{
    char error[256];
    ecs_entity entity;
    assert(game_prototype_spawn_components(prototype, world, &entity, error, sizeof error) == 0);
    return entity;
}

static void authored_roundtrip(
    game_prototype *prototype,
    const game_prototype_registry *registry,
    float width,
    float height,
    unsigned color
)
{
    pin_value value;
    assert(game_prototype_read_field(prototype, "GameAppearance", "width", &value) == 0);
    assert(value.type == PIN_F32 && value.f == width);
    assert(game_prototype_read_field(prototype, "GameAppearance", "color", &value) == 0);
    assert(value.type == PIN_U32 && value.u == color);
    char error[256];
    game_prototype *edited = game_prototype_clone(prototype, error, sizeof error);
    assert(edited);
    value = (pin_value){ .type = PIN_F32, .type_name = "float", .f = 45.5f };
    assert(game_prototype_write_field(edited, "GameAppearance", "width", value) == 0);
    value = (pin_value){ .type = PIN_U32, .type_name = "unsigned int", .u = 0x11223344u };
    assert(game_prototype_write_field(edited, "GameAppearance", "color", value) == 0);
    for (int i = 0; i < (int)(sizeof style_edits / sizeof style_edits[0]); i++) {
        assert(
            game_prototype_write_field(
                edited,
                "GameAppearance",
                style_edits[i].name,
                style_edits[i].value
            ) == 0
        );
    }
    char *saved = game_prototype_save(edited);
    game_prototype *loaded = game_prototype_load(saved, registry, error, sizeof error);
    free(saved);
    assert(loaded);
    for (int i = 0; i < (int)(sizeof style_edits / sizeof style_edits[0]); i++) {
        assert(
            game_prototype_read_field(loaded, "GameAppearance", style_edits[i].name, &value) == 0
        );
        pin_value expected = style_edits[i].value;
        assert(value.type == expected.type);
        switch (expected.type) {
            case PIN_I32: assert(value.i == expected.i); break;
            case PIN_U32: assert(value.u == expected.u); break;
            case PIN_F32: assert(value.f == expected.f); break;
            default: assert(false);
        }
    }
    assert(
        game_prototype_read_field(loaded, "GameAppearance", "width", &value) == 0 &&
        value.f == 45.5f
    );
    assert(
        game_prototype_read_field(loaded, "GameAppearance", "height", &value) == 0 &&
        value.f == height
    );
    assert(
        game_prototype_read_field(loaded, "GameAppearance", "color", &value) == 0 &&
        value.u == 0x11223344u
    );
    assert(
        game_prototype_read_field(prototype, "GameAppearance", "color", &value) == 0 &&
        value.u == color
    );
    game_prototype_free(loaded);
    game_prototype_free(edited);
}

static void instance_overrides(
    game_prototype *prototype,
    const game_prototype_registry *registry,
    ecs_world *world,
    float width,
    float height,
    unsigned color
)
{
    ecs_entity original = spawn(prototype, world);
    expect_entity(world, original, width, height, color);
    GameAppearance *appearance = GAME_GameAppearance_get(world, original);
    appearance->width = 45.5f;
    appearance->color = 0x11223344u;
    edit_style(appearance);
    char *overrides = game_prototype_capture_overrides_json(prototype, world, original);
    assert(overrides && strstr(overrides, "GameAppearance") && !strstr(overrides, "height"));
    ecs_entity copied = spawn(prototype, world);
    char error[256];
    assert(
        game_prototype_apply_components_json(world, copied, overrides, registry, error, sizeof error) == 0
    );
    free(overrides);
    const GameAppearance *copy = GAME_GameAppearance_get_const(world, copied);
    assert(copy && copy->width == 45.5f && copy->height == height && copy->color == 0x11223344u);
    expect_edited_style(copy);
    ecs_entity fresh = spawn(prototype, world);
    expect_entity(world, fresh, width, height, color);
}

static void preview_reset(
    game_prototype *prototype,
    const game_prototype_registry *registry,
    float width,
    float height,
    unsigned color
)
{
    game_prototype_preview *preview = game_prototype_preview_new(registry);
    char error[256];
    assert(game_prototype_preview_launch(preview, prototype, error, sizeof error) == 0);
    ecs_world *world = game_prototype_preview_world(preview);
    ecs_entity original = game_prototype_preview_entity(preview);
    expect_entity(world, original, width, height, color);
    GAME_GameAppearance_get(world, original)->color = 0x11223344u;
    edit_style(GAME_GameAppearance_get(world, original));
    assert(game_prototype_preview_tick(preview, 0) >= 0);
    assert(GAME_GameAppearance_get_const(world, original)->color == 0x11223344u);
    expect_edited_style(GAME_GameAppearance_get_const(world, original));
    game_prototype_preview_stop(preview);
    assert(game_prototype_preview_launch(preview, prototype, error, sizeof error) == 0);
    ecs_entity fresh = game_prototype_preview_entity(preview);
    assert(ecs_entity_to_u64(fresh) != ecs_entity_to_u64(original));
    expect_entity(world, fresh, width, height, color);
    game_prototype_preview_free(preview);
}

static void legacy_appearance(const game_prototype_registry *registry, ecs_world *world)
{
    const char *json = "{\"name\":\"Legacy "
                       "appearance\",\"components\":{\"GameAppearance\":{"
                       "\"width\":24,\"height\":16,\"color\":287454020}}}";
    char error[256];
    game_prototype *prototype = game_prototype_load(json, registry, error, sizeof error);
    assert(prototype);
    ecs_entity entity = spawn(prototype, world);
    const GameAppearance *appearance = GAME_GameAppearance_get_const(world, entity);
    assert(appearance && appearance->width == 24 && appearance->height == 16);
    assert(appearance->color == 0x11223344u);
    assert(
        appearance->shape == 0 && appearance->stroke_width == 0 &&
        appearance->border_width == 0 && appearance->border_color == 0
    );
    assert(appearance->glow_radius == 0 && appearance->glow_color == 0);
    game_prototype_free(prototype);
}

void test_appearance()
{
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < registry.component_count; i++) {
        assert(ecs_world_register_component(world, registry.components[i], NULL) == ECS_OK);
    }
    legacy_appearance(&registry, world);
    game_live_world *live = game_live_world_new(world);
    engine *runtime = engine_new(world, live);
    assert(engine_set_playing(runtime, true) == 0);
    const char *names[] = { "Player", "Enemy", "Projectile" };
    const float widths[] = { 20, 28, 18 };
    const float heights[] = { 18, 28, 6 };
    const unsigned colors[] = { 0xFFFFFFFFu, 0x140F00FFu, 0x00FFF0FFu };
    for (int i = 0; i < 3; i++) {
        game_prototype *prototype = read_prototype(names[i], &registry);
        authored_roundtrip(prototype, &registry, widths[i], heights[i], colors[i]);
        instance_overrides(prototype, &registry, world, widths[i], heights[i], colors[i]);
        assert(engine_frame(runtime, 0) >= 0);
        preview_reset(prototype, &registry, widths[i], heights[i], colors[i]);
        game_prototype_free(prototype);
    }
    engine_free(runtime);
    game_live_world_free(live);
    ecs_world_free(world);
    puts("appearance PASS: distinct authored styles, sparse instance overrides, persistence and preview reset");
}
