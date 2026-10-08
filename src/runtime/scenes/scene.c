#include "runtime/scenes/scene.h"

#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct game_scene
{
    int references;
    arena storage;
    game_scene_spec spec;
};
struct game_scene_instance
{
    game_scene *scene;
    ecs_world *world;
    game_source_registry *sources;
    ecs_entity *entities;
};
static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static const char *own(arena *storage, const char *text)
{
    if (!text) {
        return NULL;
    }
    size_t size = strlen(text) + 1;
    char *copy = arena_alloc(storage, size);
    memcpy(copy, text, size);
    return copy;
}
static bool same_name(const char *a, const char *b)
{
    return a == b || (a && b && !strcmp(a, b));
}
static bool same_component(const ecs_component_desc *a, const ecs_component_desc *b)
{
    return a->id == b->id && a->element_size == b->element_size &&
           same_name(a->key, b->key) && same_name(a->name, b->name) &&
           a->index == b->index &&
           ecs_layout_hash(a->type) == ecs_layout_hash(b->type);
}
game_scene *game_scene_prepare(const game_scene_spec *spec, char *error, size_t size)
{
    assert(spec && (!size || error));
    if (size) {
        error[0] = '\0';
    }
    if (!spec->name || !spec->name[0] || spec->object_count < 0 ||
        (spec->object_count && !spec->objects) ||
        !game_physics_settings_valid(spec->physics)) {
        fail(error, size, "invalid prepared scene metadata");
        return NULL;
    }
    game_scene *scene = xcalloc(1, sizeof *scene);
    scene->references = 1;
    scene->spec = *spec;
    scene->spec.name = own(&scene->storage, spec->name);
    scene->spec.ui = own(&scene->storage, spec->ui);
    game_scene_object *objects = arena_alloc(&scene->storage, (size_t)spec->object_count * sizeof *objects);
    scene->spec.objects = objects;
    for (int i = 0; i < spec->object_count; i++) {
        const game_scene_object *input = &spec->objects[i];
        if (!input->name || !input->name[0] || input->component_count < 0 ||
            input->behavior_count < 0 ||
            (input->component_count && !input->components) ||
            (input->behavior_count && !input->behaviors)) {
            fail(error, size, "invalid prepared scene object");
            goto failed;
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(objects[j].name, input->name)) {
                fail(error, size, "prepared scene object name is duplicated");
                goto failed;
            }
        }
        objects[i] = *input;
        objects[i].name = own(&scene->storage, input->name);
        game_scene_component *components = arena_alloc(
            &scene->storage,
            (size_t)input->component_count * sizeof *components
        );
        objects[i].components = components;
        for (int j = 0; j < input->component_count; j++) {
            const game_scene_component *component = &input->components[j];
            const ecs_component_desc *desc = component->desc;
            if (!desc || !desc->id || !desc->name || !desc->type || !desc->element_size ||
                desc->type->size != desc->element_size || !component->data) {
                fail(error, size, "invalid prepared scene component");
                goto failed;
            }
            if (desc->layout_hash && desc->layout_hash != ecs_layout_hash(desc->type)) {
                fail(error, size, "prepared scene component layout hash differs");
                goto failed;
            }
            for (int object = 0; object < i; object++) {
                for (int field = 0; field < objects[object].component_count; field++) {
                    const ecs_component_desc
                        *previous = objects[object].components[field].desc;
                    if ((previous->id == desc->id ||
                         (previous->key && desc->key &&
                          same_name(previous->key, desc->key)) ||
                         same_name(previous->name, desc->name)) &&
                        !same_component(previous, desc)) {
                        fail(error, size, "prepared scene component identity has conflicting layouts");
                        goto failed;
                    }
                }
            }
            for (int k = 0; k < j; k++) {
                const ecs_component_desc *previous = components[k].desc;
                if (previous->id == desc->id || same_name(previous->name, desc->name) ||
                    (previous->key && desc->key && same_name(previous->key, desc->key))) {
                    fail(error, size, "prepared scene object component is duplicated");
                    goto failed;
                }
            }
            ecs_component_desc *copy = arena_alloc(&scene->storage, sizeof *copy);
            *copy = *desc;
            copy->name = own(&scene->storage, desc->name);
            copy->key = own(&scene->storage, desc->key);
            copy->column_base = NULL;
            void *data = arena_alloc(&scene->storage, desc->element_size);
            memcpy(data, component->data, desc->element_size);
            if (pin_struct_snapshot_fields(&scene->storage, desc->type, data)) {
                fail(error, size, "prepared scene component payload cannot be snapshotted");
                goto failed;
            }
            components[j] = (game_scene_component){ copy, data };
        }
        const char **behaviors = arena_alloc(
            &scene->storage,
            (size_t)input->behavior_count * sizeof *behaviors
        );
        objects[i].behaviors = behaviors;
        for (int j = 0; j < input->behavior_count; j++) {
            if (!input->behaviors[j] || !input->behaviors[j][0]) {
                fail(error, size, "prepared scene behavior name is missing");
                goto failed;
            }
            for (int k = 0; k < j; k++) {
                if (!strcmp(behaviors[k], input->behaviors[j])) {
                    fail(error, size, "prepared scene behavior name is duplicated");
                    goto failed;
                }
            }
            behaviors[j] = own(&scene->storage, input->behaviors[j]);
        }
    }
    return scene;
failed:
    game_scene_free(scene);
    return NULL;
}
void game_scene_retain(game_scene *scene)
{
    assert(scene && scene->references > 0);
    scene->references++;
}
void game_scene_free(game_scene *scene)
{
    if (!scene || --scene->references) {
        return;
    }
    arena_free(&scene->storage);
    free(scene);
}
const char *game_scene_name(const game_scene *scene)
{
    return scene->spec.name;
}
const char *game_scene_ui(const game_scene *scene)
{
    return scene->spec.ui;
}
bool game_scene_perspective(const game_scene *scene)
{
    return scene->spec.perspective;
}
game_physics_settings game_scene_physics(const game_scene *scene)
{
    return scene->spec.physics;
}
int game_scene_object_count(const game_scene *scene)
{
    return scene->spec.object_count;
}
const char *game_scene_object_name(const game_scene *scene, int index)
{
    assert(index >= 0 && index < scene->spec.object_count);
    return scene->spec.objects[index].name;
}
size_t game_scene_storage_bytes(const game_scene *scene)
{
    size_t capacity = arena_storage_bytes(&scene->storage);
    assert(capacity <= SIZE_MAX - sizeof *scene);
    return capacity + sizeof *scene;
}
int game_scene_object_component_count(const game_scene *scene, int object)
{
    assert(object >= 0 && object < scene->spec.object_count);
    return scene->spec.objects[object].component_count;
}
game_scene_component game_scene_object_component_at(const game_scene *scene, int object, int component)
{
    assert(object >= 0 && object < scene->spec.object_count);
    assert(component >= 0 && component < scene->spec.objects[object].component_count);
    return scene->spec.objects[object].components[component];
}
static bool validate_world(const game_scene *scene, ecs_world *world, bool installed, char *error, size_t size)
{
    for (int i = 0; i < scene->spec.object_count; i++) {
        const game_scene_object *object = &scene->spec.objects[i];
        for (int j = 0; j < object->component_count; j++) {
            const ecs_component_desc *desc = object->components[j].desc;
            const ecs_component_desc *found = ecs_world_component(world, desc->id);
            if ((found && !same_component(desc, found)) || (installed && !found)) {
                return fail(error, size, "prepared scene component schema differs from the world");
            }
            for (int k = 0; k < ecs_world_component_count(world); k++) {
                const ecs_component_desc *previous = ecs_world_component_at(world, k);
                if (previous->id != desc->id &&
                    (same_name(previous->name, desc->name) ||
                     (previous->key && desc->key &&
                      same_name(previous->key, desc->key)))) {
                    return fail(error, size, "prepared scene component name has a conflicting identity");
                }
            }
        }
    }
    return true;
}
int game_scene_register_components(const game_scene *scene, ecs_world *world, char *error, size_t size)
{
    assert(scene && world && (!size || error) && !ecs_world_command_count(world));
    if (!validate_world(scene, world, false, error, size)) {
        return -1;
    }
    for (int i = 0; i < scene->spec.object_count; i++) {
        const game_scene_object *object = &scene->spec.objects[i];
        for (int j = 0; j < object->component_count; j++) {
            const ecs_component_desc *desc = object->components[j].desc;
            if (!ecs_world_component(world, desc->id) &&
                ecs_world_register_component(world, desc, NULL)) {
                fail(error, size, "prepared scene component could not be registered");
                return -1;
            }
        }
    }
    return 0;
}
void game_scene_instance_free(game_scene_instance *instance)
{
    if (!instance) {
        return;
    }
    for (int i = 0; i < acount(instance->entities); i++) {
        ecs_entity entity = instance->entities[i];
        if (instance->sources) {
            game_source_detach_entity(instance->sources, entity);
        }
        if (ecs_entity_is_alive(instance->world, entity)) {
            int status = ecs_world_destroy_entity(instance->world, entity);
            assert(status == ECS_OK);
            (void)status;
        }
    }
    afree(instance->entities);
    game_scene_free(instance->scene);
    free(instance);
}
static game_scene_instance *instantiate(
    game_scene *scene,
    ecs_world *world,
    game_source_registry *sources,
    bool attach,
    char *error,
    size_t size
)
{
    assert(scene && world && (!size || error) && !ecs_world_command_count(world));
    assert(!sources || game_source_world(sources) == world);
    if (size) {
        error[0] = '\0';
    }
    if (!validate_world(scene, world, true, error, size)) {
        return NULL;
    }
    for (int i = 0; i < scene->spec.object_count; i++) {
        const game_scene_object *object = &scene->spec.objects[i];
        for (int j = 0; attach && j < object->behavior_count; j++) {
            if (!sources || !game_source_registered(sources, object->behaviors[j])) {
                fail(error, size, "prepared scene behavior module is not registered");
                return NULL;
            }
        }
    }
    game_scene_instance *instance = xcalloc(1, sizeof *instance);
    instance->scene = scene;
    instance->world = world;
    instance->sources = sources;
    game_scene_retain(scene);
    for (int i = 0; i < scene->spec.object_count; i++) {
        ecs_entity entity = ecs_world_record_create(world, (uint64_t)i);
        apush(instance->entities, entity);
        if (!entity.slot) {
            goto failed;
        }
    }
    if (ecs_world_apply_commands(world)) {
        goto failed;
    }
    for (int i = 0; i < scene->spec.object_count; i++) {
        ecs_entity entity = instance->entities[i];
        const game_scene_object *object = &scene->spec.objects[i];
        for (int j = 0; j < object->component_count; j++) {
            if (ecs_world_record_add(world, entity, object->components[j].desc->id, NULL, (uint64_t)i)) {
                goto failed;
            }
        }
    }
    if (ecs_world_apply_commands(world)) {
        goto failed;
    }
    for (int i = 0; i < scene->spec.object_count; i++) {
        const game_scene_object *object = &scene->spec.objects[i];
        ecs_entity entity = instance->entities[i];
        for (int j = 0; j < object->component_count; j++) {
            const game_scene_component *component = &object->components[j];
            pin_value value = { .type = PIN_STRUCT,
                                .type_name = component->desc->type->name,
                                .ptr = (void *)component->data };
            if (ecs_component_write_value(world, entity, component->desc->id, 0, value, value)) {
                goto failed;
            }
        }
    }
    for (int i = 0; i < scene->spec.object_count; i++) {
        const game_scene_object *object = &scene->spec.objects[i];
        for (int j = 0; attach && j < object->behavior_count; j++) {
            if (game_source_attach(sources, instance->entities[i], object->behaviors[j], true, 0, 0)) {
                goto failed;
            }
        }
    }
    return instance;
failed:
    ecs_world_clear_commands(world);
    game_scene_instance_free(instance);
    fail(error, size, "prepared scene could not instantiate");
    return NULL;
}
const game_scene *game_scene_instance_scene(const game_scene_instance *instance)
{
    assert(instance);
    return instance->scene;
}
ecs_entity game_scene_instance_entity(const game_scene_instance *instance, int index)
{
    assert(index >= 0 && index < acount(instance->entities));
    return instance->entities[index];
}

int game_scene_object_behavior_count(const game_scene *scene, int index)
{
    assert(index >= 0 && index < scene->spec.object_count);
    return scene->spec.objects[index].behavior_count;
}
const char *game_scene_object_behavior(const game_scene *scene, int object, int behavior)
{
    assert(object >= 0 && object < scene->spec.object_count);
    assert(behavior >= 0 && behavior < scene->spec.objects[object].behavior_count);
    return scene->spec.objects[object].behaviors[behavior];
}
game_scene_instance *game_scene_instantiate(
    game_scene *scene,
    ecs_world *world,
    game_source_registry *sources,
    char *error,
    size_t size
)
{
    return instantiate(scene, world, sources, true, error, size);
}
game_scene_instance *game_scene_instantiate_components(
    game_scene *scene,
    ecs_world *world,
    char *error,
    size_t size
)
{
    return instantiate(scene, world, NULL, false, error, size);
}
ecs_entity game_scene_instance_take_entity(game_scene_instance *instance)
{
    assert(instance && !instance->sources && acount(instance->entities) == 1);
    ecs_entity entity = instance->entities[0];
    aclear(instance->entities);
    game_scene_instance_free(instance);
    return entity;
}
