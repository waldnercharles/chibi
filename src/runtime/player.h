#pragma once
#include "runtime/behaviors/package.h"
#include "runtime/scenes/scene.h"
#include "runtime/world.h"

typedef struct game_player game_player;
typedef enum
{
    GAME_PLAYER_ASSET_SHADER,
    GAME_PLAYER_ASSET_MODEL,
    GAME_PLAYER_ASSET_MESH,
    GAME_PLAYER_ASSET_IMAGE,
    GAME_PLAYER_ASSET_SHADER_VALUES,
    GAME_PLAYER_ASSET_PARTS,
    GAME_PLAYER_ASSET_UI,
    GAME_PLAYER_ASSET_FONT,
    GAME_PLAYER_ASSET_SOUND, /* an encoded WAV or OGG file, by sound name */
    GAME_PLAYER_ASSET_COUNT
} game_player_asset_kind;
typedef struct
{
    const char *name;
    game_player_asset_kind kind;
    const void *data;
    size_t length;
    /* External binary artifacts require a normalized content-relative path,
     * expected byte length and FNV-1a64 fingerprint. Embedded records may carry
     * provenance and an optional fingerprint. */
    const char *path;
    uint64_t content_hash;
    bool external;
} game_player_asset;
uint64_t game_player_asset_fingerprint(const void *, size_t);
bool game_player_asset_valid(const game_player_asset *);
#define GAME_PLAYER_MAX_ASSETS 4096
#define GAME_PLAYER_MAX_ASSET_BYTES (64 * 1024 * 1024)
typedef struct
{
    void *context;
    /* Decode allocations are bounded by remaining and accepted retained storage
     * is charged before return. Failure preserves the budget. */
    void *(*stage)(void *, const game_player_asset *, int count, size_t *remaining, char *error, size_t size);
    /* Validate every prepared scene against this staged generation before
     * instantiation, publication or Begin. Failure does not publish resources. */
    bool (*scene)(void *, void *generation, const game_scene *, char *error, size_t size);
    /* Infallible publication after world construction, without dispatch. */
    void (*commit)(void *, void *generation, game_world *);
    void (*discard)(void *, void *generation);
    /* Called after the committed world and its pending work are destroyed. */
    void (*retire)(void *, void *generation);
} game_player_resource_host;
typedef struct
{
    const char *name;
    game_scene *scene;
} game_player_prototype;
typedef struct
{
    game_world_options world;
    const game_package_module *modules;
    int module_count;
    const execution_prepared_linker *linker;
    behavior_backend backend;
    const game_player_prototype *prototypes;
    int prototype_count;
    game_scene *scene;
    const game_player_asset *assets;
    int asset_count;
    game_player_resource_host resources;
    /* Optional shared remaining decoded-storage budget, borrowed only during
     * construction. Decoded input generations are already charged by readers. */
    size_t *decoded_remaining;
} game_player_spec;
/* Borrows all prepared inputs only during construction; generations are copied
 * or retained by their owning runtime. World fields remain caller-owned (the
 * player clones their portable prepared catalog). No source/parser/editor or
 * implicit capability discovery is performed. Failure dispatches no Begin and
 * leaves caller generations untouched. Descriptors/callback code/user contexts
 * still outlive the player. */
game_player *game_player_new(const game_player_spec *, char *error, size_t size);
void game_player_free(game_player *);
game_world *game_player_world(const game_player *);
const game_scene_instance *game_player_scene(const game_player *);
