#ifndef CHIBI_REFERENCE_INDEX_H
#define CHIBI_REFERENCE_INDEX_H

#include "graph/model/behavior_asset.h"

#include <stdbool.h>
#include <stdint.h>

struct game_prototype;

/* Where a project's authored assets name each other and the symbols inside
 * them, for find references, go to definition, rename and use counts. Studio
 * and the MCP each build one from their copy of the project's assets.
 *
 * Add every asset first, then its content: graphs, prototypes, scene objects
 * and ui documents. Each reference records the asset holding it, its node and
 * pin when a graph holds it, its role, and the symbol it names. A name no
 * added asset has becomes an unresolved asset of its kind. Events are symbols
 * by name: an event belongs to an asset only when an event definition of that
 * name was added, since host, action and ui events have none. */
typedef struct reference_index reference_index;

typedef enum
{
    REFERENCE_ASSET_SCENE,
    REFERENCE_ASSET_PROTOTYPE,
    REFERENCE_ASSET_BEHAVIOR,
    REFERENCE_ASSET_EVENT,
    REFERENCE_ASSET_MATERIAL,
    REFERENCE_ASSET_SHAPE,
    REFERENCE_ASSET_FIELD,
    REFERENCE_ASSET_LIBRARY, /* libraries and shader libraries */
    REFERENCE_ASSET_SYSTEM,
    REFERENCE_ASSET_UI,
    REFERENCE_ASSET_KIND_COUNT,
} reference_asset_kind;

typedef struct
{
    reference_asset_kind kind;
    const char *name;
    const char *path; /* NULL while unsaved or unresolved */
    bool unresolved;  /* named by a reference but never added */
} reference_asset;

/* A symbol, and the fields that identify it:
 * - ASSET: asset.
 * - GRAPH, VARIABLE: asset, id. A graph is a function or macro.
 * - PARAMETER: asset, graph_id (its function), id (its Entry or Return pin).
 * - LOCAL: asset, graph_id, id.
 * - EVENT: name. Its asset is its event definition, or -1.
 * - COMPONENT: name, as GameHealth. FIELD adds field, a dot path.
 * A definition of a graph, variable, parameter or local also has its name. */
typedef enum
{
    REFERENCE_SYMBOL_ASSET,
    REFERENCE_SYMBOL_GRAPH,
    REFERENCE_SYMBOL_VARIABLE,
    REFERENCE_SYMBOL_PARAMETER,
    REFERENCE_SYMBOL_LOCAL,
    REFERENCE_SYMBOL_EVENT,
    REFERENCE_SYMBOL_COMPONENT,
    REFERENCE_SYMBOL_FIELD,
} reference_symbol_kind;

typedef struct
{
    reference_symbol_kind kind;
    int asset; /* the defining asset; -1 for host events, components and fields */
    uint64_t graph_id;
    uint64_t id;
    const char *name;
    const char *field;
} reference_symbol;

typedef enum
{
    REFERENCE_DEFINE,   /* where the symbol is declared */
    REFERENCE_USE,      /* a scene object of a prototype, a <use ui> widget */
    REFERENCE_CALL,     /* a function or macro call */
    REFERENCE_ARGUMENT, /* a call's pin for a parameter or result */
    REFERENCE_READ,
    REFERENCE_WRITE,
    REFERENCE_SEND,
    REFERENCE_HANDLE,
    REFERENCE_SPAWN,
    REFERENCE_ATTACH, /* a behavior on a prototype or scene object */
    REFERENCE_BIND,   /* a component field or pin default names an asset */
    REFERENCE_ROLE_COUNT,
} reference_role;

#define REFERENCE_ROLE(role) (1u << (role))
#define REFERENCE_USES (~REFERENCE_ROLE(REFERENCE_DEFINE))

typedef struct
{
    reference_role role;
    reference_symbol symbol;
    int source; /* the asset holding the reference */
    /* Its node and pin in source's graphs; zero outside graphs. */
    uint64_t graph_id, node_id, pin_id;
    const char *graph;     /* that graph's name; NULL when unnamed */
    const char *pin;       /* that pin's name; NULL without a pin */
    const char *object;    /* the scene object holding it */
    const char *component; /* the component field holding it, as GameUi.document */
} reference;

reference_index *reference_index_new();
void reference_index_free(reference_index *index);
/* Drops every asset and reference. */
void reference_index_clear(reference_index *index);

/* Adds an asset, or updates the one it already is: the asset at the same
 * path; without a path, the asset with the same key (Studio keys an unsaved
 * asset by its tab so it stays one asset while renamed); or an unresolved or
 * unsaved asset of that name. Returns its position. */
int reference_index_add_asset(
    reference_index *index,
    reference_asset_kind kind,
    const char *name,
    const char *path,
    const char *key
);
/* By path, or by key when path is NULL; -1 when absent. */
int reference_index_find_asset(
    const reference_index *index,
    reference_asset_kind kind,
    const char *path,
    const char *key
);
int reference_index_find_name(const reference_index *index, reference_asset_kind kind, const char *name);
/* The event definition an event name belongs to; -1 for host events and
 * events without one. */
int reference_index_event_asset(const reference_index *index, const char *name);
int reference_index_asset_count(const reference_index *index);
const reference_asset *reference_index_asset_at(const reference_index *index, int position);
/* The kind of asset a graph asset of this domain is. */
reference_asset_kind reference_domain_kind(behavior_domain domain);

/* Graph content: definitions of functions, macros, parameters, variables,
 * locals and an event definition's event; calls and their arguments;
 * variable, component and field reads and writes, through nodes and pin
 * bindings; locals and parameters read; events sent and handled; prototypes
 * spawned; and assets pin defaults name, as a GameShader value's material. */
void reference_index_add_graphs(reference_index *index, int source, const behavior_asset *asset);
/* Its behaviors, and the assets and events its component fields name. */
void reference_index_add_prototype(reference_index *index, int source, const struct game_prototype *prototype);

/* A scene object: its prototype, behaviors, and the assets its component
 * fields name. With base, the object also holds what base's fields name,
 * unless its overrides set those fields. */
typedef struct
{
    const char *name;
    const char *prototype;
    const struct game_prototype *base; /* may be NULL */
    const char *overrides;             /* components JSON; may be NULL */
    const char *const *behaviors;
    int behavior_count;
} reference_object;
void reference_index_add_object(reference_index *index, int source, const reference_object *object);
/* A ui document's widgets (<use ui>) and the events it sends (emit('Name'),
 * data-keys). */
void reference_index_add_ui(reference_index *index, int source, const char *rml);

int reference_index_count(const reference_index *index);
const reference *reference_index_at(const reference_index *index, int position);
/* The asset whose content a reference uses, making its source depend on it:
 * an asset it names, a library it calls, an event definition. -1 for none,
 * and for the source's own symbols. */
int reference_dependency(const reference *r);
/* Appends each reference that uses symbol to *uses (support/array.h). An
 * asset's uses are the references that depend on it. A component's include
 * its fields'. Events match by name. */
void reference_index_find_uses(
    const reference_index *index,
    reference_symbol symbol,
    const reference ***uses
);
/* How many references use symbol in one of roles, a mask of REFERENCE_ROLE
 * bits such as REFERENCE_USES. */
int reference_index_count_uses(const reference_index *index, reference_symbol symbol, unsigned roles);
/* Where a graph, variable, parameter, local or event-definition event is
 * declared; NULL for assets, which are their own definition, and for host
 * events, components and fields. */
const reference *reference_index_definition(const reference_index *index, reference_symbol symbol);
/* Whether the run a handler starts reaches a node of its graph: along exec
 * wires from its Event node, and through the pure nodes feeding what runs.
 * handle is the Event node's HANDLE reference. */
bool reference_index_reaches(const reference_index *index, const reference *handle, uint64_t node_id);

#endif
