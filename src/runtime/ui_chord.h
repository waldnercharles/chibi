#pragma once
#include <stdbool.h>
#include <stddef.h>
/* Portable chord bits; renderers translate their input modifiers to these. */
enum
{
    UI_CHORD_CTRL = 1,
    UI_CHORD_SHIFT = 2,
    UI_CHORD_ALT = 4,
    UI_CHORD_META = 8
};
bool ui_chord_valid(const char *chord);
bool ui_chord_binding(
    const char *bindings,
    const char *key,
    unsigned modifiers,
    unsigned primary,
    char *event,
    size_t size
);
