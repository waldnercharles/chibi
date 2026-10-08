#include "authoring/graph_catalog.h"

#include "authoring/presentation_schema.h"
#include "graph/document/events.h"
#include "graph/modeling/model_compile.h"
#include "graph/nodes/component_nodes.h"
#include "graph/nodes/core_nodes.h"
#include "graph/nodes/field_nodes.h"
#include "graph/shader/shader_graph.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct authoring_graph_catalog
{
    native_capabilities *native;
    graph storage;
    registry_pack types, declarations;
    const registry_pack **packs;
    node_desc **nodes;
};
static const registry_pack *const BASE[] = {
    &SHADER_PACK,        &MODEL_PACK,        &FIELD_NODE_PACK,
    &CORE_EVENT_PACK,    &CORE_EXTRA_PACK,   &CORE_FLOW_PACK,
    &CORE_ARRAY_PACK,    &CORE_STATE_PACK,   &CORE_SWITCH_PACK,
    &CORE_REROUTE_PACK,  &CORE_SET_MAP_PACK, &CORE_STRUCT_PACK,
    &CORE_COMPONENT_PACK
};
static const node_desc *existing(const authoring_graph_catalog *c, const char *name)
{
    for (int i = 0; i < acount(c->packs); i++) {
        const registry_pack *pack = c->packs[i];
        for (int j = 0; j < pack->node_count; j++) {
            if (!strcmp(pack->nodes[j]->name, name)) {
                return pack->nodes[j];
            }
        }
    }
    return NULL;
}
static pin_spec flow(const char *name)
{
    return (pin_spec){ .name = name, .type = PIN_FLOW, .kind = PIN_EXEC };
}
static pin_spec parameter(
    authoring_graph_catalog *c,
    const script_catalog *catalog,
    const script_catalog_parameter *p,
    uint64_t **groups
)
{
    pin_spec result = authoring_presentation_pin(&c->storage, catalog, p->name, p->type);
    result.reference_kind = (pin_reference_kind)p->reference_kind;
    if (p->has_default) {
        result.default_value = p->default_value;
    } else {
        result.default_value = (pin_value){ .type = p->type.storage,
                                            .container = p->type.container };
    }
    if (p->default_owner) {
        result.flags |= PIN_FLAG_DEFAULT_SELF;
    }
    if (p->type.parameter) {
        int i = 0;
        while (i < acount(*groups) && (*groups)[i] != p->type.parameter) {
            i++;
        }
        if (i == acount(*groups)) {
            apush(*groups, p->type.parameter);
        }
        result.group = i + 1;
    }
    return result;
}
static const ecs_component_desc *component(ecs_component_catalog components, uint64_t id)
{
    for (int i = 0; i < components.component_count; i++) {
        if (components.components[i]->id == id) {
            return components.components[i];
        }
    }
    return NULL;
}
static void component_metadata(
    node_desc *d,
    const script_catalog *catalog,
    const script_catalog_capability *cap,
    ecs_component_catalog components
)
{
    if (!cap->component) {
        return;
    }
    const script_catalog_type *type = script_catalog_type_by_id(catalog, cap->component);
    if (!type) {
        return;
    }
    d->component = component(components, type->component_id);
    if (!d->component) {
        return;
    }
    d->type = d->component->type;
    bool self = cap->component_entity < 0;
    switch (cap->component_access) {
        case SCRIPT_COMPONENT_ACCESS_READ:
            d->component_kind = self ? NODE_COMPONENT_GET_SELF
                                     : NODE_COMPONENT_GET_ENTITY;
            break;
        case SCRIPT_COMPONENT_ACCESS_HAS:
            d->component_kind = self ? NODE_COMPONENT_HAS_SELF
                                     : NODE_COMPONENT_HAS_ENTITY;
            break;
        case SCRIPT_COMPONENT_ACCESS_WRITE:
            d->component_kind = cap->component_field ? NODE_COMPONENT_SET_FIELD
                              : self ? NODE_COMPONENT_SET_SELF
                                     : NODE_COMPONENT_SET_ENTITY;
            break;
        default: break;
    }
    for (int i = 0; cap->component_field && i < type->field_count; i++) {
        if (type->fields[i].id == cap->component_field) {
            d->component_field = i;
            return;
        }
    }
}
static void declaration(
    authoring_graph_catalog *c,
    const script_catalog *catalog,
    const script_catalog_capability *cap,
    ecs_component_catalog components
)
{
    const char *name = cap->qualified_name ? cap->qualified_name : cap->name;
    if (existing(c, name)) {
        return;
    }
    pin_spec *inputs = NULL, *outputs = NULL;
    uint64_t *groups = NULL;
    for (int i = 0; i < cap->entry_count; i++) {
        apush(inputs, flow(cap->entries[i]));
    }
    if (cap->kind == SCRIPT_CAPABILITY_ACTION && !cap->entry_count) {
        apush(inputs, flow("Exec"));
    }
    for (int i = 0; i < cap->parameter_count; i++) {
        pin_spec pin = parameter(c, catalog, &cap->parameters[i], &groups);
        if (cap->kind == SCRIPT_CAPABILITY_EVENT) {
            apush(outputs, pin);
        } else {
            apush(inputs, pin);
        }
    }
    /* Events expose their dispatch execution port before the typed payload. */
    if (cap->kind == SCRIPT_CAPABILITY_EVENT) {
        pin_spec *payload = outputs;
        outputs = NULL;
        apush(outputs, flow("Exec"));
        for (int i = 0; i < acount(payload); i++) {
            apush(outputs, payload[i]);
        }
        afree(payload);
    }
    for (int i = 0; i < cap->outcome_count; i++) {
        apush(outputs, flow(cap->outcomes[i]));
    }
    for (int i = 0; i < cap->result_count; i++) {
        apush(outputs, parameter(c, catalog, &cap->results[i], &groups));
    }
    node_display_class display = cap->kind == SCRIPT_CAPABILITY_EVENT ? NODE_DISPLAY_EVENT
                               : cap->kind == SCRIPT_CAPABILITY_ACTION
                                   ? NODE_DISPLAY_FLOW
                                   : NODE_DISPLAY_PURE;
    node_desc *d = authoring_presentation_descriptor(
        &c->storage,
        name,
        display,
        cap->kind == SCRIPT_CAPABILITY_EVENT ? "Events" : "Native",
        inputs,
        acount(inputs),
        outputs,
        acount(outputs)
    );
    if (cap->category) {
        d->category = cap->category;
    }
    d->keywords = cap->keywords;
    d->description = cap->description;
    d->source_name = graph_own_str(&c->storage, cap->name, strlen(cap->name));
    d->method_name = cap->method_name;
    d->operator_name = cap->operator_name;
    d->numeric_aggregate = cap->numeric_aggregate;
    d->source_state_access = (node_source_state_access)cap->state_access;
    d->source_component_access = (node_source_component_access)cap->component_access;
    d->source_component_entity = cap->component_entity;
    d->source_component_value = cap->component_value;
    d->source_event_sender = cap->event_sender;
    d->source_variadic_optional = cap->variadic_optional;
    d->source_flow = (node_source_flow){
        .kind = (node_source_flow_kind)cap->flow.kind,
        .inputs = { cap->flow.inputs[0], cap->flow.inputs[1] },
        .results = { cap->flow.results[0], cap->flow.results[1] },
        .outcomes = { cap->flow.outcomes[0], cap->flow.outcomes[1] },
        .break_entry = cap->flow.break_entry
    };
    d->flow_kind = cap->kind == SCRIPT_CAPABILITY_EVENT  ? NODE_FLOW_EVENT
                 : cap->kind == SCRIPT_CAPABILITY_ACTION ? NODE_FLOW_CUSTOM
                                                         : NODE_FLOW_NONE;
    d->latent = (cap->effects & SCRIPT_EFFECT_SUSPEND) != 0;
    if (cap->struct_access) {
        d->pure_kind = cap->struct_access == SCRIPT_STRUCT_ACCESS_MAKE
                         ? NODE_PURE_MAKE
                         : NODE_PURE_BREAK;
        d->type = native_capabilities_layout(c->native, cap->aggregate);
    }
    component_metadata(d, catalog, cap, components);
    const native_capability_binding *binding = native_capabilities_find(c->native, cap->id, 0);
    if (binding) {
        d->execution_call = binding->call;
        d->host_call = cap->kind == SCRIPT_CAPABILITY_ACTION ? binding->call : NULL;
        d->command = binding->command;
        d->access = binding->access;
        d->query = binding->query;
        d->query_service = binding->query_service;
        d->execution_intrinsic = binding->intrinsic;
        d->instance_state = binding->instance_state;
    }
    apush(c->nodes, d);
    afree(inputs);
    afree(outputs);
    afree(groups);
}
authoring_graph_catalog *authoring_graph_catalog_new(
    authoring_graph_catalog_config config,
    char *error,
    size_t size
)
{
    if (!config.native || config.extra_count < 0 || config.extra_count > 64 ||
        (config.extra_count && !config.extra_packs)) {
        if (size) {
            snprintf(error, size, "Invalid graph catalog composition");
        }
        return NULL;
    }
    authoring_graph_catalog *c = xcalloc(1, sizeof *c);
    graph_init(&c->storage);
    c->native = native_capabilities_retain(config.native);
    c->types = (registry_pack){ .name = "native.types",
                                .types = config.types.types,
                                .type_count = config.types.type_count,
                                .enums = config.types.enums,
                                .enum_count = config.types.enum_count };
    apush(c->packs, &c->types);
    for (int i = 0; i < (int)(sizeof BASE / sizeof *BASE); i++) {
        apush(c->packs, BASE[i]);
    }
    for (int i = 0; i < config.extra_count; i++) {
        assert(config.extra_packs[i]);
        apush(c->packs, config.extra_packs[i]);
    }
    const script_catalog *catalog = native_capabilities_catalog(c->native);
    for (int i = 0; i < catalog->capability_count; i++) {
        declaration(c, catalog, &catalog->capabilities[i], config.components);
    }
    c->declarations = (registry_pack){ .name = "native.declarations",
                                       .nodes = c->nodes,
                                       .node_count = acount(c->nodes) };
    apush(c->packs, &c->declarations);
    return c;
}
const registry_pack *const *authoring_graph_catalog_packs(const authoring_graph_catalog *c, int *count)
{
    assert(c && count);
    *count = acount(c->packs);
    return c->packs;
}
int authoring_graph_catalog_type_pack_count(const authoring_graph_catalog *c)
{
    assert(c);
    return acount(c->packs);
}
type_catalog_pack authoring_graph_catalog_type_pack_at(const authoring_graph_catalog *c, int index)
{
    assert(c && index >= 0 && index < acount(c->packs));
    const registry_pack *p = c->packs[index];
    return (type_catalog_pack){ p->types, p->type_count, p->enums, p->enum_count };
}
void authoring_graph_catalog_free(authoring_graph_catalog *c)
{
    if (!c) {
        return;
    }
    graph_free(&c->storage);
    afree(c->nodes);
    afree(c->packs);
    native_capabilities_free(c->native);
    free(c);
}
