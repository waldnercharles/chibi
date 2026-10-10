#include "authoring/preview_runtime.h"

#include "runtime/declared_components.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#define PREVIEW_LOG_LIMIT 1024
static void log_message(void *user, const execution_log_message *message)
{
    game_preview_runtime *r = user;
    if (acount(r->logs) == PREVIEW_LOG_LIMIT) {
        r->dropped_logs++;
        return;
    }
    apush(
        r->logs,
        ((game_preview_log){
            .owner = message->owner,
            .node = message->node_id,
            .now = message->now,
            .program = xstrdup(message->program_name ? message->program_name : ""),
            .text = xstrdup(message->text ? message->text : "") })
    );
}
game_preview_runtime *game_preview_runtime_new(ecs_world *ecs)
{
    game_preview_runtime *r = xcalloc(1, sizeof *r);
    asetcap(r->logs, PREVIEW_LOG_LIMIT);
    game_world_options options = { .ecs = ecs,
                                   .modules = GAME_WORLD_MODULES_ALL,
                                   .components = project_components(),
                                   .fields = material_fields_new(),
                                   .log = { .emit = log_message, .user = r } };
    char error[256];
    r->owner = game_world_new(&options, error, sizeof error);
    if (!r->owner) {
        material_fields_free(options.fields);
        afree(r->logs);
        free(r);
        return NULL;
    }
    const engine_modules *m = game_world_modules(r->owner);
    r->ecs = game_world_ecs(r->owner);
    r->sources = m->sources;
    r->entities = m->entities;
    r->physics = m->physics;
    r->physics3d = m->physics3d;
    r->particles = m->particles;
    r->materials = m->materials;
    r->visibility = m->visibility;
    r->audio = m->audio;
    r->save = m->save;
    r->ui = m->ui;
    return r;
}
void game_preview_runtime_clear_logs(game_preview_runtime *r)
{
    for (int i = 0; i < acount(r->logs); i++) {
        free(r->logs[i].program);
        free(r->logs[i].text);
    }
    aclear(r->logs);
    r->dropped_logs = 0;
}
void game_preview_runtime_free(game_preview_runtime *r)
{
    assert(r);
    game_world_free(r->owner);
    game_preview_runtime_clear_logs(r);
    afree(r->logs);
    free(r);
}
int game_preview_runtime_log_count(const game_preview_runtime *r)
{
    assert(r);
    return acount(r->logs);
}
const game_preview_log *game_preview_runtime_log_at(const game_preview_runtime *r, int index)
{
    assert(r && index >= 0 && index < acount(r->logs));
    return &r->logs[index];
}
