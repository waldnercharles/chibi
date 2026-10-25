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
    uint64_t collection; /* Immutable catalog descriptor for nested element/key types. */
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
    /* A named capability scope; only its fields produce executable values. */
    SCRIPT_TYPE_NAMESPACE = 1u << 7,
    SCRIPT_TYPE_UNION = 1u << 8,
    SCRIPT_TYPE_SINGLETON = 1u << 9,
    SCRIPT_TYPE_LITERAL = 1u << 10,
    SCRIPT_TYPE_BOXED_OPTIONAL = 1u << 11,
};
enum
{
    SCRIPT_EFFECT_READ = 1u << 0,
    SCRIPT_EFFECT_WRITE = 1u << 1,
    SCRIPT_EFFECT_STRUCTURAL = 1u << 2,
    SCRIPT_EFFECT_HOST = 1u << 3,
    SCRIPT_EFFECT_SUSPEND = 1u << 4,
    SCRIPT_EFFECT_RANDOM = 1u << 5,
    /* Retained storage or invocation values unavailable to query filters.
     * Ordinary calls and bindings may still read these values. */
    SCRIPT_EFFECT_INVOCATION_READ = 1u << 6,
};
typedef struct
{
    uint64_t id;
    const char *name;
    script_type type;
    size_t offset;
    /* Getter and setter capabilities; a getter takes the receiver. */
    uint64_t read, write;
    /* A keyed getter takes this static name instead of receiver storage. */
    const char *read_key;
    bool writable;
    /* Reads as its own name, a string constant, with no receiver storage. */
    bool constant;
    bool c_string; /* Native storage requires a NUL-free C string. */
    /* Measure, such as "u/s"; NULL without one. Checked, never stored. */
    const char *unit;
    /* Stored-value contract. A compiler may use these bounds only for values
     * produced by a validating boundary, never an unchecked native pointer. */
    bool range_contract;
    double min, max;
    bool has_default; /* Authored record construction; otherwise required. */
    pin_value default_value;
} script_catalog_field;
typedef struct
{
    uint64_t tag;
    const char *name;
    script_type type; /* Raw alternative type; tagged cases use their union type. */
    const script_catalog_field *fields;
    int field_count;
    pin_value literal;
    bool tagged, is_literal;
} script_catalog_variant;
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
    /* Computed by the engine: scripts read it but never write, add or remove it. */
    bool derived;
    const script_catalog_variant *variants;
    int variant_count;
    bool internal; /* Structural metadata without a source-visible type name. */
    bool authored_record; /* Owned flat layout, never a native catalog alias. */
    pin_container container;
    script_type key_type, element_type;
} script_catalog_type;
typedef enum
{
    SCRIPT_REFERENCE_NONE,
    SCRIPT_REFERENCE_PROTOTYPE,
    SCRIPT_REFERENCE_COMPONENT,
    SCRIPT_REFERENCE_RELATIONSHIP,
} script_reference_kind;
typedef struct
{
    const char *name;
    const char *unit; /* Erased dimensional contract; NULL means unknown. */
    script_type type;
    script_reference_kind reference_kind;
    uint64_t member; /* Reflected field identity for aggregate make/break ports. */
    bool c_string; /* An explicit foreign C-string input boundary. */
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
/* Closed, implementation-checked operations. Zero grants no extra contract;
 * generic host declarations must not acquire guarantees from a pure label. */
enum
{
    SCRIPT_OPERATION_SCALAR_ARGUMENTS = 1u << 0,
    SCRIPT_OPERATION_SCALAR_RESULTS = 1u << 1,
    SCRIPT_OPERATION_NO_ARGUMENT_RETENTION = 1u << 2,
    SCRIPT_OPERATION_NO_RESULT_ALIAS = 1u << 3,
    SCRIPT_OPERATION_NO_ALLOCATION = 1u << 4,
    SCRIPT_OPERATION_NO_PAYLOAD_COPY = 1u << 5,
    SCRIPT_OPERATION_ACTIVATION_THREAD = 1u << 6,
    SCRIPT_OPERATION_NO_REENTRY = 1u << 7,
    SCRIPT_OPERATION_SAVES_LIVE_VALUES = 1u << 8,
};
#define SCRIPT_OPERATION_SCALAR_READ                                             \
    (SCRIPT_OPERATION_SCALAR_ARGUMENTS | SCRIPT_OPERATION_SCALAR_RESULTS |       \
     SCRIPT_OPERATION_NO_ARGUMENT_RETENTION | SCRIPT_OPERATION_NO_RESULT_ALIAS | \
     SCRIPT_OPERATION_NO_ALLOCATION | SCRIPT_OPERATION_NO_PAYLOAD_COPY |         \
     SCRIPT_OPERATION_ACTIVATION_THREAD | SCRIPT_OPERATION_NO_REENTRY)
/* Identity, actual instruction name, semantic version, effects, guarantees.
 * Both catalog ABI and execution binding are generated from these same rows. */
#define SCRIPT_ENGINE_OPERATIONS(X)                                                 \
    X(OWNER, OWNER, 1, SCRIPT_EFFECT_INVOCATION_READ, SCRIPT_OPERATION_SCALAR_READ) \
    X(DELAY,                                                                        \
      DELAY,                                                                        \
      1,                                                                            \
      SCRIPT_EFFECT_SUSPEND,                                                        \
      SCRIPT_OPERATION_SCALAR_ARGUMENTS | SCRIPT_OPERATION_ACTIVATION_THREAD |      \
          SCRIPT_OPERATION_SAVES_LIVE_VALUES)
typedef enum
{
    SCRIPT_OPERATION_UNKNOWN,
#define OPERATION(NAME, INSTRUCTION, VERSION, EFFECTS, GUARANTEES)             \
    SCRIPT_OPERATION_##NAME,
    SCRIPT_ENGINE_OPERATIONS(OPERATION)
#undef OPERATION
} script_operation;
typedef struct
{
    const char *name, *instruction;
    unsigned version, effects, guarantees;
} script_operation_contract;
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
    script_operation operation;
    uint64_t receiver;
    bool pure, variadic, variadic_optional, numeric_aggregate, event_sender;
} script_catalog_capability;
typedef struct
{
    uint64_t id;
    const char *name;
    script_type type;
    bool writable;
    const char *unit; /* NULL without one */
} script_catalog_global;
typedef struct
{
    uint64_t id;
    const char *name;
    bool exclusive, traversable;
} script_catalog_relationship;
typedef struct
{
    bool is_type;
    script_type type;
    pin_value value;
} script_compile_value;
typedef struct
{
    char *name;
    script_compile_value value;
} script_compile_binding;
typedef enum
{
    SCRIPT_TEMPLATE_FUNCTION,
    SCRIPT_TEMPLATE_RECORD,
    SCRIPT_TEMPLATE_TYPE_FUNCTION,
} script_template_kind;
typedef struct
{
    const char *name, *binder;
    script_type type;
    bool comptime, type_value, binds_type;
} script_template_parameter;
typedef struct
{
    uint64_t id;
    const char *name, *owner;
    uint64_t source_generation;
    script_template_kind kind;
    const script_template_parameter *parameters;
    int parameter_count;
    int start, end; /* Declaration range in its owning source, never the caller. */
    bool generic;
} script_catalog_template;
typedef struct
{
    uint64_t id, template_id;
    const script_compile_binding *bindings;
    int binding_count;
    script_compile_value result;
    bool executable;
} script_catalog_specialization;
/* Stable built-in storage identity, shared with the ECS ChildOf contract. */
#define SCRIPT_CHILD_OF UINT64_C(0x84771d550890f1a1)
typedef enum
{
    SCRIPT_SOURCE_SELF,
    SCRIPT_SOURCE_ANCESTOR,
    SCRIPT_SOURCE_SELF_OR_ANCESTOR,
} script_query_source;
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
    const script_catalog_relationship *relationships;
    int relationship_count;
    const script_catalog_template *templates;
    int template_count;
    const script_catalog_specialization *specializations;
    int specialization_count;
} script_catalog;

uint64_t script_catalog_id(const char *domain, const char *name);
uint64_t script_catalog_id_bytes(const char *domain, const char *bytes, size_t length);
/* The loaded project's declared script types. The owner sets the slot while no
 * project is loaded and clears it (NULL) before freeing them. */
void script_catalog_set_project(const script_catalog *);
const script_catalog *script_catalog_project();
bool script_name_matches(const char *a, const char *b);
/* -1 is missing; -2 is ambiguous. Exact names precede normalized aliases. */
int script_catalog_find_type(const script_catalog *, const char *name);
int script_catalog_find_capability(const script_catalog *, const char *name, script_capability_kind);
int script_catalog_find_field(const script_catalog_type *, const char *name);
/* Declared executable ABI; excludes callbacks, defaults and source aliases. */
uint64_t script_catalog_capability_abi(const script_catalog *, const script_catalog_capability *);
/* Bounded exact evidence for declared defaults and specialization constants.
 * Caller releases the returned dynamic byte array with afree. */
bool script_catalog_values_key(const script_catalog *, unsigned char **);
/* NULL for unknown/unrecognized operations. Absent guarantees are unknown,
 * never proof of the opposite. Known declarations must match their contract. */
const script_operation_contract *script_operation_describe(script_operation);
bool script_catalog_operation_valid(const script_catalog_capability *);
const script_catalog_type *script_catalog_type_by_id(const script_catalog *, uint64_t id);
const script_catalog_capability *script_catalog_capability_by_id(const script_catalog *, uint64_t id);
script_type script_builtin_type(const char *name);
/* One Unicode scalar: int storage under its own nominal, so a char converts
 * to int implicitly and back only through a checked char(value). */
script_type script_char_type();
bool script_type_char(script_type);
/* The source name of a numeric type: char, or its storage's name. */
const char *script_numeric_name(script_type);
script_type script_reference_type(script_reference_kind);
script_reference_kind script_reference_kind_for_type(script_type);
const char *script_reference_type_name(script_reference_kind);
script_type script_catalog_canonical_type(const script_catalog *, script_type);
bool script_catalog_type_assignable(const script_catalog *, script_type expected, script_type actual);
bool script_type_equal(script_type a, script_type b);
script_type script_catalog_pointee_type(const script_catalog *, script_type);
script_type script_catalog_element_type(const script_catalog *, script_type);
script_type script_catalog_key_type(const script_catalog *, script_type);
bool script_type_numeric(script_type type);
/* Numeric storages convert implicitly when no integer value is lost: integer
 * widening and any conversion involving a floating type. Integer narrowing
 * and sign changes require an explicit T(value). */
bool script_numeric_implicit(pin_type from, pin_type to);
bool script_type_assignable(script_type expected, script_type actual);
uint64_t script_type_tag(script_type);
script_type script_cancelled_type();
const char *script_scalar_name(pin_type storage);
#endif
