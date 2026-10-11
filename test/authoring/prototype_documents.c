#include "assets/prototype.h"
#include "assets/scene_document.h"
#include "authoring/native_prototype.h"
#include "runtime/component_catalog.h"
#include "support/array.h"
#include "test/asset_fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Prototype and scene documents over the game's reflected components:
 * includes, rebased and detached components, spawning, captured overrides and
 * scene round trips. */

static ecs_component_catalog game_components()
{
    return (ecs_component_catalog){ GAME_REGISTRY_COMPONENTS,
                                    GAME_REGISTRY_COMPONENT_COUNT };
}
static game_prototype *prototype, *copy;
static ecs_world *world;
static char *saved, *again;
static game_scene_document *scene, *loaded;
static av_value *values;
static char **owned_strings;

static void cleanup()
{
    game_prototype_free(prototype);
    game_prototype_free(copy);
    ecs_world_free(world);
    free(saved);
    free(again);
    game_scene_document_free(scene);
    game_scene_document_free(loaded);
    av_value_free(values);
    game_prototype_owned_strings_free(owned_strings);
    prototype = copy = NULL;
    world = NULL;
    saved = again = NULL;
    scene = loaded = NULL;
    values = NULL;
    owned_strings = NULL;
}

static bool rebases(const char *local, const char *previous, const char *current, const char *expected)
{
    av_value *input = test_read_asset_value(local);
    av_value *before = test_read_asset_value(previous);
    av_value *after = test_read_asset_value(current);
    av_value *wanted = test_read_asset_value(expected);
    av_value *actual = game_prototype_rebase_component_value(input, before, after);
    bool same = actual && av_equals(actual, wanted);
    av_value_free(input);
    av_value_free(before);
    av_value_free(after);
    av_value_free(wanted);
    av_value_free(actual);
    return same;
}

static bool refuses_rebase(const char *local, const char *previous, const char *current)
{
    av_value *input = test_read_asset_value(local);
    av_value *before = test_read_asset_value(previous);
    av_value *after = test_read_asset_value(current);
    av_value *actual = game_prototype_rebase_component_value(input, before, after);
    av_value_free(input);
    av_value_free(before);
    av_value_free(after);
    av_value_free(actual);
    return !actual;
}

/* Local values survive evolving defaults, nested fields included, with or
 * without a base; invalid snapshots are refused. */
TEST_CASE(rebase_keeps_local_values_over_evolving_defaults)
{
    REQUIRE(rebases(
        "Health current=73 maximum=100\n",
        "Health current=100 maximum=100\n",
        "Health current=200 maximum=200\n",
        "Health current=73 maximum=200\n"
    ));
    REQUIRE(rebases(
        "Input {\n    move x=3 y=0\n    label \"custom\"\n}\n",
        "Input {\n    move x=0 y=0\n    label \"default\"\n}\n",
        "Input {\n    move x=1 y=2\n    label \"new\"\n    enabled #true\n}\n",
        "Input {\n    move x=3 y=2\n    label \"custom\"\n    enabled "
        "#true\n}\n"
    ));
    REQUIRE(
        rebases("Health current=73\n", "", "Health current=200 maximum=200\n", "Health current=73 maximum=200\n")
    );
    REQUIRE(rebases("Health current=73\n", "Health current=100\n", "", "Health current=73\n"));

    av_value *empty = test_read_asset_value("");
    av_doc *null_doc = av_doc_new();
    av_value *rebased = game_prototype_rebase_component_value(av_null(null_doc), empty, empty);
    av_doc_free(null_doc);
    av_value_free(empty);
    REQUIRE(!rebased);
    REQUIRE(refuses_rebase("", "", ""));
    REQUIRE(refuses_rebase("Health #null\n", "", ""));
    REQUIRE(refuses_rebase("(object)Health\n(object)Input\n", "", ""));
    REQUIRE(refuses_rebase("(object)Health\n", "(object)Input\n", ""));
    REQUIRE(refuses_rebase("(object)Health\n", "", "Health #null\n"));
    return true;
}

/* Owned strings survive cloning, and stable component keys survive
 * display-name changes. */
TEST_CASE(retained_strings_survive_clones_and_renames)
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
    prototype = game_prototype_load(
        "name \"StringOwner\"\ncomponents {\n    test.Label "
        "text=\"customized\"\n}\n",
        &registry,
        error,
        sizeof error
    );
    REQUIRE(prototype && game_prototype_detach_component(prototype, "Label") == 0);
    copy = game_prototype_clone(prototype, error, sizeof error);
    game_prototype_free(prototype);
    prototype = NULL;
    REQUIRE(copy && game_prototype_add_component(copy, &component) == 0);
    pin_value value;
    REQUIRE(game_prototype_read_field(copy, "test.Label", "text", &value) == 0);
    REQUIRE(value.type == PIN_STR && strcmp(value.str, "customized") == 0);
    REQUIRE(game_prototype_detach_component(copy, "test.Label") == 0);
    saved = game_prototype_save(copy);

    ecs_component_desc renamed = component;
    renamed.name = "DisplayLabel";
    const ecs_component_desc *renamed_components[] = { &renamed };
    game_prototype_registry renamed_registry = { renamed_components, 1, NULL, NULL };
    prototype = game_prototype_load(saved, &renamed_registry, error, sizeof error);
    REQUIRE(prototype && game_prototype_add_component(prototype, &renamed) == 0);
    REQUIRE(game_prototype_read_field(prototype, "DisplayLabel", "text", &value) == 0);
    REQUIRE(value.type == PIN_STR && strcmp(value.str, "customized") == 0);
    return true;
}

/* Detached components save, reload and clone; they are absent from spawned
 * entities until reattached with their retained values. */
TEST_CASE(detached_components_spawn_absent_and_reattach)
{
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    char error[256];
    prototype = game_prototype_load(
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
    REQUIRE(prototype);
    again = game_prototype_save(prototype);
    REQUIRE(game_prototype_detach_component(prototype, "GameHealth") == 0);
    REQUIRE(game_prototype_detach_component(prototype, "GameInput") == 0);
    REQUIRE(game_prototype_detach_component(prototype, "GameHealth") == -1);
    pin_value value;
    REQUIRE(game_prototype_read_field(prototype, "GameHealth", "current", &value) == -1);
    saved = game_prototype_save(prototype);
    game_prototype_free(prototype);
    prototype = game_prototype_load(saved, &registry, error, sizeof error);
    REQUIRE(prototype && acount(prototype->detached_components) == 2);
    copy = game_prototype_clone(prototype, error, sizeof error);
    REQUIRE(copy && acount(copy->detached_components) == 2);

    world = ecs_world_new();
    for (int i = 0; i < registry.component_count; i++) {
        REQUIRE(ecs_world_register_component(world, registry.components[i], NULL) == ECS_OK);
    }
    ecs_entity entity;
    REQUIRE(game_prototype_spawn_components(prototype, world, &entity, error, sizeof error) == 0);
    REQUIRE(GAME_GameTransform_has(world, entity));
    REQUIRE(!GAME_GameHealth_has(world, entity));
    REQUIRE(!GAME_GameInput_has(world, entity));
    REQUIRE(game_prototype_add_component(copy, &GAME_COMPONENT_GameInput) == 0);
    REQUIRE(game_prototype_add_component(copy, &GAME_COMPONENT_GameHealth) == 0);
    REQUIRE(acount(copy->detached_components) == 0);
    REQUIRE(acount(prototype->detached_components) == 2);
    free(saved);
    saved = game_prototype_save(copy);
    REQUIRE(strcmp(again, saved) == 0);
    REQUIRE(game_prototype_spawn_components(copy, world, &entity, error, sizeof error) == 0);
    REQUIRE(GAME_GameHealth_get(world, entity)->current == 73);
    REQUIRE(GAME_GameInput_get(world, entity)->move.x == 3);
    REQUIRE(GAME_GameInput_get(world, entity)->move.y == 4);

    game_prototype *invalid = game_prototype_load(
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
    REQUIRE(!invalid && strstr(error, "both attached and detached"));
    return true;
}

static const char *base_enemy = "name \"BaseEnemy\"\n"
                                "components {\n"
                                "    GameHealth current=50 maximum=50\n"
                                "    GameTransform x=1 y=2\n"
                                "}\n"
                                "(array)behaviors \"idle.graph\"\n";
static const char *derived_enemy = "name \"Enemy\"\n"
                                   "identity \"enemy-id\"\n"
                                   "(array)includes \"BaseEnemy\"\n"
                                   "components {\n"
                                   "    GameHealth current=100\n"
                                   "}\n"
                                   "(array)behaviors \"attack.graph\"\n";
static const char *cycle_a = "name \"CycleA\"\n(array)includes \"CycleB\"\n";
static const char *cycle_b = "name \"CycleB\"\n(array)includes \"CycleA\"\n";
static int attached;

static const char *find_base(const char *name, void *)
{
    return strcmp(name, "BaseEnemy") == 0 ? base_enemy : NULL;
}

static const char *find_cycle(const char *name, void *)
{
    if (strcmp(name, "CycleA") == 0) {
        return cycle_a;
    }
    return strcmp(name, "CycleB") == 0 ? cycle_b : NULL;
}

static int attach(ecs_world *, ecs_entity, const char *graph_name, void *)
{
    attached += graph_name && graph_name[0] != '\0';
    return 0;
}

/* The game's components, with BaseEnemy to include. */
static game_prototype_registry enemies()
{
    return (
        game_prototype_registry
    ){ GAME_REGISTRY_COMPONENTS, GAME_REGISTRY_COMPONENT_COUNT, find_base, NULL };
}

static const GameHealth *health_of(const game_prototype *p)
{
    for (int i = 0; i < acount(p->components); i++) {
        if (strcmp(p->components[i].desc->name, "GameHealth") == 0) {
            return p->components[i].data;
        }
    }
    return NULL;
}

/* Includes load first and local fields override them by name; saving is
 * deterministic, names derive identities, and missing components and include
 * cycles are refused. */
TEST_CASE(prototype_includes_override_and_save_deterministically)
{
    game_prototype_registry registry = enemies();
    char error[256];
    prototype = game_prototype_load(derived_enemy, &registry, error, sizeof error);
    REQUIRE(prototype && strcmp(prototype->name, "Enemy") == 0);
    REQUIRE(acount(prototype->components) == 2);
    const GameHealth *health = health_of(prototype);
    REQUIRE(health && health->current == 100 && health->maximum == 50);
    saved = game_prototype_save(prototype);
    copy = game_prototype_load(
        "(array)behaviors \"attack.graph\" \"idle.graph\"\n"
        "identity \"enemy-id\"\n"
        "components {\n"
        "    GameTransform y=2 x=1\n"
        "    GameHealth maximum=50 current=100\n"
        "}\n"
        "name \"Enemy\"\n",
        &registry,
        error,
        sizeof error
    );
    REQUIRE(copy);
    again = game_prototype_save(copy);
    REQUIRE(saved && again && strcmp(saved, again) == 0);
    REQUIRE(strstr(saved, "GameTransform") && strstr(saved, "GameHealth"));
    game_prototype_free(prototype);
    game_prototype_free(copy);
    free(again);
    prototype = game_prototype_load("name \"Plain\"\n", &registry, error, sizeof error);
    copy = game_prototype_load("name \"Other\"\n", &registry, error, sizeof error);
    again = prototype ? game_prototype_save(prototype) : NULL;
    REQUIRE(prototype && copy && again);
    REQUIRE(strcmp(game_prototype_identity(prototype), game_prototype_identity(copy)) != 0);
    REQUIRE(!strstr(again, "identity"));

    game_prototype_registry missing = { GAME_REGISTRY_COMPONENTS, 1, find_base, NULL };
    REQUIRE(!game_prototype_load(derived_enemy, &missing, error, sizeof error));
    REQUIRE(strstr(error, "GameHealth"));
    const game_prototype_registry cycles = {
        GAME_REGISTRY_COMPONENTS,
        GAME_REGISTRY_COMPONENT_COUNT,
        find_cycle,
        NULL,
    };
    REQUIRE(!game_prototype_load(cycle_a, &cycles, error, sizeof error));
    REQUIRE(strstr(error, "cycle"));
    return true;
}

/* Spawning writes component fields and attaches each behavior; prototypes
 * stay editable data, and a live entity captures as sparse overrides. */
TEST_CASE(prototype_spawns_edits_and_captures_overrides)
{
    game_prototype_registry registry = enemies();
    char error[256];
    prototype = game_prototype_load(derived_enemy, &registry, error, sizeof error);
    REQUIRE(prototype);
    world = ecs_world_new();
    REQUIRE(ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL) == ECS_OK);
    REQUIRE(ecs_world_register_component(world, &GAME_COMPONENT_GameHealth, NULL) == ECS_OK);
    attached = 0;
    const game_prototype_spawn_hooks hooks = { attach, NULL };
    ecs_entity entity = { 0 };
    /* Behavior-bearing prototypes cannot silently skip attachment. */
    REQUIRE(
        game_prototype_spawn(prototype, world, NULL, &entity, error, sizeof error) == ECS_ERR_ARGUMENT
    );
    REQUIRE(ecs_world_live_count(world) == 0 && strstr(error, "attach hook"));
    REQUIRE(game_prototype_spawn(prototype, world, &hooks, &entity, error, sizeof error) == ECS_OK);
    REQUIRE(attached == 2 && ecs_entity_is_alive(world, entity));

    pin_value field = { 0 };
    REQUIRE(game_prototype_read_field(prototype, "GameHealth", "current", &field) == 0);
    REQUIRE(field.i == 100);
    pin_value edited = { .type = PIN_I32, .i = 101 };
    REQUIRE(game_prototype_write_field(prototype, "GameHealth", "current", edited) == 0);
    REQUIRE(game_prototype_read_field(prototype, "GameHealth", "current", &field) == 0);
    REQUIRE(field.i == 101);
    edited.i = 100;
    REQUIRE(game_prototype_write_field(prototype, "GameHealth", "current", edited) == 0);
    REQUIRE(game_prototype_add_graph(prototype, "debug.graph") == 0);
    REQUIRE(game_prototype_has_graph(prototype, "debug.graph"));
    REQUIRE(game_prototype_remove_graph(prototype, "debug.graph") == 0);
    REQUIRE(!game_prototype_has_graph(prototype, "debug.graph"));
    REQUIRE(game_prototype_remove_component(prototype, "GameTransform") == 0);
    REQUIRE(acount(prototype->components) == 1);
    REQUIRE(game_prototype_add_native_component(prototype, &GAME_COMPONENT_GameTransform) == 0);
    REQUIRE(acount(prototype->components) == 2);

    copy = game_prototype_clone(prototype, error, sizeof error);
    REQUIRE(copy);
    GAME_GameHealth_get(world, entity)->current = 7;
    values = game_prototype_capture_overrides_value(copy, world, entity);
    REQUIRE(values);
    const av_value *health = av_obj_get(values, "GAME.GameHealth");
    REQUIRE(av_get_int(av_obj_get(health, "current")) == 7);
    REQUIRE(!av_obj_get(health, "maximum"));
    av_value_free(values);

    /* Sparse field overrides apply without zeroing sibling fields. */
    values = test_read_asset_value("GameHealth current=7\n");
    ecs_entity overridden = { 0 };
    REQUIRE(game_prototype_spawn(copy, world, &hooks, &overridden, error, sizeof error) == ECS_OK);
    REQUIRE(
        game_prototype_apply_components_value(world, overridden, values, &registry, &owned_strings, error, sizeof error) ==
        0
    );
    REQUIRE(GAME_GameHealth_get(world, overridden)->current == 7);
    REQUIRE(GAME_GameHealth_get(world, overridden)->maximum == 50);
    av_value_free(values);
    values = NULL;

    GAME_GameHealth_get(world, entity)->current = 100;
    game_prototype_free(copy);
    copy = game_prototype_capture_entity("CapturedEnemy", world, entity, &registry);
    REQUIRE(copy);
    values = game_prototype_components_value(copy);
    REQUIRE(values && av_obj_get(values, "GAME.GameHealth"));

    /* Freeing a prototype does not change what it spawned. */
    game_prototype_free(prototype);
    prototype = NULL;
    const GameHealth *spawned = GAME_GameHealth_get(world, entity);
    REQUIRE(spawned && spawned->current == 100 && spawned->maximum == 50);
    return true;
}

/* Reflected values save readably: enum labels, hex colors and floats in their
 * shortest form, unchanged by a second round trip. */
TEST_CASE(scene_documents_save_reflected_values_readably)
{
    char error[256];
    scene = game_scene_document_load(
        "version 1\n"
        "name \"Readable\"\n"
        "(array)entities {\n"
        "    - {\n"
        "        name \"Fluid\"\n"
        "        prototype \"Base\"\n"
        "        components {\n"
        "            GameFluidVolume kind=1 color=966781183 absorption=0.035\n"
        "            GameTransform x=0.15000000596046448 y=-0.0\n"
        "            GameCollider category=16\n"
        "        }\n"
        "    }\n"
        "}\n",
        error,
        sizeof error
    );
    REQUIRE(scene);
    saved = game_scene_document_save(scene, game_components());
    REQUIRE(saved && strstr(saved, "kind=\"Liquid\"") && strstr(saved, "color=0x399fe8ff"));
    REQUIRE(strstr(saved, "absorption=0.035") && strstr(saved, "x=0.15"));
    REQUIRE(strstr(saved, "y=-0.0") && strstr(saved, "category=16"));
    loaded = game_scene_document_load(saved, error, sizeof error);
    REQUIRE(loaded);
    again = game_scene_document_save(loaded, game_components());
    REQUIRE(again && !strcmp(saved, again));
    av_value *before = av_obj_get(scene->entities[0].components, "GameTransform");
    av_value *after = av_obj_get(loaded->entities[0].components, "GameTransform");
    float a = (float)av_get_num(av_obj_get(before, "x"));
    float b = (float)av_get_num(av_obj_get(after, "x"));
    REQUIRE(!memcmp(&a, &b, sizeof a));
    REQUIRE(signbit(av_get_num(av_obj_get(after, "y"))));
    return true;
}

/* A scene entity's prototype, behaviors and component overrides round-trip,
 * enum overrides save as labels without truncation, and physics settings
 * round-trip or fall back to defaults. */
TEST_CASE(scene_documents_round_trip_entities_and_physics)
{
    const char *behaviors[] = { "SceneBehavior", "Damage" };
    scene = game_scene_document_new("CombatScene");
    REQUIRE(scene && game_scene_document_add_entity(scene, -1, "Player", "PlayerPrototype") == 0);
    REQUIRE(game_scene_document_set_entity_behaviors(scene, 0, behaviors, 2) == 0);
    values = test_read_asset_value("GameHealth current=7 maximum=9\n");
    REQUIRE(game_scene_document_set_entity_components(scene, 0, values) == 0);
    REQUIRE(av_obj_get(scene->entities[0].components, "GameHealth"));
    av_value_free(values);

    values = test_read_asset_value("GameAppearance shape=0\n");
    REQUIRE(game_scene_document_set_entity_components(scene, 0, values) == 0);
    saved = game_scene_document_save(scene, game_components());
    char error[256];
    av_doc *labeled = av_doc_read(saved, error, sizeof error);
    av_value *entity = labeled
                         ? av_arr_get(av_obj_get(av_doc_root(labeled), "entities"), 0)
                         : NULL;
    av_value *appearance = av_obj_get(av_obj_get(entity, "components"), "GameAppearance");
    bool label = labeled && av_is_str(av_obj_get(appearance, "shape"));
    av_doc_free(labeled);
    REQUIRE(label);
    REQUIRE(av_get_int(av_obj_get(av_obj_get(values, "GameAppearance"), "shape")) == 0);
    av_value_free(values);
    free(saved);
    saved = NULL;

    values = test_read_asset_value(
        "GameHealth current=7 maximum=9\n"
        "GameAppearance shape=4294967296\n"
    );
    REQUIRE(game_scene_document_set_entity_components(scene, 0, values) == 0);
    scene->physics = (game_physics_settings){ .workers = 8,
                                              .substeps = 2,
                                              .sleep = false,
                                              .continuous = true,
                                              .force_drive = true,
                                              .contact_hertz = 15,
                                              .contact_damping = 1 };
    saved = game_scene_document_save(scene, game_components());
    REQUIRE(saved && strstr(saved, "4294967296"));
    loaded = game_scene_document_load(saved, error, sizeof error);
    REQUIRE(loaded && game_physics_settings_equal(scene->physics, loaded->physics));
    REQUIRE(acount(loaded->entities) == 1);
    const game_scene_entity *player = &loaded->entities[0];
    REQUIRE(!strcmp(player->name, "Player") && !strcmp(player->prototype, "PlayerPrototype"));
    REQUIRE(av_obj_get(player->components, "GameHealth"));
    REQUIRE(acount(player->behaviors) == 2 && !strcmp(player->behaviors[1], "Damage"));
    again = game_scene_document_save(loaded, game_components());
    REQUIRE(again && !strcmp(saved, again));

    game_scene_document_free(loaded);
    loaded = game_scene_document_load("version 1\nname \"Minimal\"\n(array)entities\n", error, sizeof error);
    REQUIRE(loaded && game_physics_settings_equal(loaded->physics, game_physics_defaults()));
    return true;
}

TEST_CASE(scene_documents_refuse_invalid_physics_names_and_versions)
{
    const char *invalid_physics[] = { "substeps 0;", "substeps 1.5;",
                                      "workers 33;", "contact_damping -1;",
                                      "sleep 0;",    "unknown #true;" };
    char error[256];
    for (int i = 0; i < 6; i++) {
        char text[256];
        snprintf(
            text,
            sizeof text,
            "version 1\nname \"Invalid\"\n(array)entities\nphysics { %s }\n",
            invalid_physics[i]
        );
        loaded = game_scene_document_load(text, error, sizeof error);
        REQUIRE(!loaded && strstr(error, "physics"));
    }
    scene = game_scene_document_new("Duplicate");
    REQUIRE(scene && game_scene_document_add_entity(scene, -1, "A", "P") == 0);
    REQUIRE(game_scene_document_add_entity(scene, -1, "A", "P") != 0);
    loaded = game_scene_document_load("version 2\nname \"Bad\"\n(array)entities\n", error, sizeof error);
    REQUIRE(!loaded && strstr(error, "version 1"));
    return true;
}

void test_prototype_documents()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(rebase_keeps_local_values_over_evolving_defaults);
    RUN_TEST_CASE(retained_strings_survive_clones_and_renames);
    RUN_TEST_CASE(detached_components_spawn_absent_and_reattach);
    RUN_TEST_CASE(prototype_includes_override_and_save_deterministically);
    RUN_TEST_CASE(prototype_spawns_edits_and_captures_overrides);
    RUN_TEST_CASE(scene_documents_save_reflected_values_readably);
    RUN_TEST_CASE(scene_documents_round_trip_entities_and_physics);
    RUN_TEST_CASE(scene_documents_refuse_invalid_physics_names_and_versions);
    pu_clear_setup();
}
