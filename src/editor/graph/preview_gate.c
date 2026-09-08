#include "editor/graph/preview_gate.h"

int editor_preview_allowed(flow_state state)
{
    return state == FLOW_IDLE;
}
