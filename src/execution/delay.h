#ifndef CHIBI_EXECUTION_DELAY_H
#define CHIBI_EXECUTION_DELAY_H
#include "execution/host.h"
/* One double Duration argument, no payload results. Completion is scheduled
 * by the activation owner independently of sibling commands. */
#define EXECUTION_DELAY_ID UINT64_C(0x4348494900000101)
#define EXECUTION_DELAY_ABI UINT64_C(1)
extern const node_command EXECUTION_Delay;
#endif
