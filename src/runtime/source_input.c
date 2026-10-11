#include "runtime/source_input.h"

#include <assert.h>
#include <stdio.h>

static bool has_event(void *context, const char *event)
{
    source_input *host = context;
    return source_has_event(host->sources, event);
}
static int result(source_input *host, int status, char *message, size_t size)
{
    if (status < 0) {
        snprintf(message, size, "%s", source_error(host->sources));
    }
    return status;
}
static int dispatch(
    void *context,
    ecs_entity entity,
    const char *event,
    execution_arguments arguments,
    char *message,
    size_t size
)
{
    source_input *host = context;
    return result(
        host,
        source_dispatch(
            host->sources,
            entity,
            event,
            arguments.values,
            arguments.count,
            host->now,
            host->delta
        ),
        message,
        size
    );
}
engine_input_host source_input_host(source_input *host)
{
    assert(host && host->sources);
    return (engine_input_host){ .context = host,
                                .has_event = has_event,
                                .dispatch = dispatch };
}
