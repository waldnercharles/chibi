#include "assets/code_source_context.h"

#include "graph/model/registry.h"
#include "graph/nodes/component_nodes.h"

#include <stdio.h>
#include <string.h>

static void diagnostic(char *error, size_t size, const char *format, const char *name)
{
    if (size) {
        snprintf(error, size, format, name ? name : "");
    }
}

const graph *code_source_context_graph(
    const code_source_context *context,
    const behavior_asset *local,
    const char *asset,
    const char *name,
    graph_kind kind,
    const char **resolved_asset,
    char *error,
    size_t size
)
{
    if (resolved_asset) {
        *resolved_asset = NULL;
    }
    if (size) {
        error[0] = 0;
    }
    if (!name || !name[0] || (kind != GRAPH_FUNCTION && kind != GRAPH_MACRO)) {
        diagnostic(error, size, "Invalid callable reference '%s'", name);
        return NULL;
    }
    bool qualified = asset && asset[0];
    if (local && (!qualified || !strcmp(local->name, asset))) {
        const graph *found = behavior_asset_find_graph_by_name(local, name);
        if (found) {
            if (found->kind != kind) {
                diagnostic(error, size, "Callable declaration kind differs for '%s'", name);
                return NULL;
            }
            return found;
        }
    }
    const graph *found = NULL;
    const behavior_asset *owner = NULL;
    int assets = 0;
    for (int i = 0; context && i < context->library_count; i++) {
        const behavior_asset *library = context->libraries[i];
        if (!library || (qualified && strcmp(library->name, asset))) {
            continue;
        }
        assets++;
        if (qualified && assets > 1) {
            diagnostic(error, size, "Ambiguous library asset '%s'", asset);
            return NULL;
        }
        const graph *target = behavior_asset_find_graph_by_name(library, name);
        if (!target || target->kind != kind) {
            continue;
        }
        if (found) {
            diagnostic(error, size, "Ambiguous external callable '%s'; qualify its library", name);
            return NULL;
        }
        found = target;
        owner = library;
    }
    if (!found) {
        diagnostic(error, size, "Unknown callable reference '%s'", name);
        return NULL;
    }
    if (resolved_asset) {
        *resolved_asset = owner->name;
    }
    return found;
}

const behavior_asset *code_source_context_event(
    const code_source_context *context,
    const char *name,
    char *error,
    size_t size
)
{
    if (size) {
        error[0] = 0;
    }
    const behavior_asset *found = NULL;
    for (int i = 0; name && context && i < context->event_count; i++) {
        const behavior_asset *event = context->events[i];
        if (!event || event->domain != BEHAVIOR_DOMAIN_EVENT_DEFINITION ||
            strcmp(event->name, name)) {
            continue;
        }
        if (found) {
            diagnostic(error, size, "Ambiguous event definition '%s'", name);
            return NULL;
        }
        found = event;
    }
    if (!found) {
        diagnostic(error, size, "Unknown event definition '%s'", name);
    }
    return found;
}

const char *code_source_context_reference(
    const code_source_context *context,
    code_reference_kind kind,
    const char *name,
    char *error,
    size_t size
)
{
    const char *found = NULL;
    int count = 0;
    if (kind == CODE_REFERENCE_COMPONENT) {
        for (int i = 0; i < registry_node_count(); i++) {
            const node_desc *desc = registry_node(i);
            if (desc->component_kind != NODE_COMPONENT_GET_SELF) {
                continue;
            }
            const ecs_component_desc *component = desc->component;
            if (!strcmp(component->name, name) ||
                (component->key && !strcmp(component->key, name))) {
                found = component->key ? component->key : component->name;
                count++;
            }
        }
    }
    for (int i = 0; context && i < context->reference_count; i++) {
        const code_reference *ref = &context->references[i];
        if (ref->kind == kind && !strcmp(ref->name, name)) {
            found = ref->name;
            count++;
        }
    }
    if (count == 1) {
        return found;
    }
    bool wrong_kind = false;
    for (int i = 0; context && i < context->reference_count; i++) {
        wrong_kind |= !strcmp(context->references[i].name, name) &&
                      context->references[i].kind != kind;
    }
    if (kind == CODE_REFERENCE_PROTOTYPE) {
        wrong_kind |= component_by_key(name) != NULL;
    }
    for (int i = 0; context && i < context->library_count; i++) {
        wrong_kind |= !strcmp(context->libraries[i]->name, name);
    }
    for (int i = 0; context && i < context->event_count; i++) {
        wrong_kind |= !strcmp(context->events[i]->name, name);
    }
    const char *category = kind == CODE_REFERENCE_PROTOTYPE ? "prototype" : "component";
    if (size) {
        snprintf(
            error,
            size,
            "%s %s symbol '%s'",
            count        ? "Ambiguous"
            : wrong_kind ? "Wrong kind for"
                         : "Unknown",
            category,
            name
        );
    }
    return NULL;
}
