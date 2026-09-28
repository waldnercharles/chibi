#ifndef CHIBI_GAME_UI_H
#define CHIBI_GAME_UI_H

#include "graph/model/registry.h"

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
    bool focus;        /* requested and not yet applied */
    bool click;        /* requested and not yet applied */
    int move_focus;    /* a requested step through the matches; 0 for none */
    uint64_t revision; /* the ui revision of its last change */
} game_ui_element;

/* A value behaviors gave an entity's documents, which bind it as
 * self.<name>: a number, text, flag, list or record, as JSON text. */
typedef struct game_ui_datum
{
    uint64_t entity;
    char *name;
    char *json;
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
/* The value last set under this selector, or false when none was. */
bool game_ui_value(const game_ui *ui, uint64_t entity, const char *element, float *value);
/* Sets self.<name> for the entity's documents to `json`; false, changing
 * nothing, for text that is not JSON or a name that is not a path: a word,
 * then words or list indices after dots ("items.2.name"). */
bool game_ui_set_data(game_ui *ui, uint64_t entity, const char *name, const char *json);
/* Drops an entity's elements and data, as when its GameUi goes away. */
void game_ui_forget(game_ui *ui, uint64_t entity);

/* For presenters: bumped by every change. */
uint64_t game_ui_revision(const game_ui *ui);
int game_ui_element_count(const game_ui *ui);
game_ui_element *game_ui_element_at(game_ui *ui, int index);
int game_ui_data_count(const game_ui *ui);
const game_ui_datum *game_ui_data_at(const game_ui *ui, int index);

extern const registry_pack GAME_UI_PACK;

#endif
