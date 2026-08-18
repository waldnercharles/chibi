#ifndef CBLUEPRINT_EDITOR_PREVIEW_GATE_H
#define CBLUEPRINT_EDITOR_PREVIEW_GATE_H

#include "../core/flow.h"

/* DBG-6, DBG-10: preview evaluation runs only while no run owns pin storage. */
int editor_preview_allowed(flow_state state);

#endif
