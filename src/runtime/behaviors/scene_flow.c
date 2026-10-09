#include "runtime/behaviors/scene_flow.h"

#include "execution/function.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct game_scene_flow
{
    char **names;
    char *current, *next;
    char message[192];
};

game_scene_flow *game_scene_flow_new()
{
    game_scene_flow *flow = xcalloc(1, sizeof *flow);
    flow->current = xstrdup("");
    return flow;
}

static void clear_names(game_scene_flow *flow)
{
    for (int i = 0; i < acount(flow->names); i++) {
        free(flow->names[i]);
    }
    afree(flow->names);
}

void game_scene_flow_free(game_scene_flow *flow)
{
    clear_names(flow);
    free(flow->current);
    free(flow->next);
    free(flow);
}

void game_scene_flow_set(game_scene_flow *flow, const char *const *names, int count)
{
    clear_names(flow);
    for (int i = 0; i < count; i++) {
        apush(flow->names, xstrdup(names[i]));
    }
}

void game_scene_flow_enter(game_scene_flow *flow, const char *name)
{
    char *copy = xstrdup(name);
    free(flow->current);
    flow->current = copy;
}

const char *game_scene_flow_current(const game_scene_flow *flow)
{
    return flow->current;
}

char *game_scene_flow_take(game_scene_flow *flow)
{
    char *next = flow->next;
    flow->next = NULL;
    return next;
}

static const char missing[] = "Loading scenes requires a game world";

/* A later request in the same frame replaces an earlier one. */
static const char *request(game_scene_flow *flow, const char *name)
{
    if (!acount(flow->names)) {
        return "This host cannot load scenes";
    }
    for (int i = 0; i < acount(flow->names); i++) {
        if (!strcmp(flow->names[i], name)) {
            free(flow->next);
            flow->next = xstrdup(name);
            return NULL;
        }
    }
    snprintf(flow->message, sizeof flow->message, "No scene named '%s'", name);
    return flow->message;
}

static const char *load(void *service, const pin_value *in, pin_value *)
{
    return request(service, in[0].str ? in[0].str : "");
}

static const char *reload(void *service, const pin_value *, pin_value *)
{
    game_scene_flow *flow = service;
    return request(flow, flow->current);
}

static const char *current(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *,
    pin_value *out
)
{
    assert(ctx->copy_string);
    const game_scene_flow *flow = service;
    out[0].str = ctx->copy_string(ctx->value_storage, flow->current);
    return NULL;
}

const node_host_call GAME_SCENE_LOAD_CALL = {
    .service = "game.scenes",
    .invoke = load,
    .missing_service = missing,
};
const node_host_call GAME_SCENE_RELOAD_CALL = {
    .service = "game.scenes",
    .invoke = reload,
    .missing_service = missing,
};
const node_host_call GAME_SCENE_CURRENT_CALL = {
    .service = "game.scenes",
    .context = current,
    .missing_service = missing,
};
const node_access GAME_SCENE_REQUEST_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
const node_access GAME_SCENE_READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
