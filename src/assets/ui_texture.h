#ifndef CHIBI_UI_TEXTURE_H
#define CHIBI_UI_TEXTURE_H
#include <stdbool.h>
#include <string.h>

enum
{
    UI_TEXTURE_KEY_MAX = 256,
    UI_TEXTURE_BINDINGS_MAX = 4096
};
/* Managed keys are opaque printable ASCII identifiers in one render context.
 * They never resolve a filesystem or network URL. */
static inline bool ui_texture_key_valid(const char *key)
{
    if (!key || !*key) {
        return false;
    }
    for (unsigned i = 0; i <= UI_TEXTURE_KEY_MAX; i++) {
        unsigned char c = (unsigned char)key[i];
        if (!c) {
            return true;
        }
        if (i == UI_TEXTURE_KEY_MAX || c <= 32 || c >= 127 || c == '\\') {
            return false;
        }
    }
    return false;
}
static inline bool ui_texture_source_valid(const char *source)
{
    return source && !strncmp(source, "kit:", 4) && ui_texture_key_valid(source + 4);
}
#endif
