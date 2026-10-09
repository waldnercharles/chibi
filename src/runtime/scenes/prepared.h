#ifndef CHIBI_PREPARED_SCENE_H
#define CHIBI_PREPARED_SCENE_H
#include "ecs/component_catalog.h"
#include "runtime/scenes/scene.h"

#define GAME_SCENE_PREPARED_VERSION UINT64_C(2)

/* Versioned field-wise scene/prototype records. Stable component IDs, names,
 * sizes and reflected layout hashes are verified against the explicit catalog.
 * Reflected nominal values use the supplying host's immutable type catalog.
 * No process pointers, padding, graph, source or authored document is stored.
 * Nonzero handles are rejected until their resource identities have an owning
 * portable contract. The scene's UI/behavior names remain package references. */
bool game_scene_prepared_write(
    const game_scene *,
    unsigned char **data,
    size_t *length,
    char *error,
    size_t size
);
/* Returned bytes use malloc/free. Failure leaves no bytes. Decoding returns an
 * owned scene only after complete validation; the catalog's type descriptors
 * must outlive it. A prototype uses this same format with exactly one object.
 * Input and owned payloads are limited to 64 MiB, depth 64 and one million values;
 * at most 1024 objects/items per object and 4096 total scene item records. */
game_scene *game_scene_prepared_read(
    const void *,
    size_t length,
    ecs_component_catalog,
    char *error,
    size_t size
);
/* Applies remaining to decoded allocations before reading; charges accepted
 * generation storage before returning. Failure leaves the budget unchanged. */
game_scene *game_scene_prepared_read_budget(
    const void *,
    size_t length,
    ecs_component_catalog,
    size_t *remaining,
    char *error,
    size_t size
);

#endif
