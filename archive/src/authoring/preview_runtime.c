#include "authoring/preview_runtime.h"

#include "runtime/declared_components.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#define PREVIEW_LOG_LIMIT 1024
static void log_message(void *user, const execution_log_message *message)
{
    preview_runtime *r = user;
    if (acount(r->logs) == PREVIEW_LOG_LIMIT) {
        r->dropped_logs++;
        return;
    }
    char *text = xmalloc(message->text_length + 1);
    if (message->text_length) {
        memcpy(text, message->text, message->text_length);
    }
    text[message->text_length] = 0;
    apush(
        r->logs,
        ((preview_log){ .owner = message->owner,
                        .node = message->node_id,
                        .now = message->now,
                        .program = xstrdup(message->program_name ? message->program_name : ""),
                        .text = text,
                        .text_length = message->text_length })
    );
}
preview_runtime *preview_runtime_new(ecs_world *ecs)
{
    preview_runtime *r = xcalloc(1, sizeof *r);
    asetcap(r->logs, PREVIEW_LOG_LIMIT);
    world_options options = { .ecs = ecs,
                              .modules = WORLD_MODULES_ALL,
                              .components = project_components(),
                              .fields = material_fields_new(),
                              .log = { .emit = log_message, .user = r } };
    char error[256];
    r->owner = world_new(&options, error, sizeof error);
    if (!r->owner) {
        material_fields_free(options.fields);
        afree(r->logs);
        free(r);
        return NULL;
    }
    const engine_modules *m = world_modules(r->owner);
    r->ecs = world_ecs(r->owner);
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
void preview_runtime_clear_logs(preview_runtime *r)
{
    for (int i = 0; i < acount(r->logs); i++) {
        free(r->logs[i].program);
        free(r->logs[i].text);
    }
    aclear(r->logs);
    r->dropped_logs = 0;
}
void preview_runtime_free(preview_runtime *r)
{
    assert(r);
    world_free(r->owner);
    preview_runtime_clear_logs(r);
    afree(r->logs);
    free(r);
}
int preview_runtime_log_count(const preview_runtime *r)
{
    assert(r);
    return acount(r->logs);
}
const preview_log *preview_runtime_log_at(const preview_runtime *r, int index)
{
    assert(r && index >= 0 && index < acount(r->logs));
    return &r->logs[index];
}
