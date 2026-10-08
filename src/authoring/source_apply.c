#include "authoring/source_apply.h"

#include "runtime/behaviors/source.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int authoring_source_apply(
    game_source_registry *world,
    const char *registration,
    const authoring_source *source,
    bool restart,
    char *error,
    size_t error_size
)
{
    assert(world && registration && source && error && error_size);
    if (!source->valid) {
        snprintf(
            error,
            error_size,
            "%s",
            source->diagnostic_count ? source->diagnostics[0].message : "Source generation is not executable"
        );
        return -1;
    }
    const char *name = source->analysis->name;
    if (strcmp(registration, name)) {
        snprintf(error, error_size, "Source declaration name differs from registration '%s'", registration);
        return -1;
    }
    if (game_source_registered(world, name)) {
        return game_source_publish(
            world,
            &(game_source_publication){ .name = name,
                                        .previous_name = name,
                                        .module = source->module,
                                        .restart = restart },
            1,
            NULL,
            error,
            error_size
        );
    }
    return game_source_publish(
        world,
        &(game_source_publication){ .name = name, .module = source->module },
        1,
        NULL,
        error,
        error_size
    );
}
