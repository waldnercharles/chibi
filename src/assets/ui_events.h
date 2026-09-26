#ifndef CHIBI_UI_EVENTS_H
#define CHIBI_UI_EVENTS_H

#include "assets/ui_document.h"

/* An event ui documents send the GameUi entity showing them, words to find
 * it by, and when, without the period: "Sent when the player clicks #go in
 * Hud". The MCP's searchNodes and Studio's Add menu offer these as Event
 * nodes. */
typedef struct ui_event
{
    char *name;
    const char *keywords;
    char description[256];
} ui_event;

/* Appends the events a ui asset's text sends (ui_document_events); an
 * invalid asset sends none until it reads. */
void ui_events_read(const char *text, ui_document_event **found);
/* Adds what the player does in `document` to send `found` to its event's
 * sentence while it fits, starting one for a new name (support/array.h). */
void ui_events_add(ui_event **events, const ui_document_event *found, const char *document);
void ui_events_free(ui_event *events);

#endif
