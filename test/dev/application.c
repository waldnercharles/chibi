#include "chibi/application.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static chibi_host allocator;
static void *create(const chibi_host *host, const char *text, char *error, size_t size)
{
    (void)error;
    (void)size;
    allocator = *host;
    int *state = host->allocate(sizeof *state);
    *state = text ? atoi(text) : 0;
    fprintf(stderr, "Fixture restored %d\n", *state);
    return state;
}
static char *checkpoint(void *state)
{
    char *text = allocator.allocate(32);
    snprintf(text, 32, "%d", *(int *)state);
    return text;
}
static void activate(void *state)
{
    (void)state;
}
static int frame(void *state)
{
    *(int *)state += 1;
    struct timespec delay = { .tv_nsec = 10000000 };
    nanosleep(&delay, NULL);
    return 0;
}
static void destroy(void *state)
{
    allocator.release(state);
}
CHIBI_APPLICATION_EXPORT const chibi_application_api *chibi_application(void)
{
    static const chibi_application_api api = { .abi = CHIBI_APPLICATION_ABI,
                                               .create = create,
                                               .checkpoint = checkpoint,
                                               .activate = activate,
                                               .deactivate = activate,
                                               .frame = frame,
                                               .destroy = destroy };
    return &api;
}
