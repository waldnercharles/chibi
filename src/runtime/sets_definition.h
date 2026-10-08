#ifndef CHIBI_NATIVE_SET_DEFINITION_H
#define CHIBI_NATIVE_SET_DEFINITION_H
#include "runtime/native_definitions.h"
#define NATIVE_SET_PORT(X, NAME, STORAGE, TYPE_NAME, CONTAINER, FLAGS, GROUP)  \
    X(NAME,                                                                    \
      NATIVE_PARAMETER_DATA,                                                   \
      STORAGE,                                                                 \
      TYPE_NAME,                                                               \
      CONTAINER,                                                               \
      FLAGS,                                                                   \
      GROUP,                                                                   \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = STORAGE, .container = CONTAINER }))
#define NATIVE_SET_VALUE(X)                                                    \
    NATIVE_SET_PORT(X, "Set", PIN_FLOW, NULL, PIN_CONTAINER_SET, SCRIPT_TYPE_UNTYPED, 1)
#define NATIVE_SET_ITEM(X)                                                     \
    NATIVE_SET_PORT(X, "Item", PIN_FLOW, NULL, PIN_CONTAINER_SINGLE, SCRIPT_TYPE_UNTYPED, 1)
#define NATIVE_SET_MAKE_INPUTS(X) NATIVE_SET_ITEM(X)
#define NATIVE_SET_SET_OUTPUTS(X) NATIVE_SET_VALUE(X)
#define NATIVE_SET_ONLY_INPUTS(X) NATIVE_SET_VALUE(X)
#define NATIVE_SET_ITEM_INPUTS(X) NATIVE_SET_VALUE(X) NATIVE_SET_ITEM(X)
#define NATIVE_SET_BOOL_OUTPUT(X)                                              \
    NATIVE_SET_PORT(X, "Return Value", PIN_BOOL, "bool", PIN_CONTAINER_SINGLE, 0, 0)
#define NATIVE_SET_INT_OUTPUT(X)                                               \
    NATIVE_SET_PORT(X, "Length", PIN_I32, "int", PIN_CONTAINER_SINGLE, 0, 0)
#define NATIVE_SET_MAKE_NAME "Make Set"
#define NATIVE_SET_MAKE_SOURCE "make_set"
#define NATIVE_SET_ADD_NAME "Set Add"
#define NATIVE_SET_ADD_SOURCE "set_add"
#define NATIVE_SET_REMOVE_NAME "Set Remove"
#define NATIVE_SET_REMOVE_SOURCE "set_remove"
#define NATIVE_SET_CONTAINS_NAME "Set Contains"
#define NATIVE_SET_CONTAINS_SOURCE "set_contains"
#define NATIVE_SET_LENGTH_NAME "Set Length"
#define NATIVE_SET_LENGTH_SOURCE "set_length"
#define NATIVE_SET_OPERATIONS(X)                                                                              \
    X(make,                                                                                                   \
      NATIVE_SET_MAKE_NAME,                                                                                   \
      NATIVE_SET_MAKE_SOURCE,                                                                                 \
      NATIVE_SET_MAKE_INPUTS,                                                                                 \
      NATIVE_SET_SET_OUTPUTS,                                                                                 \
      true)                                                                                                   \
    X(add, NATIVE_SET_ADD_NAME, NATIVE_SET_ADD_SOURCE, NATIVE_SET_ITEM_INPUTS, NATIVE_SET_SET_OUTPUTS, false) \
    X(remove,                                                                                                 \
      NATIVE_SET_REMOVE_NAME,                                                                                 \
      NATIVE_SET_REMOVE_SOURCE,                                                                               \
      NATIVE_SET_ITEM_INPUTS,                                                                                 \
      NATIVE_SET_SET_OUTPUTS,                                                                                 \
      false)                                                                                                  \
    X(contains,                                                                                               \
      NATIVE_SET_CONTAINS_NAME,                                                                               \
      NATIVE_SET_CONTAINS_SOURCE,                                                                             \
      NATIVE_SET_ITEM_INPUTS,                                                                                 \
      NATIVE_SET_BOOL_OUTPUT,                                                                                 \
      false)                                                                                                  \
    X(length,                                                                                                 \
      NATIVE_SET_LENGTH_NAME,                                                                                 \
      NATIVE_SET_LENGTH_SOURCE,                                                                               \
      NATIVE_SET_ONLY_INPUTS,                                                                                 \
      NATIVE_SET_INT_OUTPUT,                                                                                  \
      false)
extern const native_definition_pack NATIVE_SET_DEFINITION_PACK;

#define NATIVE_SET_make_PRESENTATION                                           \
    .category = "Set", .keywords = "set make collection",                      \
    .description = "Builds a set from the Item inputs, dropping duplicates."
#define NATIVE_SET_add_PRESENTATION                                            \
    .category = "Set", .keywords = "set add unique",                           \
    .description = "Returns a copy of Set with Item added, unchanged if an "   \
                   "equal item is already present."
#define NATIVE_SET_remove_PRESENTATION                                         \
    .category = "Set", .keywords = "set remove delete item",                   \
    .description = "Returns a copy of Set with Item removed, unchanged if "    \
                   "the "                                                      \
                   "item is absent."
#define NATIVE_SET_contains_PRESENTATION                                       \
    .category = "Set", .keywords = "set contains equality",                    \
    .description = "Whether Set holds an item equal to Item."
#define NATIVE_SET_length_PRESENTATION                                         \
    .category = "Set", .keywords = "set length",                               \
    .description = "Returns the number of items in Set."

#endif
