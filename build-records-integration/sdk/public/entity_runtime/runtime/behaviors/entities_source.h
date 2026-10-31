#ifndef CHIBI_ENTITIES_SOURCE_H
#define CHIBI_ENTITIES_SOURCE_H

#include "runtime/behaviors/entities.h"
#include "runtime/behaviors/source.h"

typedef struct source_entities source_entities;
/* The source registry and its ECS world outlive this host. Native capability
 * service bindings borrow source_entities_host() and must be retired
 * before freeing it. Prototype generations are retained by the entity kernel. */
source_entities *source_entities_new(source_registry *);
void source_entities_free(source_entities *);
entities *source_entities_host(const source_entities *);
/* Call at each simulation boundary before flushing commands/queued events.
 * Newly spawned attachments start Begin at that boundary; initializers always
 * complete before their behavior attachments become visible. */
void source_entities_time(source_entities *, double now, double delta, bool defer_begin);

#endif
