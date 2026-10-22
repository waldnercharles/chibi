#ifndef CHIBI_PROTOTYPE_H
#define CHIBI_PROTOTYPE_H

#include "assets/scene_document.h"
#include "ecs/ecs.h"
#include "support/asset_value.h"

#include <stddef.h>

/* The longest name a prototype may declare, in bytes. */
#define PROTOTYPE_NAME_MAX 1024
/* The most prototype documents one load, or the loads a project prepares
 * together, may read through includes and children, so a small set of
 * documents cannot expand without bound. This is independent of the prepared
 * scene item and storage budgets. */
#define PROTOTYPE_EXPANSION_MAX 16384
/* The most bytes of those documents the same loads may read, the most a
 * prepared player artifact holds, so many instances of one large document
 * cannot multiply its size without bound. */
#define PROTOTYPE_READ_MAX (64 * 1024 * 1024)

typedef const char *(*prototype_include_fn)(const char *name, void *user);

/* Borrowed typed rows matching the registered descriptor's native layout.
 * Adding a component copies its fields and retains managed strings. */
typedef struct
{
    ecs_component_id component;
    const void *data;
    size_t size;
} prototype_default;

typedef struct prototype_registry prototype_registry;
typedef av_doc *(*prototype_decode_fn)(const char *, const prototype_registry *, char *, size_t);
struct prototype_registry
{
    const ecs_component_desc *const *components;
    int component_count;
    prototype_include_fn include;
    void *include_user;
    const prototype_default *defaults;
    int default_count;
    const ecs_relationship_desc *const *relationships;
    int relationship_count;
    /* Optional authoring decoder. Runtime/document-only users retain KDL. */
    prototype_decode_fn decode;
};

typedef int (*prototype_attach_fn)(ecs_world *world, ecs_entity entity, const char *graph_name, void *user);

typedef struct
{
    prototype_attach_fn attach;
    void *attach_user;
} prototype_spawn_hooks;

typedef struct
{
    const ecs_component_desc *desc;
    void *data;
} prototype_component;

typedef struct
{
    ecs_component_desc desc;
    char *name;
} prototype_tag;

/* A handle field authored as an entity path, which holds zero until a scene
 * or spawn resolves it. The '.'-separated path names an entity from the roots
 * of the document that authored the value: this prototype's children, or,
 * when `outer`, the entities of the scene or parent prototype that holds it as
 * an entity. */
typedef struct
{
    const ecs_component_desc *component;
    int field;  /* top-level, in the component's type */
    char *path; /* owned */
    bool outer;
} prototype_link;

typedef struct
{
    const ecs_relationship_desc *relationship;
    char *path;
} prototype_pair;

typedef struct prototype
{
    char *name;
    char *identity;
    /* Registry value is owned; descriptors, default rows and include context
     * remain borrowed for subsequent authoring operations. */
    prototype_registry registry_storage;
    const prototype_registry *registry;
    prototype_tag tag;
    prototype_component *components;
    /* Authoring-only values; never included when spawning runtime components. */
    prototype_component *detached_components;
    char **graphs;
    char **includes; /* the prototypes its document includes, in order */
    /* Loaded as authored: it holds only its own content, so saving names its
     * includes again. */
    bool authored;
    /* Its children, nested in the scene entity shape: the document's roots
     * are this prototype's direct children. NULL when it has none. */
    scene_document *children;
    /* Loaded with an include resolver, each of `children`'s entities as it
     * spawns: its prototype with the child's components and behaviors, named
     * as the child. Empty for an authored load. */
    struct prototype **child_prototypes;
    /* Its handle fields that name entities, attached or detached. */
    prototype_link *links;
    prototype_pair *pairs;
    int expanded; /* the other documents its load read, at most PROTOTYPE_EXPANSION_MAX */
    size_t read; /* their bytes, at most PROTOTYPE_READ_MAX */
    char **strings;
} prototype;

/* The component a document names by stable key or reflected name, or NULL. */
const ecs_component_desc *prototype_registry_find(const prototype_registry *registry, const char *name);
prototype *prototype_load(const char *value, const prototype_registry *registry, char *error, size_t error_size);
/* Only what the document itself authors: its includes are named in
 * `includes`, and its children's prototypes in `children`, but neither is
 * loaded, so no resolver is needed. For indexing a
 * prototype's direct references; its components are not what it spawns. */
prototype *prototype_load_authored(
    const char *text,
    const prototype_registry *registry,
    char *error,
    size_t error_size
);
/* Applies the entity's component values from the document that holds the
 * prototype as an entity, so the paths they name are `outer`. */
int prototype_apply_entity_components(prototype *prototype, av_value *components, char *error, size_t error_size);
/* The entity paths a document's component values name, as `outer` links; the
 * caller frees them with prototype_links_free. */
prototype_link *prototype_components_links(const prototype_registry *registry, av_value *components);
void prototype_links_free(prototype_link *links);
/* A prototype named `name` with no components or behaviors. */
prototype *prototype_new(const char *name);
prototype *prototype_clone(const prototype *prototype, char *error, size_t error_size);
const char *prototype_identity(const prototype *prototype);
const ecs_component_desc *prototype_tag_desc(const prototype *prototype);
prototype *prototype_capture_entity(
    const char *name,
    const ecs_world *world,
    ecs_entity entity,
    const prototype_registry *registry
);
void prototype_free(prototype *prototype);
char *prototype_save(const prototype *prototype);
av_value *prototype_to_value(const prototype *prototype);
prototype *prototype_load_value(
    av_value *value,
    const prototype_registry *registry,
    char *error,
    size_t error_size
);
av_value *prototype_components_value(const prototype *prototype);
/* Owned single-component native value, including reflected nested fields. */
av_value *prototype_entity_component_value(
    const ecs_world *world,
    ecs_entity entity,
    const ecs_component_desc *desc,
    const prototype_registry *registry
);
/* Sparse fields, full added components, and null removed components, using
 * the same semantics as capturing an entity. A handle overrides only where
 * `current` names another entity path, so `current` may be a captured
 * entity's. Caller owns the returned native value. */
av_value *prototype_overrides_value(const prototype *base, const prototype *current);
av_value *prototype_capture_overrides_value(const prototype *base, const ecs_world *world, ecs_entity entity);
/* Entity paths leave their handles zero; prototype_components_links
 * lists them for the caller to resolve. */
int prototype_apply_components_value(
    ecs_world *world,
    ecs_entity entity,
    av_value *components,
    const prototype_registry *registry,
    char ***owned_strings,
    char *error,
    size_t error_size
);
/* Free strings retained by prototype_apply_components_value after their
 * ECS component values are no longer used. */
void prototype_owned_strings_free(char **strings);
/* Allocate authored components without running attached behaviors. */
int prototype_spawn_components(
    const prototype *prototype,
    ecs_world *world,
    ecs_entity *entity,
    char *error,
    size_t error_size
);
int prototype_spawn(
    const prototype *prototype,
    ecs_world *world,
    const prototype_spawn_hooks *hooks,
    ecs_entity *out_entity,
    char *error,
    size_t error_size
);

int prototype_read_field(
    arena *storage,
    const prototype *prototype,
    const char *component_name,
    const char *field_name,
    pin_value *out
);
int prototype_write_field(
    prototype *prototype,
    const char *component_name,
    const char *field_name,
    pin_value value
);
/* Apply one reflected native value field using the same conversion rules as loading a
 * prototype document. The value is borrowed for the duration of the call. */
int prototype_write_field_value(
    prototype *prototype,
    const char *component_name,
    const char *field_name,
    av_value *value,
    char *error,
    size_t error_size
);
/* Restores detached values, otherwise uses the first supplied typed default
 * row for this component, or zero initialization when the registry omits it. */
int prototype_add_component(prototype *prototype, const ecs_component_desc *desc);
int prototype_remove_component(prototype *prototype, const char *component_name);
/* Detach retains values for add_component to restore, including after save/load. */
int prototype_detach_component(prototype *prototype, const char *component_name);
/* Single-component objects from entity_component_value. Defaults may be {} when
 * the component is local or no longer in the prototype. The owned result keeps
 * local edits and adopts current defaults for unchanged fields, recursively. */
av_value *prototype_rebase_component_value(
    av_value *values,
    av_value *previous_defaults,
    av_value *current_defaults
);
/* Owned single-component object; {} if this prototype has no such component. */
av_value *prototype_component_value(const prototype *prototype, const char *name);
/* Owned default component values shared by prototype and scene attachment. */
av_value *prototype_component_defaults_value(
    const prototype_registry *registry,
    const ecs_component_desc *desc
);
int prototype_add_graph(prototype *prototype, const char *asset_name);
int prototype_remove_graph(prototype *prototype, const char *asset_name);
int prototype_has_graph(const prototype *prototype, const char *asset_name);

#endif
