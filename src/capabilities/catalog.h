#ifndef CHIBI_CAPABILITIES_CATALOG_H
#define CHIBI_CAPABILITIES_CATALOG_H

#include "types/state_machine.h"
#include "types/value.h"

/* Resolved meaning is independent of process addresses and graph presentation.
 * IDs are stable within the catalog ABI; nominal zero denotes a scalar type. */
typedef struct
{
    pin_type storage;
    pin_container container;
    uint64_t nominal, parameter;
    unsigned flags;
} script_type;
enum
{
    SCRIPT_TYPE_ERROR = 1u << 0,
    SCRIPT_TYPE_OPTIONAL = 1u << 1,
    SCRIPT_TYPE_REFERENCE = 1u << 2,
    SCRIPT_TYPE_RESULTS = 1u << 3,
    SCRIPT_TYPE_UNTYPED = 1u << 4,
    SCRIPT_TYPE_GENERIC = 1u << 5,
    /* A task handle; its nominal names the function that started it, if known. */
    SCRIPT_TYPE_TASK = 1u << 6,
};
enum
{
    SCRIPT_EFFECT_READ = 1u << 0,
    SCRIPT_EFFECT_WRITE = 1u << 1,
    SCRIPT_EFFECT_STRUCTURAL = 1u << 2,
    SCRIPT_EFFECT_HOST = 1u << 3,
    SCRIPT_EFFECT_SUSPEND = 1u << 4,
    SCRIPT_EFFECT_RANDOM = 1u << 5,
};
typedef struct
{
    uint64_t id;
    const char *name;
    script_type type;
    size_t offset;
    uint64_t read, write;
    bool writable;
} script_catalog_field;
typedef struct
{
    uint64_t id;
    const char *name, *source_name, *key;
    script_type type;
    const script_catalog_field *fields;
    int field_count;
    size_t size;
    uint64_t layout, underlying;
    uint64_t component_id; /* ECS storage ID, distinct from this catalog type ID. */
    bool component;
} script_catalog_type;
typedef enum
{
    SCRIPT_REFERENCE_NONE,
    SCRIPT_REFERENCE_PROTOTYPE,
    SCRIPT_REFERENCE_COMPONENT,
} script_reference_kind;
typedef struct
{
    const char *name;
    script_type type;
    script_reference_kind reference_kind;
    uint64_t member; /* Reflected field identity for aggregate make/break ports. */
    bool has_default;
    bool default_owner;
    pin_value default_value;
} script_catalog_parameter;
typedef enum
{
    SCRIPT_CAPABILITY_FUNCTION,
    SCRIPT_CAPABILITY_ACTION,
    SCRIPT_CAPABILITY_EVENT,
    SCRIPT_CAPABILITY_PROTOTYPE,
} script_capability_kind;
typedef enum
{
    SCRIPT_STATE_ACCESS_NONE,
    SCRIPT_STATE_ACCESS_READ,
    SCRIPT_STATE_ACCESS_WRITE
} script_state_access;
typedef enum
{
    SCRIPT_COMPONENT_ACCESS_NONE,
    SCRIPT_COMPONENT_ACCESS_READ,
    SCRIPT_COMPONENT_ACCESS_WRITE,
    SCRIPT_COMPONENT_ACCESS_HAS,
    SCRIPT_COMPONENT_ACCESS_ADD,
    SCRIPT_COMPONENT_ACCESS_REMOVE,
} script_component_access;
typedef enum
{
    SCRIPT_STRUCT_ACCESS_NONE,
    SCRIPT_STRUCT_ACCESS_MAKE,
    SCRIPT_STRUCT_ACCESS_BREAK,
} script_struct_access;
typedef enum
{
    SCRIPT_FLOW_ACCESS_NONE,
    SCRIPT_FLOW_ACCESS_BRANCH,
    SCRIPT_FLOW_ACCESS_RANGE,
    SCRIPT_FLOW_ACCESS_SEQUENCE,
    SCRIPT_FLOW_ACCESS_WHILE,
    SCRIPT_FLOW_ACCESS_EACH,
    SCRIPT_FLOW_ACCESS_BREAK,
} script_flow_access;
typedef struct
{
    script_flow_access kind;
    /* Data ordinals: condition/array/first, last; index, element.
     * Outcome ordinals: true/body, false/completed. Break is an entry ordinal. */
    int inputs[2], results[2], outcomes[2], break_entry;
} script_catalog_flow;
typedef struct
{
    uint64_t id;
    const char *name, *qualified_name, *operator_name, *method_name;
    /* Optional authoring display text; excluded from executable ABI. */
    const char *category, *keywords, *description;
    script_capability_kind kind;
    script_state_access state_access;
    native_state_kind state_kind;
    script_component_access component_access;
    script_struct_access struct_access;
    uint64_t component; /* Nominal catalog type ID; resolve its component_id for ECS. */
    uint64_t component_field, aggregate;
    int component_entity, component_value; /* Data ordinals; -1 means owner/no value. */
    script_catalog_flow flow;
    const script_catalog_parameter *parameters, *results;
    int parameter_count, result_count;
    const char *const *entries, *const *outcomes;
    int entry_count, outcome_count;
    int completion_outcome;
    unsigned effects;
    uint64_t receiver;
    bool pure, variadic, variadic_optional, numeric_aggregate, event_sender;
} script_catalog_capability;
typedef struct
{
    uint64_t id;
    const char *name;
    script_type type;
    bool writable;
} script_catalog_global;
/* Immutable borrowed declaration catalog. Analysis copies resolved identities
 * and types; callers can release this catalog after analysis completes. */
typedef struct
{
    const script_catalog_type *types;
    int type_count;
    const script_catalog_capability *capabilities;
    int capability_count;
    const script_catalog_global *globals;
    int global_count;
    uint64_t entity_type;
    uint64_t tick_event;
} script_catalog;

uint64_t script_catalog_id(const char *domain, const char *name);
bool script_name_matches(const char *a, const char *b);
/* -1 is missing; -2 is ambiguous. Exact names precede normalized aliases. */
int script_catalog_find_type(const script_catalog *, const char *name);
int script_catalog_find_capability(const script_catalog *, const char *name, script_capability_kind);
int script_catalog_find_field(const script_catalog_type *, const char *name);
/* Declared executable ABI; excludes callbacks, defaults and source aliases. */
uint64_t script_catalog_capability_abi(const script_catalog *, const script_catalog_capability *);
const script_catalog_type *script_catalog_type_by_id(const script_catalog *, uint64_t id);
const script_catalog_capability *script_catalog_capability_by_id(const script_catalog *, uint64_t id);
script_type script_builtin_type(const char *name);
script_type script_catalog_canonical_type(const script_catalog *, script_type);
bool script_catalog_type_assignable(const script_catalog *, script_type expected, script_type actual);
bool script_type_equal(script_type a, script_type b);
bool script_type_numeric(script_type type);
bool script_type_assignable(script_type expected, script_type actual);
const char *script_scalar_name(pin_type storage);
#endif
