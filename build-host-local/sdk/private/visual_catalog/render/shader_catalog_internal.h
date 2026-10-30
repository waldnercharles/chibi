#ifndef CHIBI_SHADER_CATALOG_INTERNAL_H
#define CHIBI_SHADER_CATALOG_INTERNAL_H
#include "render/shader_catalog_records.h"

#include <stdatomic.h>
enum
{
    MODEL_SNAPSHOT_CACHE_MAX = 32
};
typedef struct
{
    float incoming[32][4], prepared[32][4];
    model_bounds bounds;
} model_snapshot;
typedef struct
{
    ecs_world *world;
    ecs_entity entity;
    model_snapshot snapshot;
} model_caller;
struct shader_asset_runtime
{
    model_snapshot *snapshots;
    int next_snapshot;
    model_caller *callers;
    int *caller_slots;
};
typedef struct
{
    ecs_world *world;
    uint64_t entity;
    char *name;
    float value[4];
    int width, shader;
} shader_override;

extern shader_override *shader_catalog_overrides;
extern atomic_flag shader_catalog_override_lock;
const char *shader_catalog_write_parameter_value(void *, const pin_value *, bool);
#endif
