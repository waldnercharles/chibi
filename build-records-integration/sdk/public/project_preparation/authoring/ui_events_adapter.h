#ifndef CHIBI_AUTHORING_UI_EVENTS_ADAPTER_H
#define CHIBI_AUTHORING_UI_EVENTS_ADAPTER_H
#include "render/ui.h"
struct preview_world;
/* Borrowed world; the copied hooks may outlive individual source generations. */
render_ui_host authoring_ui_events(struct preview_world *);
#endif
