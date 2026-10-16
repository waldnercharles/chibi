#include "authoring/source_catalog_snapshot.h"

#include "support/array.h"
#include "types/array.h"
#include "types/record.h"
#include "types/scalar.h"

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
static bool compile_value_copy(arena *storage, script_compile_value *out, const script_compile_value *value)
{
    *out = *value;
    if (value->is_type) {
        out->value = (pin_value){ 0 };
        return true;
    }
    return !pin_value_snapshot_to_arena(storage, &out->value, &value->value);
}
static bool generic_metadata(arena *storage, const script_catalog *source, script_catalog *out)
{
    assert(source->template_count >= 0 && (!source->template_count || source->templates));
    assert(
        source->specialization_count >= 0 &&
        (!source->specialization_count || source->specializations)
    );
    script_catalog_template *templates = arena_alloc(
        storage,
        (size_t)source->template_count * sizeof *templates
    );
    for (int i = 0; i < source->template_count; i++) {
        templates[i] = source->templates[i];
        templates[i].name = text(storage, templates[i].name);
        templates[i].owner = text(storage, templates[i].owner);
        int count = templates[i].parameter_count;
        assert(count >= 0 && (!count || templates[i].parameters));
        script_template_parameter *parameters = arena_alloc(storage, (size_t)count * sizeof *parameters);
        for (int j = 0; j < count; j++) {
            parameters[j] = templates[i].parameters[j];
            parameters[j].name = text(storage, parameters[j].name);
            parameters[j].binder = text(storage, parameters[j].binder);
        }
        templates[i].parameters = parameters;
    }
    out->templates = templates;
    out->template_count = source->template_count;
    script_catalog_specialization *specializations = arena_alloc(
        storage,
        (size_t)source->specialization_count * sizeof *specializations
    );
    for (int i = 0; i < source->specialization_count; i++) {
        specializations[i] = source->specializations[i];
        int count = specializations[i].binding_count;
        assert(count >= 0 && (!count || specializations[i].bindings));
        script_compile_binding *bindings = arena_alloc(storage, (size_t)count * sizeof *bindings);
        for (int j = 0; j < count; j++) {
            const script_compile_binding *source_binding = &specializations[i].bindings[j];
            bindings[j].name = (char *)text(storage, source_binding->name);
            if (!compile_value_copy(storage, &bindings[j].value, &source_binding->value)) {
                return false;
            }
        }
        specializations[i].bindings = bindings;
        if (!compile_value_copy(
                storage,
                &specializations[i].result,
                &source->specializations[i].result
            )) {
            return false;
        }
    }
    out->specializations = specializations;
    out->specialization_count = source->specialization_count;
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
            if (fields[j].has_default && pin_value_snapshot_to_arena(
                                             storage,
                                             &fields[j].default_value,
                                             &types[i].fields[j].default_value
                                         )) {
                return false;
            }
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
    return generic_metadata(storage, source, out);
}

static bool same_text(const char *a, const char *b)
{
    return a == b || (a && b && !strcmp(a, b));
}
static bool same_type(script_type a, script_type b)
{
    return a.storage == b.storage && a.container == b.container &&
           a.nominal == b.nominal && a.parameter == b.parameter &&
           a.collection == b.collection && a.flags == b.flags;
}
static bool same_compile_value(script_compile_value a, script_compile_value b)
{
    if (a.is_type != b.is_type) {
        return false;
    }
    if (a.is_type) {
        return same_type(a.type, b.type);
    }
    if (a.value.type != b.value.type || a.value.container != b.value.container ||
        !same_text(a.value.type_name, b.value.type_name) ||
        !pin_record_same(a.value.record, b.value.record)) {
        return false;
    }
    if (a.value.type == PIN_FLOW && a.value.container == PIN_CONTAINER_SINGLE) {
        return true;
    }
    if (a.value.container == PIN_CONTAINER_SINGLE && a.value.type == PIN_F32) {
        return !memcmp(&a.value.f, &b.value.f, sizeof a.value.f);
    }
    if (a.value.container == PIN_CONTAINER_SINGLE && a.value.type == PIN_F64) {
        return !memcmp(&a.value.d, &b.value.d, sizeof a.value.d);
    }
    if (a.value.container == PIN_CONTAINER_SINGLE &&
        ((a.value.type >= PIN_BOOL && a.value.type <= PIN_F64) ||
         a.value.type == PIN_ENUM || a.value.type == PIN_HANDLE)) {
        return behavior_scalar_bits(a.value) == behavior_scalar_bits(b.value);
    }
    return pin_value_equal(&a.value, &b.value);
}
static bool same_fields(const script_catalog_field *a, const script_catalog_field *b, int count)
{
    for (int i = 0; i < count; i++) {
        const script_catalog_field *x = &a[i], *y = &b[i];
        if (x->id != y->id || !same_text(x->name, y->name) ||
            !same_type(x->type, y->type) || x->offset != y->offset ||
            x->read != y->read || x->write != y->write ||
            !same_text(x->read_key, y->read_key) || x->writable != y->writable ||
            x->constant != y->constant || x->has_default != y->has_default ||
            !same_text(x->unit, y->unit) || x->range_contract != y->range_contract ||
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
        a->relationship_count != b->relationship_count ||
        a->template_count != b->template_count ||
        a->specialization_count != b->specialization_count) {
        return false;
    }
    for (int i = 0; i < a->type_count; i++) {
        const script_catalog_type *x = &a->types[i], *y = &b->types[i];
        if (x->id != y->id || !same_text(x->name, y->name) ||
            !same_text(x->source_name, y->source_name) ||
            !same_text(x->key, y->key) || !same_type(x->type, y->type) ||
            x->field_count != y->field_count || x->size != y->size ||
            x->layout != y->layout || x->underlying != y->underlying ||
            x->component_id != y->component_id || x->component != y->component ||
            x->derived != y->derived || x->internal != y->internal ||
            x->authored_record != y->authored_record ||
            x->container != y->container || !same_type(x->key_type, y->key_type) ||
            !same_type(x->element_type, y->element_type) ||
            x->variant_count != y->variant_count ||
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
            x->effects != y->effects || x->receiver != y->receiver ||
            x->pure != y->pure || x->variadic != y->variadic ||
            x->variadic_optional != y->variadic_optional ||
            x->numeric_aggregate != y->numeric_aggregate ||
            x->event_sender != y->event_sender ||
            !same_parameters(x->parameters, y->parameters, x->parameter_count) ||
            !same_parameters(x->results, y->results, x->result_count) ||
            !same_names(x->entries, y->entries, x->entry_count) ||
            !same_names(x->outcomes, y->outcomes, x->outcome_count)) {
            return false;
        }
    }
    for (int i = 0; i < a->template_count; i++) {
        const script_catalog_template *x = &a->templates[i], *y = &b->templates[i];
        if (x->id != y->id || x->source_generation != y->source_generation ||
            !same_text(x->name, y->name) || !same_text(x->owner, y->owner) ||
            x->kind != y->kind || x->parameter_count != y->parameter_count ||
            x->start != y->start || x->end != y->end || x->generic != y->generic) {
            return false;
        }
        for (int j = 0; j < x->parameter_count; j++) {
            const script_template_parameter *p = &x->parameters[j],
                                            *q = &y->parameters[j];
            if (!same_text(p->name, q->name) || !same_text(p->binder, q->binder) ||
                !same_type(p->type, q->type) || p->comptime != q->comptime ||
                p->type_value != q->type_value || p->binds_type != q->binds_type) {
                return false;
            }
        }
    }
    for (int i = 0; i < a->specialization_count; i++) {
        const script_catalog_specialization *x = &a->specializations[i],
                                            *y = &b->specializations[i];
        if (x->id != y->id || x->template_id != y->template_id ||
            x->binding_count != y->binding_count || x->executable != y->executable ||
            !same_compile_value(x->result, y->result)) {
            return false;
        }
        for (int j = 0; j < x->binding_count; j++) {
            if (!same_text(x->bindings[j].name, y->bindings[j].name) ||
                !same_compile_value(x->bindings[j].value, y->bindings[j].value)) {
                return false;
            }
        }
    }
    return true;
}

bool source_catalog_default_key(const script_catalog *catalog, unsigned char **out)
{
    return script_catalog_values_key(catalog, out);
}
