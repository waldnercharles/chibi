#ifndef CHIBI_AUTHORING_PREVIEW_H
#define CHIBI_AUTHORING_PREVIEW_H

#include "authoring/preview_runtime.h"
#include "graph/compiler/validation.h"
#include "graph/model/behavior_asset.h"
#include "runtime/behaviors/entities.h"
#include "runtime/materials.h"
#include "runtime/scenes/physics.h"
#include "runtime/visibility.h"

#include <stddef.h>

typedef struct preview_world preview_world;
typedef struct preview_source_state preview_source_state;
typedef struct authoring_source_presentation authoring_source_presentation;

typedef struct preview_definition
{
    char *name;
    char *path;
    behavior_asset *working;
    behavior_asset *applied;
    /* Owned last published field document, before library linking. */
    behavior_asset *field_source;
    authoring_source_presentation *working_presentation, *applied_presentation;
    preview_world *world;
    int registered;
    int dirty; /* Working edits not yet running. */
    bool unsaved;
    compile_report diagnostics;
} preview_definition;

struct preview_world
{
    preview_runtime *runtime; /* owned; source documents below are independent */
    preview_definition **definitions;
    preview_source_state *source_state; /* Owned canonical source generations. */
};

preview_definition *preview_definition_new(const char *asset_name, behavior_asset *working);
/* Retains the source view and owns an independent applied presentation copy. */
preview_definition *preview_projection_new(const char *asset_name, authoring_source_presentation *);
/* Replaces one authoring generation; applied views are installed only after
 * canonical module publication. Does not register or compile a graph. */
int preview_projection_set(preview_definition *, authoring_source_presentation *, bool applied);
void preview_definition_free(preview_definition *definition);
preview_definition *preview_find_definition(preview_world *world, const char *asset_name);
const char *preview_definition_path(const preview_definition *definition);
int preview_definition_set_path(preview_definition *definition, const char *path);
int preview_definition_count(const preview_world *world);
preview_definition *preview_definition_at(preview_world *world, int index);
/* Copies source into the working copy. Moving automatic nodes leaves the
   definition saved and applied. */
int preview_sync_working(preview_definition *definition, const behavior_asset *source);
int preview_mark_dirty(preview_definition *definition);

preview_world *preview_world_new(ecs_world *ecs);
void preview_world_free(preview_world *world);
int preview_add_definition(preview_world *world, preview_definition *definition);
/* Adds an authoring projection after canonical module publication. The view
 * retains its own snapshot and never supplies executable graph state. */
int preview_add_projection(preview_world *, preview_definition *);
int preview_remove_definition(preview_world *world, const char *asset_name);
/* Removes only a canonical authoring view, preserving its live source module. */
int preview_remove_projection(preview_world *, const char *asset_name);
/* Renames a definition in place: its runtime registration and attachments
   follow. Returns -1 when the name is taken. */
int preview_rename_definition(preview_world *world, const char *from, const char *to);
int preview_can_update_library(
    preview_world *world,
    preview_definition *definition,
    char *message,
    size_t size
);
int preview_apply(preview_world *world, preview_definition *definition, char *message, size_t message_size);

/* Publishes nonbehavior graph artifacts; source projections use the source collection. */
int preview_update(preview_world *world, preview_definition *definition, char *message, size_t message_size);
/* What applying source would run: libraries linked from the running
   definitions and component nodes lowered. NULL when it does not link. */
behavior_asset *preview_link(preview_world *world, const behavior_asset *source);

#endif
