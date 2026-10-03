#include "assets/scene_document.h"
#include "editor/collab/actions_scene.h"
#include "editor/workspace/editor.h"
#include "editor/workspace/scene_internal.h"
#include "graph/document/serialize_internal.h"
#include "graph/model/registry.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/asset_text.h"
#include "support/text_file.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Undo and Redo for the open document, as Studio's Edit menu applies them. */
static int history(app_scene_authoring *scene, bool redo)
{
    if (scene->document.kind == APP_EDITOR_DOCUMENT_PROTOTYPE) {
        return redo ? prototype_redo(scene) : prototype_undo(scene);
    }
    return replay_scene_edit(scene, redo);
}

static void prototype_component_gain(app_scene_authoring *scene, ecs_world *world)
{
    assert(
        app_scene_authoring_register_prototype(
            scene,
            "version 1\n"
            "name \"ComponentGain\"\n"
            "components {\n"
            "    GameTransform {\n"
            "        x 0\n"
            "        y 0\n"
            "    }\n"
            "}\n"
            "(array)behaviors\n",
            NULL
        ) == 0
    );
    assert(app_scene_authoring_add_instance(scene, "ComponentGain") == 0);
    ecs_entity entity = app_scene_authoring_runtime_state(scene).selected_entity;
    assert(!GAME_GameHealth_has(world, entity));
    assert(app_scene_authoring_add_prototype_component(scene, "ComponentGain", "GameHealth") == 0);
    assert(GAME_GameHealth_has(world, entity));
    assert(app_scene_authoring_open_prototype(scene, "ComponentGain") == 0);
    assert(history(scene, false) == 0);
    assert(!GAME_GameHealth_has(world, entity));
    assert(history(scene, true) == 0);
    assert(GAME_GameHealth_has(world, entity));
    puts("prototype components PASS: an added prototype component reaches instances and follows Undo and Redo");
}

static ecs_entity spawn_registered_child(game_live_world *live, ecs_entity owner)
{
    assert(
        game_behavior_dispatch(live->behaviors, owner, "SpawnRegistered", (flow_inputs){ 0 }, NULL) == 1
    );
    assert(game_entities_flush_initializers(live->entities, 0) == 0);
    ecs_entity *spawned = NULL;
    game_entities_copy_live(live->entities, &spawned);
    assert(acount(spawned) > 0);
    ecs_entity entity = spawned[acount(spawned) - 1];
    afree(spawned);
    return entity;
}

static void prototype_spawn_refresh(app_scene_authoring *scene, game_live_world *live, ecs_world *world)
{
    behavior_asset *asset = behavior_asset_new("PrototypeRefreshSpawner");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    graph *g = asset->graph;
    node *spawn = graph_add_node(g, registry_node_by_name("Spawn Prototype"));
    assert(
        spawn &&
        graph_set_pin_default(g, spawn, 0, 1, &(pin_value){ .type = PIN_NAME, .str = "RuntimeRefreshChild" }) ==
            0
    );
    const char *events[] = { "Begin", "SpawnRegistered" };
    for (int i = 0; i < 2; i++) {
        node *event = graph_add_node(g, registry_node_by_name("Event"));
        assert(event && graph_set_event_name(g, event, events[i]) == 0);
        assert(graph_connect_exec(g, event, 0, spawn, 0, 0) == 0);
    }
    assert(
        game_live_add_definition(live, game_live_definition_new("PrototypeRefreshSpawner", asset)) == 0
    );
    behavior_asset *child_behavior = behavior_asset_new("RefreshedChild");
    assert(behavior_asset_set_owner_type(child_behavior, "ecs.entity") == 0);
    assert(
        game_live_add_definition(live, game_live_definition_new("RefreshedChild", child_behavior)) == 0
    );
    assert(
        app_scene_authoring_register_prototype(
            scene,
            "name \"RuntimeRefreshChild\"\n"
            "components {\n"
            "    GameHealth {\n"
            "        current 73\n"
            "        maximum 100\n"
            "    }\n"
            "}\n",
            NULL
        ) == 0
    );
    assert(
        app_scene_authoring_register_prototype(
            scene,
            "name \"RuntimeRefreshSpawner\"\n"
            "(object)components\n"
            "(array)behaviors {\n"
            "    - \"PrototypeRefreshSpawner\"\n"
            "}\n",
            NULL
        ) == 0
    );
    ecs_entity owner = ecs_world_create_entity(world);
    assert(game_behavior_attach(live->behaviors, owner, "PrototypeRefreshSpawner") == 0);
    ecs_entity original = spawn_registered_child(live, owner);
    assert(GAME_GameHealth_get_const(world, original)->current == 73);
    for (int pass = 0; pass < 6; pass++) {
        int expected = pass == 0 || pass == 2 ? 73 : 19;
        if (pass == 1) {
            assert(
                app_scene_authoring_set_prototype_field(
                    scene,
                    "RuntimeRefreshChild",
                    "GameHealth",
                    "current",
                    (pin_value){ .type = PIN_I32, .i = expected }
                ) == 0
            );
        }
        if (pass == 2) {
            assert(app_scene_authoring_open_prototype(scene, "RuntimeRefreshChild") == 0);
            assert(history(scene, false) == 0);
        }
        if (pass == 3) {
            assert(app_scene_authoring_open_prototype(scene, "RuntimeRefreshChild") == 0);
            assert(history(scene, true) == 0);
        }
        if (pass == 4) {
            assert(
                app_scene_authoring_attach_prototype_behavior(scene, "RuntimeRefreshChild", "RefreshedChild") ==
                0
            );
        }
        if (pass == 5) {
            assert(
                app_scene_authoring_detach_prototype_behavior(scene, "RuntimeRefreshChild", "RefreshedChild") ==
                0
            );
        }
        /* Spawn before any editor frame: mutations update the runtime snapshot immediately. */
        ecs_entity child = spawn_registered_child(live, owner);
        assert(GAME_GameHealth_get_const(world, child)->current == expected);
        assert(
            (game_behavior_instance(live->behaviors, child, "RefreshedChild") !=
             NULL) == (pass == 4)
        );
        assert(GAME_GameHealth_get_const(world, original)->current == 73);
        assert(app_scene_authoring_preview_prototype(scene, "RuntimeRefreshSpawner") == 0);
        assert(app_scene_authoring_frame(scene, (double)pass) == FLOW_OK);
        const ecs_world *preview = app_scene_authoring_preview_world(scene);
        ecs_component_id health = GAME_COMPONENT_GameHealth.id;
        ecs_query query;
        assert(ecs_query_begin((ecs_world *)preview, &query, &health, 1) == ECS_OK);
        assert(ecs_query_next(&query, &child));
        assert(GAME_GameHealth_get_const(preview, child)->current == expected);
        assert(!ecs_query_next(&query, &child));
        ecs_query_end(&query);
        app_scene_authoring_stop_prototype_preview(scene);
    }
    assert(game_behavior_destroy_entity(live->behaviors, owner) == 0);
    game_entities_reset(live->entities);
    puts("prototype spawn refresh PASS: initial registration, edits, Undo/Redo and behavior attachment changes reach the next scene/preview spawn without changing earlier instances");
}

static void duplicate_instance_preserves_edits(app_scene_authoring *scene, ecs_world *world)
{
    assert(app_scene_authoring_add_instance(scene, "First") == 0);
    ecs_entity original = app_scene_authoring_runtime_state(scene).selected_entity;
    GAME_GameTransform_get(world, original)->x = 123;
    GAME_GameHealth_get(world, original)->current = 42;
    assert(app_scene_authoring_duplicate_instance(scene) == 0);
    ecs_entity copy = app_scene_authoring_runtime_state(scene).selected_entity;
    assert(ecs_entity_to_u64(copy) != ecs_entity_to_u64(original));
    assert(GAME_GameTransform_get_const(world, copy)->x == 123);
    assert(GAME_GameHealth_get_const(world, copy)->current == 42);
    assert(history(scene, false) == 0);
    assert(!ecs_entity_is_alive(world, copy));
    assert(ecs_entity_is_alive(world, original));
    assert(history(scene, true) == 0);
    copy = app_scene_authoring_runtime_state(scene).selected_entity;
    assert(GAME_GameHealth_get_const(world, copy)->current == 42);
    GAME_GameHealth_get(world, copy)->current = 10;
    assert(GAME_GameHealth_get_const(world, original)->current == 42);
}

static void prototype_vectors_and_text(app_scene_authoring *scene, ecs_world *world)
{
    const char *json = "name \"GuideTest\"\n"
                       "components {\n"
                       "    (object)GameTransform\n"
                       "    (object)GameAim\n"
                       "    (object)GameUi\n"
                       "}\n"
                       "(array)behaviors\n";
    assert(app_scene_authoring_register_prototype(scene, json, NULL) == 0);
    assert(app_scene_authoring_add_instance(scene, "GuideTest") == 0);
    ecs_entity entity = app_scene_authoring_runtime_state(scene).selected_entity;
    CF_V2 direction = { 0.5f, 1.0f };
    assert(
        app_scene_authoring_set_prototype_field(
            scene,
            "GuideTest",
            "GameAim",
            "direction",
            (pin_value){ .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = &direction }
        ) == 0
    );
    assert(GAME_GameAim_get_const(world, entity)->direction.x == 0.5f);
    assert(app_scene_authoring_open_prototype(scene, "GuideTest") == 0);
    assert(history(scene, false) == 0);
    assert(GAME_GameAim_get_const(world, entity)->direction.x == 0);
    assert(history(scene, true) == 0);
    assert(GAME_GameAim_get_const(world, entity)->direction.y == 1);
    char text[] = "Guide";
    assert(
        app_scene_authoring_set_prototype_field(
            scene,
            "GuideTest",
            "GameUi",
            "document",
            (pin_value){ .type = PIN_STR, .str = text }
        ) == 0
    );
    text[0] = 'X';
    assert(!strcmp(GAME_GameUi_get_const(world, entity)->document, "Guide"));

    scene_authoring_select_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    scene_actor *actor = NULL;
    for (int i = 0; i < acount(scene->actors); i++) {
        if (ecs_entity_to_u64(scene->actors[i].entity) == ecs_entity_to_u64(entity)) {
            actor = &scene->actors[i];
            break;
        }
    }
    assert(actor);
    CF_V2 replayed = { -4.5f, 7.25f };
    yyjson_mut_doc *doc = NULL;
    yyjson_mut_val *root = scene_action_root(scene, "property.set", &doc);
    yyjson_mut_obj_add_uint(doc, root, "actor", actor->identity);
    yyjson_mut_obj_add_strcpy(doc, root, "component", "GameAim");
    yyjson_mut_obj_add_strcpy(doc, root, "field", "direction");
    av_doc *value_doc = av_doc_new();
    av_value *value = serialize_value(
        value_doc,
        &(pin_value){ .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = &replayed }
    );
    yyjson_mut_obj_add_val(doc, root, "value", av_to_json(doc, value));
    av_doc_free(value_doc);
    char *action = scene_action_write(doc);
    char error[256] = { 0 };
    assert(action && scene_collab_apply_action(scene, action, error, sizeof error));
    free(action);
    assert(GAME_GameAim_get_const(world, entity)->direction.x == replayed.x);
    assert(GAME_GameAim_get_const(world, entity)->direction.y == replayed.y);
    root = scene_action_root(scene, "property.set", &doc);
    yyjson_mut_obj_add_uint(doc, root, "actor", actor->identity);
    yyjson_mut_obj_add_strcpy(doc, root, "component", "GameAim");
    yyjson_mut_obj_add_strcpy(doc, root, "field", "direction");
    yyjson_mut_val *invalid = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, root, "value", invalid);
    yyjson_mut_obj_add_real(doc, invalid, "x", 1e39);
    yyjson_mut_obj_add_real(doc, invalid, "y", 2);
    action = scene_action_write(doc);
    assert(action && !scene_collab_apply_action(scene, action, error, sizeof error));
    free(action);
    assert(GAME_GameAim_get_const(world, entity)->direction.x == replayed.x);
    assert(GAME_GameAim_get_const(world, entity)->direction.y == replayed.y);
    puts("prototype and scene vectors PASS: prototype Undo/Redo, collab replay, and owned text");
}

/* The file at `path`, which `canonical` turns into the text
   chibi_ctl --format writes for its JSON. */
static char *saved_canonically(const char *path, char *(*canonical)(const char *json))
{
    char *text = text_file_read(path);
    assert(text);
    char *json = canonical(text);
    char error[256];
    char *disk = asset_text_to_disk(json, error, sizeof error);
    assert(disk && !strcmp(disk, text));
    free(json);
    free(disk);
    return text;
}

static char *canonical_scene(const char *text)
{
    char error[256];
    game_scene_document *document = game_scene_document_load(text, error, sizeof error);
    assert(document);
    char *json = game_scene_document_save(document);
    game_scene_document_free(document);
    return json;
}

static char *canonical_prototype(const char *text)
{
    game_prototype_registry registry = app_component_registry();
    char error[256];
    game_prototype *prototype = game_prototype_load(text, &registry, error, sizeof error);
    assert(prototype);
    char *json = game_prototype_save(prototype);
    game_prototype_free(prototype);
    return json;
}

static const char *actor_name(app_scene_authoring *scene, ecs_entity entity)
{
    for (int i = 0; i < acount(scene->actors); i++) {
        if (ecs_entity_to_u64(scene->actors[i].entity) == ecs_entity_to_u64(entity)) {
            return scene->actors[i].name;
        }
    }
    return NULL;
}

/* A rename is one Scene Undo step, refuses a name in use or an empty one,
   survives a history checkpoint, and saves as the entity's name. */
static void rename_instance(app_scene_authoring *scene, const char *folder)
{
    assert(app_scene_authoring_add_instance(scene, "First") == 0);
    ecs_entity other = app_scene_authoring_runtime_state(scene).selected_entity;
    assert(app_scene_authoring_add_instance(scene, "First") == 0);
    ecs_entity entity = app_scene_authoring_runtime_state(scene).selected_entity;
    char *before = strdup(actor_name(scene, entity));
    scene->scene_dirty = false;
    assert(app_scene_authoring_rename_instance(scene, entity, "Hero") == 0);
    assert(!strcmp(actor_name(scene, entity), "Hero") && scene->scene_dirty);
    assert(app_scene_authoring_rename_instance(scene, other, "Hero") == -1);
    assert(strstr(scene->message, "Hero") && strcmp(actor_name(scene, other), "Hero"));
    assert(app_scene_authoring_rename_instance(scene, other, "") == -1);
    /* The same name changes nothing and records nothing. */
    assert(app_scene_authoring_rename_instance(scene, entity, "Hero") == 0);
    uint64_t id = ecs_entity_to_u64(entity);
    char *checkpoint = scene_history_checkpoint(scene->history);
    scene_history *restored = scene_history_new();
    assert(scene_history_restore_checkpoint(restored, checkpoint, &id, &id, 1));
    ecs_entity at;
    const char *undone = scene_history_peek_rename(restored, false, &at);
    assert(undone && !strcmp(undone, before) && ecs_entity_to_u64(at) == id);
    scene_history_free(restored);
    free(checkpoint);
    assert(history(scene, false) == 0);
    assert(!strcmp(actor_name(scene, entity), before));
    assert(history(scene, true) == 0);
    assert(!strcmp(actor_name(scene, entity), "Hero"));
    char path[1024];
    snprintf(path, sizeof path, "%s/Renamed.kdl", folder);
    assert(app_scene_authoring_save(scene, path) == 0);
    char *text = saved_canonically(path, canonical_scene);
    assert(strstr(text, "name \"Hero\"\n"));
    free(text);
    free(before);
    puts("rename instance PASS: a rename is one Undo step, refuses names in use, survives checkpoints and saves");
}

/* Whether the object keeps its values of a component its prototype lost. */
static bool retains(app_scene_authoring *scene, ecs_entity entity, const char *component)
{
    for (int i = 0; i < acount(scene->actors); i++) {
        const scene_actor *actor = &scene->actors[i];
        for (int j = 0; ecs_entity_to_u64(actor->entity) == ecs_entity_to_u64(entity) &&
                        j < acount(actor->retained);
             j++) {
            if (!strcmp(actor->retained[j].name, component)) {
                return true;
            }
        }
    }
    return false;
}

static bool has_component(app_scene_authoring *scene, const char *prototype, ecs_component_id id)
{
    return prototype_has_component_id(find_prototype(scene, prototype)->prototype, id);
}

/* Adding and removing a prototype's component is one step of its Undo each;
   an instance's override of a removed component returns with it, and the
   prototype saves in canonical form. */
static void prototype_component_removal(app_scene_authoring *scene, ecs_world *world, const char *folder)
{
    assert(
        app_scene_authoring_register_prototype(
            scene,
            "version 1\n"
            "name \"Removable\"\n"
            "components {\n"
            "    GameTransform {\n"
            "        x 0\n"
            "        y 0\n"
            "    }\n"
            "    GameHealth {\n"
            "        current 73\n"
            "        maximum 100\n"
            "    }\n"
            "}\n"
            "(array)behaviors\n",
            NULL
        ) == 0
    );
    assert(app_scene_authoring_add_instance(scene, "Removable") == 0);
    ecs_entity entity = app_scene_authoring_runtime_state(scene).selected_entity;
    GAME_GameHealth_get(world, entity)->current = 42;
    scene_prototype_asset *asset = find_prototype(scene, "Removable");
    int steps = acount(asset->undo);
    assert(app_scene_authoring_add_prototype_component(scene, "Removable", "GameHealth") == -1);
    assert(app_scene_authoring_remove_prototype_component(scene, "Removable", "GameAim") == -1);
    assert(acount(asset->undo) == steps);
    assert(app_scene_authoring_remove_prototype_component(scene, "Removable", "GameHealth") == 0);
    assert(acount(asset->undo) == steps + 1 && asset->dirty);
    assert(!has_component(scene, "Removable", GAME_COMPONENT_GameHealth.id));
    assert(!GAME_GameHealth_has(world, entity) && retains(scene, entity, "GameHealth"));
    assert(app_scene_authoring_add_prototype_component(scene, "Removable", "GameAim") == 0);
    assert(acount(asset->undo) == steps + 2 && GAME_GameAim_has(world, entity));
    assert(app_scene_authoring_open_prototype(scene, "Removable") == 0);
    assert(history(scene, false) == 0);
    /* The prototype's own values leave nothing behind in the scene. */
    assert(!GAME_GameAim_has(world, entity) && !retains(scene, entity, "GameAim"));
    assert(history(scene, false) == 0);
    assert(has_component(scene, "Removable", GAME_COMPONENT_GameHealth.id));
    assert(GAME_GameHealth_get_const(world, entity)->current == 42);
    assert(!retains(scene, entity, "GameHealth"));
    assert(history(scene, true) == 0);
    assert(!GAME_GameHealth_has(world, entity));
    char path[1024];
    snprintf(path, sizeof path, "%s/Removable.kdl", folder);
    assert(app_scene_authoring_save_prototype(scene, "Removable", path) == 0);
    char *text = saved_canonically(path, canonical_prototype);
    assert(!strstr(text, "GameHealth") && strstr(text, "GameTransform"));
    free(text);
    assert(history(scene, true) == 0);
    assert(GAME_GameAim_has(world, entity));
    assert(app_scene_authoring_save_prototype(scene, "Removable", path) == 0);
    text = saved_canonically(path, canonical_prototype);
    assert(strstr(text, "GameAim"));
    free(text);
    puts("prototype component removal PASS: add and remove are one Undo step each, instances keep their overrides, and the prototype saves canonically");
}

static float position_x(ecs_world *world, const scene_actor *actor)
{
    return GAME_GameTransform_get_const(world, actor->entity)->x;
}

static int health(ecs_world *world, const scene_actor *actor)
{
    return GAME_GameHealth_get_const(world, actor->entity)->current;
}

static void write_health(app_scene_authoring *scene, const scene_actor *actor, int current)
{
    pin_value value = { .type = PIN_I32, .i = current };
    assert(
        ecs_component_write_field(
            scene->world,
            actor->entity,
            GAME_COMPONENT_GameHealth.id,
            0,
            value
        ) == ECS_OK
    );
}

/* While the scene runs, moves and writes change only its world: no Undo step
   and no unsaved mark. Keep Changes waits for Stop, which applies the values
   each object's latest record decides as one Undo step. */
static void live_edits_keep_changes(app_scene_authoring *scene, ecs_world *world)
{
    assert(app_scene_authoring_add_instance(scene, "First") == 0);
    int index = acount(scene->actors) - 1;
    select_instance(scene, index);
    float authored_x = position_x(world, &scene->actors[index]);
    int authored_health = health(world, &scene->actors[index]);
    scene_history_clear(scene->history);
    assert(
        app_scene_authoring_runtime_command(scene, APP_RUNTIME_SCENE, APP_RUNTIME_PLAY) == 0 &&
        app_scene_authoring_runtime_command(scene, APP_RUNTIME_SCENE, APP_RUNTIME_PAUSE) == 0
    );
    scene_actor *actor = &scene->actors[index];
    scene->scene_dirty = false;
    GameTransform moved = *GAME_GameTransform_get_const(world, actor->entity);
    moved.x = authored_x + 9;
    begin_transform_edit(scene, actor->entity);
    write_transform_edit(scene, actor->entity, moved);
    finish_transform_edit(scene, false);
    write_health(scene, actor, 50);
    assert(position_x(world, actor) == moved.x && health(world, actor) == 50);
    assert(!scene->scene_dirty && !scene_history_can_undo(scene->history));
    assert(scene_keep_changes(scene, NULL) == 2);
    /* Undo in its toast forgets the latest record only. */
    write_health(scene, actor, 40);
    assert(scene_keep_changes(scene, &GAME_COMPONENT_GameHealth) == 1);
    scene_kept_drop(scene);
    /* A later record of one component decides that component's values. */
    write_health(scene, actor, 30);
    assert(scene_keep_changes(scene, &GAME_COMPONENT_GameHealth) == 1);
    int objects;
    const char *name;
    assert(scene_kept_values(scene, &objects, &name) == 2);
    assert(objects == 1 && !strcmp(name, actor->name));
    assert(!scene->scene_dirty);
    assert(app_scene_authoring_runtime_command(scene, APP_RUNTIME_SCENE, APP_RUNTIME_STOP) == 0);
    actor = &scene->actors[index];
    assert(position_x(world, actor) == moved.x && health(world, actor) == 30);
    assert(scene->scene_dirty && !acount(scene->kept));
    assert(history(scene, false) == 0);
    assert(position_x(world, actor) == authored_x && health(world, actor) == authored_health);
    assert(!scene_history_can_undo(scene->history));
    assert(history(scene, true) == 0);
    assert(position_x(world, actor) == moved.x && health(world, actor) == 30);
    puts("live edits PASS: running edits leave the scene clean, and Keep Changes applies at Stop as one Undo step");
}

void test_scene_instances()
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    behavior_asset *asset = behavior_asset_new("SceneInstances");
    editor *e = editor_new(asset);
    game_live_world *live = game_live_world_new(world);
    game_live_definition *definition = game_live_definition_new(
        "SceneInstances",
        behavior_asset_new("SceneInstances")
    );
    assert(game_live_add_definition(live, definition) == 0);
    engine *runtime = engine_new(world, live);
    app_scene_authoring *
        scene = app_scene_authoring_new(world, live, definition, asset, e, NULL, NULL, runtime);
    assert(scene);
    const char *first = "version 1\n"
                        "name \"First\"\n"
                        "components {\n"
                        "    GameTransform {\n"
                        "        x 0\n"
                        "        y 0\n"
                        "    }\n"
                        "    GameHealth {\n"
                        "        current 73\n"
                        "        maximum 100\n"
                        "    }\n"
                        "}\n"
                        "(array)behaviors\n";
    assert(app_scene_authoring_register_prototype(scene, first, NULL) == 0);
    prototype_component_gain(scene, world);
    prototype_spawn_refresh(scene, live, world);
    duplicate_instance_preserves_edits(scene, world);
    prototype_vectors_and_text(scene, world);
    live_edits_keep_changes(scene, world);
    char folder[] = "/tmp/chibi-scene-instances-XXXXXX";
    assert(mkdtemp(folder));
    rename_instance(scene, folder);
    prototype_component_removal(scene, world, folder);
    char path[1024];
    snprintf(path, sizeof path, "%s/Renamed.kdl", folder);
    unlink(path);
    snprintf(path, sizeof path, "%s/Removable.kdl", folder);
    unlink(path);
    rmdir(folder);
    app_scene_authoring_free(scene);
    engine_free(runtime);
    game_live_world_free(live);
    editor_free(e);
    ecs_world_free(world);
}
