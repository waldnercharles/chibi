#ifndef CHIBI_REFERENCE_INDEX_H
#define CHIBI_REFERENCE_INDEX_H

#include <stdbool.h>
#include <stdint.h>

/* Where a project's authored assets name each other and the symbols inside
 * them, for find references, go to definition, rename and use counts. Studio
 * and native inspection each build one from their copy of the project's assets.
 *
 * Add every asset first, then resolved references from its owning source or
 * native document adapter. Each reference records the asset holding it, its
 * node and pin when a graph holds it, its role, and the symbol it names. A name
 * no added asset has becomes an unresolved asset of its kind. Events are
 * symbols by name: an event belongs to an asset only when an event definition
 * of that name was added, since host, action and ui events have none. */
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
    REFERENCE_ASSET_RELATIONSHIP,
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
 * - COMPONENT: name, as Health. FIELD adds field, a dot path.
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
    int source_start,
        source_end; /* Exact executable source bytes; zero for native graphs. */
    const char *graph;     /* that graph's name; NULL when unnamed */
    const char *pin;       /* that pin's name; NULL without a pin */
    const char *object;    /* the scene object holding it */
    const char *component; /* the component field holding it, as Ui.document */
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
/* Finds a named asset or adds its unresolved record. */
int reference_index_named_asset(reference_index *, reference_asset_kind, const char *name);
/* The kind of asset a component field's text names, as Ui.document names
 * a ui asset and SpawnRequest.prototype a prototype; REFERENCE_ASSET_KIND_COUNT
 * for a field that names none. `component` is its type's name, as Ui. */
reference_asset_kind reference_named_field_kind(const char *component, const char *field);
typedef struct
{
    const char *component, *field;
    reference_asset_kind kind;
    reference_role role;
} reference_named_field_entry;
int reference_named_field_count();
/* Borrowed constant metadata; position must be in [0, count). */
reference_named_field_entry reference_named_field_at(int position);
/* Appends the relationship declared by this named field. Shader.material may
 * name Library.Function: it binds the library's graph, resolved when its
 * definition is added, regardless of reference/definition insertion order. */
void reference_index_add_named_field(
    reference_index *,
    reference at,
    const char *component,
    const char *field,
    const char *value
);

/* Appends a resolved reference and owns copies of its strings. */
void reference_index_add_reference(reference_index *, reference);
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
/* The ui assets shown on the entities `behavior` is attached to: the
 * Ui.document of each prototype and scene object that attaches it, whose
 * events reach the behavior. Appends their positions once each to
 * *documents (support/array.h). */
void reference_index_attached_documents(const reference_index *index, int behavior, int **documents);
/* Whether the run a handler starts reaches a node of its graph: along exec
 * wires from its Event node, and through the pure nodes feeding what runs.
 * handle is the Event node's HANDLE reference. */
bool reference_index_reaches(const reference_index *index, const reference *handle, uint64_t node_id);
/* Copies graph-neutral reachability facts. Node IDs remain owned by the caller;
 * index storage is independent after this call. */
void reference_index_add_reachability(
    reference_index *,
    int source,
    uint64_t graph,
    uint64_t event,
    const uint64_t *nodes,
    int count
);

/* ---------- UI pages (workstream B) ---------- */

/* The component a page reference names: the scene's own ui:, or the
 * project's, which a scene naming none shows its UI on. */
#define REFERENCE_SCENE_PAGE "ui"
#define REFERENCE_PROJECT_PAGE "project ui"

/* A scene's page, its root ui document: `scene_ui` when the scene names
 * one, else `project_ui`, the project's default; with neither, nothing. It
 * binds the ui asset, and its component says which of the two names it. */
void reference_index_add_page(reference_index *index, int source, const char *scene_ui, const char *project_ui);

#endif
