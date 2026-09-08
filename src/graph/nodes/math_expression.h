#ifndef CBLUEPRINT_MATH_EXPRESSION_H
#define CBLUEPRINT_MATH_EXPRESSION_H

#include "graph/model/behavior_asset.h"

typedef struct
{
    int offset;
    int length;
    char message[192];
} math_expression_error;

/* Numeric expressions: inferred generic inputs and one generic result. Generated
 * function graphs are inspectable through the call's reference_id. Failed
 * parsing/validation leaves the entire authored asset unchanged. */
node *math_expression_add(behavior_asset *asset, graph *caller, const char *source, math_expression_error *error);
int math_expression_set(behavior_asset *asset, node *call, const char *source, math_expression_error *error);
int math_expression_refresh(behavior_asset *asset, node *call, math_expression_error *error);
int math_expression_repair(behavior_asset *asset, node *call);
const char *math_expression_source(const behavior_asset *asset, const node *call);

#endif
