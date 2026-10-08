#ifndef CHIBI_UI_FONT_H
#define CHIBI_UI_FONT_H

#define GAME_UI_FONT_PREPARED_VERSION 1
#include <stdbool.h>
#include <stddef.h>
/* Decoded records own all fields; builtin views borrow immutable bytes. Only
 * decoded/owned records may be passed to free. Font engines retain bytes until
 * shutdown; retiring a renderer generation does not retire registered faces. */
typedef struct game_ui_font
{
    const char *key, *family;
    const unsigned char *data;
    size_t length;
    int style, weight;
    bool fallback;
} game_ui_font;
void game_ui_font_free(game_ui_font *);
bool game_ui_font_valid(const game_ui_font *);
bool game_ui_font_prepared_write(const game_ui_font *, unsigned char **, size_t *, char *, size_t);
bool game_ui_font_prepared_read_budget(const void *, size_t, size_t *, game_ui_font *, char *, size_t);
#endif
