#ifndef CBLUEPRINT_EDITOR_UI_SHELL_H
#define CBLUEPRINT_EDITOR_UI_SHELL_H

#include <stdbool.h>
#include <stddef.h>

typedef enum editor_ui_asset_kind
{
    EDITOR_UI_ASSET_PROTOTYPE,
    EDITOR_UI_ASSET_BEHAVIOR,
    EDITOR_UI_ASSET_FOLDER,
    EDITOR_UI_ASSET_FILE,
} editor_ui_asset_kind;

typedef enum editor_ui_asset_selection
{
    EDITOR_UI_ASSET_NOT_SELECTED,
    EDITOR_UI_ASSET_SELECTED,
    EDITOR_UI_ASSET_ACTIVE,
} editor_ui_asset_selection;

typedef enum editor_ui_focus_target
{
    EDITOR_UI_FOCUS_NONE,
    EDITOR_UI_FOCUS_SCENE,
    EDITOR_UI_FOCUS_NODE_CANVAS,
    EDITOR_UI_FOCUS_CONTENT_DRAWER,
} editor_ui_focus_target;

#define EDITOR_UI_ASSET_CARD_WIDTH 148.0f
#define EDITOR_UI_ASSET_CARD_HEIGHT 148.0f

/* Pending until the window takes it immediately before Begin. NONE clears. */
void editor_ui_request_focus(editor_ui_focus_target target);
bool editor_ui_take_focus(editor_ui_focus_target target);

bool editor_ui_content_drawer_open(void);
void editor_ui_set_content_drawer_open(bool open);

void editor_ui_prepare(void);
bool editor_ui_section_header(const char *label, const char *action_id);
bool editor_ui_asset_row(
    const char *id,
    const char *name,
    const char *detail,
    editor_ui_asset_kind kind,
    editor_ui_asset_selection selection
);
bool editor_ui_asset_card(
    const char *id,
    const char *name,
    editor_ui_asset_kind kind,
    editor_ui_asset_selection selection
);

bool editor_ui_icon_button(const char *id, int glyph, bool enabled);
bool editor_ui_right_icon_button(const char *id, int glyph);
void editor_ui_breadcrumb_separator(int glyph);
bool editor_ui_link(const char *id, const char *label);
bool editor_ui_search_field(const char *id, char *buffer, size_t buffer_size, const char *hint);

#endif
