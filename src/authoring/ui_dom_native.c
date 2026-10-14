#include "authoring/ui_dom_definition.h"
#include "execution/function.h"

#include <assert.h>
#include <string.h>
static void returned(const behavior_function_context *ctx, pin_value *out, ui_dom_status status)
{
    assert(out->type == PIN_STRUCT && out->ptr);
    status.error = ctx->copy_string(ctx->value_storage, status.error);
    memcpy(out->ptr, &status, sizeof status);
}
static const char *query(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    pin_array *rows;
    returned(ctx, out, ui_dom_query(service, in[0].i, in[1].i, ctx->value_storage, &rows));
    out[1].ptr = rows;
    return NULL;
}
static const char *inspect(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    pin_array *rows;
    ui_dom_row row;
    returned(
        ctx,
        out,
        ui_dom_inspect(service, in[0].str, in[1].str, in[2].i, in[3].i, ctx->value_storage, &rows, &row)
    );
    out[1].ptr = rows;
    assert(out[2].ptr);
    memcpy(out[2].ptr, &row, sizeof row);
    return NULL;
}
static const char *pick(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    ui_dom_row row;
    returned(ctx, out, ui_dom_pick(service, in[0].d, in[1].d, in[2].str, ctx->value_storage, &row));
    assert(out[1].ptr);
    memcpy(out[1].ptr, &row, sizeof row);
    return NULL;
}
static const char *pseudo(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    returned(ctx, out, ui_dom_pseudo(service, in[0].str, in[1].str, in[2].b));
    return NULL;
}
static const char *contexts(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *,
    pin_value *out
)
{
    pin_array *rows;
    returned(ctx, out, ui_dom_contexts(service, ctx->value_storage, &rows));
    out[1].ptr = rows;
    return NULL;
}
static const char *select_context(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    returned(ctx, out, ui_dom_select(service, in[0].str));
    return NULL;
}
static const char *unit(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    returned(
        ctx,
        out,
        ui_dom_unit(service, in[0].str, in[1].str, in[2].str, &out[1].d)
    );
    return NULL;
}
#define CALL(F)                                                                \
    { .service = "ui_dom",                                                     \
      .missing_service = "Live UI inspection host context unavailable",        \
      .context = F }
const node_host_call UI_DOM_QUERY_CALL = CALL(query),
                     UI_DOM_INSPECT_CALL = CALL(inspect),
                     UI_DOM_PICK_CALL = CALL(pick),
                     UI_DOM_PSEUDO_CALL = CALL(pseudo),
                     UI_DOM_CONTEXTS_CALL = CALL(contexts),
                     UI_DOM_SELECT_CALL = CALL(select_context),
                     UI_DOM_UNIT_CALL = CALL(unit);
const node_access UI_DOM_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };

static const char *measure_text(void *service, const pin_value *in, pin_value *out)
{
    out[0].b = ui_dom_measure_text(
        service,
        in[4].str,
        in[0].str,
        in[1].str,
        in[2].f,
        in[3].i,
        &out[1].f
    );
    return NULL;
}
const node_host_call UI_DOM_MEASURE_CALL = {
    .service = "ui_dom",
    .invoke = measure_text,
    .missing_service = "Live UI measurement host context unavailable"
};

const node_access UI_DOM_MEASURE_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
