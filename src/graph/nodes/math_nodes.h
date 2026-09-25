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
    X(Step) X(Mix) X(Power) X(Atan2) X(RandomReal) X(RandomRangeReal)
#define MATH_OPERATION_NODES(X)                                                \
    X(Advance_Damped_Spring)                                                   \
    X(Quantize_Angle)                                                          \
    X(Smooth_Step)
#define MATH_DECLARE(name) extern node_desc NODE_##name;
MATH_EXPRESSION_NODES(MATH_DECLARE)
MATH_OPERATION_NODES(MATH_DECLARE)
#undef MATH_DECLARE

#endif
