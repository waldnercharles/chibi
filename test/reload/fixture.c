#include <chibi/application.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef GENERATION
#define GENERATION 1
#endif
static chibi_host allocator;
static void *create(const chibi_host *host, const char *text, char *error, size_t size)
{
#ifdef REJECT_IMPORT
    snprintf(error, size, "candidate rejected checkpoint");
    return NULL;
#else
    allocator = *host;
    int *state = host->allocate(sizeof *state);
    *state = text ? atoi(text) : 0;
    return state;
#endif
}
static char *checkpoint(void *state)
{
#ifdef FAIL_CHECKPOINT
    if (*(int *)state == 1) {
        return NULL;
    }
#endif
    char *text = allocator.allocate(32);
    snprintf(text, 32, "%d", *(int *)state);
    return text;
}
static void activate(void *) {}
static void deactivate(void *) {}
static int frame(void *state)
{
    *(int *)state += GENERATION;
    return 0;
}
static void destroy(void *state)
{
    allocator.release(state);
}
CHIBI_APPLICATION_EXPORT const chibi_application_api *chibi_application()
{
    static const chibi_application_api api = {
#ifdef BAD_ABI
        .abi = CHIBI_APPLICATION_ABI + 1,
#else
        .abi = CHIBI_APPLICATION_ABI,
#endif
        .create = create,
        .checkpoint = checkpoint,
        .activate = activate,
        .deactivate = deactivate,
        .frame = frame,
        .destroy = destroy
    };
    return &api;
}
