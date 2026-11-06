#ifndef CHIBI_UI_H
#define CHIBI_UI_H

#include "support/asset_value.h"

#include <stdbool.h>
#include <stdint.h>

/* What behaviors set on the elements of Ui documents, keyed by entity and
 * the RCSS selector that picks the elements. A presenter applies each setting
 * to every match in the documents it shows and records input values back
 * under `#id`, so UI state stays headless and part of the world. */
typedef struct ui ui;

typedef struct ui_class
{
    char *name;
    bool on;
} ui_class;

/* An RCSS property set inline; a NULL value leaves it to the stylesheets. */
typedef struct ui_property_override
{
    char *property;
    char *value;
} ui_property_override;

typedef struct ui_element
{
    uint64_t entity;
    char *selector;
    char *text;                   /* NULL until set */
    ui_class *classes;            /* the last setting of each class */
    ui_property_override *styles; /* the last setting of each property */
    int visible;                  /* -1 until set, else 0 or 1 */
    bool has_value;
    float value;
    bool focus;     /* requested and not yet applied */
    bool click;     /* requested and not yet applied */
    int move_focus; /* a requested step through the matches; 0 for none */
    /* A requested popover opening at a point in the document's pixels. */
    bool popover;
    float popover_x, popover_y;
    int reveal; /* a requested list row to scroll into view; -1 for none */
    /* Where a presenter last measured the element scrolled, in dp. */
    bool measured;
    float scroll_top, view_height, content_height;
    uint64_t revision; /* the ui revision of its last change */
} ui_element;

/* A value behaviors gave an entity's documents, which bind it as
 * self.<name>: a number, text, flag, list or record, owned as a native value. */
typedef struct ui_datum
{
    uint64_t entity;
    char *name;
    av_value *value;
    uint64_t revision; /* the ui revision of its last change */
} ui_datum;

ui *ui_new();
void ui_free(ui *ui);
void ui_reset(ui *ui);

/* Each returns false, changing nothing, for an empty selector or an invalid
 * argument. */
bool ui_set_text(ui *ui, uint64_t entity, const char *element, const char *text);
bool ui_set_class(ui *ui, uint64_t entity, const char *element, const char *name, bool on);
bool ui_set_visible(ui *ui, uint64_t entity, const char *element, bool visible);
/* Sets an RCSS property, such as "width" to "40%", on the matches; an empty
 * value returns it to the stylesheets. */
bool ui_set_style(ui *ui, uint64_t entity, const char *element, const char *property, const char *value);
bool ui_set_value(ui *ui, uint64_t entity, const char *element, float value);
bool ui_focus(ui *ui, uint64_t entity, const char *element);
/* Clicks the first element the selector matches, as the player would. */
bool ui_click(ui *ui, uint64_t entity, const char *element);
/* Moves focus `step` places through the elements the selector matches, from
 * the one that holds focus, wrapping at the ends. */
bool ui_move_focus(ui *ui, uint64_t entity, const char *element, int step);
/* Opens the first match, a popover, at (`x`, `y`) in the document's pixels,
 * as a context menu opens at the pointer. */
bool ui_show_popover(ui *ui, uint64_t entity, const char *element, float x, float y);
/* Scrolls row `row` of the matches, lists their presenter windows
 * (data-list-rows), into view once, as Virtuoso's scrollToIndex does. */
bool ui_reveal_row(ui *ui, uint64_t entity, const char *element, int row);
/* Records where a presenter measured the element scrolled, in dp: how far
 * down, and the heights of its view and its content. A measurement is not a
 * setting, so presenters do not apply it back. */
void ui_measure(ui *ui, uint64_t entity, const char *element, float scroll_top, float view_height, float content_height);
/* The value last set under this selector, or false when none was. */
bool ui_value(const ui *ui, uint64_t entity, const char *element, float *value);
/* Copies `value` into self.<name>. False for a missing value or an invalid
 * path: a word, then words or list indices after dots ("items.2.name"). */
bool ui_set_data(ui *ui, uint64_t entity, const char *name, const av_value *value);
/* Drops an entity's elements and data, as when its Ui goes away. */
void ui_forget(ui *ui, uint64_t entity);

/* For presenters: bumped by every change. */
uint64_t ui_revision(const ui *ui);
int ui_element_count(const ui *ui);
ui_element *ui_element_at(ui *ui, int index);
int ui_data_count(const ui *ui);
const ui_datum *ui_data_at(const ui *ui, int index);

/* Last measured scroll/view/content heights; outputs zero when unknown. */
bool ui_scroll(const ui *, uint64_t entity, const char *selector, float values[3]);

#endif
