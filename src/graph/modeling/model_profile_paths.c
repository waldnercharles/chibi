#include "graph/modeling/model_profile_paths.h"

#include "graph/modeling/model_internal.h"
#include "support/xalloc.h"
#include "types/array.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void model_profile_samples_free(pin_array *samples)
{
    for (int i = 0; i < samples->count; i++) {
        free(samples->items[i].ptr);
    }
    pin_array_free(samples);
}
static void simplify_closed_profile(pin_array *samples)
{
    bool changed = true;
    while (changed && samples->count > 3) {
        changed = false;
        for (int i = 0; i < samples->count; i++) {
            const float
                *a = samples->items[(i + samples->count - 1) % samples->count].ptr;
            const float *b = samples->items[i].ptr;
            const float *c = samples->items[(i + 1) % samples->count].ptr;
            double cross = ((double)b[0] - a[0]) * ((double)c[1] - a[1]) -
                           ((double)b[1] - a[1]) * ((double)c[0] - a[0]);
            if (cross != 0 || (a[0] == c[0] && a[1] == c[1]) ||
                b[0] < fmin(a[0], c[0]) || b[0] > fmax(a[0], c[0]) ||
                b[1] < fmin(a[1], c[1]) || b[1] > fmax(a[1], c[1])) {
                continue;
            }
            free(samples->items[i].ptr);
            memmove(
                samples->items + i,
                samples->items + i + 1,
                (size_t)(samples->count - i - 1) * sizeof *samples->items
            );
            samples->items[--samples->count] = (pin_value){ 0 };
            changed = true;
            break;
        }
    }
}
bool model_profile_sample_cubic(
    const pin_array *controls,
    int width,
    bool closed,
    int steps,
    uint64_t node_id,
    pin_array **out,
    shader_error *error
)
{
    const char *type = width == 2 ? "ShaderVec2" : "ShaderVec3";
    if ((width != 2 && width != 3) || !controls ||
        controls->element_type != PIN_STRUCT || !controls->type_name ||
        strcmp(controls->type_name, type) || controls->count < (closed ? 3 : 4) ||
        (controls->count - (closed ? 0 : 1)) % 3 || steps < 1 || steps > 32) {
        return model_fail(error, node_id, "Cubic path requires typed 3N closed or 3N+1 open controls and Steps from 1 to 32");
    }
    int curves = (controls->count - (closed ? 0 : 1)) / 3;
    if (curves > 32 / steps || (closed && curves * steps < 3)) {
        return model_fail(error, node_id, "Cubic path must sample at most 32 edges and at least 3 closed edges");
    }
    for (int i = 0; i < controls->count; i++) {
        pin_value element = pin_array_get(controls, i);
        const pin_value *value = &element;
        if (value->type != PIN_STRUCT || value->container != PIN_CONTAINER_SINGLE ||
            !value->type_name || strcmp(value->type_name, type) || !value->ptr) {
            return model_fail(error, node_id, "Cubic controls require typed vector values");
        }
        const float *point = value->ptr;
        for (int axis = 0; axis < width; axis++) {
            if (!isfinite(point[axis])) {
                return model_fail(error, node_id, "Cubic controls must be finite");
            }
        }
    }
    int count = curves * steps + (closed ? 0 : 1);
    pin_array *samples = pin_array_new(PIN_STRUCT, type, count);
    samples->count = count;
    for (int i = 0; i < count; i++) {
        int curve = i / steps, sample = i % steps;
        if (curve == curves) {
            curve--;
            sample = steps;
        }
        double t = (double)sample / steps, u = 1 - t;
        const float *a = pin_array_data(controls, curve * 3);
        const float *b = pin_array_data(controls, curve * 3 + 1);
        const float *c = pin_array_data(controls, curve * 3 + 2);
        const float *d = pin_array_data(controls, (curve * 3 + 3) % controls->count);
        float *point = xmalloc((size_t)width * sizeof *point);
        samples->items[i] = (pin_value){ .type = PIN_STRUCT,
                                         .container = PIN_CONTAINER_SINGLE,
                                         .type_name = type,
                                         .ptr = point };
        for (int axis = 0; axis < width; axis++) {
            point[axis] = (float)(u * u * u * a[axis] + 3 * u * u * t * b[axis] +
                                  3 * u * t * t * c[axis] + t * t * t * d[axis]);
            if (!isfinite(point[axis])) {
                model_profile_samples_free(samples);
                return model_fail(error, node_id, "Cubic sampling exceeds finite vector arithmetic");
            }
        }
    }
    for (int i = 0; i < count - (closed ? 0 : 1); i++) {
        const float *a = samples->items[i].ptr,
                    *b = samples->items[(i + 1) % count].ptr;
        bool equal = true;
        for (int axis = 0; axis < width; axis++) {
            equal &= a[axis] == b[axis];
        }
        if (equal) {
            model_profile_samples_free(samples);
            return model_fail(error, node_id, "Cubic sampling produced a degenerate edge; change controls or Steps");
        }
    }
    if (closed && width == 2) {
        simplify_closed_profile(samples);
    }
    *out = samples;
    return true;
}
