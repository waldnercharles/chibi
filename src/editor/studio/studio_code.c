/* The Text Editor area (Blender's): the source of the active UI document,
 * its RML and RCSS, or of a stylesheet. */
#include "editor/studio/studio_internal.h"

void studio_code_header(studio *s, int area)
{
    (void)area;
    kit_label(s->kit, "title", "dim", "No UI document open");
}

void studio_code_body(studio *s, int area)
{
    (void)area;
    kit *k = s->kit;
    kit_push(k, "empty", NULL, "pending-editor");
    kit_icon(k, "icon", NULL, KIT_ICON_EXPRESSION);
    kit_label(k, "name", NULL, "Open a UI asset or stylesheet from Assets");
    kit_pop(k);
}

bool studio_open_ui_source(studio *s, const char *path, int line)
{
    (void)s;
    (void)path;
    (void)line;
    return false;
}
