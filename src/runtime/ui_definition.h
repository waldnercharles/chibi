#ifndef CHIBI_GAME_UI_DEFINITION_H
#define CHIBI_GAME_UI_DEFINITION_H
#include "runtime/native_definitions.h"
#define GAME_UI_PORT(X, NAME, KIND, STORAGE, NOMINAL, FLAGS, OWNER, VALUE)     \
    X(NAME, KIND, STORAGE, NOMINAL, PIN_CONTAINER_SINGLE, FLAGS, 0, SCRIPT_REFERENCE_NONE, OWNER, VALUE)
#define GAME_UI_DATA(X, NAME, STORAGE, NOMINAL)                                \
    GAME_UI_PORT(X, NAME, NATIVE_PARAMETER_DATA, STORAGE, NOMINAL, 0, false, ((pin_value){ 0 }))
#define GAME_UI_EXEC(X, NAME)                                                  \
    GAME_UI_PORT(X, NAME, NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL, 0, false, ((pin_value){ 0 }))
#define GAME_UI_ENTITY(X)                                                      \
    GAME_UI_PORT(X, "Entity", NATIVE_PARAMETER_DATA, PIN_HANDLE, "ecs.entity", 0, true, ((pin_value){ 0 }))
#define GAME_UI_GET_INPUTS(X)                                                  \
    GAME_UI_ENTITY(X)                                                          \
    GAME_UI_DATA(X, "Element", PIN_STR, "const char *")
#define GAME_UI_BASE_INPUTS(X)                                                 \
    GAME_UI_EXEC(X, "Exec")                                                    \
    GAME_UI_GET_INPUTS(X)
#define GAME_UI_TEXT_INPUTS(X)                                                 \
    GAME_UI_BASE_INPUTS(X)                                                     \
    GAME_UI_DATA(X, "Text", PIN_STR, "const char *")
#define GAME_UI_CLASS_INPUTS(X)                                                \
    GAME_UI_BASE_INPUTS(X)                                                     \
    GAME_UI_DATA(X, "Class", PIN_NAME, "name")                                 \
    GAME_UI_PORT(                                                              \
        X,                                                                     \
        "On",                                                                  \
        NATIVE_PARAMETER_DATA,                                                 \
        PIN_BOOL,                                                              \
        "bool",                                                                \
        0,                                                                     \
        false,                                                                 \
        ((pin_value){ .type = PIN_BOOL, .b = true })                           \
    )
#define GAME_UI_STYLE_INPUTS(X)                                                \
    GAME_UI_BASE_INPUTS(X)                                                     \
    GAME_UI_DATA(X, "Property", PIN_NAME, "name")                              \
    GAME_UI_DATA(X, "Value", PIN_STR, "const char *")
#define GAME_UI_VISIBLE_INPUTS(X)                                              \
    GAME_UI_BASE_INPUTS(X)                                                     \
    GAME_UI_PORT(                                                              \
        X,                                                                     \
        "Visible",                                                             \
        NATIVE_PARAMETER_DATA,                                                 \
        PIN_BOOL,                                                              \
        "bool",                                                                \
        0,                                                                     \
        false,                                                                 \
        ((pin_value){ .type = PIN_BOOL, .b = true })                           \
    )
#define GAME_UI_VALUE_INPUTS(X)                                                \
    GAME_UI_BASE_INPUTS(X)                                                     \
    GAME_UI_DATA(X, "Value", PIN_F32, "float")
#define GAME_UI_FOCUS_INPUTS(X) GAME_UI_BASE_INPUTS(X)
#define GAME_UI_MOVE_FOCUS_INPUTS(X)                                           \
    GAME_UI_BASE_INPUTS(X)                                                     \
    GAME_UI_PORT(                                                              \
        X,                                                                     \
        "Step",                                                                \
        NATIVE_PARAMETER_DATA,                                                 \
        PIN_I32,                                                               \
        "int",                                                                 \
        0,                                                                     \
        false,                                                                 \
        ((pin_value){ .type = PIN_I32, .i = 1 })                               \
    )
#define GAME_UI_POPOVER_INPUTS(X)                                              \
    GAME_UI_BASE_INPUTS(X)                                                     \
    GAME_UI_DATA(X, "At", PIN_STRUCT, "CF_V2")
#define GAME_UI_REVEAL_INPUTS(X)                                               \
    GAME_UI_BASE_INPUTS(X)                                                     \
    GAME_UI_DATA(X, "Row", PIN_I32, "int")
#define GAME_UI_DATA_INPUTS(X)                                                 \
    GAME_UI_EXEC(X, "Exec")                                                    \
    GAME_UI_ENTITY(X)                                                          \
    GAME_UI_DATA(X, "Name", PIN_NAME, "name")                                  \
    GAME_UI_PORT(                                                              \
        X,                                                                     \
        "Value",                                                               \
        NATIVE_PARAMETER_DATA,                                                 \
        PIN_FLOW,                                                              \
        NULL,                                                                  \
        SCRIPT_TYPE_UNTYPED,                                                   \
        false,                                                                 \
        ((pin_value){ 0 })                                                     \
    )
#define GAME_UI_THEN_OUTPUTS(X) GAME_UI_EXEC(X, "Then")
#define GAME_UI_VALUE_OUTPUTS(X)                                               \
    GAME_UI_DATA(X, "Value", PIN_F32, "float")                                 \
    GAME_UI_DATA(X, "Known", PIN_BOOL, "bool")
#define GAME_UI_SCROLL_OUTPUTS(X)                                              \
    GAME_UI_DATA(X, "Scroll Top", PIN_F32, "float")                            \
    GAME_UI_DATA(X, "View Height", PIN_F32, "float")                           \
    GAME_UI_DATA(X, "Content Height", PIN_F32, "float")                        \
    GAME_UI_DATA(X, "Known", PIN_BOOL, "bool")
#define GAME_UI_SET_TEXT_NAME "Set UI Text"
#define GAME_UI_SET_TEXT_SOURCE "set_ui_text"
#define GAME_UI_SET_CLASS_NAME "Set UI Class"
#define GAME_UI_SET_CLASS_SOURCE "set_ui_class"
#define GAME_UI_SET_STYLE_NAME "Set UI Style"
#define GAME_UI_SET_STYLE_SOURCE "set_ui_style"
#define GAME_UI_SET_VISIBLE_NAME "Set UI Visible"
#define GAME_UI_SET_VISIBLE_SOURCE "set_ui_visible"
#define GAME_UI_SET_VALUE_NAME "Set UI Value"
#define GAME_UI_SET_VALUE_SOURCE "set_ui_value"
#define GAME_UI_FOCUS_NAME "Focus UI Element"
#define GAME_UI_FOCUS_SOURCE "focus_ui_element"
#define GAME_UI_CLICK_NAME "Click UI Element"
#define GAME_UI_CLICK_SOURCE "click_ui_element"
#define GAME_UI_MOVE_FOCUS_NAME "Move UI Focus"
#define GAME_UI_MOVE_FOCUS_SOURCE "move_ui_focus"
#define GAME_UI_SHOW_POPOVER_NAME "Show UI Popover"
#define GAME_UI_SHOW_POPOVER_SOURCE "show_ui_popover"
#define GAME_UI_REVEAL_ROW_NAME "Reveal UI List Row"
#define GAME_UI_REVEAL_ROW_SOURCE "reveal_ui_list_row"
#define GAME_UI_SET_DATA_NAME "Set UI Data"
#define GAME_UI_SET_DATA_SOURCE "set_ui_data"
#define GAME_UI_GET_VALUE_NAME "Get UI Value"
#define GAME_UI_GET_VALUE_SOURCE "get_ui_value"
#define GAME_UI_GET_SCROLL_NAME "Get UI Scroll"
#define GAME_UI_GET_SCROLL_SOURCE "get_ui_scroll"
#define GAME_UI_OPERATIONS(X)                                                                            \
    X(SET_TEXT,                                                                                          \
      GAME_UI_SET_TEXT_NAME,                                                                             \
      GAME_UI_SET_TEXT_SOURCE,                                                                           \
      GAME_UI_TEXT_INPUTS,                                                                               \
      GAME_UI_THEN_OUTPUTS,                                                                              \
      true)                                                                                              \
    X(SET_CLASS,                                                                                         \
      GAME_UI_SET_CLASS_NAME,                                                                            \
      GAME_UI_SET_CLASS_SOURCE,                                                                          \
      GAME_UI_CLASS_INPUTS,                                                                              \
      GAME_UI_THEN_OUTPUTS,                                                                              \
      true)                                                                                              \
    X(SET_STYLE,                                                                                         \
      GAME_UI_SET_STYLE_NAME,                                                                            \
      GAME_UI_SET_STYLE_SOURCE,                                                                          \
      GAME_UI_STYLE_INPUTS,                                                                              \
      GAME_UI_THEN_OUTPUTS,                                                                              \
      true)                                                                                              \
    X(SET_VISIBLE,                                                                                       \
      GAME_UI_SET_VISIBLE_NAME,                                                                          \
      GAME_UI_SET_VISIBLE_SOURCE,                                                                        \
      GAME_UI_VISIBLE_INPUTS,                                                                            \
      GAME_UI_THEN_OUTPUTS,                                                                              \
      true)                                                                                              \
    X(SET_VALUE,                                                                                         \
      GAME_UI_SET_VALUE_NAME,                                                                            \
      GAME_UI_SET_VALUE_SOURCE,                                                                          \
      GAME_UI_VALUE_INPUTS,                                                                              \
      GAME_UI_THEN_OUTPUTS,                                                                              \
      true)                                                                                              \
    X(FOCUS, GAME_UI_FOCUS_NAME, GAME_UI_FOCUS_SOURCE, GAME_UI_FOCUS_INPUTS, GAME_UI_THEN_OUTPUTS, true) \
    X(CLICK, GAME_UI_CLICK_NAME, GAME_UI_CLICK_SOURCE, GAME_UI_FOCUS_INPUTS, GAME_UI_THEN_OUTPUTS, true) \
    X(MOVE_FOCUS,                                                                                        \
      GAME_UI_MOVE_FOCUS_NAME,                                                                           \
      GAME_UI_MOVE_FOCUS_SOURCE,                                                                         \
      GAME_UI_MOVE_FOCUS_INPUTS,                                                                         \
      GAME_UI_THEN_OUTPUTS,                                                                              \
      true)                                                                                              \
    X(SHOW_POPOVER,                                                                                      \
      GAME_UI_SHOW_POPOVER_NAME,                                                                         \
      GAME_UI_SHOW_POPOVER_SOURCE,                                                                       \
      GAME_UI_POPOVER_INPUTS,                                                                            \
      GAME_UI_THEN_OUTPUTS,                                                                              \
      true)                                                                                              \
    X(REVEAL_ROW,                                                                                        \
      GAME_UI_REVEAL_ROW_NAME,                                                                           \
      GAME_UI_REVEAL_ROW_SOURCE,                                                                         \
      GAME_UI_REVEAL_INPUTS,                                                                             \
      GAME_UI_THEN_OUTPUTS,                                                                              \
      true)                                                                                              \
    X(SET_DATA,                                                                                          \
      GAME_UI_SET_DATA_NAME,                                                                             \
      GAME_UI_SET_DATA_SOURCE,                                                                           \
      GAME_UI_DATA_INPUTS,                                                                               \
      GAME_UI_THEN_OUTPUTS,                                                                              \
      true)                                                                                              \
    X(GET_VALUE,                                                                                         \
      GAME_UI_GET_VALUE_NAME,                                                                            \
      GAME_UI_GET_VALUE_SOURCE,                                                                          \
      GAME_UI_GET_INPUTS,                                                                                \
      GAME_UI_VALUE_OUTPUTS,                                                                             \
      false)                                                                                             \
    X(GET_SCROLL,                                                                                        \
      GAME_UI_GET_SCROLL_NAME,                                                                           \
      GAME_UI_GET_SCROLL_SOURCE,                                                                         \
      GAME_UI_GET_INPUTS,                                                                                \
      GAME_UI_SCROLL_OUTPUTS,                                                                            \
      false)
extern const native_definition_pack GAME_UI_DEFINITION_PACK;

/* Shared authoring presentation for these native declarations. */
#define GAME_UI_SET_TEXT_PRESENTATION                                          \
    .category = "UI",                                                          \
    .keywords = "label hud score caption widget element selector",             \
    .description = "Sets the text of the entity's UI elements that match the " \
                   "Element "                                                  \
                   "selector."

#define GAME_UI_SET_CLASS_PRESENTATION                                         \
    .category = "UI",                                                          \
    .keywords = "style state highlight rcss hud widget element selector",      \
    .description = "Adds or removes a class on the entity's UI elements that " \
                   "match the "                                                \
                   "Element selector."

#define GAME_UI_SET_STYLE_PRESENTATION                                         \
    .category = "UI",                                                          \
    .keywords = "css rcss property width height color transform position "     \
                "size inline "                                                 \
                "style widget element selector",                               \
    .description = "Sets an RCSS property, such as width to 40%, on the "      \
                   "entity's UI elements "                                     \
                   "that match the Element selector; an empty Value returns "  \
                   "it to the "                                                \
                   "stylesheets."

#define GAME_UI_SET_VISIBLE_PRESENTATION                                       \
    .category = "UI",                                                          \
    .keywords = "show hide display widget menu hud element selector", .description = "Shows or hides the entity's UI elements that match the Element selector."

#define GAME_UI_SET_VALUE_PRESENTATION                                         \
    .category = "UI",                                                          \
    .keywords = "progress slider meter bar hud widget element selector",       \
    .description = "Sets the value of the entity's sliders, meters and "       \
                   "inputs that match the "                                    \
                   "Element selector."

#define GAME_UI_FOCUS_PRESENTATION                                                \
    .category = "UI", .keywords = "keyboard select input widget button selector", \
    .description = "Gives keyboard focus to the entity's first UI element "       \
                   "that matches the "                                            \
                   "Element selector."

#define GAME_UI_CLICK_PRESENTATION                                                \
    .category = "UI", .keywords = "press activate button choose widget selector", \
    .description = "Clicks the entity's first UI element that matches the "       \
                   "Element selector, "                                           \
                   "as a pointer would."

#define GAME_UI_MOVE_FOCUS_PRESENTATION                                                                 \
    .category = "UI", .keywords = "next previous navigate arrow keyboard menu widget element selector", \
    .description = "Moves keyboard focus Step places through the entity's UI "                          \
                   "elements that "                                                                     \
                   "match the Element selector."

#define GAME_UI_SHOW_POPOVER_PRESENTATION                                                                      \
    .category = "UI", .keywords = "context menu right click popup open pointer point widget element selector", \
    .description = "Opens the entity's first popover that matches the "                                        \
                   "Element selector at "                                                                      \
                   "At, a point in the document's pixels, as a context menu "                                  \
                   "opens at the "                                                                             \
                   "pointer a UI event's Pointer gives."

#define GAME_UI_REVEAL_ROW_PRESENTATION                                        \
    .category = "UI",                                                          \
    .keywords = "scroll into view list virtual window row index show widget "  \
                "element "                                                     \
                "selector",                                                    \
    .description = "Scrolls the least that brings row Row, counted from 0, "   \
                   "of the entity's "                                          \
                   "lists that match the Element selector into view, in the "  \
                   "window their "                                             \
                   "presenter keeps for data-list-rows rows."

#define GAME_UI_SET_DATA_PRESENTATION                                          \
    .category = "UI",                                                          \
    .keywords = "bind binding self list rows table inventory leaderboard "     \
                "data-for record hud widget",                                  \
    .description = "Gives the entity's UI documents Value to bind as "         \
                   "self.<Name>: text, a number, a struct's fields, or a "     \
                   "list "                                                     \
                   "data-for repeats; a path such as items.2.name sets one "   \
                   "row's field."

#define GAME_UI_GET_SCROLL_PRESENTATION                                        \
    .category = "UI",                                                          \
    .keywords = "scroll position offset list virtual window height view "      \
                "content measure element selector",                            \
    .description = "Reads, in dp, how far the element under #id is scrolled "  \
                   "and the heights of its view and its content, as the "      \
                   "presenter last measured an element with "                  \
                   "data-event-scroll; "                                       \
                   "Known is false until it has."

#define GAME_UI_GET_VALUE_PRESENTATION                                         \
    .category = "UI",                                                          \
    .keywords = "slider input progress read widget element selector",          \
    .description = "Reads the value last set under the Element selector, or "  \
                   "a player's input under #id; Known is false until there "   \
                   "is "                                                       \
                   "one."

#endif
