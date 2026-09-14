#ifndef CHIBI_AUTOMATION_RENDER_PREVIEW_H
#define CHIBI_AUTOMATION_RENDER_PREVIEW_H

/* Tooling-only GPU feedback for authored material and shape assets. The
 * renderer remains unaware of MCP or JSON; this seam owns request parsing,
 * safe asset resolution, and the readback response. */
char *chibi_render_preview_handle(const char *request, const char *default_root, int *exit_code);

#endif
