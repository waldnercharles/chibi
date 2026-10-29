#ifndef CHIBI_SHADER_PREPARED_INTERNAL_H
#define CHIBI_SHADER_PREPARED_INTERNAL_H
#include "runtime/shaders/program.h"
#include "types/value_wire.h"
/* Reader graph is fresh and owns every appended row; caller retires it on error.
 * Nested model records share this same aggregate allocation/record budget. */
void shader_graph_wire(execution_wire *, shader_graph *);
#endif
