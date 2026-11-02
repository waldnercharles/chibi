#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* This ordinary native game's public symbols deliberately match the compiler
 * module's former names. Linking and running both tests their isolation. */
typedef struct game_context game_context;
struct game_context
{
    void (*format)(game_context *, const void *, int32_t, void *);
    const void *failure;
    void *(*allocate)(game_context *, uint64_t);
    void (*index_failed)(game_context *, int32_t, int32_t);
    void (*release)(game_context *, void *);
};
extern int32_t chibi_scalar(game_context *);
extern void chibi_format_text(game_context *, const void *, int32_t, void *);
static unsigned steps;
static uint64_t remaining;
static bool bounded;
static unsigned char text[256];

unsigned wasm_game_steps(void)
{
    return steps;
}
int32_t chibi_native_scope_begin(uint64_t budget)
{
    remaining = budget;
    bounded = true;
    return 0;
}
void chibi_native_step(void)
{
    steps++;
    if (bounded) {
        assert(remaining);
        remaining--;
    }
}
_Noreturn void chibi_native_failure(int32_t kind)
{
    (void)kind;
    abort();
}
void *chibi_allocate(game_context *context, uint64_t size)
{
    (void)context;
    assert(size <= sizeof text);
    memset(text, 0, sizeof text);
    return text;
}
static void release(game_context *context, void *allocation)
{
    (void)context;
    assert(allocation == text);
}
static _Noreturn void index_failed(game_context *context, int32_t index, int32_t length)
{
    (void)context;
    (void)index;
    (void)length;
    abort();
}
int wasm_game_run(void)
{
    bounded = false;
    game_context context = { .format = chibi_format_text,
                             .allocate = chibi_allocate,
                             .release = release,
                             .index_failed = index_failed };
    int status = chibi_scalar(&context);
    assert(!context.failure && steps > 0);
    bounded = false;
    return status;
}
