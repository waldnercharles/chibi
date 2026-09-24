/* Shortcuts follow the platform: Command is the primary modifier on macOS,
 * including the web build on a Mac, and Control elsewhere; chords are written
 * in each platform's notation (⇧⌘Z, Ctrl+Shift+Z). */
#include "editor/studio/studio_internal.h"

#include <stdio.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

EM_JS(bool, browser_is_mac, (), {
    const platform = (navigator.userAgentData && navigator.userAgentData.platform) ||
                     navigator.platform || "";
    return / mac | iphone | ipad | ipod / i.test(platform);
})
#endif

bool studio_detect_mac()
{
#if defined(__EMSCRIPTEN__)
    return browser_is_mac();
#elif defined(__APPLE__)
    return true;
#else
    return false;
#endif
}

bool studio_primary(const studio *s, SDL_Keymod mod)
{
    return mod & (s->mac ? SDL_KMOD_GUI : SDL_KMOD_CTRL);
}

/* `chord` joins Mod, Ctrl, Shift and Alt with a key by '+', as "Mod+Shift+Z". */
const char *studio_chord(const studio *s, const char *chord)
{
    static const struct
    {
        const char *name, *mac, *other;
        int mac_order;
    } MODIFIERS[] = {
        { "Ctrl", "\xe2\x8c\x83", "Ctrl", 0 },
        { "Alt", "\xe2\x8c\xa5", "Alt", 1 },
        { "Shift", "\xe2\x87\xa7", "Shift", 2 },
        { "Mod", "\xe2\x8c\x98", "Ctrl", 3 },
    };
    enum
    {
        MODIFIER_COUNT = sizeof MODIFIERS / sizeof *MODIFIERS
    };
    bool held[MODIFIER_COUNT] = { false };
    char key[32] = "";
    for (const char *at = chord; *at;) {
        const char *end = strchr(at, '+');
        size_t length = end ? (size_t)(end - at) : strlen(at);
        bool modifier = false;
        for (int m = 0; m < MODIFIER_COUNT; m++) {
            if (strlen(MODIFIERS[m].name) == length &&
                !strncmp(at, MODIFIERS[m].name, length)) {
                held[m] = modifier = true;
            }
        }
        if (!modifier) {
            snprintf(key, sizeof key, "%.*s", (int)length, at);
        }
        at += length + (end != NULL);
    }
    char text[64] = "";
    size_t used = 0;
    /* macOS writes ⌃⌥⇧⌘ then the key; others write Ctrl+Alt+Shift+key. */
    for (int order = 0; order < MODIFIER_COUNT; order++) {
        for (int m = 0; m < MODIFIER_COUNT; m++) {
            if (!held[m] || MODIFIERS[m].mac_order != order) {
                continue;
            }
            used += (size_t)snprintf(
                text + used,
                sizeof text - used,
                s->mac ? "%s" : "%s+",
                s->mac ? MODIFIERS[m].mac : MODIFIERS[m].other
            );
        }
    }
    bool word = strlen(key) > 1 && used && s->mac;
    snprintf(text + used, sizeof text - used, "%s%s", word ? " " : "", key);
    size_t length = strlen(text);
    if (!key[0] && length && text[length - 1] == '+') {
        text[length - 1] = '\0';
    }
    return kit_string(s->kit, text);
}
