#include "runtime/material_field.h"

#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *copy_text(arena *storage, const char *text, size_t budget)
{
    size_t size = strlen(text) + 1;
    char *copy = arena_alloc_bounded(storage, size, budget);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, text, size);
    return copy;
}

typedef struct
{
    char *name;
    field_program *program;
    unsigned revision;
} field_entry;
typedef struct
{
    material_field_arguments value;
    arena storage;
    size_t bytes;
} argument_entry;
struct material_fields
{
    field_entry *entries;
    argument_entry *arguments;
    size_t argument_bytes;
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
}
void material_fields_free(material_fields *fields)
{
    for (int i = 0; i < acount(fields->entries); i++) {
        free_entry(&fields->entries[i]);
    }
    afree(fields->entries);
    for (int i = 0; i < acount(fields->arguments); i++) {
        arena_free(&fields->arguments[i].storage);
    }
    afree(fields->arguments);
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
bool material_field_register_budget(
    material_fields *fields,
    const char *name,
    field_program *program,
    size_t budget
)
{
    assert(fields && name && name[0] && program);
    int index = find(fields, name);
    assert(index < 0 || fields->entries[index].program != program);
    int count = acount(fields->entries), capacity = acap(fields->entries);
    size_t growth = 0;
    if (index < 0 && count == capacity) {
        int next = capacity ? capacity * 2 : 16;
        growth = (size_t)(next - capacity) * sizeof *fields->entries;
        if (!capacity) {
            growth += sizeof(dyna_hdr);
        }
    }
    size_t retained = material_fields_storage_bytes(fields);
    if (index >= 0) {
        retained -= strlen(fields->entries[index].name) + 1;
        retained -= field_program_storage_bytes(fields->entries[index].program);
    }
    size_t added = growth + strlen(name) + 1 + field_program_storage_bytes(program);
    if (retained > budget || added > budget - retained) {
        return false;
    }
    field_entry entry = { .name = xstrdup(name), .program = program, .revision = 1 };
    if (index < 0) {
        apush(fields->entries, entry);
        return true;
    }
    entry.revision = fields->entries[index].revision + 1;
    free_entry(&fields->entries[index]);
    fields->entries[index] = entry;
    return true;
}
bool material_field_register(material_fields *fields, const char *name, field_program *program)
{
    return material_field_register_budget(fields, name, program, SIZE_MAX);
}
const field_program *material_field_program(const material_fields *fields, const char *name)
{
    int index = find(fields, name);
    return index < 0 ? NULL : fields->entries[index].program;
}
int material_field_count(const material_fields *fields)
{
    return acount(fields->entries);
}
size_t material_fields_storage_bytes(const material_fields *fields)
{
    size_t bytes = sizeof *fields;
    if (fields->entries) {
        bytes += sizeof(dyna_hdr) +
                 (size_t)acap(fields->entries) * sizeof *fields->entries;
    }
    if (fields->arguments) {
        bytes += sizeof(dyna_hdr) +
                 (size_t)acap(fields->arguments) * sizeof *fields->arguments;
    }
    for (int i = 0; i < acount(fields->entries); i++) {
        bytes += strlen(fields->entries[i].name) + 1;
        bytes += field_program_storage_bytes(fields->entries[i].program);
    }
    for (int i = 0; i < acount(fields->arguments); i++) {
        bytes += arena_storage_bytes(&fields->arguments[i].storage);
    }
    return bytes;
}
const char *material_field_name_at(const material_fields *fields, int index)
{
    assert(index >= 0 && index < acount(fields->entries));
    return fields->entries[index].name;
}
unsigned material_field_revision(const material_fields *fields, const char *name)
{
    int index = find(fields, name);
    return index < 0 ? 0 : fields->entries[index].revision;
}

static int argument_index(const material_fields *fields, const char *name, const char *key)
{
    for (int i = 0; i < acount(fields->arguments); i++) {
        const material_field_arguments *value = &fields->arguments[i].value;
        if (!strcmp(value->field, name) && !strcmp(value->key, key)) {
            return i;
        }
    }
    return -1;
}
bool material_field_register_arguments_budget(
    material_fields *fields,
    const char *name,
    const char *key,
    field_context context,
    size_t budget,
    char *error,
    size_t size
)
{
    assert(fields && name && key);
    const field_program *program = material_field_program(fields, name);
    if (!program || strlen(name) > FIELD_PARAMETER_NAME_MAX || !key[0] ||
        strlen(key) > 65536 || context.parameter_count < 0 ||
        context.parameter_count > 4096 ||
        (context.parameter_count && !context.parameters)) {
        snprintf(error, size, "Invalid prepared field argument record");
        return false;
    }
    int index = argument_index(fields, name, key);
    if (index < 0 && acount(fields->arguments) >= 1024) {
        snprintf(error, size, "Prepared field argument record limit exceeded");
        return false;
    }
    size_t bytes = sizeof(argument_entry) + strlen(name) + strlen(key) + 2 +
                   (size_t)context.parameter_count * sizeof(field_parameter);
    for (int i = 0; i < context.parameter_count; i++) {
        const field_parameter *parameter = &context.parameters[i];
        if (!parameter->name || strlen(parameter->name) > FIELD_PARAMETER_NAME_MAX ||
            (parameter->value.type_name &&
             strlen(parameter->value.type_name) > FIELD_PARAMETER_NAME_MAX)) {
            snprintf(error, size, "Invalid prepared field argument name");
            return false;
        }
        bytes += strlen(parameter->name) + 1;
        if (parameter->value.type_name) {
            bytes += strlen(parameter->value.type_name) + 1;
        }
    }
    size_t previous = index < 0 ? 0 : fields->arguments[index].bytes;
    if (bytes > 8u * 1024u * 1024u ||
        fields->argument_bytes - previous > 64u * 1024u * 1024u - bytes) {
        snprintf(error, size, "Prepared field argument byte limit exceeded");
        return false;
    }
    field_program_baker *validation = field_program_baker_new(program, context, 1, error, size);
    if (!validation) {
        return false;
    }
    field_program_baker_free(validation);
    size_t retained = material_fields_storage_bytes(fields);
    if (index >= 0) {
        retained -= arena_storage_bytes(&fields->arguments[index].storage);
    }
    int count = acount(fields->arguments), capacity = acap(fields->arguments);
    size_t growth = 0;
    if (index < 0 && count == capacity) {
        int next = capacity ? capacity * 2 : 16;
        growth = (size_t)(next - capacity) * sizeof *fields->arguments;
        if (!capacity) {
            growth += sizeof(dyna_hdr);
        }
    }
    if (retained > budget || growth > budget - retained) {
        snprintf(error, size, "Prepared field argument retained storage exceeds budget");
        return false;
    }
    size_t available = budget - retained - growth;
    argument_entry entry = { .bytes = bytes };
    entry.value.field = copy_text(&entry.storage, name, available);
    entry.value.key = copy_text(&entry.storage, key, available);
    field_parameter *parameters = arena_alloc_bounded(
        &entry.storage,
        (size_t)context.parameter_count * sizeof *parameters,
        available
    );
    if (!entry.value.field || !entry.value.key || !parameters) {
        goto budget_fail;
    }
    for (int i = 0; i < context.parameter_count; i++) {
        parameters[i] = context.parameters[i];
        parameters[i].name = copy_text(&entry.storage, parameters[i].name, available);
        if (!parameters[i].name) {
            goto budget_fail;
        }
        if (parameters[i].value.type_name) {
            parameters[i].value.type_name = copy_text(
                &entry.storage,
                parameters[i].value.type_name,
                available
            );
            if (!parameters[i].value.type_name) {
                goto budget_fail;
            }
        }
    }
    entry.value.context = (field_context){ context.seed, parameters, context.parameter_count };
    fields->argument_bytes = fields->argument_bytes - previous + bytes;
    if (index < 0) {
        apush(fields->arguments, entry);
        return true;
    }
    arena_free(&fields->arguments[index].storage);
    fields->arguments[index] = entry;
    return true;
budget_fail:
    arena_free(&entry.storage);
    snprintf(error, size, "Prepared field argument retained storage exceeds budget");
    return false;
}
bool material_field_register_arguments(
    material_fields *fields,
    const char *name,
    const char *key,
    field_context context,
    char *error,
    size_t size
)
{
    return material_field_register_arguments_budget(fields, name, key, context, SIZE_MAX, error, size);
}
bool material_field_context(
    const material_fields *fields,
    const char *name,
    const char *key,
    unsigned seed,
    field_context *out,
    char *error,
    size_t size
)
{
    assert(fields && out);
    if (!material_field_program(fields, name)) {
        snprintf(error, size, "Unknown procedural field: %s", name ? name : "");
        return false;
    }
    *out = (field_context){ .seed = seed };
    if (!key || !key[0]) {
        return true;
    }
    int index = argument_index(fields, name, key);
    if (index < 0) {
        snprintf(error, size, "Procedural field arguments have not been prepared: %s", name);
        return false;
    }
    *out = fields->arguments[index].value.context;
    out->seed = seed;
    return true;
}
int material_field_argument_count(const material_fields *fields)
{
    return acount(fields->arguments);
}
material_field_arguments material_field_argument_at(const material_fields *fields, int index)
{
    assert(index >= 0 && index < acount(fields->arguments));
    return fields->arguments[index].value;
}

bool material_field_bake(
    const material_fields *fields,
    const char *name,
    field_context context,
    field_grid grid,
    float *out,
    const task_system *tasks,
    char *error,
    size_t size
)
{
    return material_field_bake_rows(fields, name, context, grid, 0, grid.height, out, tasks, error, size);
}

bool material_field_bake_rows(
    const material_fields *fields,
    const char *name,
    field_context context,
    field_grid grid,
    int first,
    int count,
    float *out,
    const task_system *tasks,
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
    return field_program_bake_rows(entry->program, context, grid, first, count, out, tasks, error, size);
}

material_field_session *material_field_session_new(
    const material_fields *fields,
    const char *name,
    field_context context,
    const task_system *tasks,
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
    field_program_baker *baker = field_program_baker_new(
        entry->program,
        context,
        tasks ? tasks->workers : 1,
        error,
        size
    );
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
    const task_system *tasks,
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
