#ifndef CHIBI_GRAPH_NATIVE_DEFINITION_H
#define CHIBI_GRAPH_NATIVE_DEFINITION_H

#include "capabilities/definition.h"
#include "graph/model/pin.h"

#define GRAPH_NATIVE_PORT_FLAGS(                                               \
    NAME,                                                                      \
    KIND,                                                                      \
    STORAGE,                                                                   \
    TYPE_NAME,                                                                 \
    CONTAINER,                                                                 \
    TYPE_FLAGS,                                                                \
    GROUP,                                                                     \
    REFERENCE,                                                                 \
    OWNER,                                                                     \
    VALUE,                                                                     \
    GRAPH_FLAGS                                                                \
)                                                                              \
    { .name = NAME,                                                            \
      .kind = (pin_kind)KIND,                                                  \
      .type = STORAGE,                                                         \
      .type_name = TYPE_NAME,                                                  \
      .container = CONTAINER,                                                  \
      .flags = ((TYPE_FLAGS & SCRIPT_TYPE_UNTYPED) ? PIN_FLAG_WILDCARD : 0) |  \
               (OWNER ? PIN_FLAG_DEFAULT_SELF : 0) | GRAPH_FLAGS,              \
      .group = GROUP,                                                          \
      .reference_kind = (pin_reference_kind)REFERENCE,                         \
      .default_value = VALUE },

#define GRAPH_NATIVE_PORT(NAME, KIND, STORAGE, TYPE_NAME, CONTAINER, TYPE_FLAGS, GROUP, REFERENCE, OWNER, VALUE) \
    GRAPH_NATIVE_PORT_FLAGS(NAME, KIND, STORAGE, TYPE_NAME, CONTAINER, TYPE_FLAGS, GROUP, REFERENCE, OWNER, VALUE, 0)

#endif
