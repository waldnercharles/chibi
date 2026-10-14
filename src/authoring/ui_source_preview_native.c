#include "authoring/ui_source_preview_definition.h"
#include "execution/function.h"

#include <assert.h>
#include <string.h>
static void returned(const behavior_function_context *ctx, pin_value *out, ui_source_preview_result result)
{
    assert(out->ptr);
    result.context = ctx->copy_string(ctx->value_storage, result.context);
    result.error = ctx->copy_string(ctx->value_storage, result.error);
    memcpy(out->ptr, &result, sizeof result);
}
static const char *missing(const behavior_function_context *ctx, pin_value *out)
{
    returned(ctx, out, (ui_source_preview_result){ .context = "", .error = "UI source preview requires an active workspace graphics host" });
    return NULL;
}
static const char *open_preview(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    if (!service) {
        return missing(ctx, out);
    }
    returned(
        ctx,
        out,
        ui_source_preview_open(
            service,
            in[0].i,
            in[1].i,
            in[2].str,
            in[3].i,
            in[4].u64
        )
    );
    return NULL;
}
static const char *draw_preview(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    if (!service) {
        return missing(ctx, out);
    }
    returned(
        ctx,
        out,
        ui_source_preview_draw(
            service,
            in[0].i,
            in[1].i,
            in[2].i,
            in[3].i,
            in[4].d
        )
    );
    return NULL;
}
static const char *close_preview(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    if (!service) {
        return missing(ctx, out);
    }
    returned(ctx, out, ui_source_preview_close(service, in[0].i));
    return NULL;
}
#define C(F)                                                                   \
    { .service = "ui_source_preview",                                          \
      .optional_service = true,                                                \
      .missing_service = "UI source preview requires a graphics host",         \
      .context = F }
const node_host_call UI_SOURCE_PREVIEW_CALLS[3] = { C(open_preview),
                                                    C(draw_preview),
                                                    C(close_preview) };
const node_access UI_SOURCE_PREVIEW_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
