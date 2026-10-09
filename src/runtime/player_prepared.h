#pragma once

#define GAME_PLAYER_PREPARED_VERSION 8
#include "runtime/player.h"

#define GAME_PLAYER_PREPARED_MAX_BYTES (64 * 1024 * 1024)
#define GAME_PLAYER_APPLICATION_TEXT_MAX 1024
#define GAME_PLAYER_APPLICATION_SIZE_MAX 16384
/* How a host presents the game's window. NULL strings and zero sizes are
 * unset; the icon is a PNG path relative to the host's resource root. */
typedef struct
{
    char *title;
    int width, height;
    bool fullscreen;
    char *icon;
} game_player_application;
typedef struct
{
    ecs_component_catalog components;
    const execution_prepared_linker *linker;
    execution_log_service log;
    int (*sdf_motion)(ecs_world *, double);
    game_player_resource_host resources;
    size_t decoded_budget; /* Zero selects 64 MiB; otherwise caller-selected. */
} game_player_host;
/* One portable project artifact owns the named start scene, the other named
 * scenes Load Scene can start, named prototypes, executable module records
 * and typed field catalog. Native implementations,
 * descriptors and callbacks are supplied by the host and outlive the player.
 * Resource handles must have the portable identity supported by scene encoding;
 * no authored text, source compiler or process pointers are saved.
 * Input bytes are capped at 64 MiB. One caller-selected decoded budget (default
 * 64 MiB) bounds retained canonical scenes, events, field catalogs, executable
 * module closures, copied sounds and staged resource generations. Decoder
 * scratch is bounded separately and released before return. Generated backend
 * code and mutable simulation state, registry bookkeeping and fixed bounded
 * envelope tables are outside this canonical artifact storage budget. */
bool game_player_prepared_write(
    const game_player_spec *,
    const game_player_application *,
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
/* Reads only the application settings at the front of an artifact file, so a
 * host can create its window before opening the game. The strings are owned;
 * release them with game_player_application_free. */
bool game_player_application_read(const char *path, game_player_application *, char *error, size_t size);
void game_player_application_free(game_player_application *);
/* Reads one bounded artifact. No filesystem access occurs during simulation. */
game_player *game_player_open(
    const char *path,
    const game_player_host *,
    behavior_backend,
    char *error,
    size_t size
);
