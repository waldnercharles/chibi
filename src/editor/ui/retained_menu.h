#ifndef CHIBI_EDITOR_RETAINED_MENU_H
#define CHIBI_EDITOR_RETAINED_MENU_H

#include "editor/rmlui/menu.h"
#include "editor/rmlui/toolbar.h"

/* The host adapter owns input, composition and RmlUi lifetime. Commands remain
 * in the C workspace and are consumed after the retained event dispatch. */
void editor_menu_frame(const editor_rml_menu_state *state);
void editor_menu_toolbar_state(const editor_rml_toolbar_state *state, bool visible);
float editor_menu_toolbar_draw(float x, float y, float height);
const char *editor_menu_take_action();
float editor_menu_height();
bool editor_menu_captures_keyboard();
bool editor_menu_item_rect(const char *id, editor_rml_menu_rect *out);
void editor_menu_render();
void editor_menu_shutdown();

#endif
