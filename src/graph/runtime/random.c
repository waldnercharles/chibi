#include "graph/runtime/random.h"

#define PCG_MULTIPLIER UINT64_C(6364136223846793005)
#define PCG_INCREMENT UINT64_C(1442695040888963407)

static behavior_random process_stream = { UINT64_C(0x853c49e6748fea9b) };
static behavior_random *selected = &process_stream;

static uint32_t next(behavior_random *stream)
{
    uint64_t old = stream->state;
    stream->state = old * PCG_MULTIPLIER + PCG_INCREMENT;
    uint32_t shifted = (uint32_t)(((old >> 18) ^ old) >> 27);
    uint32_t rotation = (uint32_t)(old >> 59);
    return (shifted >> rotation) | (shifted << ((32 - rotation) & 31));
}

void behavior_random_seed(behavior_random *stream, uint64_t seed)
{
    stream->state = 0;
    next(stream);
    stream->state += seed;
    next(stream);
}

void behavior_random_use(behavior_random *stream)
{
    selected = stream ? stream : &process_stream;
}

void behavior_srand(uint64_t seed)
{
    behavior_random_seed(selected, seed);
}

int behavior_rand()
{
    return (int)(next(selected) >> 1);
}
