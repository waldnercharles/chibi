#ifndef CHIBI_MATH_NODES_H
#define CHIBI_MATH_NODES_H

#include "graph/model/node.h"

#define MATH_EXPRESSION_NODES(X)                                               \
    X(Sine)                                                                    \
    X(Cosine)                                                                  \
    X(Tangent)                                                                 \
    X(ArcSine)                                                                 \
    X(ArcCosine)                                                               \
    X(ArcTangent)                                                              \
    X(SquareRoot)                                                              \
    X(Exponential)                                                             \
    X(Logarithm)                                                               \
    X(Logarithm10)                                                             \
    X(Floor)                                                                   \
    X(Ceiling)                                                                 \
    X(Round)                                                                   \
    X(Fract)                                                                   \
    X(Sign)                                                                    \
    X(Power) X(Atan2) X(RandomReal) X(RandomRangeReal)
#define MATH_DECLARE(name) extern node_desc NODE_##name;
MATH_EXPRESSION_NODES(MATH_DECLARE)
#undef MATH_DECLARE

#endif
