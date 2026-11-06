#ifndef CHIBI_SCENE3D_PARTS_TABLE_H
#define CHIBI_SCENE3D_PARTS_TABLE_H
#include "render/scene3d_parts.h"
/* Detached copied part generations. Exchange is infallible and transfers
 * the previously resident records to the supplied owner. */
typedef struct render_parts_table render_parts_table;
render_parts_table *render_parts_prepare(const void *, size_t, size_t *remaining, char *, size_t);
render_parts_table *render_parts_new();
size_t render_parts_header_bytes();
int render_parts_count(const render_parts_table *);
const char *render_parts_key(const render_parts_table *, int);
const scene3d_parts *render_parts_at(const render_parts_table *, int);
void render_parts_exchange(render_parts_table *);
void render_parts_free(render_parts_table *);
#endif
