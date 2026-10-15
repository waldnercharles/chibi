#include "runtime/save_native.h"

#include "execution/function.h"
#include "runtime/save.h"

#include <assert.h>

static const char missing_read[] = "Read Saved Text requires a game world";
static const char missing_write[] = "Saving requires a game world";
static const char *read_text(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    save *save = service;
    if (!save) {
        return missing_read;
    }
    assert(ctx->copy_string);
    const char *text = save_get(save, in[0].str);
    out[0].b = text != NULL;
    if (!text) {
        text = in[1].str ? in[1].str : "";
    }
    out[1].str = ctx->copy_string(ctx->value_storage, text);
    return NULL;
}
static const char *set_text(void *service, const pin_value *in, pin_value *)
{
    save *save = service;
    if (!save) {
        return missing_write;
    }
    return save_set(save, in[0].str, in[1].str ? in[1].str : "") ? NULL
                                                                 : save_error(save);
}
static const char *remove_text(void *service, const pin_value *in, pin_value *)
{
    save *save = service;
    if (!save) {
        return missing_write;
    }
    return save_remove(save, in[0].str) ? NULL : save_error(save);
}
static const char *commit(
    const behavior_function_context *,
    void *service,
    const pin_value *,
    int argument_count,
    pin_value *,
    int result_count,
    const uint64_t *,
    int outcome_count,
    int *selected
)
{
    save *save = service;
    if (!save) {
        return missing_write;
    }
    assert(argument_count == 0 && result_count == 0 && outcome_count == 2);
    (void)argument_count;
    (void)result_count;
    (void)outcome_count;
    *selected = save_commit(save) ? 0 : 1;
    return NULL;
}
static const char *discard(void *service, const pin_value *, pin_value *)
{
    if (!service) {
        return missing_write;
    }
    save_discard(service);
    return NULL;
}
const node_host_call SAVE_READ_CALL = {
    .service = "game.save",
    .context = read_text,
    .missing_service = missing_read,
};
const node_host_call SAVE_SET_CALL = {
    .service = "game.save",
    .invoke = set_text,
    .missing_service = missing_write,
};
const node_host_call SAVE_REMOVE_CALL = {
    .service = "game.save",
    .invoke = remove_text,
    .missing_service = missing_write,
};
const node_host_call SAVE_COMMIT_CALL = {
    .service = "game.save",
    .action = commit,
    .missing_service = missing_write,
};
const node_host_call SAVE_DISCARD_CALL = {
    .service = "game.save",
    .invoke = discard,
    .missing_service = missing_write,
};
const node_access SAVE_READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
const node_access SAVE_WRITE_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
