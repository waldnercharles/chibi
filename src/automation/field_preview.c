#include "automation/field_preview.h"

#include "assets/field_parameters.h"
#include "graph/compiler/material_field.h"
#include "platform/cute.h"
#include "render/field_image.h"
#include "runtime/material_field.h"
#include "support/asset_document.h"
#include "support/asset_json.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

bool automation_field_preview(
    const behavior_asset *asset,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char *error,
    size_t size
)
{
    yyjson_val *w = yyjson_obj_get(request, "width"),
               *h = yyjson_obj_get(request, "height");
    yyjson_val *cell = yyjson_obj_get(request, "cell_size"),
               *seed = yyjson_obj_get(request, "seed");
    int width = w ? yyjson_get_int(w) : 256, height = h ? yyjson_get_int(h) : 256;
    double spacing = cell ? yyjson_get_num(cell) : 2;
    if ((w && !yyjson_is_int(w)) || (h && !yyjson_is_int(h)) || width < 1 ||
        height < 1 || width > 512 || height > 512 || (cell && !yyjson_is_num(cell)) ||
        !isfinite(spacing) || spacing <= 0 || spacing > 1024 ||
        (seed && (!yyjson_is_uint(seed) || yyjson_get_uint(seed) > UINT32_MAX))) {
        snprintf(error, size, "Field preview requires 1..512 pixels, positive cell size up to 1024, and a uint seed");
        return false;
    }
    yyjson_val *overrides = yyjson_obj_get(request, "parameters");
    av_doc *parameter_doc = av_doc_new();
    char *parameters = overrides
                         ? asset_document_write(av_from_json(parameter_doc, overrides))
                         : NULL;
    av_doc_free(parameter_doc);
    material_fields *fields = material_fields_new();
    float *values = xmalloc((size_t)width * height * sizeof(float));
    bool valid = material_field_compile(fields, asset, error, size) &&
                 material_field_bake_text(
                     fields,
                     asset->name,
                     parameters,
                     (unsigned)yyjson_get_uint(seed),
                     (field_grid){ .width = width,
                                   .height = height,
                                   .cell_size = (float)spacing },
                     values,
                     NULL,
                     error,
                     size
                 );
    material_fields_free(fields);
    free(parameters);
    if (!valid) {
        free(values);
        return false;
    }
    CF_Pixel *pixels = xmalloc((size_t)width * height * sizeof(CF_Pixel));
    uint64_t hash = UINT64_C(14695981039346656037);
    float minimum = values[0], maximum = values[0];
    for (int i = 0; i < width * height; i++) {
        minimum = fminf(minimum, values[i]);
        maximum = fmaxf(maximum, values[i]);
    }
    render_field_image(values, width, height, (float)spacing, pixels);
    const unsigned char *bytes = (const unsigned char *)values;
    for (size_t i = 0; i < (size_t)width * height * sizeof(float); i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    CF_Image image = { width, height, pixels };
    void *png = NULL;
    int png_size = 0;
    valid = !cf_is_error(cf_image_save_png_to_memory(&image, &png, &png_size));
    if (valid) {
        size_t length = CF_BASE64_ENCODED_SIZE((size_t)png_size);
        char *encoded = xcalloc(length + 1, 1);
        valid = !cf_is_error(cf_base64_encode(encoded, length, png, (size_t)png_size));
        if (valid) {
            yyjson_mut_val *preview = yyjson_mut_obj_add_obj(doc, result, "preview");
            yyjson_mut_obj_add_str(doc, preview, "mime_type", "image/png");
            yyjson_mut_obj_add_strcpy(doc, preview, "data", encoded);
            yyjson_mut_obj_add_int(doc, preview, "width", width);
            yyjson_mut_obj_add_int(doc, preview, "height", height);
            yyjson_mut_obj_add_real(doc, result, "minimum", minimum);
            yyjson_mut_obj_add_real(doc, result, "maximum", maximum);
            char digest[17];
            snprintf(digest, sizeof digest, "%016llx", (unsigned long long)hash);
            yyjson_mut_obj_add_strcpy(doc, result, "field_hash", digest);
        }
        free(encoded);
    }
    if (!valid) {
        snprintf(error, size, "Could not encode field preview");
    }
    cf_free(png);
    free(pixels);
    free(values);
    return valid;
}
