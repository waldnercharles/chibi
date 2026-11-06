#ifndef CHIBI_TEST_RENDER_HIDDEN_APP_H
#define CHIBI_TEST_RENDER_HIDDEN_APP_H

#include "platform/cute.h"

#include <stdbool.h>
#include <stdint.h>

/* The hidden app that render cases draw in, and reading its targets back. */

/* Opens a hidden `width` x `height` app without audio that loads the render
 * shaders from the source tree. */
void test_open_hidden_app(const char *title, int width, int height);
/* Reads all of `target`, which must be `size` bytes, into `data`. */
bool test_read_canvas(CF_Canvas target, void *data, int size);
/* Reads `count` pixels of an RGBA16F texture into `halves`. */
bool test_read_texture(CF_Texture texture, uint16_t *halves, int count);
/* The value an RGBA16F channel holds. */
float test_half(uint16_t value);

#endif
