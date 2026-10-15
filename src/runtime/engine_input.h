#ifndef CHIBI_ENGINE_INPUT_H
#define CHIBI_ENGINE_INPUT_H

#include "ecs/ecs.h"
#include "execution/arguments.h"
#include "execution/host.h"
#include "runtime/components.h"
#include "runtime/input_action.h"
#include "runtime/input_timeline.h"

/* Private engine input snapshot, local ownership and transition queue. */
typedef struct engine_input engine_input;
/* The record is copied; context remains borrowed until input is freed. Hooks
 * preserve the existing event delivery boundaries. Negative results
 * may write an owned error message into the supplied buffer. */
typedef struct
{
    void *context;
    bool (*has_event)(void *, const char *);
    int (*dispatch)(void *, ecs_entity, const char *, execution_arguments, char *, size_t);
} engine_input_host;
engine_input *engine_input_new(ecs_world *world, const engine_input_host *);
void engine_input_free(engine_input *input);
/* Copies the project's input map, which controls hold each action. Each
 * action is listed once. */
void engine_input_set_actions(engine_input *input, const input_action *actions, int count);
void engine_input_clear(engine_input *input);
void engine_input_reset(engine_input *input);
int engine_input_action(engine_input *input, const char *action, bool pressed);
void engine_input_movement(engine_input *input, CF_V2 movement);
void engine_input_aim(engine_input *input, CF_V2 aim, bool firing, bool point);
int engine_input_controls(engine_input *input, uint64_t controls, bool *firing);
int engine_input_apply(engine_input *input);
const char *engine_input_error(const engine_input *input);
/* The controls held after each change since the last step, oldest first;
 * the last entry is the current controls. Several entries mean an action
 * may have gone down and up between steps. */
int engine_input_trail(const engine_input *input, const uint64_t **controls);
/* The held input as a key at step zero. */
input_keyframe engine_input_held(const engine_input *input);
/* Hold a key's input as the host samples it: controls, movement, then aim. */
int engine_input_hold(engine_input *input, const input_keyframe *key);

/* Action Held and Action Pressed read every source of an action, as combined at
 * the start of the fixed step. Without the service they are false. */
#define INPUT_SERVICE "game.input"
extern const node_host_call INPUT_HELD_CALL;
extern const node_host_call INPUT_PRESSED_CALL;
extern const node_access INPUT_READ_ACCESS;

#endif
