#include "runtime/ui_native.h"

#include "runtime/ui.h"
#include "types/value_document.h"

#include <cute_math.h>

static const char MISSING_UI[] = "UI nodes require a game world";
static const char INVALID_UI[] = "Select an element and give a valid value";

static const char *text_call(void *ui, const pin_value *a, pin_value *)
{
    return game_ui_set_text(ui, a[0].u64, a[1].str, a[2].str ? a[2].str : "")
             ? NULL
             : INVALID_UI;
}

static const char *class_call(void *ui, const pin_value *a, pin_value *)
{
    return game_ui_set_class(ui, a[0].u64, a[1].str, a[2].str, a[3].b) ? NULL : INVALID_UI;
}

static const char *style_call(void *ui, const pin_value *a, pin_value *)
{
    return game_ui_set_style(ui, a[0].u64, a[1].str, a[2].str, a[3].str ? a[3].str : "")
             ? NULL
             : INVALID_UI;
}

static const char *visible_call(void *ui, const pin_value *a, pin_value *)
{
    return game_ui_set_visible(ui, a[0].u64, a[1].str, a[2].b) ? NULL : INVALID_UI;
}

static const char *value_call(void *ui, const pin_value *a, pin_value *)
{
    return game_ui_set_value(ui, a[0].u64, a[1].str, a[2].f) ? NULL : INVALID_UI;
}

static const char *focus_call(void *ui, const pin_value *a, pin_value *)
{
    return game_ui_focus(ui, a[0].u64, a[1].str) ? NULL : INVALID_UI;
}

static const char *click_call(void *ui, const pin_value *a, pin_value *)
{
    return game_ui_click(ui, a[0].u64, a[1].str) ? NULL : INVALID_UI;
}

static const char *move_focus_call(void *ui, const pin_value *a, pin_value *)
{
    return game_ui_move_focus(ui, a[0].u64, a[1].str, a[2].i) ? NULL : INVALID_UI;
}

static const char *popover_call(void *ui, const pin_value *a, pin_value *)
{
    const CF_V2 *at = a[2].ptr;
    return at && game_ui_show_popover(ui, a[0].u64, a[1].str, at->x, at->y)
             ? NULL
             : INVALID_UI;
}

static const char *reveal_call(void *ui, const pin_value *a, pin_value *)
{
    return game_ui_reveal_row(ui, a[0].u64, a[1].str, a[2].i) ? NULL : INVALID_UI;
}

static const char *scroll_call(void *ui, const pin_value *a, pin_value *results)
{
    float values[3];
    results[3].b = game_ui_scroll(ui, a[0].u64, a[1].str, values);
    for (int i = 0; i < 3; i++) {
        results[i].f = values[i];
    }
    return NULL;
}

static const char *get_call(void *ui, const pin_value *a, pin_value *results)
{
    float value = 0;
    results[1].b = game_ui_value(ui, a[0].u64, a[1].str, &value);
    results[0].f = value;
    return NULL;
}

static const char *set_data_call(void *service, const pin_value *values, pin_value *)
{
    av_doc *doc = av_doc_new();
    av_value *value = pin_value_to_document(doc, &values[2]);
    bool set = game_ui_set_data(service, values[0].u64, values[1].str, value);
    av_doc_free(doc);
    return set ? NULL : "Name must be a word, or a path such as items.2.name";
}

const node_access GAME_UI_WRITE_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
const node_access GAME_UI_READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
const node_host_call GAME_UI_SET_TEXT_CALL = { .service = "game.ui",
                                               .invoke = text_call,
                                               .missing_service = MISSING_UI };
const node_host_call GAME_UI_SET_CLASS_CALL = { .service = "game.ui",
                                                .invoke = class_call,
                                                .missing_service = MISSING_UI };
const node_host_call GAME_UI_SET_STYLE_CALL = { .service = "game.ui",
                                                .invoke = style_call,
                                                .missing_service = MISSING_UI };
const node_host_call GAME_UI_SET_VISIBLE_CALL = { .service = "game.ui",
                                                  .invoke = visible_call,
                                                  .missing_service = MISSING_UI };
const node_host_call GAME_UI_SET_VALUE_CALL = { .service = "game.ui",
                                                .invoke = value_call,
                                                .missing_service = MISSING_UI };
const node_host_call GAME_UI_FOCUS_CALL = { .service = "game.ui",
                                            .invoke = focus_call,
                                            .missing_service = MISSING_UI };
const node_host_call GAME_UI_CLICK_CALL = { .service = "game.ui",
                                            .invoke = click_call,
                                            .missing_service = MISSING_UI };
const node_host_call GAME_UI_MOVE_FOCUS_CALL = { .service = "game.ui",
                                                 .invoke = move_focus_call,
                                                 .missing_service = MISSING_UI };
const node_host_call GAME_UI_SHOW_POPOVER_CALL = { .service = "game.ui",
                                                   .invoke = popover_call,
                                                   .missing_service = MISSING_UI };
const node_host_call GAME_UI_REVEAL_ROW_CALL = { .service = "game.ui",
                                                 .invoke = reveal_call,
                                                 .missing_service = MISSING_UI };
const node_host_call GAME_UI_SET_DATA_CALL = { .service = "game.ui",
                                               .invoke = set_data_call,
                                               .missing_service = MISSING_UI };
const node_host_call GAME_UI_GET_SCROLL_CALL = {
    .service = "game.ui",
    .invoke = scroll_call,
    .missing_service = "Get UI Scroll requires a game world",
    .reusable = true
};
const node_host_call GAME_UI_GET_VALUE_CALL = {
    .service = "game.ui",
    .invoke = get_call,
    .missing_service = "Get UI Value requires a game world",
    .reusable = true
};
