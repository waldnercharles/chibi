#include "assets/scene_document.h"

#include "assets/physics_settings.h"
#include "graph/model/registry.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/asset_value.h"
#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
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
    av_value_free(entity->components);
    av_value_free(entity->retained);
    free_names(&entity->behaviors);
}

static int read_name_array(
    av_value *array,
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
    if (!av_is_arr(array)) {
        set_error(error, error_size, "invalid %s at scene entity %d", kind, entity);
        return -1;
    }
    av_arr_iter iterator = av_arr_iter_with(array);
    av_value *value;
    while ((value = av_arr_iter_next(&iterator))) {
        if (!av_is_str(value) || !av_get_len(value) ||
            name_used((const char *const *)*out, acount(*out), av_get_str(value))) {
            set_error(error, error_size, "invalid %s at scene entity %d", kind, entity);
            free_names(out);
            return -1;
        }
        apush(*out, xstrdup(av_get_str(value)));
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
    document->physics = game_physics_defaults();
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
    free(document->ui);
    free(document);
}

void game_scene_document_set_ui(game_scene_document *document, const char *ui)
{
    free(document->ui);
    document->ui = ui && ui[0] ? xstrdup(ui) : NULL;
}

int game_scene_document_set_perspective(game_scene_document *document, bool perspective)
{
    if (!document) {
        return -1;
    }
    document->perspective = perspective;
    return 0;
}

int game_scene_document_entity_index(const game_scene_document *document, const char *name)
{
    if (!document || !name || !name[0]) {
        return -1;
    }
    for (int i = 0; i < acount(document->entities); i++) {
        if (!strcmp(document->entities[i].name, name)) {
            return i;
        }
    }
    return -1;
}

int game_scene_entity_validate_name(const char *name, bool name_used, char *error, size_t error_size)
{
    if (!name || !name[0]) {
        set_error(error, error_size, "An object needs a name.");
        return -1;
    }
    if (name_used) {
        set_error(error, error_size, "Another object is named %s.", name);
        return -1;
    }
    return 0;
}

int game_scene_document_rename_entity(
    game_scene_document *document,
    int index,
    const char *name,
    char *error,
    size_t error_size
)
{
    assert(index >= 0 && index < acount(document->entities));
    int existing = game_scene_document_entity_index(document, name);
    if (game_scene_entity_validate_name(name, existing >= 0 && existing != index, error, error_size)) {
        return -1;
    }
    if (existing == index) {
        return 0;
    }
    char *copy = xstrdup(name);
    free(document->entities[index].name);
    document->entities[index].name = copy;
    return 0;
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
        .components = NULL,
    };
    if (copy_names(&entity.behaviors, behaviors, behavior_count) != 0) {
        free_entity(&entity);
        return -1;
    }
    apush(document->entities, entity);
    return 0;
}

int game_scene_document_remove_entity(game_scene_document *document, int index)
{
    if (!document || index < 0 || index >= acount(document->entities)) {
        return -1;
    }
    free_entity(&document->entities[index]);
    int remaining = acount(document->entities) - index - 1;
    if (remaining > 0) {
        memmove(
            &document->entities[index],
            &document->entities[index + 1],
            (size_t)remaining * sizeof *document->entities
        );
    }
    asetlen(document->entities, acount(document->entities) - 1);
    return 0;
}

int game_scene_document_set_entity_prototype(game_scene_document *document, int index, const char *prototype)
{
    if (!document || index < 0 || index >= acount(document->entities) ||
        !prototype || !prototype[0]) {
        return -1;
    }
    char *copy = xstrdup(prototype);
    free(document->entities[index].prototype);
    document->entities[index].prototype = copy;
    return 0;
}

int game_scene_document_set_entity_behaviors(
    game_scene_document *document,
    int index,
    const char *const *behaviors,
    int behavior_count
)
{
    if (!document || index < 0 || index >= acount(document->entities) ||
        behavior_count < 0 || (behavior_count && !behaviors)) {
        return -1;
    }
    char **copy = NULL;
    if (copy_names(&copy, behaviors, behavior_count) != 0) {
        free_names(&copy);
        return -1;
    }
    free_names(&document->entities[index].behaviors);
    document->entities[index].behaviors = copy;
    return 0;
}

static int set_object_value(av_value **target, av_value *value)
{
    if (value && !av_is_obj(value)) {
        return -1;
    }
    av_value *copy = value ? av_value_copy(value) : NULL;
    av_value_free(*target);
    *target = copy;
    return 0;
}

int game_scene_document_set_entity_components(game_scene_document *document, int index, av_value *value)
{
    assert(index >= 0 && index < acount(document->entities));
    return set_object_value(&document->entities[index].components, value);
}

int game_scene_document_set_entity_retained(game_scene_document *document, int index, av_value *value)
{
    assert(index >= 0 && index < acount(document->entities));
    return set_object_value(&document->entities[index].retained, value);
}

static const type_desc *component_type(const char *key)
{
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        const ecs_component_desc *component = GAME_REGISTRY_COMPONENTS[i];
        if ((component->key && !strcmp(component->key, key)) ||
            !strcmp(component->name, key)) {
            return component->type;
        }
    }
    return NULL;
}

static bool enum_integer(av_value *value, int *out)
{
    if (av_is_sint(value)) {
        int64_t number = av_get_sint(value);
        if (number < INT32_MIN || number > INT32_MAX) {
            return false;
        }
        *out = (int)number;
        return true;
    }
    if (av_is_uint(value)) {
        uint64_t number = av_get_uint(value);
        if (number > INT32_MAX) {
            return false;
        }
        *out = (int)number;
        return true;
    }
    return false;
}

static void format_fields(av_doc *doc, av_value *object, const type_desc *type)
{
    for (int i = 0; type && i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        av_value *value = av_obj_get(object, field->name);
        if (field->type == PIN_STRUCT && av_is_obj(value)) {
            format_fields(doc, value, registry_type_by_name(field->type_name));
            continue;
        }
        if (field->type == PIN_F32 && av_is_num(value)) {
            av_obj_set(doc, object, field->name, av_float(doc, (float)av_get_num(value)));
            continue;
        }
        if (field->color && av_is_uint(value) && av_get_uint(value) <= UINT32_MAX) {
            av_obj_set(doc, object, field->name, av_color(doc, (uint32_t)av_get_uint(value)));
            continue;
        }
        int enum_value;
        const char *label = field->type == PIN_ENUM && enum_integer(value, &enum_value)
                              ? registry_enum_label(field->type_name, enum_value)
                              : NULL;
        if (label) {
            av_obj_set(doc, object, field->name, av_str(doc, label));
        }
    }
}

/* Format only known reflected fields; unknown overrides remain available for
 * validation. The runtime loads float fields at their declared precision. */
static av_value *components_value(av_doc *doc, av_value *components)
{
    av_value *copy = av_clone(doc, components);
    size_t i, count;
    av_value *key, *value;
    av_obj_foreach(copy, i, count, key, value)
    {
        if (av_is_obj(value)) {
            format_fields(doc, value, component_type(av_get_str(key)));
        }
    }
    return copy;
}

av_value *game_scene_document_to_value(const game_scene_document *document)
{
    av_doc *json = av_doc_new();
    av_value *root = av_obj(json);
    av_doc_set_root(json, root);
    av_obj_add_uint(json, root, "version", 1);
    av_obj_add_strcpy(json, root, "name", document->name);
    if (document->ui) {
        av_obj_add_strcpy(json, root, "ui", document->ui);
    }
    if (document->perspective) {
        av_obj_add_str(json, root, "presentation", "3d");
    }
    av_value *physics = game_physics_settings_write(json, document->physics);
    if (av_obj_size(physics)) {
        av_obj_add_val(json, root, "physics", physics);
    }
    av_value *entities = av_arr(json);
    for (int i = 0; i < acount(document->entities); i++) {
        const game_scene_entity *entity = &document->entities[i];
        av_value *value = av_obj(json);
        av_obj_add_strcpy(json, value, "name", entity->name);
        av_obj_add_strcpy(json, value, "prototype", entity->prototype);
        if (av_obj_size(entity->components)) {
            av_obj_add_val(
                json,
                value,
                "components",
                components_value(json, entity->components)
            );
        }
        if (av_obj_size(entity->retained)) {
            av_obj_add_val(
                json,
                value,
                "retained_components",
                components_value(json, entity->retained)
            );
        }
        if (acount(entity->behaviors)) {
            av_value *behaviors = av_obj_add_arr(json, value, "behaviors");
            for (int j = 0; j < acount(entity->behaviors); j++) {
                av_arr_add_strcpy(json, behaviors, entity->behaviors[j]);
            }
        }
        av_arr_add_val(entities, value);
    }
    av_obj_add_val(json, root, "entities", entities);
    av_value *result = av_value_copy(av_doc_root(json));
    av_doc_free(json);
    return result;
}

char *game_scene_document_save(const game_scene_document *document)
{
    av_value *value = game_scene_document_to_value(document);
    char *text = asset_text_write(value);
    av_value_free(value);
    return text;
}

static void load_fail(game_scene_document *document, av_doc *parsed, char **behaviors)
{
    free_names(&behaviors);
    game_scene_document_free(document);
    av_doc_free(parsed);
}

static game_scene_document *load_document_value(av_doc *parsed, char *error, size_t error_size)
{
    av_value *root = av_doc_root(parsed);
    av_value *version = av_is_obj(root) ? av_obj_get(root, "version") : NULL;
    av_value *name = av_is_obj(root) ? av_obj_get(root, "name") : NULL;
    av_value *entities = av_is_obj(root) ? av_obj_get(root, "entities") : NULL;
    if (!av_is_uint(version) || av_get_uint(version) != 1 || !av_is_str(name) ||
        !av_get_len(name) || !av_is_arr(entities)) {
        set_error(error, error_size, "scene document requires version 1, name, and entities");
        av_doc_free(parsed);
        return NULL;
    }
    game_scene_document *document = game_scene_document_new(av_get_str(name));
    if (!game_physics_settings_read(av_obj_get(root, "physics"), &document->physics)) {
        set_error(error, error_size, "invalid scene physics settings");
        game_scene_document_free(document);
        av_doc_free(parsed);
        return NULL;
    }
    av_value *presentation = av_obj_get(root, "presentation");
    if (presentation &&
        (!av_is_str(presentation) || (strcmp(av_get_str(presentation), "2d") &&
                                      strcmp(av_get_str(presentation), "3d")))) {
        set_error(error, error_size, "scene presentation must be 2d or 3d");
        game_scene_document_free(document);
        av_doc_free(parsed);
        return NULL;
    }
    document->perspective = presentation && !strcmp(av_get_str(presentation), "3d");
    av_value *ui = av_obj_get(root, "ui");
    if (ui && (!av_is_str(ui) || !av_get_len(ui))) {
        set_error(error, error_size, "scene ui must name a ui asset");
        game_scene_document_free(document);
        av_doc_free(parsed);
        return NULL;
    }
    game_scene_document_set_ui(document, av_get_str(ui));
    av_arr_iter iterator = av_arr_iter_with(entities);
    av_value *entity_value;
    int index = 0;
    while ((entity_value = av_arr_iter_next(&iterator))) {
        av_value *entity_name = av_is_obj(entity_value)
                                  ? av_obj_get(entity_value, "name")
                                  : NULL;
        av_value *prototype = av_is_obj(entity_value)
                                ? av_obj_get(entity_value, "prototype")
                                : NULL;
        av_value *behaviors = av_is_obj(entity_value)
                                ? av_obj_get(entity_value, "behaviors")
                                : NULL;
        av_value *components = av_is_obj(entity_value)
                                 ? av_obj_get(entity_value, "components")
                                 : NULL;
        if (!av_is_str(entity_name) || !av_get_len(entity_name) ||
            !av_is_str(prototype) || !av_get_len(prototype) ||
            (behaviors && !av_is_arr(behaviors)) ||
            (components && !av_is_obj(components))) {
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
                av_get_str(entity_name),
                av_get_str(prototype),
                (const char *const *)behavior_names,
                acount(behavior_names)
            ) != 0) {
            set_error(error, error_size, "duplicate or invalid scene entity at index %d", index);
            load_fail(document, parsed, behavior_names);
            return NULL;
        }
        int entity_index = acount(document->entities) - 1;
        av_value *retained = av_obj_get(entity_value, "retained_components");
        if (retained) {
            int result = game_scene_document_set_entity_retained(document, entity_index, retained);
            if (result != 0) {
                set_error(error, error_size, "invalid retained components at scene entity %d", index);
                load_fail(document, parsed, behavior_names);
                return NULL;
            }
        }
        if (components) {
            int component_result = game_scene_document_set_entity_components(document, entity_index, components);
            if (component_result != 0) {
                set_error(error, error_size, "invalid components at scene entity %d", index);
                load_fail(document, parsed, behavior_names);
                return NULL;
            }
        }
        free_names(&behavior_names);
        index++;
    }
    av_doc_free(parsed);
    return document;
}

game_scene_document *game_scene_document_load_value(av_value *value, char *error, size_t error_size)
{
    if (!value) {
        set_error(error, error_size, "scene document is missing");
        return NULL;
    }
    return load_document_value(av_doc_clone_value(value), error, error_size);
}

game_scene_document *game_scene_document_load(const char *text, char *error, size_t error_size)
{
    av_doc *parsed = text ? asset_text_read(text, error, error_size) : NULL;
    return parsed ? load_document_value(parsed, error, error_size) : NULL;
}
