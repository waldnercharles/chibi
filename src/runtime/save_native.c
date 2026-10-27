#include "runtime/save_native.h"

#include "execution/function.h"
#include "runtime/save.h"
#include "types/array.h"
#include "types/text.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <string.h>

static const char missing_read[] = "Reading save data requires a game world";
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
    const char *key;
    const char *error = pin_text_cstring(ctx->value_storage, in[0], &key, NULL);
    if (error) {
        return error;
    }
    const char *text = save_get(save, key);
    out[0].b = text != NULL;
    pin_value value;
    if (!text) {
        if (pin_value_snapshot_to_arena(ctx->value_storage, &value, &in[1])) {
            return "Cannot retain save fallback";
        }
    } else {
        error = pin_value_text(ctx->value_storage, &value, text, strlen(text));
        if (error) {
            return error;
        }
    }
    out[1].text = value.text;
    return NULL;
}
static const char *set_text(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *
)
{
    const char *error;
    const char *text0;
    error = pin_text_cstring(ctx->value_storage, in[0], &text0, NULL);
    if (error) {
        return error;
    }
    const char *text1;
    error = pin_text_cstring(ctx->value_storage, in[1], &text1, NULL);
    if (error) {
        return error;
    }

    save *save = service;
    if (!save) {
        return missing_write;
    }
    return save_set(save, text0, text1) ? NULL : save_error(save);
}
static const char *read_number(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    const char *error;
    const char *text0;
    error = pin_text_cstring(ctx->value_storage, in[0], &text0, NULL);
    if (error) {
        return error;
    }

    if (!service) {
        return missing_read;
    }
    /* A saved number past f32's range reads as missing. */
    double number = in[1].f;
    out[0].b = save_get_number(service, text0, &number) && fabs(number) <= FLT_MAX;
    out[1].f = out[0].b ? (float)number : in[1].f;
    return NULL;
}
static const char *set_number(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *
)
{
    const char *error;
    const char *text0;
    error = pin_text_cstring(ctx->value_storage, in[0], &text0, NULL);
    if (error) {
        return error;
    }

    if (!service) {
        return missing_write;
    }
    return save_set_number(service, text0, in[1].f) ? NULL : save_error(service);
}
static const char *remove_text(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *
)
{
    const char *error;
    const char *text0;
    error = pin_text_cstring(ctx->value_storage, in[0], &text0, NULL);
    if (error) {
        return error;
    }

    save *save = service;
    if (!save) {
        return missing_write;
    }
    return save_remove(save, text0) ? NULL : save_error(save);
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
    .context = set_text,
    .missing_service = missing_write,
};
const node_host_call SAVE_READ_NUMBER_CALL = {
    .service = "game.save",
    .context = read_number,
    .missing_service = missing_read,
};
const node_host_call SAVE_SET_NUMBER_CALL = {
    .service = "game.save",
    .context = set_number,
    .missing_service = missing_write,
};
const node_host_call SAVE_REMOVE_CALL = {
    .service = "game.save",
    .context = remove_text,
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
