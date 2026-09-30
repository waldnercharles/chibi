#include "runtime/material_field.h"

#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/yyjson/yyjson.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char *name;
    field_program *program;
    field_parameter *parameters;
    unsigned revision;
} field_entry;
struct material_fields
{
    field_entry *entries;
};

material_fields *material_fields_new()
{
    return xcalloc(1, sizeof(material_fields));
}
static void free_entry(field_entry *entry)
{
    free(entry->name);
    field_program_free(entry->program);
    for (int i = 0; i < acount(entry->parameters); i++) {
        free((char *)entry->parameters[i].name);
    }
    afree(entry->parameters);
}
void material_fields_free(material_fields *fields)
{
    for (int i = 0; i < acount(fields->entries); i++) {
        free_entry(&fields->entries[i]);
    }
    afree(fields->entries);
    free(fields);
}
static int find(const material_fields *fields, const char *name)
{
    for (int i = 0; name && i < acount(fields->entries); i++) {
        if (!strcmp(fields->entries[i].name, name)) {
            return i;
        }
    }
    return -1;
}
bool material_field_compile(material_fields *fields, const behavior_asset *asset, char *error, size_t size)
{
    field_program *program = field_program_compile(asset, error, size);
    if (!program) {
        return false;
    }
    int index = find(fields, asset->name);
    field_entry entry = { .name = xstrdup(asset->name), .program = program, .revision = 1 };
    for (int i = 0; i < acount(asset->variables); i++) {
        behavior_variable v = asset->variables[i];
        apush(entry.parameters, ((field_parameter){ xstrdup(v.name), v.default_value }));
    }
    if (index < 0) {
        apush(fields->entries, entry);
        return true;
    }
    entry.revision = fields->entries[index].revision + 1;
    free_entry(&fields->entries[index]);
    fields->entries[index] = entry;
    return true;
}
unsigned material_field_revision(const material_fields *fields, const char *name)
{
    int index = find(fields, name);
    return index < 0 ? 0 : fields->entries[index].revision;
}
bool material_field_bake(
    const material_fields *fields,
    const char *name,
    const char *json,
    unsigned seed,
    field_grid grid,
    float *out,
    const game_task_system *tasks,
    char *error,
    size_t size
)
{
    return material_field_bake_rows(fields, name, json, seed, grid, 0, grid.height, out, tasks, error, size);
}

bool material_field_bake_rows(
    const material_fields *fields,
    const char *name,
    const char *json,
    unsigned seed,
    field_grid grid,
    int first,
    int count,
    float *out,
    const game_task_system *tasks,
    char *error,
    size_t size
)
{
    int index = find(fields, name);
    if (index < 0) {
        snprintf(error, size, "Unknown procedural field: %s", name ? name : "");
        return false;
    }
    const field_entry *entry = &fields->entries[index];
    field_parameter *parameters = NULL;
    yyjson_doc *doc = NULL;
    bool valid = true;
    if (json && json[0]) {
        doc = yyjson_read(json, strlen(json), 0);
        valid = doc && yyjson_is_obj(yyjson_doc_get_root(doc));
        if (valid) {
            yyjson_obj_iter iter = yyjson_obj_iter_with(yyjson_doc_get_root(doc));
            yyjson_val *key;
            while ((key = yyjson_obj_iter_next(&iter))) {
                const char *parameter_name = yyjson_get_str(key);
                int parameter_index = -1;
                for (int j = 0; j < acount(entry->parameters); j++) {
                    if (!strcmp(entry->parameters[j].name, parameter_name)) {
                        parameter_index = j;
                        break;
                    }
                }
                yyjson_val *value = yyjson_obj_iter_get_val(key);
                double number = yyjson_get_num(value);
                if (parameter_index < 0 || !yyjson_is_num(value) || !isfinite(number)) {
                    valid = false;
                    break;
                }
                field_parameter p = entry->parameters[parameter_index];
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
                apush(parameters, p);
            }
        }
    }
    if (!valid) {
        snprintf(error, size, "Invalid field parameter object");
    }
    if (valid) {
        valid = field_program_bake_rows(
            entry->program,
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
    yyjson_doc_free(doc);
    afree(parameters);
    return valid;
}
