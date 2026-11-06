#include "runtime/behaviors/scene_flow.h"

#include "execution/function.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/text.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct scene_flow
{
    char **names;
    char *current, *next;
    char message[192];
};

scene_flow *scene_flow_new()
{
    scene_flow *flow = xcalloc(1, sizeof *flow);
    flow->current = xstrdup("");
    return flow;
}

static void clear_names(scene_flow *flow)
{
    for (int i = 0; i < acount(flow->names); i++) {
        free(flow->names[i]);
    }
    afree(flow->names);
}

void scene_flow_free(scene_flow *flow)
{
    clear_names(flow);
    free(flow->current);
    free(flow->next);
    free(flow);
}

void scene_flow_set(scene_flow *flow, const char *const *names, int count)
{
    clear_names(flow);
    for (int i = 0; i < count; i++) {
        apush(flow->names, xstrdup(names[i]));
    }
}

void scene_flow_enter(scene_flow *flow, const char *name)
{
    char *copy = xstrdup(name);
    free(flow->current);
    flow->current = copy;
}

const char *scene_flow_current(const scene_flow *flow)
{
    return flow->current;
}

char *scene_flow_take(scene_flow *flow)
{
    char *next = flow->next;
    flow->next = NULL;
    return next;
}

static const char missing[] = "Loading scenes requires a game world";

/* A later request in the same frame replaces an earlier one. */
static const char *request(scene_flow *flow, const char *name)
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

static const char *load(const behavior_function_context *ctx, void *service, const pin_value *in, pin_value *)
{
    const char *text;
    const char *error = pin_text_cstring(ctx->value_storage, in[0], &text, NULL);
    if (error) {
        return error;
    }

    return request(service, text);
}

static const char *reload(void *service, const pin_value *, pin_value *)
{
    scene_flow *flow = service;
    return request(flow, flow->current);
}

static const char *current(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *,
    pin_value *out
)
{
    const scene_flow *flow = service;
    pin_value value;
    const char *text_error = pin_value_text(ctx->value_storage, &value, flow->current, strlen(flow->current));
    if (text_error) {
        return text_error;
    }
    out[0].text = value.text;
    return NULL;
}

const node_host_call SCENE_LOAD_CALL = {
    .service = "game.scenes",
    .context = load,
    .missing_service = missing,
};
const node_host_call SCENE_RELOAD_CALL = {
    .service = "game.scenes",
    .invoke = reload,
    .missing_service = missing,
};
const node_host_call SCENE_CURRENT_CALL = {
    .service = "game.scenes",
    .context = current,
    .missing_service = missing,
};
const node_access SCENE_REQUEST_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
const node_access SCENE_READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
