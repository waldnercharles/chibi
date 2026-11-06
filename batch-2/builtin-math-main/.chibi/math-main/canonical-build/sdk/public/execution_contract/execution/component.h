#ifndef CHIBI_EXECUTION_COMPONENT_H
#define CHIBI_EXECUTION_COMPONENT_H
#include "types/value.h"

struct ecs_execution_view;
enum
{
    EVAL_COMPONENT_READ_OK = 0,
    EVAL_COMPONENT_READ_MISSING_COMPONENT = 1,
    EVAL_COMPONENT_READ_MISSING_FIELD = 2,
    EVAL_COMPONENT_READ_INVALID_OWNER = 3,
};

typedef int (*eval_component_read_fn)(
    arena *storage,
    void *user,
    uint64_t owner,
    const char *component_key,
    /* NULL reads the complete reflected component. */
    const char *field_name,
    pin_value *out
);

typedef struct eval_component_reader
{
    const struct ecs_execution_view *execution; /* Optional direct ECS storage ABI. */
    eval_component_read_fn read;
    void *user;
} eval_component_reader;

#define EVAL_COMPONENT_READER_SERVICE "ecs.component_reader"

#endif
