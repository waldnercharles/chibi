#ifndef CHIBI_GAME_ENTITIES_SOURCE_H
#define CHIBI_GAME_ENTITIES_SOURCE_H

#include "runtime/behaviors/entities.h"
#include "runtime/behaviors/source.h"

typedef struct game_source_entities game_source_entities;
/* The source registry and its ECS world outlive this host. Native capability
 * service bindings borrow game_source_entities_host() and must be retired
 * before freeing it. Prototype generations are retained by the entity kernel. */
game_source_entities *game_source_entities_new(game_source_registry *);
void game_source_entities_free(game_source_entities *);
game_entities *game_source_entities_host(const game_source_entities *);
/* Call at each simulation boundary before flushing commands/queued events.
 * Newly spawned attachments start Begin at that boundary; initializers always
 * complete before their behavior attachments become visible. */
void game_source_entities_time(game_source_entities *, double now, double delta, bool defer_begin);

#endif
