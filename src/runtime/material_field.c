#include "runtime/material_field.h"

#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

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
struct material_field_session
{
    const material_fields *fields;
    char *name;
    unsigned revision;
    field_program_baker *baker;
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

static bool parse_parameters(
    const field_entry *entry,
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
                for (int j = 0; j < acount(entry->parameters); j++) {
                    if (!strcmp(entry->parameters[j].name, parameter_name)) {
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
bool material_field_bake(
    const material_fields *fields,
    const char *name,
    const char *text,
    unsigned seed,
    field_grid grid,
    float *out,
    const game_task_system *tasks,
    char *error,
    size_t size
)
{
    return material_field_bake_rows(fields, name, text, seed, grid, 0, grid.height, out, tasks, error, size);
}

bool material_field_bake_rows(
    const material_fields *fields,
    const char *name,
    const char *text,
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
    bool valid = parse_parameters(entry, text, &parameters, error, size);
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
    afree(parameters);
    return valid;
}

material_field_session *material_field_session_new(
    const material_fields *fields,
    const char *name,
    const char *text,
    unsigned seed,
    const game_task_system *tasks,
    char *error,
    size_t size
)
{
    int index = find(fields, name);
    if (index < 0) {
        snprintf(error, size, "Unknown procedural field: %s", name ? name : "");
        return NULL;
    }
    const field_entry *entry = &fields->entries[index];
    field_parameter *parameters = NULL;
    if (!parse_parameters(entry, text, &parameters, error, size)) {
        return NULL;
    }
    field_program_baker *baker = field_program_baker_new(
        entry->program,
        (field_context){ seed, parameters, acount(parameters) },
        tasks ? tasks->workers : 1,
        error,
        size
    );
    afree(parameters);
    if (!baker) {
        return NULL;
    }
    material_field_session *session = xcalloc(1, sizeof *session);
    *session = (material_field_session){ fields, xstrdup(name), entry->revision, baker };
    return session;
}

void material_field_session_free(material_field_session *session)
{
    if (!session) {
        return;
    }
    field_program_baker_free(session->baker);
    free(session->name);
    free(session);
}

bool material_field_session_matches(const material_field_session *session, const material_fields *fields)
{
    return session && fields == session->fields &&
           material_field_revision(fields, session->name) == session->revision;
}

bool material_field_session_bake_rows(
    material_field_session *session,
    const material_fields *fields,
    field_grid grid,
    int first,
    int count,
    float *out,
    const game_task_system *tasks,
    char *error,
    size_t size
)
{
    if (!material_field_session_matches(session, fields)) {
        snprintf(error, size, "Procedural field changed; recreate material stream");
        return false;
    }
    return field_program_baker_bake_rows(session->baker, grid, first, count, out, tasks, error, size);
}
