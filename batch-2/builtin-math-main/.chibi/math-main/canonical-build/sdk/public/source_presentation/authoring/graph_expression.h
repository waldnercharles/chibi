#ifndef CHIBI_AUTHORING_GRAPH_EXPRESSION_H
#define CHIBI_AUTHORING_GRAPH_EXPRESSION_H
#include "graph/model/behavior_asset.h"
#include "graph/model/node.h"

#include <stddef.h>

/* Display and resolution return malloc-owned text. Input labels borrow the
 * asset. Presentation never changes executable pin names or graph history. */
char *graph_expression_display(const behavior_asset *, const node *);
char *graph_expression_resolve(const behavior_asset *, const node *, const char *, char *error, size_t size);
const char *graph_expression_input_label(const behavior_asset *, const node *, int input);
void graph_expression_input_name(const behavior_asset *, const node *, int input, char *out, size_t size);
void graph_expression_capture_name(const char *, char *out, size_t size);
#endif
