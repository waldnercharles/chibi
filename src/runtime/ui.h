#ifndef CHIBI_GAME_UI_H
#define CHIBI_GAME_UI_H

#include "support/asset_value.h"

#include <stdbool.h>
#include <stdint.h>

/* What behaviors set on the elements of GameUi documents, keyed by entity and
 * the RCSS selector that picks the elements. A presenter applies each setting
 * to every match in the documents it shows and records input values back
 * under `#id`, so UI state stays headless and part of the world. */
typedef struct game_ui game_ui;

typedef struct game_ui_class
{
    char *name;
    bool on;
} game_ui_class;

/* An RCSS property set inline; a NULL value leaves it to the stylesheets. */
typedef struct game_ui_style
{
    char *property;
    char *value;
} game_ui_style;

typedef struct game_ui_element
{
    uint64_t entity;
    char *selector;
    char *text;             /* NULL until set */
    game_ui_class *classes; /* the last setting of each class */
    game_ui_style *styles;  /* the last setting of each property */
    int visible;            /* -1 until set, else 0 or 1 */
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
} game_ui_element;

/* A value behaviors gave an entity's documents, which bind it as
 * self.<name>: a number, text, flag, list or record, owned as a native value. */
typedef struct game_ui_datum
{
    uint64_t entity;
    char *name;
    av_value *value;
    uint64_t revision; /* the ui revision of its last change */
} game_ui_datum;

game_ui *game_ui_new();
void game_ui_free(game_ui *ui);
void game_ui_reset(game_ui *ui);

/* Each returns false, changing nothing, for an empty selector or an invalid
 * argument. */
bool game_ui_set_text(game_ui *ui, uint64_t entity, const char *element, const char *text);
bool game_ui_set_class(game_ui *ui, uint64_t entity, const char *element, const char *name, bool on);
bool game_ui_set_visible(game_ui *ui, uint64_t entity, const char *element, bool visible);
/* Sets an RCSS property, such as "width" to "40%", on the matches; an empty
 * value returns it to the stylesheets. */
bool game_ui_set_style(
    game_ui *ui,
    uint64_t entity,
    const char *element,
    const char *property,
    const char *value
);
bool game_ui_set_value(game_ui *ui, uint64_t entity, const char *element, float value);
bool game_ui_focus(game_ui *ui, uint64_t entity, const char *element);
/* Clicks the first element the selector matches, as the player would. */
bool game_ui_click(game_ui *ui, uint64_t entity, const char *element);
/* Moves focus `step` places through the elements the selector matches, from
 * the one that holds focus, wrapping at the ends. */
bool game_ui_move_focus(game_ui *ui, uint64_t entity, const char *element, int step);
/* Opens the first match, a popover, at (`x`, `y`) in the document's pixels,
 * as a context menu opens at the pointer. */
bool game_ui_show_popover(game_ui *ui, uint64_t entity, const char *element, float x, float y);
/* Scrolls row `row` of the matches, lists their presenter windows
 * (data-list-rows), into view once, as Virtuoso's scrollToIndex does. */
bool game_ui_reveal_row(game_ui *ui, uint64_t entity, const char *element, int row);
/* Records where a presenter measured the element scrolled, in dp: how far
 * down, and the heights of its view and its content. A measurement is not a
 * setting, so presenters do not apply it back. */
void game_ui_measure(
    game_ui *ui,
    uint64_t entity,
    const char *element,
    float scroll_top,
    float view_height,
    float content_height
);
/* The value last set under this selector, or false when none was. */
bool game_ui_value(const game_ui *ui, uint64_t entity, const char *element, float *value);
/* Copies `value` into self.<name>. False for a missing value or an invalid
 * path: a word, then words or list indices after dots ("items.2.name"). */
bool game_ui_set_data(game_ui *ui, uint64_t entity, const char *name, const av_value *value);
/* Drops an entity's elements and data, as when its GameUi goes away. */
void game_ui_forget(game_ui *ui, uint64_t entity);

/* For presenters: bumped by every change. */
uint64_t game_ui_revision(const game_ui *ui);
int game_ui_element_count(const game_ui *ui);
game_ui_element *game_ui_element_at(game_ui *ui, int index);
int game_ui_data_count(const game_ui *ui);
const game_ui_datum *game_ui_data_at(const game_ui *ui, int index);

/* Last measured scroll/view/content heights; outputs zero when unknown. */
bool game_ui_scroll(const game_ui *, uint64_t entity, const char *selector, float values[3]);

#endif
