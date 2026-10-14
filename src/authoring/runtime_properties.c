#include "authoring/runtime_properties.h"

#include "runtime/native_definitions.h"
#include "runtime/type_definitions.h"
#include "runtime/value_json.h"
#include "support/asset_json.h"
#include "support/utf8.h"
#include "types/catalog.h"
#include "types/value_document.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FIELD(T, F, K, N)                                                      \
    { .name = #F,                                                              \
      .type = K,                                                               \
      .type_name = N,                                                          \
      .offset = offsetof(T, F),                                                \
      .size = sizeof(((T *)0)->F) }
static const type_field FIELDS[] = {
    FIELD(runtime_property, component, PIN_STR, "const char *"),
    FIELD(runtime_property, field, PIN_STR, "const char *"),
    FIELD(runtime_property, label, PIN_STR, "const char *"),
    FIELD(runtime_property, kind, PIN_STR, "const char *"),
    FIELD(runtime_property, json, PIN_STR, "const char *"),
    FIELD(runtime_property, text, PIN_STR, "const char *"),
    FIELD(runtime_property, target, PIN_STR, "const char *"),
    FIELD(runtime_property, unit, PIN_STR, "const char *"),
    FIELD(runtime_property, presentation_category, PIN_STR, "const char *"),
    FIELD(runtime_property, icon, PIN_STR, "const char *"),
    FIELD(runtime_property, number, PIN_F64, "double"),
    FIELD(runtime_property, min, PIN_F64, "double"),
    FIELD(runtime_property, max, PIN_F64, "double"),
    FIELD(runtime_property, boolean, PIN_BOOL, "bool"),
    FIELD(runtime_property, writable, PIN_BOOL, "bool"),
    FIELD(runtime_property, bounded, PIN_BOOL, "bool"),
    FIELD(runtime_property, color, PIN_BOOL, "bool"),
    FIELD(runtime_property, computed, PIN_BOOL, "bool"),
};
static const type_field CHOICES[] = {
    FIELD(runtime_property_choice, component, PIN_STR, "const char *"),
    FIELD(runtime_property_choice, field, PIN_STR, "const char *"),
    FIELD(runtime_property_choice, label, PIN_STR, "const char *"),
    FIELD(runtime_property_choice, json, PIN_STR, "const char *"),
};
const type_desc RUNTIME_PROPERTY_LAYOUT = { .name = "RuntimeProperty",
                                            .size = sizeof(runtime_property),
                                            .fields = FIELDS,
                                            .field_count = sizeof FIELDS /
                                                           sizeof *FIELDS };
const type_desc RUNTIME_PROPERTY_CHOICE_LAYOUT = {
    .name = "RuntimePropertyChoice",
    .size = sizeof(runtime_property_choice),
    .fields = CHOICES,
    .field_count = sizeof CHOICES / sizeof *CHOICES
};
static const type_desc *const PROPERTY_TYPES[] = { &RUNTIME_PROPERTY_LAYOUT,
                                                   &RUNTIME_PROPERTY_CHOICE_LAYOUT };
const type_catalog_pack RUNTIME_PROPERTY_TYPE_PACK = { .types = PROPERTY_TYPES,
                                                       .type_count = 2 };
const native_type_definition RUNTIME_PROPERTY_TYPE_DEFINITIONS[2] = {
    { .layout = &RUNTIME_PROPERTY_LAYOUT, .storage = PIN_STRUCT },
    { .layout = &RUNTIME_PROPERTY_CHOICE_LAYOUT, .storage = PIN_STRUCT },
};
static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
void runtime_subject_token(ecs_entity entity, char out[64])
{
    out[0] = 0;
    if (entity.slot) {
        snprintf(out, 64, "%" PRIu32 ":%" PRIu32, entity.slot, entity.generation);
    }
}
bool runtime_subject_resolve(const ecs_world *world, const char *text, ecs_entity *out)
{
    *out = (ecs_entity){ 0 };
    if (!world || !text || !*text || strlen(text) > 32 || *text < '0' || *text > '9') {
        return false;
    }
    char *end;
    errno = 0;
    uintmax_t slot = strtoumax(text, &end, 10);
    if (errno || !slot || slot > UINT32_MAX || *end != ':' || end[1] < '0' ||
        end[1] > '9') {
        return false;
    }
    const char *generation = end + 1;
    errno = 0;
    uintmax_t gen = strtoumax(generation, &end, 10);
    if (errno || gen > UINT32_MAX || *end) {
        return false;
    }
    ecs_entity entity = { (uint32_t)slot, (uint32_t)gen };
    if (!ecs_entity_is_alive(world, entity) || ecs_entity_is_retiring(world, entity)) {
        return false;
    }
    *out = entity;
    return true;
}
static char *copy(arena *storage, const char *text)
{
    if (!text) {
        text = "";
    }
    size_t bytes = strlen(text) + 1;
    char *out = arena_alloc(storage, bytes);
    memcpy(out, text, bytes);
    return out;
}
static const char *kind(pin_type type)
{
    switch (type) {
        case PIN_BOOL: return "bool";
        case PIN_I8:
        case PIN_U8:
        case PIN_I16:
        case PIN_U16:
        case PIN_I32:
        case PIN_U32:
        case PIN_I64:
        case PIN_U64: return "integer";
        case PIN_F32:
        case PIN_F64: return "number";
        case PIN_STR:
        case PIN_NAME: return "string";
        case PIN_ENUM: return "enum";
        case PIN_HANDLE: return "handle";
        default: return "unsupported";
    }
}
static bool editable(const ecs_component_desc *component, const type_field *field)
{
    return component->id != ECS_NAME && !component->derived_from &&
           strcmp(kind(field->type), "unsupported") &&
           (field->type != PIN_HANDLE ||
            (field->type_name && !strcmp(field->type_name, "ecs.entity")));
}
static const ecs_component_desc *component_of(const ecs_world *world, const char *name)
{
    if (!name || strlen(name) > 256) {
        return NULL;
    }
    for (int i = 0; i < ecs_world_component_count(world); i++) {
        const ecs_component_desc *row = ecs_world_component_at(world, i);
        if ((row->key && !strcmp(row->key, name)) ||
            (row->name && !strcmp(row->name, name))) {
            return row;
        }
    }
    return NULL;
}
typedef struct
{
    const ecs_world *world;
    ecs_entity entity;
    arena *storage;
    pin_array *rows, *choices;
    int first, count, total, visited;
    size_t bytes;
    char *error;
    size_t error_size;
} query;
static bool leaf(
    query *q,
    const ecs_component_desc *component,
    const void *data,
    const type_field *field,
    const char *path
)
{
    if (q->total >= 4096) {
        return fail(q->error, q->error_size, "Runtime properties exceed 4096 reflected leaves");
    }
    int index = q->total++;
    if (index < q->first || q->rows->count >= q->count) {
        return true;
    }
    runtime_property *row = arena_alloc(q->storage, sizeof *row);
    *row = (runtime_property){
        .component = copy(
            q->storage,
            component->key ? component->key : component->name
        ),
        .field = copy(q->storage, path),
        .label = copy(q->storage, path),
        .kind = kind(field->type),
        .presentation_category = component_presentation_category(
            component_presentation_tab(&(component_presentation){ 0 }, component)
        ),
        .icon = component_presentation_icon_name(
            component_presentation_icon(&(component_presentation){ 0 }, component)
        ),
        .text = "",
        .json = "null",
        .target = "",
        .unit = copy(q->storage, field->unit),
        .writable = editable(component, field),
        .computed = component->derived_from != 0,
        .bounded = isfinite(field->min) && isfinite(field->max) &&
                   field->min < field->max,
        .min = isfinite(field->min) ? field->min : 0,
        .max = isfinite(field->max) ? field->max : 0,
        .color = field->color && field->type == PIN_U32
    };
    pin_value value;
    pin_read_field(data, field, &value);
    char *json = NULL, message[256] = "";
    if (!value_to_json(&value, &json, message, sizeof message) || !json ||
        strlen(json) > 65536 || strlen(json) > 1048576 - q->bytes) {
        free(json);
        row->writable = false;
        row->kind = "unsupported";
        row->text = copy(q->storage, *message ? message : "Value exceeds runtime property byte limit");
    } else {
        q->bytes += strlen(json);
        row->json = copy(q->storage, json);
        row->text = copy(
            q->storage,
            value.type == PIN_STR || value.type == PIN_NAME ? value.str : json
        );
        if (value.type == PIN_BOOL) {
            row->boolean = value.b;
        }
        if (value.type == PIN_F32) {
            row->number = value.f;
        }
        if (value.type == PIN_F64) {
            row->number = value.d;
        }
        if (value.type == PIN_U32) {
            row->number = value.u;
        }
        if (value.type == PIN_HANDLE && field->type_name &&
            !strcmp(field->type_name, "ecs.entity")) {
            ecs_entity target = ecs_entity_from_u64(value.u64);
            if (ecs_entity_is_alive(q->world, target)) {
                char token[64];
                runtime_subject_token(target, token);
                row->target = copy(q->storage, token);
            }
        }
        free(json);
    }
    if (field->type == PIN_ENUM) {
        const enum_desc *choices = type_catalog_enum_find(field->type_name);
        if (!choices || choices->value_count <= 0 || choices->value_count > 256 ||
            choices->value_count > 1024 - q->choices->count) {
            row->writable = false;
        } else {
            for (int i = 0; i < choices->value_count; i++) {
                pin_value enum_value = { .type = PIN_ENUM,
                                         .type_name = field->type_name,
                                         .e = choices->values[i].value };
                char *json = NULL;
                const char *label = choices->values[i].label
                                      ? choices->values[i].label
                                      : choices->values[i].name;
                if (!value_to_json(&enum_value, &json, message, sizeof message) ||
                    !json || strlen(json) > 65536 || strlen(json) > 1048576 - q->bytes ||
                    (label && strlen(label) > 65536)) {
                    free(json);
                    row->writable = false;
                    break;
                }
                q->bytes += strlen(json);
                runtime_property_choice *choice = arena_alloc(q->storage, sizeof *choice);
                *choice = (runtime_property_choice){
                    row->component,
                    row->field,
                    copy(
                        q->storage,
                        choices->values[i].label ? choices->values[i].label
                                                 : choices->values[i].name
                    ),
                    copy(q->storage, json)
                };
                free(json);
                q->choices->items[q->choices->count++] = (pin_value){
                    .type = PIN_STRUCT,
                    .type_name = "RuntimePropertyChoice",
                    .ptr = choice
                };
            }
        }
    }
    q->rows->items[q->rows->count++] = (pin_value){
        .type = PIN_STRUCT,
        .type_name = "RuntimeProperty",
        .ptr = row
    };
    return true;
}
static bool fields(
    query *q,
    const ecs_component_desc *component,
    const void *data,
    const type_desc *type,
    const char *prefix,
    size_t offset,
    int depth
)
{
    if (depth > 8) {
        return fail(q->error, q->error_size, "Runtime reflection nesting exceeds 8");
    }
    for (int i = 0; i < type->field_count; i++) {
        if (++q->visited > 65536) {
            return fail(q->error, q->error_size, "Runtime reflection traversal exceeds 65536 fields");
        }
        type_field field = type->fields[i];
        char path[512];
        int length = snprintf(
            path,
            sizeof path,
            "%s%s%s",
            prefix,
            *prefix ? "." : "",
            field.name
        );
        if (length < 0 || length >= (int)sizeof path) {
            return fail(q->error, q->error_size, "Runtime field path exceeds 511 bytes");
        }
        if (field.type == PIN_STRUCT) {
            const type_desc *nested = type_catalog_find(field.type_name);
            if (nested) {
                if (!fields(q, component, data, nested, path, offset + field.offset, depth + 1)) {
                    return false;
                }
                continue;
            }
        }
        field.offset += offset;
        if (!leaf(q, component, data, &field, path)) {
            return false;
        }
    }
    return true;
}
static pin_array *array(arena *storage, const char *name, int capacity)
{
    pin_array *out = arena_alloc(storage, sizeof *out);
    *out = (pin_array){
        .element_type = PIN_STRUCT,
        .type_name = name,
        .capacity = capacity,
        .items = capacity ? arena_alloc(storage, (size_t)capacity * sizeof(pin_value)) : NULL
    };
    return out;
}
bool runtime_properties_query(
    const ecs_world *world,
    ecs_entity entity,
    int first,
    int count,
    arena *storage,
    pin_array **rows,
    pin_array **choices,
    int *total,
    char *error,
    size_t size
)
{
    *rows = array(storage, "RuntimeProperty", count >= 0 && count <= 256 ? count : 0);
    *choices = array(storage, "RuntimePropertyChoice", 1024);
    *total = 0;
    if (!world || !ecs_entity_is_alive(world, entity) ||
        ecs_entity_is_retiring(world, entity) || first < 0 || count < 0 ||
        count > 256) {
        return fail(error, size, "Runtime property query is invalid or stale");
    }
    int components = ecs_world_component_count(world);
    if (components > 1024) {
        return fail(error, size, "Runtime component inventory exceeds 1024");
    }
    query q = { .world = world,
                .entity = entity,
                .storage = storage,
                .rows = *rows,
                .choices = *choices,
                .first = first,
                .count = count,
                .error = error,
                .error_size = size };
    for (int i = 0; i < components; i++) {
        const ecs_component_desc *component = ecs_world_component_at(world, i);
        const void *data = ecs_component_data_const(world, entity, component->id);
        if (data && component->type &&
            !fields(&q, component, data, component->type, "", 0, 0)) {
            (*rows)->count = (*choices)->count = 0;
            return false;
        }
    }
    *total = q.total;
    return true;
}
static bool field_path(const type_desc *type, const char *path, type_field *out)
{
    size_t offset = 0;
    for (int depth = 0; type && depth <= 8; depth++) {
        const char *end = strchr(path, '.');
        size_t length = end ? (size_t)(end - path) : strlen(path);
        const type_field *selected = NULL;
        for (int i = 0; i < type->field_count; i++) {
            const type_field *field = &type->fields[i];
            if (strlen(field->name) == length && !strncmp(field->name, path, length)) {
                selected = field;
                break;
            }
        }
        if (!selected) {
            return false;
        }
        offset += selected->offset;
        if (!end) {
            *out = *selected;
            out->offset = offset;
            return true;
        }
        if (selected->type != PIN_STRUCT || !selected->type_name) {
            return false;
        }
        type = type_catalog_find(selected->type_name);
        path = end + 1;
    }
    return false;
}
bool runtime_properties_write(
    ecs_world *world,
    ecs_entity entity,
    const char *component,
    const char *path,
    const char *json,
    char *error,
    size_t size
)
{
    if (!world || !ecs_entity_is_alive(world, entity) ||
        ecs_entity_is_retiring(world, entity) || !path || !*path || strlen(path) > 511 ||
        !json || strlen(json) > 65536 || !utf8_text_valid(json, strlen(json))) {
        return fail(error, size, "Runtime property write is invalid or stale");
    }
    const ecs_component_desc *desc = component_of(world, component);
    type_field selected;
    const type_field *field = desc && desc->type &&
                                      field_path(desc->type, path, &selected)
                                ? &selected
                                : NULL;
    if (!field || !editable(desc, field) ||
        !ecs_component_has(world, entity, desc->id)) {
        return fail(error, size, "Runtime field is unavailable or read only");
    }
    av_doc *doc = av_json_read(json);
    const enum_desc *enumeration = field->type == PIN_ENUM
                                     ? type_catalog_enum_find(field->type_name)
                                     : NULL;
    if (field->type == PIN_ENUM && (!enumeration || enumeration->value_count <= 0 ||
                                    enumeration->value_count > 256)) {
        av_doc_free(doc);
        return fail(error, size, "Runtime enum definition is unavailable or exceeds 256 choices");
    }
    /* Decode enum names against this exact descriptor, tolerating an absent
     * presentation label without passing it to the shared label comparator. */
    if (doc && enumeration && av_is_str(av_doc_root(doc))) {
        const av_value *root = av_doc_root(doc);
        const char *text = av_get_str(root);
        bool matched = false;
        for (int i = 0; strlen(text) == av_get_len(root) && i < enumeration->value_count;
             i++) {
            const enum_value_desc *choice = &enumeration->values[i];
            if ((choice->name && !strcmp(choice->name, text)) ||
                (choice->label && !strcmp(choice->label, text))) {
                av_doc_set_root(doc, av_sint(doc, choice->value));
                matched = true;
                break;
            }
        }
        if (!matched) {
            av_doc_free(doc);
            return fail(error, size, "Runtime enum choice is unavailable");
        }
    }
    arena storage = { 0 };
    pin_value declared = { .type = field->type, .type_name = field->type_name },
              value = { 0 };
    bool ok = doc &&
              !pin_value_read_document(&storage, declared, av_doc_root(doc), &value, error, size);
    if (ok && ((value.type == PIN_F32 && !isfinite(value.f)) ||
               (value.type == PIN_F64 && !isfinite(value.d)))) {
        ok = false;
    }
    if (ok && value.type == PIN_ENUM) {
        ok = false;
        for (int i = 0; i < enumeration->value_count; i++) {
            if (enumeration->values[i].value == value.e) {
                ok = true;
                break;
            }
        }
    }
    if (ok && value.type == PIN_HANDLE && value.u64) {
        ok = ecs_entity_is_alive(world, ecs_entity_from_u64(value.u64)) &&
             !ecs_entity_is_retiring(world, ecs_entity_from_u64(value.u64));
    }
    if (ok) {
        ok = ecs_component_write_value(world, entity, desc->id, field->offset, declared, value) ==
             ECS_OK;
    }
    arena_free(&storage);
    av_doc_free(doc);
    if (!ok) {
        return fail(error, size, "Runtime scalar value is invalid, unsupported or out of range");
    }
    return true;
}

void runtime_properties_present(
    const ecs_world *world,
    pin_array *rows,
    arena *storage,
    const component_presentation *presentation
)
{
    for (int i = 0; i < rows->count; i++) {
        runtime_property *row = rows->items[i].ptr;
        const ecs_component_desc *component = component_of(world, row->component);
        assert(component);
        row->presentation_category = copy(
            storage,
            component_presentation_category(component_presentation_tab(presentation, component))
        );
        row->icon = copy(
            storage,
            component_presentation_icon_name(component_presentation_icon(presentation, component))
        );
        for (int f = 0; component->type && f < component->type->field_count; f++) {
            if (strcmp(component->type->fields[f].name, row->field)) {
                continue;
            }
            field_hint hint = component_presentation_field(presentation, component, f);
            row->unit = copy(storage, hint.unit);
            row->min = hint.min;
            row->max = hint.max;
            row->bounded = hint.max > hint.min;
            break;
        }
    }
}
