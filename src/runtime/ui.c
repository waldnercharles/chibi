#include "runtime/ui.h"

#include "graph/runtime/eval.h"
#include "graph/runtime/flow.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct game_ui
{
    game_ui_element *elements;
    uint64_t revision;
};

game_ui *game_ui_new()
{
    return xcalloc(1, sizeof(game_ui));
}

static void element_free(game_ui_element *e)
{
    free(e->id);
    free(e->text);
    for (int i = 0; i < acount(e->classes); i++) {
        free(e->classes[i].name);
    }
    afree(e->classes);
}

void game_ui_reset(game_ui *ui)
{
    for (int i = 0; i < acount(ui->elements); i++) {
        element_free(&ui->elements[i]);
    }
    afree(ui->elements);
    ui->revision++;
}

void game_ui_free(game_ui *ui)
{
    if (!ui) {
        return;
    }
    game_ui_reset(ui);
    free(ui);
}

static game_ui_element *find(const game_ui *ui, uint64_t entity, const char *element)
{
    for (int i = 0; i < acount(ui->elements); i++) {
        game_ui_element *e = &ui->elements[i];
        if (e->entity == entity && strcmp(e->id, element) == 0) {
            return e;
        }
    }
    return NULL;
}

/* The element to change, created on first use and stamped with a new
 * revision, or NULL for an empty id. */
static game_ui_element *change(game_ui *ui, uint64_t entity, const char *element)
{
    if (!element || !element[0]) {
        return NULL;
    }
    game_ui_element *e = find(ui, entity, element);
    if (!e) {
        apush(
            ui->elements,
            ((game_ui_element){ .entity = entity, .id = xstrdup(element), .visible = -1 })
        );
        e = &alast(ui->elements);
    }
    e->revision = ++ui->revision;
    return e;
}

bool game_ui_set_text(game_ui *ui, uint64_t entity, const char *element, const char *text)
{
    game_ui_element *e = text ? change(ui, entity, element) : NULL;
    if (!e) {
        return false;
    }
    free(e->text);
    e->text = xstrdup(text);
    return true;
}

bool game_ui_set_class(game_ui *ui, uint64_t entity, const char *element, const char *name, bool on)
{
    game_ui_element *e = name && name[0] ? change(ui, entity, element) : NULL;
    if (!e) {
        return false;
    }
    for (int i = 0; i < acount(e->classes); i++) {
        if (strcmp(e->classes[i].name, name) == 0) {
            e->classes[i].on = on;
            return true;
        }
    }
    apush(e->classes, ((game_ui_class){ xstrdup(name), on }));
    return true;
}

bool game_ui_set_visible(game_ui *ui, uint64_t entity, const char *element, bool visible)
{
    game_ui_element *e = change(ui, entity, element);
    if (!e) {
        return false;
    }
    e->visible = visible;
    return true;
}

bool game_ui_set_value(game_ui *ui, uint64_t entity, const char *element, float value)
{
    game_ui_element *e = isfinite(value) ? change(ui, entity, element) : NULL;
    if (!e) {
        return false;
    }
    e->has_value = true;
    e->value = value;
    return true;
}

bool game_ui_focus(game_ui *ui, uint64_t entity, const char *element)
{
    game_ui_element *e = change(ui, entity, element);
    if (!e) {
        return false;
    }
    e->focus = true;
    return true;
}

bool game_ui_value(const game_ui *ui, uint64_t entity, const char *element, float *value)
{
    const game_ui_element *e = element ? find(ui, entity, element) : NULL;
    if (!e || !e->has_value) {
        return false;
    }
    *value = e->value;
    return true;
}

void game_ui_forget(game_ui *ui, uint64_t entity)
{
    int kept = 0;
    for (int i = 0; i < acount(ui->elements); i++) {
        if (ui->elements[i].entity == entity) {
            element_free(&ui->elements[i]);
            continue;
        }
        ui->elements[kept++] = ui->elements[i];
    }
    if (kept != acount(ui->elements)) {
        asetlen(ui->elements, kept);
        ui->revision++;
    }
}

uint64_t game_ui_revision(const game_ui *ui)
{
    return ui->revision;
}

int game_ui_element_count(const game_ui *ui)
{
    return acount(ui->elements);
}

game_ui_element *game_ui_element_at(game_ui *ui, int index)
{
    assert(index >= 0 && index < acount(ui->elements));
    return &ui->elements[index];
}

/* ---------- Nodes ---------- */

typedef enum
{
    UI_TEXT,
    UI_CLASS,
    UI_VISIBLE,
    UI_VALUE,
    UI_FOCUS,
} ui_effect;

/* Evaluates the Entity and Element inputs every UI node starts with. */
static int target(flow_context *ctx, game_ui **ui, uint64_t *entity, const char **element)
{
    *ui = flow_context_service(ctx, "game.ui");
    if (!*ui) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "UI nodes require a game world");
    }
    pin_value values[2];
    for (int i = 0; i < 2; i++) {
        int result = flow_context_eval_data(ctx, i + 1, &values[i]);
        if (result < 0) {
            return result;
        }
    }
    *entity = values[0].u64;
    *element = values[1].str;
    return 0;
}

static int apply(flow_context *ctx, ui_effect effect)
{
    game_ui *ui;
    uint64_t entity;
    const char *element;
    int result = target(ctx, &ui, &entity, &element);
    if (result < 0) {
        return result;
    }
    pin_value a = { 0 }, b = { 0 };
    if (effect != UI_FOCUS && (result = flow_context_eval_data(ctx, 3, &a)) < 0) {
        return result;
    }
    if (effect == UI_CLASS && (result = flow_context_eval_data(ctx, 4, &b)) < 0) {
        return result;
    }
    bool ok = false;
    switch (effect) {
        case UI_TEXT:
            ok = game_ui_set_text(ui, entity, element, a.str ? a.str : "");
            break;
        case UI_CLASS:
            ok = game_ui_set_class(ui, entity, element, a.str, b.b);
            break;
        case UI_VISIBLE:
            ok = game_ui_set_visible(ui, entity, element, a.b);
            break;
        case UI_VALUE: ok = game_ui_set_value(ui, entity, element, a.f); break;
        case UI_FOCUS: ok = game_ui_focus(ui, entity, element); break;
    }
    if (!ok) {
        return flow_context_fail(ctx, FLOW_ERR_ARGUMENT, "Name an element and a valid value");
    }
    return flow_context_continue(ctx, 0);
}

static int set_text(node *, flow_context *ctx)
{
    return apply(ctx, UI_TEXT);
}

static int set_class(node *, flow_context *ctx)
{
    return apply(ctx, UI_CLASS);
}

static int set_visible(node *, flow_context *ctx)
{
    return apply(ctx, UI_VISIBLE);
}

static int set_value(node *, flow_context *ctx)
{
    return apply(ctx, UI_VALUE);
}

static int focus(node *, flow_context *ctx)
{
    return apply(ctx, UI_FOCUS);
}

static const char *get_value(const eval_context *ctx, const pin_value *inputs, arena *, pin_value *out)
{
    const game_ui *ui = eval_context_service(ctx, "game.ui");
    if (!ui) {
        return "Get UI Value requires a game world";
    }
    float value = 0;
    bool known = game_ui_value(ui, inputs[0].u64, inputs[1].str, &value);
    out[0] = (pin_value){ .type = PIN_F32, .type_name = "float", .f = value };
    out[1] = (pin_value){ .type = PIN_BOOL, .type_name = "bool", .b = known };
    return NULL;
}

#define EXEC_IN { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC }
#define ENTITY_IN                                                              \
    { .name = "Entity", .type = PIN_HANDLE, .type_name = "ecs.entity", .flags = PIN_FLAG_DEFAULT_SELF }
#define ELEMENT_IN { .name = "Element", .type = PIN_NAME, .type_name = "name" }

static pin_spec TEXT_IN[] = {
    EXEC_IN,
    ENTITY_IN,
    ELEMENT_IN,
    { .name = "Text", .type = PIN_STR, .type_name = "const char *" }
};
static pin_spec CLASS_IN[] = { EXEC_IN,
                               ENTITY_IN,
                               ELEMENT_IN,
                               { .name = "Class", .type = PIN_NAME, .type_name = "name" },
                               { .name = "On",
                                 .type = PIN_BOOL,
                                 .type_name = "bool",
                                 .default_value = { .type = PIN_BOOL, .b = true } } };
static pin_spec VISIBLE_IN[] = { EXEC_IN,
                                 ENTITY_IN,
                                 ELEMENT_IN,
                                 { .name = "Visible",
                                   .type = PIN_BOOL,
                                   .type_name = "bool",
                                   .default_value = { .type = PIN_BOOL, .b = true } } };
static pin_spec VALUE_IN[] = { EXEC_IN,
                               ENTITY_IN,
                               ELEMENT_IN,
                               { .name = "Value", .type = PIN_F32, .type_name = "float" } };
static pin_spec FOCUS_IN[] = { EXEC_IN, ENTITY_IN, ELEMENT_IN };
static pin_spec THEN_OUT[] = { { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC } };
static pin_spec GET_IN[] = { ENTITY_IN, ELEMENT_IN };
static pin_spec GET_OUT[] = {
    { .name = "Value", .type = PIN_F32, .type_name = "float" },
    { .name = "Known", .type = PIN_BOOL, .type_name = "bool" }
};

static const node_access WRITE = { .flags = NODE_EFFECT_HOST_SINK };
static const node_access READ = { .flags = NODE_EFFECT_WORLD_READ };

#define EFFECT_NODE(ident, label, words, pins, fn)                             \
    static node_desc ident = { .name = label,                                  \
                               .category = "UI",                               \
                               .keywords = words,                              \
                               .input_count = sizeof pins / sizeof *pins,      \
                               .inputs = pins,                                 \
                               .output_count = 1,                              \
                               .outputs = THEN_OUT,                            \
                               .flow_kind = NODE_FLOW_CUSTOM,                  \
                               .flow = fn,                                     \
                               .display_class = NODE_DISPLAY_FLOW,             \
                               .access = &WRITE }

EFFECT_NODE(SET_TEXT, "Set UI Text", "label hud score caption", TEXT_IN, set_text);
EFFECT_NODE(SET_CLASS, "Set UI Class", "style state highlight rcss", CLASS_IN, set_class);
EFFECT_NODE(SET_VISIBLE, "Set UI Visible", "show hide display", VISIBLE_IN, set_visible);
EFFECT_NODE(SET_VALUE, "Set UI Value", "progress slider meter bar", VALUE_IN, set_value);
EFFECT_NODE(FOCUS, "Focus UI Element", "keyboard select input", FOCUS_IN, focus);

static node_desc GET_VALUE = { .name = "Get UI Value",
                               .category = "UI",
                               .keywords = "slider input progress read",
                               .input_count = 2,
                               .inputs = GET_IN,
                               .output_count = 2,
                               .outputs = GET_OUT,
                               .pure_kind = NODE_PURE_READ,
                               .read_inputs = get_value,
                               .access = &READ };

static node_desc *const NODES[] = { &SET_TEXT,  &SET_CLASS, &SET_VISIBLE,
                                    &SET_VALUE, &FOCUS,     &GET_VALUE };
const registry_pack GAME_UI_PACK = { .name = "game.ui",
                                     .nodes = NODES,
                                     .node_count = sizeof NODES / sizeof *NODES };
