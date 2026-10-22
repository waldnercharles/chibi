#ifndef CHIBI_PREVIEW_RUNTIME_H
#define CHIBI_PREVIEW_RUNTIME_H
#include "runtime/world.h"
/* Owns the simulation factory; ECS and the copied module pointers are borrowed
 * from that factory. A supplied ECS remains owned by the caller. */
typedef struct preview_runtime
{
    world *owner;
    /* The engine that plays this preview, borrowed; its input takes the
     * committed input map. NULL until one is created. */
    engine *engine;
    ecs_world *ecs;
    source_registry *sources;
    entities *entities;
    physics *physics;
    physics3d *physics3d;
    particles *particles;
    materials *materials;
    visibility *visibility;
    audio *audio;
    save *save;
    ui *ui;
    bool deferred;
    double now, delta;
    struct preview_log *logs;
    unsigned dropped_logs;
} preview_runtime;
typedef struct preview_log
{
    uint64_t owner, node;
    double now;
    char *program, *text;
    size_t text_length;
} preview_log;
preview_runtime *preview_runtime_new(ecs_world *);
void preview_runtime_free(preview_runtime *);
int preview_runtime_log_count(const preview_runtime *);
/* Rows and their copied strings are borrowed until clear_logs or free. */
const preview_log *preview_runtime_log_at(const preview_runtime *, int);
void preview_runtime_clear_logs(preview_runtime *);
#endif
