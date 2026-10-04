#include "assets/reference_index.h"

#include "assets/prototype.h"
#include "assets/ui_document.h"
#include "graph/document/events.h"
#include "graph/document/graph_signature.h"
#include "graph/model/local.h"
#include "graph/model/registry.h"
#include "graph/nodes/component_nodes.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/asset_value.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char *identity; /* the canonical path */
    char *key;
} asset_identity;

/* The nodes the run an Event node starts reaches. */
typedef struct
{
    int source;
    uint64_t graph_id, event_id;
    uint64_t *nodes;
} event_run;

struct reference_index
{
    reference_asset *assets;
    asset_identity *identities; /* parallel to assets */
    reference *references;
    event_run *runs;
};

/* Component fields whose text names an asset, and the role they give it. An
 * action names the event its binding sends. */
static const struct
{
    const char *component, *field;
    reference_asset_kind kind;
    reference_role role;
} NAMED_FIELDS[] = {
    { "MaterialTerrain", "field", REFERENCE_ASSET_FIELD, REFERENCE_BIND },
    { "EngineSystem", "asset", REFERENCE_ASSET_SYSTEM, REFERENCE_BIND },
    { "GameShader", "material", REFERENCE_ASSET_MATERIAL, REFERENCE_BIND },
    { "GameShader", "shape", REFERENCE_ASSET_SHAPE, REFERENCE_BIND },
    { "GameUi", "document", REFERENCE_ASSET_UI, REFERENCE_BIND },
    { "GameSpawnRequest", "prototype", REFERENCE_ASSET_PROTOTYPE, REFERENCE_SPAWN },
    { "GameActionBinding", "action", REFERENCE_ASSET_EVENT, REFERENCE_SEND },
};

#define NAMED_FIELD_COUNT ((int)(sizeof NAMED_FIELDS / sizeof *NAMED_FIELDS))

/* GameMeshParts.parts holds KDL parts, each naming its material. */
#define MESH_PARTS "GameMeshParts"

static char *copy(const char *text)
{
    return text ? xstrdup(text) : NULL;
}

reference_index *reference_index_new()
{
    return xcalloc(1, sizeof(reference_index));
}

void reference_index_clear(reference_index *index)
{
    for (int i = 0; i < acount(index->assets); i++) {
        free((char *)index->assets[i].name);
        free((char *)index->assets[i].path);
        free(index->identities[i].identity);
        free(index->identities[i].key);
    }
    for (int i = 0; i < acount(index->references); i++) {
        reference *r = &index->references[i];
        free((char *)r->symbol.name);
        free((char *)r->symbol.field);
        free((char *)r->graph);
        free((char *)r->pin);
        free((char *)r->object);
        free((char *)r->component);
    }
    for (int i = 0; i < acount(index->runs); i++) {
        afree(index->runs[i].nodes);
    }
    asetlen(index->assets, 0);
    asetlen(index->identities, 0);
    asetlen(index->references, 0);
    asetlen(index->runs, 0);
}

void reference_index_free(reference_index *index)
{
    if (!index) {
        return;
    }
    reference_index_clear(index);
    afree(index->assets);
    afree(index->identities);
    afree(index->references);
    afree(index->runs);
    free(index);
}

/* ---------- Assets ---------- */

static char *path_identity(const char *path)
{
    char *canonical = realpath(path, NULL);
    if (canonical) {
        return canonical;
    }
    while (path[0] == '.' && path[1] == '/') {
        path += 2;
    }
    return xstrdup(path);
}

int reference_index_find_asset(
    const reference_index *index,
    reference_asset_kind kind,
    const char *path,
    const char *key
)
{
    char *identity = path ? path_identity(path) : NULL;
    const char *wanted = path ? identity : key;
    int found = -1;
    for (int i = 0; wanted && found < 0 && i < acount(index->assets); i++) {
        const char *own = path ? index->identities[i].identity
                               : index->identities[i].key;
        if (index->assets[i].kind == kind && own && !strcmp(own, wanted)) {
            found = i;
        }
    }
    free(identity);
    return found;
}

int reference_index_find_name(const reference_index *index, reference_asset_kind kind, const char *name)
{
    for (int i = 0; name && i < acount(index->assets); i++) {
        if (index->assets[i].kind == kind && !strcmp(index->assets[i].name, name)) {
            return i;
        }
    }
    return -1;
}

/* An asset of that name without a path: when adding one with a path, any,
 * so a saved asset replaces its unsaved one; otherwise an unresolved one. */
static int pathless_name(const reference_index *index, reference_asset_kind kind, const char *name, bool saved)
{
    for (int i = 0; i < acount(index->assets); i++) {
        const reference_asset *asset = &index->assets[i];
        if (asset->kind == kind && !asset->path &&
            (saved || asset->unresolved) && !strcmp(asset->name, name)) {
            return i;
        }
    }
    return -1;
}

static int append_asset(
    reference_index *index,
    reference_asset_kind kind,
    const char *name,
    const char *path,
    const char *key
)
{
    apush(
        index->assets,
        ((reference_asset){ .kind = kind,
                            .name = xstrdup(name),
                            .path = copy(path),
                            .unresolved = !path && !key })
    );
    apush(
        index->identities,
        ((asset_identity){ .identity = path ? path_identity(path) : NULL,
                           .key = copy(key) })
    );
    return acount(index->assets) - 1;
}

int reference_index_add_asset(
    reference_index *index,
    reference_asset_kind kind,
    const char *name,
    const char *path,
    const char *key
)
{
    assert(name && name[0] && (path || key));
    int at = reference_index_find_asset(index, kind, path, key);
    if (at < 0) {
        at = pathless_name(index, kind, name, path != NULL);
    }
    if (at < 0) {
        return append_asset(index, kind, name, path, key);
    }
    reference_asset *asset = &index->assets[at];
    asset_identity *identity = &index->identities[at];
    if (path && !asset->path) {
        asset->path = xstrdup(path);
        identity->identity = path_identity(path);
    }
    if (key && !identity->key) {
        identity->key = xstrdup(key);
    }
    asset->unresolved = false;
    if (strcmp(asset->name, name)) {
        free((char *)asset->name);
        asset->name = xstrdup(name);
    }
    return at;
}

int reference_index_event_asset(const reference_index *index, const char *name)
{
    return graph_event_host_name(name)
             ? -1
             : reference_index_find_name(index, REFERENCE_ASSET_EVENT, name);
}

/* The asset a name refers to, unresolved when none was added. */
static int named_asset(reference_index *index, reference_asset_kind kind, const char *name)
{
    int at = reference_index_find_name(index, kind, name);
    return at >= 0 ? at : append_asset(index, kind, name, NULL, NULL);
}

int reference_index_asset_count(const reference_index *index)
{
    return acount(index->assets);
}

const reference_asset *reference_index_asset_at(const reference_index *index, int position)
{
    return position >= 0 && position < acount(index->assets)
             ? &index->assets[position]
             : NULL;
}

reference_asset_kind reference_domain_kind(behavior_domain domain)
{
    switch (domain) {
        case BEHAVIOR_DOMAIN_EVENT: return REFERENCE_ASSET_BEHAVIOR;
        case BEHAVIOR_DOMAIN_LIBRARY:
        case BEHAVIOR_DOMAIN_SHADER_LIBRARY: return REFERENCE_ASSET_LIBRARY;
        case BEHAVIOR_DOMAIN_MATERIAL: return REFERENCE_ASSET_MATERIAL;
        case BEHAVIOR_DOMAIN_FIELD: return REFERENCE_ASSET_FIELD;
        case BEHAVIOR_DOMAIN_MODEL:
        case BEHAVIOR_DOMAIN_SHAPE: return REFERENCE_ASSET_SHAPE;
        case BEHAVIOR_DOMAIN_EVENT_DEFINITION: return REFERENCE_ASSET_EVENT;
    }
    return REFERENCE_ASSET_BEHAVIOR;
}

/* ---------- References ---------- */

static void add(reference_index *index, reference r)
{
    assert(r.source >= 0 && r.source < acount(index->assets));
    assert(r.symbol.asset >= -1 && r.symbol.asset < acount(index->assets));
    r.symbol.name = copy(r.symbol.name);
    r.symbol.field = copy(r.symbol.field);
    r.graph = copy(r.graph);
    r.pin = copy(r.pin);
    r.object = copy(r.object);
    r.component = copy(r.component);
    apush(index->references, r);
}

static reference_symbol asset_symbol(int asset)
{
    return (reference_symbol){ .kind = REFERENCE_SYMBOL_ASSET, .asset = asset };
}

static reference_symbol event_symbol(const reference_index *index, const char *name)
{
    return (reference_symbol){ .kind = REFERENCE_SYMBOL_EVENT,
                               .asset = reference_index_event_asset(index, name),
                               .name = name };
}

/* What NAMED_FIELDS[field] holds, value, names: an asset, or a sent event.
 * `at` gives the reference's source and place. */
static void add_named(reference_index *index, reference at, int field, const char *value)
{
    char component[128];
    snprintf(
        component,
        sizeof component,
        "%s.%s",
        NAMED_FIELDS[field].component,
        NAMED_FIELDS[field].field
    );
    reference_asset_kind kind = NAMED_FIELDS[field].kind;
    at.role = NAMED_FIELDS[field].role;
    at.symbol = kind == REFERENCE_ASSET_EVENT
                  ? event_symbol(index, value)
                  : asset_symbol(named_asset(index, kind, value));
    at.component = component;
    add(index, at);
}

static void add_mesh_parts(reference_index *index, reference at, const char *parts)
{
    av_doc *doc = av_value_read(parts, NULL, 0);
    av_value *root = av_doc_root(doc);
    at.role = REFERENCE_BIND;
    at.component = MESH_PARTS ".parts";
    size_t i, count;
    av_value *part;
    av_arr_foreach(root, i, count, part)
    {
        const char *material = av_get_str(av_obj_get(part, "material"));
        if (material && material[0]) {
            at.symbol = asset_symbol(named_asset(index, REFERENCE_ASSET_MATERIAL, material));
            add(index, at);
        }
    }
    av_doc_free(doc);
}

static int named_field(const char *component, const char *field)
{
    for (int i = 0; i < NAMED_FIELD_COUNT; i++) {
        if (!strcmp(NAMED_FIELDS[i].component, component) &&
            !strcmp(NAMED_FIELDS[i].field, field)) {
            return i;
        }
    }
    return -1;
}

reference_asset_kind reference_named_field_kind(const char *component, const char *field)
{
    int named = named_field(component, field);
    return named < 0 ? REFERENCE_ASSET_KIND_COUNT : NAMED_FIELDS[named].kind;
}

/* ---------- Graphs ---------- */

/* Where in a graph asset references are being added. */
typedef struct
{
    reference_index *index;
    int source;
    const graph *graph;
    const node *node; /* NULL outside nodes */
} site;

/* Where the site is: its node, pin and graph, when it has them. */
static reference place_of(const site *s, uint64_t pin)
{
    const graph *g = s->graph;
    int output, at;
    const pin_spec *spec = s->node && pin
                             ? graph_pin_by_id(s->node, pin, &output, &at)
                             : NULL;
    return (reference){ .source = s->source,
                        .graph_id = g ? g->graph_id : 0,
                        .node_id = s->node ? s->node->id : 0,
                        .pin_id = pin,
                        .graph = g && g->name && g->name[0] ? g->name : NULL,
                        .pin = spec && spec->name && spec->name[0] ? spec->name : NULL };
}

static void add_at(const site *s, reference_role role, reference_symbol symbol, uint64_t pin)
{
    reference r = place_of(s, pin);
    r.role = role;
    r.symbol = symbol;
    add(s->index, r);
}

/* A graph, variable, parameter or local of the site's asset. */
static reference_symbol own_symbol(const site *s, reference_symbol_kind kind, uint64_t graph_id, uint64_t id)
{
    return (
        reference_symbol
    ){ .kind = kind, .asset = s->source, .graph_id = graph_id, .id = id };
}

/* One of them where it is declared, with its name. */
static void add_definition(const site *s, reference_symbol symbol, const char *name, uint64_t pin)
{
    symbol.name = name && name[0] ? name : NULL;
    add_at(s, REFERENCE_DEFINE, symbol, pin);
}

static const char *pin_text(const pin_spec *pin)
{
    bool text = pin->default_value.type == PIN_NAME ||
                pin->default_value.type == PIN_STR;
    return pin->container == PIN_CONTAINER_SINGLE && text &&
                   pin->default_value.str && pin->default_value.str[0]
             ? pin->default_value.str
             : NULL;
}

static bool graph_call(const node *n)
{
    return n->desc == &NODE_CallFunction || n->desc == &NODE_CallPureFunction ||
           n->desc == &NODE_CallMacro ||
           (n->reference_asset && n->reference_asset[0]);
}

/* A call names its graph, in its library or its own asset, and each pin
 * that follows a parameter or result names that parameter. */
static void add_call(const site *s)
{
    const node *n = s->node;
    int asset = n->reference_asset && n->reference_asset[0]
                  ? named_asset(s->index, REFERENCE_ASSET_LIBRARY, n->reference_asset)
                  : s->source;
    reference_symbol parameter = { .kind = REFERENCE_SYMBOL_PARAMETER,
                                   .asset = asset,
                                   .graph_id = n->reference_id };
    add_at(
        s,
        REFERENCE_CALL,
        (reference_symbol){ .kind = REFERENCE_SYMBOL_GRAPH, .asset = asset, .id = n->reference_id },
        0
    );
    for (int output = 0; output < 2; output++) {
        const pin_spec *pins = output ? n->output_pins : n->input_pins;
        for (int i = 0; i < (output ? n->output_count : n->input_count); i++) {
            parameter.id = pins[i].declaration_id;
            if (parameter.id) {
                add_at(s, REFERENCE_ARGUMENT, parameter, pins[i].id);
            }
        }
    }
}

static void add_event(const site *s)
{
    const node *n = s->node;
    bool handle = !strcmp(n->desc->name, "Event");
    bool send = n->desc == &NODE_CallEvent || !strcmp(n->desc->name, "Send Event");
    if ((handle || send) && n->reference_name && n->reference_name[0]) {
        add_at(s, handle ? REFERENCE_HANDLE : REFERENCE_SEND, event_symbol(s->index, n->reference_name), 0);
    }
}

static void add_spawn(const site *s)
{
    const node *n = s->node;
    for (int i = 0; !strcmp(n->desc->name, "Spawn Prototype") && i < n->input_count;
         i++) {
        const char *prototype = pin_text(&n->input_pins[i]);
        if (!strcmp(n->input_pins[i].name, "Prototype") && prototype) {
            int asset = named_asset(s->index, REFERENCE_ASSET_PROTOTYPE, prototype);
            add_at(s, REFERENCE_SPAWN, asset_symbol(asset), n->input_pins[i].id);
        }
    }
}

static const char *component_name(const char *key)
{
    const ecs_component_desc *component = component_by_key(key);
    return component ? component->name : key;
}

static reference_symbol field_symbol(const char *component, const char *field)
{
    return (reference_symbol){ .kind = REFERENCE_SYMBOL_FIELD,
                               .asset = -1,
                               .name = component,
                               .field = field };
}

static const pin_spec *output_by_id(const node *n, uint64_t id)
{
    for (int i = 0; i < n->output_count; i++) {
        if (n->output_pins[i].id == id) {
            return &n->output_pins[i];
        }
    }
    return NULL;
}

/* The dot path of the field a component getter's output exposes, as
 * exposed fields (field_source) name it; false for the component itself. */
static bool exposed_path(const node *n, const pin_spec *pin, char *path, size_t size)
{
    if (!pin->field_source || !pin->field_name) {
        return false;
    }
    const pin_spec *parent = output_by_id(n, pin->field_source);
    char prefix[128];
    if (parent && exposed_path(n, parent, prefix, sizeof prefix)) {
        snprintf(path, size, "%s.%s", prefix, pin->field_name);
        return true;
    }
    snprintf(path, size, "%s", pin->field_name);
    return true;
}

/* A split pin names a field of the struct it split: the component's own, or
 * an exposed field's. */
static bool field_path(const node *n, const pin_spec *pin, const char *type, char *path, size_t size)
{
    if (!pin->split_parent) {
        return pin->kind == PIN_DATA && exposed_path(n, pin, path, size);
    }
    char prefix[128];
    if (exposed_path(n, pin, prefix, sizeof prefix)) {
        snprintf(path, size, "%s.%s", prefix, pin->name);
        return true;
    }
    if (pin->split_type_name && !strcmp(pin->split_type_name, type)) {
        snprintf(path, size, "%s", pin->name);
        return true;
    }
    return false;
}

/* The field a component setter's input writes: a field setter's Value, a
 * split Set Component's field input, or a split Value's part. */
static bool set_field(const node *n, int input, const ecs_component_desc *c, char *path, size_t size)
{
    const pin_spec *pin = &n->input_pins[input];
    if (pin->kind != PIN_DATA) {
        return false;
    }
    if (pin->split_parent) {
        return field_path(n, pin, c->type->name, path, size);
    }
    if (n->desc->component_kind == NODE_COMPONENT_SET_FIELD) {
        snprintf(path, size, "%s", c->type->fields[n->desc->component_field].name);
        return !strcmp(pin->name, "Value");
    }
    snprintf(path, size, "%s", pin->name);
    return component_node_kind_of(n->desc) == COMPONENT_NODE_SET &&
           component_node_split(n) && input >= component_node_setting(n) + 2;
}

/* A field's text default that names an asset, as a Set of GameUi.document. */
static void add_field_default(const site *s, const char *component, const char *field, const pin_spec *pin)
{
    int named = named_field(component, field);
    const char *value = pin_text(pin);
    if (named >= 0 && value) {
        add_named(s->index, place_of(s, pin->id), named, value);
    }
}

static void add_component_fields(const site *s, const ecs_component_desc *c, bool write)
{
    const node *n = s->node;
    char path[256];
    for (int i = 0; write && i < n->input_count; i++) {
        if (set_field(n, i, c, path, sizeof path)) {
            add_at(
                s,
                REFERENCE_WRITE,
                field_symbol(c->name, path),
                n->input_pins[i].id
            );
            add_field_default(s, c->name, path, &n->input_pins[i]);
        }
    }
    for (int i = 0; !write && i < n->output_count; i++) {
        if (field_path(n, &n->output_pins[i], c->type->name, path, sizeof path)) {
            add_at(
                s,
                REFERENCE_READ,
                field_symbol(c->name, path),
                n->output_pins[i].id
            );
        }
    }
}

/* Get, Has, Set, Add and Remove nodes, authored or generated, and the fields
 * a getter exposes or a setter writes. */
static void add_component_access(const site *s)
{
    const char *verb, *name;
    if (!component_node_access(s->node, &verb, &name) ||
        !strcmp(verb, "Make") || !strcmp(verb, "Break")) {
        return;
    }
    bool write = strcmp(verb, "Get") && strcmp(verb, "Has");
    reference_symbol symbol = { .kind = REFERENCE_SYMBOL_COMPONENT,
                                .asset = -1,
                                .name = name };
    add_at(s, write ? REFERENCE_WRITE : REFERENCE_READ, symbol, 0);
    const ecs_component_desc *component = component_node_component(s->node);
    if (component && (!strcmp(verb, "Get") || !strcmp(verb, "Set"))) {
        add_component_fields(s, component, write);
    }
}

/* A struct default of a component type whose fields name assets, as a
 * GameShader value's material. */
static void add_struct_default(const site *s, const pin_spec *pin)
{
    const pin_value *value = &pin->default_value;
    const type_desc *type = value->type == PIN_STRUCT && value->ptr &&
                                    pin->type_name && pin->container == PIN_CONTAINER_SINGLE
                              ? registry_type_by_name(pin->type_name)
                              : NULL;
    pin_value field;
    for (int i = 0; type && i < NAMED_FIELD_COUNT; i++) {
        if (!strcmp(NAMED_FIELDS[i].component, type->name) &&
            pin_read_field_path(value->ptr, type, NAMED_FIELDS[i].field, &field) == 0 &&
            field.str && field.str[0]) {
            add_named(s->index, place_of(s, pin->id), i, field.str);
        }
    }
    if (type && !strcmp(type->name, MESH_PARTS) &&
        pin_read_field_path(value->ptr, type, "parts", &field) == 0 &&
        field.str && field.str[0]) {
        add_mesh_parts(s->index, place_of(s, pin->id), field.str);
    }
}

/* Variable, local and component bindings read what they name. */
static void add_binding(const site *s, const pin_spec *pin)
{
    const pin_binding *b = pin->binding;
    switch (b->kind) {
        case PIN_BINDING_VARIABLE:
            add_at(s, REFERENCE_READ, own_symbol(s, REFERENCE_SYMBOL_VARIABLE, 0, b->variable_id), pin->id);
            return;
        case PIN_BINDING_LOCAL:
            add_at(
                s,
                REFERENCE_READ,
                own_symbol(s, REFERENCE_SYMBOL_LOCAL, s->graph->graph_id, b->local_id),
                pin->id
            );
            return;
        case PIN_BINDING_COMPONENT:
            add_at(
                s,
                REFERENCE_READ,
                (reference_symbol){ .kind = REFERENCE_SYMBOL_COMPONENT,
                                    .asset = -1,
                                    .name = component_name(b->component_key) },
                pin->id
            );
            return;
        case PIN_BINDING_COMPONENT_FIELD:
            add_at(
                s,
                REFERENCE_READ,
                field_symbol(component_name(b->component_key), b->field_name),
                pin->id
            );
            return;
        case PIN_BINDING_DELTA_TIME:
        case PIN_BINDING_EVENT_OUTPUT: return;
    }
}

static void add_node(const site *s)
{
    const node *n = s->node;
    if (!strcmp(n->desc->name, "Model Instance") && n->reference_asset &&
        n->reference_asset[0]) {
        int asset = named_asset(s->index, REFERENCE_ASSET_SHAPE, n->reference_asset);
        add_at(s, REFERENCE_CALL, (reference_symbol){ .kind = REFERENCE_SYMBOL_ASSET, .asset = asset }, 0);
        for (int i = 0; i < n->input_count; i++) {
            const pin_spec *pin = &n->input_pins[i];
            if (pin->declaration_id) {
                add_at(
                    s,
                    REFERENCE_ARGUMENT,
                    (reference_symbol){ .kind = REFERENCE_SYMBOL_PARAMETER,
                                        .asset = asset,
                                        .id = pin->declaration_id },
                    pin->id
                );
            }
        }
        for (int i = 1; i < n->output_count; i++) {
            const pin_spec *pin = &n->output_pins[i];
            if (pin->declaration_id) {
                add_at(
                    s,
                    REFERENCE_ARGUMENT,
                    (reference_symbol){ .kind = REFERENCE_SYMBOL_PARAMETER,
                                        .asset = asset,
                                        .id = pin->declaration_id },
                    pin->id
                );
            }
        }
    } else if (!strcmp(n->desc->name, "Model Material Slot") && n->reference_asset && n->reference_asset[0]) {
        int asset = named_asset(s->index, REFERENCE_ASSET_MATERIAL, n->reference_asset);
        add_at(s, REFERENCE_CALL, (reference_symbol){ .kind = REFERENCE_SYMBOL_ASSET, .asset = asset }, 0);
    } else if (graph_call(n)) {
        add_call(s);
    }
    if (!strcmp(n->desc->name, "Model Parameter") ||
        !strcmp(n->desc->name, "Model Material Slot") ||
        !strcmp(n->desc->name, "Model Anchor")) {
        add_definition(
            s,
            own_symbol(s, REFERENCE_SYMBOL_PARAMETER, 0, n->id),
            n->reference_name ? n->reference_name : "",
            0
        );
    }
    if (n->desc == &NODE_GetVariable || n->desc == &NODE_SetVariable) {
        reference_role role = n->desc == &NODE_SetVariable ? REFERENCE_WRITE
                                                           : REFERENCE_READ;
        add_at(s, role, own_symbol(s, REFERENCE_SYMBOL_VARIABLE, 0, n->reference_id), 0);
    }
    add_event(s);
    add_spawn(s);
    add_component_access(s);
    for (int i = 0; i < n->input_count; i++) {
        const pin_spec *pin = &n->input_pins[i];
        if (pin->kind == PIN_DATA && pin->binding) {
            add_binding(s, pin);
        }
        if (pin->kind == PIN_DATA && pin->type == PIN_STR && pin->type_name &&
            !strcmp(pin->type_name, "MaterialAsset") &&
            pin->default_value.str && pin->default_value.str[0]) {
            int asset = named_asset(
                s->index,
                REFERENCE_ASSET_MATERIAL,
                pin->default_value.str
            );
            add_at(
                s,
                REFERENCE_BIND,
                (reference_symbol){ .kind = REFERENCE_SYMBOL_ASSET, .asset = asset },
                pin->id
            );
        }
        if (pin->kind == PIN_DATA && pin->type == PIN_STRUCT) {
            add_struct_default(s, pin);
        }
    }
}

/* A function's or macro's Entry outputs and Return inputs declare its
 * parameters and results. */
static void add_signature(site *s, uint64_t node_id, bool output)
{
    s->node = graph_node_by_id(s->graph, node_id);
    const node *n = s->node;
    for (int i = 0; n && i < (output ? n->output_count : n->input_count); i++) {
        const pin_spec *pin = output ? &n->output_pins[i] : &n->input_pins[i];
        if (graph_signature_declared(s->graph, pin)) {
            add_definition(
                s,
                own_symbol(s, REFERENCE_SYMBOL_PARAMETER, s->graph->graph_id, pin->id),
                pin->name,
                pin->id
            );
        }
    }
}

/* Wires from Entry read its parameters. */
static void add_parameter_reads(site *s)
{
    const graph *g = s->graph;
    const node *entry = graph_node_by_id(g, g->entry_id);
    for (int i = 0; entry && i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        const pin_spec *pin = e->kind == GRAPH_EDGE_DATA && e->from_id == g->entry_id
                                ? output_by_id(entry, e->from_pin_id)
                                : NULL;
        s->node = graph_node_by_id(g, e->to_id);
        if (pin && s->node && graph_signature_declared(g, pin)) {
            add_at(
                s,
                REFERENCE_READ,
                own_symbol(s, REFERENCE_SYMBOL_PARAMETER, g->graph_id, pin->id),
                e->to_pin_id
            );
        }
    }
}

static bool holds(const uint64_t *ids, uint64_t id)
{
    for (int i = 0; i < acount(ids); i++) {
        if (ids[i] == id) {
            return true;
        }
    }
    return false;
}

/* The node a wire leads to from `from` in a run: an exec wire's target, or a
 * pure node feeding it data. 0 for neither. */
static uint64_t run_step(const graph *g, const edge *e, uint64_t from)
{
    if (e->kind == GRAPH_EDGE_EXEC) {
        return e->from_id == from ? e->to_id : 0;
    }
    const node *feed = e->to_id == from ? graph_node_by_id(g, e->from_id) : NULL;
    return feed && node_behavior_for(feed) == NODE_BEHAVIOR_PURE ? feed->id : 0;
}

static void add_run(reference_index *index, int source, const graph *g, const node *event)
{
    event_run run = { source, g->graph_id, event->id, NULL };
    apush(run.nodes, event->id);
    for (int at = 0; at < acount(run.nodes); at++) {
        uint64_t from = run.nodes[at];
        for (int i = 0; i < acount(g->edges); i++) {
            uint64_t next = run_step(g, &g->edges[i], from);
            if (next && !holds(run.nodes, next)) {
                apush(run.nodes, next);
            }
        }
    }
    apush(index->runs, run);
}

static void add_graph(reference_index *index, int source, const graph *g)
{
    site s = { index, source, g, NULL };
    if (g->kind != GRAPH_EVENT) {
        s.node = graph_node_by_id(g, g->entry_id);
        add_definition(&s, own_symbol(&s, REFERENCE_SYMBOL_GRAPH, 0, g->graph_id), g->name, 0);
        add_signature(&s, g->entry_id, true);
        add_signature(&s, g->return_id, false);
        add_parameter_reads(&s);
    }
    for (int i = 0; i < acount(g->locals); i++) {
        const graph_local *local = &g->locals[i];
        s.node = graph_node_by_id(g, local->source_id);
        add_definition(
            &s,
            own_symbol(&s, REFERENCE_SYMBOL_LOCAL, g->graph_id, local->id),
            local->name,
            local->source_pin_id
        );
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        s.node = g->nodes[i];
        if (s.node && s.node->desc) {
            add_node(&s);
        }
        if (s.node && s.node->desc && !strcmp(s.node->desc->name, "Event")) {
            add_run(index, source, g, s.node);
        }
    }
}

void reference_index_add_graphs(reference_index *index, int source, const behavior_asset *asset)
{
    site s = { index, source, NULL, NULL };
    for (int i = 0; i < acount(asset->variables); i++) {
        add_definition(
            &s,
            own_symbol(&s, REFERENCE_SYMBOL_VARIABLE, 0, asset->variables[i].id),
            asset->variables[i].name,
            0
        );
    }
    if (asset->domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION) {
        const char *name = index->assets[source].name;
        add_at(
            &s,
            REFERENCE_DEFINE,
            (reference_symbol){ .kind = REFERENCE_SYMBOL_EVENT, .asset = source, .name = name },
            0
        );
    }
    /* Expression graphs are generated from their source text. */
    for (int i = 0; i < acount(asset->graphs); i++) {
        if (asset->graphs[i] && !asset->graphs[i]->math_expression) {
            add_graph(index, source, asset->graphs[i]);
        }
    }
}

/* ---------- Prototypes and scene objects ---------- */

/* An override's components: the object itself, or its components. */
static av_value *override_components(av_value *root)
{
    av_value *components = av_obj_get(root, "components");
    return components ? components : root;
}

/* An override names a component by name, or by its GAME. key. */
static av_value *override_component(av_value *components, const char *name)
{
    char key[128];
    snprintf(key, sizeof key, "GAME.%s", name);
    av_value *value = av_obj_get(components, name);
    return value ? value : av_obj_get(components, key);
}

/* An override that sets the field, or removes its component. */
static bool overridden(av_value *components, const char *component, const char *field)
{
    av_value *value = override_component(components, component);
    return av_is_null(value) || av_obj_get(value, field);
}

static void add_prototype_fields(
    reference_index *index,
    reference at,
    const game_prototype *prototype,
    av_value *overrides
)
{
    pin_value value;
    for (int i = 0; i < NAMED_FIELD_COUNT; i++) {
        if (!overridden(overrides, NAMED_FIELDS[i].component, NAMED_FIELDS[i].field) &&
            game_prototype_read_field(
                prototype,
                NAMED_FIELDS[i].component,
                NAMED_FIELDS[i].field,
                &value
            ) == 0 &&
            value.str && value.str[0]) {
            add_named(index, at, i, value.str);
        }
    }
    if (!overridden(overrides, MESH_PARTS, "parts") &&
        game_prototype_read_field(prototype, MESH_PARTS, "parts", &value) == 0 &&
        value.str && value.str[0]) {
        add_mesh_parts(index, at, value.str);
    }
}

static void add_override_fields(reference_index *index, reference at, av_value *components)
{
    for (int i = 0; i < NAMED_FIELD_COUNT; i++) {
        av_value *component = override_component(components, NAMED_FIELDS[i].component);
        const char *value = av_get_str(av_obj_get(component, NAMED_FIELDS[i].field));
        if (value && value[0]) {
            add_named(index, at, i, value);
        }
    }
    const char *parts = av_get_str(
        av_obj_get(override_component(components, MESH_PARTS), "parts")
    );
    if (parts && parts[0]) {
        add_mesh_parts(index, at, parts);
    }
}

static void add_behavior(reference_index *index, reference at, const char *behavior)
{
    at.role = REFERENCE_ATTACH;
    at.symbol = asset_symbol(named_asset(index, REFERENCE_ASSET_BEHAVIOR, behavior));
    add(index, at);
}

void reference_index_add_prototype(reference_index *index, int source, const game_prototype *prototype)
{
    reference at = { .source = source };
    for (int i = 0; i < acount(prototype->graphs); i++) {
        add_behavior(index, at, prototype->graphs[i]);
    }
    add_prototype_fields(index, at, prototype, NULL);
}

void reference_index_add_object(reference_index *index, int source, const reference_object *object)
{
    reference at = { .source = source, .object = object->name };
    reference use = at;
    use.role = REFERENCE_USE;
    use.symbol = asset_symbol(
        named_asset(index, REFERENCE_ASSET_PROTOTYPE, object->prototype)
    );
    add(index, use);
    for (int i = 0; i < object->behavior_count; i++) {
        add_behavior(index, at, object->behaviors[i]);
    }
    av_value *components = override_components(object->overrides);
    if (object->base) {
        add_prototype_fields(index, at, object->base, components);
    }
    add_override_fields(index, at, components);
}

void reference_index_add_ui(reference_index *index, int source, const char *rml)
{
    reference at = { .source = source };
    char **names = NULL;
    ui_document_use_names(rml, &names);
    at.role = REFERENCE_USE;
    for (int i = 0; i < acount(names); i++) {
        at.symbol = asset_symbol(named_asset(index, REFERENCE_ASSET_UI, names[i]));
        add(index, at);
        free(names[i]);
    }
    afree(names);
    ui_document_event *events = NULL;
    ui_document_events(rml, &events);
    at.role = REFERENCE_SEND;
    for (int i = 0; i < acount(events); i++) {
        at.symbol = event_symbol(index, events[i].name);
        add(index, at);
    }
    ui_document_events_free(events);
}

/* ---------- Queries ---------- */

int reference_index_count(const reference_index *index)
{
    return acount(index->references);
}

const reference *reference_index_at(const reference_index *index, int position)
{
    assert(position >= 0 && position < acount(index->references));
    return &index->references[position];
}

int reference_dependency(const reference *r)
{
    bool named = r->symbol.kind == REFERENCE_SYMBOL_ASSET ||
                 r->symbol.kind == REFERENCE_SYMBOL_GRAPH ||
                 r->symbol.kind == REFERENCE_SYMBOL_EVENT;
    return named && r->role != REFERENCE_DEFINE && r->symbol.asset != r->source
             ? r->symbol.asset
             : -1;
}

static bool same_text(const char *a, const char *b)
{
    return a && b && !strcmp(a, b);
}

/* Whether a reference names the symbol, its definition included. */
static bool names(const reference *r, reference_symbol symbol)
{
    const reference_symbol *own = &r->symbol;
    switch (symbol.kind) {
        case REFERENCE_SYMBOL_ASSET:
            return symbol.asset >= 0 && reference_dependency(r) == symbol.asset;
        case REFERENCE_SYMBOL_GRAPH:
        case REFERENCE_SYMBOL_VARIABLE:
        case REFERENCE_SYMBOL_PARAMETER:
        case REFERENCE_SYMBOL_LOCAL:
            return own->kind == symbol.kind && own->asset == symbol.asset &&
                   own->graph_id == symbol.graph_id && own->id == symbol.id;
        case REFERENCE_SYMBOL_EVENT:
            return own->kind == symbol.kind && same_text(own->name, symbol.name);
        case REFERENCE_SYMBOL_COMPONENT:
            return (own->kind == symbol.kind || own->kind == REFERENCE_SYMBOL_FIELD) &&
                   same_text(own->name, symbol.name);
        case REFERENCE_SYMBOL_FIELD:
            return own->kind == symbol.kind && same_text(own->name, symbol.name) &&
                   same_text(own->field, symbol.field);
    }
    return false;
}

void reference_index_find_uses(
    const reference_index *index,
    reference_symbol symbol,
    const reference ***uses
)
{
    for (int i = 0; i < acount(index->references); i++) {
        const reference *r = &index->references[i];
        if (r->role != REFERENCE_DEFINE && names(r, symbol)) {
            apush(*uses, r);
        }
    }
}

int reference_index_count_uses(const reference_index *index, reference_symbol symbol, unsigned roles)
{
    int count = 0;
    for (int i = 0; i < acount(index->references); i++) {
        const reference *r = &index->references[i];
        count += (roles & REFERENCE_ROLE(r->role)) && names(r, symbol);
    }
    return count;
}

const reference *reference_index_definition(const reference_index *index, reference_symbol symbol)
{
    for (int i = 0; i < acount(index->references); i++) {
        const reference *r = &index->references[i];
        if (r->role == REFERENCE_DEFINE && names(r, symbol)) {
            return r;
        }
    }
    return NULL;
}

/* Both references belong to the same prototype, or the same scene object. */
static bool same_holder(const reference *a, const reference *b)
{
    return a->source == b->source &&
           (a->object ? same_text(a->object, b->object) : !b->object);
}

static bool attaches(const reference *r, int behavior)
{
    return r->role == REFERENCE_ATTACH &&
           r->symbol.kind == REFERENCE_SYMBOL_ASSET && r->symbol.asset == behavior;
}

/* A GameUi.document naming a ui asset. */
static bool shows_document(const reference_index *index, const reference *r)
{
    return r->role == REFERENCE_BIND && r->component &&
           !strcmp(r->component, "GameUi.document") && r->symbol.asset >= 0 &&
           index->assets[r->symbol.asset].kind == REFERENCE_ASSET_UI;
}

static bool listed(const int *positions, int position)
{
    for (int i = 0; i < acount(positions); i++) {
        if (positions[i] == position) {
            return true;
        }
    }
    return false;
}

void reference_index_attached_documents(const reference_index *index, int behavior, int **documents)
{
    const reference **holders = NULL;
    for (int i = 0; i < acount(index->references); i++) {
        if (attaches(&index->references[i], behavior)) {
            apush(holders, &index->references[i]);
        }
    }
    for (int i = 0; acount(holders) && i < acount(index->references); i++) {
        const reference *r = &index->references[i];
        bool held = false;
        for (int h = 0; !held && shows_document(index, r) && h < acount(holders); h++) {
            held = same_holder(r, holders[h]);
        }
        if (held && !listed(*documents, r->symbol.asset)) {
            apush(*documents, r->symbol.asset);
        }
    }
    afree(holders);
}

bool reference_index_reaches(const reference_index *index, const reference *handle, uint64_t node_id)
{
    assert(handle->role == REFERENCE_HANDLE);
    for (int i = 0; i < acount(index->runs); i++) {
        const event_run *run = &index->runs[i];
        if (run->source == handle->source && run->graph_id == handle->graph_id &&
            run->event_id == handle->node_id) {
            return holds(run->nodes, node_id);
        }
    }
    return false;
}

/* ---------- UI pages (workstream B) ---------- */

void reference_index_add_page(reference_index *index, int source, const char *scene_ui, const char *project_ui)
{
    bool own = scene_ui && scene_ui[0];
    const char *page = own ? scene_ui : project_ui;
    if (!page || !page[0]) {
        return;
    }
    reference at = {
        .role = REFERENCE_BIND,
        .source = source,
        .component = own ? REFERENCE_SCENE_PAGE : REFERENCE_PROJECT_PAGE,
    };
    at.symbol = asset_symbol(named_asset(index, REFERENCE_ASSET_UI, page));
    add(index, at);
}
