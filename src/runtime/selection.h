#ifndef CHIBI_NATIVE_SELECTION_H
#define CHIBI_NATIVE_SELECTION_H

#include "runtime/selection_definition.h"

/* Graph evaluation may borrow the chosen input for the current evaluation.
 * The execution callback snapshots it into activation-owned storage. */
pin_value native_select_value(const pin_value *, int, arena *);
extern const node_host_call NATIVE_SELECT_CALL;
extern const node_access NATIVE_SELECTION_ACCESS;
int native_switch_index(int selection, int outcome_count);
int native_switch_text(const char *selection, const uint64_t *outcomes, int outcome_count);
extern const node_host_call NATIVE_SWITCH_INT_CALL, NATIVE_SWITCH_TEXT_CALL;

#endif
