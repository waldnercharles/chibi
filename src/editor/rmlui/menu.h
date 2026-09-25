#ifndef CHIBI_EDITOR_RMLUI_MENU_H
#define CHIBI_EDITOR_RMLUI_MENU_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct editor_rml_menu editor_rml_menu;

    typedef enum editor_rml_menu_pane
    {
        RML_INPUT_MENU_PANE_INSPECTOR,
        RML_INPUT_MENU_PANE_NODE_CANVAS,
        RML_INPUT_MENU_PANE_SCENE,
        RML_INPUT_MENU_PANE_COMPILER_RESULTS,
        RML_INPUT_MENU_PANE_OUTPUT_LOG,
        RML_INPUT_MENU_PANE_PERFORMANCE,
        RML_INPUT_MENU_PANE_COUNT,
    } editor_rml_menu_pane;

    typedef struct editor_rml_menu_state
    {
        bool commands_available;
        bool design_preview;
        bool can_undo;
        bool can_redo;
        bool navigator_visible;
        bool panes[RML_INPUT_MENU_PANE_COUNT];
        bool reduced_motion;
        bool mac_shortcuts;
    } editor_rml_menu_state;

    typedef struct editor_rml_menu_rect
    {
        float x;
        float y;
        float width;
        float height;
    } editor_rml_menu_rect;

    /* The callback receives one stable semantic action ID during Rml event dispatch.
     * Queue application work there; execute it after input processing returns. */
    typedef void (*editor_rml_menu_action_fn)(void *user, const char *semantic_id);

    /* context is a borrowed Rml::Context* passed as an opaque pointer. */
    editor_rml_menu *editor_rml_menu_create(void *context, editor_rml_menu_action_fn action, void *user);
    void editor_rml_menu_destroy(editor_rml_menu *menu);
    void editor_rml_menu_set_state(editor_rml_menu *menu, const editor_rml_menu_state *state);
    void editor_rml_menu_close(editor_rml_menu *menu);
    /* Enter keyboard navigation at the current trigger, or File when focus is elsewhere. */
    void editor_rml_menu_focus_bar(editor_rml_menu *menu);
    bool editor_rml_menu_is_open(const editor_rml_menu *menu);
    /* True while a menu is open or Rml focus remains in a bar trigger/popup. */
    bool editor_rml_menu_captures_keyboard(const editor_rml_menu *menu);
    float editor_rml_menu_bar_height(const editor_rml_menu *menu);
    /* Visible elements are queried by existing semantic IDs, e.g. "file.save".
     * Rectangles are in the Rml context's logical coordinates from its latest Update. */
    bool editor_rml_menu_item_rect(
        const editor_rml_menu *menu,
        const char *semantic_id,
        editor_rml_menu_rect *out
    );
    /* Returns the current open menu popup bounds, false when no menu is open. */
    bool editor_rml_menu_popup_rect(const editor_rml_menu *menu, editor_rml_menu_rect *out);

#ifdef __cplusplus
}
#endif

#endif
