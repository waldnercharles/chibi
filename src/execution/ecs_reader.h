#ifndef CHIBI_EXECUTION_ECS_READER_H
#define CHIBI_EXECUTION_ECS_READER_H

#include "ecs/ecs.h"
#include "execution/component.h"

/* Values and the direct storage view borrow the world. A null field reads the
 * complete component; dotted paths select reflected nested fields. */
void ecs_component_reader_init(eval_component_reader *, ecs_world *);

#endif
