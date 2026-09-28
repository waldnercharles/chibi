#include "runtime/ui.h"

#include "graph/document/serialize.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/flow.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/yyjson/yyjson.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct game_ui
{
    game_ui_element *elements;
    game_ui_datum *data;
    uint64_t revision;
};

game_ui *game_ui_new()
{
    return xcalloc(1, sizeof(game_ui));
}

static void element_free(game_ui_element *e)
{
    free(e->selector);
    free(e->text);
    for (int i = 0; i < acount(e->classes); i++) {
        free(e->classes[i].name);
    }
    afree(e->classes);
}

static void datum_free(game_ui_datum *d)
{
    free(d->name);
    free(d->json);
}

void game_ui_reset(game_ui *ui)
{
    for (int i = 0; i < acount(ui->elements); i++) {
        element_free(&ui->elements[i]);
    }
    afree(ui->elements);
    for (int i = 0; i < acount(ui->data); i++) {
        datum_free(&ui->data[i]);
    }
    afree(ui->data);
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
        if (e->entity == entity && strcmp(e->selector, element) == 0) {
            return e;
        }
    }
    return NULL;
}

/* The element to change, created on first use and stamped with a new
 * revision, or NULL for an empty selector. */
static game_ui_element *change(game_ui *ui, uint64_t entity, const char *element)
{
    if (!element || !element[0]) {
        return NULL;
    }
    game_ui_element *e = find(ui, entity, element);
    if (!e) {
        apush(
            ui->elements,
            ((game_ui_element){ .entity = entity, .selector = xstrdup(element), .visible = -1 })
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

bool game_ui_click(game_ui *ui, uint64_t entity, const char *element)
{
    game_ui_element *e = change(ui, entity, element);
    if (!e) {
        return false;
    }
    e->click = true;
    return true;
}

bool game_ui_move_focus(game_ui *ui, uint64_t entity, const char *element, int step)
{
    game_ui_element *e = step ? change(ui, entity, element) : NULL;
    if (!e) {
        return false;
    }
    e->move_focus = step;
    return true;
}

/* A path bindings can reach under self: a word, then words or list indices,
   each after a '.': "score", "items.2.name". */
static bool data_path(const char *path)
{
    if (!path || !(isalpha((unsigned char)path[0]) || path[0] == '_')) {
        return false;
    }
    for (const char *segment = path; segment;) {
        size_t length = strcspn(segment, ".");
        bool index = length && isdigit((unsigned char)segment[0]);
        for (size_t i = 0; i < length; i++) {
            char c = segment[i];
            if (index ? !isdigit((unsigned char)c)
                      : !isalnum((unsigned char)c) && c != '_') {
                return false;
            }
        }
        if (!length) {
            return false;
        }
        segment = segment[length] ? segment + length + 1 : NULL;
    }
    return true;
}

bool game_ui_set_data(game_ui *ui, uint64_t entity, const char *name, const char *json)
{
    yyjson_doc *doc = data_path(name) && json ? yyjson_read(json, strlen(json), 0)
                                              : NULL;
    if (!doc) {
        return false;
    }
    yyjson_doc_free(doc);
    game_ui_datum *d = NULL;
    for (int i = 0; !d && i < acount(ui->data); i++) {
        d = ui->data[i].entity == entity && !strcmp(ui->data[i].name, name)
              ? &ui->data[i]
              : NULL;
    }
    if (d && !strcmp(d->json, json)) {
        return true;
    }
    if (!d) {
        apush(ui->data, ((game_ui_datum){ .entity = entity, .name = xstrdup(name) }));
        d = &alast(ui->data);
    }
    free(d->json);
    d->json = xstrdup(json);
    d->revision = ++ui->revision;
    return true;
}

int game_ui_data_count(const game_ui *ui)
{
    return acount(ui->data);
}

const game_ui_datum *game_ui_data_at(const game_ui *ui, int index)
{
    assert(index >= 0 && index < acount(ui->data));
    return &ui->data[index];
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
    int kept = 0, before = acount(ui->elements) + acount(ui->data);
    for (int i = 0; i < acount(ui->elements); i++) {
        if (ui->elements[i].entity == entity) {
            element_free(&ui->elements[i]);
            continue;
        }
        ui->elements[kept++] = ui->elements[i];
    }
    asetlen(ui->elements, kept);
    kept = 0;
    for (int i = 0; i < acount(ui->data); i++) {
        if (ui->data[i].entity == entity) {
            datum_free(&ui->data[i]);
            continue;
        }
        ui->data[kept++] = ui->data[i];
    }
    asetlen(ui->data, kept);
    if (acount(ui->elements) + acount(ui->data) != before) {
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

/* Each UI node is one typed host call on game.ui, shared by the interpreter
   and compiled behaviors. Arguments are the data inputs in order: Entity,
   Element, then the node's own values. */

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

static const char *get_call(void *ui, const pin_value *a, pin_value *results)
{
    float value = 0;
    results[1].b = game_ui_value(ui, a[0].u64, a[1].str, &value);
    results[0].f = value;
    return NULL;
}

/* The interpreted path evaluates the data inputs and makes the same call. */
static int ui_flow(node *n, flow_context *ctx)
{
    game_ui *ui = flow_context_service(ctx, "game.ui");
    if (!ui) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, MISSING_UI);
    }
    pin_value values[4];
    int count = n->desc->input_count - 1;
    assert(count <= (int)(sizeof values / sizeof *values));
    for (int i = 0; i < count; i++) {
        int result = flow_context_eval_data(ctx, i + 1, &values[i]);
        if (result < 0) {
            return result;
        }
    }
    const char *error = n->desc->host_call->invoke(ui, values, NULL);
    return error ? flow_context_fail(ctx, FLOW_ERR_ARGUMENT, error)
                 : flow_context_continue(ctx, 0);
}

static const char *get_value(const eval_context *ctx, const pin_value *inputs, arena *, pin_value *out)
{
    void *ui = eval_context_service(ctx, "game.ui");
    if (!ui) {
        return "Get UI Value requires a game world";
    }
    out[0] = (pin_value){ .type = PIN_F32, .type_name = "float" };
    out[1] = (pin_value){ .type = PIN_BOOL, .type_name = "bool" };
    return get_call(ui, inputs, out);
}

#define EXEC_IN { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC }
#define ENTITY_IN                                                              \
    { .name = "Entity", .type = PIN_HANDLE, .type_name = "ecs.entity", .flags = PIN_FLAG_DEFAULT_SELF }
#define ELEMENT_IN                                                             \
    { .name = "Element", .type = PIN_STR, .type_name = "const char *" }

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
static pin_spec MOVE_FOCUS_IN[] = { EXEC_IN,
                                    ENTITY_IN,
                                    ELEMENT_IN,
                                    { .name = "Step",
                                      .type = PIN_I32,
                                      .type_name = "int",
                                      .default_value = { .type = PIN_I32, .i = 1 } } };
static pin_spec THEN_OUT[] = { { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC } };
static pin_spec GET_IN[] = { ENTITY_IN, ELEMENT_IN };
static pin_spec GET_OUT[] = {
    { .name = "Value", .type = PIN_F32, .type_name = "float" },
    { .name = "Known", .type = PIN_BOOL, .type_name = "bool" }
};

static const node_access WRITE = { .flags = NODE_EFFECT_HOST_SINK };
static const node_access READ = { .flags = NODE_EFFECT_WORLD_READ };

#define EFFECT_NODE(ident, label, words, text, pins, call)                        \
    static const node_host_call ident##_CALL = { .service = "game.ui",            \
                                                 .invoke = call,                  \
                                                 .missing_service = MISSING_UI }; \
    static node_desc ident = { .name = label,                                     \
                               .category = "UI",                                  \
                               .keywords = words,                                 \
                               .description = text,                               \
                               .input_count = sizeof pins / sizeof *pins,         \
                               .inputs = pins,                                    \
                               .output_count = 1,                                 \
                               .outputs = THEN_OUT,                               \
                               .flow_kind = NODE_FLOW_CUSTOM,                     \
                               .flow = ui_flow,                                   \
                               .host_call = &ident##_CALL,                        \
                               .display_class = NODE_DISPLAY_FLOW,                \
                               .access = &WRITE }

EFFECT_NODE(
    SET_TEXT,
    "Set UI Text",
    "label hud score caption widget element selector",
    "Sets the text of the entity's UI elements that match the Element "
    "selector.",
    TEXT_IN,
    text_call
);
EFFECT_NODE(
    SET_CLASS,
    "Set UI Class",
    "style state highlight rcss hud widget element selector",
    "Adds or removes a class on the entity's UI elements that match the "
    "Element selector.",
    CLASS_IN,
    class_call
);
EFFECT_NODE(
    SET_VISIBLE,
    "Set UI Visible",
    "show hide display widget menu hud element selector",
    "Shows or hides the entity's UI elements that match the Element selector.",
    VISIBLE_IN,
    visible_call
);
EFFECT_NODE(
    SET_VALUE,
    "Set UI Value",
    "progress slider meter bar hud widget element selector",
    "Sets the value of the entity's sliders, meters and inputs that match the "
    "Element selector.",
    VALUE_IN,
    value_call
);
EFFECT_NODE(
    FOCUS,
    "Focus UI Element",
    "keyboard select input widget button selector",
    "Gives keyboard focus to the entity's first UI element that matches the "
    "Element selector.",
    FOCUS_IN,
    focus_call
);
EFFECT_NODE(
    CLICK,
    "Click UI Element",
    "press activate button choose widget selector",
    "Clicks the entity's first UI element that matches the Element selector, "
    "as a pointer would.",
    FOCUS_IN,
    click_call
);
EFFECT_NODE(
    MOVE_FOCUS,
    "Move UI Focus",
    "next previous navigate arrow keyboard menu widget element selector",
    "Moves keyboard focus Step places through the entity's UI elements that "
    "match the Element selector.",
    MOVE_FOCUS_IN,
    move_focus_call
);

/* Any value converts to JSON, so the node takes a wildcard; an array does
   not fit a host call's signature, so it runs as its own flow. */
static int set_data_flow(node *, flow_context *ctx)
{
    game_ui *ui = flow_context_service(ctx, "game.ui");
    if (!ui) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, MISSING_UI);
    }
    pin_value values[3];
    for (int i = 0; i < 3; i++) {
        int result = flow_context_eval_data(ctx, i + 1, &values[i]);
        if (result < 0) {
            return result;
        }
    }
    char *json = serialize_value_json(&values[2]);
    bool set = game_ui_set_data(ui, values[0].u64, values[1].str, json);
    free(json);
    return set ? flow_context_continue(ctx, 0)
               : flow_context_fail(ctx, FLOW_ERR_ARGUMENT, "Name must be a word, or a path such as items.2.name");
}

static pin_spec DATA_IN[] = {
    EXEC_IN,
    ENTITY_IN,
    { .name = "Name", .type = PIN_NAME, .type_name = "name" },
    { .name = "Value", .type = PIN_FLOW, .kind = PIN_DATA, .flags = PIN_FLAG_WILDCARD },
};
static node_desc SET_DATA = {
    .name = "Set UI Data",
    .category = "UI",
    .keywords = "bind binding self list rows table inventory leaderboard "
                "data-for record json hud widget",
    .description = "Gives the entity's UI documents Value to bind as "
                   "self.<Name>: text, a number, a struct's fields, or a list "
                   "data-for repeats; a path such as items.2.name sets one "
                   "row's field.",
    .input_count = sizeof DATA_IN / sizeof *DATA_IN,
    .inputs = DATA_IN,
    .output_count = 1,
    .outputs = THEN_OUT,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = set_data_flow,
    .display_class = NODE_DISPLAY_FLOW,
    .access = &WRITE
};

static const node_host_call GET_CALL = {
    .service = "game.ui",
    .invoke = get_call,
    .missing_service = "Get UI Value requires a game world",
    .reusable = true
};
static node_desc GET_VALUE = {
    .name = "Get UI Value",
    .category = "UI",
    .keywords = "slider input progress read widget element selector",
    .description = "Reads the value last set under the Element selector, or "
                   "a player's input under #id; Known is false until there is "
                   "one.",
    .input_count = 2,
    .inputs = GET_IN,
    .output_count = 2,
    .outputs = GET_OUT,
    .pure_kind = NODE_PURE_READ,
    .read_inputs = get_value,
    .host_call = &GET_CALL,
    .access = &READ
};

static node_desc *const NODES[] = { &SET_TEXT,  &SET_CLASS,  &SET_VISIBLE,
                                    &SET_VALUE, &SET_DATA,   &FOCUS,
                                    &CLICK,     &MOVE_FOCUS, &GET_VALUE };
const registry_pack GAME_UI_PACK = { .name = "game.ui",
                                     .nodes = NODES,
                                     .node_count = sizeof NODES / sizeof *NODES };
