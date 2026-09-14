#ifndef CHIBI_ACTIVATION_H
#define CHIBI_ACTIVATION_H
#include "graph/model/node.h"
#include "graph/runtime/value_array.h"
#include "support/arena.h"

/* Backend-neutral continuation state. Managed iteration snapshots belong to
 * the activation and are released on completion, return, failure or cancel. */
typedef enum
{
    ACTIVATION_BLOCK,
    ACTIVATION_LOOP,
    ACTIVATION_FOREACH,
    ACTIVATION_WHILE,
} activation_frame_kind;

typedef struct
{
    int block;  /* compiled continuation PC; -1 for the explicit oracle */
    node *node; /* oracle only; compiled source nodes come from the source map */
    activation_frame_kind kind;
    int current;
    int last;
    int input_pin;
    int body_output;
    int value_output;
    int completed_output;
    int index_output;
    int break_requested;
    pin_array *array;
    arena array_storage;
} activation_frame;

#endif
