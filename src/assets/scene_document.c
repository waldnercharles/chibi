#include "assets/scene_document.h"

#include "assets/physics_settings.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/asset_value.h"
#include "support/xalloc.h"
#include "types/catalog.h"

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

static void free_entity(scene_entity *entity)
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

scene_document *scene_document_new(const char *name)
{
    if (!name || !name[0]) {
        return NULL;
    }
    scene_document *document = (scene_document *)xcalloc(1, sizeof *document);
    document->name = xstrdup(name);
    document->physics = physics_defaults();
    return document;
}

void scene_document_free(scene_document *document)
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

void scene_document_set_ui(scene_document *document, const char *ui)
{
    free(document->ui);
    document->ui = ui && ui[0] ? xstrdup(ui) : NULL;
}

static int child_named(const scene_document *document, int parent, const char *name, size_t length)
{
    for (int i = parent + 1; i < acount(document->entities); i++) {
        const scene_entity *entity = &document->entities[i];
        if (entity->parent == parent && !strncmp(entity->name, name, length) &&
            !entity->name[length]) {
            return i;
        }
    }
    return -1;
}

int scene_document_entity_index(const scene_document *document, const char *path)
{
    if (!document || !path || !path[0]) {
        return -1;
    }
    int found = -1;
    for (const char *segment = path;; segment = strchr(segment, '.') + 1) {
        const char *dot = strchr(segment, '.');
        found = child_named(document, found, segment, dot ? (size_t)(dot - segment) : strlen(segment));
        if (found < 0 || !dot) {
            return found;
        }
    }
}

char *scene_document_entity_path(const scene_document *document, int index)
{
    assert(index >= 0 && index < acount(document->entities));
    const scene_entity *entity = &document->entities[index];
    if (entity->parent < 0) {
        return xstrdup(entity->name);
    }
    char *parent = scene_document_entity_path(document, entity->parent);
    size_t size = strlen(parent) + strlen(entity->name) + 2;
    char *path = xmalloc(size);
    snprintf(path, size, "%s.%s", parent, entity->name);
    free(parent);
    return path;
}

int scene_entity_validate_name(const char *name, bool name_used, char *error, size_t error_size)
{
    if (!name || !name[0]) {
        set_error(error, error_size, "An object needs a name.");
        return -1;
    }
    if (strchr(name, '.')) {
        set_error(error, error_size, "Object names cannot contain '.', which separates paths.");
        return -1;
    }
    if (name_used) {
        set_error(error, error_size, "Another object is named %s.", name);
        return -1;
    }
    return 0;
}

int scene_document_rename_entity(scene_document *document, int index, const char *name, char *error, size_t error_size)
{
    assert(index >= 0 && index < acount(document->entities));
    int existing = name
                     ? child_named(document, document->entities[index].parent, name, strlen(name))
                     : -1;
    if (scene_entity_validate_name(name, existing >= 0 && existing != index, error, error_size)) {
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

/* How many levels deep a child of parent would nest. */
static int child_depth(const scene_document *document, int parent)
{
    int depth = 1;
    for (int at = parent; at >= 0; at = document->entities[at].parent) {
        depth++;
    }
    return depth;
}

int scene_document_add_entity(scene_document *document, int parent, const char *name, const char *prototype)
{
    if (parent < -1 || parent >= acount(document->entities) ||
        child_depth(document, parent) > SCENE_DEPTH_MAX ||
        scene_entity_validate_name(name, false, NULL, 0) ||
        child_named(document, parent, name, strlen(name)) >= 0 || !prototype ||
        !prototype[0]) {
        return -1;
    }
    scene_entity entity = {
        .name = xstrdup(name),
        .parent = parent,
        .prototype = xstrdup(prototype),
    };
    apush(document->entities, entity);
    return 0;
}

int scene_document_remove_entity(scene_document *document, int index)
{
    if (!document || index < 0 || index >= acount(document->entities)) {
        return -1;
    }
    /* Parents precede children, so one pass knows whether a parent went. */
    int count = acount(document->entities), kept = 0;
    int *moved = xmalloc((size_t)count * sizeof *moved);
    for (int i = 0; i < count; i++) {
        scene_entity entity = document->entities[i];
        if (i == index || (entity.parent >= 0 && moved[entity.parent] < 0)) {
            free_entity(&entity);
            moved[i] = -1;
            continue;
        }
        entity.parent = entity.parent >= 0 ? moved[entity.parent] : -1;
        moved[i] = kept;
        document->entities[kept++] = entity;
    }
    asetlen(document->entities, kept);
    free(moved);
    return 0;
}

int scene_document_set_entity_prototype(scene_document *document, int index, const char *prototype)
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

int scene_document_set_entity_behaviors(
    scene_document *document,
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
    document->entities[index].own_behaviors = true;
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

int scene_document_set_entity_components(scene_document *document, int index, av_value *value)
{
    assert(index >= 0 && index < acount(document->entities));
    return set_object_value(&document->entities[index].components, value);
}

int scene_document_set_entity_retained(scene_document *document, int index, av_value *value)
{
    assert(index >= 0 && index < acount(document->entities));
    return set_object_value(&document->entities[index].retained, value);
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
            format_fields(doc, value, type_catalog_find(field->type_name));
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
                              ? type_catalog_enum_label(field->type_name, enum_value)
                              : NULL;
        if (label) {
            av_obj_set(doc, object, field->name, av_str(doc, label));
        }
    }
}

/* Format only known reflected fields; unknown overrides remain available for
 * validation. The runtime loads float fields at their declared precision. */
static av_value *components_value(av_doc *doc, av_value *components, ecs_component_catalog catalog)
{
    av_value *copy = av_clone(doc, components);
    size_t i, count;
    av_value *key, *value;
    av_obj_foreach(copy, i, count, key, value)
    {
        if (av_is_obj(value)) {
            const ecs_component_desc
                *component = ecs_component_catalog_find(catalog, av_get_str(key));
            format_fields(doc, value, component ? component->type : NULL);
        }
    }
    return copy;
}

/* A document being written, with each entity's first child and next sibling
 * in document order, or -1. */
typedef struct
{
    av_doc *json;
    const scene_document *document;
    ecs_component_catalog components;
    int *first_child, *next_sibling;
} scene_writer;

static av_value *entity_value(const scene_writer *writer, int index)
{
    av_doc *json = writer->json;
    const scene_entity *entity = &writer->document->entities[index];
    av_value *value = av_obj(json);
    av_obj_add_strcpy(json, value, "name", entity->name);
    av_obj_add_strcpy(json, value, "prototype", entity->prototype);
    if (av_obj_size(entity->components)) {
        av_obj_add_val(
            json,
            value,
            "components",
            components_value(json, entity->components, writer->components)
        );
    }
    if (av_obj_size(entity->retained)) {
        av_obj_add_val(
            json,
            value,
            "retained_components",
            components_value(json, entity->retained, writer->components)
        );
    }
    if (entity->own_behaviors) {
        av_value *behaviors = av_obj_add_arr(json, value, "behaviors");
        for (int j = 0; j < acount(entity->behaviors); j++) {
            av_arr_add_strcpy(json, behaviors, entity->behaviors[j]);
        }
    }
    int child = writer->first_child[index];
    av_value *children = child < 0 ? NULL : av_obj_add_arr(json, value, "children");
    for (; child >= 0; child = writer->next_sibling[child]) {
        av_arr_add_val(children, entity_value(writer, child));
    }
    return value;
}

av_value *scene_document_entities_value(av_doc *doc, const scene_document *document, ecs_component_catalog components)
{
    int count = acount(document->entities), first_root = -1;
    scene_writer writer = { .json = doc,
                            .document = document,
                            .components = components,
                            .first_child = xmalloc((size_t)count * sizeof(int)),
                            .next_sibling = xmalloc((size_t)count * sizeof(int)) };
    for (int i = 0; i < count; i++) {
        writer.first_child[i] = -1;
    }
    /* Link backwards so each list keeps document order. */
    for (int i = count - 1; i >= 0; i--) {
        int parent = document->entities[i].parent;
        int *first = parent < 0 ? &first_root : &writer.first_child[parent];
        writer.next_sibling[i] = *first;
        *first = i;
    }
    av_value *entities = av_arr(doc);
    for (int i = first_root; i >= 0; i = writer.next_sibling[i]) {
        av_arr_add_val(entities, entity_value(&writer, i));
    }
    free(writer.first_child);
    free(writer.next_sibling);
    return entities;
}

av_value *scene_document_to_value(const scene_document *document, ecs_component_catalog components)
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
    av_value *physics = physics_settings_write(json, document->physics);
    if (av_obj_size(physics)) {
        av_obj_add_val(json, root, "physics", physics);
    }
    av_obj_add_val(json, root, "entities", scene_document_entities_value(json, document, components));
    av_value *result = av_value_copy(av_doc_root(json));
    av_doc_free(json);
    return result;
}

char *scene_document_save(const scene_document *document, ecs_component_catalog components)
{
    av_value *value = scene_document_to_value(document, components);
    char *text = asset_text_write(value);
    av_value_free(value);
    return text;
}

static void load_fail(scene_document *document, av_doc *parsed)
{
    scene_document_free(document);
    av_doc_free(parsed);
}

/* Reads entities and their children depth first; index counts them in that
 * order for error messages. */
static bool read_entities(
    scene_document *document,
    av_value *entities,
    int parent,
    int *index,
    char *error,
    size_t error_size
)
{
    av_arr_iter iterator = av_arr_iter_with(entities);
    av_value *value;
    while ((value = av_arr_iter_next(&iterator))) {
        av_value *name = av_is_obj(value) ? av_obj_get(value, "name") : NULL;
        av_value *prototype = av_is_obj(value) ? av_obj_get(value, "prototype") : NULL;
        av_value *behaviors = av_is_obj(value) ? av_obj_get(value, "behaviors") : NULL;
        av_value *components = av_is_obj(value) ? av_obj_get(value, "components")
                                                : NULL;
        av_value *children = av_is_obj(value) ? av_obj_get(value, "children") : NULL;
        if (!av_is_str(name) || !av_get_len(name) || !av_is_str(prototype) ||
            !av_get_len(prototype) || (behaviors && !av_is_arr(behaviors)) ||
            (components && !av_is_obj(components)) ||
            (children && !av_is_arr(children))) {
            set_error(error, error_size, "invalid scene entity at index %d", *index);
            return false;
        }
        if (child_depth(document, parent) > SCENE_DEPTH_MAX) {
            set_error(
                error,
                error_size,
                "scene entity at index %d nests deeper than %d levels",
                *index,
                SCENE_DEPTH_MAX
            );
            return false;
        }
        if (scene_document_add_entity(document, parent, av_get_str(name), av_get_str(prototype)) !=
            0) {
            set_error(error, error_size, "duplicate or invalid scene entity at index %d", *index);
            return false;
        }
        int entity_index = acount(document->entities) - 1;
        scene_entity *entity = &document->entities[entity_index];
        entity->own_behaviors = behaviors != NULL;
        if (read_name_array(behaviors, &entity->behaviors, error, error_size, *index, "behaviors") !=
            0) {
            return false;
        }
        av_value *retained = av_obj_get(value, "retained_components");
        if (retained &&
            scene_document_set_entity_retained(document, entity_index, retained)) {
            set_error(error, error_size, "invalid retained components at scene entity %d", *index);
            return false;
        }
        if (components &&
            scene_document_set_entity_components(document, entity_index, components)) {
            set_error(error, error_size, "invalid components at scene entity %d", *index);
            return false;
        }
        (*index)++;
        if (children &&
            !read_entities(document, children, entity_index, index, error, error_size)) {
            return false;
        }
    }
    return true;
}

int scene_document_read_entities(scene_document *document, av_value *entities, char *error, size_t error_size)
{
    int index = 0;
    if (!av_is_arr(entities)) {
        set_error(error, error_size, "scene entities must be an array");
        return -1;
    }
    return read_entities(document, entities, -1, &index, error, error_size) ? 0 : -1;
}

int scene_document_take_entities(scene_document *document, scene_document *from, char *error, size_t error_size)
{
    for (int i = 0; i < acount(from->entities); i++) {
        const scene_entity *entity = &from->entities[i];
        if (entity->parent < 0 &&
            child_named(document, -1, entity->name, strlen(entity->name)) >= 0) {
            set_error(
                error,
                error_size,
                "two scene entities are named %s",
                entity->name
            );
            return -1;
        }
    }
    int offset = acount(document->entities);
    for (int i = 0; i < acount(from->entities); i++) {
        scene_entity entity = from->entities[i];
        entity.parent = entity.parent < 0 ? -1 : entity.parent + offset;
        apush(document->entities, entity);
    }
    aclear(from->entities);
    return 0;
}

static scene_document *load_document_value(av_doc *parsed, char *error, size_t error_size)
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
    scene_document *document = scene_document_new(av_get_str(name));
    if (!physics_settings_read(av_obj_get(root, "physics"), &document->physics)) {
        set_error(error, error_size, "invalid scene physics settings");
        scene_document_free(document);
        av_doc_free(parsed);
        return NULL;
    }
    av_value *presentation = av_obj_get(root, "presentation");
    if (presentation &&
        (!av_is_str(presentation) || (strcmp(av_get_str(presentation), "2d") &&
                                      strcmp(av_get_str(presentation), "3d")))) {
        set_error(error, error_size, "scene presentation must be 2d or 3d");
        scene_document_free(document);
        av_doc_free(parsed);
        return NULL;
    }
    document->perspective = presentation && !strcmp(av_get_str(presentation), "3d");
    av_value *ui = av_obj_get(root, "ui");
    if (ui && (!av_is_str(ui) || !av_get_len(ui))) {
        set_error(error, error_size, "scene ui must name a ui asset");
        scene_document_free(document);
        av_doc_free(parsed);
        return NULL;
    }
    scene_document_set_ui(document, av_get_str(ui));
    if (scene_document_read_entities(document, entities, error, error_size)) {
        load_fail(document, parsed);
        return NULL;
    }
    av_doc_free(parsed);
    return document;
}

scene_document *scene_document_load_value(av_value *value, char *error, size_t error_size)
{
    if (!value) {
        set_error(error, error_size, "scene document is missing");
        return NULL;
    }
    return load_document_value(av_doc_clone_value(value), error, error_size);
}

scene_document *scene_document_load(const char *text, char *error, size_t error_size)
{
    av_doc *parsed = text ? asset_text_read(text, error, error_size) : NULL;
    return parsed ? load_document_value(parsed, error, error_size) : NULL;
}
