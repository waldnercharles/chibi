#ifndef CHIBI_OPERATOR_H
#define CHIBI_OPERATOR_H

#include "graph/model/pin.h"

typedef struct operator_signature
{
    const char *name;
    pin_type left;
    pin_type right;
    pin_type result;
    int priority;
} operator_signature;

int operator_register(const operator_signature *signature);
const operator_signature *operator_resolve(const char *name, pin_type left, pin_type right);
const operator_signature *operator_signatures(int *count);

#endif
