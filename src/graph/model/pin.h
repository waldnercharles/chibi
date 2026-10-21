#ifndef CHIBI_PIN_H
#define CHIBI_PIN_H

#include "types/value.h"

typedef enum
{
    PIN_DATA = 0,
    PIN_EXEC = 1,
} pin_kind;
typedef enum
{
    PIN_REFERENCE_NONE,
    PIN_REFERENCE_PROTOTYPE,
    PIN_REFERENCE_COMPONENT,
} pin_reference_kind;

typedef enum
{
    PIN_BINDING_VARIABLE = 1,
    PIN_BINDING_COMPONENT_FIELD,
    PIN_BINDING_COMPONENT,
    PIN_BINDING_LOCAL,
    PIN_BINDING_EVENT_OUTPUT,
} pin_binding_kind;

/* Authored source for an unconnected data input. Strings are owned by the
 * containing graph. Variable IDs are stable within their source asset; a
 * source asset is retained so clipboard copies cannot silently bind a local
 * ID from another behavior. Component bindings use component_key as the
 * stable reflected identity; field_name is NULL for a whole component or a
 * dot-delimited path for a nested field. Event output bindings use the event
 * node and output pin IDs from the containing event graph. */
typedef struct pin_binding
{
    pin_binding_kind kind;
    uint64_t variable_id;
    uint64_t local_id;
    uint64_t event_node_id;
    uint64_t event_pin_id;
    const char *source_asset;
    const char *component_key;
    const char *field_name;
} pin_binding;

enum
{
    PIN_FLAG_ADVANCED = 1u << 0,
    PIN_FLAG_WILDCARD = 1u << 1,
    PIN_FLAG_REFERENCE = 1u << 2,
    PIN_FLAG_SERVICE = 1u << 3,
    PIN_FLAG_WATCH = 1u << 4,
    PIN_FLAG_DEFAULT_SELF = 1u << 5,
    PIN_FLAG_COLOR = 1u << 6, /* RGB(A) presentation for vectors and packed colors. */
    PIN_FLAG_SETTING = 1u << 7, /* Authored configuration without a data socket. */
};

/* A descriptor pin is also the shape of an instance pin. Descriptor fields
 * are templates; graph_add_node() copies them and assigns id/default storage. */
typedef struct
{
    const char *name;
    pin_type type;
    const char *type_name;
    pin_kind kind;
    uint64_t id;
    pin_container container;
    pin_value default_value;
    uint64_t split_parent;
    unsigned flags;
    int group;
    const char *split_type_name;
    uint64_t field_source; /* retained aggregate output pin ID on the same node */
    const char *field_name;
    uint64_t declaration_id; /* Stable parameter identity in an event definition. */
    pin_binding *binding; /* Optional authored source for an unconnected input. */
    const char *display_name; /* Optional editor label; never an identity or runtime name. */
    pin_reference_kind reference_kind;
} pin_spec;

#endif
