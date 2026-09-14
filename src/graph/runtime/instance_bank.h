#ifndef CHIBI_INSTANCE_BANK_H
#define CHIBI_INSTANCE_BANK_H

#include "graph/runtime/instance.h"

/* Allocation internals. Public instance creation/destruction owns initialization
 * and value cleanup; pages only own instance bytes and variable headers. */
behavior_instance *behavior_instance_bank_alloc(behavior_instance_bank **bank, int variables);
pin_value *behavior_instance_bank_values(behavior_instance *instance, int count);
void behavior_instance_bank_release(behavior_instance *instance);

#endif
