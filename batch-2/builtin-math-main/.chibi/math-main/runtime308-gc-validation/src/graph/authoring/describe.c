#include "graph/authoring/describe.h"

#include "graph/nodes/component_nodes.h"
#include "graph/nodes/core_nodes.h"

#include <stdio.h>
#include <string.h>

static bool sends(const node_desc *desc)
{
    return !strcmp(desc->name, "Send Event");
}

static const host_event_desc *find_host_event(const char *name)
{
    for (int i = 0; name && i < HOST_EVENT_COUNT; i++) {
        if (!strcmp(HOST_EVENTS[i].name, name)) {
            return &HOST_EVENTS[i];
        }
    }
    return NULL;
}

static const behavior_asset *definition_named(
    const behavior_asset *const *catalog,
    int count,
    const char *name
)
{
    for (int i = 0; name && i < count; i++) {
        if (catalog[i]->domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION &&
            !strcmp(catalog[i]->name, name)) {
            return catalog[i];
        }
    }
    return NULL;
}

node_subject node_subject_of(const node *n, const behavior_asset *const *catalog, int count)
{
    node_subject subject = { .desc = n->desc };
    if (component_node_kind_of(n->desc)) {
        subject.setting = component_node_key(n);
        return subject;
    }
    if (n->desc == &NODE_Event) {
        subject.host = find_host_event(n->reference_name);
    }
    bool event = n->desc == &NODE_Event || n->desc == &NODE_CallEvent ||
                 sends(n->desc);
    if (event && !subject.host) {
        subject.definition = definition_named(catalog, count, n->reference_name);
    }
    return subject;
}

/* The component or struct type a component node's setting names. */
static const type_desc *setting_type(const node_subject *s)
{
    component_node_kind kind = component_node_kind_of(s->desc);
    if (kind == COMPONENT_NODE_MAKE || kind == COMPONENT_NODE_BREAK) {
        const node_desc *shape = component_node_shape(s->desc, s->setting);
        return shape ? shape->type : NULL;
    }
    const ecs_component_desc *component = component_by_key(s->setting);
    return component ? component->type : NULL;
}

bool node_subject_describe(const node_subject *s, char *text, size_t size)
{
    const type_desc *type = component_node_kind_of(s->desc) ? setting_type(s) : NULL;
    if (type) {
        component_node_preset preset = { s->desc, s->setting, s->field, type };
        component_node_preset_describe(&preset, text, size);
        return true;
    }
    if (s->definition && sends(s->desc)) {
        snprintf(text, size, "Sends %s to Entity.", s->definition->name);
        return true;
    }
    const char *about = s->desc->description;
    if (s->host) {
        about = s->host->description;
    } else if (
        s->definition && s->desc == &NODE_Event && s->definition->description &&
        s->definition->description[0]
    ) {
        about = s->definition->description;
    }
    snprintf(text, size, "%s", about ? about : "");
    return text[0] != '\0';
}
