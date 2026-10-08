#ifndef CHIBI_TEST_PREPARED_FIXTURE_H
#define CHIBI_TEST_PREPARED_FIXTURE_H
#include "ecs/ecs.h"
#include "types/type.h"

#include <stdint.h>

typedef struct
{
    int32_t value;
} prepared_counter;
static const type_field prepared_counter_field = {
    .name = "value",
    .type = PIN_I32,
    .type_name = "int",
    .offset = 0,
    .size = sizeof(int32_t),
};
static const type_desc prepared_counter_type = {
    .name = "PersistCounter",
    .size = sizeof(prepared_counter),
    .fields = &prepared_counter_field,
    .field_count = 1,
};
static const ecs_component_desc prepared_counter_component = {
    .id = 901,
    .key = "fixture.persist_counter",
    .name = "PersistCounter",
    .element_size = sizeof(prepared_counter),
    .type = &prepared_counter_type,
};
enum
{
    PREPARED_TEST_MAX_BYTES = 512 * 1024,
    PREPARED_TEST_MAX_PATH = 4096
};
#endif
