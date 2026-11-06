#ifndef CHIBI_STUDIO_DEFINITION_H
#define CHIBI_STUDIO_DEFINITION_H
#include "authoring/studio_schema.h"
#include "runtime/native_definitions.h"
#include "runtime/type_definitions.h"
#define STUDIO_PORT(X, NAME, KIND, STORAGE, NOMINAL, CONTAINER)                \
    X(NAME, KIND, STORAGE, NOMINAL, CONTAINER, 0, 0, SCRIPT_REFERENCE_NONE, false, ((pin_value){ 0 }))
#define STUDIO_THEN_OUT_PORTS(X)                                               \
    STUDIO_PORT(X, "Then", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL, PIN_CONTAINER_SINGLE)
#define STUDIO_WINDOW_IN_PORTS(X)                                                        \
    STUDIO_PORT(X, "Exec", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL, PIN_CONTAINER_SINGLE)  \
    STUDIO_PORT(X, "First", NATIVE_PARAMETER_DATA, PIN_I32, "int", PIN_CONTAINER_SINGLE) \
    STUDIO_PORT(X, "Count", NATIVE_PARAMETER_DATA, PIN_I32, "int", PIN_CONTAINER_SINGLE)
#define STUDIO_OBJECTS_OUT_PORTS(X)                                                                        \
    STUDIO_PORT(X, "Then", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL, PIN_CONTAINER_SINGLE)                    \
    STUDIO_PORT(X, "Rows", NATIVE_PARAMETER_DATA, PIN_STRUCT, STUDIO_OBJECT_ROW_TYPE, PIN_CONTAINER_ARRAY) \
    STUDIO_PORT(X, "Shown", NATIVE_PARAMETER_DATA, PIN_I32, "int", PIN_CONTAINER_SINGLE)
#define STUDIO_INFO_OUT_PORTS(X)                                                                  \
    STUDIO_PORT(X, "Title", NATIVE_PARAMETER_DATA, PIN_STR, "const char *", PIN_CONTAINER_SINGLE) \
    STUDIO_PORT(X, "Objects", NATIVE_PARAMETER_DATA, PIN_I32, "int", PIN_CONTAINER_SINGLE)        \
    STUDIO_PORT(X, "Filter", NATIVE_PARAMETER_DATA, PIN_STR, "const char *", PIN_CONTAINER_SINGLE)
#define STUDIO_SELECTION_OUT_PORTS(X)                                                              \
    STUDIO_PORT(X, "Active", NATIVE_PARAMETER_DATA, PIN_STR, "const char *", PIN_CONTAINER_SINGLE) \
    STUDIO_PORT(X, "Count", NATIVE_PARAMETER_DATA, PIN_I32, "int", PIN_CONTAINER_SINGLE)           \
    STUDIO_PORT(X, "Name", NATIVE_PARAMETER_DATA, PIN_STR, "const char *", PIN_CONTAINER_SINGLE)   \
    STUDIO_PORT(X, "Prototype", NATIVE_PARAMETER_DATA, PIN_STR, "const char *", PIN_CONTAINER_SINGLE)
#define STUDIO_OBJECT_IN_PORTS(X)                                              \
    STUDIO_PORT(X, "Object", NATIVE_PARAMETER_DATA, PIN_STR, "const char *", PIN_CONTAINER_SINGLE)
#define STUDIO_SELECTED_OUT_PORTS(X)                                           \
    STUDIO_PORT(X, "Selected", NATIVE_PARAMETER_DATA, PIN_BOOL, "bool", PIN_CONTAINER_SINGLE)
#define STUDIO_ROW_OUT_PORTS(X)                                                \
    STUDIO_PORT(X, "Row", NATIVE_PARAMETER_DATA, PIN_I32, "int", PIN_CONTAINER_SINGLE)
#define STUDIO_RENAMING_OUT_PORTS(X)                                           \
    STUDIO_PORT(X, "Object", NATIVE_PARAMETER_DATA, PIN_STR, "const char *", PIN_CONTAINER_SINGLE)
#define STUDIO_STATE_OUT_PORTS(X)                                                             \
    STUDIO_PORT(X, "Editable", NATIVE_PARAMETER_DATA, PIN_BOOL, "bool", PIN_CONTAINER_SINGLE) \
    STUDIO_PORT(X, "Live", NATIVE_PARAMETER_DATA, PIN_BOOL, "bool", PIN_CONTAINER_SINGLE)     \
    STUDIO_PORT(X, "Mac", NATIVE_PARAMETER_DATA, PIN_BOOL, "bool", PIN_CONTAINER_SINGLE)
#define STUDIO_PROTOTYPE_IN_PORTS(X)                                           \
    STUDIO_PORT(X, "Prototype", NATIVE_PARAMETER_DATA, PIN_STR, "const char *", PIN_CONTAINER_SINGLE)
#define STUDIO_ON_OUT_PORTS(X)                                                 \
    STUDIO_PORT(X, "On", NATIVE_PARAMETER_DATA, PIN_BOOL, "bool", PIN_CONTAINER_SINGLE)
#define STUDIO_QUERY_IN_PORTS(X)                                               \
    STUDIO_PORT(X, "Exec", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL, PIN_CONTAINER_SINGLE)
#define STUDIO_CHOICES_OUT_PORTS(X)                                                     \
    STUDIO_PORT(X, "Then", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL, PIN_CONTAINER_SINGLE) \
    STUDIO_PORT(X, "Options", NATIVE_PARAMETER_DATA, PIN_STRUCT, STUDIO_CHOICE_TYPE, PIN_CONTAINER_ARRAY)
#define STUDIO_SELECT_IN_PORTS(X)                                                                  \
    STUDIO_PORT(X, "Exec", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL, PIN_CONTAINER_SINGLE)            \
    STUDIO_PORT(X, "Object", NATIVE_PARAMETER_DATA, PIN_STR, "const char *", PIN_CONTAINER_SINGLE) \
    STUDIO_PORT(X, "Toggle", NATIVE_PARAMETER_DATA, PIN_BOOL, "bool", PIN_CONTAINER_SINGLE)        \
    STUDIO_PORT(X, "Extend", NATIVE_PARAMETER_DATA, PIN_BOOL, "bool", PIN_CONTAINER_SINGLE)
#define STUDIO_RENAME_IN_PORTS(X)                                                                  \
    STUDIO_PORT(X, "Exec", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL, PIN_CONTAINER_SINGLE)            \
    STUDIO_PORT(X, "Object", NATIVE_PARAMETER_DATA, PIN_STR, "const char *", PIN_CONTAINER_SINGLE) \
    STUDIO_PORT(X, "Name", NATIVE_PARAMETER_DATA, PIN_STR, "const char *", PIN_CONTAINER_SINGLE)
#define STUDIO_END_IN_PORTS(X)                                                 \
    STUDIO_PORT(X, "Exec", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL, PIN_CONTAINER_SINGLE)
#define STUDIO_HIDDEN_IN_PORTS(X)                                                                  \
    STUDIO_PORT(X, "Exec", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL, PIN_CONTAINER_SINGLE)            \
    STUDIO_PORT(X, "Object", NATIVE_PARAMETER_DATA, PIN_STR, "const char *", PIN_CONTAINER_SINGLE) \
    STUDIO_PORT(X, "Hidden", NATIVE_PARAMETER_DATA, PIN_BOOL, "bool", PIN_CONTAINER_SINGLE)
#define STUDIO_LOCKED_IN_PORTS(X)                                                                  \
    STUDIO_PORT(X, "Exec", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL, PIN_CONTAINER_SINGLE)            \
    STUDIO_PORT(X, "Object", NATIVE_PARAMETER_DATA, PIN_STR, "const char *", PIN_CONTAINER_SINGLE) \
    STUDIO_PORT(X, "Locked", NATIVE_PARAMETER_DATA, PIN_BOOL, "bool", PIN_CONTAINER_SINGLE)
#define STUDIO_OBJECT_EXEC_IN_PORTS(X)                                                  \
    STUDIO_PORT(X, "Exec", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL, PIN_CONTAINER_SINGLE) \
    STUDIO_PORT(X, "Object", NATIVE_PARAMETER_DATA, PIN_STR, "const char *", PIN_CONTAINER_SINGLE)
#define STUDIO_PROTOTYPE_EXEC_IN_PORTS(X)                                               \
    STUDIO_PORT(X, "Exec", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL, PIN_CONTAINER_SINGLE) \
    STUDIO_PORT(X, "Prototype", NATIVE_PARAMETER_DATA, PIN_STR, "const char *", PIN_CONTAINER_SINGLE)
#define STUDIO_DROP_IN_PORTS(X)                                                                    \
    STUDIO_PORT(X, "Exec", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL, PIN_CONTAINER_SINGLE)            \
    STUDIO_PORT(X, "Object", NATIVE_PARAMETER_DATA, PIN_STR, "const char *", PIN_CONTAINER_SINGLE) \
    STUDIO_PORT(X, "Type", NATIVE_PARAMETER_DATA, PIN_STR, "const char *", PIN_CONTAINER_SINGLE)   \
    STUDIO_PORT(X, "Payload", NATIVE_PARAMETER_DATA, PIN_STR, "const char *", PIN_CONTAINER_SINGLE)
#define STUDIO_MOVE_IN_PORTS(X)                                                                    \
    STUDIO_PORT(X, "Exec", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL, PIN_CONTAINER_SINGLE)            \
    STUDIO_PORT(X, "Object", NATIVE_PARAMETER_DATA, PIN_STR, "const char *", PIN_CONTAINER_SINGLE) \
    STUDIO_PORT(X, "Target", NATIVE_PARAMETER_DATA, PIN_STR, "const char *", PIN_CONTAINER_SINGLE) \
    STUDIO_PORT(X, "Zone", NATIVE_PARAMETER_DATA, PIN_STR, "const char *", PIN_CONTAINER_SINGLE)
#define STUDIO_FILTER_IN_PORTS(X)                                                       \
    STUDIO_PORT(X, "Exec", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL, PIN_CONTAINER_SINGLE) \
    STUDIO_PORT(X, "Filter", NATIVE_PARAMETER_DATA, PIN_STR, "const char *", PIN_CONTAINER_SINGLE)
#define STUDIO_NONE_PORTS(X)
#define STUDIO_SCENE_OBJECTS_NAME "Scene Objects"
#define STUDIO_SCENE_OBJECTS_INPUTS(X) STUDIO_WINDOW_IN_PORTS(X)
#define STUDIO_SCENE_OBJECTS_OUTPUTS(X) STUDIO_OBJECTS_OUT_PORTS(X)
#define STUDIO_SCENE_INFO_NAME "Scene Info"
#define STUDIO_SCENE_INFO_INPUTS(X) STUDIO_NONE_PORTS(X)
#define STUDIO_SCENE_INFO_OUTPUTS(X) STUDIO_INFO_OUT_PORTS(X)
#define STUDIO_SELECTION_NAME "Selection"
#define STUDIO_SELECTION_INPUTS(X) STUDIO_NONE_PORTS(X)
#define STUDIO_SELECTION_OUTPUTS(X) STUDIO_SELECTION_OUT_PORTS(X)
#define STUDIO_OBJECT_SELECTED_NAME "Object Selected"
#define STUDIO_OBJECT_SELECTED_INPUTS(X) STUDIO_OBJECT_IN_PORTS(X)
#define STUDIO_OBJECT_SELECTED_OUTPUTS(X) STUDIO_SELECTED_OUT_PORTS(X)
#define STUDIO_OBJECT_ROW_NAME "Object Row"
#define STUDIO_OBJECT_ROW_INPUTS(X) STUDIO_OBJECT_IN_PORTS(X)
#define STUDIO_OBJECT_ROW_OUTPUTS(X) STUDIO_ROW_OUT_PORTS(X)
#define STUDIO_RENAMING_NAME "Renaming Object"
#define STUDIO_RENAMING_INPUTS(X) STUDIO_NONE_PORTS(X)
#define STUDIO_RENAMING_OUTPUTS(X) STUDIO_RENAMING_OUT_PORTS(X)
#define STUDIO_EDITOR_STATE_NAME "Editor State"
#define STUDIO_EDITOR_STATE_INPUTS(X) STUDIO_NONE_PORTS(X)
#define STUDIO_EDITOR_STATE_OUTPUTS(X) STUDIO_STATE_OUT_PORTS(X)
#define STUDIO_SPAWN_BREAK_NAME "Breaks When Spawned"
#define STUDIO_SPAWN_BREAK_INPUTS(X) STUDIO_PROTOTYPE_IN_PORTS(X)
#define STUDIO_SPAWN_BREAK_OUTPUTS(X) STUDIO_ON_OUT_PORTS(X)
#define STUDIO_PROTOTYPE_CHOICES_NAME "Prototype Choices"
#define STUDIO_PROTOTYPE_CHOICES_INPUTS(X) STUDIO_QUERY_IN_PORTS(X)
#define STUDIO_PROTOTYPE_CHOICES_OUTPUTS(X) STUDIO_CHOICES_OUT_PORTS(X)
#define STUDIO_SELECT_OBJECT_NAME "Select Object"
#define STUDIO_SELECT_OBJECT_INPUTS(X) STUDIO_SELECT_IN_PORTS(X)
#define STUDIO_SELECT_OBJECT_OUTPUTS(X) STUDIO_THEN_OUT_PORTS(X)
#define STUDIO_RENAME_OBJECT_NAME "Rename Object"
#define STUDIO_RENAME_OBJECT_INPUTS(X) STUDIO_RENAME_IN_PORTS(X)
#define STUDIO_RENAME_OBJECT_OUTPUTS(X) STUDIO_THEN_OUT_PORTS(X)
#define STUDIO_END_RENAME_NAME "End Rename"
#define STUDIO_END_RENAME_INPUTS(X) STUDIO_END_IN_PORTS(X)
#define STUDIO_END_RENAME_OUTPUTS(X) STUDIO_THEN_OUT_PORTS(X)
#define STUDIO_SET_HIDDEN_NAME "Set Object Hidden"
#define STUDIO_SET_HIDDEN_INPUTS(X) STUDIO_HIDDEN_IN_PORTS(X)
#define STUDIO_SET_HIDDEN_OUTPUTS(X) STUDIO_THEN_OUT_PORTS(X)
#define STUDIO_SET_LOCKED_NAME "Set Object Locked"
#define STUDIO_SET_LOCKED_INPUTS(X) STUDIO_LOCKED_IN_PORTS(X)
#define STUDIO_SET_LOCKED_OUTPUTS(X) STUDIO_THEN_OUT_PORTS(X)
#define STUDIO_ISOLATE_OBJECT_NAME "Isolate Object"
#define STUDIO_ISOLATE_OBJECT_INPUTS(X) STUDIO_OBJECT_EXEC_IN_PORTS(X)
#define STUDIO_ISOLATE_OBJECT_OUTPUTS(X) STUDIO_THEN_OUT_PORTS(X)
#define STUDIO_ADD_OBJECT_NAME "Add Object"
#define STUDIO_ADD_OBJECT_INPUTS(X) STUDIO_PROTOTYPE_EXEC_IN_PORTS(X)
#define STUDIO_ADD_OBJECT_OUTPUTS(X) STUDIO_THEN_OUT_PORTS(X)
#define STUDIO_DROP_ON_OBJECT_NAME "Drop on Object"
#define STUDIO_DROP_ON_OBJECT_INPUTS(X) STUDIO_DROP_IN_PORTS(X)
#define STUDIO_DROP_ON_OBJECT_OUTPUTS(X) STUDIO_THEN_OUT_PORTS(X)
#define STUDIO_MOVE_OBJECTS_NAME "Move Objects"
#define STUDIO_MOVE_OBJECTS_INPUTS(X) STUDIO_MOVE_IN_PORTS(X)
#define STUDIO_MOVE_OBJECTS_OUTPUTS(X) STUDIO_THEN_OUT_PORTS(X)
#define STUDIO_SET_FILTER_NAME "Set Object Filter"
#define STUDIO_SET_FILTER_INPUTS(X) STUDIO_FILTER_IN_PORTS(X)
#define STUDIO_SET_FILTER_OUTPUTS(X) STUDIO_THEN_OUT_PORTS(X)
#define STUDIO_TOGGLE_SPAWN_BREAK_NAME "Toggle Spawn Break"
#define STUDIO_TOGGLE_SPAWN_BREAK_INPUTS(X) STUDIO_PROTOTYPE_EXEC_IN_PORTS(X)
#define STUDIO_TOGGLE_SPAWN_BREAK_OUTPUTS(X) STUDIO_THEN_OUT_PORTS(X)
#define STUDIO_OPERATIONS(X)                                                   \
    X(SCENE_OBJECTS,                                                           \
      STUDIO_NODE_SCENE_OBJECTS,                                               \
      STUDIO_SCENE_OBJECTS_NAME,                                               \
      "scene_objects",                                                         \
      SCRIPT_CAPABILITY_ACTION,                                                \
      false,                                                                   \
      false,                                                                   \
      2)                                                                       \
    X(SCENE_INFO,                                                              \
      STUDIO_NODE_SCENE_INFO,                                                  \
      STUDIO_SCENE_INFO_NAME,                                                  \
      "scene_info",                                                            \
      SCRIPT_CAPABILITY_FUNCTION,                                              \
      true,                                                                    \
      false,                                                                   \
      3)                                                                       \
    X(SELECTION,                                                               \
      STUDIO_NODE_SELECTION,                                                   \
      STUDIO_SELECTION_NAME,                                                   \
      "selection",                                                             \
      SCRIPT_CAPABILITY_FUNCTION,                                              \
      true,                                                                    \
      false,                                                                   \
      4)                                                                       \
    X(OBJECT_SELECTED,                                                         \
      STUDIO_NODE_OBJECT_SELECTED,                                             \
      STUDIO_OBJECT_SELECTED_NAME,                                             \
      "object_selected",                                                       \
      SCRIPT_CAPABILITY_FUNCTION,                                              \
      true,                                                                    \
      false,                                                                   \
      1)                                                                       \
    X(OBJECT_ROW,                                                              \
      STUDIO_NODE_OBJECT_ROW,                                                  \
      STUDIO_OBJECT_ROW_NAME,                                                  \
      "object_row",                                                            \
      SCRIPT_CAPABILITY_FUNCTION,                                              \
      true,                                                                    \
      false,                                                                   \
      1)                                                                       \
    X(RENAMING,                                                                \
      STUDIO_NODE_RENAMING,                                                    \
      STUDIO_RENAMING_NAME,                                                    \
      "renaming_object",                                                       \
      SCRIPT_CAPABILITY_FUNCTION,                                              \
      true,                                                                    \
      false,                                                                   \
      1)                                                                       \
    X(EDITOR_STATE,                                                            \
      STUDIO_NODE_EDITOR_STATE,                                                \
      STUDIO_EDITOR_STATE_NAME,                                                \
      "editor_state",                                                          \
      SCRIPT_CAPABILITY_FUNCTION,                                              \
      true,                                                                    \
      false,                                                                   \
      3)                                                                       \
    X(SPAWN_BREAK,                                                             \
      STUDIO_NODE_SPAWN_BREAK,                                                 \
      STUDIO_SPAWN_BREAK_NAME,                                                 \
      "breaks_when_spawned",                                                   \
      SCRIPT_CAPABILITY_FUNCTION,                                              \
      true,                                                                    \
      false,                                                                   \
      1)                                                                       \
    X(PROTOTYPE_CHOICES,                                                       \
      STUDIO_NODE_PROTOTYPE_CHOICES,                                           \
      STUDIO_PROTOTYPE_CHOICES_NAME,                                           \
      "prototype_choices",                                                     \
      SCRIPT_CAPABILITY_ACTION,                                                \
      false,                                                                   \
      false,                                                                   \
      1)                                                                       \
    X(SELECT_OBJECT,                                                           \
      STUDIO_NODE_SELECT_OBJECT,                                               \
      STUDIO_SELECT_OBJECT_NAME,                                               \
      "select_object",                                                         \
      SCRIPT_CAPABILITY_ACTION,                                                \
      false,                                                                   \
      true,                                                                    \
      0)                                                                       \
    X(RENAME_OBJECT,                                                           \
      STUDIO_NODE_RENAME_OBJECT,                                               \
      STUDIO_RENAME_OBJECT_NAME,                                               \
      "rename_object",                                                         \
      SCRIPT_CAPABILITY_ACTION,                                                \
      false,                                                                   \
      true,                                                                    \
      0)                                                                       \
    X(END_RENAME,                                                              \
      STUDIO_NODE_END_RENAME,                                                  \
      STUDIO_END_RENAME_NAME,                                                  \
      "end_rename",                                                            \
      SCRIPT_CAPABILITY_ACTION,                                                \
      false,                                                                   \
      true,                                                                    \
      0)                                                                       \
    X(SET_HIDDEN,                                                              \
      STUDIO_NODE_SET_HIDDEN,                                                  \
      STUDIO_SET_HIDDEN_NAME,                                                  \
      "set_object_hidden",                                                     \
      SCRIPT_CAPABILITY_ACTION,                                                \
      false,                                                                   \
      true,                                                                    \
      0)                                                                       \
    X(SET_LOCKED,                                                              \
      STUDIO_NODE_SET_LOCKED,                                                  \
      STUDIO_SET_LOCKED_NAME,                                                  \
      "set_object_locked",                                                     \
      SCRIPT_CAPABILITY_ACTION,                                                \
      false,                                                                   \
      true,                                                                    \
      0)                                                                       \
    X(ISOLATE_OBJECT,                                                          \
      STUDIO_NODE_ISOLATE,                                                     \
      STUDIO_ISOLATE_OBJECT_NAME,                                              \
      "isolate_object",                                                        \
      SCRIPT_CAPABILITY_ACTION,                                                \
      false,                                                                   \
      true,                                                                    \
      0)                                                                       \
    X(ADD_OBJECT,                                                              \
      STUDIO_NODE_ADD_OBJECT,                                                  \
      STUDIO_ADD_OBJECT_NAME,                                                  \
      "add_object",                                                            \
      SCRIPT_CAPABILITY_ACTION,                                                \
      false,                                                                   \
      true,                                                                    \
      0)                                                                       \
    X(DROP_ON_OBJECT,                                                          \
      STUDIO_NODE_DROP_ON_OBJECT,                                              \
      STUDIO_DROP_ON_OBJECT_NAME,                                              \
      "drop_on_object",                                                        \
      SCRIPT_CAPABILITY_ACTION,                                                \
      false,                                                                   \
      true,                                                                    \
      0)                                                                       \
    X(MOVE_OBJECTS,                                                            \
      STUDIO_NODE_MOVE_OBJECTS,                                                \
      STUDIO_MOVE_OBJECTS_NAME,                                                \
      "move_objects",                                                          \
      SCRIPT_CAPABILITY_ACTION,                                                \
      false,                                                                   \
      true,                                                                    \
      0)                                                                       \
    X(SET_FILTER,                                                              \
      STUDIO_NODE_SET_FILTER,                                                  \
      STUDIO_SET_FILTER_NAME,                                                  \
      "set_object_filter",                                                     \
      SCRIPT_CAPABILITY_ACTION,                                                \
      false,                                                                   \
      true,                                                                    \
      0)                                                                       \
    X(TOGGLE_SPAWN_BREAK,                                                      \
      STUDIO_NODE_TOGGLE_SPAWN_BREAK,                                          \
      STUDIO_TOGGLE_SPAWN_BREAK_NAME,                                          \
      "toggle_spawn_break",                                                    \
      SCRIPT_CAPABILITY_ACTION,                                                \
      false,                                                                   \
      true,                                                                    \
      0)
extern const native_definition_pack STUDIO_DEFINITION_PACK;
extern const native_type_definition STUDIO_TYPE_DEFINITIONS[2];
extern const type_desc STUDIO_OBJECT_ROW_LAYOUT, STUDIO_CHOICE_LAYOUT;
extern const node_host_call STUDIO_CALLS[STUDIO_NODE_COUNT];
extern const node_access STUDIO_READ_ACCESS, STUDIO_WRITE_ACCESS;
#endif
