#include "runtime/ui.h"

#include "support/array.h"
#include "support/xalloc.h"

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
    for (int i = 0; i < acount(e->styles); i++) {
        free(e->styles[i].property);
        free(e->styles[i].value);
    }
    afree(e->styles);
}

static void datum_free(game_ui_datum *d)
{
    free(d->name);
    av_value_free(d->value);
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
            ((game_ui_element){ .entity = entity,
                                .selector = xstrdup(element),
                                .visible = -1,
                                .reveal = -1 })
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

/* An RCSS property name: lowercase words joined by '-'. */
static bool property_name(const char *name)
{
    if (!name || !islower((unsigned char)name[0])) {
        return false;
    }
    for (const char *c = name; *c; c++) {
        if (!islower((unsigned char)*c) && *c != '-') {
            return false;
        }
    }
    return true;
}

bool game_ui_set_style(game_ui *ui, uint64_t entity, const char *element, const char *property, const char *value)
{
    game_ui_element *e = property_name(property) && value
                           ? change(ui, entity, element)
                           : NULL;
    if (!e) {
        return false;
    }
    game_ui_style *style = NULL;
    for (int i = 0; !style && i < acount(e->styles); i++) {
        style = !strcmp(e->styles[i].property, property) ? &e->styles[i] : NULL;
    }
    if (!style) {
        apush(e->styles, ((game_ui_style){ .property = xstrdup(property) }));
        style = &alast(e->styles);
    }
    free(style->value);
    style->value = value[0] ? xstrdup(value) : NULL;
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

bool game_ui_show_popover(game_ui *ui, uint64_t entity, const char *element, float x, float y)
{
    game_ui_element *e = isfinite(x) && isfinite(y) ? change(ui, entity, element)
                                                    : NULL;
    if (!e) {
        return false;
    }
    e->popover = true;
    e->popover_x = x;
    e->popover_y = y;
    return true;
}

bool game_ui_reveal_row(game_ui *ui, uint64_t entity, const char *element, int row)
{
    game_ui_element *e = row >= 0 ? change(ui, entity, element) : NULL;
    if (!e) {
        return false;
    }
    e->reveal = row;
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

bool game_ui_set_data(game_ui *ui, uint64_t entity, const char *name, const av_value *value)
{
    if (!data_path(name) || !value) {
        return false;
    }
    game_ui_datum *d = NULL;
    for (int i = 0; !d && i < acount(ui->data); i++) {
        d = ui->data[i].entity == entity && !strcmp(ui->data[i].name, name)
              ? &ui->data[i]
              : NULL;
    }
    if (d && av_equals(d->value, value)) {
        return true;
    }
    if (!d) {
        apush(ui->data, ((game_ui_datum){ .entity = entity, .name = xstrdup(name) }));
        d = &alast(ui->data);
    }
    av_value_free(d->value);
    d->value = av_value_copy(value);
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

void game_ui_measure(
    game_ui *ui,
    uint64_t entity,
    const char *element,
    float scroll_top,
    float view_height,
    float content_height
)
{
    assert(element && element[0]);
    game_ui_element *e = find(ui, entity, element);
    if (!e) {
        apush(
            ui->elements,
            ((game_ui_element){ .entity = entity,
                                .selector = xstrdup(element),
                                .visible = -1,
                                .reveal = -1 })
        );
        e = &alast(ui->elements);
    }
    e->measured = true;
    e->scroll_top = scroll_top;
    e->view_height = view_height;
    e->content_height = content_height;
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

bool game_ui_scroll(const game_ui *ui, uint64_t entity, const char *selector, float values[3])
{
    const game_ui_element *element = selector ? find(ui, entity, selector) : NULL;
    bool known = element && element->measured;
    values[0] = known ? element->scroll_top : 0;
    values[1] = known ? element->view_height : 0;
    values[2] = known ? element->content_height : 0;
    return known;
}
