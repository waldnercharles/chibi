#ifndef BEHAVIOR_ASSET_INTERFACE_H
#define BEHAVIOR_ASSET_INTERFACE_H

#include "graph/model/annotate.h"

NODE_TYPE(Vec3);
NODE_TYPE(Transform);

NODE_FN float add_floats(float a, float b);
NODE_READ const char *split_values(float a, NODE_OUT double *sum, float b, NODE_OUT double *difference);

#endif
