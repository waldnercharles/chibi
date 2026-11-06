#ifndef CHIBI_STUDIO_SCHEMA_H
#define CHIBI_STUDIO_SCHEMA_H

/* Neutral Studio service and result records. The capability definition pack
 * declares their signatures; Studio binds the service implementation. Objects
 * are addressed by authored keys carried in panel rows. */

#include "support/arena.h"
#include "types/catalog.h"
#include "types/value.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum studio_node_op
{
    STUDIO_NODE_SCENE_OBJECTS,
    STUDIO_NODE_SCENE_INFO,
    STUDIO_NODE_SELECTION,
    STUDIO_NODE_OBJECT_SELECTED,
    STUDIO_NODE_OBJECT_ROW,
    STUDIO_NODE_RENAMING,
    STUDIO_NODE_EDITOR_STATE,
    STUDIO_NODE_SPAWN_BREAK,
    STUDIO_NODE_PROTOTYPE_CHOICES,
    STUDIO_NODE_SELECT_OBJECT,
    STUDIO_NODE_RENAME_OBJECT,
    STUDIO_NODE_END_RENAME,
    STUDIO_NODE_SET_HIDDEN,
    STUDIO_NODE_SET_LOCKED,
    STUDIO_NODE_ISOLATE,
    STUDIO_NODE_ADD_OBJECT,
    STUDIO_NODE_DROP_ON_OBJECT,
    STUDIO_NODE_MOVE_OBJECTS,
    STUDIO_NODE_SET_FILTER,
    STUDIO_NODE_TOGGLE_SPAWN_BREAK,
    STUDIO_NODE_COUNT,
} studio_node_op;

struct studio;

/* Runs node `op` on Studio: a read fills `out` from `in`, its strings and
 * arrays in `storage`; an effect (NULL `storage` and `out`) acts on `in`.
 * Returns an error message, or NULL. */
typedef const char *(*studio_node_fn)(
    struct studio *s,
    studio_node_op op,
    const pin_value *in,
    arena *storage,
    pin_value *out
);

/* The "studio" service: Studio and the call that runs its nodes. */
typedef struct studio_node_service
{
    struct studio *studio;
    studio_node_fn run;
} studio_node_service;

/* An object as a row of the scene's hierarchy, shaped for TreeRow: the
   rest of a multi-selection is `marked`, the active object `active`. */
typedef struct studio_object_row
{
    const char *key, *label, *icon, *prototype, *drop;
    int depth, behaviors;
    bool active, marked, hidden, locked, renaming;
} studio_object_row;

/* An option a ChoiceList lists. */
typedef struct studio_choice
{
    const char *value, *label, *icon;
} studio_choice;

#define STUDIO_OBJECT_ROW_TYPE "SceneObjectRow"
#define STUDIO_CHOICE_TYPE "Choice"

/* Optional Studio reflected payloads; application composition owns registration. */
extern const type_catalog_pack STUDIO_TYPE_PACK;

#endif
