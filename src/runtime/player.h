#pragma once
#include "runtime/behaviors/package.h"
#include "runtime/declared_components.h"
#include "runtime/scenes/scene.h"
#include "runtime/world.h"

typedef struct player player;
typedef enum
{
    PLAYER_ASSET_SHADER,
    PLAYER_ASSET_MODEL,
    PLAYER_ASSET_MESH,
    PLAYER_ASSET_IMAGE,
    PLAYER_ASSET_SHADER_VALUES,
    PLAYER_ASSET_PARTS,
    PLAYER_ASSET_UI,
    PLAYER_ASSET_FONT,
    PLAYER_ASSET_SKY,
    PLAYER_ASSET_SOUND, /* an encoded WAV or OGG file, by sound name */
    PLAYER_ASSET_COUNT
} player_asset_kind;
typedef struct
{
    const char *name;
    player_asset_kind kind;
    const void *data;
    size_t length;
    /* External binary artifacts require a normalized content-relative path,
     * expected byte length and FNV-1a64 fingerprint. Embedded records may carry
     * provenance and an optional fingerprint. */
    const char *path;
    uint64_t content_hash;
    bool external;
} player_asset;
uint64_t player_asset_fingerprint(const void *, size_t);
/* A normalized path under a resource root: no empty, `.` or `..` parts, no
 * leading slash, backslash or colon, and at most 1024 bytes. */
bool player_relative_path(const char *path);
bool player_asset_valid(const player_asset *);
/* False, with the reason, when two of the scene's entities bind one global
 * name, which entering the scene would refuse. */
bool player_scene_bindings_valid(const scene *, char *error, size_t size);
#define PLAYER_MAX_ASSETS 4096
#define PLAYER_MAX_ASSET_BYTES (64 * 1024 * 1024)
typedef struct
{
    void *context;
    /* Decode allocations are bounded by remaining and accepted retained storage
     * is charged before return. Failure preserves the budget. */
    void *(*stage)(void *, const player_asset *, int count, size_t *remaining, char *error, size_t size);
    /* Validate every prepared scene against this staged generation before
     * instantiation, publication or Begin. Failure does not publish resources. */
    bool (*scene)(void *, void *generation, const scene *, char *error, size_t size);
    /* Infallible publication after world construction, without dispatch. */
    void (*commit)(void *, void *generation, world *);
    void (*discard)(void *, void *generation);
    /* Called after the committed world and its pending work are destroyed. */
    void (*retire)(void *, void *generation);
} player_resource_host;
typedef struct
{
    const char *name;
    scene *scene;
} player_prototype;
/* A scene Load Scene can start, by the name scripts give it. */
typedef struct
{
    const char *name;
    scene *scene;
} player_named_scene;
typedef struct
{
    world_options world;
    /* Optional project-declared components, retained by the player. World
     * components and the linker must already include them. */
    declared_components *declared;
    const package_module *modules;
    int module_count;
    const execution_prepared_linker *linker;
    /* Native library manifests the modules' library functions bind to, which
     * outlive the player, and the VM's operations they call. */
    const execution_native_manifest *const *natives;
    int native_count;
    execution_native_runtime native_runtime;
    behavior_backend backend;
    const player_prototype *prototypes;
    int prototype_count;
    scene *scene;
    /* The start scene's name (its own name when NULL) and the other scenes
     * Load Scene can start, each named once. */
    const char *scene_name;
    const player_named_scene *scenes;
    int scene_count;
    const player_asset *assets;
    int asset_count;
    player_resource_host resources;
    /* Optional shared remaining decoded-storage budget, borrowed only during
     * construction. Decoded input generations are already charged by readers. */
    size_t *decoded_remaining;
} player_spec;
/* Borrows all prepared inputs only during construction; generations are copied
 * or retained by their owning runtime. World fields remain caller-owned (the
 * player clones their portable prepared catalog). No source/parser/editor or
 * implicit capability discovery is performed. Failure dispatches no Begin and
 * leaves caller generations untouched. Descriptors/callback code/user contexts
 * still outlive the player. */
player *player_new(const player_spec *, char *error, size_t size);
/* Destroys the entire owned world, including entities outside the current
 * scene; Panic links cannot veto this whole-world teardown. Call between
 * frames, with no active ECS query or source dispatch. */
void player_free(player *);
world *player_world(const player *);
const scene_instance *player_scene(const player *);
/* Releases the current scene, resets per-scene state and starts the named
 * scene; time, input and committed saves go on. Call between frames, as
 * scene_flow_take asks. An unknown name or refused release changes nothing.
 * Later failures can leave no current scene, and the host should stop. */
bool player_load_scene(player *, const char *name, char *error, size_t size);
