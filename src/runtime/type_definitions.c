#include "runtime/type_definitions.h"

#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TYPES 65536
#define MAX_FIELDS 65536
struct native_type_catalog
{
    arena storage;
    script_catalog catalog;
    script_catalog_type *types;
    const type_desc **layouts;
};
static bool fail(char *error, size_t size, const char *name, const char *message)
{
    if (size) {
        snprintf(error, size, "%s: %s", name ? name : "Native types", message);
    }
    return false;
}
static script_type field_type(const type_field *field)
{
    script_type type = { .storage = field->type };
    if (field->type_name && (field->type == PIN_STRUCT || field->type == PIN_HANDLE ||
                             field->type == PIN_ENUM)) {
        type.nominal = script_catalog_id("type", field->type_name);
    }
    return type;
}
static bool scalar_layout(const type_field *field)
{
    size_t bytes, alignment;
#define SCALAR(KIND, TYPE)                                                     \
    case KIND:                                                                 \
        bytes = sizeof(TYPE);                                                  \
        alignment = _Alignof(TYPE);                                            \
        break
    switch (field->type) {
        SCALAR(PIN_BOOL, bool);
        SCALAR(PIN_I8, int8_t);
        SCALAR(PIN_U8, uint8_t);
        SCALAR(PIN_I16, int16_t);
        SCALAR(PIN_U16, uint16_t);
        SCALAR(PIN_I32, int32_t);
        SCALAR(PIN_U32, uint32_t);
        SCALAR(PIN_I64, int64_t);
        SCALAR(PIN_U64, uint64_t);
        SCALAR(PIN_F32, float);
        SCALAR(PIN_F64, double);
        SCALAR(PIN_ENUM, int32_t);
        SCALAR(PIN_HANDLE, uint64_t);
        SCALAR(PIN_STR, const char *);
        SCALAR(PIN_NAME, const char *);
        default: return false;
    }
#undef SCALAR
    return field->size == bytes && field->offset % alignment == 0;
}
static bool append(
    native_type_catalog *out,
    const native_type_definition *definition,
    int *fields,
    char *error,
    size_t size
)
{
    const type_desc *layout = definition->layout;
    if (!layout || !layout->name || !*layout->name || !layout->size ||
        layout->field_count < 0 || layout->field_count > MAX_FIELDS - *fields ||
        (layout->field_count && !layout->fields) || definition->storage != PIN_STRUCT) {
        return fail(error, size, layout ? layout->name : NULL, "Invalid explicit type declaration");
    }
    uint64_t id = script_catalog_id("type", layout->name);
    for (int i = 0; i < acount(out->types); i++) {
        if (out->types[i].id == id) {
            return fail(error, size, layout->name, "Duplicate explicit type declaration");
        }
    }
    script_catalog_field *converted = arena_alloc(
        &out->storage,
        (size_t)layout->field_count * sizeof *converted
    );
    for (int i = 0; i < layout->field_count; i++) {
        const type_field *field = &layout->fields[i];
        if (!field->name || !*field->name || field->offset > layout->size ||
            field->size > layout->size - field->offset || !scalar_layout(field)) {
            return fail(error, size, layout->name, "Invalid explicit field declaration");
        }
        size_t bytes = strlen(layout->name) + strlen(field->name) + 2;
        char *name = arena_alloc(&out->storage, bytes);
        snprintf(name, bytes, "%s.%s", layout->name, field->name);
        converted[i] = (script_catalog_field){ .id = script_catalog_id("field", name),
                                               .name = field->name,
                                               .type = field_type(field),
                                               .offset = field->offset,
                                               .writable = true,
                                               .unit = field->unit };
        for (int j = 0; j < i; j++) {
            if (converted[j].id == converted[i].id ||
                !strcmp(converted[j].name, field->name)) {
                return fail(error, size, layout->name, "Duplicate explicit field declaration");
            }
        }
    }
    *fields += layout->field_count;
    apush(
        out->types,
        ((script_catalog_type){
            .id = id,
            .name = layout->name,
            .source_name = definition->source_name,
            .key = definition->key,
            .type = { .storage = definition->storage, .nominal = id },
            .fields = converted,
            .field_count = layout->field_count,
            .size = layout->size,
            .underlying = layout->alias_of ? script_catalog_id("type", layout->alias_of) : 0,
            .component = definition->component != 0,
            .component_id = definition->component })
    );
    apush(out->layouts, layout);
    return true;
}
native_type_catalog *native_type_catalog_new(
    const native_type_definition *definitions,
    int count,
    char *error,
    size_t size
)
{
    assert(!size || error);
    if (size) {
        error[0] = 0;
    }
    if (count < 0 || count > MAX_TYPES || (count && !definitions)) {
        fail(error, size, NULL, "Invalid explicit type count");
        return NULL;
    }
    native_type_catalog *out = xcalloc(1, sizeof *out);
    int fields = 0;
    for (int i = 0; i < count; i++) {
        if (!append(out, &definitions[i], &fields, error, size)) {
            native_type_catalog_free(out);
            return NULL;
        }
    }
    out->catalog.types = out->types;
    out->catalog.type_count = acount(out->types);
    return out;
}
native_capability_pack native_type_catalog_pack(const native_type_catalog *out)
{
    assert(out);
    return (native_capability_pack){ .catalog = &out->catalog,
                                     .layouts = out->layouts,
                                     .layout_count = acount(out->layouts) };
}
void native_type_catalog_free(native_type_catalog *out)
{
    if (!out) {
        return;
    }
    afree(out->types);
    afree(out->layouts);
    arena_free(&out->storage);
    free(out);
}
