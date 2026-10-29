#include "assets/field_parameters.h"

#include "support/array.h"
#include "support/asset_text.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static bool parse_parameters(
    const field_program *program,
    const char *text,
    field_parameter **parameters,
    char *error,
    size_t size
)
{
    *parameters = NULL;
    av_doc *doc = NULL;
    bool valid = true;
    if (text && text[0]) {
        doc = av_value_read(text, NULL, 0);
        valid = doc && av_is_obj(av_doc_root(doc));
        if (valid) {
            av_obj_iter iter = av_obj_iter_with(av_doc_root(doc));
            av_value *key;
            while ((key = av_obj_iter_next(&iter))) {
                const char *parameter_name = av_get_str(key);
                int parameter_index = -1;
                for (int j = 0; j < field_program_parameter_count(program); j++) {
                    if (!strcmp(field_program_parameter_at(program, j).name, parameter_name)) {
                        parameter_index = j;
                        break;
                    }
                }
                av_value *value = av_obj_iter_get_val(key);
                double number = av_get_num(value);
                if (parameter_index < 0 || !av_is_num(value) || !isfinite(number)) {
                    valid = false;
                    break;
                }
                field_parameter p = field_program_parameter_at(program, parameter_index);
                if (p.value.type == PIN_F32) {
                    p.value.f = (float)number;
                }
                if (p.value.type == PIN_I32) {
                    if (number < INT32_MIN || number > INT32_MAX ||
                        trunc(number) != number) {
                        valid = false;
                        break;
                    }
                    p.value.i = (int)number;
                }
                if (p.value.type == PIN_U32) {
                    if (number < 0 || number > UINT32_MAX || trunc(number) != number) {
                        valid = false;
                        break;
                    }
                    p.value.u = (unsigned)number;
                }
                apush(*parameters, p);
            }
        }
    }
    av_doc_free(doc);
    if (!valid) {
        afree(*parameters);
        snprintf(error, size, "Invalid field parameter object");
    }
    return valid;
}
static bool prepare_parameters(
    const material_fields *fields,
    const char *name,
    const char *text,
    field_parameter **parameters,
    char *error,
    size_t size
)
{
    const field_program *program = material_field_program(fields, name);
    if (!program) {
        snprintf(error, size, "Unknown procedural field: %s", name ? name : "");
        return false;
    }
    return parse_parameters(program, text, parameters, error, size);
}
bool material_field_prepare_arguments(
    material_fields *fields,
    const char *name,
    const char *text,
    char *error,
    size_t size
)
{
    field_parameter *parameters = NULL;
    if (!prepare_parameters(fields, name, text, &parameters, error, size)) {
        return false;
    }
    bool valid = !text || !text[0] ||
                 material_field_register_arguments(
                     fields,
                     name,
                     text,
                     (field_context){ .parameters = parameters,
                                      .parameter_count = acount(parameters) },
                     error,
                     size
                 );
    afree(parameters);
    return valid;
}
bool material_field_bake_text(
    const material_fields *fields,
    const char *name,
    const char *text,
    unsigned seed,
    field_grid grid,
    float *out,
    const task_system *tasks,
    char *error,
    size_t size
)
{
    return material_field_bake_rows_text(fields, name, text, seed, grid, 0, grid.height, out, tasks, error, size);
}
bool material_field_bake_rows_text(
    const material_fields *fields,
    const char *name,
    const char *text,
    unsigned seed,
    field_grid grid,
    int first,
    int count,
    float *out,
    const task_system *tasks,
    char *error,
    size_t size
)
{
    field_parameter *parameters = NULL;
    bool valid = prepare_parameters(fields, name, text, &parameters, error, size);
    if (valid) {
        valid = material_field_bake_rows(
            fields,
            name,
            (field_context){ seed, parameters, acount(parameters) },
            grid,
            first,
            count,
            out,
            tasks,
            error,
            size
        );
    }
    afree(parameters);
    return valid;
}
