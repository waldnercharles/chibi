#pragma once
#include "authoring/image_import.h"
#include "runtime/player.h"
/* Export resident immutable shader/model generations and typed parameter
 * records. The caller owns the returned array, names and encoded payloads. */
bool authoring_visual_prepare(const scene *const *, int scene_count, player_asset **, char *error, size_t size);
void authoring_visual_prepared_free(player_asset *);

typedef struct
{
    size_t bytes;
    int count;
} authoring_visual_cost;
typedef struct
{
    authoring_visual_cost current[PLAYER_ASSET_COUNT], proposed[PLAYER_ASSET_COUNT];
    size_t current_bytes, proposed_bytes, current_pixel_bytes, proposed_pixel_bytes;
    size_t limit_bytes;
    uint64_t current_hash, proposed_hash; /* FNV-1a64 over ordered kind/key/payload records. */
    int current_count, proposed_count, limit_count;
    bool fits;
} authoring_visual_budget;
/* Measures the same resident collection as prepare, with proposed images
 * replacing matching image keys or adding new keys. Serializes each actual
 * codec payload then frees it, without retaining an aggregate snapshot.
 * Like prepare, this may populate the CPU mesh asset cache for scene sources.
 * A successful measurement may report fits=false. Malformed inputs fail.
 * Bytes are exact prepared visual payloads (including each codec's headers),
 * NOT the complete player envelope, retained decoded storage, peak or GPU
 * memory. pixel_bytes separately counts unique image-source RGBA8 payloads.
 * Scenes, the resident catalog and proposed graph changes must already match
 * the plan. Up to 64 proposals; duplicate keys are rejected. */
bool authoring_visual_measure(
    const scene *const *,
    int scene_count,
    const image_import_proposal *,
    int proposal_count,
    authoring_visual_budget *,
    char *error,
    size_t size
);
