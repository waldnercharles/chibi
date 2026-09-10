#ifndef CHIBI_EDITOR_PREVIEW_GATE_H
#define CHIBI_EDITOR_PREVIEW_GATE_H

#include "graph/runtime/flow.h"

/* DBG-6, DBG-10: preview evaluation runs only while no run owns pin storage. */
int editor_preview_allowed(flow_state state);

#endif
