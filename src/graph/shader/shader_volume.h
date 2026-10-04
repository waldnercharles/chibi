#ifndef CHIBI_SHADER_VOLUME_H
#define CHIBI_SHADER_VOLUME_H
#include "graph/shader/shader_graph.h"
/* RG stores unsigned 16-bit distance, most significant byte first; B=0 A=255.
 * Lattice endpoints are -half and +half. X is atlas column, Y atlas row,
 * Z selects a row-major square tile. No filtering, mips, or color conversion. */
bool shader_volume_contract(const shader_node *n);
bool shader_volume_validate(const shader_node *, const shader_image_view *, shader_error *);
bool shader_volume_eval(const shader_node *, const shader_image_view *, const float p[3], float *);
char *shader_volume_glsl(const shader_node *, const char *prefix, int index);
float shader_graph_lipschitz(const shader_graph *);
void shader_graph_copy_images(shader_graph *to, const shader_graph *from);
#endif
