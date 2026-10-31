#ifndef CHIBI_AUTOMATION_RENDER_PREVIEW_H
#define CHIBI_AUTOMATION_RENDER_PREVIEW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

bool chibi_preview_readback(int width, int height, char **encoded, char *error, size_t error_size);
/* Appends the current frame as raw RGBA8 rows. */
bool chibi_preview_readback_raw(int width, int height, FILE *out, char *error, size_t error_size);
char *chibi_scene_preview_handle(const char *request, const char *default_root, int *exit_code);

/* Tooling-only GPU feedback for authored material and shape assets. The
 * renderer remains unaware of automation JSON; this seam owns request parsing,
 * safe asset resolution, and the readback response. */
char *chibi_render_preview_handle(const char *request, const char *default_root, int *exit_code);

#endif
