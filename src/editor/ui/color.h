#ifndef CHIBI_EDITOR_COLOR_H
#define CHIBI_EDITOR_COLOR_H

#include <stdbool.h>

typedef struct editor_color_state
{
    char hex[9];
    bool changed;
} editor_color_state;

/* Edits packed 0xRRGGBBAA in a caller-owned local value with RGB/HSV inputs and
 * an exact eight-digit Hex field. Snapshot the model when first_change is true,
 * before committing the local value. Opening starts a new session; picker and
 * Hex edits share one session until the popup closes. */
bool editor_color_draw(editor_color_state *state, const char *id, unsigned *value, bool *first_change);

/* Floating RGB(A) values retain emission intensities above one. */
bool editor_color_draw_float(
    editor_color_state *state,
    const char *id,
    float value[4],
    int channels,
    bool *first_change
);

/* Query immediately in the same host ID scope as draw. */
bool editor_color_open(const char *id);

#endif
