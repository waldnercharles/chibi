#pragma once

#define PLAYER_PREPARED_VERSION 16
#include "runtime/player.h"

#define PLAYER_PREPARED_MAX_BYTES (64 * 1024 * 1024)
#define PLAYER_APPLICATION_TEXT_MAX 1024
#define PLAYER_APPLICATION_SIZE_MAX 16384
/* How a host presents the game's window. NULL strings and zero sizes are
 * unset; the icon is a PNG path relative to the host's resource root. */
typedef struct
{
    char *project_name; /* Stable save identity; independent of display title. */
    char *title;
    int width, height;
    bool fullscreen;
    char *icon;
} player_application;
typedef struct
{
    ecs_component_catalog components;
    const execution_prepared_linker *linker;
    /* The game's native library manifests (see player_spec). */
    const execution_native_manifest *const *natives;
    int native_count;
    execution_log_service log;
    player_resource_host resources;
    size_t decoded_budget; /* Zero selects 64 MiB; otherwise caller-selected. */
} player_host;
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
bool player_prepared_write(
    const player_spec *,
    const player_application *,
    unsigned char **data,
    size_t *length,
    char *error,
    size_t size
);
player *player_prepared_read(
    const void *,
    size_t length,
    const player_host *,
    behavior_backend,
    char *error,
    size_t size
);
/* Locates an artifact's executable module records without opening the game,
 * so a tool can compare what two compilers declare apart from their code. The
 * records borrow their bytes from the artifact and their names from names;
 * every other byte lies before *first or from *end on. Free the records with
 * afree. False with why when the artifact is malformed up to its last module. */
bool player_prepared_modules(
    const void *,
    size_t length,
    arena *names,
    package_module **modules,
    size_t *first,
    size_t *end,
    char *error,
    size_t size
);
/* Reads only the application settings at the front of an artifact file, so a
 * host can create its window before opening the game. The strings are owned;
 * release them with player_application_free. */
bool player_application_read(const char *path, player_application *, char *error, size_t size);
void player_application_free(player_application *);
/* Reads one bounded artifact. No filesystem access occurs during simulation. */
player *player_open(const char *path, const player_host *, behavior_backend, char *error, size_t size);
