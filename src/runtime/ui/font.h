#ifndef CHIBI_UI_FONT_H
#define CHIBI_UI_FONT_H

#define UI_FONT_PREPARED_VERSION 2
#include <stdbool.h>
#include <stddef.h>
/* Decoded records own all fields; builtin views borrow immutable bytes. Only
 * decoded/owned records may be passed to free. Font engines retain bytes until
 * shutdown; retiring a renderer generation does not retire registered faces. */
typedef struct ui_font
{
    const char *key, *family;
    const unsigned char *data;
    size_t length;
    int style, weight;
    bool fallback;
} ui_font;
void ui_font_free(ui_font *);
bool ui_font_valid(const ui_font *);
bool ui_font_prepared_write(const ui_font *, unsigned char **, size_t *, char *, size_t);
bool ui_font_prepared_read_budget(const void *, size_t, size_t *, ui_font *, char *, size_t);
#endif
