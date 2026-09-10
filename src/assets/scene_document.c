#include "assets/scene_document.h"

#include "../third_party/yyjson/yyjson.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char *error, size_t size, const char *format, ...)
{
    if (!error || !size) {
        return;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(error, size, format, args);
    va_end(args);
}

static int name_used(const char *const *names, int count, const char *name)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(names[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void free_names(char ***names)
{
    for (int i = 0; i < acount(*names); i++) {
        free((*names)[i]);
    }
    afree(*names);
}

static void free_entity(game_scene_entity *entity)
{
    free(entity->name);
    free(entity->prototype);
    free(entity->components_json);
    free(entity->retained_json);
    free_names(&entity->behaviors);
}

static int read_name_array(
    yyjson_val *array,
    char ***out,
    char *error,
    size_t error_size,
    int entity,
    const char *kind
)
{
    if (!array) {
        return 0;
    }
    if (!yyjson_is_arr(array)) {
        set_error(error, error_size, "invalid %s at scene entity %d", kind, entity);
        return -1;
    }
    yyjson_arr_iter iterator = yyjson_arr_iter_with(array);
    yyjson_val *value;
    while ((value = yyjson_arr_iter_next(&iterator))) {
        if (!yyjson_is_str(value) || !yyjson_get_len(value) ||
            name_used((const char *const *)*out, acount(*out), yyjson_get_str(value))) {
            set_error(error, error_size, "invalid %s at scene entity %d", kind, entity);
            free_names(out);
            return -1;
        }
        apush(*out, xstrdup(yyjson_get_str(value)));
    }
    return 0;
}

static int copy_names(char ***out, const char *const *names, int count)
{
    for (int i = 0; i < count; i++) {
        if (!names[i] || !names[i][0] || name_used(names, i, names[i])) {
            return -1;
        }
        apush(*out, xstrdup(names[i]));
    }
    return 0;
}

game_scene_document *game_scene_document_new(const char *name)
{
    if (!name || !name[0]) {
        return NULL;
    }
    game_scene_document *document = (game_scene_document *)xcalloc(1, sizeof *document);
    document->name = xstrdup(name);
    return document;
}

void game_scene_document_free(game_scene_document *document)
{
    if (!document) {
        return;
    }
    for (int i = 0; i < acount(document->entities); i++) {
        free_entity(&document->entities[i]);
    }
    afree(document->entities);
    free(document->name);
    free(document);
}

int game_scene_document_add_entity(
    game_scene_document *document,
    const char *name,
    const char *prototype,
    const char *const *behaviors,
    int behavior_count
)
{
    if (!name || !name[0] || !prototype || !prototype[0] ||
        behavior_count < 0 || (behavior_count && !behaviors)) {
        return -1;
    }
    for (int i = 0; i < acount(document->entities); i++) {
        if (strcmp(document->entities[i].name, name) == 0) {
            return -1;
        }
    }
    game_scene_entity entity = {
        .name = xstrdup(name),
        .prototype = xstrdup(prototype),
        .components_json = xstrdup("{}"),
    };
    if (copy_names(&entity.behaviors, behaviors, behavior_count) != 0) {
        free_entity(&entity);
        return -1;
    }
    apush(document->entities, entity);
    return 0;
}

static int set_object_json(char **target, const char *components_json)
{
    if (!components_json) {
        components_json = "{}";
    }
    yyjson_doc *parsed = yyjson_read(components_json, strlen(components_json), 0);
    yyjson_val *root = parsed ? yyjson_doc_get_root(parsed) : NULL;
    if (!root || !yyjson_is_obj(root)) {
        yyjson_doc_free(parsed);
        return -1;
    }
    char *copy = yyjson_val_write(root, YYJSON_WRITE_PRETTY_TWO_SPACES, NULL);
    yyjson_doc_free(parsed);
    if (!copy) {
        return -1;
    }
    free(*target);
    *target = copy;
    return 0;
}

int game_scene_document_set_entity_components(game_scene_document *document, int index, const char *json)
{
    assert(index >= 0 && index < acount(document->entities));
    return set_object_json(&document->entities[index].components_json, json);
}

int game_scene_document_set_entity_retained(game_scene_document *document, int index, const char *json)
{
    assert(index >= 0 && index < acount(document->entities));
    return set_object_json(&document->entities[index].retained_json, json);
}

char *game_scene_document_save(const game_scene_document *document)
{
    yyjson_mut_doc *json = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(json);
    yyjson_mut_doc_set_root(json, root);
    yyjson_mut_obj_add_uint(json, root, "version", 1);
    yyjson_mut_obj_add_strcpy(json, root, "name", document->name);
    yyjson_mut_obj_add_str(json, root, "presentation", document->perspective ? "3d" : "2d");
    yyjson_mut_val *entities = yyjson_mut_arr(json);
    for (int i = 0; i < acount(document->entities); i++) {
        const game_scene_entity *entity = &document->entities[i];
        yyjson_mut_val *value = yyjson_mut_obj(json);
        yyjson_mut_obj_add_strcpy(json, value, "name", entity->name);
        yyjson_mut_obj_add_strcpy(json, value, "prototype", entity->prototype);
        const char *components_json = entity->components_json ? entity->components_json
                                                              : "{}";
        yyjson_doc *components_doc = yyjson_read(components_json, strlen(components_json), 0);
        yyjson_mut_val *components = components_doc &&
                                             yyjson_is_obj(yyjson_doc_get_root(components_doc))
                                       ? yyjson_val_mut_copy(json, yyjson_doc_get_root(components_doc))
                                       : yyjson_mut_obj(json);
        yyjson_mut_obj_add_val(json, value, "components", components);
        yyjson_doc_free(components_doc);
        if (entity->retained_json) {
            yyjson_doc *retained = yyjson_read(
                entity->retained_json,
                strlen(entity->retained_json),
                0
            );
            assert(retained && yyjson_is_obj(yyjson_doc_get_root(retained)));
            if (yyjson_obj_size(yyjson_doc_get_root(retained))) {
                yyjson_mut_obj_add_val(
                    json,
                    value,
                    "retained_components",
                    yyjson_val_mut_copy(json, yyjson_doc_get_root(retained))
                );
            }
            yyjson_doc_free(retained);
        }
        yyjson_mut_val *behaviors = yyjson_mut_arr(json);
        for (int j = 0; j < acount(entity->behaviors); j++) {
            yyjson_mut_arr_add_strcpy(json, behaviors, entity->behaviors[j]);
        }
        yyjson_mut_obj_add_val(json, value, "behaviors", behaviors);
        yyjson_mut_arr_add_val(entities, value);
    }
    yyjson_mut_obj_add_val(json, root, "entities", entities);
    char *result = yyjson_mut_write(json, YYJSON_WRITE_PRETTY_TWO_SPACES, NULL);
    yyjson_mut_doc_free(json);
    return result;
}

static void load_fail(game_scene_document *document, yyjson_doc *parsed, char **behaviors)
{
    free_names(&behaviors);
    game_scene_document_free(document);
    yyjson_doc_free(parsed);
}

game_scene_document *game_scene_document_load(const char *json, char *error, size_t error_size)
{
    if (!json) {
        set_error(error, error_size, "scene document is missing");
        return NULL;
    }
    yyjson_doc *parsed = yyjson_read(json, strlen(json), 0);
    if (!parsed) {
        set_error(error, error_size, "malformed scene document");
        return NULL;
    }
    yyjson_val *root = yyjson_doc_get_root(parsed);
    yyjson_val *version = yyjson_is_obj(root) ? yyjson_obj_get(root, "version") : NULL;
    yyjson_val *name = yyjson_is_obj(root) ? yyjson_obj_get(root, "name") : NULL;
    yyjson_val *entities = yyjson_is_obj(root) ? yyjson_obj_get(root, "entities")
                                               : NULL;
    if (!yyjson_is_uint(version) || yyjson_get_uint(version) != 1 ||
        !yyjson_is_str(name) || !yyjson_get_len(name) || !yyjson_is_arr(entities)) {
        set_error(error, error_size, "scene document requires version 1, name, and entities");
        yyjson_doc_free(parsed);
        return NULL;
    }
    game_scene_document *document = game_scene_document_new(yyjson_get_str(name));
    yyjson_val *presentation = yyjson_obj_get(root, "presentation");
    if (presentation && (!yyjson_is_str(presentation) ||
                         (strcmp(yyjson_get_str(presentation), "2d") &&
                          strcmp(yyjson_get_str(presentation), "3d")))) {
        set_error(error, error_size, "scene presentation must be 2d or 3d");
        game_scene_document_free(document);
        yyjson_doc_free(parsed);
        return NULL;
    }
    document->perspective = presentation &&
                            !strcmp(yyjson_get_str(presentation), "3d");
    yyjson_arr_iter iterator = yyjson_arr_iter_with(entities);
    yyjson_val *entity_value;
    int index = 0;
    while ((entity_value = yyjson_arr_iter_next(&iterator))) {
        yyjson_val *entity_name = yyjson_is_obj(entity_value)
                                    ? yyjson_obj_get(entity_value, "name")
                                    : NULL;
        yyjson_val *prototype = yyjson_is_obj(entity_value)
                                  ? yyjson_obj_get(entity_value, "prototype")
                                  : NULL;
        yyjson_val *behaviors = yyjson_is_obj(entity_value)
                                  ? yyjson_obj_get(entity_value, "behaviors")
                                  : NULL;
        yyjson_val *components = yyjson_is_obj(entity_value)
                                   ? yyjson_obj_get(entity_value, "components")
                                   : NULL;
        if (!yyjson_is_str(entity_name) || !yyjson_get_len(entity_name) ||
            !yyjson_is_str(prototype) || !yyjson_get_len(prototype) ||
            (behaviors && !yyjson_is_arr(behaviors)) ||
            (components && !yyjson_is_obj(components))) {
            set_error(error, error_size, "invalid scene entity at index %d", index);
            load_fail(document, parsed, NULL);
            return NULL;
        }
        char **behavior_names = NULL;
        if (read_name_array(behaviors, &behavior_names, error, error_size, index, "behaviors") !=
            0) {
            load_fail(document, parsed, behavior_names);
            return NULL;
        }
        if (game_scene_document_add_entity(
                document,
                yyjson_get_str(entity_name),
                yyjson_get_str(prototype),
                (const char *const *)behavior_names,
                acount(behavior_names)
            ) != 0) {
            set_error(error, error_size, "duplicate or invalid scene entity at index %d", index);
            load_fail(document, parsed, behavior_names);
            return NULL;
        }
        int entity_index = acount(document->entities) - 1;
        yyjson_val *retained = yyjson_obj_get(entity_value, "retained_components");
        if (retained) {
            char *retained_json = yyjson_val_write(retained, 0, NULL);
            int result = game_scene_document_set_entity_retained(document, entity_index, retained_json);
            free(retained_json);
            if (result != 0) {
                set_error(error, error_size, "invalid retained components at scene entity %d", index);
                load_fail(document, parsed, behavior_names);
                return NULL;
            }
        }
        if (components) {
            char *components_json = yyjson_val_write(components, YYJSON_WRITE_PRETTY_TWO_SPACES, NULL);
            int component_result = game_scene_document_set_entity_components(document, entity_index, components_json);
            free(components_json);
            if (component_result != 0) {
                set_error(error, error_size, "invalid components at scene entity %d", index);
                load_fail(document, parsed, behavior_names);
                return NULL;
            }
        }
        free_names(&behavior_names);
        index++;
    }
    yyjson_doc_free(parsed);
    return document;
}
