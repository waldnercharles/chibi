#ifndef CHIBI_ENGINE_INPUT_H
#define CHIBI_ENGINE_INPUT_H

#include "runtime/behaviors/behavior.h"
#include "runtime/components.h"

/* Private engine input snapshot, local ownership and transition queue. */
typedef struct engine_input engine_input;
engine_input *engine_input_new(ecs_world *world, game_behavior_world *behaviors);
void engine_input_free(engine_input *input);
void engine_input_clear(engine_input *input);
void engine_input_reset(engine_input *input);
int engine_input_action(engine_input *input, const char *action, bool pressed);
void engine_input_movement(engine_input *input, CF_V2 movement);
void engine_input_aim(engine_input *input, CF_V2 aim, bool firing, bool point);
int engine_input_apply(engine_input *input);

#endif
