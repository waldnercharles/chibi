#include "execution/prepared_internal.h"
#include "support/array.h"
#include "types/array.h"
#include "types/catalog.h"

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
