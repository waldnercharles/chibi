#include "language/math_types.h"

#include "types/record.h"
#include "types/type.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *const NAMES[] = {
    [SCRIPT_MATH_NONE] = "",     [SCRIPT_MATH_SCALAR] = "number",
    [SCRIPT_MATH_VEC2] = "vec2", [SCRIPT_MATH_VEC3] = "vec3",
    [SCRIPT_MATH_VEC4] = "vec4", [SCRIPT_MATH_QUAT] = "quat",
    [SCRIPT_MATH_MAT2] = "mat2", [SCRIPT_MATH_MAT3] = "mat3",
    [SCRIPT_MATH_MAT4] = "mat4",
};
const char *script_math_name(script_math_kind kind)
{
    return kind >= SCRIPT_MATH_NONE && kind <= SCRIPT_MATH_MAT4 ? NAMES[kind] : "";
}
script_math_kind script_math_named(const char *name)
{
    for (int i = SCRIPT_MATH_VEC2; i <= SCRIPT_MATH_MAT4; i++) {
        if (!strcmp(name, NAMES[i])) {
            return (script_math_kind)i;
        }
    }
    return SCRIPT_MATH_NONE;
}
bool script_math_is_vector(script_math_kind kind)
{
    return kind >= SCRIPT_MATH_VEC2 && kind <= SCRIPT_MATH_VEC4;
}
bool script_math_is_matrix(script_math_kind kind)
{
    return kind >= SCRIPT_MATH_MAT2 && kind <= SCRIPT_MATH_MAT4;
}
int script_math_width(script_math_kind kind)
{
    if (script_math_is_vector(kind)) {
        return kind;
    }
    return script_math_is_matrix(kind) ? kind - SCRIPT_MATH_MAT2 + 2 : 0;
}
int script_math_components(script_math_kind kind)
{
    if (kind == SCRIPT_MATH_SCALAR) {
        return 1;
    }
    if (kind == SCRIPT_MATH_QUAT) {
        return 4;
    }
    int width = script_math_width(kind);
    return script_math_is_matrix(kind) ? width * width : width;
}
script_math_kind script_math_of(const script_catalog *catalog, script_type type)
{
    if (type.container != PIN_CONTAINER_SINGLE || type.collection ||
        (type.flags &
         ~(SCRIPT_TYPE_LITERAL | SCRIPT_TYPE_UNTYPED | SCRIPT_TYPE_REFERENCE))) {
        return SCRIPT_MATH_NONE;
    }
    if (type.storage == PIN_F32 && !type.nominal) {
        return SCRIPT_MATH_SCALAR;
    }
    if (type.storage != PIN_STRUCT) {
        return SCRIPT_MATH_NONE;
    }
    type = script_catalog_canonical_type(catalog, type);
    const script_catalog_type *record = script_catalog_type_by_id(catalog, type.nominal);
    if (!record || !record->name || (type.flags & SCRIPT_TYPE_ERROR)) {
        return SCRIPT_MATH_NONE;
    }
    script_math_kind kind = script_math_named(record->name);
    return kind && record->id == script_catalog_id("type", script_math_name(kind))
             ? kind
             : SCRIPT_MATH_NONE;
}
static void field_name(script_math_kind kind, int index, char out[8])
{
    if (script_math_is_matrix(kind)) {
        int width = script_math_width(kind);
        snprintf(out, 8, "c%dr%d", index / width, index % width);
        return;
    }
    out[0] = "xyzw"[index];
    out[1] = 0;
}
bool script_math_layout_valid(const script_catalog_type *type)
{
    if (!type || !type->name) {
        return false;
    }
    script_math_kind kind = script_math_named(type->name);
    int count = script_math_components(kind);
    if (!kind || type->id != script_catalog_id("type", type->name) ||
        type->type.storage != PIN_STRUCT || type->type.nominal != type->id ||
        type->type.container != PIN_CONTAINER_SINGLE || type->type.collection ||
        type->type.parameter || type->type.flags || type->underlying ||
        type->component || type->component_id || type->derived ||
        type->internal || type->variant_count || type->field_count != count ||
        !type->fields || type->size != (size_t)count * sizeof(float)) {
        return false;
    }
    type_field shape[16];
    char names[16][8];
    for (int i = 0; i < count; i++) {
        field_name(kind, i, names[i]);
        const script_catalog_field *field = &type->fields[i];
        char qualified[32];
        snprintf(qualified, sizeof qualified, "%s.%s", type->name, names[i]);
        if (!field->name || strcmp(field->name, names[i]) ||
            field->id != script_catalog_id("field", qualified) ||
            field->type.storage != PIN_F32 || field->type.nominal ||
            field->type.parameter || field->type.collection ||
            field->type.flags || field->type.container != PIN_CONTAINER_SINGLE ||
            field->offset != (size_t)i * sizeof(float) || !field->writable ||
            field->constant || field->read || field->write || field->c_string ||
            field->unit || field->range_contract || field->has_default) {
            return false;
        }
        shape[i] = (type_field){ .name = names[i],
                                 .type = PIN_F32,
                                 .offset = (size_t)i * sizeof(float),
                                 .size = sizeof(float) };
    }
    type_desc descriptor = { .name = type->name,
                             .size = type->size,
                             .fields = shape,
                             .field_count = count };
    return !type->layout || type->layout == type_layout_hash(&descriptor);
}
bool script_math_layout_equivalent(const script_catalog_type *left, const script_catalog_type *right)
{
    return script_math_layout_valid(left) && script_math_layout_valid(right) &&
           left->id == right->id;
}
static const char *own(arena *storage, const char *text)
{
    size_t bytes = strlen(text) + 1;
    char *copy = arena_alloc(storage, bytes);
    memcpy(copy, text, bytes);
    return copy;
}
script_catalog_type script_math_type(arena *storage, script_math_kind kind)
{
    assert(kind >= SCRIPT_MATH_VEC2 && kind <= SCRIPT_MATH_MAT4);
    const char *name = script_math_name(kind);
    uint64_t id = script_catalog_id("type", name);
    int count = script_math_components(kind);
    type_field shape[16];
    script_catalog_field *fields = arena_alloc(storage, (size_t)count * sizeof *fields);
    for (int i = 0; i < count; i++) {
        char field[8], qualified[32];
        field_name(kind, i, field);
        snprintf(qualified, sizeof qualified, "%s.%s", name, field);
        const char *field_text = own(storage, field);
        shape[i] = (type_field){ .name = field_text, .type = PIN_F32 };
        fields[i] = (script_catalog_field){ .id = script_catalog_id("field", qualified),
                                            .name = field_text,
                                            .type = { .storage = PIN_F32 },
                                            .offset = (size_t)i * sizeof(float),
                                            .writable = true };
    }
    const pin_record *record = pin_record_create(storage, name, id, shape, count);
    assert(record && record->type.size == (size_t)count * sizeof(float));
    return (script_catalog_type){ .id = id,
                                  .name = name,
                                  .source_name = name,
                                  .type = { .storage = PIN_STRUCT, .nominal = id },
                                  .fields = fields,
                                  .field_count = count,
                                  .size = record->type.size,
                                  .layout = record->layout,
                                  .authored_record = true };
}
