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

typedef struct game_ui_element
{
    uint64_t entity;
    char *selector;
    char *text;             /* NULL until set */
    game_ui_class *classes; /* the last setting of each class */
    int visible;            /* -1 until set, else 0 or 1 */
    bool has_value;
    float value;
    bool focus;        /* requested and not yet applied */
    uint64_t revision; /* the ui revision of its last change */
} game_ui_element;

game_ui *game_ui_new();
void game_ui_free(game_ui *ui);
void game_ui_reset(game_ui *ui);

/* Each returns false, changing nothing, for an empty selector or an invalid
 * argument. */
bool game_ui_set_text(game_ui *ui, uint64_t entity, const char *element, const char *text);
bool game_ui_set_class(game_ui *ui, uint64_t entity, const char *element, const char *name, bool on);
bool game_ui_set_visible(game_ui *ui, uint64_t entity, const char *element, bool visible);
bool game_ui_set_value(game_ui *ui, uint64_t entity, const char *element, float value);
bool game_ui_focus(game_ui *ui, uint64_t entity, const char *element);
/* The value last set under this selector, or false when none was. */
bool game_ui_value(const game_ui *ui, uint64_t entity, const char *element, float *value);
/* Drops an entity's elements, as when its GameUi goes away. */
void game_ui_forget(game_ui *ui, uint64_t entity);

/* For presenters: bumped by every change. */
uint64_t game_ui_revision(const game_ui *ui);
int game_ui_element_count(const game_ui *ui);
game_ui_element *game_ui_element_at(game_ui *ui, int index);

extern const registry_pack GAME_UI_PACK;

#endif
