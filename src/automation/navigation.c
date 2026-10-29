/* Native find_references, call_hierarchy and graph use counts over the
 * catalog's shared reference index. Responses identify saved definitions,
 * graph/node IDs and pin names for inspection. */
#include "automation/navigation.h"

#include "assets/call_hierarchy.h"
#include "graph/document/events.h"
#include "graph/nodes/component_nodes.h"
#include "support/array.h"

#include <stdio.h>
#include <string.h>
#include <yyjson.h>

enum
{
    USES_PAGE = 50,
    USES_PAGE_MAX = 200,
    DEPTH_DEFAULT = 2,
    DEPTH_MAX = 6,
};

static const char *const ROLES[REFERENCE_ROLE_COUNT] = {
    [REFERENCE_DEFINE] = "define", [REFERENCE_USE] = "use",
    [REFERENCE_CALL] = "call",     [REFERENCE_ARGUMENT] = "argument",
    [REFERENCE_READ] = "read",     [REFERENCE_WRITE] = "write",
    [REFERENCE_SEND] = "send",     [REFERENCE_HANDLE] = "handle",
    [REFERENCE_SPAWN] = "spawn",   [REFERENCE_ATTACH] = "attach",
    [REFERENCE_BIND] = "bind",
};

static const char *const SYMBOLS[] = {
    [REFERENCE_SYMBOL_ASSET] = "asset",
    [REFERENCE_SYMBOL_GRAPH] = "graph",
    [REFERENCE_SYMBOL_VARIABLE] = "variable",
    [REFERENCE_SYMBOL_PARAMETER] = "parameter",
    [REFERENCE_SYMBOL_LOCAL] = "local",
    [REFERENCE_SYMBOL_EVENT] = "event",
    [REFERENCE_SYMBOL_COMPONENT] = "component",
    [REFERENCE_SYMBOL_FIELD] = "field",
};

static const char *const CALL_KINDS[] = {
    [CALL_FUNCTION] = "function", [CALL_HANDLER] = "handler",
    [CALL_GRAPH] = "graph",       [CALL_EVENT] = "event",
    [CALL_PLACE] = "place",
};

static int fail(char *error, size_t size, int status, const char *message)
{
    snprintf(error, size, "%s", message);
    return status;
}

static const char *text(yyjson_val *object, const char *key)
{
    return yyjson_get_str(yyjson_obj_get(object, key));
}

/* A non-negative integer field; false when present but not one. */
static bool whole(yyjson_val *object, const char *key, int64_t *out)
{
    yyjson_val *value = yyjson_obj_get(object, key);
    if (!value) {
        return true;
    }
    *out = yyjson_is_int(value) ? yyjson_get_sint(value) : -1;
    return *out >= 0;
}

/* ---------- Selection ---------- */

static int find_asset(const navigation *nav, yyjson_val *selector, int *out, char *error, size_t size)
{
    const char *kind = text(selector, "kind"), *name = text(selector, "name");
    if (!kind || !name) {
        return fail(error, size, CHIBI_AUTOMATION_INVALID_REQUEST, "asset takes kind and name");
    }
    for (int i = 0; i < reference_index_asset_count(nav->index); i++) {
        if (!strcmp(nav->assets[i].kind, kind) && !strcmp(nav->assets[i].name, name)) {
            *out = i;
            return CHIBI_AUTOMATION_OK;
        }
    }
    return fail(error, size, CHIBI_AUTOMATION_NOT_FOUND, "no asset of that kind and name is in the project or named by it");
}

/* A local's graph when it may be in any. */
#define ANY_GRAPH UINT64_MAX

/* The first definition of symbol's kind in its asset and graph with this
 * name; *matches counts them all. */
static const reference *named_definition(
    const navigation *nav,
    reference_symbol symbol,
    const char *name,
    int *matches
)
{
    const reference *first = NULL;
    *matches = 0;
    for (int i = 0; i < reference_index_count(nav->index); i++) {
        const reference *r = reference_index_at(nav->index, i);
        const reference_symbol *own = &r->symbol;
        if (r->role == REFERENCE_DEFINE && own->kind == symbol.kind &&
            own->asset == symbol.asset && own->name && !strcmp(own->name, name) &&
            (symbol.graph_id == ANY_GRAPH || own->graph_id == symbol.graph_id)) {
            first = first ? first : r;
            (*matches)++;
        }
    }
    return first;
}

/* The function or macro "graph" or "graph_id" names in asset. */
static int find_graph(
    const navigation *nav,
    yyjson_val *request,
    int asset,
    const reference **out,
    char *error,
    size_t size
)
{
    int64_t id = 0;
    if (!whole(request, "graph_id", &id)) {
        return fail(error, size, CHIBI_AUTOMATION_INVALID_REQUEST, "graph_id must be a non-negative integer");
    }
    reference_symbol graph = { .kind = REFERENCE_SYMBOL_GRAPH,
                               .asset = asset,
                               .id = (uint64_t)id };
    const char *name = text(request, "graph");
    int matches = 0;
    *out = name ? named_definition(nav, graph, name, &matches)
                : reference_index_definition(nav->index, graph);
    if (!*out) {
        return fail(error, size, CHIBI_AUTOMATION_NOT_FOUND, "no function or macro of the asset has that graph name or id");
    }
    return CHIBI_AUTOMATION_OK;
}

/* A local within the graph "graph_id" or "graph" names, or any graph. */
static int find_local(
    const navigation *nav,
    yyjson_val *request,
    reference_symbol *symbol,
    char *error,
    size_t size
)
{
    int64_t id = 0;
    if (!whole(request, "graph_id", &id)) {
        return fail(error, size, CHIBI_AUTOMATION_INVALID_REQUEST, "graph_id must be a non-negative integer");
    }
    symbol->graph_id = yyjson_obj_get(request, "graph_id") ? (uint64_t)id : ANY_GRAPH;
    const reference *function = NULL;
    if (text(request, "graph")) {
        int status = find_graph(nav, request, symbol->asset, &function, error, size);
        if (status != CHIBI_AUTOMATION_OK) {
            return status;
        }
        symbol->graph_id = function->symbol.id;
    }
    symbol->kind = REFERENCE_SYMBOL_LOCAL;
    int matches = 0;
    const reference *local = named_definition(nav, *symbol, text(request, "local"), &matches);
    if (!local) {
        return fail(error, size, CHIBI_AUTOMATION_NOT_FOUND, "no graph of the asset has a local by that name");
    }
    if (matches > 1) {
        return fail(error, size, CHIBI_AUTOMATION_INVALID_REQUEST, "several graphs have a local by that name; pass graph or graph_id");
    }
    *symbol = local->symbol;
    return CHIBI_AUTOMATION_OK;
}

/* The asset, or the graph, variable, parameter or local of it the request
 * names. */
static int select_member(
    const navigation *nav,
    yyjson_val *request,
    int asset,
    reference_symbol *out,
    char *error,
    size_t size
)
{
    const char *variable = text(request, "variable"),
               *parameter = text(request, "parameter");
    const char *local = text(request, "local");
    bool graph = yyjson_obj_get(request, "graph") ||
                 yyjson_obj_get(request, "graph_id");
    if ((variable != NULL) + (parameter != NULL) + (local != NULL) > 1) {
        return fail(error, size, CHIBI_AUTOMATION_INVALID_REQUEST, "select one of variable, parameter or local");
    }
    *out = (reference_symbol){ .kind = REFERENCE_SYMBOL_ASSET, .asset = asset };
    if (local) {
        return find_local(nav, request, out, error, size);
    }
    int matches = 0;
    if (variable) {
        out->kind = REFERENCE_SYMBOL_VARIABLE;
        const reference *found = named_definition(nav, *out, variable, &matches);
        *out = found ? found->symbol : *out;
        return found
                 ? CHIBI_AUTOMATION_OK
                 : fail(error, size, CHIBI_AUTOMATION_NOT_FOUND, "the asset has no variable by that name");
    }
    if (!graph && parameter && !strcmp(nav->assets[asset].kind, "model")) {
        reference_symbol wanted = { .kind = REFERENCE_SYMBOL_PARAMETER, .asset = asset };
        const reference *found = named_definition(nav, wanted, parameter, &matches);
        if (matches > 1) {
            return fail(error, size, CHIBI_AUTOMATION_INVALID_REQUEST, "the model has several parameters by that name");
        }
        if (!found) {
            return fail(error, size, CHIBI_AUTOMATION_NOT_FOUND, "the model has no public parameter by that name");
        }
        *out = found->symbol;
        return CHIBI_AUTOMATION_OK;
    }
    if (!graph) {
        return parameter
                 ? fail(error, size, CHIBI_AUTOMATION_INVALID_REQUEST, "parameter takes its function's graph or graph_id")
                 : CHIBI_AUTOMATION_OK;
    }
    const reference *function = NULL;
    int status = find_graph(nav, request, asset, &function, error, size);
    if (status != CHIBI_AUTOMATION_OK || !parameter) {
        *out = function ? function->symbol : *out;
        return status;
    }
    reference_symbol wanted = { .kind = REFERENCE_SYMBOL_PARAMETER,
                                .asset = asset,
                                .graph_id = function->symbol.id };
    const reference *found = named_definition(nav, wanted, parameter, &matches);
    if (!found) {
        return fail(error, size, CHIBI_AUTOMATION_NOT_FOUND, "the function has no input or output by that name");
    }
    if (matches > 1) {
        return fail(error, size, CHIBI_AUTOMATION_INVALID_REQUEST, "the function has an input and an output by that name");
    }
    *out = found->symbol;
    return CHIBI_AUTOMATION_OK;
}

static int select_component(yyjson_val *request, reference_symbol *out, char *error, size_t size)
{
    const ecs_component_desc *component = component_by_key(text(request, "component"));
    const char *field = text(request, "field");
    if (!component) {
        return fail(error, size, CHIBI_AUTOMATION_NOT_FOUND, "no component has that name or key; call describeComponents");
    }
    if (field && !pin_type_field_path(component->type, field)) {
        return fail(error, size, CHIBI_AUTOMATION_NOT_FOUND, "the component has no field by that path; call describeComponents");
    }
    *out = (reference_symbol){ .kind = field ? REFERENCE_SYMBOL_FIELD
                                             : REFERENCE_SYMBOL_COMPONENT,
                               .asset = -1,
                               .name = component->name,
                               .field = field };
    return CHIBI_AUTOMATION_OK;
}

static int select_symbol(
    const navigation *nav,
    yyjson_val *request,
    reference_symbol *out,
    char *error,
    size_t size
)
{
    const char *event = text(request, "event");
    yyjson_val *asset = yyjson_obj_get(request, "asset");
    bool component = yyjson_obj_get(request, "component") != NULL;
    if ((event != NULL) + (asset != NULL) + component != 1 ||
        (asset && !yyjson_is_obj(asset))) {
        return fail(error, size, CHIBI_AUTOMATION_INVALID_REQUEST, "select one of asset {kind, name}, event or component");
    }
    if (component) {
        return select_component(request, out, error, size);
    }
    if (event) {
        *out = (reference_symbol){ .kind = REFERENCE_SYMBOL_EVENT,
                                   .asset = reference_index_event_asset(nav->index, event),
                                   .name = event };
        return CHIBI_AUTOMATION_OK;
    }
    int position = -1;
    int status = find_asset(nav, asset, &position, error, size);
    return status == CHIBI_AUTOMATION_OK
             ? select_member(nav, request, position, out, error, size)
             : status;
}

/* ---------- Output ---------- */

static void add_asset(const navigation *nav, yyjson_mut_doc *doc, yyjson_mut_val *object, int asset, bool path)
{
    const navigation_asset *named = &nav->assets[asset];
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(doc, object, "asset");
    yyjson_mut_obj_add_strcpy(doc, value, "kind", named->kind);
    yyjson_mut_obj_add_strcpy(doc, value, "name", named->name);
    if (path && named->path) {
        yyjson_mut_obj_add_strcpy(doc, value, "path", named->path);
    }
    if (!named->path) {
        yyjson_mut_obj_add_bool(doc, value, "unresolved", true);
    }
}

static void add_text(yyjson_mut_doc *doc, yyjson_mut_val *object, const char *key, const char *value)
{
    if (value) {
        yyjson_mut_obj_add_strcpy(doc, object, key, value);
    }
}

/* Where in its asset a reference is: its graph, node and pin, or its scene
 * object and component field. */
static void add_within(yyjson_mut_doc *doc, yyjson_mut_val *object, const reference *r)
{
    if (r->node_id) {
        yyjson_mut_obj_add_uint(doc, object, "graph_id", r->graph_id);
        add_text(doc, object, "graph", r->graph);
        yyjson_mut_obj_add_uint(doc, object, "node", r->node_id);
        add_text(doc, object, "pin", r->pin);
    }
    add_text(doc, object, "object", r->object);
    add_text(doc, object, "component", r->component);
}

/* Where a reference is: its asset, and where in it. */
static void add_place(
    const navigation *nav,
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const reference *r,
    bool path
)
{
    add_asset(nav, doc, object, r->source, path);
    add_within(doc, object, r);
}

static void add_symbol(const navigation *nav, yyjson_mut_doc *doc, yyjson_mut_val *result, reference_symbol symbol)
{
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(doc, result, "symbol");
    yyjson_mut_obj_add_str(doc, value, "kind", SYMBOLS[symbol.kind]);
    add_text(doc, value, "name", symbol.name);
    add_text(doc, value, "field", symbol.field);
    if (symbol.asset >= 0 && symbol.kind != REFERENCE_SYMBOL_EVENT) {
        add_asset(nav, doc, value, symbol.asset, true);
    }
    if (symbol.kind == REFERENCE_SYMBOL_EVENT && host_event_reserved(symbol.name)) {
        yyjson_mut_obj_add_bool(doc, value, "host", true);
    }
}

static void add_component_definition(yyjson_mut_doc *doc, yyjson_mut_val *value, reference_symbol symbol)
{
    const ecs_component_desc *component = component_by_key(symbol.name);
    yyjson_mut_obj_add_strcpy(doc, value, "component", component->name);
    add_text(doc, value, "key", component->key);
    if (symbol.field) {
        const type_field *field = pin_type_field_path(component->type, symbol.field);
        yyjson_mut_obj_add_strcpy(doc, value, "field", symbol.field);
        add_text(doc, value, "type", type_field_type_name(field));
    }
}

/* Where the symbol is declared: an asset is its own definition, and a
 * component its reflected type; host and undeclared events have none. */
static void add_definition(const navigation *nav, yyjson_mut_doc *doc, yyjson_mut_val *result, reference_symbol symbol)
{
    const reference *declared = symbol.kind == REFERENCE_SYMBOL_ASSET
                                  ? NULL
                                  : reference_index_definition(nav->index, symbol);
    bool asset = symbol.kind == REFERENCE_SYMBOL_ASSET &&
                 nav->assets[symbol.asset].path;
    bool component = symbol.kind == REFERENCE_SYMBOL_COMPONENT ||
                     symbol.kind == REFERENCE_SYMBOL_FIELD;
    if (!declared && !asset && !component) {
        yyjson_mut_obj_add_null(doc, result, "definition");
        return;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(doc, result, "definition");
    if (component) {
        add_component_definition(doc, value, symbol);
        return;
    }
    if (asset) {
        add_asset(nav, doc, value, symbol.asset, true);
        return;
    }
    add_place(nav, doc, value, declared, true);
}

/* ---------- findReferences ---------- */

static int request_roles(yyjson_val *request, unsigned *roles, char *error, size_t size)
{
    yyjson_val *names = yyjson_obj_get(request, "roles");
    *roles = REFERENCE_USES;
    if (!names) {
        return CHIBI_AUTOMATION_OK;
    }
    *roles = 0;
    size_t at, count;
    yyjson_val *name;
    yyjson_arr_foreach(names, at, count, name)
    {
        unsigned before = *roles;
        for (int role = REFERENCE_USE; role < REFERENCE_ROLE_COUNT; role++) {
            *roles |= yyjson_equals_str(name, ROLES[role]) ? REFERENCE_ROLE(role) : 0;
        }
        if (*roles == before) {
            return fail(error, size, CHIBI_AUTOMATION_INVALID_REQUEST, "roles are use, call, argument, read, write, send, handle, spawn, attach or bind");
        }
    }
    return yyjson_is_arr(names) && *roles
             ? CHIBI_AUTOMATION_OK
             : fail(error, size, CHIBI_AUTOMATION_INVALID_REQUEST, "roles must be a non-empty array of role names");
}

static void add_counts(yyjson_mut_doc *doc, yyjson_mut_val *object, const char *key, const reference **uses)
{
    int counts[REFERENCE_ROLE_COUNT] = { 0 };
    for (int i = 0; i < acount(uses); i++) {
        counts[uses[i]->role]++;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(doc, object, key);
    for (int role = 0; role < REFERENCE_ROLE_COUNT; role++) {
        if (counts[role]) {
            yyjson_mut_obj_add_int(doc, value, ROLES[role], counts[role]);
        }
    }
}

static void add_uses(
    const navigation *nav,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    const reference **uses,
    unsigned roles,
    const int64_t page[2]
)
{
    yyjson_mut_val *list = yyjson_mut_obj_add_arr(doc, result, "uses");
    int64_t total = 0, count = 0;
    for (int i = 0; i < acount(uses); i++) {
        if (!(roles & REFERENCE_ROLE(uses[i]->role))) {
            continue;
        }
        if (total++ < page[0] || count == page[1]) {
            continue;
        }
        yyjson_mut_val *use = yyjson_mut_arr_add_obj(doc, list);
        yyjson_mut_obj_add_str(doc, use, "role", ROLES[uses[i]->role]);
        add_place(nav, doc, use, uses[i], false);
        count++;
    }
    yyjson_mut_obj_add_int(doc, result, "total", total);
    yyjson_mut_obj_add_int(doc, result, "offset", page[0]);
    yyjson_mut_obj_add_int(doc, result, "count", count);
    yyjson_mut_obj_add_bool(doc, result, "has_more", page[0] + count < total);
}

int automation_find_references(
    const navigation *nav,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char *error,
    size_t error_size
)
{
    int64_t page[2] = { 0, USES_PAGE };
    if (!whole(request, "offset", &page[0]) || !whole(request, "limit", &page[1]) ||
        page[1] < 1 || page[1] > USES_PAGE_MAX) {
        return fail(error, error_size, CHIBI_AUTOMATION_INVALID_REQUEST, "offset must be 0 or more and limit 1 to 200");
    }
    reference_symbol symbol;
    unsigned roles;
    int status = select_symbol(nav, request, &symbol, error, error_size);
    status = status == CHIBI_AUTOMATION_OK
               ? request_roles(request, &roles, error, error_size)
               : status;
    if (status != CHIBI_AUTOMATION_OK) {
        return status;
    }
    const reference **uses = NULL;
    reference_index_find_uses(nav->index, symbol, &uses);
    add_symbol(nav, doc, result, symbol);
    add_definition(nav, doc, result, symbol);
    add_counts(doc, result, "counts", uses);
    add_uses(nav, doc, result, uses, roles, page);
    afree(uses);
    return CHIBI_AUTOMATION_OK;
}

/* ---------- callHierarchy ---------- */

static void add_item(
    const navigation *nav,
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const call_item *items,
    int at,
    const char *children
);

static void add_item_identity(
    const navigation *nav,
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const call_item *item
)
{
    yyjson_mut_obj_add_str(doc, object, "kind", CALL_KINDS[item->kind]);
    if (item->kind == CALL_EVENT) {
        add_text(doc, object, "name", item->name);
        if (host_event_reserved(item->name)) {
            yyjson_mut_obj_add_bool(doc, object, "host", true);
        }
        if (item->asset >= 0) {
            add_asset(nav, doc, object, item->asset, false);
        }
        return;
    }
    add_asset(nav, doc, object, item->asset, false);
    if (item->kind != CALL_PLACE) {
        yyjson_mut_obj_add_uint(doc, object, "graph_id", item->graph_id);
    }
    switch (item->kind) {
        case CALL_FUNCTION: add_text(doc, object, "name", item->name); break;
        case CALL_HANDLER:
            yyjson_mut_obj_add_uint(doc, object, "node", item->node_id);
            add_text(doc, object, "event", item->name);
            break;
        case CALL_GRAPH: add_text(doc, object, "graph", item->name); break;
        case CALL_PLACE: add_text(doc, object, "place", item->name); break;
        case CALL_EVENT: break;
    }
}

/* The call and send nodes joining an item to its parent; an Event node is
 * its handler's own. */
static void add_sites(yyjson_mut_doc *doc, yyjson_mut_val *object, const call_item *item)
{
    yyjson_mut_val *sites = NULL;
    for (int i = 0; i < acount(item->sites); i++) {
        const reference *site = item->sites[i];
        if (site->node_id && site->role != REFERENCE_HANDLE) {
            sites = sites ? sites : yyjson_mut_obj_add_arr(doc, object, "sites");
            yyjson_mut_arr_add_uint(doc, sites, site->node_id);
        }
    }
}

static void add_item(
    const navigation *nav,
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const call_item *items,
    int at,
    const char *children
)
{
    const call_item *item = &items[at];
    add_item_identity(nav, doc, object, item);
    add_sites(doc, object, item);
    if (item->cycle) {
        yyjson_mut_obj_add_bool(doc, object, "cycle", true);
    }
    if (item->more) {
        yyjson_mut_obj_add_bool(doc, object, "more", true);
    }
    yyjson_mut_val *list = item->count ? yyjson_mut_obj_add_arr(doc, object, children)
                                       : NULL;
    for (int i = 0; i < item->count; i++) {
        add_item(nav, doc, yyjson_mut_arr_add_obj(doc, list), items, item->first + i, children);
    }
}

static int request_hierarchy(yyjson_val *request, bool directions[2], int64_t *depth, char *error, size_t size)
{
    const char *direction = text(request, "direction");
    *depth = DEPTH_DEFAULT;
    if (!whole(request, "depth", depth) || *depth < 1 || *depth > DEPTH_MAX) {
        return fail(error, size, CHIBI_AUTOMATION_INVALID_REQUEST, "depth must be 1 to 6");
    }
    if (yyjson_obj_get(request, "direction") && !direction) {
        direction = "";
    }
    directions[0] = !direction || !strcmp(direction, "incoming") ||
                    !strcmp(direction, "both");
    directions[1] = !direction || !strcmp(direction, "outgoing") ||
                    !strcmp(direction, "both");
    return directions[0] || directions[1]
             ? CHIBI_AUTOMATION_OK
             : fail(error, size, CHIBI_AUTOMATION_INVALID_REQUEST, "direction must be incoming, outgoing or both");
}

int automation_call_hierarchy(
    const navigation *nav,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char *error,
    size_t error_size
)
{
    reference_symbol symbol;
    bool directions[2];
    int64_t depth;
    int status = select_symbol(nav, request, &symbol, error, error_size);
    if (status == CHIBI_AUTOMATION_OK && symbol.kind != REFERENCE_SYMBOL_GRAPH &&
        symbol.kind != REFERENCE_SYMBOL_EVENT) {
        status = fail(error, error_size, CHIBI_AUTOMATION_INVALID_REQUEST, "select a function or macro (asset with graph or graph_id) or an event");
    }
    status = status == CHIBI_AUTOMATION_OK
               ? request_hierarchy(request, directions, &depth, error, error_size)
               : status;
    if (status != CHIBI_AUTOMATION_OK) {
        return status;
    }
    yyjson_mut_obj_add_int(doc, result, "depth", depth);
    yyjson_mut_val *root = yyjson_mut_obj_add_obj(doc, result, "root");
    static const char *const CHILDREN[2] = { "callers", "callees" };
    bool more = false;
    for (int callees = 0; callees < 2; callees++) {
        if (!directions[callees]) {
            continue;
        }
        call_item *items = call_hierarchy(nav->index, symbol, callees, (int)depth);
        if (!yyjson_mut_obj_size(root)) {
            add_item_identity(nav, doc, root, &items[0]);
        }
        yyjson_mut_val *list = yyjson_mut_obj_add_arr(doc, root, CHILDREN[callees]);
        for (int i = 0; i < items[0].count; i++) {
            add_item(nav, doc, yyjson_mut_arr_add_obj(doc, list), items, items[0].first + i, CHILDREN[callees]);
        }
        more |= items[0].more;
        call_hierarchy_free(items);
    }
    /* The root's own list was cut at the item limit. */
    if (more) {
        yyjson_mut_obj_add_bool(doc, root, "more", true);
    }
    return CHIBI_AUTOMATION_OK;
}

/* ---------- Use counts ---------- */

static void add_use_counts(const navigation *nav, yyjson_mut_doc *doc, yyjson_mut_val *object, reference_symbol symbol)
{
    const reference **uses = NULL;
    reference_index_find_uses(nav->index, symbol, &uses);
    add_counts(doc, object, "uses", uses);
    afree(uses);
}

/* Expression functions belong to their nodes, and only behaviors' Get and Set
 * nodes read variables: event and field parameters are read by name. */
void automation_add_use_counts(const navigation *nav, int asset, yyjson_mut_doc *doc, yyjson_mut_val *result)
{
    add_use_counts(nav, doc, result, (reference_symbol){ .kind = REFERENCE_SYMBOL_ASSET, .asset = asset });
    size_t at, count;
    yyjson_mut_val *item;
    yyjson_mut_arr_foreach(yyjson_mut_obj_get(result, "graphs"), at, count, item)
    {
        reference_symbol graph = {
            .kind = REFERENCE_SYMBOL_GRAPH,
            .asset = asset,
            .id = yyjson_mut_get_uint(yyjson_mut_obj_get(item, "id"))
        };
        if (reference_index_definition(nav->index, graph)) {
            add_use_counts(nav, doc, item, graph);
        }
    }
    if (strcmp(nav->assets[asset].kind, "behavior")) {
        return;
    }
    yyjson_mut_arr_foreach(yyjson_mut_obj_get(result, "variables"), at, count, item)
    {
        reference_symbol variable = {
            .kind = REFERENCE_SYMBOL_VARIABLE,
            .asset = asset,
            .id = yyjson_mut_get_uint(yyjson_mut_obj_get(item, "id"))
        };
        add_use_counts(nav, doc, item, variable);
    }
}

/* ---------- Shared with rename and delete ---------- */
