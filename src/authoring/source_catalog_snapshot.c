#include "authoring/source_catalog_snapshot.h"

#include "support/array.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/union.h"

#include <assert.h>
#include <string.h>

static const char *text(arena *storage, const char *source)
{
    if (!source) {
        return NULL;
    }
    size_t size = strlen(source) + 1;
    char *copy = arena_alloc(storage, size);
    memcpy(copy, source, size);
    return copy;
}
static const char *const *names(arena *storage, const char *const *source, int count)
{
    assert(count >= 0 && (!count || source));
    const char **copy = arena_alloc(storage, (size_t)count * sizeof *copy);
    for (int i = 0; i < count; i++) {
        copy[i] = text(storage, source[i]);
    }
    return copy;
}
static bool parameters(
    arena *storage,
    const script_catalog_parameter *source,
    int count,
    const script_catalog_parameter **out
)
{
    assert(count >= 0 && (!count || source));
    script_catalog_parameter *copy = arena_alloc(storage, (size_t)count * sizeof *copy);
    for (int i = 0; i < count; i++) {
        copy[i] = source[i];
        copy[i].name = text(storage, source[i].name);
        copy[i].unit = text(storage, source[i].unit);
        if (source[i].has_default && pin_value_snapshot_to_arena(
                                         storage,
                                         &copy[i].default_value,
                                         &source[i].default_value
                                     )) {
            return false;
        }
    }
    *out = copy;
    return true;
}
bool source_catalog_snapshot(arena *storage, const script_catalog *source, script_catalog *out)
{
    assert(storage && source && out);
    assert(source->type_count >= 0 && source->global_count >= 0 && source->capability_count >= 0);
    *out = (script_catalog){ .entity_type = source->entity_type,
                             .tick_event = source->tick_event };
    script_catalog_type *types = arena_alloc(storage, (size_t)source->type_count * sizeof *types);
    for (int i = 0; i < source->type_count; i++) {
        types[i] = source->types[i];
        types[i].name = text(storage, types[i].name);
        types[i].source_name = text(storage, types[i].source_name);
        types[i].key = text(storage, types[i].key);
        assert(types[i].field_count >= 0);
        script_catalog_field *fields = arena_alloc(storage, (size_t)types[i].field_count * sizeof *fields);
        for (int j = 0; j < types[i].field_count; j++) {
            fields[j] = types[i].fields[j];
            fields[j].name = text(storage, fields[j].name);
            fields[j].unit = text(storage, fields[j].unit);
            fields[j].read_key = text(storage, fields[j].read_key);
        }
        types[i].fields = fields;
        script_catalog_variant *variants = arena_alloc(
            storage,
            (size_t)types[i].variant_count * sizeof *variants
        );
        for (int j = 0; j < types[i].variant_count; j++) {
            variants[j] = types[i].variants[j];
            variants[j].name = text(storage, variants[j].name);
            script_catalog_field *payload = arena_alloc(
                storage,
                (size_t)variants[j].field_count * sizeof *payload
            );
            for (int k = 0; k < variants[j].field_count; k++) {
                payload[k] = variants[j].fields[k];
                payload[k].name = text(storage, payload[k].name);
                payload[k].unit = text(storage, payload[k].unit);
                payload[k].read_key = text(storage, payload[k].read_key);
            }
            variants[j].fields = payload;
            if (variants[j].is_literal && pin_value_snapshot_to_arena(
                                              storage,
                                              &variants[j].literal,
                                              &types[i].variants[j].literal
                                          )) {
                return false;
            }
        }
        types[i].variants = variants;
    }
    out->types = types;
    out->type_count = source->type_count;
    script_catalog_relationship *relationships = arena_alloc(
        storage,
        (size_t)source->relationship_count * sizeof *relationships
    );
    for (int i = 0; i < source->relationship_count; i++) {
        relationships[i] = source->relationships[i];
        relationships[i].name = text(storage, relationships[i].name);
    }
    out->relationships = relationships;
    out->relationship_count = source->relationship_count;
    script_catalog_global *globals = arena_alloc(storage, (size_t)source->global_count * sizeof *globals);
    for (int i = 0; i < source->global_count; i++) {
        globals[i] = source->globals[i];
        globals[i].name = text(storage, globals[i].name);
        globals[i].unit = text(storage, globals[i].unit);
    }
    out->globals = globals;
    out->global_count = source->global_count;
    script_catalog_capability *capabilities = arena_alloc(
        storage,
        (size_t)source->capability_count * sizeof *capabilities
    );
    for (int i = 0; i < source->capability_count; i++) {
        capabilities[i] = source->capabilities[i];
        script_catalog_capability *capability = &capabilities[i];
        capability->name = text(storage, capability->name);
        capability->qualified_name = text(storage, capability->qualified_name);
        capability->operator_name = text(storage, capability->operator_name);
        capability->method_name = text(storage, capability->method_name);
        capability->category = text(storage, capability->category);
        capability->keywords = text(storage, capability->keywords);
        capability->description = text(storage, capability->description);
        capability->entries = names(storage, capability->entries, capability->entry_count);
        capability->outcomes = names(storage, capability->outcomes, capability->outcome_count);
        if (!parameters(
                storage,
                capability->parameters,
                capability->parameter_count,
                &capability->parameters
            ) ||
            !parameters(
                storage,
                capability->results,
                capability->result_count,
                &capability->results
            )) {
            return false;
        }
    }
    out->capabilities = capabilities;
    out->capability_count = source->capability_count;
    return true;
}

static bool same_text(const char *a, const char *b)
{
    return a == b || (a && b && !strcmp(a, b));
}
static bool same_type(script_type a, script_type b)
{
    return a.storage == b.storage && a.container == b.container &&
           a.nominal == b.nominal && a.parameter == b.parameter && a.flags == b.flags;
}
static bool same_fields(const script_catalog_field *a, const script_catalog_field *b, int count)
{
    for (int i = 0; i < count; i++) {
        const script_catalog_field *x = &a[i], *y = &b[i];
        if (x->id != y->id || !same_text(x->name, y->name) ||
            !same_type(x->type, y->type) || x->offset != y->offset ||
            x->read != y->read || x->write != y->write ||
            !same_text(x->read_key, y->read_key) || x->writable != y->writable ||
            x->constant != y->constant || !same_text(x->unit, y->unit) ||
            x->range_contract != y->range_contract ||
            memcmp(&x->min, &y->min, sizeof x->min) ||
            memcmp(&x->max, &y->max, sizeof x->max)) {
            return false;
        }
    }
    return true;
}
static bool same_parameters(const script_catalog_parameter *a, const script_catalog_parameter *b, int count)
{
    for (int i = 0; i < count; i++) {
        const script_catalog_parameter *x = &a[i], *y = &b[i];
        if (!same_text(x->name, y->name) || !same_text(x->unit, y->unit) ||
            !same_type(x->type, y->type) || x->reference_kind != y->reference_kind ||
            x->member != y->member || x->has_default != y->has_default ||
            x->default_owner != y->default_owner) {
            return false;
        }
    }
    return true;
}
static bool same_names(const char *const *a, const char *const *b, int count)
{
    for (int i = 0; i < count; i++) {
        if (!same_text(a[i], b[i])) {
            return false;
        }
    }
    return true;
}
bool source_catalog_metadata_equal(const script_catalog *a, const script_catalog *b)
{
    if (a->entity_type != b->entity_type || a->tick_event != b->tick_event ||
        a->type_count != b->type_count || a->global_count != b->global_count ||
        a->capability_count != b->capability_count ||
        a->relationship_count != b->relationship_count) {
        return false;
    }
    for (int i = 0; i < a->type_count; i++) {
        const script_catalog_type *x = &a->types[i], *y = &b->types[i];
        if (x->id != y->id || !same_text(x->name, y->name) ||
            !same_text(x->source_name, y->source_name) || !same_text(x->key, y->key) ||
            !same_type(x->type, y->type) || x->field_count != y->field_count ||
            x->size != y->size || x->layout != y->layout ||
            x->underlying != y->underlying || x->component_id != y->component_id ||
            x->component != y->component || x->derived != y->derived ||
            x->internal != y->internal || x->variant_count != y->variant_count ||
            !same_fields(x->fields, y->fields, x->field_count)) {
            return false;
        }
        for (int j = 0; j < x->variant_count; j++) {
            const script_catalog_variant *v = &x->variants[j], *w = &y->variants[j];
            if (v->tag != w->tag || !same_text(v->name, w->name) ||
                !same_type(v->type, w->type) || v->tagged != w->tagged ||
                v->is_literal != w->is_literal || v->field_count != w->field_count ||
                !same_fields(v->fields, w->fields, v->field_count)) {
                return false;
            }
        }
    }
    for (int i = 0; i < a->relationship_count; i++) {
        const script_catalog_relationship *x = &a->relationships[i],
                                          *y = &b->relationships[i];
        if (x->id != y->id || !same_text(x->name, y->name) ||
            x->exclusive != y->exclusive || x->traversable != y->traversable) {
            return false;
        }
    }
    for (int i = 0; i < a->global_count; i++) {
        const script_catalog_global *x = &a->globals[i], *y = &b->globals[i];
        if (x->id != y->id || !same_text(x->name, y->name) ||
            !same_type(x->type, y->type) || x->writable != y->writable ||
            !same_text(x->unit, y->unit)) {
            return false;
        }
    }
    for (int i = 0; i < a->capability_count; i++) {
        const script_catalog_capability *x = &a->capabilities[i],
                                        *y = &b->capabilities[i];
        if (x->id != y->id || !same_text(x->name, y->name) ||
            !same_text(x->qualified_name, y->qualified_name) ||
            !same_text(x->operator_name, y->operator_name) ||
            !same_text(x->method_name, y->method_name) || x->kind != y->kind ||
            x->state_access != y->state_access || x->state_kind != y->state_kind ||
            x->component_access != y->component_access ||
            x->struct_access != y->struct_access || x->component != y->component ||
            x->component_field != y->component_field || x->aggregate != y->aggregate ||
            x->component_entity != y->component_entity ||
            x->component_value != y->component_value || x->flow.kind != y->flow.kind ||
            memcmp(x->flow.inputs, y->flow.inputs, sizeof x->flow.inputs) ||
            memcmp(x->flow.results, y->flow.results, sizeof x->flow.results) ||
            memcmp(x->flow.outcomes, y->flow.outcomes, sizeof x->flow.outcomes) ||
            x->flow.break_entry != y->flow.break_entry ||
            x->parameter_count != y->parameter_count ||
            x->result_count != y->result_count || x->entry_count != y->entry_count ||
            x->outcome_count != y->outcome_count ||
            x->completion_outcome != y->completion_outcome ||
            x->effects != y->effects || x->operation != y->operation ||
            x->receiver != y->receiver || x->pure != y->pure ||
            x->variadic != y->variadic || x->variadic_optional != y->variadic_optional ||
            x->numeric_aggregate != y->numeric_aggregate ||
            x->event_sender != y->event_sender ||
            !same_parameters(x->parameters, y->parameters, x->parameter_count) ||
            !same_parameters(x->results, y->results, x->result_count) ||
            !same_names(x->entries, y->entries, x->entry_count) ||
            !same_names(x->outcomes, y->outcomes, x->outcome_count)) {
            return false;
        }
    }
    return true;
}

/* Bounded exact comparison evidence, rather than a collision-prone content
 * hash or borrowed type descriptor. This is process-local cache metadata. */
enum
{
    DEFAULT_KEY_BYTES = 1024 * 1024,
    DEFAULT_KEY_DEPTH = 64
};
static bool key_bytes(unsigned char **key, const void *data, size_t size)
{
    size_t before = (size_t)acount(*key);
    if (size > DEFAULT_KEY_BYTES - before) {
        return false;
    }
    asetlen(*key, (int)(before + size));
    if (size) {
        memcpy(*key + before, data, size);
    }
    return true;
}
static bool key_word(unsigned char **key, uint64_t word)
{
    return key_bytes(key, &word, sizeof word);
}
static bool key_text(unsigned char **key, const char *text)
{
    size_t size = text ? strlen(text) : 0;
    return key_word(key, text ? (uint64_t)size : UINT64_MAX) &&
           key_bytes(key, text, size);
}
static bool key_value(unsigned char **key, const pin_value *value, int depth);
static bool key_layout(unsigned char **key, const type_desc *type, int depth)
{
    if (!type || !type->size || type->field_count < 0 || depth >= DEFAULT_KEY_DEPTH ||
        !key_word(key, type->size) || !key_word(key, (uint64_t)type->field_count) ||
        !key_text(key, type->name) || !key_text(key, type->alias_of)) {
        return false;
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        size_t width = 0;
        const type_desc *nested = NULL;
        switch (field->type) {
            case PIN_FLOW: break;
            case PIN_BOOL: width = sizeof(bool); break;
            case PIN_I8:
            case PIN_U8: width = 1; break;
            case PIN_I16:
            case PIN_U16: width = 2; break;
            case PIN_I32:
            case PIN_U32:
            case PIN_F32: width = 4; break;
            case PIN_I64:
            case PIN_U64:
            case PIN_F64:
            case PIN_HANDLE:
            case PIN_TASK: width = 8; break;
            case PIN_NAME:
            case PIN_STR:
            case PIN_UNION: width = sizeof(void *); break;
            case PIN_ENUM:
                if (field->size != 1 && field->size != 2 && field->size != 4) {
                    return false;
                }
                width = field->size;
                break;
            case PIN_STRUCT:
                nested = type_catalog_find(field->type_name);
                if (!nested) {
                    return false;
                }
                width = nested->size;
                break;
            default: return false;
        }
        if (field->size != width || field->offset > type->size ||
            width > type->size - field->offset || !key_word(key, field->type) ||
            !key_text(key, field->type_name) || !key_text(key, field->name) ||
            !key_word(key, field->offset) || !key_word(key, field->size) ||
            !key_word(key, field->range_contract) ||
            !key_bytes(key, &field->min, sizeof field->min) ||
            !key_bytes(key, &field->max, sizeof field->max) ||
            !key_bytes(key, &field->range_default, sizeof field->range_default) ||
            !key_text(key, field->unit) ||
            (nested && !key_layout(key, nested, depth + 1))) {
            return false;
        }
    }
    return true;
}
static bool key_struct(unsigned char **key, const pin_value *value, int depth)
{
    if (!key_word(key, value->ptr != NULL)) {
        return false;
    }
    if (!value->ptr) {
        return true;
    }
    const type_desc *type = type_catalog_find(value->type_name);
    /* Capture every allocated byte, including opaque/partially reflected
     * fields. The descriptor is the one used by this construction's snapshot,
     * never a later descriptor or the independent catalog-declared size.
     * Raw copying handles packed scalar fields without alignment assumptions.
     * Separately snapshotted managed pointers and padding may conservatively
     * miss; their addresses cannot coincide between two live owned snapshots. */
    return key_layout(key, type, depth) && key_bytes(key, value->ptr, type->size);
}
static bool key_value(unsigned char **key, const pin_value *value, int depth)
{
    if (depth >= DEFAULT_KEY_DEPTH || !key_word(key, value->type) ||
        !key_word(key, value->container) || !key_text(key, value->type_name)) {
        return false;
    }
    if (value->container) {
        const pin_array *array = value->ptr;
        if (!key_word(key, array != NULL)) {
            return false;
        }
        if (!array) {
            return true;
        }
        if (!key_word(key, array->element_type) || !key_text(key, array->type_name) ||
            !key_word(key, (uint64_t)array->count)) {
            return false;
        }
        for (int i = 0; i < array->count; i++) {
            if (!key_value(key, &array->items[i], depth + 1)) {
                return false;
            }
        }
        return true;
    }
    switch (value->type) {
        case PIN_FLOW: return true;
        case PIN_BOOL: return key_word(key, value->b);
        case PIN_I8: return key_bytes(key, &value->i8, sizeof value->i8);
        case PIN_U8: return key_bytes(key, &value->u8, sizeof value->u8);
        case PIN_I16: return key_bytes(key, &value->i16, sizeof value->i16);
        case PIN_U16: return key_bytes(key, &value->u16, sizeof value->u16);
        case PIN_I32: return key_bytes(key, &value->i, sizeof value->i);
        case PIN_U32: return key_bytes(key, &value->u, sizeof value->u);
        case PIN_I64: return key_bytes(key, &value->i64, sizeof value->i64);
        case PIN_TASK:
        case PIN_HANDLE:
        case PIN_U64: return key_bytes(key, &value->u64, sizeof value->u64);
        case PIN_ENUM: return key_bytes(key, &value->e, sizeof value->e);
        case PIN_F32: return key_bytes(key, &value->f, sizeof value->f);
        case PIN_F64: return key_bytes(key, &value->d, sizeof value->d);
        case PIN_NAME:
        case PIN_STR: return key_text(key, value->str);
        case PIN_STRUCT: return key_struct(key, value, depth);
        case PIN_UNION: {
            const pin_union *tagged = value->ptr;
            if (!key_word(key, tagged != NULL)) {
                return false;
            }
            if (!tagged) {
                return true;
            }
            if (!key_word(key, tagged->tag) || !key_word(key, (uint64_t)tagged->count)) {
                return false;
            }
            for (int i = 0; i < tagged->count; i++) {
                if (!key_value(key, &tagged->values[i], depth + 1)) {
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}
bool source_catalog_default_key(const script_catalog *catalog, unsigned char **out)
{
    assert(out && !*out);
    for (int i = 0; i < catalog->type_count; i++) {
        const script_catalog_type *type = &catalog->types[i];
        for (int j = 0; j < type->variant_count; j++) {
            if (type->variants[j].is_literal &&
                !key_value(out, &type->variants[j].literal, 0)) {
                goto unavailable;
            }
        }
    }
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *c = &catalog->capabilities[i];
        for (int result = 0; result < 2; result++) {
            const script_catalog_parameter *parameters = result ? c->results
                                                                : c->parameters;
            int count = result ? c->result_count : c->parameter_count;
            for (int j = 0; j < count; j++) {
                if (parameters[j].has_default &&
                    !key_value(out, &parameters[j].default_value, 0)) {
                    goto unavailable;
                }
            }
        }
    }
    return true;
unavailable:
    afree(*out);
    return false;
}
