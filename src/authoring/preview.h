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

typedef struct game_preview_world game_preview_world;
typedef struct game_preview_source_state game_preview_source_state;
typedef struct authoring_source_presentation authoring_source_presentation;

typedef struct game_preview_definition
{
    char *name;
    char *path;
    behavior_asset *working;
    behavior_asset *applied;
    authoring_source_presentation *working_presentation, *applied_presentation;
    game_preview_world *world;
    int registered;
    int dirty; /* Working edits not yet running. */
    bool unsaved;
    compile_report diagnostics;
} game_preview_definition;

struct game_preview_world
{
    game_preview_runtime *runtime; /* owned; source documents below are independent */
    game_preview_definition **definitions;
    game_preview_source_state *source_state; /* Owned canonical source generations. */
};

game_preview_definition *game_preview_definition_new(const char *asset_name, behavior_asset *working);
/* Retains the source view and owns an independent applied presentation copy. */
game_preview_definition *game_preview_projection_new(const char *asset_name, authoring_source_presentation *);
/* Replaces one authoring generation; applied views are installed only after
 * canonical module publication. Does not register or compile a graph. */
int game_preview_projection_set(game_preview_definition *, authoring_source_presentation *, bool applied);
void game_preview_definition_free(game_preview_definition *definition);
game_preview_definition *game_preview_find_definition(game_preview_world *world, const char *asset_name);
const char *game_preview_definition_path(const game_preview_definition *definition);
int game_preview_definition_set_path(game_preview_definition *definition, const char *path);
int game_preview_definition_count(const game_preview_world *world);
game_preview_definition *game_preview_definition_at(game_preview_world *world, int index);
/* Copies source into the working copy. Moving automatic nodes leaves the
   definition saved and applied. */
int game_preview_sync_working(game_preview_definition *definition, const behavior_asset *source);
int game_preview_mark_dirty(game_preview_definition *definition);

game_preview_world *game_preview_world_new(ecs_world *ecs);
void game_preview_world_free(game_preview_world *world);
int game_preview_add_definition(game_preview_world *world, game_preview_definition *definition);
/* Adds an authoring projection after canonical module publication. The view
 * retains its own snapshot and never supplies executable graph state. */
int game_preview_add_projection(game_preview_world *, game_preview_definition *);
int game_preview_remove_definition(game_preview_world *world, const char *asset_name);
/* Removes only a canonical authoring view, preserving its live source module. */
int game_preview_remove_projection(game_preview_world *, const char *asset_name);
/* Renames a definition in place: its runtime registration and attachments
   follow. Returns -1 when the name is taken. */
int game_preview_rename_definition(game_preview_world *world, const char *from, const char *to);
int game_preview_can_update_library(
    game_preview_world *world,
    game_preview_definition *definition,
    char *message,
    size_t size
);
int game_preview_apply(
    game_preview_world *world,
    game_preview_definition *definition,
    char *message,
    size_t message_size
);

/* Publishes nonbehavior graph artifacts; source projections use the source collection. */
int game_preview_update(
    game_preview_world *world,
    game_preview_definition *definition,
    char *message,
    size_t message_size
);
/* What applying source would run: libraries linked from the running
   definitions and component nodes lowered. NULL when it does not link. */
behavior_asset *game_preview_link(game_preview_world *world, const behavior_asset *source);

#endif
