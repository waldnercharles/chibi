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
    av_value *actual = prototype_rebase_component_value(input, before, after);
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
    assert(!prototype_rebase_component_value(av_null(null_doc), empty, empty));
    av_doc_free(null_doc);
    for (int i = 0; i < 3; i++) {
        av_value *value = test_read_asset_value(invalid[i]);
        assert(!prototype_rebase_component_value(value, empty, empty));
        av_value_free(value);
    }
    av_value *health = test_read_asset_value("(object)Health\n");
    av_value *input = test_read_asset_value("(object)Input\n");
    av_value *removed = test_read_asset_value("Health #null\n");
    assert(!prototype_rebase_component_value(health, input, empty));
    assert(!prototype_rebase_component_value(health, empty, removed));
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
    prototype_registry registry = { components, 1, NULL, NULL };
    char error[256];
    prototype *prototype = prototype_load(
        "name \"StringOwner\"\n"
        "components {\n"
        "    test.Label text=\"customized\"\n"
        "}\n",
        &registry,
        error,
        sizeof error
    );
    assert(prototype && prototype_detach_component(prototype, "Label") == 0);
    prototype *copy = prototype_clone(prototype, error, sizeof error);
    prototype_free(prototype);
    assert(copy && prototype_add_component(copy, &component) == 0);
    pin_value value;
    assert(prototype_read_field(copy, "test.Label", "text", &value) == 0);
    assert(value.type == PIN_STR && strcmp(value.str, "customized") == 0);
    assert(prototype_detach_component(copy, "test.Label") == 0);
    char *saved = prototype_save(copy);
    prototype_free(copy);
    ecs_component_desc renamed = component;
    renamed.name = "DisplayLabel";
    const ecs_component_desc *renamed_components[] = { &renamed };
    prototype_registry renamed_registry = { renamed_components, 1, NULL, NULL };
    copy = prototype_load(saved, &renamed_registry, error, sizeof error);
    free(saved);
    assert(copy && prototype_add_component(copy, &renamed) == 0);
    assert(prototype_read_field(copy, "DisplayLabel", "text", &value) == 0);
    assert(value.type == PIN_STR && strcmp(value.str, "customized") == 0);
    prototype_free(copy);
    puts("retained components PASS: owned strings survive cloning and stable component keys survive display-name changes");
}

void test_retained_components()
{
    retained_inheritance();
    retained_strings();
    prototype_registry registry = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT, NULL, NULL };
    char error[256];
    prototype *prototype = prototype_load(
        "name \"Retained\"\n"
        "components {\n"
        "    Transform x=10 y=20\n"
        "    Health current=73 maximum=100\n"
        "    Input {\n"
        "        move x=3 y=4\n"
        "    }\n"
        "}\n",
        &registry,
        error,
        sizeof error
    );
    assert(prototype);
    char *original = prototype_save(prototype);
    assert(prototype_detach_component(prototype, "Health") == 0);
    assert(prototype_detach_component(prototype, "Input") == 0);
    assert(prototype_detach_component(prototype, "Health") == -1);
    pin_value value;
    assert(prototype_read_field(prototype, "Health", "current", &value) == -1);
    char *saved = prototype_save(prototype);
    prototype_free(prototype);
    prototype = prototype_load(saved, &registry, error, sizeof error);
    free(saved);
    assert(prototype && acount(prototype->detached_components) == 2);
    prototype *copy = prototype_clone(prototype, error, sizeof error);
    assert(copy && acount(copy->detached_components) == 2);

    ecs_world *world = ecs_world_new();
    for (int i = 0; i < registry.component_count; i++) {
        assert(ecs_world_register_component(world, registry.components[i], NULL) == ECS_OK);
    }
    ecs_entity entity;
    assert(prototype_spawn_components(prototype, world, &entity, error, sizeof error) == 0);
    assert(Transform_has(world, entity));
    assert(!Health_has(world, entity));
    assert(!Input_has(world, entity));
    assert(prototype_add_component(copy, &COMPONENT_Input) == 0);
    assert(prototype_add_component(copy, &COMPONENT_Health) == 0);
    assert(acount(copy->detached_components) == 0);
    assert(acount(prototype->detached_components) == 2);
    char *restored = prototype_save(copy);
    assert(strcmp(original, restored) == 0);
    assert(prototype_spawn_components(copy, world, &entity, error, sizeof error) == 0);
    assert(Health_get(world, entity)->current == 73);
    assert(Input_get(world, entity)->move.x == 3);
    assert(Input_get(world, entity)->move.y == 4);
    free(restored);
    free(original);
    prototype_free(copy);
    prototype_free(prototype);
    ecs_world_free(world);

    prototype = prototype_load(
        "name \"Invalid\"\n"
        "components {\n"
        "    (object)Health\n"
        "}\n"
        "detached_components {\n"
        "    (object)Health\n"
        "}\n",
        &registry,
        error,
        sizeof error
    );
    assert(!prototype && strstr(error, "both attached and detached"));
    puts("retained components PASS: detach, save/reload, clone, nested values, runtime absence, and reattach");
}
