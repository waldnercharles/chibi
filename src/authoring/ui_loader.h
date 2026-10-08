#pragma once
#include "render/ui.h"
#include "render/viewport.h"
#include "support/file_watch.h"
extern const render_ui_loader AUTHORING_UI_LOADER;
ui_mount *authoring_ui_mount_new(const char *content_root, const char *name);
bool authoring_ui_page_sync(ui_page *, const char *content_root, const char *name, double time);
render_ui *authoring_ui_new(const char *content_root, yyjson_mut_doc *data);

void authoring_viewport_set_ui(engine_viewport *, const ui_world *, const char *content_root);

/* Borrowed authoring dependency vector until the next page sync/free. */
const file_watch *authoring_ui_page_files(const ui_page *);
