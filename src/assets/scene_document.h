#ifndef CHIBI_SCENE_DOCUMENT_H
#define CHIBI_SCENE_DOCUMENT_H

#include "ecs/component_catalog.h"
#include "runtime/scenes/physics_settings.h"
#include "support/asset_value.h"

#include <stdbool.h>
#include <stddef.h>

/* Entities nest: each names its parent by index, and a parent always precedes
 * its children, which keep their array order. A name is unique among its
 * siblings, and a path of names joined by '.' names an entity from the roots. */
typedef struct
{
    char *name;
    int parent; /* the parent's index, or -1 for a root */
    char *prototype;
    av_value *components;
    av_value *retained;
    char **behaviors;
    bool own_behaviors; /* behaviors replace the prototype's; otherwise it keeps them */
} scene_entity;

typedef struct scene_document
{
    char *name;
    char *ui; /* the root ui document its UI shows on (render/ui.h), or NULL for the project's */
    scene_entity *entities;
    bool perspective;
    physics_settings physics;
} scene_document;

scene_document *scene_document_new(const char *name);
void scene_document_free(scene_document *document);
int scene_document_set_perspective(scene_document *document, bool perspective);
/* Names the scene's root ui document; NULL or "" leaves the project's. */
void scene_document_set_ui(scene_document *document, const char *ui);
/* The entity a '.'-separated path names, or -1. */
int scene_document_entity_index(const scene_document *document, const char *path);
/* The entity's path from the roots; the caller frees it. */
char *scene_document_entity_path(const scene_document *document, int index);
/* Shared by document edits and Studio's live actors. name_used says a sibling
 * other than the object being renamed has the name. Returns the user-facing
 * rejection in error. */
int scene_entity_validate_name(const char *name, bool name_used, char *error, size_t error_size);
int scene_document_rename_entity(
    scene_document *document,
    int index,
    const char *name,
    char *error,
    size_t error_size
);
/* Objects nest at most this many levels deep, a root being the first, so
 * loading, saving and paths stay bounded. */
#define SCENE_DEPTH_MAX 256
/* Appends a child of parent, or a root when parent is -1, unless that would
 * nest it deeper than SCENE_DEPTH_MAX. The new entity keeps its
 * prototype's behaviors until it sets its own. */
int scene_document_add_entity(scene_document *document, int parent, const char *name, const char *prototype);
/* Removes the entity with its descendants. */
int scene_document_remove_entity(scene_document *document, int index);
int scene_document_set_entity_prototype(scene_document *document, int index, const char *prototype);
int scene_document_set_entity_behaviors(
    scene_document *document,
    int index,
    const char *const *behaviors,
    int behavior_count
);
int scene_document_set_entity_components(scene_document *document, int index, av_value *components);
/* Serialization borrows the supplied component descriptions for precision,
 * color and enum formatting. Unknown components retain their authored values;
 * an empty catalog preserves all component values without reflected formatting.
 * Returned values/text own their storage and outlive the catalog/document. */
/* The document's roots with their children nested, as an array in doc. */
av_value *scene_document_entities_value(av_doc *doc, const scene_document *document, ecs_component_catalog components);
/* Appends the entities of a nested array value, each with its children, as
 * roots. */
int scene_document_read_entities(scene_document *document, av_value *entities, char *error, size_t error_size);
/* Moves every entity of `from` into `document`, its roots staying roots,
 * unless a root's name is taken there. */
int scene_document_take_entities(scene_document *document, scene_document *from, char *error, size_t error_size);
av_value *scene_document_to_value(const scene_document *document, ecs_component_catalog components);
scene_document *scene_document_load_value(av_value *value, char *error, size_t error_size);
char *scene_document_save(const scene_document *document, ecs_component_catalog components);
int scene_document_set_entity_retained(scene_document *document, int index, av_value *value);
scene_document *scene_document_load(const char *json, char *error, size_t error_size);

#endif
