#include "assets/prototype.h"

#include "assets/scene_document.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    const char *label;
} nested_default;
typedef struct
{
    int count;
    nested_default nested;
} component_default;
static const type_field nested_fields[] = {
    { .name = "label", .type = PIN_STR, .size = sizeof(char *), .type_name = "const char *" },
};
static const type_desc nested_type = {
    .name = "DefaultNested",
    .size = sizeof(nested_default),
    .fields = nested_fields,
    .field_count = 1,
};
static const type_field component_fields[] = {
    { .name = "count",
      .type = PIN_I32,
      .offset = offsetof(component_default, count),
      .size = sizeof(int) },
    { .name = "nested",
      .type = PIN_STRUCT,
      .offset = offsetof(component_default, nested),
      .size = sizeof(nested_default),
      .type_name = "DefaultNested" },
};
static const type_desc component_type = {
    .name = "DefaultRow",
    .size = sizeof(component_default),
    .fields = component_fields,
    .field_count = 2,
};
static const ecs_component_desc component = {
    .id = 9001,
    .key = "test.DefaultRow",
    .name = "DefaultRow",
    .element_size = sizeof(component_default),
    .type = &component_type,
};
static const ecs_component_desc nested = {
    .id = 9002,
    .key = "test.DefaultNested",
    .name = "DefaultNested",
    .element_size = sizeof(nested_default),
    .type = &nested_type,
};
/* The engine computes Derived from DefaultRow, so it is never authored. */
static const ecs_component_desc derived = {
    .id = 9003,
    .key = "test.Derived",
    .name = "Derived",
    .element_size = sizeof(component_default),
    .type = &component_type,
    .derived_from = 9001,
};
static const ecs_component_desc *const components[] = { &component, &nested, &derived };
static game_prototype *prototype, *loaded;
static game_scene_document *scene, *reloaded;
static char *saved;
static av_value *before, *after, *values, *rebased;

static void cleanup()
{
    game_prototype_free(prototype);
    game_prototype_free(loaded);
    prototype = loaded = NULL;
    av_value_free(before);
    av_value_free(after);
    av_value_free(values);
    av_value_free(rebased);
    before = after = values = rebased = NULL;
    game_scene_document_free(scene);
    game_scene_document_free(reloaded);
    scene = reloaded = NULL;
    free(saved);
    saved = NULL;
}

TEST_CASE(prototype_defaults_are_owned_and_detached_values_take_precedence)
{
    char label[] = "initial";
    component_default data = { 9, { label } };
    game_prototype_default initial = { component.id, &data, sizeof data };
    game_prototype_registry registry = {
        .components = components,
        .component_count = 2,
        .defaults = &initial,
        .default_count = 1,
    };
    prototype = game_prototype_new("DefaultOwner");
    prototype->registry_storage = registry;
    REQUIRE(game_prototype_add_component(prototype, &component) == 0);
    label[0] = 'X';
    data.count = 41;
    const component_default *copy = prototype->components[0].data;
    REQUIRE(copy->count == 9 && !strcmp(copy->nested.label, "initial"));
    REQUIRE(game_prototype_detach_component(prototype, component.name) == 0);
    REQUIRE(game_prototype_add_component(prototype, &component) == 0);
    copy = prototype->components[0].data;
    REQUIRE(copy->count == 9 && !strcmp(copy->nested.label, "initial"));

    data = (component_default){ 9, { "baseline" } };
    before = game_prototype_component_defaults_value(&registry, &component);
    values = game_prototype_component_value(prototype, component.name);
    REQUIRE(before && values);
    data = (component_default){ 12, { "updated" } };
    after = game_prototype_component_defaults_value(&registry, &component);
    REQUIRE(after != NULL);
    rebased = game_prototype_rebase_component_value(values, before, after);
    REQUIRE(rebased != NULL);
    av_value_free(values);
    values = NULL;
    av_value_free(before);
    before = NULL;
    av_value_free(after);
    after = NULL;
    const av_value *row = av_obj_get(rebased, component.key);
    REQUIRE(av_get_int(av_obj_get(row, "count")) == 12);
    REQUIRE(!strcmp(av_get_str(av_obj_get(av_obj_get(row, "nested"), "label")), "initial"));

    char error[192];
    loaded = game_prototype_load(
        "name \"Partial\"\ncomponents { test.DefaultRow { nested "
        "label=\"document\"; }; }\n",
        &registry,
        error,
        sizeof error
    );
    REQUIRE(loaded != NULL);
    copy = loaded->components[0].data;
    REQUIRE(copy->count == 0 && !strcmp(copy->nested.label, "document"));
    REQUIRE(game_prototype_remove_component(loaded, component.name) == 0);
    initial.size--;
    REQUIRE(game_prototype_add_component(loaded, &component) == -1);
    REQUIRE(acount(loaded->components) == 0);
    initial.size++;
    type_desc malformed = component_type;
    ecs_component_desc invalid = component;
    invalid.type = &malformed;
    malformed.size++;
    REQUIRE(game_prototype_add_component(loaded, &invalid) == -1);
    REQUIRE(acount(loaded->components) == 0);
    malformed.size = component_type.size;
    type_field fields[2];
    memcpy(fields, component_fields, sizeof fields);
    malformed.fields = fields;
    fields[0].offset = malformed.size;
    REQUIRE(game_prototype_add_component(loaded, &invalid) == -1);
    REQUIRE(acount(loaded->components) == 0);
    fields[0] = component_fields[0];
    fields[0].size = 1;
    REQUIRE(game_prototype_add_component(loaded, &invalid) == -1);
    REQUIRE(acount(loaded->components) == 0);
    return true;
}

static bool keeps_prototype_behaviors(const game_scene_document *document)
{
    const game_scene_entity *entities = document->entities;
    return acount(entities) == 3 && !entities[0].own_behaviors &&
           !acount(entities[0].behaviors) && entities[1].own_behaviors &&
           acount(entities[1].behaviors) == 1 &&
           !strcmp(entities[1].behaviors[0], "Steer") &&
           entities[2].own_behaviors && !acount(entities[2].behaviors);
}

TEST_CASE(derived_components_are_never_authored_or_captured)
{
    const game_prototype_registry registry = { .components = components,
                                               .component_count = 3 };
    char error[256];
    loaded = game_prototype_load(
        "name \"Computed\"\ncomponents {\n    Derived count=1\n}\n",
        &registry,
        error,
        sizeof error
    );
    REQUIRE(!loaded && strstr(error, "Derived is computed by the engine"));
    prototype = game_prototype_new("Computed");
    prototype->registry_storage = registry;
    REQUIRE(game_prototype_add_component(prototype, &derived) != 0);
    game_prototype_free(prototype);
    prototype = NULL;
    ecs_world *world = ecs_world_new();
    REQUIRE(ecs_world_register_component(world, &component, NULL) == ECS_OK);
    REQUIRE(ecs_world_register_component(world, &derived, NULL) == ECS_OK);
    ecs_entity entity = ecs_world_create_entity(world);
    REQUIRE(ecs_world_record_add(world, entity, component.id, NULL, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(ecs_component_has(world, entity, derived.id));
    prototype = game_prototype_capture_entity("Captured", world, entity, &registry);
    bool captured = prototype && acount(prototype->components) == 1 &&
                    prototype->components[0].desc == &component;
    ecs_world_free(world);
    REQUIRE(captured);
    return true;
}

TEST_CASE(scene_entities_keep_prototype_behaviors_until_they_list_their_own)
{
    char error[192];
    scene = game_scene_document_load(
        "version 1\nname \"Start\"\n(array)entities {\n"
        "    - name=\"Inherits\" prototype=\"Mover\"\n"
        "    - name=\"Replaces\" prototype=\"Mover\" { (array)behaviors "
        "\"Steer\"; }\n"
        "    - name=\"Opts out\" prototype=\"Mover\" { (array)behaviors; }\n"
        "}\n",
        error,
        sizeof error
    );
    REQUIRE(scene && keeps_prototype_behaviors(scene));
    saved = game_scene_document_save(scene, (ecs_component_catalog){ 0 });
    REQUIRE(saved);
    reloaded = game_scene_document_load(saved, error, sizeof error);
    REQUIRE(reloaded && keeps_prototype_behaviors(reloaded));
    REQUIRE(game_scene_document_add_entity(reloaded, -1, "Added", "Mover") == 0);
    REQUIRE(!alast(reloaded->entities).own_behaviors);
    return true;
}

static bool at_path(const game_scene_document *document, const char *path, const char *prototype)
{
    int index = game_scene_document_entity_index(document, path);
    if (index < 0 || strcmp(document->entities[index].prototype, prototype)) {
        return false;
    }
    char *named = game_scene_document_entity_path(document, index);
    bool same = !strcmp(named, path);
    free(named);
    return same;
}

/* Children nest under their parent, are named by path, and save back nested
 * in the same order. */
TEST_CASE(scene_children_nest_by_path)
{
    char error[192];
    const char
        *text = "version 1\nname \"Fleet\"\n(array)entities {\n"
                "    - name=\"Ship\" prototype=\"Hull\" { (array)children {\n"
                "        - name=\"Turret\" prototype=\"Gun\" { (array)children "
                "{\n"
                "            - name=\"Barrel\" prototype=\"Tube\"; }; }\n"
                "        - name=\"Light\" prototype=\"Lamp\"; }; }\n"
                "    - name=\"Turret\" prototype=\"Gun\"\n"
                "}\n";
    scene = game_scene_document_load(text, error, sizeof error);
    REQUIRE(scene && acount(scene->entities) == 5);
    REQUIRE(at_path(scene, "Ship", "Hull") && at_path(scene, "Ship.Turret.Barrel", "Tube"));
    REQUIRE(at_path(scene, "Ship.Light", "Lamp") && at_path(scene, "Turret", "Gun"));
    REQUIRE(scene->entities[game_scene_document_entity_index(scene, "Turret")].parent == -1);
    REQUIRE(game_scene_document_entity_index(scene, "Barrel") < 0);
    REQUIRE(game_scene_document_entity_index(scene, "Ship..Light") < 0);
    /* Names are unique among siblings and cannot hold the separator. */
    int ship = game_scene_document_entity_index(scene, "Ship");
    REQUIRE(game_scene_document_add_entity(scene, ship, "Light", "Lamp") != 0);
    REQUIRE(game_scene_document_add_entity(scene, ship, "Port.Light", "Lamp") != 0);
    REQUIRE(game_scene_document_rename_entity(scene, ship + 1, "Light", error, sizeof error) != 0);
    REQUIRE(game_scene_document_add_entity(scene, ship, "Shield", "Dome") == 0);
    saved = game_scene_document_save(scene, (ecs_component_catalog){ 0 });
    reloaded = game_scene_document_load(saved, error, sizeof error);
    REQUIRE(reloaded && acount(reloaded->entities) == 6);
    /* The appended child saves among its siblings, ahead of the later root. */
    REQUIRE(game_scene_document_entity_index(reloaded, "Ship.Shield") == 4);
    /* Removing a parent removes its descendants and keeps the rest in place. */
    REQUIRE(game_scene_document_remove_entity(reloaded, 1) == 0);
    REQUIRE(acount(reloaded->entities) == 4);
    REQUIRE(at_path(reloaded, "Ship.Light", "Lamp") && at_path(reloaded, "Ship.Shield", "Dome"));
    REQUIRE(at_path(reloaded, "Turret", "Gun"));
    return true;
}

/* A chain of one object per level, `depth` levels deep. */
static char *nested_scene(int depth)
{
    static const char
        header[] = "version 1\nname \"Deep\"\n(array)entities {\n",
        open[] = "- name=\"N\" prototype=\"P\" { (array)children {\n",
        leaf[] = "- name=\"N\" prototype=\"P\"\n", close[] = "}\n}\n";
    size_t size = sizeof header + (size_t)depth * (sizeof open + sizeof close) +
                  sizeof leaf + 2;
    char *text = xmalloc(size);
    strcpy(text, header);
    for (int i = 1; i < depth; i++) {
        strcat(text, open);
    }
    strcat(text, leaf);
    for (int i = 1; i < depth; i++) {
        strcat(text, close);
    }
    strcat(text, "}\n");
    return text;
}

TEST_CASE(scene_children_nest_at_most_the_depth_limit)
{
    char error[192];
    char *deepest = nested_scene(GAME_SCENE_DEPTH_MAX),
         *deeper = nested_scene(GAME_SCENE_DEPTH_MAX + 1);
    scene = game_scene_document_load(deepest, error, sizeof error);
    reloaded = game_scene_document_load(deeper, error, sizeof error);
    free(deepest);
    free(deeper);
    REQUIRE(scene && acount(scene->entities) == GAME_SCENE_DEPTH_MAX);
    REQUIRE(!reloaded && strstr(error, "nests deeper than 256 levels"));
    REQUIRE(game_scene_document_add_entity(scene, GAME_SCENE_DEPTH_MAX - 1, "M", "P") != 0);
    REQUIRE(game_scene_document_add_entity(scene, GAME_SCENE_DEPTH_MAX - 2, "M", "P") == 0);
    char *path = game_scene_document_entity_path(scene, GAME_SCENE_DEPTH_MAX - 1);
    bool long_path = strlen(path) == 2 * GAME_SCENE_DEPTH_MAX - 1;
    free(path);
    REQUIRE(long_path);
    saved = game_scene_document_save(scene, (ecs_component_catalog){ 0 });
    return saved != NULL;
}

void test_prototype_document()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(prototype_defaults_are_owned_and_detached_values_take_precedence);
    RUN_TEST_CASE(scene_entities_keep_prototype_behaviors_until_they_list_their_own);
    RUN_TEST_CASE(scene_children_nest_by_path);
    RUN_TEST_CASE(scene_children_nest_at_most_the_depth_limit);
    RUN_TEST_CASE(derived_components_are_never_authored_or_captured);
    pu_clear_setup();
}
