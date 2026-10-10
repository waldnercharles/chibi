#include "authoring/source_catalog_snapshot.h"

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
        }
        types[i].fields = fields;
    }
    out->types = types;
    out->type_count = source->type_count;
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
