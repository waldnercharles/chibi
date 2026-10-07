#ifndef CHIBI_CODE_REACTIVE_H
#define CHIBI_CODE_REACTIVE_H
#include "assets/code_program_internal.h"

/* bind is consumed by the caller; event probing starts after on. */
bool code_reactive_parse_bind(parser *p);
bool code_reactive_parse_event(parser *p);
void code_reactive_finish(parser *p);
#endif
