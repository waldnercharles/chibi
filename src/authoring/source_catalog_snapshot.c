#include "authoring/source_catalog_snapshot.h"

#include "support/array.h"
#include "types/array.h"
#include "types/record.h"
#include "types/scalar.h"

#include <assert.h>
#include <string.h>

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
            x->constant != y->constant || x->c_string != y->c_string ||
            x->has_default != y->has_default || !same_text(x->unit, y->unit) ||
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
            !same_type(x->type, y->type) ||
            x->reference_kind != y->reference_kind || x->member != y->member ||
            x->c_string != y->c_string || x->has_default != y->has_default ||
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
            x->effects != y->effects || x->operation != y->operation ||
            x->receiver != y->receiver || x->pure != y->pure ||
            x->fallible != y->fallible || x->language_values != y->language_values ||
            x->owned_result != y->owned_result || x->variadic != y->variadic ||
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
