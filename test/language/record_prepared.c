#include "execution/prepared_internal.h"
#include "support/array.h"
#include "types/array.h"
#include "types/catalog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exactly two prepared-owner preflights. No compilation, finalization or backend
 * execution. A same-name native layout must never reinterpret authored bytes. */
bool record_prepared_guard(const pin_value *input)
{
    const type_field field = { .name = "nested",
                               .type = PIN_STRUCT,
                               .type_name = "MissingNativeType",
                               .size = sizeof(void *) };
    const type_desc type = { .name = input->type_name,
                             .size = sizeof(void *),
                             .fields = &field,
                             .field_count = 1 };
    const type_desc *types[] = { &type };
    const type_catalog_pack pack = { .types = types, .type_count = 1 };
    type_catalog_set_project(&pack);
    execution_module_plan plan = { .type_count = 1 };
    apush(
        plan.types,
        ((execution_type_requirement){ .id = 91,
                                       .storage = PIN_STRUCT,
                                       .name = input->type_name })
    );
    pin_value value = *input;
    pin_array array = { .element_type = PIN_STRUCT,
                        .type_name = input->type_name,
                        .record = input->record,
                        .count = 1,
                        .capacity = 1,
                        .items = &value };
    pin_value values[] = { value,
                           { .type = PIN_STRUCT,
                             .type_name = input->type_name,
                             .container = PIN_CONTAINER_ARRAY,
                             .ptr = &array } };
    bool valid = true;
    for (int i = 0; i < 2; i++) {
        /* Writing the type table indexes it for value lookups; the guard
         * is that the value then writes nothing. */
        prepared_context context = { .plan = &plan };
        prepared_types(&context);
        valid = valid && !context.wire.error;
        afree(context.wire.data);
        context.wire = (execution_wire){ 0 };
        prepared_value(&context, &values[i]);
        valid = valid && context.wire.error &&
                !strcmp(
                    context.wire.error,
                    i == 0
                        ? "prepared authored/native type identity differs"
                        : "prepared authored/native collection identity differs"
                ) &&
                !context.wire.size && !context.wire.data;
        afree(context.wire.data);
        free(context.records);
    }
    afree(plan.types);
    type_catalog_set_project(NULL);
    return valid;
}

enum
{
    TABLE_TYPES = 20000
};
/* The requirement ids count down from TABLE_TYPES and the names count down
 * with them, so every entry sorts before all earlier ones. */
static void table_name(char *name, size_t size, uint64_t id)
{
    snprintf(name, size, "Table.T%07llu", (unsigned long long)id);
}
static bool table_type(void *, uint64_t id, execution_prepared_type *type)
{
    static char name[32];
    table_name(name, sizeof name, id);
    *type = (execution_prepared_type){ .name = name,
                                       .storage = PIN_I32,
                                       .size = 4,
                                       .layout = id * 7 };
    return true;
}
/* Writes and reads back a table of many requirements within the default
 * layout-work bound, which insertion into the two sorted orders overran
 * (4 bytes per move, about n squared moves), then checks that a repeated name
 * or id is still rejected by both directions. */
bool record_prepared_type_table()
{
    char (*names)[32] = malloc(TABLE_TYPES * sizeof *names);
    execution_module_plan plan = { .type_count = TABLE_TYPES };
    for (int i = 0; i < TABLE_TYPES; i++) {
        uint64_t id = TABLE_TYPES - i;
        table_name(names[i], sizeof names[i], id);
        apush(
            plan.types,
            ((
                execution_type_requirement
            ){ .id = id, .layout = id * 7, .storage = PIN_I32, .size = 4, .name = names[i] })
        );
    }
    prepared_context writer = { .plan = &plan };
    prepared_types(&writer);
    bool valid = !writer.wire.error && writer.wire.size > 0;
    arena storage;
    arena_init(&storage, 1 << 20);
    execution_module_plan loaded = { 0 };
    execution_prepared_linker linker = { .type = table_type };
    prepared_context reader = { .plan = &loaded,
                                .linker = &linker,
                                .wire = { .data = writer.wire.data,
                                          .size = writer.wire.size,
                                          .reading = true,
                                          .storage = &storage } };
    prepared_types(&reader);
    valid = valid && !reader.wire.error && loaded.type_count == TABLE_TYPES;
    for (int i = 0; valid && i < TABLE_TYPES; i++) {
        valid = loaded.types[i].id == plan.types[i].id &&
                !strcmp(loaded.types[i].name, names[i]);
    }
    free(reader.records);
    free(writer.records);
    afree(writer.wire.data);
    arena_free(&storage);
    for (int repeat = 0; repeat < 2; repeat++) {
        prepared_context again = { .plan = &plan };
        if (repeat == 0) {
            plan.types[TABLE_TYPES - 1].name = names[0];
        } else {
            plan.types[TABLE_TYPES - 1].name = names[TABLE_TYPES - 1];
            plan.types[TABLE_TYPES - 1].id = plan.types[0].id;
        }
        prepared_types(&again);
        valid = valid && again.wire.error &&
                !strcmp(again.wire.error, "duplicate prepared type requirement");
        afree(again.wire.data);
        free(again.records);
    }
    afree(plan.types);
    free(names);
    return valid;
}
