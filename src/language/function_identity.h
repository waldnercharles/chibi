#ifndef CHIBI_LANGUAGE_FUNCTION_IDENTITY_H
#define CHIBI_LANGUAGE_FUNCTION_IDENTITY_H
#include "language/semantic.h"
/* Same canonical identity used by lowering, source inspection and debugging. */
uint64_t script_function_identity(const script_analysis *, int function);
#endif
