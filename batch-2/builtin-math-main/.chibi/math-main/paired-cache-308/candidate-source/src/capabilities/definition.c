#include "capabilities/definition.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFINITION_MAX_RECORDS 65536
#define DEFINITION_MAX_PORTS 4096

typedef struct
{
    script_catalog_parameter *inputs, *outputs;
    const char **entries, **outcomes;
    int *result_pins;
} definition_arrays;
struct native_definition_catalog
{
    script_catalog view;
    script_catalog_capability *capabilities;
    definition_arrays *arrays;
    script_catalog_global *globals;
};
static bool fail(char *error, size_t size, const char *name, const char *message)
{
    if (size) {
        snprintf(error, size, "%s: %s", name ? name : "Native definition", message);
    }
    return false;
}
static bool port(
    const native_capability_definition *definition,
    const native_parameter_definition *source,
    script_catalog_parameter *result,
    char *error,
    size_t size
)
{
    if (!source->name || !*source->name || source->kind < NATIVE_PARAMETER_DATA ||
        source->kind > NATIVE_PARAMETER_EXEC || source->generic_group < 0 ||
        source->storage < PIN_FLOW || source->storage > PIN_ENUM ||
        source->container < PIN_CONTAINER_SINGLE ||
        source->container > PIN_CONTAINER_VIEW ||
        (source->container == PIN_CONTAINER_VIEW && source->default_value.ptr) ||
        source->reference_kind < SCRIPT_REFERENCE_NONE ||
        source->reference_kind > SCRIPT_REFERENCE_RELATIONSHIP) {
        return fail(error, size, definition->canonical_name, "Invalid port declaration");
    }
    if ((source->c_string && ((source->storage != PIN_STR && source->storage != PIN_NAME) ||
                              source->container != PIN_CONTAINER_SINGLE)) ||
        (source->type_name && !*source->type_name) ||
        (source->member_name && !*source->member_name) ||
        (source->type_flags & (SCRIPT_TYPE_ERROR | SCRIPT_TYPE_RESULTS)) ||
        ((source->type_flags & SCRIPT_TYPE_GENERIC) && !source->generic_group) ||
        (source->kind == NATIVE_PARAMETER_EXEC &&
         (source->storage != PIN_FLOW || source->container != PIN_CONTAINER_SINGLE ||
          source->generic_group || source->type_flags || source->member_name ||
          source->reference_kind != SCRIPT_REFERENCE_NONE || source->default_owner))) {
        return fail(error, size, definition->canonical_name, "Invalid port type semantics");
    }
    *result = (script_catalog_parameter){
        .name = source->name,
        .unit = source->unit,
        .type = { .storage = source->storage,
                  .container = source->container,
                  .flags = source->type_flags },
        .reference_kind = source->reference_kind,
        .member = source->member_name ? script_catalog_id("field", source->member_name)
                                      : 0,
        .c_string = source->c_string,
        .has_default = source->has_default,
        .default_owner = source->default_owner,
        .default_value = source->default_value,
    };
    if (source->type_name && (source->storage == PIN_STRUCT || source->storage == PIN_HANDLE ||
                              source->storage == PIN_ENUM)) {
        result->type.nominal = script_catalog_id("type", source->type_name);
    }
    if (source->reference_kind != SCRIPT_REFERENCE_NONE) {
        script_type reference = script_reference_type(source->reference_kind);
        if (source->storage != reference.storage ||
            source->container != PIN_CONTAINER_SINGLE) {
            return fail(error, size, definition->canonical_name, "Symbol reference has incompatible native storage");
        }
        result->type.nominal = reference.nominal;
        result->has_default = source->has_default &&
                              (reference.storage == PIN_U64
                                   ? source->default_value.u64 != 0
                                   : source->default_value.str &&
                                         *source->default_value.str);
    }
    if (source->generic_group) {
        size_t bytes = strlen(definition->canonical_name) + 32;
        char *name = xmalloc(bytes);
        snprintf(name, bytes, "%s.%d", definition->canonical_name, source->generic_group);
        result->type.parameter = script_catalog_id("native-type-parameter", name);
        result->type.flags |= SCRIPT_TYPE_GENERIC | SCRIPT_TYPE_UNTYPED;
        free(name);
    }
    result->default_value.type = source->storage;
    result->default_value.container = source->container;
    result->default_value.type_name = source->type_name;
    if (source->unit && (!*source->unit || !script_type_numeric(result->type))) {
        return fail(error, size, definition->canonical_name, "A measured port requires a numeric type and a nonempty unit");
    }
    if (source->default_owner && source->storage != PIN_HANDLE) {
        return fail(error, size, definition->canonical_name, "Owner default requires a handle port");
    }
    return true;
}
static bool ports(
    const native_capability_definition *definition,
    const native_parameter_definition *source,
    int count,
    script_catalog_parameter **data,
    const char ***flow,
    int **pins,
    char *error,
    size_t size
)
{
    for (int i = 0; i < count; ++i) {
        script_catalog_parameter parameter;
        if (!port(definition, &source[i], &parameter, error, size)) {
            return false;
        }
        for (int j = 0; j < i; ++j) {
            if (source[j].kind == source[i].kind &&
                !strcmp(source[j].name, source[i].name)) {
                return fail(error, size, definition->canonical_name, "Duplicate port name");
            }
        }
        if (source[i].kind == NATIVE_PARAMETER_EXEC) {
            apush(*flow, source[i].name);
            continue;
        }
        apush(*data, parameter);
        if (pins) {
            apush(*pins, i);
        }
    }
    return true;
}
static uint64_t type_id(const char *name)
{
    return name ? script_catalog_id("type", name) : 0;
}
static bool ordinal(int value, int count)
{
    return value >= 0 && value < count;
}
static bool flow_valid(const script_catalog_capability *c)
{
    const script_catalog_flow *f = &c->flow;
    if (f->kind == SCRIPT_FLOW_ACCESS_NONE) {
        return true;
    }
    if (f->kind < SCRIPT_FLOW_ACCESS_BRANCH || f->kind > SCRIPT_FLOW_ACCESS_BREAK) {
        return false;
    }
    for (int i = 0; i < 2; ++i) {
        if ((f->inputs[i] != -1 && !ordinal(f->inputs[i], c->parameter_count)) ||
            (f->results[i] != -1 && !ordinal(f->results[i], c->result_count)) ||
            (f->outcomes[i] != -1 && !ordinal(f->outcomes[i], c->outcome_count))) {
            return false;
        }
    }
    if (f->break_entry != -1 && !ordinal(f->break_entry, c->entry_count)) {
        return false;
    }
    if (f->kind == SCRIPT_FLOW_ACCESS_BREAK || f->kind == SCRIPT_FLOW_ACCESS_SEQUENCE) {
        return true;
    }
    if (!ordinal(f->inputs[0], c->parameter_count) ||
        !ordinal(f->outcomes[0], c->outcome_count) ||
        !ordinal(f->outcomes[1], c->outcome_count)) {
        return false;
    }
    if (f->kind == SCRIPT_FLOW_ACCESS_RANGE) {
        return ordinal(f->inputs[1], c->parameter_count) &&
               ordinal(f->results[0], c->result_count);
    }
    if (f->kind == SCRIPT_FLOW_ACCESS_EACH) {
        return ordinal(f->results[0], c->result_count) &&
               ordinal(f->results[1], c->result_count);
    }
    return true;
}
uint64_t native_capability_definition_id(const native_capability_definition *definition)
{
    assert(definition && definition->canonical_name);
    const char *domain = definition->kind == SCRIPT_CAPABILITY_EVENT ? "event"
                       : definition->kind == SCRIPT_CAPABILITY_PROTOTYPE
                           ? "prototype"
                           : "capability";
    return script_catalog_id(domain, definition->canonical_name);
}
static bool state_ports_valid(const script_catalog_capability *c)
{
    if (!c->state_kind) {
        return true;
    }
    int entries = c->state_kind == NATIVE_STATE_GATE ? 4
                : c->state_kind == NATIVE_STATE_ONCE ||
                          c->state_kind == NATIVE_STATE_COUNT ||
                          c->state_kind == NATIVE_STATE_MULTI
                    ? 2
                    : 1;
    int inputs = c->state_kind == NATIVE_STATE_FLIP  ? 0
               : c->state_kind == NATIVE_STATE_MULTI ? 3
                                                     : 1;
    int results = c->state_kind == NATIVE_STATE_COUNT || c->state_kind == NATIVE_STATE_FLIP
                    ? 1
                    : 0;
    int outcomes = c->state_kind == NATIVE_STATE_FLIP || c->state_kind == NATIVE_STATE_MULTI
                     ? 2
                     : 1;
    if (c->entry_count != entries || c->parameter_count != inputs ||
        c->result_count != results || c->outcome_count != outcomes || c->pure ||
        c->variadic) {
        return false;
    }
    for (int i = 0; i < inputs; i++) {
        pin_type type = c->state_kind == NATIVE_STATE_RETRIGGER ? PIN_F64
                      : c->state_kind == NATIVE_STATE_COUNT ||
                                (c->state_kind == NATIVE_STATE_MULTI && i == 2)
                          ? PIN_I32
                          : PIN_BOOL;
        if (c->parameters[i].type.storage != type ||
            c->parameters[i].type.container != PIN_CONTAINER_SINGLE) {
            return false;
        }
    }
    return !results ||
           (c->results[0].type.container == PIN_CONTAINER_SINGLE &&
            c->results[0].type.storage ==
                (c->state_kind == NATIVE_STATE_COUNT ? PIN_I32 : PIN_BOOL));
}

static bool add_definition(
    native_definition_catalog *out,
    const native_capability_definition *source,
    char *error,
    size_t size
)
{
    if (!source->canonical_name || !*source->canonical_name ||
        source->kind < SCRIPT_CAPABILITY_FUNCTION ||
        source->kind > SCRIPT_CAPABILITY_PROTOTYPE || source->input_count < 0 ||
        source->input_count > DEFINITION_MAX_PORTS || source->output_count < 0 ||
        source->output_count > DEFINITION_MAX_PORTS ||
        (source->input_count && !source->inputs) ||
        (source->output_count && !source->outputs) || source->completion_output < -1 ||
        source->completion_output >= source->output_count ||
        (source->variadic_optional && !source->variadic) ||
        source->state_kind < NATIVE_STATE_NONE ||
        source->state_kind > NATIVE_STATE_RETRIGGER ||
        (source->state_kind && source->kind != SCRIPT_CAPABILITY_ACTION)) {
        return fail(error, size, source->canonical_name, "Invalid capability declaration");
    }
    uint64_t id = native_capability_definition_id(source);
    for (int i = 0; i < acount(out->capabilities); ++i) {
        if (out->capabilities[i].id == id) {
            return fail(error, size, source->canonical_name, "Duplicate capability identity");
        }
    }
    definition_arrays arrays = { 0 };
    apush(out->arrays, arrays);
    definition_arrays *a = &out->arrays[acount(out->arrays) - 1];
    if (!ports(source, source->inputs, source->input_count, &a->inputs, &a->entries, NULL, error, size) ||
        !ports(source, source->outputs, source->output_count, &a->outputs, &a->outcomes, &a->result_pins, error, size)) {
        return false;
    }
    int completion = -1;
    for (int i = 0, execution = 0; i < source->output_count; ++i) {
        if (i == source->completion_output) {
            if (source->outputs[i].kind != NATIVE_PARAMETER_EXEC) {
                return fail(error, size, source->canonical_name, "Completion output must be EXEC");
            }
            completion = execution;
        }
        execution += source->outputs[i].kind == NATIVE_PARAMETER_EXEC;
    }
    script_catalog_capability c = {
        .id = id,
        .name = source->source_name ? source->source_name : source->canonical_name,
        .qualified_name = source->canonical_name,
        .operator_name = source->operator_name,
        .method_name = source->method_name,
        .category = source->category,
        .keywords = source->keywords,
        .description = source->description,
        .kind = source->kind,
        .state_access = source->state_access,
        .state_kind = source->state_kind,
        .component_access = source->component_access,
        .struct_access = source->struct_access,
        .component = type_id(source->component_type),
        .aggregate = type_id(source->aggregate_type),
        .component_field = source->component_field
                             ? script_catalog_id("field", source->component_field)
                             : 0,
        .component_entity = source->component_entity,
        .component_value = source->component_value,
        .flow = source->flow,
        .parameters = a->inputs,
        .results = a->outputs,
        .parameter_count = acount(a->inputs),
        .result_count = acount(a->outputs),
        .entries = a->entries,
        .outcomes = a->outcomes,
        .entry_count = acount(a->entries),
        .outcome_count = acount(a->outcomes),
        .completion_outcome = completion,
        .effects = source->effects,
        .operation = source->operation,
        .receiver = type_id(source->receiver_type),
        .pure = source->pure,
        .variadic = source->variadic,
        .variadic_optional = source->variadic_optional,
        .language_values = source->language_values,
        .owned_result = source->owned_result,
        .numeric_aggregate = source->numeric_aggregate,
        .event_sender = source->event_sender
    };
    switch (c.state_access) {
        case SCRIPT_STATE_ACCESS_NONE: break;
        case SCRIPT_STATE_ACCESS_READ: c.effects |= SCRIPT_EFFECT_READ; break;
        case SCRIPT_STATE_ACCESS_WRITE: c.effects |= SCRIPT_EFFECT_WRITE; break;
        default:
            return fail(error, size, source->canonical_name, "Invalid state access");
    }
    switch (c.component_access) {
        case SCRIPT_COMPONENT_ACCESS_NONE: break;
        case SCRIPT_COMPONENT_ACCESS_READ:
        case SCRIPT_COMPONENT_ACCESS_HAS:
            c.effects |= SCRIPT_EFFECT_READ;
            break;
        case SCRIPT_COMPONENT_ACCESS_WRITE:
            c.effects |= SCRIPT_EFFECT_WRITE;
            break;
        case SCRIPT_COMPONENT_ACCESS_ADD:
        case SCRIPT_COMPONENT_ACCESS_REMOVE:
            c.effects |= SCRIPT_EFFECT_WRITE | SCRIPT_EFFECT_STRUCTURAL;
            break;
        default:
            return fail(error, size, source->canonical_name, "Invalid component access");
    }
    if (c.struct_access < SCRIPT_STRUCT_ACCESS_NONE ||
        c.struct_access > SCRIPT_STRUCT_ACCESS_BREAK) {
        return fail(error, size, source->canonical_name, "Invalid struct access");
    }
    if (!state_ports_valid(&c)) {
        return fail(error, size, source->canonical_name, "Invalid state machine port contract");
    }
    if (!flow_valid(&c)) {
        return fail(error, size, source->canonical_name, "Invalid flow port ordinal");
    }
    if (c.component_access != SCRIPT_COMPONENT_ACCESS_NONE &&
        ((c.component_entity != -1 && !ordinal(c.component_entity, c.parameter_count)) ||
         (c.component_value != -1 && !ordinal(c.component_value, c.parameter_count)))) {
        return fail(error, size, source->canonical_name, "Invalid component port ordinal");
    }
    if (!script_catalog_operation_valid(&c)) {
        return fail(error, size, source->canonical_name, "Invalid engine operation declaration");
    }
    apush(out->capabilities, c);
    return true;
}
uint64_t native_global_definition_id(const native_global_definition *definition)
{
    assert(definition && definition->canonical_name);
    return script_catalog_id(
        definition->identity == NATIVE_GLOBAL_IDENTITY_CAPABILITY ? "capability" : "global",
        definition->canonical_name
    );
}
static bool add_global(
    native_definition_catalog *out,
    const native_global_definition *source,
    char *error,
    size_t size
)
{
    if (!source->canonical_name || !*source->canonical_name || !source->type ||
        source->type->kind != NATIVE_PARAMETER_DATA || source->type->generic_group ||
        source->identity < NATIVE_GLOBAL_IDENTITY_GLOBAL ||
        source->identity > NATIVE_GLOBAL_IDENTITY_CAPABILITY) {
        return fail(error, size, source->canonical_name, "Invalid global definition");
    }
    native_capability_definition definition = { .canonical_name = source->canonical_name };
    script_catalog_parameter type;
    if (!port(&definition, source->type, &type, error, size) ||
        (type.type.flags & SCRIPT_TYPE_UNTYPED)) {
        return fail(error, size, source->canonical_name, "Global needs a resolved value type");
    }
    uint64_t id = native_global_definition_id(source);
    for (int i = 0; i < acount(out->globals); ++i) {
        if (out->globals[i].id == id) {
            return fail(error, size, source->canonical_name, "Duplicate global identity");
        }
    }
    apush(
        out->globals,
        ((script_catalog_global){ .id = id,
                                  .name = source->source_name ? source->source_name
                                                              : source->canonical_name,
                                  .type = type.type,
                                  .writable = source->writable,
                                  .unit = source->unit })
    );
    return true;
}
native_definition_catalog *native_definition_catalog_new(
    const script_catalog *base,
    const native_capability_definition *definitions,
    int count,
    const native_global_definition *globals,
    int global_count,
    char *error,
    size_t size
)
{
    assert(base && (!size || error));
    if (size) {
        error[0] = 0;
    }
    if (count < 0 || count > DEFINITION_MAX_RECORDS || (count && !definitions) ||
        base->capability_count || base->global_count || global_count < 0 ||
        global_count > DEFINITION_MAX_RECORDS || (global_count && !globals)) {
        fail(error, size, NULL, "Invalid definition pack");
        return NULL;
    }
    native_definition_catalog *out = xcalloc(1, sizeof *out);
    out->view = *base;
    for (int i = 0; i < count; ++i) {
        if (!add_definition(out, &definitions[i], error, size)) {
            native_definition_catalog_free(out);
            return NULL;
        }
    }
    for (int i = 0; i < global_count; ++i) {
        if (!add_global(out, &globals[i], error, size)) {
            native_definition_catalog_free(out);
            return NULL;
        }
    }
    out->view.globals = out->globals;
    out->view.global_count = acount(out->globals);
    out->view.capabilities = out->capabilities;
    out->view.capability_count = acount(out->capabilities);
    return out;
}
void native_definition_catalog_free(native_definition_catalog *c)
{
    if (!c) {
        return;
    }
    for (int i = 0; i < acount(c->arrays); ++i) {
        definition_arrays *a = &c->arrays[i];
        afree(a->inputs);
        afree(a->outputs);
        afree(a->entries);
        afree(a->outcomes);
        afree(a->result_pins);
    }
    afree(c->globals);
    afree(c->arrays);
    afree(c->capabilities);
    free(c);
}
const script_catalog *native_definition_catalog_view(const native_definition_catalog *c)
{
    assert(c);
    return &c->view;
}
const int *native_definition_result_pins(const native_definition_catalog *c, uint64_t id, int *count)
{
    assert(c && count);
    for (int i = 0; i < acount(c->capabilities); ++i) {
        if (c->capabilities[i].id == id) {
            *count = c->capabilities[i].result_count;
            return c->arrays[i].result_pins;
        }
    }
    *count = -1;
    return NULL;
}
