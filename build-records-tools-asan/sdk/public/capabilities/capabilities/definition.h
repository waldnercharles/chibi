#ifndef CHIBI_CAPABILITY_DEFINITION_H
#define CHIBI_CAPABILITY_DEFINITION_H

#include "capabilities/catalog.h"

typedef enum
{
    NATIVE_PARAMETER_DATA,
    NATIVE_PARAMETER_EXEC,
} native_parameter_kind;
typedef struct
{
    const char *name;
    native_parameter_kind kind;
    pin_type storage;
    pin_container container;
    const char *type_name;
    const char *unit; /* Optional erased numeric parameter/result dimension. */
    unsigned type_flags;
    int generic_group;
    script_reference_kind reference_kind;
    const char *member_name; /* Fully qualified reflected field name. */
    bool c_string; /* Input bytes must be representable by a C string. */
    bool has_default, default_owner;
    pin_value default_value;
} native_parameter_definition;
/* Maps one X-macro port row onto a parameter definition. Every native pack
 * describes its ports with these ten columns. */
#define NATIVE_PORT(NAME, KIND, STORAGE, TYPE_NAME, CONTAINER, TYPE_FLAGS, GROUP, REFERENCE, OWNER, VALUE) \
    { .name = NAME,                                                                                        \
      .kind = KIND,                                                                                        \
      .storage = STORAGE,                                                                                  \
      .type_name = TYPE_NAME,                                                                              \
      .container = CONTAINER,                                                                              \
      .type_flags = TYPE_FLAGS,                                                                            \
      .generic_group = GROUP,                                                                              \
      .reference_kind = REFERENCE,                                                                         \
      .has_default = true,                                                                                 \
      .default_owner = OWNER,                                                                              \
      .default_value = VALUE },
/* Explicit opt-in for native inputs that require C strings. */
#define NATIVE_CSTRING_PORT(NAME, KIND, STORAGE, TYPE_NAME, CONTAINER, TYPE_FLAGS, GROUP, REFERENCE, OWNER, VALUE) \
    { .name = NAME,                                                                                                \
      .c_string = ((STORAGE) == PIN_STR || (STORAGE) == PIN_NAME) &&                                               \
                  (CONTAINER) == PIN_CONTAINER_SINGLE,                                                             \
      .kind = KIND,                                                                                                \
      .storage = STORAGE,                                                                                          \
      .type_name = TYPE_NAME,                                                                                      \
      .container = CONTAINER,                                                                                      \
      .type_flags = TYPE_FLAGS,                                                                                    \
      .generic_group = GROUP,                                                                                      \
      .reference_kind = REFERENCE,                                                                                 \
      .has_default = true,                                                                                         \
      .default_owner = OWNER,                                                                                      \
      .default_value = VALUE },
/* A port row that also carries graph pin flags, which definitions ignore. */
#define NATIVE_PORT_FLAGS(NAME, KIND, STORAGE, TYPE_NAME, CONTAINER, TYPE_FLAGS, GROUP, REFERENCE, OWNER, VALUE, GRAPH_FLAGS) \
    NATIVE_PORT(NAME, KIND, STORAGE, TYPE_NAME, CONTAINER, TYPE_FLAGS, GROUP, REFERENCE, OWNER, VALUE)
#define NATIVE_COUNT(ARRAY) ((int)(sizeof(ARRAY) / sizeof *(ARRAY)))
typedef struct
{
    const char *canonical_name, *source_name, *operator_name, *method_name;
    /* Optional authoring display text; excluded from executable ABI. */
    const char *category, *keywords, *description;
    script_capability_kind kind;
    const native_parameter_definition *inputs, *outputs;
    int input_count, output_count;
    /* Original output port index; must identify EXEC, or -1 for no completion. */
    int completion_output;
    unsigned effects;
    script_operation operation;
    const char *receiver_type;
    script_state_access state_access;
    native_state_kind state_kind;
    script_component_access component_access;
    script_struct_access struct_access;
    const char *component_type, *component_field, *aggregate_type;
    int component_entity, component_value; /* Filtered DATA ordinals. */
    script_catalog_flow flow;              /* Filtered DATA/EXEC ordinals. */
    bool pure, variadic, variadic_optional, numeric_aggregate, event_sender,
        language_values, owned_result;
} native_capability_definition;
typedef enum
{
    NATIVE_GLOBAL_IDENTITY_GLOBAL,
    NATIVE_GLOBAL_IDENTITY_CAPABILITY,
} native_global_identity;
typedef struct
{
    const char *canonical_name, *source_name;
    const native_parameter_definition *type; /* One resolved DATA port. */
    native_global_identity identity;
    bool writable;
    const char *unit; /* NULL without one */
} native_global_definition;
typedef struct native_definition_catalog native_definition_catalog;

uint64_t native_capability_definition_id(const native_capability_definition *);
uint64_t native_global_definition_id(const native_global_definition *);

/* Definitions/base strings/defaults are borrowed until this intermediate is
 * freed. Only the converted arrays are owned. Base supplies types and
 * builtin IDs; base capability_count/global_count must be zero. An immutable runtime table
 * snapshots this catalog before releasing the intermediate. */
native_definition_catalog *native_definition_catalog_new(
    const script_catalog *base,
    const native_capability_definition *,
    int count,
    const native_global_definition *,
    int global_count,
    char *error,
    size_t size
);
void native_definition_catalog_free(native_definition_catalog *);
const script_catalog *native_definition_catalog_view(const native_definition_catalog *);
const int *native_definition_result_pins(const native_definition_catalog *, uint64_t id, int *count);
#endif
