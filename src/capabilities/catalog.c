#include "capabilities/catalog.h"

#include <assert.h>
#include <ctype.h>
#include <string.h>

static const script_catalog *project;
void script_catalog_set_project(const script_catalog *catalog)
{
    assert(!catalog != !project);
    project = catalog;
}
const script_catalog *script_catalog_project()
{
    return project;
}
uint64_t script_catalog_id(const char *domain, const char *name)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *)domain; *p; p++) {
        hash = (hash ^ *p) * UINT64_C(1099511628211);
    }
    hash = (hash ^ ':') * UINT64_C(1099511628211);
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        hash = (hash ^ *p) * UINT64_C(1099511628211);
    }
    return hash ? hash : 1;
}
static unsigned next(const char **p)
{
    while (**p == '_' || **p == ' ' || **p == '-') {
        (*p)++;
    }
    unsigned c = (unsigned char)**p;
    if (c) {
        (*p)++;
    }
    return (unsigned)tolower(c);
}
bool script_name_matches(const char *a, const char *b)
{
    for (;;) {
        unsigned x = next(&a), y = next(&b);
        if (x != y) {
            return false;
        }
        if (!x) {
            return true;
        }
    }
}
int script_catalog_find_type(const script_catalog *catalog, const char *name)
{
    int found = -1;
    for (int i = 0; i < catalog->type_count; i++) {
        const char *full = catalog->types[i].name;
        if (!strcmp(full, name)) {
            return i;
        }
        const char *short_name = strrchr(full, '.');
        short_name = short_name ? short_name + 1 : full;
        const script_catalog_type *type = &catalog->types[i];
        if (script_name_matches(full, name) || script_name_matches(short_name, name) ||
            (type->source_name && script_name_matches(type->source_name, name)) ||
            (type->key && script_name_matches(type->key, name))) {
            if (found >= 0) {
                found = -2;
                continue;
            }
            if (found != -2) {
                found = i;
            }
        }
    }
    return found;
}
int script_catalog_find_capability(const script_catalog *catalog, const char *name, script_capability_kind kind)
{
    int found = -1;
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *capability = &catalog->capabilities[i];
        if (capability->kind != kind) {
            continue;
        }
        const char *full = capability->qualified_name ? capability->qualified_name
                                                      : capability->name;
        if (!strcmp(full, name)) {
            return i;
        }
        if (script_name_matches(full, name) ||
            script_name_matches(capability->name, name)) {
            if (found >= 0) {
                found = -2;
                continue;
            }
            if (found != -2) {
                found = i;
            }
        }
    }
    return found;
}
int script_catalog_find_field(const script_catalog_type *type, const char *name)
{
    int found = -1;
    for (int i = 0; i < type->field_count; i++) {
        if (!strcmp(type->fields[i].name, name)) {
            return i;
        }
        if (script_name_matches(type->fields[i].name, name)) {
            if (found >= 0) {
                found = -2;
                continue;
            }
            if (found != -2) {
                found = i;
            }
        }
    }
    return found;
}
const script_catalog_type *script_catalog_type_by_id(const script_catalog *catalog, uint64_t id)
{
    for (int i = 0; i < catalog->type_count; i++) {
        if (catalog->types[i].id == id) {
            return &catalog->types[i];
        }
    }
    return NULL;
}
const script_catalog_capability *script_catalog_capability_by_id(const script_catalog *catalog, uint64_t id)
{
    for (int i = 0; i < catalog->capability_count; i++) {
        if (catalog->capabilities[i].id == id) {
            return &catalog->capabilities[i];
        }
    }
    return NULL;
}
const char *script_scalar_name(pin_type storage)
{
    static const char *const names[] = {
        [PIN_FLOW] = "void",     [PIN_BOOL] = "bool",  [PIN_I8] = "int8",
        [PIN_U8] = "byte",       [PIN_I16] = "int16",  [PIN_U16] = "uint16",
        [PIN_I32] = "int",       [PIN_U32] = "uint",   [PIN_I64] = "int64",
        [PIN_U64] = "uint64",    [PIN_F32] = "float",  [PIN_F64] = "double",
        [PIN_NAME] = "name",     [PIN_STR] = "string", [PIN_HANDLE] = "handle",
        [PIN_STRUCT] = "struct", [PIN_ENUM] = "enum"
    };
    return storage >= PIN_FLOW && storage <= PIN_ENUM ? names[storage] : "error";
}
script_type script_builtin_type(const char *name)
{
    for (int i = PIN_FLOW; i <= PIN_ENUM; i++) {
        if (!strcmp(name, script_scalar_name((pin_type)i))) {
            return (script_type){ .storage = (pin_type)i };
        }
    }
    if (!strcmp(name, "number")) {
        return (script_type){ .storage = PIN_F64 };
    }
    if (!strcmp(name, "exec") || !strcmp(name, "flow")) {
        return (script_type){ .storage = PIN_FLOW };
    }
    if (!strcmp(name, "entity")) {
        return (script_type){ .storage = PIN_HANDLE,
                              .nominal = script_catalog_id("type", "ecs.entity") };
    }
    if (!strcmp(name, "any")) {
        return (script_type){ .storage = PIN_FLOW, .flags = SCRIPT_TYPE_UNTYPED };
    }
    return (script_type){ .flags = SCRIPT_TYPE_ERROR };
}
bool script_type_equal(script_type a, script_type b)
{
    return a.storage == b.storage && a.container == b.container &&
           a.nominal == b.nominal &&
           (a.flags & (SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_RESULTS)) ==
               (b.flags & (SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_RESULTS));
}
bool script_type_numeric(script_type type)
{
    return type.container == PIN_CONTAINER_SINGLE &&
           (!type.nominal || (type.flags & SCRIPT_TYPE_GENERIC)) &&
           type.storage >= PIN_I8 && type.storage <= PIN_F64 &&
           !(type.flags &
             (SCRIPT_TYPE_ERROR | SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_RESULTS));
}
bool script_type_assignable(script_type expected, script_type actual)
{
    if ((expected.flags | actual.flags) & SCRIPT_TYPE_ERROR) {
        return true;
    }
    if ((expected.flags | actual.flags) & SCRIPT_TYPE_UNTYPED) {
        return true;
    }
    expected.flags &= ~SCRIPT_TYPE_GENERIC;
    actual.flags &= ~SCRIPT_TYPE_GENERIC;
    if ((actual.flags & SCRIPT_TYPE_OPTIONAL) &&
        !(expected.flags & SCRIPT_TYPE_OPTIONAL)) {
        return false;
    }
    if (script_type_numeric(expected) && script_type_numeric(actual)) {
        return true;
    }
    if ((expected.storage == PIN_NAME || expected.storage == PIN_STR) &&
        (actual.storage == PIN_NAME || actual.storage == PIN_STR)) {
        actual.storage = expected.storage;
    }
    expected.flags &= ~SCRIPT_TYPE_REFERENCE;
    actual.flags &= ~SCRIPT_TYPE_REFERENCE;
    if (!(actual.flags & SCRIPT_TYPE_OPTIONAL)) {
        expected.flags &= ~SCRIPT_TYPE_OPTIONAL;
    }
    return script_type_equal(expected, actual);
}

script_type script_catalog_canonical_type(const script_catalog *catalog, script_type type)
{
    if (!type.nominal) {
        return type;
    }
    for (int depth = 0; depth <= catalog->type_count; depth++) {
        const script_catalog_type *record = script_catalog_type_by_id(catalog, type.nominal);
        if (!record || !record->underlying) {
            return type;
        }
        if (depth == catalog->type_count) {
            break;
        }
        type.nominal = record->underlying;
    }
    type.flags |= SCRIPT_TYPE_ERROR;
    return type;
}
bool script_catalog_type_assignable(const script_catalog *catalog, script_type expected, script_type actual)
{
    return script_type_assignable(
        script_catalog_canonical_type(catalog, expected),
        script_catalog_canonical_type(catalog, actual)
    );
}

static uint64_t abi_word(uint64_t hash, uint64_t value)
{
    for (int i = 0; i < 8; i++) {
        hash = (hash ^ (value & 255)) * UINT64_C(1099511628211);
        value >>= 8;
    }
    return hash;
}
static uint64_t abi_text(uint64_t hash, const char *text)
{
    if (text) {
        for (; *text; text++) {
            hash = (hash ^ (unsigned char)*text) * UINT64_C(1099511628211);
        }
    }
    return (hash ^ 0) * UINT64_C(1099511628211);
}
static uint64_t abi_type(uint64_t hash, const script_catalog *catalog, script_type type)
{
    hash = abi_word(hash, type.storage);
    hash = abi_word(hash, type.container);
    hash = abi_word(hash, type.nominal);
    hash = abi_word(hash, type.parameter);
    hash = abi_word(hash, type.flags);
    const script_catalog_type *record = script_catalog_type_by_id(catalog, type.nominal);
    if (record) {
        hash = abi_word(hash, record->layout);
        hash = abi_word(hash, record->size);
        hash = abi_word(hash, record->underlying);
    }
    return hash;
}
uint64_t script_catalog_capability_abi(const script_catalog *catalog, const script_catalog_capability *capability)
{
    uint64_t hash = script_catalog_id("capability-abi", "1");
    hash = abi_word(hash, capability->kind);
    hash = abi_word(hash, capability->parameter_count);
    for (int i = 0; i < capability->parameter_count; i++) {
        const script_catalog_parameter *parameter = &capability->parameters[i];
        hash = abi_type(hash, catalog, parameter->type);
        hash = abi_word(hash, parameter->reference_kind);
        hash = abi_word(hash, parameter->member);
    }
    hash = abi_word(hash, capability->result_count);
    for (int i = 0; i < capability->result_count; i++) {
        hash = abi_type(hash, catalog, capability->results[i].type);
        hash = abi_word(hash, capability->results[i].member);
    }
    hash = abi_word(hash, capability->entry_count);
    for (int i = 0; i < capability->entry_count; i++) {
        hash = abi_text(hash, capability->entries[i]);
    }
    hash = abi_word(hash, capability->outcome_count);
    for (int i = 0; i < capability->outcome_count; i++) {
        hash = abi_text(hash, capability->outcomes[i]);
    }
    hash = abi_word(hash, capability->completion_outcome);
    hash = abi_word(hash, capability->state_kind);
    hash = abi_word(hash, capability->effects);
    hash = abi_word(hash, capability->state_access);
    hash = abi_word(hash, capability->component_access);
    if (capability->component_access != SCRIPT_COMPONENT_ACCESS_NONE) {
        hash = abi_word(hash, capability->component_entity);
        hash = abi_word(hash, capability->component_value);
    }
    hash = abi_word(hash, capability->struct_access);
    hash = abi_word(hash, capability->component);
    hash = abi_word(hash, capability->component_field);
    hash = abi_word(hash, capability->aggregate);
    hash = abi_word(hash, capability->flow.kind);
    if (capability->flow.kind != SCRIPT_FLOW_ACCESS_NONE) {
        for (int i = 0; i < 2; i++) {
            hash = abi_word(hash, capability->flow.inputs[i]);
            hash = abi_word(hash, capability->flow.results[i]);
            hash = abi_word(hash, capability->flow.outcomes[i]);
        }
        hash = abi_word(hash, capability->flow.break_entry);
    }
    hash = abi_word(hash, capability->receiver);
    hash = abi_word(hash, capability->pure);
    hash = abi_word(hash, capability->variadic);
    hash = abi_word(hash, capability->variadic_optional);
    hash = abi_word(hash, capability->event_sender);
    return abi_word(hash, capability->numeric_aggregate);
}
