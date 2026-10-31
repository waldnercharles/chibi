#ifndef CHIBI_IMAGE_TABLE_H
#define CHIBI_IMAGE_TABLE_H
#include "runtime/images/catalog.h"
/* Detached immutable pixel/sampler generations. Decode and variant insertion
 * charge retained capacity through remaining before ownership is accepted.
 * Exchange is infallible; the detached owner then retains the prior catalog. */
typedef struct render_image_table render_image_table;
render_image_table *render_image_table_new();
bool render_image_table_decode(render_image_table *, const char *, const void *, size_t, size_t *, shader_error *);
size_t render_image_table_header_bytes();
int render_image_table_variant(render_image_table *, const shader_image_source *, size_t *, shader_error *);
bool render_image_table_resolve(void *, const shader_image_source *, shader_image_view *);
void render_image_table_exchange(render_image_table *);
void render_image_table_free(render_image_table *);
#endif
