#include "graph/modeling/model_internal.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int width(const pin_spec *pin)
{
    if (pin->kind != PIN_DATA || pin->container != PIN_CONTAINER_SINGLE) {
        return 0;
    }
    if (pin->type == PIN_F32 || pin->type == PIN_I32) {
        return 1;
    }
    if (pin->type != PIN_STRUCT || !pin->type_name) {
        return 0;
    }
    if (!strcmp(pin->type_name, "ShaderVec2")) {
        return 2;
    }
    if (!strcmp(pin->type_name, "ShaderVec3")) {
        return 3;
    }
    if (!strcmp(pin->type_name, "ShaderVec4")) {
        return 4;
    }
    return 0;
}
static bool declaration_error(shader_error *err, const node *n, const char *text)
{
    if (err) {
        err->node = n->id;
        snprintf(err->message, sizeof err->message, "%s", text);
    }
    return false;
}
bool model_parameter_get(const node *n, model_parameter_decl *out, shader_error *err)
{
    if (n->disabled || n->desc != &NODE_ModelParameter || n->input_count != 3 ||
        n->output_count != 1 || !n->reference_name || !*n->reference_name) {
        return declaration_error(err, n, "Model Parameter requires a name and Default/Minimum/Maximum declaration");
    }
    if (strlen(n->reference_name) > 255) {
        return declaration_error(err, n, "Model Parameter name exceeds 255 characters");
    }
    if (!strncmp(n->reference_name, "shape.", 6) ||
        !strncmp(n->reference_name, "material.", 9)) {
        return declaration_error(err, n, "Model Parameter name uses a reserved family prefix");
    }
    int w = width(&n->output_pins[0]);
    if (!w || n->output_pins[0].type == PIN_I32) {
        return declaration_error(err, n, "Model Parameter requires number or ShaderVec2/3/4");
    }
    *out = (model_parameter_decl){ .id = n->id,
                                   .name = n->reference_name,
                                   .type = n->output_pins[0].type,
                                   .type_name = n->output_pins[0].type_name,
                                   .width = w };
    float *values[] = { out->defaults, out->minimum, out->maximum };
    for (int i = 0; i < 3; i++) {
        const pin_spec *p = &n->input_pins[i];
        if (p->binding || width(p) != w || p->type != out->type ||
            (out->type_name && (!p->type_name || strcmp(p->type_name, out->type_name)))) {
            return declaration_error(err, n, "Model Parameter declaration values require the same numeric type");
        }
        if (w == 1) {
            values[i][0] = p->default_value.f;
        } else if (p->default_value.ptr) {
            memcpy(values[i], p->default_value.ptr, w * sizeof(float));
        }
        for (int j = 0; j < w; j++) {
            if (!isfinite(values[i][j])) {
                return declaration_error(err, n, "Model Parameter range and default must be finite");
            }
        }
    }
    for (int i = 0; i < w; i++) {
        if (out->minimum[i] > out->maximum[i] || out->defaults[i] < out->minimum[i] ||
            out->defaults[i] > out->maximum[i]) {
            return declaration_error(err, n, "Model Parameter default must lie in its declared ordered range");
        }
    }
    return true;
}
