#ifndef CHIBI_EXECUTION_ARGUMENTS_H
#define CHIBI_EXECUTION_ARGUMENTS_H

#include "types/value.h"

/* Borrowed typed event/call payload. Containers obey the receiving contract. */
typedef struct
{
    const pin_value *values;
    int count;
} execution_arguments;

#endif
