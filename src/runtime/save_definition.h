#ifndef CHIBI_SAVE_DEFINITION_H
#define CHIBI_SAVE_DEFINITION_H
#include "runtime/native_definitions.h"

#define GAME_SAVE_READ_NAME "Read Saved Text"
#define GAME_SAVE_SET_NAME "Set Saved Text"
#define GAME_SAVE_READ_NUMBER_NAME "Read Saved Number"
#define GAME_SAVE_SET_NUMBER_NAME "Set Saved Number"
#define GAME_SAVE_REMOVE_NAME "Remove Saved Text"
#define GAME_SAVE_COMMIT_NAME "Commit Save"
#define GAME_SAVE_DISCARD_NAME "Discard Save Changes"
#define GAME_SAVE_PORT(X, NAME, KIND, STORAGE, TYPE_NAME)                      \
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
#define GAME_SAVE_EXEC(X)                                                      \
    GAME_SAVE_PORT(X, "Exec", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL)
#define GAME_SAVE_KEY(X)                                                       \
    GAME_SAVE_PORT(X, "Key", NATIVE_PARAMETER_DATA, PIN_NAME, NULL)
#define GAME_SAVE_TEXT(X)                                                      \
    GAME_SAVE_PORT(X, "Text", NATIVE_PARAMETER_DATA, PIN_STR, NULL)
#define GAME_SAVE_READ_INPUTS(X)                                               \
    GAME_SAVE_KEY(X)                                                           \
    GAME_SAVE_PORT(X, "Default", NATIVE_PARAMETER_DATA, PIN_STR, NULL)
#define GAME_SAVE_READ_OUTPUTS(X)                                              \
    GAME_SAVE_PORT(X, "Found", NATIVE_PARAMETER_DATA, PIN_BOOL, "bool")        \
    GAME_SAVE_TEXT(X)
#define GAME_SAVE_SET_INPUTS(X)                                                \
    GAME_SAVE_EXEC(X) GAME_SAVE_KEY(X) GAME_SAVE_TEXT(X)
#define GAME_SAVE_NUMBER(X)                                                    \
    GAME_SAVE_PORT(X, "Number", NATIVE_PARAMETER_DATA, PIN_F64, "double")
#define GAME_SAVE_READ_NUMBER_INPUTS(X)                                        \
    GAME_SAVE_KEY(X)                                                           \
    GAME_SAVE_PORT(X, "Default", NATIVE_PARAMETER_DATA, PIN_F64, "double")
#define GAME_SAVE_READ_NUMBER_OUTPUTS(X)                                       \
    GAME_SAVE_PORT(X, "Found", NATIVE_PARAMETER_DATA, PIN_BOOL, "bool")        \
    GAME_SAVE_NUMBER(X)
#define GAME_SAVE_SET_NUMBER_INPUTS(X)                                         \
    GAME_SAVE_EXEC(X) GAME_SAVE_KEY(X) GAME_SAVE_NUMBER(X)
#define GAME_SAVE_REMOVE_INPUTS(X) GAME_SAVE_EXEC(X) GAME_SAVE_KEY(X)
#define GAME_SAVE_THEN(X)                                                      \
    GAME_SAVE_PORT(X, "Then", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL)
#define GAME_SAVE_COMMIT_OUTPUTS(X)                                            \
    GAME_SAVE_THEN(X)                                                          \
    GAME_SAVE_PORT(X, "Failed", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL)
extern const native_definition_pack GAME_SAVE_DEFINITION_PACK;

#define GAME_SAVE_READ_PRESENTATION                                            \
    .category = "Storage", .keywords = "save profile unlock persistent load",  \
    .description = "Reads Key from save data, including uncommitted changes; " \
                   "when missing, Found is false and Text is Default."

#define GAME_SAVE_SET_PRESENTATION                                             \
    .category = "Storage",                                                     \
    .description = "Sets Key to Text in save data; the change persists only "  \
                   "after Commit Save."

#define GAME_SAVE_READ_NUMBER_PRESENTATION                                     \
    .category = "Storage", .keywords = "save profile score persistent load",   \
    .description = "Reads a finite decimal Number, including uncommitted "     \
                   "changes; missing or invalid text returns Found false "     \
                   "and Number Default."

#define GAME_SAVE_SET_NUMBER_PRESENTATION                                      \
    .category = "Storage",                                                     \
    .description = "Sets Key to exact finite decimal Number text; the change " \
                   "persists only after Commit Save."

#define GAME_SAVE_REMOVE_PRESENTATION                                          \
    .category = "Storage",                                                     \
    .description = "Removes Key from save data; the removal persists only "    \
                   "after Commit Save."

#define GAME_SAVE_DISCARD_PRESENTATION                                         \
    .category = "Storage",                                                     \
    .description = "Reverts save data to the last commit, dropping "           \
                   "uncommitted changes."

#define GAME_SAVE_COMMIT_PRESENTATION                                          \
    .category = "Storage", .description = "Writes save data to storage; if "   \
                                          "writing fails, runs Failed "        \
                                          "and keeps the previous save."

#endif
