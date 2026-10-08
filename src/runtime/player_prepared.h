#pragma once

#define GAME_PLAYER_PREPARED_VERSION 3
#include "runtime/player.h"

#define GAME_PLAYER_PREPARED_MAX_BYTES (64 * 1024 * 1024)
typedef struct
{
    ecs_component_catalog components;
    const execution_prepared_linker *linker;
    execution_log_service log;
    int (*sdf_motion)(ecs_world *, double);
    game_player_resource_host resources;
    size_t decoded_budget; /* Zero selects 64 MiB; otherwise caller-selected. */
} game_player_host;
/* One portable project artifact owns the initial scene, named prototypes,
 * executable module records and typed field catalog. Native implementations,
 * descriptors and callbacks are supplied by the host and outlive the player.
 * Resource handles must have the portable identity supported by scene encoding;
 * no authored text, source compiler or process pointers are saved.
 * Input bytes are capped at 64 MiB. One caller-selected decoded budget (default
 * 64 MiB) bounds retained canonical scenes, events, field catalogs, executable
 * module closures and staged resource generations. Decoder scratch is bounded
 * separately and released before return. Generated backend code and mutable
 * simulation state, registry bookkeeping and fixed bounded envelope tables are
 * outside this canonical artifact storage budget. */
bool game_player_prepared_write(
    const game_player_spec *,
    unsigned char **data,
    size_t *length,
    char *error,
    size_t size
);
game_player *game_player_prepared_read(
    const void *,
    size_t length,
    const game_player_host *,
    behavior_backend,
    char *error,
    size_t size
);
/* Reads one bounded artifact. No filesystem access occurs during simulation. */
game_player *game_player_open(
    const char *path,
    const game_player_host *,
    behavior_backend,
    char *error,
    size_t size
);
