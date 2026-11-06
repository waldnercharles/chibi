#ifndef CHIBI_EXECUTION_PROGRAM_INSPECTION_H
#define CHIBI_EXECUTION_PROGRAM_INSPECTION_H
#include "execution/program_internal.h"
void program_inspection_bind(execution_activation *);
void program_inspection_finish(execution_activation *);
void program_inspection_retire(execution_program *);
void program_inspection_release(execution_activation *);
void program_inspection_remove_owner(execution_instance *);
bool program_inspection_paused(const execution_activation *);
#endif
