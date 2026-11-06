#include "language/type_constructors.h"

#include "capabilities/catalog_snapshot.h"
#include "support/array.h"
#include "types/union.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char *name(arena *storage, const char *prefix, uint64_t id)
{
    char text[64];
    snprintf(text, sizeof text, "%s:%" PRIu64, prefix, id);
    size_t bytes = strlen(text) + 1;
    char *copy = arena_alloc(storage, bytes);
    memcpy(copy, text, bytes);
    return copy;
}
script_catalog_type script_constructor_collection(
    arena *storage,
    pin_container container,
    script_type key,
    script_type value,
    size_t pointer_size
)
{
    char identity[128];
    snprintf(
        identity,
        sizeof identity,
        "%d:%" PRIu64 ":%" PRIu64,
        container,
        script_type_tag(key),
        script_type_tag(value)
    );
    uint64_t id = script_catalog_id("collection-type", identity);
    script_type type = container == PIN_CONTAINER_MAP
                         ? (script_type){ .storage = PIN_UNION }
                         : value;
    /* The descriptor retains the element's full type. Its optional wrapper is
     * an element value, not an optional collection; boxed elements physically
     * occupy union slots. */
    if (container != PIN_CONTAINER_MAP && (value.flags & SCRIPT_TYPE_BOXED_OPTIONAL)) {
        type = (script_type){ .storage = PIN_UNION,
                              .flags = value.flags & (SCRIPT_TYPE_ERROR | SCRIPT_TYPE_UNTYPED |
                                                      SCRIPT_TYPE_GENERIC) };
    }
    type.flags &= ~SCRIPT_TYPE_OPTIONAL;
    type.container = container;
    type.collection = id;
    char text[64];
    snprintf(text, sizeof text, "__collection_%" PRIu64, id);
    size_t bytes = strlen(text) + 1;
    char *owned = arena_alloc(storage, bytes);
    memcpy(owned, text, bytes);
    return (script_catalog_type){ .id = id,
                                  .name = owned,
                                  .type = type,
                                  .size = pointer_size,
                                  .internal = true,
                                  .container = container,
                                  .key_type = key,
                                  .element_type = value };
}
script_constructor_plan script_constructor_collection_plan(
    arena *storage,
    const script_constructor_facts *facts,
    pin_container container,
    script_type key,
    script_type element,
    size_t pointer_size
)
{
    script_catalog_type definition = script_constructor_collection(storage, container, key, element, pointer_size);
    script_constructor_plan plan = { .type = definition.type };
    if (script_catalog_type_by_id(facts->catalog, definition.id)) {
        return plan;
    }
    plan.publish = true;
    plan.id = definition.id;
    plan.name = definition.name;
    plan.definition_type = definition.type;
    plan.element_type = definition.element_type;
    plan.collection_container = definition.container;
    plan.key_type = definition.key_type;
    plan.size = definition.size;
    plan.internal = true;
    return plan;
}
script_catalog_type script_constructor_pointer(arena *storage, script_type pointee, size_t pointer_size)
{
    char identity[32];
    snprintf(identity, sizeof identity, "%" PRIu64, script_type_tag(pointee));
    uint64_t id = script_catalog_id("pointer-type", identity);
    char text[64];
    snprintf(text, sizeof text, "__pointer_%" PRIu64, id);
    size_t bytes = strlen(text) + 1;
    char *owned = arena_alloc(storage, bytes);
    memcpy(owned, text, bytes);
    return (script_catalog_type){ .id = id,
                                  .name = owned,
                                  .type = { .storage = PIN_POINTER, .nominal = id },
                                  .size = pointer_size,
                                  .internal = true,
                                  .element_type = pointee };
}
script_constructor_plan script_constructor_union(
    arena *storage,
    const script_constructor_facts *facts,
    const script_type *members,
    int count,
    size_t pointer_size
)
{
    script_constructor_variant *variants = NULL;
    for (int i = 0; i < count; i++) {
        const script_catalog_type
            *nested = (members[i].flags & SCRIPT_TYPE_UNION) &&
                              !(members[i].flags & SCRIPT_TYPE_BOXED_OPTIONAL) &&
                              members[i].container == PIN_CONTAINER_SINGLE
                        ? script_catalog_type_by_id(facts->catalog, members[i].nominal)
                        : NULL;
        int n = nested ? nested->variant_count : 1;
        for (int j = 0; j < n; j++) {
            if (nested && members[i].parameter &&
                nested->variants[j].tag != members[i].parameter) {
                continue;
            }
            script_constructor_variant
                selection = nested
                              ? (
                                    script_constructor_variant
                                ){ .catalog_type = (int)(nested - facts->catalog->types),
                                   .variant = j,
                                   .member = -1,
                                   .tag = nested->variants[j].tag,
                                   .type = nested->variants[j].type }
                              : (script_constructor_variant){
                                    .catalog_type = -1,
                                    .variant = -1,
                                    .member = i,
                                    .tag = script_type_tag(members[i]),
                                    .type = members[i]
                                };
            bool exists = false;
            for (int k = 0; k < acount(variants); k++) {
                exists |= variants[k].tag == selection.tag;
            }
            if (!exists) {
                apush(variants, selection);
            }
        }
    }
    for (int i = 1; i < acount(variants); i++) {
        for (int j = i; j > 0 && variants[j - 1].tag > variants[j].tag; j--) {
            script_constructor_variant swap = variants[j];
            variants[j] = variants[j - 1];
            variants[j - 1] = swap;
        }
    }
    uint64_t id = UINT64_C(14695981039346656037);
    for (int i = 0; i < acount(variants); i++) {
        id = (id ^ variants[i].tag) * UINT64_C(1099511628211);
    }
    script_constructor_plan plan = {
        .type = { .storage = PIN_UNION, .nominal = id, .flags = SCRIPT_TYPE_UNION }
    };
    if (!script_catalog_type_by_id(facts->catalog, id)) {
        plan.publish = true;
        plan.id = id;
        plan.name = name(storage, "union", id);
        plan.definition_type = plan.type;
        plan.size = pointer_size;
        plan.internal = true;
        plan.variant_count = acount(variants);
        if (plan.variant_count) {
            script_constructor_variant
                *copy = arena_alloc(storage, (size_t)plan.variant_count * sizeof *copy);
            memcpy(copy, variants, (size_t)plan.variant_count * sizeof *copy);
            plan.variants = copy;
        }
    }
    afree(variants);
    return plan;
}
script_constructor_plan script_constructor_task(
    arena *storage,
    const script_constructor_facts *facts,
    script_type result,
    uint64_t function
)
{
    uint64_t id = script_type_tag(result);
    script_constructor_plan plan = { .type = { .storage = PIN_TASK,
                                               .nominal = function,
                                               .parameter = id,
                                               .flags = SCRIPT_TYPE_TASK } };
    if (script_catalog_type_by_id(facts->catalog, id)) {
        return plan;
    }
    plan.publish = true;
    plan.id = id;
    plan.name = name(storage, "task-result", id);
    plan.definition_type = result;
    plan.internal = true;
    script_constructor_field *fields = NULL;
    if (result.flags & SCRIPT_TYPE_RESULTS) {
        const script_catalog_capability
            *capability = script_catalog_capability_by_id(facts->catalog, function);
        if (capability) {
            for (int i = 0; i < capability->result_count; i++) {
                apush(
                    fields,
                    ((script_constructor_field){
                        .capability = (int)(capability - facts->catalog->capabilities),
                        .parameter = i,
                        .symbol = -1 })
                );
            }
        } else {
            for (int i = 0; i < facts->function_count; i++) {
                const script_function *f = &facts->functions[i];
                if (f->symbol < 0 || facts->symbols[f->symbol].id != function) {
                    continue;
                }
                for (int j = 0; j < acount(f->results); j++) {
                    apush(
                        fields,
                        ((script_constructor_field){ .capability = -1,
                                                     .parameter = -1,
                                                     .symbol = f->results[j] })
                    );
                }
                break;
            }
        }
    }
    plan.field_count = acount(fields);
    if (plan.field_count) {
        script_constructor_field *copy = arena_alloc(storage, (size_t)plan.field_count * sizeof *copy);
        memcpy(copy, fields, (size_t)plan.field_count * sizeof *copy);
        plan.fields = copy;
    }
    afree(fields);
    return plan;
}
script_constructor_plan script_constructor_await(
    arena *storage,
    const script_constructor_facts *facts,
    script_type success,
    script_range range,
    size_t pointer_size
)
{
    const script_catalog_type
        *definition = success.flags & SCRIPT_TYPE_UNION
                        ? script_catalog_type_by_id(facts->catalog, success.nominal)
                        : NULL;
    bool cancelled = success.flags & SCRIPT_TYPE_SINGLETON;
    for (int i = 0; definition && i < definition->variant_count; i++) {
        cancelled |= definition->variants[i].tag == PIN_UNION_CANCELLED_TAG;
    }
    script_type members[] = { success, script_cancelled_type() };
    script_constructor_plan plan = script_constructor_union(storage, facts, members, 2, pointer_size);
    if (cancelled) {
        plan.diagnostic_count = 1;
        plan.diagnostics[0].range = range;
        snprintf(plan.diagnostics[0].message, sizeof plan.diagnostics[0].message, "Cancelled is reserved for task cancellation, not a successful task result");
    }
    return plan;
}
bool script_constructor_materialize(
    arena *storage,
    const script_constructor_facts *facts,
    const script_constructor_plan *plan,
    script_catalog_type *out
)
{
    if (!plan->publish) {
        return false;
    }
    script_catalog_variant *variants = NULL;
    for (int i = 0; i < plan->variant_count; i++) {
        const script_constructor_variant *selection = &plan->variants[i];
        script_catalog_variant variant = selection->catalog_type >= 0
                                           ? facts->catalog
                                                 ->types[selection->catalog_type]
                                                 .variants[selection->variant]
                                           : (script_catalog_variant){
                                                 .tag = selection->tag,
                                                 .type = selection->type
                                             };
        apush(variants, variant);
    }
    script_catalog_field *fields = NULL;
    for (int i = 0; i < plan->field_count; i++) {
        const script_constructor_field *selection = &plan->fields[i];
        script_catalog_field field = { 0 };
        if (selection->capability >= 0) {
            const script_catalog_parameter
                *source = &facts->catalog->capabilities[selection->capability]
                               .results[selection->parameter];
            field.name = source->name;
            field.type = source->type;
        } else {
            const script_symbol *source = &facts->symbols[selection->symbol];
            field.name = source->name;
            field.type = source->type;
        }
        apush(fields, field);
    }
    script_catalog_type type = { .id = plan->id,
                                 .name = plan->name,
                                 .type = plan->definition_type,
                                 .element_type = plan->element_type,
                                 .container = plan->collection_container,
                                 .key_type = plan->key_type,
                                 .size = plan->size,
                                 .internal = plan->internal,
                                 .variants = variants,
                                 .variant_count = acount(variants),
                                 .fields = fields,
                                 .field_count = acount(fields) };
    script_catalog input = { .types = &type, .type_count = 1 }, copied = { 0 };
    bool ok = script_catalog_snapshot(storage, &input, &copied);
    if (ok) {
        *out = copied.types[0];
    }
    afree(variants);
    afree(fields);
    return ok;
}
