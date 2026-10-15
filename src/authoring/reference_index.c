#include "authoring/reference_index.h"

#include "runtime/events.h"
#include "support/array.h"
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

static const reference_named_field_entry REFERENCE_NAMED_FIELDS[] = {
    { "MaterialTerrain", "field", REFERENCE_ASSET_FIELD, REFERENCE_BIND },
    { "EngineSystem", "asset", REFERENCE_ASSET_SYSTEM, REFERENCE_BIND },
    { "Shader", "material", REFERENCE_ASSET_MATERIAL, REFERENCE_BIND },
    { "Shader", "shape", REFERENCE_ASSET_SHAPE, REFERENCE_BIND },
    { "Ui", "document", REFERENCE_ASSET_UI, REFERENCE_BIND },
    { "SpawnRequest", "prototype", REFERENCE_ASSET_PROTOTYPE, REFERENCE_SPAWN },
};

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
    return host_event_reserved(name)
             ? -1
             : reference_index_find_name(index, REFERENCE_ASSET_EVENT, name);
}

/* The asset a name refers to, unresolved when none was added. */
int reference_index_named_asset(reference_index *index, reference_asset_kind kind, const char *name)
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

static void reference_append(reference_index *index, reference r)
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

void reference_index_add_reference(reference_index *index, reference r)
{
    reference_append(index, r);
}

static reference_symbol reference_asset_symbol(int asset)
{
    return (reference_symbol){ .kind = REFERENCE_SYMBOL_ASSET, .asset = asset };
}

static reference_symbol reference_event_symbol(const reference_index *index, const char *name)
{
    return (reference_symbol){ .kind = REFERENCE_SYMBOL_EVENT,
                               .asset = reference_index_event_asset(index, name),
                               .name = name };
}

/* What REFERENCE_NAMED_FIELDS[field] holds, value, names: an asset, or a sent
 * event. `at` gives the reference's source and place. */
static void reference_add_named(reference_index *index, reference at, int field, const char *value)
{
    char component[128];
    snprintf(
        component,
        sizeof component,
        "%s.%s",
        REFERENCE_NAMED_FIELDS[field].component,
        REFERENCE_NAMED_FIELDS[field].field
    );
    reference_asset_kind kind = REFERENCE_NAMED_FIELDS[field].kind;
    at.role = REFERENCE_NAMED_FIELDS[field].role;
    at.symbol = kind == REFERENCE_ASSET_EVENT
                  ? reference_event_symbol(index, value)
                  : reference_asset_symbol(reference_index_named_asset(index, kind, value));
    at.component = component;
    reference_append(index, at);
}

static int reference_named_field(const char *component, const char *field)
{
    for (int i = 0; i < reference_named_field_count(); i++) {
        if (!strcmp(REFERENCE_NAMED_FIELDS[i].component, component) &&
            !strcmp(REFERENCE_NAMED_FIELDS[i].field, field)) {
            return i;
        }
    }
    return -1;
}

reference_asset_kind reference_named_field_kind(const char *component, const char *field)
{
    int named = reference_named_field(component, field);
    return named < 0 ? REFERENCE_ASSET_KIND_COUNT
                     : REFERENCE_NAMED_FIELDS[named].kind;
}

static bool reference_run_contains(const uint64_t *ids, uint64_t id)
{
    for (int i = 0; i < acount(ids); i++) {
        if (ids[i] == id) {
            return true;
        }
    }
    return false;
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

/* A Ui.document naming a ui asset. */
static bool shows_document(const reference_index *index, const reference *r)
{
    return r->role == REFERENCE_BIND && r->component &&
           !strcmp(r->component, "Ui.document") && r->symbol.asset >= 0 &&
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
            return reference_run_contains(run->nodes, node_id);
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
    at.symbol = reference_asset_symbol(
        reference_index_named_asset(index, REFERENCE_ASSET_UI, page)
    );
    reference_append(index, at);
}

int reference_named_field_count()
{
    return (int)(sizeof REFERENCE_NAMED_FIELDS / sizeof *REFERENCE_NAMED_FIELDS);
}
reference_named_field_entry reference_named_field_at(int position)
{
    assert(position >= 0 && position < reference_named_field_count());
    return REFERENCE_NAMED_FIELDS[position];
}
void reference_index_add_named_field(
    reference_index *index,
    reference at,
    const char *component,
    const char *field,
    const char *value
)
{
    int position = reference_named_field(component, field);
    assert(position >= 0);
    reference_add_named(index, at, position, value);
}
void reference_index_add_reachability(
    reference_index *index,
    int source,
    uint64_t graph,
    uint64_t event,
    const uint64_t *nodes,
    int count
)
{
    assert(source >= 0 && source < acount(index->assets));
    assert(count >= 0 && (!count || nodes));
    event_run run = { source, graph, event, NULL };
    if (count) {
        asetlen(run.nodes, count);
        memcpy(run.nodes, nodes, (size_t)count * sizeof *nodes);
    }
    apush(index->runs, run);
}
