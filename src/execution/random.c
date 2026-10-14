#include "execution/random.h"

#include "execution/function.h"
#include "execution/service.h"
#include "support/array.h"

#include <assert.h>
#include <string.h>

static _Thread_local behavior_random *active;
static _Thread_local behavior_random unbound;
static uint32_t next(behavior_random *random)
{
    uint64_t previous = random->state;
    random->state = previous * UINT64_C(6364136223846793005) + UINT64_C(109);
    uint32_t shifted = (uint32_t)(((previous >> 18) ^ previous) >> 27);
    unsigned rotation = (unsigned)(previous >> 59);
    return (shifted >> rotation) | (shifted << ((0u - rotation) & 31u));
}
void behavior_random_seed(behavior_random *random, unsigned seed)
{
    assert(random);
    *random = (behavior_random){ .initialized = true };
    next(random);
    random->state += seed;
    next(random);
}
uint32_t behavior_random_u32(behavior_random *random)
{
    assert(random);
    if (!random->initialized) {
        behavior_random_seed(random, 1);
    }
    return next(random);
}
behavior_random *behavior_random_enter(behavior_random *random)
{
    assert(random);
    behavior_random *previous = active;
    active = random;
    return previous;
}
void behavior_random_leave(behavior_random *previous)
{
    active = previous;
}
behavior_random *behavior_random_context(behavior_function_context *context)
{
    assert(context);
    for (behavior_function_context *c = context; c; c = c->parent) {
        for (int i = 0; i < acount(c->services); i++) {
            if (c->services[i].value &&
                !strcmp(c->services[i].type_name, BEHAVIOR_RANDOM_SERVICE)) {
                return c->services[i].value;
            }
        }
        if (!c->parent) {
            return &c->random;
        }
    }
    assert(false);
    return NULL;
}
int behavior_random_int()
{
    return (int)(behavior_random_u32(active ? active : &unbound) >> 1);
}
double behavior_random_unit()
{
    return (double)behavior_random_u32(active ? active : &unbound) / 4294967296.0;
}
