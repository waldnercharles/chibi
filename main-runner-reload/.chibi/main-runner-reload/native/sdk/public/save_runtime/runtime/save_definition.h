#ifndef CHIBI_SAVE_DEFINITION_H
#define CHIBI_SAVE_DEFINITION_H
#include "runtime/native_definitions.h"

#define SAVE_READ_NAME "Read Saved Text"
#define SAVE_SET_NAME "Set Saved Text"
#define SAVE_READ_NUMBER_NAME "Read Saved Number"
#define SAVE_SET_NUMBER_NAME "Set Saved Number"
#define SAVE_REMOVE_NAME "Remove Saved Text"
#define SAVE_COMMIT_NAME "Commit Save"
#define SAVE_DISCARD_NAME "Discard Save Changes"
#define SAVE_PORT(X, NAME, KIND, STORAGE, TYPE_NAME)                           \
    X(NAME,                                                                    \
      KIND,                                                                    \
      STORAGE,                                                                 \
      TYPE_NAME,                                                               \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = STORAGE }))
#define SAVE_EXEC(X) SAVE_PORT(X, "Exec", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL)
#define SAVE_KEY(X) SAVE_PORT(X, "Key", NATIVE_PARAMETER_DATA, PIN_NAME, NULL)
#define SAVE_TEXT(X) SAVE_PORT(X, "Text", NATIVE_PARAMETER_DATA, PIN_STR, NULL)
#define SAVE_READ_INPUTS(X)                                                    \
    SAVE_KEY(X)                                                                \
    SAVE_PORT(X, "Default", NATIVE_PARAMETER_DATA, PIN_STR, NULL)
#define SAVE_READ_OUTPUTS(X)                                                   \
    SAVE_PORT(X, "Found", NATIVE_PARAMETER_DATA, PIN_BOOL, "bool")             \
    SAVE_TEXT(X)
#define SAVE_SET_INPUTS(X) SAVE_EXEC(X) SAVE_KEY(X) SAVE_TEXT(X)
#define SAVE_NUMBER(X)                                                         \
    SAVE_PORT(X, "Number", NATIVE_PARAMETER_DATA, PIN_F32, "number")
#define SAVE_READ_NUMBER_INPUTS(X)                                             \
    SAVE_KEY(X)                                                                \
    SAVE_PORT(X, "Default", NATIVE_PARAMETER_DATA, PIN_F32, "number")
#define SAVE_READ_NUMBER_OUTPUTS(X)                                            \
    SAVE_PORT(X, "Found", NATIVE_PARAMETER_DATA, PIN_BOOL, "bool")             \
    SAVE_NUMBER(X)
#define SAVE_SET_NUMBER_INPUTS(X) SAVE_EXEC(X) SAVE_KEY(X) SAVE_NUMBER(X)
#define SAVE_REMOVE_INPUTS(X) SAVE_EXEC(X) SAVE_KEY(X)
#define SAVE_THEN(X) SAVE_PORT(X, "Then", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL)
#define SAVE_COMMIT_OUTPUTS(X)                                                 \
    SAVE_THEN(X)                                                               \
    SAVE_PORT(X, "Failed", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL)
extern const native_definition_pack SAVE_DEFINITION_PACK;

#define SAVE_READ_PRESENTATION                                                 \
    .category = "Storage", .keywords = "save profile unlock persistent load",  \
    .description = "Reads Key from save data, including uncommitted changes; " \
                   "when missing, Found is false and Text is Default."

#define SAVE_SET_PRESENTATION                                                  \
    .category = "Storage",                                                     \
    .description = "Sets Key to Text in save data; the change persists only "  \
                   "after Commit Save."

#define SAVE_READ_NUMBER_PRESENTATION                                          \
    .category = "Storage", .keywords = "save profile score persistent load",   \
    .description = "Reads a finite decimal Number, including uncommitted "     \
                   "changes; missing or invalid text returns Found false "     \
                   "and Number Default."

#define SAVE_SET_NUMBER_PRESENTATION                                           \
    .category = "Storage",                                                     \
    .description = "Sets Key to exact finite decimal Number text; the change " \
                   "persists only after Commit Save."

#define SAVE_REMOVE_PRESENTATION                                               \
    .category = "Storage",                                                     \
    .description = "Removes Key from save data; the removal persists only "    \
                   "after Commit Save."

#define SAVE_DISCARD_PRESENTATION                                              \
    .category = "Storage",                                                     \
    .description = "Reverts save data to the last commit, dropping "           \
                   "uncommitted changes."

#define SAVE_COMMIT_PRESENTATION                                               \
    .category = "Storage", .description = "Writes save data to storage; if "   \
                                          "writing fails, runs Failed "        \
                                          "and keeps the previous save."

#endif
