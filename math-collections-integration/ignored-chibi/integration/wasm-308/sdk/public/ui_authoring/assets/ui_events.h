#ifndef CHIBI_UI_EVENTS_H
#define CHIBI_UI_EVENTS_H

#include "assets/ui_document.h"

/* An event ui documents send the Ui entity showing them, words to find
 * it by, and when, without the period: "Sent when the player clicks #go in
 * Hud". Native search_nodes and Studio's Add menu offer these as Event
 * nodes. */
typedef struct ui_event
{
    char *name;
    const char *keywords;
    char description[256];
} ui_event;

/* A value a ui document binds under self, which Set UI Data gives it. */
typedef struct ui_data_binding
{
    ui_document_binding binding;
    char document[128];
} ui_data_binding;

/* Adds what the player does in `document` to send `found` to its event's
 * sentence while it fits, starting one for a new name (support/array.h). */
void ui_events_add(ui_event **events, const ui_document_event *found, const char *document);
void ui_events_free(ui_event *events);

#endif
