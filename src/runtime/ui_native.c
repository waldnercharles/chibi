#include "runtime/ui_native.h"

#include "execution/function.h"
#include "runtime/ui.h"
#include "types/text.h"
#include "types/value_document.h"

#include <cute_math.h>

static const char MISSING_UI[] = "UI nodes require a game world";
static const char INVALID_UI[] = "Select an element and give a valid value";

static const char *text_call(const behavior_function_context *ctx, void *ui, const pin_value *a, pin_value *)
{
    const char *error;
    const char *text1;
    error = pin_text_cstring(ctx->value_storage, a[1], &text1, NULL);
    if (error) {
        return error;
    }
    const char *text2;
    error = pin_text_cstring(ctx->value_storage, a[2], &text2, NULL);
    if (error) {
        return error;
    }

    return ui_set_text(ui, a[0].u64, text1, text2) ? NULL : INVALID_UI;
}

static const char *class_call(const behavior_function_context *ctx, void *ui, const pin_value *a, pin_value *)
{
    const char *error;
    const char *text1;
    error = pin_text_cstring(ctx->value_storage, a[1], &text1, NULL);
    if (error) {
        return error;
    }
    const char *text2;
    error = pin_text_cstring(ctx->value_storage, a[2], &text2, NULL);
    if (error) {
        return error;
    }

    return ui_set_class(ui, a[0].u64, text1, text2, a[3].b) ? NULL : INVALID_UI;
}

static const char *style_call(const behavior_function_context *ctx, void *ui, const pin_value *a, pin_value *)
{
    const char *error;
    const char *text1;
    error = pin_text_cstring(ctx->value_storage, a[1], &text1, NULL);
    if (error) {
        return error;
    }
    const char *text2;
    error = pin_text_cstring(ctx->value_storage, a[2], &text2, NULL);
    if (error) {
        return error;
    }
    const char *text3;
    error = pin_text_cstring(ctx->value_storage, a[3], &text3, NULL);
    if (error) {
        return error;
    }

    return ui_set_style(ui, a[0].u64, text1, text2, text3) ? NULL : INVALID_UI;
}

static const char *visible_call(const behavior_function_context *ctx, void *ui, const pin_value *a, pin_value *)
{
    const char *error;
    const char *text1;
    error = pin_text_cstring(ctx->value_storage, a[1], &text1, NULL);
    if (error) {
        return error;
    }

    return ui_set_visible(ui, a[0].u64, text1, a[2].b) ? NULL : INVALID_UI;
}

static const char *value_call(const behavior_function_context *ctx, void *ui, const pin_value *a, pin_value *)
{
    const char *error;
    const char *text1;
    error = pin_text_cstring(ctx->value_storage, a[1], &text1, NULL);
    if (error) {
        return error;
    }

    return ui_set_value(ui, a[0].u64, text1, a[2].f) ? NULL : INVALID_UI;
}

static const char *focus_call(const behavior_function_context *ctx, void *ui, const pin_value *a, pin_value *)
{
    const char *error;
    const char *text1;
    error = pin_text_cstring(ctx->value_storage, a[1], &text1, NULL);
    if (error) {
        return error;
    }

    return ui_focus(ui, a[0].u64, text1) ? NULL : INVALID_UI;
}

static const char *click_call(const behavior_function_context *ctx, void *ui, const pin_value *a, pin_value *)
{
    const char *error;
    const char *text1;
    error = pin_text_cstring(ctx->value_storage, a[1], &text1, NULL);
    if (error) {
        return error;
    }

    return ui_click(ui, a[0].u64, text1) ? NULL : INVALID_UI;
}

static const char *move_focus_call(
    const behavior_function_context *ctx,
    void *ui,
    const pin_value *a,
    pin_value *
)
{
    const char *error;
    const char *text1;
    error = pin_text_cstring(ctx->value_storage, a[1], &text1, NULL);
    if (error) {
        return error;
    }

    return ui_move_focus(ui, a[0].u64, text1, a[2].i) ? NULL : INVALID_UI;
}

static const char *popover_call(const behavior_function_context *ctx, void *ui, const pin_value *a, pin_value *)
{
    const char *error;
    const char *text1;
    error = pin_text_cstring(ctx->value_storage, a[1], &text1, NULL);
    if (error) {
        return error;
    }

    const CF_V2 *at = a[2].ptr;
    return at && ui_show_popover(ui, a[0].u64, text1, at->x, at->y) ? NULL : INVALID_UI;
}

static const char *reveal_call(const behavior_function_context *ctx, void *ui, const pin_value *a, pin_value *)
{
    const char *error;
    const char *text1;
    error = pin_text_cstring(ctx->value_storage, a[1], &text1, NULL);
    if (error) {
        return error;
    }

    return ui_reveal_row(ui, a[0].u64, text1, a[2].i) ? NULL : INVALID_UI;
}

static const char *scroll_call(
    const behavior_function_context *ctx,
    void *ui,
    const pin_value *a,
    pin_value *results
)
{
    const char *error;
    const char *text1;
    error = pin_text_cstring(ctx->value_storage, a[1], &text1, NULL);
    if (error) {
        return error;
    }

    float values[3];
    results[3].b = ui_scroll(ui, a[0].u64, text1, values);
    for (int i = 0; i < 3; i++) {
        results[i].f = values[i];
    }
    return NULL;
}

static const char *get_call(
    const behavior_function_context *ctx,
    void *ui,
    const pin_value *a,
    pin_value *results
)
{
    const char *error;
    const char *text1;
    error = pin_text_cstring(ctx->value_storage, a[1], &text1, NULL);
    if (error) {
        return error;
    }

    float value = 0;
    results[1].b = ui_value(ui, a[0].u64, text1, &value);
    results[0].f = value;
    return NULL;
}

static const char *set_data_call(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *values,
    pin_value *
)
{
    const char *error;
    const char *text1;
    error = pin_text_cstring(ctx->value_storage, values[1], &text1, NULL);
    if (error) {
        return error;
    }

    av_doc *doc = av_doc_new();
    av_value *value = pin_value_to_document(doc, &values[2]);
    if (!value) {
        av_doc_free(doc);
        return "UI data requires representable text values";
    }
    bool set = ui_set_data(service, values[0].u64, text1, value);
    av_doc_free(doc);
    return set ? NULL : "Name must be a word, or a path such as items.2.name";
}

const node_access UI_WRITE_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
const node_access UI_READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
const node_host_call UI_SET_TEXT_CALL = { .service = "game.ui",
                                          .context = text_call,
                                          .missing_service = MISSING_UI };
const node_host_call UI_SET_CLASS_CALL = { .service = "game.ui",
                                           .context = class_call,
                                           .missing_service = MISSING_UI };
const node_host_call UI_SET_STYLE_CALL = { .service = "game.ui",
                                           .context = style_call,
                                           .missing_service = MISSING_UI };
const node_host_call UI_SET_VISIBLE_CALL = { .service = "game.ui",
                                             .context = visible_call,
                                             .missing_service = MISSING_UI };
const node_host_call UI_SET_VALUE_CALL = { .service = "game.ui",
                                           .context = value_call,
                                           .missing_service = MISSING_UI };
const node_host_call UI_FOCUS_CALL = { .service = "game.ui",
                                       .context = focus_call,
                                       .missing_service = MISSING_UI };
const node_host_call UI_CLICK_CALL = { .service = "game.ui",
                                       .context = click_call,
                                       .missing_service = MISSING_UI };
const node_host_call UI_MOVE_FOCUS_CALL = { .service = "game.ui",
                                            .context = move_focus_call,
                                            .missing_service = MISSING_UI };
const node_host_call UI_SHOW_POPOVER_CALL = { .service = "game.ui",
                                              .context = popover_call,
                                              .missing_service = MISSING_UI };
const node_host_call UI_REVEAL_ROW_CALL = { .service = "game.ui",
                                            .context = reveal_call,
                                            .missing_service = MISSING_UI };
const node_host_call UI_SET_DATA_CALL = { .service = "game.ui",
                                          .context = set_data_call,
                                          .missing_service = MISSING_UI };
const node_host_call UI_GET_SCROLL_CALL = {
    .service = "game.ui",
    .context = scroll_call,
    .missing_service = "Get UI Scroll requires a game world",
    .reusable = true
};
const node_host_call UI_GET_VALUE_CALL = {
    .service = "game.ui",
    .context = get_call,
    .missing_service = "Get UI Value requires a game world",
    .reusable = true
};
