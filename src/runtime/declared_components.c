#include "runtime/declared_components.h"

#include "capabilities/catalog.h"
#include "runtime/component_catalog.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/catalog.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_COMPONENTS 1024
#define MAX_FIELDS 256
#define ENTITY_TYPE "ecs.entity"

struct declared_components
{
    int references;
    arena storage;
    int count;
    type_desc *types;
    const type_desc **type_list;
    ecs_component_desc *descs;
    const ecs_component_desc **desc_list;
    script_catalog_type *script_types;
    script_catalog catalog;
    /* Authored field type spellings, kept for the wire. */
    const char ***spellings;
    /* The native registry followed by desc_list. */
    const ecs_component_desc **project;
};
/* Sets alive now, oldest first; the newest one is the project's. */
static const declared_components **alive;
static void publish(const declared_components *c)
{
    type_catalog_set_project(
        c ? &(type_catalog_pack){ .types = c->type_list, .type_count = c->count } : NULL
    );
    script_catalog_set_project(c ? &c->catalog : NULL);
}
static bool fail(char *error, size_t size, const char *name, const char *message)
{
    if (size) {
        snprintf(error, size, "Component %s: %s", name ? name : "(unnamed)", message);
    }
    return false;
}
static const char *own(declared_components *c, const char *text)
{
    if (!text) {
        return NULL;
    }
    size_t bytes = strlen(text) + 1;
    char *copy = arena_alloc(&c->storage, bytes);
    memcpy(copy, text, bytes);
    return copy;
}
static size_t scalar_size(pin_type type)
{
    switch (type) {
        case PIN_BOOL:
        case PIN_I8:
        case PIN_U8: return 1;
        case PIN_I16:
        case PIN_U16: return 2;
        case PIN_I32:
        case PIN_U32:
        case PIN_F32:
        case PIN_ENUM: return 4;
        case PIN_I64:
        case PIN_U64:
        case PIN_F64:
        case PIN_HANDLE: return 8;
        case PIN_STR:
        case PIN_NAME: return sizeof(const char *);
        default: return 0;
    }
}
static size_t struct_alignment(const type_desc *type, int depth)
{
    size_t alignment = 1;
    for (int i = 0; i < type->field_count && depth < 16; i++) {
        const type_field *field = &type->fields[i];
        size_t a = field->type == PIN_STRUCT
                     ? struct_alignment(type_catalog_find(field->type_name), depth + 1)
                     : scalar_size(field->type);
        alignment = a > alignment ? a : alignment;
    }
    return alignment;
}
/* Fills storage, size, alignment and script type for one authored spelling. */
static bool field_type(const char *spelling, type_field *field, size_t *alignment, script_type *type)
{
    *type = script_builtin_type(spelling);
    if (!(type->flags & SCRIPT_TYPE_ERROR)) {
        field->type = type->storage;
        field->size = scalar_size(type->storage);
        field->type_name = type->storage == PIN_HANDLE ? ENTITY_TYPE : NULL;
        *alignment = field->size;
        bool entity = type->storage != PIN_HANDLE ||
                      type->nominal == script_catalog_id("type", ENTITY_TYPE);
        return field->size && type->storage != PIN_ENUM && !type->flags && entity;
    }
    const type_desc *nested = type_catalog_find(spelling);
    if (!nested || !nested->size || !nested->field_count) {
        return false;
    }
    field->type = PIN_STRUCT;
    field->size = nested->size;
    field->type_name = nested->name;
    *alignment = struct_alignment(nested, 0);
    *type = (script_type){ .storage = PIN_STRUCT,
                           .nominal = script_catalog_id("type", nested->name) };
    return true;
}
bool declared_components_field_type(const char *spelling)
{
    type_field field = { 0 };
    size_t alignment;
    script_type type;
    return field_type(spelling, &field, &alignment, &type);
}
static bool identifier(const char *name)
{
    if (!name || !(*name == '_' || (*name >= 'A' && *name <= 'Z') ||
                   (*name >= 'a' && *name <= 'z'))) {
        return false;
    }
    for (const char *p = name; *p; p++) {
        if (!(*p == '_' || (*p >= '0' && *p <= '9') ||
              (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))) {
            return false;
        }
    }
    return true;
}
static bool build(declared_components *c, int index, const declared_component *in, char *error, size_t size)
{
    const char *name = in->name;
    if (!identifier(name)) {
        return fail(error, size, name, "name must be an identifier");
    }
    if (type_catalog_find(name)) {
        return fail(error, size, name, "name is already an engine type");
    }
    if (!(script_builtin_type(name).flags & SCRIPT_TYPE_ERROR)) {
        return fail(error, size, name, "name is a built-in script type");
    }
    for (int i = 0; i < index; i++) {
        if (!strcmp(c->types[i].name, name)) {
            return fail(error, size, name, "is declared twice");
        }
    }
    if (in->field_count < 1 || in->field_count > MAX_FIELDS || !in->fields) {
        return fail(error, size, name, "needs between 1 and 256 fields");
    }
    name = own(c, name);
    int count = in->field_count;
    type_field *fields = arena_alloc(&c->storage, (size_t)count * sizeof *fields);
    script_catalog_field *script_fields = arena_alloc(&c->storage, (size_t)count * sizeof *script_fields);
    const char **spellings = arena_alloc(&c->storage, (size_t)count * sizeof *spellings);
    size_t offset = 0, alignment = 1;
    for (int i = 0; i < count; i++) {
        const declared_field *f = &in->fields[i];
        if (!identifier(f->name) || !f->type) {
            return fail(error, size, name, "field names must be identifiers with a type");
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(fields[j].name, f->name)) {
                return fail(error, size, name, "field is declared twice");
            }
        }
        type_field field = { .name = own(c, f->name),
                             .description = own(c, f->description),
                             .unit = own(c, f->unit),
                             .min = f->min,
                             .max = f->max,
                             .range_contract = f->min != f->max };
        size_t field_alignment;
        script_type type;
        if (!field_type(f->type, &field, &field_alignment, &type)) {
            char message[160];
            snprintf(message, sizeof message, "field %s has unknown type %s", f->name, f->type);
            return fail(error, size, name, message);
        }
        if (!isfinite(f->min) || !isfinite(f->max) || !type_field_range_valid(&field)) {
            return fail(error, size, name, "range endpoints must fit the field's numeric storage");
        }
        offset = (offset + field_alignment - 1) / field_alignment * field_alignment;
        field.offset = offset;
        offset += field.size;
        alignment = field_alignment > alignment ? field_alignment : alignment;
        fields[i] = field;
        spellings[i] = own(c, f->type);
        size_t bytes = strlen(name) + strlen(field.name) + 2;
        char *qualified = arena_alloc(&c->storage, bytes);
        snprintf(qualified, bytes, "%s.%s", name, field.name);
        script_fields[i] = (script_catalog_field){
            .id = script_catalog_id("field", qualified),
            .name = field.name,
            .type = type,
            .offset = field.offset,
            .writable = true,
            .unit = field.unit,
            .range_contract = field.range_contract,
            .min = field.min,
            .max = field.max
        };
    }
    type_desc *type = &c->types[index];
    *type = (type_desc){ .name = name,
                         .size = (offset + alignment - 1) / alignment * alignment,
                         .fields = fields,
                         .field_count = count };
    c->spellings[index] = spellings;
    uint64_t layout = type_layout_hash(type);
    c->descs[index] = (ecs_component_desc){ .id = script_catalog_id("component", name),
                                            .key = name,
                                            .name = name,
                                            .element_size = type->size,
                                            .type = type,
                                            .layout_hash = layout,
                                            .index = ECS_INDEX_DENSE };
    uint64_t id = script_catalog_id("type", name);
    c->script_types[index] = (script_catalog_type){
        .id = id,
        .name = name,
        .source_name = name,
        .key = name,
        .type = { .storage = PIN_STRUCT, .nominal = id },
        .fields = script_fields,
        .field_count = count,
        .size = type->size,
        .layout = layout,
        .component_id = c->descs[index].id,
        .component = true,
    };
    c->type_list[index] = type;
    c->desc_list[index] = &c->descs[index];
    return true;
}
declared_components *declared_components_new(
    const declared_component *components,
    int count,
    char *error,
    size_t size
)
{
    assert(count >= 0 && (!count || components) && (!size || error));
    if (count > MAX_COMPONENTS) {
        fail(error, size, NULL, "a project declares at most 1024 components");
        return NULL;
    }
    declared_components *c = xcalloc(1, sizeof *c);
    c->references = 1;
    c->count = count;
    c->types = xcalloc((size_t)count + 1, sizeof *c->types);
    c->type_list = xcalloc((size_t)count + 1, sizeof *c->type_list);
    c->descs = xcalloc((size_t)count + 1, sizeof *c->descs);
    c->desc_list = xcalloc((size_t)count + 1, sizeof *c->desc_list);
    c->script_types = xcalloc((size_t)count + 1, sizeof *c->script_types);
    c->spellings = xcalloc((size_t)count + 1, sizeof *c->spellings);
    /* A new set replaces the newest one, so it may reuse that set's names. */
    if (acount(alive)) {
        publish(NULL);
    }
    for (int i = 0; i < count; i++) {
        if (!build(c, i, &components[i], error, size)) {
            if (acount(alive)) {
                publish(alast(alive));
            }
            c->count = 0;
            declared_components_free(c);
            return NULL;
        }
    }
    c->catalog = (script_catalog){ .types = c->script_types, .type_count = count };
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        apush(c->project, REGISTRY_COMPONENTS[i]);
    }
    for (int i = 0; i < count; i++) {
        apush(c->project, c->desc_list[i]);
    }
    apush(alive, c);
    publish(c);
    return c;
}
declared_components *declared_components_retain(declared_components *c)
{
    assert(c && c->references > 0);
    c->references++;
    return c;
}
void declared_components_free(declared_components *c)
{
    if (!c || --c->references) {
        return;
    }
    for (int i = 0; i < acount(alive); i++) {
        if (alive[i] != c) {
            continue;
        }
        bool newest = i == acount(alive) - 1;
        if (newest) {
            publish(NULL);
        }
        memmove(&alive[i], &alive[i + 1], (size_t)(acount(alive) - i - 1) * sizeof *alive);
        asetlen(alive, acount(alive) - 1);
        if (newest && acount(alive)) {
            publish(alast(alive));
        }
        if (!acount(alive)) {
            afree(alive);
        }
        break;
    }
    afree(c->project);
    free(c->types);
    free(c->type_list);
    free(c->descs);
    free(c->desc_list);
    free(c->script_types);
    free(c->spellings);
    arena_free(&c->storage);
    free(c);
}
ecs_component_catalog declared_components_ecs(const declared_components *c)
{
    assert(c);
    return (ecs_component_catalog){ c->desc_list, c->count };
}
const script_catalog *declared_components_catalog(const declared_components *c)
{
    assert(c);
    return &c->catalog;
}
ecs_component_catalog project_components()
{
    if (!acount(alive)) {
        return (ecs_component_catalog){ REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT };
    }
    const declared_components *c = alast(alive);
    return (ecs_component_catalog){ c->project, acount(c->project) };
}
void declared_components_wire(execution_wire *wire, declared_components **out)
{
    int count = wire->reading || !*out ? 0 : (*out)->count;
    execution_wire_count(wire, &count, 2);
    if (wire->error || count > MAX_COMPONENTS) {
        wire->error = wire->error ? wire->error : "too many declared components";
        return;
    }
    declared_component *components = xcalloc((size_t)count + 1, sizeof *components);
    declared_field **fields = xcalloc((size_t)count + 1, sizeof *fields);
    for (int i = 0; i < count && !wire->error; i++) {
        declared_component *component = &components[i];
        if (!wire->reading) {
            const type_desc *type = &(*out)->types[i];
            component->name = type->name;
            component->field_count = type->field_count;
        }
        execution_wire_string(wire, &component->name);
        execution_wire_count(wire, &component->field_count, 2);
        if (wire->error || component->field_count > MAX_FIELDS) {
            wire->error = wire->error ? wire->error
                                      : "too many declared component fields";
            break;
        }
        fields[i] = xcalloc((size_t)component->field_count + 1, sizeof *fields[i]);
        for (int j = 0; j < component->field_count; j++) {
            declared_field *field = &fields[i][j];
            if (!wire->reading) {
                field->name = (*out)->types[i].fields[j].name;
                field->type = (*out)->spellings[i][j];
                field->min = (*out)->types[i].fields[j].min;
                field->max = (*out)->types[i].fields[j].max;
            }
            execution_wire_string(wire, &field->name);
            execution_wire_string(wire, &field->type);
            uint64_t min, max;
            memcpy(&min, &field->min, sizeof min);
            memcpy(&max, &field->max, sizeof max);
            execution_wire_u64(wire, &min);
            execution_wire_u64(wire, &max);
            if (wire->reading) {
                memcpy(&field->min, &min, sizeof min);
                memcpy(&field->max, &max, sizeof max);
            }
        }
        component->fields = fields[i];
    }
    /* An empty set still replaces the sets of other projects. */
    if (wire->reading && !wire->error) {
        char message[256];
        *out = declared_components_new(components, count, message, sizeof message);
        if (!*out) {
            wire->error = "invalid declared component schema";
        }
    }
    for (int i = 0; i < count; i++) {
        free(fields[i]);
    }
    free(fields);
    free(components);
}
static int find(const declared_components *c, uint64_t id)
{
    for (int i = 0; c && i < c->count; i++) {
        if (c->script_types[i].id == id) {
            return i;
        }
    }
    return -1;
}
static bool host(void *user, uint64_t id, execution_prepared_host *out)
{
    const declared_components_linking *l = user;
    return l->base->host(l->base->user, id, out);
}
static bool command(void *user, uint64_t id, const char *reference, execution_prepared_command *out)
{
    const declared_components_linking *l = user;
    return l->base->command(l->base->user, id, reference, out);
}
static bool query(void *user, uint64_t id, execution_prepared_query *out)
{
    const declared_components_linking *l = user;
    return l->base->query(l->base->user, id, out);
}
static bool event(void *user, uint64_t id, uint64_t abi)
{
    const declared_components_linking *l = user;
    return l->base->event(l->base->user, id, abi);
}
static bool type(void *user, uint64_t id, execution_prepared_type *out)
{
    const declared_components_linking *l = user;
    int index = find(l->components, id);
    if (index < 0) {
        return l->base->type && l->base->type(l->base->user, id, out);
    }
    const script_catalog_type *t = &l->components->script_types[index];
    *out = (execution_prepared_type){ .name = t->name,
                                      .type = &l->components->types[index],
                                      .storage = PIN_STRUCT,
                                      .size = t->size,
                                      .layout = t->layout };
    return true;
}
static execution_module *import(void *user, uint64_t id, uint64_t fingerprint)
{
    const declared_components_linking *l = user;
    return l->base->import(l->base->user, id, fingerprint);
}
execution_prepared_linker declared_components_linker(const declared_components_linking *l)
{
    assert(l && l->base);
    const execution_prepared_linker *b = l->base;
    return (execution_prepared_linker){ .user = (void *)l,
                                        .host = b->host ? host : NULL,
                                        .command = b->command ? command : NULL,
                                        .query = b->query ? query : NULL,
                                        .event = b->event ? event : NULL,
                                        .type = type,
                                        .import = b->import ? import : NULL };
}
