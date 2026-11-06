#include "capabilities/catalog_snapshot.h"

#include "types/array.h"

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
        if (source[i].has_default && pin_value_capture_to_arena(
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
    return !pin_value_capture_to_arena(storage, &out->value, &value->value);
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
bool script_catalog_snapshot(arena *storage, const script_catalog *source, script_catalog *out)
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
            if (fields[j].has_default && pin_value_capture_to_arena(
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
                if (payload[k].has_default && pin_value_capture_to_arena(
                                                  storage,
                                                  &payload[k].default_value,
                                                  &variants[j].fields[k].default_value
                                              )) {
                    return false;
                }
            }
            variants[j].fields = payload;
            if (variants[j].is_literal && pin_value_capture_to_arena(
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
