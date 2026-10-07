#ifndef CHIBI_UI_IFRAME_PROFILE_H
#define CHIBI_UI_IFRAME_PROFILE_H

/* Opt-in, single-threaded diagnostic for document update fixtures.
 * Timings include instrumentation; probe callbacks cover only documents
 * containing the fixture's custom element, not every RmlUi element. */
#include "ui/rml.h"

#include <stdbool.h>

typedef enum iframe_profile_phase
{
    IFRAME_PROFILE_OUTSIDE,
    IFRAME_PROFILE_PRE,
    IFRAME_PROFILE_CONTEXT,
    IFRAME_PROFILE_POST,
    IFRAME_PROFILE_PHASES,
} iframe_profile_phase;

typedef struct iframe_profile_sample
{
    int calls, holder_updates, boxes, heights, passes;
    double seconds, holder_seconds, started;
    int context_calls;
    double context_seconds[RML_UPDATE_PARTS];
} iframe_profile_sample;

void iframe_profile_enable(bool enabled);
void iframe_profile_begin(iframe_profile_phase phase);
void iframe_profile_end();
iframe_profile_phase iframe_profile_current();
iframe_profile_sample iframe_profile_get(iframe_profile_phase phase);
void iframe_profile_context_add(const double seconds[RML_UPDATE_PARTS]);

#endif
