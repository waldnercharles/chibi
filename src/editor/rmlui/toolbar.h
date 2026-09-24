#ifndef CHIBI_EDITOR_RMLUI_TOOLBAR_H
#define CHIBI_EDITOR_RMLUI_TOOLBAR_H

#include "menu.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct editor_rml_toolbar editor_rml_toolbar;

    typedef struct editor_rml_toolbar_state
    {
        bool back;
        bool forward;
        bool commands_available;
        bool reduced_motion;
    } editor_rml_toolbar_state;

    editor_rml_toolbar *editor_rml_toolbar_create(
        void *context,
        editor_rml_menu_action_fn action,
        void *user
    );
    void editor_rml_toolbar_destroy(editor_rml_toolbar *toolbar);
    void editor_rml_toolbar_set_state(editor_rml_toolbar *toolbar, const editor_rml_toolbar_state *state);
    void editor_rml_toolbar_set_bounds(editor_rml_toolbar *toolbar, editor_rml_menu_rect bounds);
    void editor_rml_toolbar_hide(editor_rml_toolbar *toolbar);
    void editor_rml_toolbar_focus(editor_rml_toolbar *toolbar);
    void editor_rml_toolbar_blur(editor_rml_toolbar *toolbar);
    bool editor_rml_toolbar_captures_keyboard(const editor_rml_toolbar *toolbar);
    bool editor_rml_toolbar_item_rect(
        const editor_rml_toolbar *toolbar,
        const char *semantic_id,
        editor_rml_menu_rect *out
    );
    float editor_rml_toolbar_preferred_width(const editor_rml_toolbar *toolbar);

#ifdef __cplusplus
}
#endif

#endif
