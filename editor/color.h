#ifndef CBLUEPRINT_EDITOR_COLOR_H
#define CBLUEPRINT_EDITOR_COLOR_H

#include <stdbool.h>

typedef struct editor_color_state
{
    char hex[9];
    bool changed;
} editor_color_state;

/* Edits packed 0xRRGGBBAA in a caller-owned local value. Snapshot the model
 * when first_change is true, before committing the local value. Opening starts
 * a new session; drags and Hex edits share one session until the popup closes.
 * State is zero-initialized and may serve one host's color controls. */
bool editor_color_draw(editor_color_state *state, const char *id, unsigned *value, bool *first_change);

/* Query immediately in the same host ID scope as draw. */
bool editor_color_open(const char *id);

#endif
