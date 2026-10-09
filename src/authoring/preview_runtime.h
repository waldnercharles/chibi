#ifndef CHIBI_PREVIEW_RUNTIME_H
#define CHIBI_PREVIEW_RUNTIME_H
#include "runtime/world.h"
/* Owns the simulation factory; ECS and the copied module pointers are borrowed
 * from that factory. A supplied ECS remains owned by the caller. */
typedef struct game_preview_runtime
{
    game_world *owner;
    /* The engine that plays this preview, borrowed; its input takes the
     * committed input map. NULL until one is created. */
    engine *engine;
    ecs_world *ecs;
    game_source_registry *sources;
    game_entities *entities;
    game_physics *physics;
    game_physics3d *physics3d;
    game_particles *particles;
    game_materials *materials;
    game_visibility *visibility;
    game_audio *audio;
    game_save *save;
    game_ui *ui;
    bool deferred;
    double now, delta;
    struct game_preview_log *logs;
    unsigned dropped_logs;
} game_preview_runtime;
typedef struct game_preview_log
{
    uint64_t owner, node;
    double now;
    char *program, *text;
} game_preview_log;
game_preview_runtime *game_preview_runtime_new(ecs_world *);
void game_preview_runtime_free(game_preview_runtime *);
int game_preview_runtime_log_count(const game_preview_runtime *);
/* Rows and their copied strings are borrowed until clear_logs or free. */
const game_preview_log *game_preview_runtime_log_at(const game_preview_runtime *, int);
void game_preview_runtime_clear_logs(game_preview_runtime *);
#endif
