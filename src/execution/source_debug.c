#include "execution/source_debug.h"

#include "execution/definition.h"
#include "execution/function.h"
#include "support/array.h"

#include <assert.h>
#include <string.h>

int behavior_source_pause(behavior_function_context *ctx, int point)
{
    if (!ctx->source_pause) {
        return 0;
    }
    return ctx->source_pause(ctx->source_debug_user, ctx, &ctx->source_points[point]);
}
void behavior_source_capture(behavior_function_context *ctx, int watch)
{
    if (!ctx->source_capture) {
        return;
    }
    const behavior_source_watch *source = &ctx->source_watches[watch];
    pin_value value = source->initial;
    memcpy(&value.d, &ctx->watched.d, sizeof value.d);
    ctx->source_capture(ctx->source_debug_user, ctx, source, source->reference ? NULL : &value);
}
int behavior_function_source_point_count(const behavior_function *f)
{
    assert(f);
    return acount(f->source_points);
}
behavior_source_point behavior_function_source_point_at(const behavior_function *f, int index)
{
    assert(f && index >= 0 && index < acount(f->source_points));
    return f->source_points[index];
}
