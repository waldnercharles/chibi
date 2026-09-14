#ifndef CHIBI_BEHAVIOR_EXPRESSION_H
#define CHIBI_BEHAVIOR_EXPRESSION_H
#include "graph/compiler/graph_ir.h"
typedef struct behavior_asset behavior_asset;
/* Optional helper-free numeric region. Unsupported regions keep their regular
 * compiled entry. False means native code allocation/emission failed. */
bool behavior_expression_compile(
    const graph_ir *ir,
    const graph_ir_block *block,
    const behavior_asset *asset,
    void **code
);
#endif
