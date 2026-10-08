#include "assets/prototype.h"
#include "runtime/component_catalog.h"
#include "support/array.h"
#include "test/asset_fixture.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_rebase(const char *values, const char *previous, const char *current, const char *expected)
{
    av_value *input = test_read_asset_value(values);
    av_value *before = test_read_asset_value(previous);
    av_value *after = test_read_asset_value(current);
    av_value *wanted = test_read_asset_value(expected);
    av_value *actual = game_prototype_rebase_component_value(input, before, after);
    assert(actual && av_equals(actual, wanted));
    av_value_free(input);
    av_value_free(before);
    av_value_free(after);
    av_value_free(wanted);
    av_value_free(actual);
}

static void retained_inheritance()
{
    check_rebase(
        "Health current=73 maximum=100\n",
        "Health current=100 maximum=100\n",
        "Health current=200 maximum=200\n",
        "Health current=73 maximum=200\n"
    );
    check_rebase(
        "Input {\n"
        "    move x=3 y=0\n"
        "    label \"custom\"\n"
        "}\n",
        "Input {\n"
        "    move x=0 y=0\n"
        "    label \"default\"\n"
        "}\n",
        "Input {\n"
        "    move x=1 y=2\n"
        "    label \"new\"\n"
        "    enabled #true\n"
        "}\n",
        "Input {\n"
        "    move x=3 y=2\n"
        "    label \"custom\"\n"
        "    enabled #true\n"
        "}\n"
    );
    check_rebase("Health current=73\n", "", "Health current=200 maximum=200\n", "Health current=73 maximum=200\n");
    check_rebase("Health current=73\n", "Health current=100\n", "", "Health current=73\n");
    const char *invalid[] = { "",
                              "Health #null\n",
                              "(object)Health\n"
                              "(object)Input\n" };
    av_value *empty = test_read_asset_value("");
    av_doc *null_doc = av_doc_new();
    assert(!game_prototype_rebase_component_value(av_null(null_doc), empty, empty));
    av_doc_free(null_doc);
    for (int i = 0; i < 3; i++) {
        av_value *value = test_read_asset_value(invalid[i]);
        assert(!game_prototype_rebase_component_value(value, empty, empty));
        av_value_free(value);
    }
    av_value *health = test_read_asset_value("(object)Health\n");
    av_value *input = test_read_asset_value("(object)Input\n");
    av_value *removed = test_read_asset_value("Health #null\n");
    assert(!game_prototype_rebase_component_value(health, input, empty));
    assert(!game_prototype_rebase_component_value(health, empty, removed));
    av_value_free(health);
    av_value_free(input);
    av_value_free(removed);
    av_value_free(empty);
    puts("retained inheritance PASS: local values, evolving defaults, nested fields, absent bases, and invalid snapshots");
}

static void retained_strings()
{
    static const type_field fields[] = {
        { "text", PIN_STR, 0, sizeof(char *), "const char *" }
    };
    static const type_desc type = { "Label", sizeof(char *), fields, 1, NULL };
    static const ecs_component_desc component = { .id = 901,
                                                  .key = "test.Label",
                                                  .name = "Label",
                                                  .element_size = sizeof(char *),
                                                  .type = &type };
    const ecs_component_desc *components[] = { &component };
    game_prototype_registry registry = { components, 1, NULL, NULL };
    char error[256];
    game_prototype *prototype = game_prototype_load(
        "name \"StringOwner\"\n"
        "components {\n"
        "    test.Label text=\"customized\"\n"
        "}\n",
        &registry,
        error,
        sizeof error
    );
    assert(prototype && game_prototype_detach_component(prototype, "Label") == 0);
    game_prototype *copy = game_prototype_clone(prototype, error, sizeof error);
    game_prototype_free(prototype);
    assert(copy && game_prototype_add_component(copy, &component) == 0);
    pin_value value;
    assert(game_prototype_read_field(copy, "test.Label", "text", &value) == 0);
    assert(value.type == PIN_STR && strcmp(value.str, "customized") == 0);
    assert(game_prototype_detach_component(copy, "test.Label") == 0);
    char *saved = game_prototype_save(copy);
    game_prototype_free(copy);
    ecs_component_desc renamed = component;
    renamed.name = "DisplayLabel";
    const ecs_component_desc *renamed_components[] = { &renamed };
    game_prototype_registry renamed_registry = { renamed_components, 1, NULL, NULL };
    copy = game_prototype_load(saved, &renamed_registry, error, sizeof error);
    free(saved);
    assert(copy && game_prototype_add_component(copy, &renamed) == 0);
    assert(game_prototype_read_field(copy, "DisplayLabel", "text", &value) == 0);
    assert(value.type == PIN_STR && strcmp(value.str, "customized") == 0);
    game_prototype_free(copy);
    puts("retained components PASS: owned strings survive cloning and stable component keys survive display-name changes");
}

void test_retained_components()
{
    retained_inheritance();
    retained_strings();
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT,
                                         NULL,
                                         NULL };
    char error[256];
    game_prototype *prototype = game_prototype_load(
        "name \"Retained\"\n"
        "components {\n"
        "    GameTransform x=10 y=20\n"
        "    GameHealth current=73 maximum=100\n"
        "    GameInput {\n"
        "        move x=3 y=4\n"
        "    }\n"
        "}\n",
        &registry,
        error,
        sizeof error
    );
    assert(prototype);
    char *original = game_prototype_save(prototype);
    assert(game_prototype_detach_component(prototype, "GameHealth") == 0);
    assert(game_prototype_detach_component(prototype, "GameInput") == 0);
    assert(game_prototype_detach_component(prototype, "GameHealth") == -1);
    pin_value value;
    assert(game_prototype_read_field(prototype, "GameHealth", "current", &value) == -1);
    char *saved = game_prototype_save(prototype);
    game_prototype_free(prototype);
    prototype = game_prototype_load(saved, &registry, error, sizeof error);
    free(saved);
    assert(prototype && acount(prototype->detached_components) == 2);
    game_prototype *copy = game_prototype_clone(prototype, error, sizeof error);
    assert(copy && acount(copy->detached_components) == 2);

    ecs_world *world = ecs_world_new();
    for (int i = 0; i < registry.component_count; i++) {
        assert(ecs_world_register_component(world, registry.components[i], NULL) == ECS_OK);
    }
    ecs_entity entity;
    assert(game_prototype_spawn_components(prototype, world, &entity, error, sizeof error) == 0);
    assert(GAME_GameTransform_has(world, entity));
    assert(!GAME_GameHealth_has(world, entity));
    assert(!GAME_GameInput_has(world, entity));
    assert(game_prototype_add_component(copy, &GAME_COMPONENT_GameInput) == 0);
    assert(game_prototype_add_component(copy, &GAME_COMPONENT_GameHealth) == 0);
    assert(acount(copy->detached_components) == 0);
    assert(acount(prototype->detached_components) == 2);
    char *restored = game_prototype_save(copy);
    assert(strcmp(original, restored) == 0);
    assert(game_prototype_spawn_components(copy, world, &entity, error, sizeof error) == 0);
    assert(GAME_GameHealth_get(world, entity)->current == 73);
    assert(GAME_GameInput_get(world, entity)->move.x == 3);
    assert(GAME_GameInput_get(world, entity)->move.y == 4);
    free(restored);
    free(original);
    game_prototype_free(copy);
    game_prototype_free(prototype);
    ecs_world_free(world);

    prototype = game_prototype_load(
        "name \"Invalid\"\n"
        "components {\n"
        "    (object)GameHealth\n"
        "}\n"
        "detached_components {\n"
        "    (object)GameHealth\n"
        "}\n",
        &registry,
        error,
        sizeof error
    );
    assert(!prototype && strstr(error, "both attached and detached"));
    puts("retained components PASS: detach, save/reload, clone, nested values, runtime absence, and reattach");
}
