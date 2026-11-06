#include "assets/ui_events.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What the player does: "presses Escape inside Dialog in Settings", "clicks
 * #go in Hud" or "changes #volume in Options". */
static void clause_of(const ui_document_event *found, const char *document, char *out, size_t size)
{
    if (found->key) {
        snprintf(out, size, "presses %s inside %s in %s", found->key, found->element, document);
        return;
    }
    snprintf(
        out,
        size,
        "%s %s in %s",
        found->input ? "changes" : "clicks",
        found->element,
        document
    );
}

void ui_events_add(ui_event **events, const ui_document_event *found, const char *document)
{
    char clause[192];
    clause_of(found, document, clause, sizeof clause);
    for (int i = 0; i < acount(*events); i++) {
        ui_event *event = &(*events)[i];
        if (strcmp(event->name, found->name)) {
            continue;
        }
        size_t used = strlen(event->description);
        if (used + strlen(clause) + 6 < sizeof event->description) {
            snprintf(event->description + used, sizeof event->description - used, ", or %s", clause);
        }
        return;
    }
    ui_event event = { .name = xstrdup(found->name),
                       .keywords = found->key   ? "UI key keyboard"
                                 : found->input ? "UI input widget"
                                                : "UI button widget" };
    snprintf(event.description, sizeof event.description, "Sent when the player %s", clause);
    apush(*events, event);
}

void ui_events_free(ui_event *events)
{
    for (int i = 0; i < acount(events); i++) {
        free(events[i].name);
    }
    afree(events);
}
