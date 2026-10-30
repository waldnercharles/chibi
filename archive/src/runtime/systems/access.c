#include "runtime/components.h"

Transform move_transform(Transform transform, float x, float y)
{
    transform.x += x;
    transform.y += y;
    return transform;
}

CF_V2 transform_to_cf(Transform transform)
{
    return transform;
}

Transform transform_from_cf(CF_V2 value)
{
    return value;
}
