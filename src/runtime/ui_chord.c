#include "runtime/ui_chord.h"

#include <stdio.h>
#include <string.h>

static bool key_valid(const char *key)
{
    if (key[0] && !key[1] &&
        ((key[0] >= 'A' && key[0] <= 'Z') || (key[0] >= '0' && key[0] <= '9'))) {
        return true;
    }
    for (int i = 1; i <= 12; i++) {
        char f[4];
        snprintf(f, sizeof f, "F%d", i);
        if (!strcmp(key, f)) {
            return true;
        }
    }
    const char *keys[] = { "Space",  "Enter", "Escape", "Tab",      "Backspace",
                           "Delete", "Up",    "Down",   "Left",     "Right",
                           "Home",   "End",   "PageUp", "PageDown", "Comma",
                           "Period", "Plus",  "Minus" };
    for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) {
        if (!strcmp(key, keys[i])) {
            return true;
        }
    }
    return false;
}

bool ui_chord_valid(const char *chord)
{
    if (!chord || !chord[0] || strlen(chord) > 128 || strpbrk(chord, ": \t\r\n")) {
        return false;
    }
    const char *key = strrchr(chord, '+');
    key = key ? key + 1 : chord;
    if (!key_valid(key)) {
        return false;
    }
    const char *part = chord;
    unsigned seen = 0;
    while (part < key) {
        const char *plus = strchr(part, '+');
        size_t length = (size_t)(plus - part);
        const char *names[] = { "Ctrl", "Shift", "Alt", "Meta", "Mod" };
        unsigned bit = 0;
        for (int i = 0; i < 5; i++) {
            if (strlen(names[i]) == length && !memcmp(part, names[i], length)) {
                bit = 1u << i;
            }
        }
        if (!bit || (seen & bit)) {
            return false;
        }
        seen |= bit;
        part = plus + 1;
    }
    return true;
}

/* The binding matches exactly the pressed chord, excluding lock modifiers. */
bool ui_chord_binding(
    const char *bindings,
    const char *key,
    unsigned modifiers,
    unsigned primary,
    char *event,
    size_t size
)
{
    unsigned mask = UI_CHORD_CTRL | UI_CHORD_META | UI_CHORD_ALT | UI_CHORD_SHIFT;
    for (const char *p = bindings; *p;) {
        p += strspn(p, " \t\r\n");
        const char *end = p + strcspn(p, " \t\r\n");
        const char *colon = memchr(p, ':', (size_t)(end - p));
        if (!colon) {
            p = end;
            continue;
        }
        char chord[129];
        size_t chord_length = (size_t)(colon - p);
        if (chord_length >= sizeof chord) {
            p = end;
            continue;
        }
        memcpy(chord, p, chord_length);
        chord[chord_length] = '\0';
        if (!ui_chord_valid(chord)) {
            p = end;
            continue;
        }
        unsigned required = 0;
        const char *part = p;
        bool valid = true;
        const char *plus;
        while ((plus = memchr(part, '+', (size_t)(colon - part)))) {
            size_t n = (size_t)(plus - part);
            if (n == 3 && !memcmp(part, "Mod", n)) {
                required |= primary;
            } else if (n == 4 && !memcmp(part, "Ctrl", n)) {
                required |= UI_CHORD_CTRL;
            } else if (n == 4 && !memcmp(part, "Meta", n)) {
                required |= UI_CHORD_META;
            } else if (n == 3 && !memcmp(part, "Alt", n)) {
                required |= UI_CHORD_ALT;
            } else if (n == 5 && !memcmp(part, "Shift", n)) {
                required |= UI_CHORD_SHIFT;
            } else {
                valid = false;
            }
            part = plus + 1;
        }
        size_t length = (size_t)(end - colon - 1);
        if (valid && (modifiers & mask) == required &&
            strlen(key) == (size_t)(colon - part) &&
            !memcmp(part, key, (size_t)(colon - part)) && length && length < size) {
            memcpy(event, colon + 1, length);
            event[length] = '\0';
            return true;
        }
        p = end;
    }
    if (size) {
        event[0] = '\0';
    }
    return false;
}
