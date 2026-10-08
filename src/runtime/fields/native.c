#include "runtime/fields/native.h"

#include <math.h>

const node_access FIELD_NATIVE_ACCESS = { .flags = NODE_EFFECT_PURE };

static const char *field_add(void *, const pin_value *in, pin_value *out)
{
    if (!isfinite(in[0].f) || !isfinite(in[1].f)) {
        return "Field Add requires finite values";
    }
    out->f = in[0].f + in[1].f;
    return isfinite(out->f) ? NULL : "Field Add result is not finite";
}
const node_host_call FIELD_ADD_CALL = { .invoke = field_add };

static const char *field_subtract(void *, const pin_value *in, pin_value *out)
{
    if (!isfinite(in[0].f) || !isfinite(in[1].f)) {
        return "Field Subtract requires finite values";
    }
    out->f = in[0].f - in[1].f;
    return isfinite(out->f) ? NULL : "Field Subtract result is not finite";
}
const node_host_call FIELD_SUBTRACT_CALL = { .invoke = field_subtract };

static const char *field_min(void *, const pin_value *in, pin_value *out)
{
    if (!isfinite(in[0].f) || !isfinite(in[1].f)) {
        return "Field Min requires finite values";
    }
    out->f = fminf(in[0].f, in[1].f);
    return NULL;
}
const node_host_call FIELD_MIN_CALL = { .invoke = field_min };

static const char *circle(void *, const pin_value *in, pin_value *out)
{
    if (!isfinite(in[0].f) || !isfinite(in[1].f) || !isfinite(in[2].f) || in[2].f < 0) {
        return "Field Circle requires finite coordinates and a nonnegative "
               "Radius";
    }
    out->f = sqrtf(in[0].f * in[0].f + in[1].f * in[1].f) - in[2].f;
    return isfinite(out->f) ? NULL : "Field Circle result is not finite";
}
const node_host_call FIELD_CIRCLE_CALL = { .invoke = circle };

static const char *box(void *, const pin_value *in, pin_value *out)
{
    for (int i = 0; i < 4; i++) {
        if (!isfinite(in[i].f)) {
            return "Field Box requires finite values";
        }
    }
    if (in[2].f < 0 || in[3].f < 0) {
        return "Field Box requires nonnegative Half Width and Half Height";
    }
    float x = fabsf(in[0].f) - in[2].f, y = fabsf(in[1].f) - in[3].f;
    float dx = fmaxf(x, 0), dy = fmaxf(y, 0);
    out->f = sqrtf(dx * dx + dy * dy) + fminf(fmaxf(x, y), 0);
    return isfinite(out->f) ? NULL : "Field Box result is not finite";
}
const node_host_call FIELD_BOX_CALL = { .invoke = box };

static const char *capsule(void *, const pin_value *in, pin_value *out)
{
    for (int i = 0; i < 7; i++) {
        if (!isfinite(in[i].f)) {
            return "Field Capsule requires finite values";
        }
    }
    if (in[6].f < 0) {
        return "Field Capsule requires a nonnegative Radius";
    }
    float dx = in[4].f - in[2].f, dy = in[5].f - in[3].f;
    float length = dx * dx + dy * dy;
    float t = length > 0
                ? fminf(1, fmaxf(0, ((in[0].f - in[2].f) * dx + (in[1].f - in[3].f) * dy) / length))
                : 0;
    float x = in[0].f - in[2].f - t * dx;
    float y = in[1].f - in[3].f - t * dy;
    out->f = sqrtf(x * x + y * y) - in[6].f;
    return isfinite(out->f) ? NULL : "Field Capsule result is not finite";
}
const node_host_call FIELD_CAPSULE_CALL = { .invoke = capsule };

static const char *smooth_union(void *, const pin_value *in, pin_value *out)
{
    float a = in[0].f, b = in[1].f, radius = in[2].f;
    if (!isfinite(a) || !isfinite(b) || !isfinite(radius) || radius < 0) {
        return "Field Smooth Union requires finite distances and a nonnegative "
               "Radius";
    }
    float h = radius > 0 ? fmaxf(radius - fabsf(a - b), 0) / radius : 0;
    out->f = fminf(a, b) - h * h * radius * .25f;
    return isfinite(out->f) ? NULL : "Field Smooth Union result is not finite";
}
const node_host_call FIELD_SMOOTH_UNION_CALL = { .invoke = smooth_union };
