#ifndef CHIBI_TERRAIN_STORAGE_H
#define CHIBI_TERRAIN_STORAGE_H

#include "ecs/ecs.h"
#include "execution/host.h"

#include <stdbool.h>
#include <stddef.h>

struct materials;
struct save;
typedef struct terrain_storage terrain_storage;
typedef struct
{
    bool pending, succeeded;
    const char *error; /* Borrowed until the next request, flush or reset. */
} terrain_storage_state;

/* Borrowed dependencies belong to the same world and outlive this module. */
terrain_storage *terrain_storage_new(struct materials *, struct save *);
void terrain_storage_free(terrain_storage *);
/* Cancel pending work and invalidate retained results on scene replacement. */
void terrain_storage_reset(terrain_storage *);
/* Copy the stable identity, which also selects the binary attachment in the
 * host's save namespace. Return a positive token, or zero with an error. One
 * request may be pending at a time. A new accepted request expires the prior
 * result. The identity contains 1-64 bytes. This call never prepares terrain. */
int terrain_storage_request(
    terrain_storage *,
    ecs_entity source,
    const char *identity,
    bool load,
    char *error,
    size_t size
);
bool terrain_storage_pending(const terrain_storage *);
terrain_storage_state terrain_storage_result(const terrain_storage *, int request);
/* Outside queries, after authored structural commands publish.
 * Apply queued terrain edits before capture. Load fully validates before
 * replacing terrain, then prepares residency/geometry for rendering. Physics
 * shapes refresh at its next normal boundary before solving. Save/load add one
 * materials preparation; successful load adds another. Storage failures are
 * retained results, not engine failures. IO/validation failures preserve terrain.
 * A post-import preparation failure reports that terrain was already restored;
 * it does not roll back the import. Unbound/browser storage cannot succeed.
 * A host using this directly must synchronize physics before inspecting shapes
 * and propagate transforms before rendering newly created terrain tiles. */
void terrain_storage_flush(terrain_storage *);

#define TERRAIN_STORAGE_SERVICE "game.terrain_storage"
extern const node_host_call TERRAIN_SAVE_CALL, TERRAIN_LOAD_CALL,
    TERRAIN_STORAGE_RESULT_CALL;
extern const node_access TERRAIN_STORAGE_WRITE_ACCESS, TERRAIN_STORAGE_READ_ACCESS;

#endif
