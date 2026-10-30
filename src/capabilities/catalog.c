#include "capabilities/catalog.h"

#include "support/array.h"
#include "support/xalloc.h"
#include "types/scalar.h"
#include "types/text.h"
#include "types/union.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
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
    return script_catalog_id_bytes(domain, name, strlen(name));
}
uint64_t script_catalog_id_bytes(const char *domain, const char *bytes, size_t length)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *)domain; *p; p++) {
        hash = (hash ^ *p) * UINT64_C(1099511628211);
    }
    hash = (hash ^ ':') * UINT64_C(1099511628211);
    for (size_t i = 0; i < length; i++) {
        hash = (hash ^ (unsigned char)bytes[i]) * UINT64_C(1099511628211);
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
        if (catalog->types[i].internal) {
            continue;
        }
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
        [PIN_FLOW] = "void",     [PIN_BOOL] = "bool",
        [PIN_I8] = "i8",         [PIN_U8] = "u8",
        [PIN_I16] = "i16",       [PIN_U16] = "u16",
        [PIN_I32] = "int",       [PIN_U32] = "u32",
        [PIN_I64] = "i64",       [PIN_U64] = "u64",
        [PIN_F32] = "number",    [PIN_F64] = "f64",
        [PIN_NAME] = "name",     [PIN_STR] = "string",
        [PIN_HANDLE] = "handle", [PIN_STRUCT] = "struct",
        [PIN_ENUM] = "enum",     [PIN_TASK] = "task",
        [PIN_UNION] = "union",   [PIN_POINTER] = "pointer"
    };
    return storage >= PIN_FLOW && storage <= PIN_POINTER ? names[storage] : "error";
}
const char *script_reference_type_name(script_reference_kind kind)
{
    switch (kind) {
        case SCRIPT_REFERENCE_PROTOTYPE: return "Prototype";
        case SCRIPT_REFERENCE_COMPONENT: return "Component";
        case SCRIPT_REFERENCE_RELATIONSHIP: return "Relationship";
        case SCRIPT_REFERENCE_NONE: return NULL;
    }
    return NULL;
}
script_type script_reference_type(script_reference_kind kind)
{
    const char *name = script_reference_type_name(kind);
    assert(name);
    return (script_type){ .storage = kind == SCRIPT_REFERENCE_RELATIONSHIP ? PIN_U64 : PIN_NAME,
                          .nominal = script_catalog_id("symbol-type", name) };
}
script_reference_kind script_reference_kind_for_type(script_type type)
{
    if (!type.nominal || type.container != PIN_CONTAINER_SINGLE) {
        return SCRIPT_REFERENCE_NONE;
    }
    for (int kind = SCRIPT_REFERENCE_PROTOTYPE; kind <= SCRIPT_REFERENCE_RELATIONSHIP;
         kind++) {
        if (type.nominal == script_reference_type((script_reference_kind)kind).nominal) {
            return (script_reference_kind)kind;
        }
    }
    return SCRIPT_REFERENCE_NONE;
}
script_type script_char_type()
{
    return (script_type){ .storage = PIN_I32,
                          .nominal = script_catalog_id("type", "char") };
}
bool script_type_char(script_type type)
{
    return type.storage == PIN_I32 && type.container == PIN_CONTAINER_SINGLE &&
           type.nominal == script_char_type().nominal;
}
const char *script_numeric_name(script_type type)
{
    return script_type_char(type) ? "char" : script_scalar_name(type.storage);
}
/* Every Unicode scalar, 0 through 0x10FFFF, fits the integer `to`. */
static bool char_widens(pin_type to)
{
    return to >= PIN_I8 && to <= PIN_U64 && behavior_scalar_integer_low(to) <= 0 &&
           behavior_scalar_integer_high(to) >= 0x10FFFF;
}
script_type script_builtin_type(const char *name)
{
    for (int kind = SCRIPT_REFERENCE_PROTOTYPE; kind <= SCRIPT_REFERENCE_RELATIONSHIP;
         kind++) {
        if (!strcmp(name, script_reference_type_name((script_reference_kind)kind))) {
            return script_reference_type((script_reference_kind)kind);
        }
    }
    if (!strcmp(name, "Cancelled")) {
        return script_cancelled_type();
    }
    for (int i = PIN_FLOW; i <= PIN_ENUM; i++) {
        if (!strcmp(name, script_scalar_name((pin_type)i))) {
            return (script_type){ .storage = (pin_type)i };
        }
    }
    /* Odin's sized names for the two game spellings. */
    if (!strcmp(name, "i32")) {
        return (script_type){ .storage = PIN_I32 };
    }
    if (!strcmp(name, "f32")) {
        return (script_type){ .storage = PIN_F32 };
    }
    if (!strcmp(name, "char")) {
        return script_char_type();
    }
    if (!strcmp(name, "exec") || !strcmp(name, "flow")) {
        return (script_type){ .storage = PIN_FLOW };
    }
    if (!strcmp(name, "entity")) {
        return (script_type){ .storage = PIN_HANDLE,
                              .nominal = script_catalog_id("type", "ecs.entity") };
    }
    if (!strcmp(name, "task")) {
        return (script_type){ .storage = PIN_TASK, .flags = SCRIPT_TYPE_TASK };
    }
    if (!strcmp(name, "any")) {
        return (script_type){ .storage = PIN_FLOW, .flags = SCRIPT_TYPE_UNTYPED };
    }
    return (script_type){ .flags = SCRIPT_TYPE_ERROR };
}
bool script_type_equal(script_type a, script_type b)
{
    if ((a.flags | b.flags) & SCRIPT_TYPE_TASK) {
        return a.container == b.container && (a.flags & b.flags & SCRIPT_TYPE_TASK) &&
               (!a.parameter || !b.parameter || a.parameter == b.parameter) &&
               (a.flags & SCRIPT_TYPE_OPTIONAL) == (b.flags & SCRIPT_TYPE_OPTIONAL);
    }
    return a.storage == b.storage && a.container == b.container &&
           a.collection == b.collection && a.nominal == b.nominal &&
           (!(a.flags & SCRIPT_TYPE_LITERAL) ||
            !(b.flags & SCRIPT_TYPE_LITERAL) || a.parameter == b.parameter) &&
           (a.flags & (SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_RESULTS |
                       SCRIPT_TYPE_QUERY | SCRIPT_TYPE_QUERY_ROW)) ==
               (b.flags & (SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_RESULTS |
                           SCRIPT_TYPE_QUERY | SCRIPT_TYPE_QUERY_ROW));
}
script_type script_catalog_pointee_type(const script_catalog *catalog, script_type type)
{
    const script_catalog_type *descriptor = type.storage == PIN_POINTER &&
                                                    type.container == PIN_CONTAINER_SINGLE
                                              ? script_catalog_type_by_id(catalog, type.nominal)
                                              : NULL;
    return descriptor ? descriptor->element_type
                      : (script_type){ .flags = SCRIPT_TYPE_ERROR };
}
script_type script_catalog_element_type(const script_catalog *catalog, script_type type)
{
    const script_catalog_type *descriptor = script_catalog_type_by_id(catalog, type.collection);
    if (descriptor) {
        return descriptor->element_type;
    }
    type.container = PIN_CONTAINER_SINGLE;
    type.collection = 0;
    return type;
}
script_type script_catalog_key_type(const script_catalog *catalog, script_type type)
{
    const script_catalog_type *descriptor = script_catalog_type_by_id(catalog, type.collection);
    return descriptor && descriptor->container == PIN_CONTAINER_MAP
             ? descriptor->key_type
             : (script_type){ .flags = SCRIPT_TYPE_ERROR };
}
bool script_type_numeric(script_type type)
{
    return type.container == PIN_CONTAINER_SINGLE &&
           (!type.nominal || (type.flags & SCRIPT_TYPE_GENERIC) ||
            script_type_char(type)) &&
           type.storage >= PIN_I8 && type.storage <= PIN_F64 &&
           !(type.flags & (SCRIPT_TYPE_ERROR | SCRIPT_TYPE_OPTIONAL |
                           SCRIPT_TYPE_RESULTS | SCRIPT_TYPE_TASK));
}
/* Integers widen to integers and to floating types, and f32 widens to f64.
 * f64 becomes f32, and a floating value an integer, only through an explicit
 * conversion. */
bool script_numeric_implicit(pin_type from, pin_type to)
{
    if (to == PIN_F64) {
        return true;
    }
    if (to == PIN_F32) {
        return from != PIN_F64;
    }
    return from <= PIN_U64 && behavior_scalar_integer_widens(from, to);
}
bool script_type_assignable(script_type expected, script_type actual)
{
    if ((expected.flags | actual.flags) & SCRIPT_TYPE_ERROR) {
        return true;
    }
    if (script_reference_kind_for_type(expected) != SCRIPT_REFERENCE_NONE ||
        script_reference_kind_for_type(actual) != SCRIPT_REFERENCE_NONE) {
        if ((actual.flags & SCRIPT_TYPE_OPTIONAL) &&
            !(expected.flags & SCRIPT_TYPE_OPTIONAL)) {
            return false;
        }
        expected.flags &= ~SCRIPT_TYPE_OPTIONAL;
        actual.flags &= ~SCRIPT_TYPE_OPTIONAL;
        return script_type_equal(expected, actual);
    }
    if (expected.flags & SCRIPT_TYPE_BOXED_OPTIONAL) {
        expected.flags &= ~(SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_BOXED_OPTIONAL);
        actual.flags &= ~(SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_BOXED_OPTIONAL);
        return script_type_equal(expected, actual);
    }
    if ((expected.flags | actual.flags) & SCRIPT_TYPE_UNTYPED) {
        return expected.storage != PIN_POINTER || actual.storage == PIN_POINTER ||
               (expected.flags & SCRIPT_TYPE_UNTYPED);
    }
    expected.flags &= ~SCRIPT_TYPE_GENERIC;
    actual.flags &= ~SCRIPT_TYPE_GENERIC;
    if ((expected.flags | actual.flags) & SCRIPT_TYPE_TASK) {
        return expected.container == actual.container &&
               (expected.flags & actual.flags & SCRIPT_TYPE_TASK) &&
               (!(actual.flags & SCRIPT_TYPE_OPTIONAL) ||
                (expected.flags & SCRIPT_TYPE_OPTIONAL)) &&
               (!expected.parameter || expected.parameter == actual.parameter);
    }
    if ((actual.flags & SCRIPT_TYPE_OPTIONAL) &&
        !(expected.flags & SCRIPT_TYPE_OPTIONAL)) {
        return false;
    }
    if (expected.flags & SCRIPT_TYPE_OPTIONAL) {
        expected.flags &= ~(SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_BOXED_OPTIONAL);
        actual.flags &= ~(SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_BOXED_OPTIONAL);
    }
    if (script_type_numeric(expected) && script_type_numeric(actual)) {
        if (script_type_char(expected) || script_type_char(actual)) {
            return script_type_char(actual) &&
                   (script_type_char(expected) || char_widens(expected.storage));
        }
        return script_numeric_implicit(actual.storage, expected.storage);
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
bool script_catalog_type_contains_error(const script_catalog *catalog, script_type type)
{
    if (type.container != PIN_CONTAINER_SINGLE || type.collection ||
        (type.storage != PIN_STRUCT && type.storage != PIN_UNION)) {
        return false;
    }
    if (type.flags & SCRIPT_TYPE_ERROR_ALTERNATIVE) {
        return true;
    }
    const script_catalog_type *definition = script_catalog_type_by_id(catalog, type.nominal);
    if (!definition) {
        return false;
    }
    if (definition->error) {
        return true;
    }
    bool found = false;
    bool *seen = xcalloc((size_t)catalog->type_count, sizeof *seen);
    script_type *work = NULL;
    apush(work, type);
    while (acount(work) && !found) {
        script_type value = apop(work);
        if (value.container != PIN_CONTAINER_SINGLE || value.collection ||
            (value.storage != PIN_STRUCT && value.storage != PIN_UNION)) {
            continue;
        }
        const script_catalog_type
            *record = script_catalog_type_by_id(catalog, value.nominal);
        if (!record) {
            continue;
        }
        int index = (int)(record - catalog->types);
        if (seen[index]) {
            continue;
        }
        seen[index] = true;
        found = record->error;
        if (record->underlying) {
            value.nominal = record->underlying;
            apush(work, value);
        }
        for (int i = 0; i < record->variant_count; i++) {
            /* Flattened transparent unions retain a declared tagged union's
             * nominal on its alternatives. Its fields remain encapsulated. */
            apush(work, record->variants[i].type);
        }
    }
    free(seen);
    afree(work);
    return found;
}
bool script_catalog_type_assignable(const script_catalog *catalog, script_type expected, script_type actual)
{
    if (expected.flags & SCRIPT_TYPE_LITERAL) {
        return (actual.flags & SCRIPT_TYPE_LITERAL) &&
               expected.parameter == actual.parameter;
    }
    if ((expected.flags & SCRIPT_TYPE_UNION) &&
        expected.container == PIN_CONTAINER_SINGLE &&
        !(actual.flags & SCRIPT_TYPE_ERROR)) {
        if (expected.nominal == actual.nominal && (actual.flags & SCRIPT_TYPE_UNION) &&
            expected.container == actual.container &&
            (!(actual.flags & SCRIPT_TYPE_OPTIONAL) ||
             (expected.flags & SCRIPT_TYPE_OPTIONAL))) {
            return true;
        }
        const script_catalog_type
            *type = script_catalog_type_by_id(catalog, expected.nominal);
        const script_catalog_type
            *source = (actual.flags & SCRIPT_TYPE_UNION) &&
                              !(actual.flags & SCRIPT_TYPE_BOXED_OPTIONAL) &&
                              actual.container == PIN_CONTAINER_SINGLE
                        ? script_catalog_type_by_id(catalog, actual.nominal)
                        : NULL;
        if (source) {
            for (int i = 0; i < source->variant_count; i++) {
                bool found = false;
                for (int j = 0; type && j < type->variant_count; j++) {
                    found |= source->variants[i].tag == type->variants[j].tag;
                }
                if (!found) {
                    return false;
                }
            }
            return true;
        }
        for (int i = 0; type && i < type->variant_count; i++) {
            const script_catalog_variant *v = &type->variants[i];
            if (!v->tagged && script_type_equal(v->type, actual) &&
                (!v->is_literal || ((actual.flags & SCRIPT_TYPE_LITERAL) &&
                                    v->type.parameter == actual.parameter))) {
                return true;
            }
        }
        return false;
    }
    return script_type_assignable(
        script_catalog_canonical_type(catalog, expected),
        script_catalog_canonical_type(catalog, actual)
    );
}

script_type script_cancelled_type()
{
    return (script_type){ .storage = PIN_UNION,
                          .nominal = PIN_UNION_CANCELLED_TAG,
                          .flags = SCRIPT_TYPE_SINGLETON };
}
uint64_t script_type_tag(script_type type)
{
    if (type.flags & SCRIPT_TYPE_SINGLETON) {
        return type.nominal;
    }
    if (type.flags & SCRIPT_TYPE_LITERAL) {
        return type.parameter;
    }
    char name[128];
    snprintf(
        name,
        sizeof name,
        "%d:%d:%llu:%llu:%u",
        type.storage,
        type.container,
        (unsigned long long)(type.flags & SCRIPT_TYPE_TASK ? 0 : type.nominal),
        (unsigned long long)(type.flags & SCRIPT_TYPE_TASK ? type.parameter : 0),
        type.flags & (SCRIPT_TYPE_RESULTS | SCRIPT_TYPE_TASK | SCRIPT_TYPE_OPTIONAL)
    );
    if (type.collection) {
        size_t length = strlen(name);
        snprintf(
            name + length,
            sizeof name - length,
            ":%llu",
            (unsigned long long)type.collection
        );
    }
    return script_catalog_id("union-type", name);
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
static uint64_t abi_type_shape(
    uint64_t hash,
    const script_catalog *catalog,
    script_type type,
    uint64_t *parents,
    int depth
)
{
    hash = abi_word(hash, type.storage);
    hash = abi_word(hash, type.container);
    hash = abi_word(hash, type.nominal);
    hash = abi_word(hash, type.parameter);
    hash = abi_word(hash, type.flags & ~SCRIPT_TYPE_ERROR_ALTERNATIVE);
    hash = abi_word(hash, type.collection);
    if (type.collection && depth < 64) {
        const script_catalog_type *collection = script_catalog_type_by_id(catalog, type.collection);
        if (collection) {
            hash = abi_word(hash, collection->container);
            for (int i = 0; i < depth; i++) {
                if (parents[i] == type.collection) {
                    return hash;
                }
            }
            parents[depth++] = type.collection;
            if (collection->container == PIN_CONTAINER_MAP) {
                hash = abi_type_shape(hash, catalog, collection->key_type, parents, depth);
            }
            hash = abi_type_shape(hash, catalog, collection->element_type, parents, depth);
        }
    }
    if ((type.flags & SCRIPT_TYPE_TASK) && type.parameter && depth < 64) {
        const script_catalog_type *result = script_catalog_type_by_id(catalog, type.parameter);
        if (result) {
            bool recursive = false;
            for (int i = 0; i < depth; i++) {
                recursive |= parents[i] == type.parameter;
            }
            if (recursive) {
                return hash;
            }
            parents[depth] = type.parameter;
            hash = abi_type_shape(hash, catalog, result->type, parents, depth + 1);
            hash = abi_word(hash, result->field_count);
            for (int i = 0; i < result->field_count; i++) {
                hash = abi_text(hash, result->fields[i].name);
                hash = abi_type_shape(hash, catalog, result->fields[i].type, parents, depth + 1);
            }
        }
    }
    const script_catalog_type *record = script_catalog_type_by_id(catalog, type.nominal);
    if (record) {
        hash = abi_word(hash, record->layout);
        hash = abi_word(hash, record->size);
        hash = abi_word(hash, record->underlying);
        for (int i = 0; i < record->field_count; i++) {
            hash = abi_word(hash, record->fields[i].c_string);
        }
        for (int i = 0; i < depth; i++) {
            if (parents[i] == record->id) {
                return hash;
            }
        }
        if (depth == 64) {
            return hash;
        }
        parents[depth++] = record->id;
        if (type.storage == PIN_POINTER && type.container == PIN_CONTAINER_SINGLE) {
            hash = abi_type_shape(hash, catalog, record->element_type, parents, depth);
        }
        hash = abi_word(hash, record->variant_count);
        for (int i = 0; i < record->variant_count; i++) {
            const script_catalog_variant *v = &record->variants[i];
            hash = abi_word(hash, v->tag);
            hash = abi_text(hash, v->name);
            hash = abi_word(hash, v->tagged);
            hash = abi_word(hash, v->is_literal);
            if (!v->tagged) {
                hash = abi_type_shape(hash, catalog, v->type, parents, depth);
            }
            if (v->is_literal) {
                hash = abi_word(hash, v->literal.type);
                pin_string text = pin_text(v->literal);
                for (uint32_t j = 0; j < text.length; j++) {
                    hash = (hash ^ (unsigned char)text.bytes[j]) *
                           UINT64_C(1099511628211);
                }
                hash = (hash ^ 0) * UINT64_C(1099511628211);
            }
            hash = abi_word(hash, v->field_count);
            for (int j = 0; j < v->field_count; j++) {
                hash = abi_text(hash, v->fields[j].name);
                hash = abi_type_shape(hash, catalog, v->fields[j].type, parents, depth);
            }
        }
    }
    return hash;
}
static uint64_t abi_type(uint64_t hash, const script_catalog *catalog, script_type type)
{
    uint64_t parents[64];
    return abi_type_shape(hash, catalog, type, parents, 0);
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
        hash = abi_word(hash, parameter->c_string);
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
    if (capability->operation != SCRIPT_OPERATION_UNKNOWN) {
        /* Preserve opaque-call identities; a checked operation is a different
         * semantic ABI even when its source signature is identical. */
        hash = abi_text(hash, "engine-operation");
        hash = abi_word(hash, capability->operation);
        const script_operation_contract *contract = script_operation_describe(
            capability->operation
        );
        if (contract) {
            hash = abi_text(hash, contract->name);
            hash = abi_text(hash, contract->instruction);
            hash = abi_word(hash, contract->version);
            hash = abi_word(hash, contract->effects);
            hash = abi_word(hash, contract->guarantees);
        }
    }
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
    hash = abi_word(hash, capability->numeric_aggregate);
    /* Only a capability that takes language values or owns its result
     * hashes them, so every other capability keeps its identity. */
    if (capability->language_values || capability->owned_result) {
        hash = abi_text(hash, "language-values");
        hash = abi_word(hash, capability->language_values);
        hash = abi_word(hash, capability->owned_result);
    }
    return hash;
}
